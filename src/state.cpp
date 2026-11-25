/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
****************************************************************************/

// Implementation of the headless C64 / VIC-II State document model
// (vchar64_core).
//
// Implements VIC-II Multicolor bit-pair pixel transforms (reverseCharRowBits,
// 2-pass horizontal shifts, Color RAM bit-3 gating, square Hi-Res rotation
// guard), iterative BFS map flood-fill (collectFloodFillRegion),
// destination-stride clipboard pasting (_pasteMap), and QUndoStack command
// orchestration.

#include "state.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRandomGenerator>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTime>
#include <QUndoStack>
#include <QtGlobal>

#include "commands.h"
#include "core/documentimagerenderer.h"
#include "core/result.h"
#include "stateexport.h"
#include "stateimport.h"

namespace {

enum class ImportedFileType {
    kVChar64,
    kPrg,
    kRaw,
    kCtm,
};

struct StartupBanner {
    const char* text;
    size_t length;
};

constexpr std::array<QLatin1String, 3> kFeatureSuffixes = {
    QLatin1String("-charset"),
    QLatin1String("-map"),
    QLatin1String("-colors"),
};

constexpr std::array<uint8_t, 8> kMulticolorMasks = {
    192U, 192U, 48U, 48U, 12U, 12U, 3U, 3U
};

constexpr std::array<QPoint, 4> kFloodFillNeighbors = {
    QPoint { -1, 0 },
    QPoint { 1, 0 },
    QPoint { 0, -1 },
    QPoint { 0, 1 },
};

constexpr char kHello64[] = "                                        "
                            "    **** COMMODORE 64 BASIC V2 ****     "
                            "                                        "
                            " 64K RAM SYSTEM  38911 BASIC BYTES FREE ";

constexpr char kHello128[] = "                                        "
                             " COMMODORE BASIC V7.0 122365 BYTES FREE "
                             "   (C)1986 COMMODORE ELECTRONICS, LTD.  "
                             "         (C)1977 MICROSOFT CORP.        "
                             "           ALL RIGHTS RESERVED          ";

constexpr std::array<StartupBanner, 2> kStartupBanners = { {
    { kHello64, sizeof(kHello64) - 1U },
    { kHello128, sizeof(kHello128) - 1U },
} };

[[nodiscard]] size_t MapLinearIndex(int x, int y, int map_width) noexcept
{
    return static_cast<size_t>(y) * static_cast<size_t>(map_width) + static_cast<size_t>(x);
}

[[nodiscard]] bool IsValidTileProperties(const State::TileProperties& properties) noexcept
{
    const int tile_width = properties.size.width();
    const int tile_height = properties.size.height();
    return tile_width >= 1 && tile_width <= State::MAX_TILE_WIDTH
        && tile_height >= 1 && tile_height <= State::MAX_TILE_HEIGHT
        && (tile_width * tile_height) <= 256
        && properties.interleaved >= 1 && properties.interleaved <= 256;
}

[[nodiscard]] vchar64::Result<void> ToVoidResult(const vchar64::Result<qint64>& byte_result)
{
    if (!byte_result.isOk()) {
        return vchar64::Result<void>::err(byte_result.error().code, byte_result.error().detail);
    }
    return vchar64::Result<void>::ok();
}

// Writes one pixel (1 bit in Hi-Res/ECM, 2-bit pair in Multicolor) for `position`
// inside the tile rooted at `base_char`. Returns true if `_charset` changed.
bool WriteTilePixelPen(
    State::CharsetBuffer& charset,
    int base_char,
    const State::TileProperties& tile_props,
    const QPoint& position,
    int pen,
    bool is_multicolor,
    bool is_ecm) noexcept
{
    const int px_x = position.x();
    const int px_y = position.y();
    const int max_px_x = tile_props.size.width() * 8;
    const int max_px_y = tile_props.size.height() * 8;
    if (px_x < 0 || px_x >= max_px_x || px_y < 0 || px_y >= max_px_y) {
        return false;
    }

    const int bit_index = (px_x % 8) + (px_y % 8) * 8;
    const int char_index = base_char
        + (px_x / 8) * tile_props.interleaved
        + (px_y / 8) * tile_props.interleaved * tile_props.size.width();
    if (char_index < 0 || char_index >= 256) {
        return false;
    }

    const int physical_char = is_ecm
        ? static_cast<int>(static_cast<unsigned int>(char_index) & 0x3FU)
        : char_index;
    const size_t byte_index = static_cast<size_t>(physical_char) * 8U
        + static_cast<size_t>(bit_index / 8);
    const unsigned int bit_in_row = static_cast<unsigned int>(bit_index % 8);

    uint8_t and_mask = 0U;
    uint8_t or_mask = 0U;
    if (!is_multicolor) {
        const unsigned int shift = 7U - bit_in_row;
        // In 1bpp modes (Hi-Res and ECM), only PEN_FOREGROUND sets bit 1; all background pens clear bit 0.
        const unsigned int bit_val = (pen == State::PEN_FOREGROUND) ? 1U : 0U;
        and_mask = static_cast<uint8_t>(1U << shift);
        or_mask = static_cast<uint8_t>(bit_val << shift);
    } else {
        const unsigned int shift = (7U - bit_in_row) & 0xFEU;
        and_mask = kMulticolorMasks[bit_in_row];
        or_mask = static_cast<uint8_t>((static_cast<unsigned int>(pen) & 0x03U) << shift);
    }

    uint8_t updated = static_cast<uint8_t>(charset[byte_index] & static_cast<uint8_t>(~and_mask));
    updated = static_cast<uint8_t>(updated | or_mask);
    if (updated == charset[byte_index]) {
        return false;
    }

    charset[byte_index] = updated;
    return true;
}

} // namespace

State::State(const QString& filename, const QSize& map_size)
    : _mapSize(
          std::clamp(map_size.width(), 1, kMaxMapDim),
          std::clamp(map_size.height(), 1, kMaxMapDim))
    , _loadedFilename(filename)
    , _undoStack(std::make_unique<QUndoStack>())
{
    _undoStack->setUndoLimit(kDefaultUndoLimit);

    const size_t cell_count = static_cast<size_t>(_mapSize.width())
        * static_cast<size_t>(_mapSize.height());
    _map.assign(cell_count, 0x20U);
    setupDefaultMap();

    _charset.fill(0U);
    _tileColors.fill(TILE_COLORS_DEFAULT);
}

State::State(const QString& filename)
    : State(filename, QSize(40, 25))
{
}

State::State()
    : State(QString(), QSize(40, 25))
{
}

State::~State() = default;

void State::copyState(const State& other)
{
    _totalChars = other._totalChars;
    _mapSize = other._mapSize;
    _characterMode = other._characterMode;
    _foregroundColorMode = other._foregroundColorMode;
    _selectedPen = other._selectedPen;
    _tileProperties = other._tileProperties;
    _charIndex = other._charIndex;
    _tileIndex = other._tileIndex;
    _exportProperties = other._exportProperties;
    _keyboardMapping = other._keyboardMapping;

    _penColors = other._penColors;
    _charset = other._charset;
    _tileColors = other._tileColors;
    _map = other._map;
}

void State::reset()
{
    _totalChars = 0;
    _characterMode = CHARACTER_MODE_HIRES;
    _foregroundColorMode = FOREGROUND_COLOR_GLOBAL;
    _selectedPen = PEN_FOREGROUND;
    _penColors = { 1U, 5U, 7U, 11U, 6U };
    _tileProperties = { QSize(1, 1), 1 };
    _charIndex = 0;
    _tileIndex = 0;
    _loadedFilename = QStringLiteral("untitled");
    _savedFilename.clear();
    _exportedFilename.clear();
    _exportProperties = { { 0x3800U, 0x4000U, 0x4400U }, EXPORT_FORMAT_RAW, EXPORT_FEATURE_CHARSET };
    _keyboardMapping = KEYBOARD_MAPPING_C64;

    _charset.fill(0U);
    _tileColors.fill(TILE_COLORS_DEFAULT);
    std::fill(_map.begin(), _map.end(), uint8_t { 0U });
    _undoStack->clear();
}

void State::emitNewState()
{
    emit fileLoaded();
    emit contentsChanged();
}

vchar64::Result<vchar64::ImportReport> State::openFile(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open file '%1': %2").arg(filename, file.errorString()));
    }

    quint16 loaded_address = 0;
    ImportedFileType filetype = ImportedFileType::kRaw;

    const QFileInfo info(file);
    const QString suffix = info.suffix().toLower();

    vchar64::Result<vchar64::ImportReport> result = vchar64::Result<vchar64::ImportReport>::err(
        vchar64::ErrorCode::FileReadFailed);

    if (suffix == QStringLiteral("vchar64proj")) {
        filetype = ImportedFileType::kVChar64;
        result = StateImport::loadVChar64(*this, file);
    } else if (suffix == QStringLiteral("64c") || suffix == QStringLiteral("prg")) {
        filetype = ImportedFileType::kPrg;
        result = StateImport::loadPRG(*this, file, &loaded_address);
    } else if (suffix == QStringLiteral("ctm")) {
        filetype = ImportedFileType::kCtm;
        result = StateImport::loadCTM(*this, file);
    } else {
        filetype = ImportedFileType::kRaw;
        result = StateImport::loadRaw(*this, file);
        if (result.isOk() && suffix == QStringLiteral("fnt")) {
            _keyboardMapping = KEYBOARD_MAPPING_ATARI8;
        }
    }

    file.close();

    if (!result.isOk()) {
        return result;
    }

    // Built-in Qt resource files (":/...") do not bind save/export paths.
    if (!filename.startsWith(QLatin1Char(':'))) {
        _loadedFilename = filename;
        _savedFilename.clear();
        _exportedFilename.clear();

        if (filetype == ImportedFileType::kVChar64) {
            _savedFilename = filename;
        } else if (filetype == ImportedFileType::kRaw || filetype == ImportedFileType::kPrg) {
            _exportedFilename = filename;
            _exportProperties.features = EXPORT_FEATURE_CHARSET;
            if (filetype == ImportedFileType::kPrg) {
                _exportProperties.addresses[0] = loaded_address;
                _exportProperties.format = EXPORT_FORMAT_PRG;
            } else {
                _exportProperties.format = EXPORT_FORMAT_RAW;
            }
        }
    }

    _undoStack->clear();
    return result;
}

vchar64::Result<void> State::saveProject(const QString& filename)
{
    if (_undoStack->isClean() && _savedFilename == filename && QFileInfo::exists(filename)) {
        return vchar64::Result<void>::ok();
    }

    const vchar64::Result<void> save_result = ToVoidResult(StateExport::saveVChar64(*this, filename));
    if (!save_result.isOk()) {
        return save_result;
    }

    _loadedFilename = filename;
    _savedFilename = filename;
    _undoStack->setClean();
    emit contentsChanged();
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportRaw(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            exportCharsetByteCount(properties)));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size()));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size()));
        if (!status.isOk()) {
            return status;
        }
    }

    _exportedFilename = filename;
    ExportProperties updated_props = properties;
    updated_props.format = EXPORT_FORMAT_RAW;
    setExportProperties(updated_props);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportPRG(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            exportCharsetByteCount(properties),
            properties.addresses[0]));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            properties.addresses[1]));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            properties.addresses[2]));
        if (!status.isOk()) {
            return status;
        }
    }

    _exportedFilename = filename;
    ExportProperties updated_props = properties;
    updated_props.format = EXPORT_FORMAT_PRG;
    setExportProperties(updated_props);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportAsm(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            exportCharsetByteCount(properties),
            QStringLiteral("charset")));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            QStringLiteral("map")));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            QStringLiteral("colors")));
        if (!status.isOk()) {
            return status;
        }
    }

    _exportedFilename = filename;
    ExportProperties updated_props = properties;
    updated_props.format = EXPORT_FORMAT_ASM;
    setExportProperties(updated_props);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportC(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            exportCharsetByteCount(properties),
            QStringLiteral("charset")));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            QStringLiteral("map")));
        if (!status.isOk()) {
            return status;
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const vchar64::Result<void> status = ToVoidResult(StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            QStringLiteral("colors")));
        if (!status.isOk()) {
            return status;
        }
    }

    _exportedFilename = filename;
    ExportProperties updated_props = properties;
    updated_props.format = EXPORT_FORMAT_C;
    setExportProperties(updated_props);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportPNG(
    const QString& filename,
    bool export_tileset,
    bool export_map,
    bool transparent_background)
{
    if (!export_tileset && !export_map) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("No PNG export target selected"));
    }

    if (export_tileset) {
        const QImage tileset_image = DocumentImageRenderer::renderTileset(*this, transparent_background);
        const vchar64::Result<void> status = ToVoidResult(StateExport::savePNG(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            tileset_image));
        if (!status.isOk()) {
            return status;
        }
    }

    if (export_map) {
        const QImage map_image = DocumentImageRenderer::renderMap(*this, transparent_background);
        const vchar64::Result<void> status = ToVoidResult(StateExport::savePNG(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            map_image));
        if (!status.isOk()) {
            return status;
        }
    }

    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::export_()
{
    if (_exportedFilename.isEmpty()) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("No export filename configured"));
    }

    switch (_exportProperties.format) {
    case EXPORT_FORMAT_RAW:
        return exportRaw(_exportedFilename, _exportProperties);
    case EXPORT_FORMAT_PRG:
        return exportPRG(_exportedFilename, _exportProperties);
    case EXPORT_FORMAT_ASM:
        return exportAsm(_exportedFilename, _exportProperties);
    case EXPORT_FORMAT_C:
        return exportC(_exportedFilename, _exportProperties);
    }

    return vchar64::Result<void>::err(
        vchar64::ErrorCode::UnsupportedVersion,
        QStringLiteral("Unknown export format"));
}

QString State::filenameFixSuffix(const QString& filename, ExportFeature suffix)
{
    const QFileInfo info(filename);
    const QString extension = info.suffix();
    const QString directory_path = info.path();
    QString base_name = info.completeBaseName();

    for (const QLatin1String known_suffix : kFeatureSuffixes) {
        if (base_name.endsWith(known_suffix)) {
            base_name.chop(known_suffix.size());
            break;
        }
    }

    if (suffix == EXPORT_FEATURE_CHARSET) {
        base_name.append(kFeatureSuffixes[0]);
    } else if (suffix == EXPORT_FEATURE_MAP) {
        base_name.append(kFeatureSuffixes[1]);
    } else {
        base_name.append(kFeatureSuffixes[2]);
    }

    const QString prefix = (directory_path == QStringLiteral("/"))
        ? QStringLiteral("/")
        : (directory_path + QLatin1Char('/'));
    if (extension.isEmpty()) {
        return prefix + base_name;
    }
    return prefix + base_name + QLatin1Char('.') + extension;
}

int State::getColorForPen(int pen, int tile_index) const
{
    if (pen < 0 || pen >= PEN_MAX) {
        return 0;
    }

    const bool is_per_tile_foreground = (pen == PEN_FOREGROUND
        && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);
    if (is_per_tile_foreground) {
        const int resolved_index = (tile_index >= 0 && tile_index < TILE_COLORS_BUFFER_SIZE)
            ? tile_index
            : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
        return static_cast<int>(_tileColors[static_cast<size_t>(resolved_index)] & 0x0FU);
    }

    return static_cast<int>(_penColors[static_cast<size_t>(pen)] & 0x0FU);
}

int State::getColorForPen(int pen) const
{
    return getColorForPen(pen, -1);
}

int State::getGlobalColorForPen(int pen) const noexcept
{
    if (pen < 0 || pen >= PEN_MAX) {
        return 0;
    }
    return static_cast<int>(_penColors[static_cast<size_t>(pen)] & 0x0FU);
}

void State::setColorForPen(int pen, int color, int tile_index)
{
    if (pen < 0 || pen >= PEN_MAX) {
        return;
    }
    const int masked_color = static_cast<int>(static_cast<unsigned int>(color) & 0x0FU);
    if (getColorForPen(pen, tile_index) == masked_color) {
        return;
    }
    _undoStack->push(new SetColorCommand(this, masked_color, pen, tile_index));
}

void State::setColorForPen(int pen, int color)
{
    setColorForPen(pen, color, -1);
}

int State::getCurrentColor() const
{
    return getColorForPen(_selectedPen, _tileIndex);
}

void State::setSelectedPen(int pen)
{
    if (pen < 0 || pen >= PEN_MAX || pen == _selectedPen) {
        return;
    }
    _selectedPen = pen;
    emit selectedPenChanged(pen);
}

bool State::isBackgroundPen(int pen) const noexcept
{
    if (pen < 0 || pen >= PEN_MAX) {
        return false;
    }
    if (_characterMode == CHARACTER_MODE_MULTICOLOR) {
        return pen == PEN_BACKGROUND;
    }
    return pen != PEN_FOREGROUND;
}

int State::getBackgroundColorForChar(int char_index) const noexcept
{
    if (_characterMode == CHARACTER_MODE_EXTENDED_COLOR) {
        const int clamped = std::clamp(char_index, 0, 255);
        const int bank = static_cast<int>((static_cast<unsigned int>(clamped) >> 6U) & 0x03U);
        return getGlobalColorForPen(penForExtendedBgBank(bank));
    }
    return getGlobalColorForPen(PEN_BACKGROUND);
}

void State::setCharacterMode(CharacterMode mode)
{
    if (_characterMode == mode) {
        return;
    }
    _undoStack->push(new SetCharacterModeCommand(this, mode));
}

void State::setMulticolorMode(bool enabled)
{
    const CharacterMode target = enabled ? CHARACTER_MODE_MULTICOLOR : CHARACTER_MODE_HIRES;
    if (_characterMode == target) {
        return;
    }
    _undoStack->push(new SetMulticolorModeCommand(this, enabled));
}

void State::setForegroundColorMode(ForegroundColorMode mode)
{
    _undoStack->push(new SetForegroundColorMode(this, mode));
}

bool State::shouldBeDisplayedInMulticolor() const noexcept
{
    return (_characterMode == CHARACTER_MODE_MULTICOLOR)
        && ((_penColors[PEN_FOREGROUND] & 0x0FU) >= 8U);
}

bool State::shouldBeDisplayedInMulticolor2(int tile_index) const noexcept
{
    if (_characterMode != CHARACTER_MODE_MULTICOLOR) {
        return false;
    }
    if (_foregroundColorMode == FOREGROUND_COLOR_GLOBAL) {
        return (_penColors[PEN_FOREGROUND] & 0x0FU) >= 8U;
    }
    const int resolved_index = (tile_index >= 0 && tile_index < TILE_COLORS_BUFFER_SIZE)
        ? tile_index
        : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
    return (_tileColors[static_cast<size_t>(resolved_index)] & 0x0FU) >= 8U;
}

void State::setTileProperties(const TileProperties& properties)
{
    if (!IsValidTileProperties(properties) || properties == _tileProperties) {
        return;
    }
    _undoStack->push(new SetTilePropertiesCommand(this, properties));
}

void State::setExportProperties(const ExportProperties& properties)
{
    if (_exportProperties != properties) {
        _undoStack->push(new SetExportPropertiesCommand(this, properties));
    }
}

void State::setMapSize(const QSize& map_size)
{
    if (map_size.width() < 1 || map_size.width() > kMaxMapDim
        || map_size.height() < 1 || map_size.height() > kMaxMapDim
        || map_size == _mapSize) {
        return;
    }
    _undoStack->push(new SetMapSizeCommand(this, map_size));
}

bool State::isValidMapCoord(const QPoint& coord) const noexcept
{
    return coord.x() >= 0
        && coord.x() < _mapSize.width()
        && coord.y() >= 0
        && coord.y() < _mapSize.height();
}

int State::getTileIndexFromMap(const QPoint& map_coord) const
{
    if (!isValidMapCoord(map_coord)) {
        return 0;
    }
    const size_t cell_index = MapLinearIndex(map_coord.x(), map_coord.y(), _mapSize.width());
    const int raw_tile = static_cast<int>(_map[cell_index]);
    return std::clamp(raw_tile, 0, maxTiles() - 1);
}

State::FloodFillPlan State::collectFloodFillRegion(
    const QPoint& start_coord,
    uint8_t target_tile,
    uint8_t replacement_tile) const
{
    FloodFillPlan plan;
    if (target_tile == replacement_tile || !isValidMapCoord(start_coord) || _map.empty()) {
        return plan;
    }

    const int map_width = _mapSize.width();
    const size_t start_index = MapLinearIndex(start_coord.x(), start_coord.y(), map_width);
    if (start_index >= _map.size() || _map[start_index] != target_tile) {
        return plan;
    }

    std::vector<uint8_t> visited(_map.size(), 0U);
    std::vector<QPoint> worklist;
    worklist.reserve(256);

    visited[start_index] = 1U;
    worklist.push_back(start_coord);

    int min_x = start_coord.x();
    int max_x = start_coord.x();
    int min_y = start_coord.y();
    int max_y = start_coord.y();

    while (!worklist.empty()) {
        const QPoint coord = worklist.back();
        worklist.pop_back();
        plan.coords.push_back(coord);

        min_x = std::min(min_x, coord.x());
        max_x = std::max(max_x, coord.x());
        min_y = std::min(min_y, coord.y());
        max_y = std::max(max_y, coord.y());

        for (const QPoint& delta : kFloodFillNeighbors) {
            const int next_x = coord.x() + delta.x();
            const int next_y = coord.y() + delta.y();
            const QPoint next_coord(next_x, next_y);
            if (!isValidMapCoord(next_coord)) {
                continue;
            }
            const size_t next_index = MapLinearIndex(next_x, next_y, map_width);
            if (visited[next_index] != 0U || _map[next_index] != target_tile) {
                continue;
            }
            visited[next_index] = 1U;
            worklist.push_back(next_coord);
        }
    }

    plan.bounds = QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
    return plan;
}

void State::mapFill(const QPoint& coord, int tile_index)
{
    if (!isValidMapCoord(coord)) {
        return;
    }
    const uint8_t replacement = static_cast<uint8_t>(static_cast<unsigned int>(tile_index) & 0xFFU);
    const size_t start_index = MapLinearIndex(coord.x(), coord.y(), _mapSize.width());
    const uint8_t target = _map[start_index];

    FloodFillPlan plan = collectFloodFillRegion(coord, target, replacement);
    if (plan.coords.empty()) {
        return;
    }

    _undoStack->push(new FillMapCommand(this, std::move(plan), replacement));
}

void State::mapPaint(const QPoint& coord, int tile_index, bool mergeable)
{
    if (!isValidMapCoord(coord)) {
        return;
    }
    _undoStack->push(new PaintMapCommand(this, coord, tile_index, mergeable));
}

void State::mapClear(int tile_index)
{
    _undoStack->push(new ClearMapCommand(this, tile_index));
}

bool State::isModified() const
{
    return !_undoStack->isClean();
}

void State::undo()
{
    _undoStack->undo();
}

void State::redo()
{
    _undoStack->redo();
}

void State::clearUndoStack()
{
    _undoStack->clear();
}

int State::maxTiles() const noexcept
{
    const int area = std::max(1, _tileProperties.size.width() * _tileProperties.size.height());
    return std::max(1, 256 / area);
}

bool State::isValidTileIndex(int tile_index) const noexcept
{
    return tile_index >= 0 && tile_index < maxTiles();
}

int State::getCharIndexFromTileIndex(int tile_index) const noexcept
{
    if (_tileProperties.interleaved == 1) {
        return tile_index * (_tileProperties.size.width() * _tileProperties.size.height());
    }
    return tile_index;
}

int State::getTileIndexFromCharIndex(int char_index) const noexcept
{
    const int clamped_char = std::clamp(char_index, 0, 255);
    if (_tileProperties.interleaved == 1) {
        const int area = std::max(1, _tileProperties.size.width() * _tileProperties.size.height());
        return clamped_char / area;
    }
    const int step = std::max(1, _tileProperties.interleaved);
    return clamped_char % step;
}

int State::effectiveCharIndex(int char_index) const noexcept
{
    const int clamped = std::clamp(char_index, 0, 255);
    if (_characterMode == CHARACTER_MODE_EXTENDED_COLOR) {
        return static_cast<int>(static_cast<unsigned int>(clamped) & 0x3FU);
    }
    return clamped;
}

bool State::hasAliasedSubChars(int tile_index) const noexcept
{
    if (_characterMode != CHARACTER_MODE_EXTENDED_COLOR || !isValidTileIndex(tile_index)) {
        return false;
    }
    // Contiguous tiles (interleaved == 1) span at most 64 consecutive glyphs,
    // which are strictly distinct modulo 64 even across bank boundaries.
    if (_tileProperties.interleaved == 1) {
        return false;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_area = _tileProperties.size.width() * _tileProperties.size.height();
    std::bitset<ECM_CHAR_COUNT> seen;
    for (int i = 0; i < tile_area; ++i) {
        const int char_index = base_char + i * _tileProperties.interleaved;
        if (char_index < 0 || char_index >= 256) {
            continue;
        }
        const size_t phys = static_cast<size_t>(effectiveCharIndex(char_index));
        if (seen.test(phys)) {
            return true;
        }
        seen.set(phys);
    }
    return false;
}

const uint8_t* State::getCharAtIndex(int char_index) const
{
    const int effective = effectiveCharIndex(char_index);
    return _charset.data() + static_cast<size_t>(effective) * 8U;
}

void State::copyTileFromIndex(int tile_index, uint8_t* buffer, int buffer_size) const
{
    if (buffer == nullptr || buffer_size <= 0 || !isValidTileIndex(tile_index)) {
        return;
    }

    const int tile_area = _tileProperties.size.width() * _tileProperties.size.height();
    if (buffer_size < tile_area * 8) {
        return;
    }

    // Bypass the contiguous block copy in ECM because tiles crossing a 64-glyph
    // bank boundary wrap around physical glyph slots.
    if (!isExtendedColorMode() && _tileProperties.interleaved == 1) {
        const size_t src_base = static_cast<size_t>(tile_index * tile_area * 8);
        const size_t bytes_to_copy = std::min(
            static_cast<size_t>(tile_area * 8),
            _charset.size() - std::min(src_base, _charset.size()));
        std::copy_n(_charset.data() + src_base, bytes_to_copy, buffer);
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    for (int i = 0; i < tile_area; ++i) {
        const int char_index = base_char + i * _tileProperties.interleaved;
        if (char_index >= 0 && char_index < 256) {
            const int effective_char = effectiveCharIndex(char_index);
            std::memcpy(
                buffer + static_cast<size_t>(i) * 8U,
                _charset.data() + static_cast<size_t>(effective_char) * 8U,
                8U);
        }
    }
}

void State::copyTileToIndex(int tile_index, const uint8_t* buffer, int buffer_size)
{
    if (buffer == nullptr || buffer_size <= 0 || !isValidTileIndex(tile_index)) {
        return;
    }

    const int tile_area = _tileProperties.size.width() * _tileProperties.size.height();
    if (buffer_size < tile_area * 8) {
        return;
    }

    // Bypass the contiguous block copy in ECM because tiles crossing a 64-glyph
    // bank boundary wrap around physical glyph slots.
    if (!isExtendedColorMode() && _tileProperties.interleaved == 1) {
        const size_t dst_base = static_cast<size_t>(tile_index * tile_area * 8);
        const size_t bytes_to_copy = std::min(
            static_cast<size_t>(tile_area * 8),
            _charset.size() - std::min(dst_base, _charset.size()));
        std::copy_n(buffer, bytes_to_copy, _charset.data() + dst_base);
    } else {
        const int base_char = getCharIndexFromTileIndex(tile_index);
        for (int i = 0; i < tile_area; ++i) {
            const int char_index = base_char + i * _tileProperties.interleaved;
            if (char_index >= 0 && char_index < 256) {
                const int effective_char = effectiveCharIndex(char_index);
                std::memcpy(
                    _charset.data() + static_cast<size_t>(effective_char) * 8U,
                    buffer + static_cast<size_t>(i) * 8U,
                    8U);
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::resetCharsetBuffer() noexcept
{
    _charset.fill(0U);
}

bool State::assignImportedBuffers(
    const CharsetBuffer& charset_buf,
    const TileColorsBuffer& tile_colors_buf,
    const QSize& new_map_size,
    std::vector<uint8_t> map_cells)
{
    if (!replaceMap(new_map_size, std::move(map_cells))) {
        return false;
    }
    _charset = charset_buf;
    for (size_t i = 0; i < _tileColors.size(); ++i) {
        _tileColors[i] = static_cast<uint8_t>(tile_colors_buf[i] & 0x0FU);
    }
    return true;
}

bool State::replaceMap(const QSize& size, std::vector<uint8_t> cells)
{
    const int map_width = size.width();
    const int map_height = size.height();
    if (map_width < 1 || map_width > kMaxMapDim || map_height < 1 || map_height > kMaxMapDim) {
        return false;
    }
    const size_t expected_cells = static_cast<size_t>(map_width) * static_cast<size_t>(map_height);
    if (expected_cells > kMaxMapCells || cells.size() != expected_cells) {
        return false;
    }

    _mapSize = size;
    _map = std::move(cells);
    return true;
}

void State::setCharsetBytes(const uint8_t* data, size_t byte_count) noexcept
{
    _charset.fill(0U);
    if (data != nullptr && byte_count > 0U) {
        const size_t bytes_to_copy = std::min(byte_count, _charset.size());
        std::copy_n(data, bytes_to_copy, _charset.data());
    }
}

void State::setTileColorsDirect(const uint8_t* data, size_t count) noexcept
{
    _tileColors.fill(TILE_COLORS_DEFAULT);
    if (data == nullptr || count == 0U) {
        return;
    }
    const size_t entries_to_copy = std::min(count, _tileColors.size());
    for (size_t i = 0; i < entries_to_copy; ++i) {
        _tileColors[i] = static_cast<uint8_t>(data[i] & 0x0FU);
    }
}

void State::setTileColorDirect(int tile_index, uint8_t color) noexcept
{
    if (tile_index >= 0 && tile_index < TILE_COLORS_BUFFER_SIZE) {
        _tileColors[static_cast<size_t>(tile_index)] = static_cast<uint8_t>(color & 0x0FU);
    }
}

void State::setPenColorDirect(int pen, uint8_t color) noexcept
{
    if (pen >= 0 && pen < PEN_MAX) {
        _penColors[static_cast<size_t>(pen)] = static_cast<uint8_t>(color & 0x0FU);
    }
}

bool State::setTilePropertiesDirect(const TileProperties& properties) noexcept
{
    if (!IsValidTileProperties(properties)) {
        return false;
    }

    _tileProperties = properties;
    const int limit = maxTiles();
    if (_tileIndex >= limit) {
        _tileIndex = limit - 1;
        _charIndex = std::clamp(getCharIndexFromTileIndex(_tileIndex), 0, 255);
    }
    return true;
}

void State::paste(int offset, const CopyRange& copy_range, const uint8_t* orig_buffer)
{
    if (orig_buffer == nullptr || copy_range.count <= 0 || copy_range.blockSize <= 0) {
        return;
    }
    if (copy_range.type == CopyRange::TILES && copy_range.tileProperties.size != _tileProperties.size) {
        qWarning() << "Cannot paste tiles with mismatched dimensions:"
                   << copy_range.tileProperties.size << "vs" << _tileProperties.size;
        return;
    }
    _undoStack->push(new PasteCommand(this, offset, copy_range, orig_buffer));
}

void State::cut(const CopyRange& copy_range)
{
    if (copy_range.count <= 0 || copy_range.blockSize <= 0) {
        return;
    }
    _undoStack->push(new CutCommand(this, copy_range));
}

bool State::canRotateTile(int tile_index) const noexcept
{
    if (!isValidTileIndex(tile_index)) {
        return false;
    }
    // 90-degree in-place rotation requires an N x N character grid.
    if (_tileProperties.size.width() != _tileProperties.size.height()) {
        return false;
    }
    // VIC-II Multicolor glyphs are 4x8 double-wide pixels (2:1 aspect ratio);
    // rotating 4x8 into 8x4 cannot map losslessly back onto 2-bit pixel pairs.
    if (shouldBeDisplayedInMulticolor2(tile_index)) {
        return false;
    }
    // In ECM, tiles whose sub-characters alias the same physical glyph cannot
    // be rotated or undone losslessly in place.
    if (hasAliasedSubChars(tile_index)) {
        return false;
    }
    return true;
}

uint8_t State::reverseCharRowBits(uint8_t row_byte, bool is_multicolor_tile) noexcept
{
    if (is_multicolor_tile) {
        // Swap 2-bit pixel pairs [7:6]<->[1:0] and [5:4]<->[3:2] intact so
        // Multicolor pen codes 01 ($D022) and 10 ($D023) never transpose.
        return static_cast<uint8_t>(
            ((row_byte & 0x03U) << 6U)
            | ((row_byte & 0x0CU) << 2U)
            | ((row_byte & 0x30U) >> 2U)
            | ((row_byte & 0xC0U) >> 6U));
    }

    uint8_t reversed = 0U;
    for (unsigned int bit = 0U; bit < 8U; ++bit) {
        reversed = static_cast<uint8_t>(
            reversed | (((static_cast<unsigned int>(row_byte) >> bit) & 0x01U) << (7U - bit)));
    }
    return reversed;
}

void State::tilePaint(int tile_index, const QPoint& point, int pen, bool mergeable)
{
    if (!isValidTileIndex(tile_index)) {
        return;
    }
    _undoStack->push(new PaintTileCommand(this, tile_index, point, pen, mergeable));
}

void State::tileInvert(int tile_index)
{
    if (!isValidTileIndex(tile_index)) {
        return;
    }
    _undoStack->push(new InvertTileCommand(this, tile_index));
}

void State::tileClear(int tile_index)
{
    if (!isValidTileIndex(tile_index)) {
        return;
    }
    _undoStack->push(new ClearTileCommand(this, tile_index));
}

void State::tileFlipHorizontally(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new FlipTileHCommand(this, tile_index));
}

void State::tileFlipVertically(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new FlipTileVCommand(this, tile_index));
}

void State::tileRotate(int tile_index)
{
    if (!canRotateTile(tile_index)) {
        return;
    }
    _undoStack->push(new RotateTileCommand(this, tile_index));
}

void State::tileShiftLeft(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new ShiftLeftTileCommand(this, tile_index));
}

void State::tileShiftRight(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new ShiftRightTileCommand(this, tile_index));
}

void State::tileShiftUp(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new ShiftUpTileCommand(this, tile_index));
}

void State::tileShiftDown(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }
    _undoStack->push(new ShiftDownTileCommand(this, tile_index));
}

int State::tileGetPen(int tile_index, const QPoint& position) const
{
    if (!isValidTileIndex(tile_index)) {
        return 0;
    }

    const int px_x = position.x();
    const int px_y = position.y();
    const int max_px_x = _tileProperties.size.width() * 8;
    const int max_px_y = _tileProperties.size.height() * 8;
    if (px_x < 0 || px_x >= max_px_x || px_y < 0 || px_y >= max_px_y) {
        return 0;
    }

    const int bit_index = (px_x % 8) + (px_y % 8) * 8;
    const int char_index = getCharIndexFromTileIndex(tile_index)
        + (px_x / 8) * _tileProperties.interleaved
        + (px_y / 8) * _tileProperties.interleaved * _tileProperties.size.width();
    if (char_index < 0 || char_index >= 256) {
        return 0;
    }

    const int effective_char = effectiveCharIndex(char_index);
    const uint8_t row_byte = _charset[static_cast<size_t>(effective_char) * 8U
        + static_cast<size_t>(bit_index / 8)];
    const unsigned int bit_in_row = static_cast<unsigned int>(bit_index % 8);

    if (!shouldBeDisplayedInMulticolor2(tile_index)) {
        const unsigned int shift = 7U - bit_in_row;
        return static_cast<int>((row_byte >> shift) & 0x01U);
    }

    const unsigned int shift = (7U - bit_in_row) & 0xFEU;
    return static_cast<int>((row_byte & kMulticolorMasks[bit_in_row]) >> shift);
}

State::MapRegionPatch State::captureMapPatch(const QRect& bounds) const
{
    MapRegionPatch patch;
    const QRect map_bounds(0, 0, _mapSize.width(), _mapSize.height());
    const QRect clipped = bounds.intersected(map_bounds);
    if (clipped.isEmpty()) {
        return patch;
    }

    patch.bounds = clipped;
    const size_t patch_width = static_cast<size_t>(clipped.width());
    const size_t patch_height = static_cast<size_t>(clipped.height());
    patch.cells.resize(patch_width * patch_height);

    const size_t map_width = static_cast<size_t>(_mapSize.width());
    for (int row = 0; row < clipped.height(); ++row) {
        const size_t src_offset = static_cast<size_t>(clipped.top() + row) * map_width
            + static_cast<size_t>(clipped.left());
        const size_t dst_offset = static_cast<size_t>(row) * patch_width;
        std::copy_n(_map.data() + src_offset, patch_width, patch.cells.data() + dst_offset);
    }
    return patch;
}

void State::restoreMapPatch(const MapRegionPatch& patch)
{
    if (patch.bounds.isEmpty()) {
        return;
    }
    const QRect map_bounds(0, 0, _mapSize.width(), _mapSize.height());
    if (!map_bounds.contains(patch.bounds)) {
        return;
    }
    const size_t patch_width = static_cast<size_t>(patch.bounds.width());
    const size_t patch_height = static_cast<size_t>(patch.bounds.height());
    if (patch.cells.size() != patch_width * patch_height) {
        return;
    }

    const size_t map_width = static_cast<size_t>(_mapSize.width());
    for (int row = 0; row < patch.bounds.height(); ++row) {
        const size_t src_offset = static_cast<size_t>(row) * patch_width;
        const size_t dst_offset = static_cast<size_t>(patch.bounds.top() + row) * map_width
            + static_cast<size_t>(patch.bounds.left());
        std::copy_n(patch.cells.data() + src_offset, patch_width, _map.data() + dst_offset);
    }

    emit mapContentUpdated();
    emit contentsChanged();
}

void State::restoreCharsetAndColors(
    const CharsetBuffer& charset_buf,
    const TileColorsBuffer& tile_colors_buf)
{
    _charset = charset_buf;
    for (size_t i = 0; i < _tileColors.size(); ++i) {
        _tileColors[i] = static_cast<uint8_t>(tile_colors_buf[i] & 0x0FU);
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    emit multicolorModeToggled(shouldBeDisplayedInMulticolor2(_tileIndex));
    emit contentsChanged();
}

void State::setCharIndex(int char_index)
{
    if (char_index < 0 || char_index >= 256 || _charIndex == char_index) {
        return;
    }
    _setCharIndex(char_index);
    const int tile_index = getTileIndexFromCharIndex(char_index);
    _setTileIndex(tile_index);
}

void State::setTileIndex(int tile_index)
{
    if (!isValidTileIndex(tile_index) || _tileIndex == tile_index) {
        return;
    }
    _setTileIndex(tile_index);
    const int char_index = getCharIndexFromTileIndex(tile_index);
    _setCharIndex(char_index);
}

uint8_t* State::mutableCharAtIndex(int char_index)
{
    const int effective = effectiveCharIndex(char_index);
    return _charset.data() + static_cast<size_t>(effective) * 8U;
}

State::CharGlyph State::getCharFromTile(int tile_index, int x, int y) const
{
    CharGlyph glyph { };
    const int char_index = getCharIndexFromTileIndex(tile_index)
        + (x + y * _tileProperties.size.width()) * _tileProperties.interleaved;
    if (char_index < 0 || char_index >= 256) {
        return glyph;
    }

    const size_t base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
    for (size_t row = 0U; row < 8U; ++row) {
        glyph[row] = _charset[base + row];
    }
    return glyph;
}

void State::setCharForTile(int tile_index, int x, int y, const CharGlyph& glyph)
{
    const int char_index = getCharIndexFromTileIndex(tile_index)
        + (x + y * _tileProperties.size.width()) * _tileProperties.interleaved;
    if (char_index < 0 || char_index >= 256) {
        return;
    }

    const size_t base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
    for (size_t row = 0U; row < 8U; ++row) {
        _charset[base + row] = glyph[row];
    }
}

void State::setupDefaultMap()
{
    std::fill(_map.begin(), _map.end(), uint8_t { 0x20U });

    QRandomGenerator prng(static_cast<quint32>(QTime::currentTime().msec()));
    const size_t banner_index = static_cast<size_t>(prng.generate() % 2U);
    const size_t copy_length = std::min(kStartupBanners[banner_index].length, _map.size());

    // Convert ASCII banner characters to C64 screen codes.
    for (size_t i = 0U; i < copy_length; ++i) {
        const uint8_t ascii_byte = static_cast<uint8_t>(kStartupBanners[banner_index].text[i]);
        _map[i] = static_cast<uint8_t>(ascii_byte & static_cast<uint8_t>(~0x40U));
    }
}

void State::_setCharIndex(int char_index)
{
    if (_charIndex != char_index) {
        _charIndex = char_index;
        emit charIndexUpdated(_charIndex);
    }
}

void State::_setTileIndex(int tile_index)
{
    if (_tileIndex == tile_index || tile_index < 0 || tile_index >= TILE_COLORS_BUFFER_SIZE) {
        return;
    }
    const int old_tile_index = std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
    _tileIndex = tile_index;

    if (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE
        && _tileColors[static_cast<size_t>(tile_index)]
            != _tileColors[static_cast<size_t>(old_tile_index)]) {
        emit colorPropertiesUpdated(PEN_FOREGROUND);
    }

    emit tileIndexUpdated(tile_index);
}

void State::_pasteChars(int char_index, const CopyRange& copy_range, const uint8_t* orig_buffer)
{
    if (orig_buffer == nullptr || char_index < 0 || char_index >= 256
        || copy_range.offset < 0 || copy_range.offset >= 256
        || copy_range.blockSize <= 0 || copy_range.count <= 0) {
        return;
    }

    const bool was_multicolor = shouldBeDisplayedInMulticolor2(_tileIndex);
    const uint8_t* colors_buffer = orig_buffer + CHAR_BUFFER_SIZE;
    const int stride = copy_range.blockSize + std::max(0, copy_range.skip);

    int dst_char = char_index;
    int src_char = copy_range.offset;
    int remaining = copy_range.count;

    while (remaining > 0 && dst_char < 256 && src_char < 256) {
        const int chars_to_copy = std::min({ copy_range.blockSize, 256 - dst_char, 256 - src_char });
        if (chars_to_copy <= 0) {
            break;
        }
        if (isExtendedColorMode()) {
            for (int col = 0; col < chars_to_copy; ++col) {
                const int effective_dst = effectiveCharIndex(dst_char + col);
                std::memcpy(
                    _charset.data() + static_cast<size_t>(effective_dst) * 8U,
                    orig_buffer + static_cast<size_t>(src_char + col) * 8U,
                    8U);
                _tileColors[static_cast<size_t>(dst_char + col)] = static_cast<uint8_t>(
                    colors_buffer[static_cast<size_t>(src_char + col)] & 0x0FU);
                emit bytesUpdated(effective_dst * 8, 8);
            }
        } else {
            const int bytes_to_copy = chars_to_copy * 8;
            std::memcpy(
                _charset.data() + static_cast<size_t>(dst_char) * 8U,
                orig_buffer + static_cast<size_t>(src_char) * 8U,
                static_cast<size_t>(bytes_to_copy));

            for (int col = 0; col < chars_to_copy; ++col) {
                _tileColors[static_cast<size_t>(dst_char + col)] = static_cast<uint8_t>(
                    colors_buffer[static_cast<size_t>(src_char + col)] & 0x0FU);
            }

            emit bytesUpdated(dst_char * 8, bytes_to_copy);
        }

        dst_char += stride;
        src_char += stride;
        --remaining;
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    const bool is_multicolor_now = shouldBeDisplayedInMulticolor2(_tileIndex);
    if (was_multicolor != is_multicolor_now) {
        emit multicolorModeToggled(is_multicolor_now);
    }
}

void State::_pasteTiles(int char_index, const CopyRange& copy_range, const uint8_t* orig_buffer)
{
    if (orig_buffer == nullptr || char_index < 0 || char_index >= 256
        || copy_range.offset < 0 || copy_range.blockSize <= 0 || copy_range.count <= 0) {
        return;
    }

    if (copy_range.tileProperties.size != _tileProperties.size) {
        qWarning() << "Tile paste skipped due to size mismatch:"
                   << copy_range.tileProperties.size << "vs" << _tileProperties.size;
        return;
    }

    const bool was_multicolor = shouldBeDisplayedInMulticolor2(_tileIndex);
    const uint8_t* colors_buffer = orig_buffer + CHAR_BUFFER_SIZE;

    const int tile_area = copy_range.tileProperties.size.width() * copy_range.tileProperties.size.height();
    if (tile_area <= 0 || tile_area > 256) {
        return;
    }

    const int tile_src_start = copy_range.offset;
    const int tile_dst_start = getTileIndexFromCharIndex(char_index);
    const int src_interleave_factor = (copy_range.tileProperties.interleaved == 1) ? tile_area : 1;
    const int dst_interleave_factor = (_tileProperties.interleaved == 1) ? tile_area : 1;
    int src_skip = 0;
    int dst_skip = 0;
    int remaining = copy_range.count;

    while (remaining > 0) {
        for (int i = 0; i < copy_range.blockSize; ++i) {
            const int src_tile = tile_src_start + i + src_skip;
            const int dst_tile = tile_dst_start + i + dst_skip;
            if (src_tile < 0 || src_tile >= 256 || dst_tile < 0 || dst_tile >= 256) {
                break;
            }

            const int src_base_char = src_tile * src_interleave_factor;
            const int dst_base_char = dst_tile * dst_interleave_factor;

            _tileColors[static_cast<size_t>(dst_tile)] = static_cast<uint8_t>(
                colors_buffer[static_cast<size_t>(src_tile)] & 0x0FU);

            if (_tileProperties.interleaved != 1 && dst_base_char >= (256 / tile_area)) {
                remaining = 0;
                break;
            }

            for (int j = 0; j < tile_area; ++j) {
                const int src_char_idx = src_base_char + j * copy_range.tileProperties.interleaved;
                const int dst_char_idx = dst_base_char + j * _tileProperties.interleaved;
                if (src_char_idx >= 0 && src_char_idx < 256
                    && dst_char_idx >= 0 && dst_char_idx < 256) {
                    const int char_src_byte = src_char_idx * 8;
                    const int char_dst_byte = effectiveCharIndex(dst_char_idx) * 8;
                    std::memcpy(
                        _charset.data() + static_cast<size_t>(char_dst_byte),
                        orig_buffer + static_cast<size_t>(char_src_byte),
                        8U);
                    emit bytesUpdated(char_dst_byte, 8);
                }
            }
        }
        src_skip += std::max(0, copy_range.skip) + copy_range.blockSize;
        dst_skip += std::max(0, copy_range.skip) + copy_range.blockSize;
        --remaining;
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    const bool is_multicolor_now = shouldBeDisplayedInMulticolor2(_tileIndex);
    if (was_multicolor != is_multicolor_now) {
        emit multicolorModeToggled(is_multicolor_now);
    }
}

void State::_pasteMap(int dst_offset, const CopyRange& copy_range, const uint8_t* orig_buffer)
{
    const int dst_width = _mapSize.width();
    const int dst_height = _mapSize.height();
    if (orig_buffer == nullptr || dst_width <= 0 || dst_height <= 0
        || dst_offset < 0 || copy_range.offset < 0
        || copy_range.blockSize <= 0 || copy_range.count <= 0) {
        return;
    }

    const uint64_t dst_total = static_cast<uint64_t>(dst_width) * static_cast<uint64_t>(dst_height);
    if (static_cast<uint64_t>(dst_offset) >= dst_total) {
        return;
    }

    const int dst_x = dst_offset % dst_width;
    const int dst_y = dst_offset / dst_width;
    const int cols_to_copy = std::clamp(copy_range.blockSize, 0, dst_width - dst_x);
    const int rows_to_copy = std::clamp(copy_range.count, 0, dst_height - dst_y);
    if (cols_to_copy <= 0 || rows_to_copy <= 0) {
        return;
    }

    const uint64_t tight_cells = static_cast<uint64_t>(copy_range.blockSize)
        * static_cast<uint64_t>(copy_range.count);
    const uint64_t row_width_cells = (copy_range.sourceRowWidth >= copy_range.blockSize)
        ? (static_cast<uint64_t>(copy_range.sourceRowWidth) * static_cast<uint64_t>(copy_range.count))
        : 0ULL;
    const bool has_explicit_row_width = (copy_range.offset == 0 && copy_range.skip <= 0)
        && (row_width_cells > 0ULL)
        && (copy_range.bufferSize > 0 && static_cast<uint64_t>(copy_range.bufferSize) == row_width_cells);
    const bool is_tight_buffer = (copy_range.offset == 0)
        && ((copy_range.bufferSize > 0 && static_cast<uint64_t>(copy_range.bufferSize) == tight_cells)
            || (copy_range.bufferSize <= 0 && copy_range.skip <= 0));
    const int src_stride = has_explicit_row_width
        ? copy_range.sourceRowWidth
        : (is_tight_buffer
                  ? copy_range.blockSize
                  : (copy_range.blockSize + std::max(0, copy_range.skip)));

    for (int row = 0; row < rows_to_copy; ++row) {
        const uint64_t src_row_offset = static_cast<uint64_t>(copy_range.offset)
            + static_cast<uint64_t>(row) * static_cast<uint64_t>(src_stride);
        int valid_cols = cols_to_copy;
        if (copy_range.bufferSize > 0) {
            if (src_row_offset >= static_cast<uint64_t>(copy_range.bufferSize)) {
                break;
            }
            const uint64_t available = static_cast<uint64_t>(copy_range.bufferSize) - src_row_offset;
            valid_cols = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(cols_to_copy), available));
        }
        if (valid_cols <= 0) {
            break;
        }

        const size_t dst_row_offset = MapLinearIndex(dst_x, dst_y + row, dst_width);
        std::memcpy(
            _map.data() + dst_row_offset,
            orig_buffer + static_cast<size_t>(src_row_offset),
            static_cast<size_t>(valid_cols));
    }

    emit mapContentUpdated();
}

void State::_paste(int offset, const CopyRange& copy_range, const uint8_t* orig_buffer)
{
    if (copy_range.count <= 0 || copy_range.blockSize <= 0 || orig_buffer == nullptr) {
        return;
    }

    switch (copy_range.type) {
    case CopyRange::CHARS:
        _pasteChars(offset, copy_range, orig_buffer);
        break;
    case CopyRange::TILES:
        _pasteTiles(offset, copy_range, orig_buffer);
        break;
    case CopyRange::MAP:
        _pasteMap(offset, copy_range, orig_buffer);
        break;
    }

    emit contentsChanged();
}

void State::_tileInvert(int tile_index)
{
    if (!isValidTileIndex(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const bool is_ecm = isExtendedColorMode();
    std::bitset<ECM_CHAR_COUNT> inverted_ecm_chars;

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const int effective_char = effectiveCharIndex(char_index);
            if (is_ecm) {
                // Deduplicate physical ECM glyphs so self-aliased tiles invert each glyph once and undo stays self-inverse.
                const size_t bit_pos = static_cast<size_t>(effective_char);
                if (inverted_ecm_chars.test(bit_pos)) {
                    continue;
                }
                inverted_ecm_chars.set(bit_pos);
            }
            const size_t char_base = static_cast<size_t>(effective_char) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                _charset[char_base + row] = static_cast<uint8_t>(~_charset[char_base + row]);
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileClear(int tile_index)
{
    if (!isValidTileIndex(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const size_t char_base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
            std::fill_n(_charset.data() + char_base, 8U, uint8_t { 0U });
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileFlipHorizontally(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const bool is_multicolor_tile = shouldBeDisplayedInMulticolor2(tile_index);

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const size_t char_base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                _charset[char_base + row] = reverseCharRowBits(
                    _charset[char_base + row],
                    is_multicolor_tile);
            }
        }
    }

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width / 2; ++x) {
            const int left_char = base_char + (x + y * tile_width) * step;
            const int right_char = base_char + ((tile_width - 1 - x) + y * tile_width) * step;
            if (left_char < 0 || left_char >= 256 || right_char < 0 || right_char >= 256) {
                continue;
            }
            const size_t left_base = static_cast<size_t>(effectiveCharIndex(left_char)) * 8U;
            const size_t right_base = static_cast<size_t>(effectiveCharIndex(right_char)) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                std::swap(_charset[left_base + row], _charset[right_base + row]);
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileFlipVertically(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const size_t char_base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
            for (size_t row = 0U; row < 4U; ++row) {
                std::swap(_charset[char_base + row], _charset[char_base + (7U - row)]);
            }
        }
    }

    for (int y = 0; y < tile_height / 2; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const int top_char = base_char + (x + y * tile_width) * step;
            const int bottom_char = base_char + (x + (tile_height - 1 - y) * tile_width) * step;
            if (top_char < 0 || top_char >= 256 || bottom_char < 0 || bottom_char >= 256) {
                continue;
            }
            const size_t top_base = static_cast<size_t>(effectiveCharIndex(top_char)) * 8U;
            const size_t bottom_base = static_cast<size_t>(effectiveCharIndex(bottom_char)) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                std::swap(_charset[top_base + row], _charset[bottom_base + row]);
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileRotate(int tile_index)
{
    if (!canRotateTile(tile_index)) {
        return;
    }

    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();

    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
            const CharGlyph src = getCharFromTile(tile_index, x, y);
            CharGlyph rotated { };

            for (unsigned int row = 0U; row < 8U; ++row) {
                for (unsigned int col = 0U; col < 8U; ++col) {
                    if ((src[row] & static_cast<uint8_t>(1U << (7U - col))) != 0U) {
                        rotated[col] = static_cast<uint8_t>(
                            rotated[col] | static_cast<uint8_t>(1U << row));
                    }
                }
            }

            setCharForTile(tile_index, x, y, rotated);
        }
    }

    if (tile_width > 1) {
        std::vector<CharGlyph> rotated_chars(static_cast<size_t>(tile_width * tile_height));

        for (int y = 0; y < tile_height; ++y) {
            for (int x = 0; x < tile_width; ++x) {
                const size_t dst_index = static_cast<size_t>((tile_width - 1 - y) + x * tile_width);
                rotated_chars[dst_index] = getCharFromTile(tile_index, x, y);
            }
        }

        for (int y = 0; y < tile_height; ++y) {
            for (int x = 0; x < tile_width; ++x) {
                const size_t src_index = static_cast<size_t>(x + y * tile_width);
                setCharForTile(tile_index, x, y, rotated_chars[src_index]);
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileShiftLeft(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const int shift_passes = shouldBeDisplayedInMulticolor2(tile_index) ? 2 : 1;

    for (int pass = 0; pass < shift_passes; ++pass) {
        for (int y = 0; y < tile_height; ++y) {
            for (size_t row = 0U; row < 8U; ++row) {
                uint8_t left_bit = 0U;
                uint8_t prev_left_bit = 0U;

                for (int x = tile_width - 1; x >= 0; --x) {
                    const int char_index = base_char + (x + y * tile_width) * step;
                    if (char_index < 0 || char_index >= 256) {
                        continue;
                    }
                    uint8_t& cell = _charset[static_cast<size_t>(effectiveCharIndex(char_index)) * 8U + row];
                    left_bit = static_cast<uint8_t>((static_cast<unsigned int>(cell) >> 7U) & 0x01U);
                    cell = static_cast<uint8_t>(
                        ((static_cast<unsigned int>(cell) << 1U) & 0xFEU)
                        | static_cast<unsigned int>(prev_left_bit));
                    prev_left_bit = left_bit;
                }

                const int rightmost_char = base_char + ((tile_width - 1) + y * tile_width) * step;
                if (rightmost_char >= 0 && rightmost_char < 256) {
                    uint8_t& cell = _charset[static_cast<size_t>(effectiveCharIndex(rightmost_char)) * 8U + row];
                    cell = static_cast<uint8_t>(
                        (static_cast<unsigned int>(cell) & 0xFEU)
                        | static_cast<unsigned int>(left_bit));
                }
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileShiftRight(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const int shift_passes = shouldBeDisplayedInMulticolor2(tile_index) ? 2 : 1;

    for (int pass = 0; pass < shift_passes; ++pass) {
        for (int y = 0; y < tile_height; ++y) {
            for (size_t row = 0U; row < 8U; ++row) {
                uint8_t right_bit = 0U;
                uint8_t prev_right_bit = 0U;

                for (int x = 0; x < tile_width; ++x) {
                    const int char_index = base_char + (x + y * tile_width) * step;
                    if (char_index < 0 || char_index >= 256) {
                        continue;
                    }
                    uint8_t& cell = _charset[static_cast<size_t>(effectiveCharIndex(char_index)) * 8U + row];
                    right_bit = static_cast<uint8_t>(static_cast<unsigned int>(cell) & 0x01U);
                    cell = static_cast<uint8_t>(
                        ((static_cast<unsigned int>(cell) >> 1U) & 0x7FU)
                        | (static_cast<unsigned int>(prev_right_bit) << 7U));
                    prev_right_bit = right_bit;
                }

                const int leftmost_char = base_char + y * tile_width * step;
                if (leftmost_char >= 0 && leftmost_char < 256) {
                    uint8_t& cell = _charset[static_cast<size_t>(effectiveCharIndex(leftmost_char)) * 8U + row];
                    cell = static_cast<uint8_t>(
                        (static_cast<unsigned int>(cell) & 0x7FU)
                        | (static_cast<unsigned int>(right_bit) << 7U));
                }
            }
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileShiftUp(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int x = 0; x < tile_width; ++x) {
        uint8_t prev_top_byte = 0U;

        for (int y = tile_height - 1; y >= 0; --y) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const size_t char_base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
            const uint8_t top_byte = _charset[char_base];

            for (size_t row = 0U; row < 7U; ++row) {
                _charset[char_base + row] = _charset[char_base + row + 1U];
            }

            _charset[char_base + 7U] = prev_top_byte;
            prev_top_byte = top_byte;
        }

        const int bottom_char = base_char + (x + (tile_height - 1) * tile_width) * step;
        if (bottom_char >= 0 && bottom_char < 256) {
            _charset[static_cast<size_t>(effectiveCharIndex(bottom_char)) * 8U + 7U] = prev_top_byte;
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileShiftDown(int tile_index)
{
    if (!isValidTileIndex(tile_index) || hasAliasedSubChars(tile_index)) {
        return;
    }

    const int base_char = getCharIndexFromTileIndex(tile_index);
    const int tile_width = _tileProperties.size.width();
    const int tile_height = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int x = 0; x < tile_width; ++x) {
        uint8_t prev_bottom_byte = 0U;

        for (int y = 0; y < tile_height; ++y) {
            const int char_index = base_char + (x + y * tile_width) * step;
            if (char_index < 0 || char_index >= 256) {
                continue;
            }
            const size_t char_base = static_cast<size_t>(effectiveCharIndex(char_index)) * 8U;
            const uint8_t bottom_byte = _charset[char_base + 7U];

            for (size_t row = 7U; row > 0U; --row) {
                _charset[char_base + row] = _charset[char_base + row - 1U];
            }

            _charset[char_base] = prev_bottom_byte;
            prev_bottom_byte = bottom_byte;
        }

        const int top_char = base_char + x * step;
        if (top_char >= 0 && top_char < 256) {
            _charset[static_cast<size_t>(effectiveCharIndex(top_char)) * 8U] = prev_bottom_byte;
        }
    }

    emit tileUpdated(tile_index);
    emit contentsChanged();
}

void State::_tileSetPen(int tile_index, const QPoint& position, int pen)
{
    if (!isValidTileIndex(tile_index) || pen < 0 || pen >= PEN_MAX) {
        return;
    }

    if (WriteTilePixelPen(
            _charset,
            getCharIndexFromTileIndex(tile_index),
            _tileProperties,
            position,
            pen,
            shouldBeDisplayedInMulticolor2(tile_index),
            isExtendedColorMode())) {
        emit tileUpdated(tile_index);
        emit contentsChanged();
    }
}

void State::_applyTilePoints(int tile_index, const QList<QPoint>& points, int pen)
{
    if (!isValidTileIndex(tile_index) || pen < 0 || pen >= PEN_MAX) {
        return;
    }

    bool any_changed = false;
    const int base_char = getCharIndexFromTileIndex(tile_index);
    const bool is_multicolor = shouldBeDisplayedInMulticolor2(tile_index);
    const bool is_ecm = isExtendedColorMode();

    for (const QPoint& position : points) {
        if (WriteTilePixelPen(
                _charset,
                base_char,
                _tileProperties,
                position,
                pen,
                is_multicolor,
                is_ecm)) {
            any_changed = true;
        }
    }

    if (any_changed) {
        emit tileUpdated(tile_index);
        emit contentsChanged();
    }
}

void State::_setCharacterMode(CharacterMode mode)
{
    if (_characterMode == mode) {
        return;
    }

    const CharacterMode old_mode = _characterMode;
    _characterMode = mode;

    emit characterModeChanged(mode);

    const bool was_multicolor = (old_mode == CHARACTER_MODE_MULTICOLOR);
    const bool is_multicolor = (mode == CHARACTER_MODE_MULTICOLOR);
    if (was_multicolor != is_multicolor) {
        emit multicolorModeToggled(is_multicolor);
    }

    emit contentsChanged();
}

void State::_setForegroundColorMode(ForegroundColorMode mode)
{
    if (_foregroundColorMode == mode) {
        return;
    }
    const bool was_multicolor = shouldBeDisplayedInMulticolor2(_tileIndex);
    _foregroundColorMode = mode;
    const bool is_multicolor_now = shouldBeDisplayedInMulticolor2(_tileIndex);

    emit colorPropertiesUpdated(PEN_FOREGROUND);
    if (was_multicolor != is_multicolor_now) {
        emit multicolorModeToggled(is_multicolor_now);
    }
    emit contentsChanged();
}

void State::_setColorForPen(int pen, int color, int tile_index)
{
    if (pen < 0 || pen >= PEN_MAX) {
        return;
    }
    const uint8_t masked_color = static_cast<uint8_t>(static_cast<unsigned int>(color) & 0x0FU);
    const bool is_per_tile_foreground = (pen == PEN_FOREGROUND
        && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);
    const int resolved_index = (tile_index >= 0 && tile_index < TILE_COLORS_BUFFER_SIZE)
        ? tile_index
        : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);

    const uint8_t current_color = is_per_tile_foreground
        ? static_cast<uint8_t>(_tileColors[static_cast<size_t>(resolved_index)] & 0x0FU)
        : static_cast<uint8_t>(_penColors[static_cast<size_t>(pen)] & 0x0FU);
    if (current_color == masked_color) {
        return;
    }

    const bool was_multicolor = shouldBeDisplayedInMulticolor2(resolved_index);
    if (is_per_tile_foreground) {
        _tileColors[static_cast<size_t>(resolved_index)] = masked_color;
    } else {
        _penColors[static_cast<size_t>(pen)] = masked_color;
    }
    const bool is_multicolor_now = shouldBeDisplayedInMulticolor2(resolved_index);

    emit colorPropertiesUpdated(pen);
    if (was_multicolor != is_multicolor_now) {
        emit multicolorModeToggled(is_multicolor_now);
    }
    emit contentsChanged();
}

void State::_setTileProperties(const TileProperties& properties)
{
    if (!IsValidTileProperties(properties) || _tileProperties == properties) {
        return;
    }

    _tileProperties = properties;
    const int limit = maxTiles();
    if (_tileIndex >= limit) {
        _setTileIndex(limit - 1);
        _setCharIndex(getCharIndexFromTileIndex(_tileIndex));
    }

    emit tilePropertiesUpdated();
    emit contentsChanged();
}

void State::_setExportProperties(const ExportProperties& properties)
{
    if (_exportProperties == properties) {
        return;
    }
    _exportProperties = properties;
    emit contentsChanged();
}

void State::_setMapSize(const QSize& map_size)
{
    if (map_size.width() < 1 || map_size.width() > kMaxMapDim
        || map_size.height() < 1 || map_size.height() > kMaxMapDim
        || _mapSize == map_size) {
        return;
    }

    const size_t new_cell_count = static_cast<size_t>(map_size.width())
        * static_cast<size_t>(map_size.height());
    std::vector<uint8_t> new_map(new_cell_count, 0U);

    const int rows_to_copy = std::min(map_size.height(), _mapSize.height());
    const int cols_to_copy = std::min(map_size.width(), _mapSize.width());
    for (int row = 0; row < rows_to_copy; ++row) {
        const size_t src_row_offset = static_cast<size_t>(row) * static_cast<size_t>(_mapSize.width());
        const size_t dst_row_offset = static_cast<size_t>(row) * static_cast<size_t>(map_size.width());
        std::copy_n(
            _map.data() + src_row_offset,
            static_cast<size_t>(cols_to_copy),
            new_map.data() + dst_row_offset);
    }

    _map = std::move(new_map);
    _mapSize = map_size;

    emit mapSizeUpdated();
    emit contentsChanged();
}

void State::_setMap(const std::vector<uint8_t>& map_cells, const QSize& size)
{
    if (_mapSize != size) {
        return;
    }
    const size_t expected_cells = static_cast<size_t>(size.width()) * static_cast<size_t>(size.height());
    if (map_cells.size() != expected_cells) {
        return;
    }
    _map = map_cells;
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_mapClear(int tile_index)
{
    const uint8_t fill_value = static_cast<uint8_t>(static_cast<unsigned int>(tile_index) & 0xFFU);
    std::fill(_map.begin(), _map.end(), fill_value);
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_mapPaint(const QPoint& coord, int tile_index)
{
    if (!isValidMapCoord(coord)) {
        return;
    }
    const size_t cell_index = MapLinearIndex(coord.x(), coord.y(), _mapSize.width());
    _map[cell_index] = static_cast<uint8_t>(static_cast<unsigned int>(tile_index) & 0xFFU);
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_applyMapPoints(const QList<QPoint>& coords, uint8_t tile_index)
{
    bool any_written = false;
    const int map_width = _mapSize.width();
    for (const QPoint& coord : coords) {
        if (!isValidMapCoord(coord)) {
            continue;
        }
        _map[MapLinearIndex(coord.x(), coord.y(), map_width)] = tile_index;
        any_written = true;
    }
    if (any_written) {
        emit mapContentUpdated();
        emit contentsChanged();
    }
}

void State::_applyFloodFillCoords(const std::vector<QPoint>& coords, uint8_t replacement_tile)
{
    if (coords.empty()) {
        return;
    }
    const int map_width = _mapSize.width();
    for (const QPoint& coord : coords) {
        if (!isValidMapCoord(coord)) {
            continue;
        }
        _map[MapLinearIndex(coord.x(), coord.y(), map_width)] = replacement_tile;
    }
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_fillMapRect(const QRect& bounds, uint8_t value)
{
    const QRect map_bounds(0, 0, _mapSize.width(), _mapSize.height());
    const QRect clipped = bounds.intersected(map_bounds);
    if (clipped.isEmpty()) {
        return;
    }

    const int map_width = _mapSize.width();
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const size_t row_base = MapLinearIndex(clipped.left(), y, map_width);
        std::fill_n(_map.data() + row_base, static_cast<size_t>(clipped.width()), value);
    }
    emit mapContentUpdated();
    emit contentsChanged();
}
