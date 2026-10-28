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

/**
 * @file state.cpp
 * @brief Implementation of the headless C64 / VIC-II `State` document model (`vchar64_core`).
 *
 * Implements VIC-II Multicolor bit-pair pixel transforms (`reverseCharRowBits`,
 * 2-pass horizontal shifts, Color RAM bit-3 gating, square Hi-Res rotation guard),
 * iterative BFS map flood-fill (`collectFloodFillRegion`), destination-stride
 * clipboard pasting (`_pasteMap`), and `QUndoStack` command orchestration.
 */

#include "state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTime>
#include <QtGlobal>

#include "commands.h"
#include "core/documentimagerenderer.h"
#include "stateexport.h"
#include "stateimport.h"

State::State(const QString& filename, const QSize& mapSize)
    : _mapSize(
          std::clamp(mapSize.width(), 1, kMaxMapDim),
          std::clamp(mapSize.height(), 1, kMaxMapDim))
    , _loadedFilename(filename)
    , _undoStack(std::make_unique<QUndoStack>())
{
    _undoStack->setUndoLimit(kDefaultUndoLimit);

    const size_t cellCount = static_cast<size_t>(_mapSize.width())
        * static_cast<size_t>(_mapSize.height());
    _map.assign(cellCount, 0x20U);
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

void State::copyState(const State& copyFromMe)
{
    _totalChars = copyFromMe._totalChars;
    _mapSize = copyFromMe._mapSize;
    _multicolorMode = copyFromMe._multicolorMode;
    _foregroundColorMode = copyFromMe._foregroundColorMode;
    _selectedPen = copyFromMe._selectedPen;
    _tileProperties = copyFromMe._tileProperties;
    _charIndex = copyFromMe._charIndex;
    _tileIndex = copyFromMe._tileIndex;
    _exportProperties = copyFromMe._exportProperties;
    _keyboardMapping = copyFromMe._keyboardMapping;

    _penColors = copyFromMe._penColors;
    _charset = copyFromMe._charset;
    _tileColors = copyFromMe._tileColors;
    _map = copyFromMe._map;
}

void State::reset()
{
    _totalChars = 0;
    _multicolorMode = false;
    _foregroundColorMode = FOREGROUND_COLOR_GLOBAL;
    _selectedPen = PEN_FOREGROUND;
    _penColors = { 1U, 5U, 7U, 11U };
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

bool State::isModified() const
{
    return !_undoStack->isClean();
}

vchar64::Result<vchar64::ImportReport> State::openFile(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open file '%1': %2").arg(filename, file.errorString()));
    }

    enum class FileType {
        VChar64,
        Prg,
        Raw,
        Ctm
    };

    quint16 loadedAddress = 0;
    FileType filetype = FileType::Raw;

    const QFileInfo info(file);
    const QString suffix = info.suffix().toLower();

    vchar64::Result<vchar64::ImportReport> result = vchar64::Result<vchar64::ImportReport>::err(
        vchar64::ErrorCode::FileReadFailed);

    if (suffix == QStringLiteral("vchar64proj")) {
        filetype = FileType::VChar64;
        result = StateImport::loadVChar64(*this, file);
    } else if (suffix == QStringLiteral("64c") || suffix == QStringLiteral("prg")) {
        filetype = FileType::Prg;
        result = StateImport::loadPRG(*this, file, &loadedAddress);
    } else if (suffix == QStringLiteral("ctm")) {
        filetype = FileType::Ctm;
        result = StateImport::loadCTM(*this, file);
    } else {
        filetype = FileType::Raw;
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

        if (filetype == FileType::VChar64) {
            _savedFilename = filename;
        } else if (filetype == FileType::Raw || filetype == FileType::Prg) {
            _exportedFilename = filename;
            _exportProperties.features = EXPORT_FEATURE_CHARSET;
            if (filetype == FileType::Prg) {
                _exportProperties.addresses[0] = loadedAddress;
                _exportProperties.format = EXPORT_FORMAT_PRG;
            } else {
                _exportProperties.format = EXPORT_FORMAT_RAW;
            }
        }
    }

    _undoStack->clear();
    return result;
}

QString State::filenameFixSuffix(const QString& filename, ExportFeature suffix)
{
    static const std::array<QString, 3> kValidDescriptions = {
        QStringLiteral("-charset"),
        QStringLiteral("-map"),
        QStringLiteral("-colors")
    };

    const QFileInfo info(filename);
    const QString extension = info.suffix();
    const QString filepath = info.path();
    QString name = info.completeBaseName();

    for (const QString& validDescription : kValidDescriptions) {
        if (name.endsWith(validDescription)) {
            name.chop(validDescription.size());
            break;
        }
    }

    if (suffix == EXPORT_FEATURE_CHARSET) {
        name.append(kValidDescriptions[0]);
    } else if (suffix == EXPORT_FEATURE_MAP) {
        name.append(kValidDescriptions[1]);
    } else {
        name.append(kValidDescriptions[2]);
    }

    const QString prefix = (filepath == QStringLiteral("/"))
        ? QStringLiteral("/")
        : (filepath + QLatin1Char('/'));
    if (extension.isEmpty()) {
        return prefix + name;
    }
    return prefix + name + QLatin1Char('.') + extension;
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

vchar64::Result<void> State::exportRaw(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const auto res = StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            _charset.size());
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const auto res = StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size());
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const auto res = StateExport::saveRaw(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size());
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    _exportedFilename = filename;
    ExportProperties copy = properties;
    copy.format = EXPORT_FORMAT_RAW;
    setExportProperties(copy);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportPRG(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const auto res = StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            _charset.size(),
            properties.addresses[0]);
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const auto res = StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            properties.addresses[1]);
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const auto res = StateExport::savePRG(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            properties.addresses[2]);
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    _exportedFilename = filename;
    ExportProperties copy = properties;
    copy.format = EXPORT_FORMAT_PRG;
    setExportProperties(copy);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportAsm(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const auto res = StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            _charset.size(),
            QStringLiteral("charset"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const auto res = StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            QStringLiteral("map"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const auto res = StateExport::saveAsm(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            QStringLiteral("colors"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    _exportedFilename = filename;
    ExportProperties copy = properties;
    copy.format = EXPORT_FORMAT_ASM;
    setExportProperties(copy);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportC(const QString& filename, const ExportProperties& properties)
{
    if ((properties.features & EXPORT_FEATURE_CHARSET) != 0U) {
        const auto res = StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset.data(),
            _charset.size(),
            QStringLiteral("charset"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_MAP) != 0U) {
        const auto res = StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(),
            _map.size(),
            QStringLiteral("map"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if ((properties.features & EXPORT_FEATURE_COLORS) != 0U) {
        const auto res = StateExport::saveC(
            filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors.data(),
            _tileColors.size(),
            QStringLiteral("colors"));
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    _exportedFilename = filename;
    ExportProperties copy = properties;
    copy.format = EXPORT_FORMAT_C;
    setExportProperties(copy);
    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::exportPNG(
    const QString& filename,
    bool exportTileset,
    bool exportMap,
    bool transparentBackground)
{
    if (!exportTileset && !exportMap) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("No PNG export target selected"));
    }

    if (exportTileset) {
        const QImage tilesetImg = DocumentImageRenderer::renderTileset(*this, transparentBackground);
        const auto res = StateExport::savePNG(
            filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            tilesetImg);
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    if (exportMap) {
        const QImage mapImg = DocumentImageRenderer::renderMap(*this, transparentBackground);
        const auto res = StateExport::savePNG(
            filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            mapImg);
        if (!res.isOk()) {
            return vchar64::Result<void>::err(res.error().code, res.error().detail);
        }
    }

    return vchar64::Result<void>::ok();
}

vchar64::Result<void> State::saveProject(const QString& filename)
{
    if (_undoStack->isClean() && _savedFilename == filename && QFileInfo::exists(filename)) {
        return vchar64::Result<void>::ok();
    }

    const auto res = StateExport::saveVChar64(*this, filename);
    if (!res.isOk()) {
        return vchar64::Result<void>::err(res.error().code, res.error().detail);
    }

    _loadedFilename = filename;
    _savedFilename = filename;
    _undoStack->setClean();
    emit contentsChanged();
    return vchar64::Result<void>::ok();
}

bool State::shouldBeDisplayedInMulticolor() const noexcept
{
    return _multicolorMode && ((_penColors[PEN_FOREGROUND] & 0x0FU) >= 8U);
}

bool State::shouldBeDisplayedInMulticolor2(int tileIdx) const noexcept
{
    if (!_multicolorMode) {
        return false;
    }
    if (_foregroundColorMode == FOREGROUND_COLOR_GLOBAL) {
        return (_penColors[PEN_FOREGROUND] & 0x0FU) >= 8U;
    }
    const int resolvedIdx = (tileIdx >= 0 && tileIdx < TILE_COLORS_BUFFER_SIZE)
        ? tileIdx
        : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
    return (_tileColors[static_cast<size_t>(resolvedIdx)] & 0x0FU) >= 8U;
}

void State::setMulticolorMode(bool enabled)
{
    _undoStack->push(new SetMulticolorModeCommand(this, enabled));
}

void State::_setMulticolorMode(bool enabled)
{
    if (_multicolorMode != enabled) {
        _multicolorMode = enabled;
        emit multicolorModeToggled(enabled);
        emit contentsChanged();
    }
}

void State::setForegroundColorMode(ForegroundColorMode mode)
{
    _undoStack->push(new SetForegroundColorMode(this, mode));
}

void State::_setForegroundColorMode(ForegroundColorMode mode)
{
    if (_foregroundColorMode != mode) {
        const bool oldMc = shouldBeDisplayedInMulticolor2(_tileIndex);
        _foregroundColorMode = mode;
        const bool newMc = shouldBeDisplayedInMulticolor2(_tileIndex);

        emit colorPropertiesUpdated(PEN_FOREGROUND);
        if (oldMc != newMc) {
            emit multicolorModeToggled(newMc);
        }
        emit contentsChanged();
    }
}

int State::getColorForPen(int pen) const
{
    return getColorForPen(pen, -1);
}

int State::getColorForPen(int pen, int tileIdx) const
{
    if (pen < 0 || pen >= PEN_MAX) {
        return 0;
    }

    const bool foregroundAndPerTile = (pen == PEN_FOREGROUND
        && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);
    if (foregroundAndPerTile) {
        const int resolvedIdx = (tileIdx >= 0 && tileIdx < TILE_COLORS_BUFFER_SIZE)
            ? tileIdx
            : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
        return static_cast<int>(_tileColors[static_cast<size_t>(resolvedIdx)] & 0x0FU);
    }

    return static_cast<int>(_penColors[static_cast<size_t>(pen)] & 0x0FU);
}

int State::getGlobalColorForPen(int pen) const noexcept
{
    if (pen < 0 || pen >= PEN_MAX) {
        return 0;
    }
    return static_cast<int>(_penColors[static_cast<size_t>(pen)] & 0x0FU);
}

void State::setColorForPen(int pen, int color)
{
    setColorForPen(pen, color, -1);
}

void State::setColorForPen(int pen, int color, int tileIdx)
{
    if (pen < 0 || pen >= PEN_MAX) {
        return;
    }
    const int maskedColor = static_cast<int>(static_cast<unsigned int>(color) & 0x0FU);
    _undoStack->push(new SetColorCommand(this, maskedColor, pen, tileIdx));
}

void State::_setColorForPen(int pen, int color, int tileIdx)
{
    if (pen < 0 || pen >= PEN_MAX) {
        return;
    }
    const uint8_t maskedColor = static_cast<uint8_t>(static_cast<unsigned int>(color) & 0x0FU);
    const bool foregroundAndPerTile = (pen == PEN_FOREGROUND
        && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);
    const int resolvedIdx = (tileIdx >= 0 && tileIdx < TILE_COLORS_BUFFER_SIZE)
        ? tileIdx
        : std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);

    const uint8_t currentColor = foregroundAndPerTile
        ? static_cast<uint8_t>(_tileColors[static_cast<size_t>(resolvedIdx)] & 0x0FU)
        : static_cast<uint8_t>(_penColors[static_cast<size_t>(pen)] & 0x0FU);

    if (currentColor != maskedColor) {
        const bool oldValue = shouldBeDisplayedInMulticolor2(resolvedIdx);

        if (foregroundAndPerTile) {
            _tileColors[static_cast<size_t>(resolvedIdx)] = maskedColor;
        } else {
            _penColors[static_cast<size_t>(pen)] = maskedColor;
        }

        const bool newValue = shouldBeDisplayedInMulticolor2(resolvedIdx);

        emit colorPropertiesUpdated(pen);
        if (oldValue != newValue) {
            emit multicolorModeToggled(newValue);
        }
        emit contentsChanged();
    }
}

int State::getCurrentColor() const
{
    return getColorForPen(_selectedPen, _tileIndex);
}

void State::setSelectedPen(int pen)
{
    if (pen < 0 || pen >= PEN_MAX) {
        return;
    }
    if (pen != _selectedPen) {
        _selectedPen = pen;
        emit selectedPenChanged(pen);
    }
}

int State::tileGetPen(int tileIndex, const QPoint& position) const
{
    if (!isValidTileIndex(tileIndex)) {
        return 0;
    }

    const int x = position.x();
    const int y = position.y();
    const int maxPxX = _tileProperties.size.width() * 8;
    const int maxPxY = _tileProperties.size.height() * 8;
    if (x < 0 || x >= maxPxX || y < 0 || y >= maxPxY) {
        return 0;
    }

    const int bitIndex = (x % 8) + (y % 8) * 8;
    const int charIndex = getCharIndexFromTileIndex(tileIndex)
        + (x / 8) * _tileProperties.interleaved
        + (y / 8) * _tileProperties.interleaved * _tileProperties.size.width();
    if (charIndex < 0 || charIndex >= 256) {
        return 0;
    }

    const uint8_t c = _charset[static_cast<size_t>(charIndex) * 8U + static_cast<size_t>(bitIndex / 8)];
    const unsigned int b = static_cast<unsigned int>(bitIndex % 8);

    if (!shouldBeDisplayedInMulticolor2(tileIndex)) {
        const unsigned int shift = 7U - b;
        return static_cast<int>((c >> shift) & 0x01U);
    }

    static constexpr std::array<uint8_t, 8> kMasks = { 192U, 192U, 48U, 48U, 12U, 12U, 3U, 3U };
    const unsigned int shift = (7U - b) & 0xFEU;
    return static_cast<int>((c & kMasks[b]) >> shift);
}

void State::_tileSetPen(int tileIndex, const QPoint& position, int pen)
{
    if (!isValidTileIndex(tileIndex) || pen < 0 || pen >= PEN_MAX) {
        return;
    }

    const int x = position.x();
    const int y = position.y();
    const int maxPxX = _tileProperties.size.width() * 8;
    const int maxPxY = _tileProperties.size.height() * 8;
    if (x < 0 || x >= maxPxX || y < 0 || y >= maxPxY) {
        return;
    }

    const int bitIndex = (x % 8) + (y % 8) * 8;
    const int charIndex = getCharIndexFromTileIndex(tileIndex)
        + (x / 8) * _tileProperties.interleaved
        + (y / 8) * _tileProperties.interleaved * _tileProperties.size.width();
    if (charIndex < 0 || charIndex >= 256) {
        return;
    }

    const size_t byteIndex = static_cast<size_t>(charIndex) * 8U + static_cast<size_t>(bitIndex / 8);
    const unsigned int b = static_cast<unsigned int>(bitIndex % 8);

    uint8_t andMask = 0U;
    uint8_t orMask = 0U;
    if (!shouldBeDisplayedInMulticolor2(tileIndex)) {
        const unsigned int shift = 7U - b;
        const unsigned int bitVal = (pen != 0) ? 1U : 0U;
        andMask = static_cast<uint8_t>(1U << shift);
        orMask = static_cast<uint8_t>(bitVal << shift);
    } else {
        static constexpr std::array<uint8_t, 8> kMasks = { 192U, 192U, 48U, 48U, 12U, 12U, 3U, 3U };
        const unsigned int shift = (7U - b) & 0xFEU;
        andMask = kMasks[b];
        orMask = static_cast<uint8_t>((static_cast<unsigned int>(pen) & 0x03U) << shift);
    }

    uint8_t updated = static_cast<uint8_t>(_charset[byteIndex] & static_cast<uint8_t>(~andMask));
    updated = static_cast<uint8_t>(updated | orMask);

    if (updated != _charset[byteIndex]) {
        _charset[byteIndex] = updated;
        emit tileUpdated(tileIndex);
        emit contentsChanged();
    }
}

void State::_applyTilePoints(int tileIndex, const QList<QPoint>& points, int pen)
{
    if (!isValidTileIndex(tileIndex) || pen < 0 || pen >= PEN_MAX) {
        return;
    }

    bool anyChanged = false;
    const int maxPxX = _tileProperties.size.width() * 8;
    const int maxPxY = _tileProperties.size.height() * 8;
    const bool isMc = shouldBeDisplayedInMulticolor2(tileIndex);

    for (const QPoint& position : points) {
        const int x = position.x();
        const int y = position.y();
        if (x < 0 || x >= maxPxX || y < 0 || y >= maxPxY) {
            continue;
        }

        const int bitIndex = (x % 8) + (y % 8) * 8;
        const int charIndex = getCharIndexFromTileIndex(tileIndex)
            + (x / 8) * _tileProperties.interleaved
            + (y / 8) * _tileProperties.interleaved * _tileProperties.size.width();
        if (charIndex < 0 || charIndex >= 256) {
            continue;
        }

        const size_t byteIndex = static_cast<size_t>(charIndex) * 8U + static_cast<size_t>(bitIndex / 8);
        const unsigned int b = static_cast<unsigned int>(bitIndex % 8);

        uint8_t andMask = 0U;
        uint8_t orMask = 0U;
        if (!isMc) {
            const unsigned int shift = 7U - b;
            const unsigned int bitVal = (pen != 0) ? 1U : 0U;
            andMask = static_cast<uint8_t>(1U << shift);
            orMask = static_cast<uint8_t>(bitVal << shift);
        } else {
            static constexpr std::array<uint8_t, 8> kMasks = { 192U, 192U, 48U, 48U, 12U, 12U, 3U, 3U };
            const unsigned int shift = (7U - b) & 0xFEU;
            andMask = kMasks[b];
            orMask = static_cast<uint8_t>((static_cast<unsigned int>(pen) & 0x03U) << shift);
        }

        uint8_t updated = static_cast<uint8_t>(_charset[byteIndex] & static_cast<uint8_t>(~andMask));
        updated = static_cast<uint8_t>(updated | orMask);
        if (updated != _charset[byteIndex]) {
            _charset[byteIndex] = updated;
            anyChanged = true;
        }
    }

    if (anyChanged) {
        emit tileUpdated(tileIndex);
        emit contentsChanged();
    }
}

void State::setTileProperties(const TileProperties& properties)
{
    const int tw = properties.size.width();
    const int th = properties.size.height();
    if (tw < 1 || tw > MAX_TILE_WIDTH
        || th < 1 || th > MAX_TILE_HEIGHT
        || (tw * th) > 256
        || properties.interleaved < 1 || properties.interleaved > 256
        || properties == _tileProperties) {
        return;
    }
    _undoStack->push(new SetTilePropertiesCommand(this, properties));
}

void State::_setTileProperties(const TileProperties& properties)
{
    const int tw = properties.size.width();
    const int th = properties.size.height();
    if (tw < 1 || tw > MAX_TILE_WIDTH
        || th < 1 || th > MAX_TILE_HEIGHT
        || (tw * th) > 256
        || properties.interleaved < 1 || properties.interleaved > 256) {
        return;
    }

    if (_tileProperties != properties) {
        _tileProperties = properties;
        const int limit = maxTiles();
        if (_tileIndex >= limit) {
            _setTileIndex(limit - 1);
            _setCharIndex(getCharIndexFromTileIndex(_tileIndex));
        }

        emit tilePropertiesUpdated();
        emit contentsChanged();
    }
}

bool State::setTilePropertiesDirect(const TileProperties& properties) noexcept
{
    const int tw = properties.size.width();
    const int th = properties.size.height();
    if (tw < 1 || tw > MAX_TILE_WIDTH
        || th < 1 || th > MAX_TILE_HEIGHT
        || (tw * th) > 256
        || properties.interleaved < 1 || properties.interleaved > 256) {
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

void State::setExportProperties(const ExportProperties& properties)
{
    if (_exportProperties != properties) {
        _undoStack->push(new SetExportPropertiesCommand(this, properties));
    }
}

void State::_setExportProperties(const ExportProperties& properties)
{
    if (_exportProperties != properties) {
        _exportProperties = properties;
        emit contentsChanged();
    }
}

void State::setMapSize(const QSize& mapSize)
{
    if (mapSize.width() < 1 || mapSize.width() > kMaxMapDim
        || mapSize.height() < 1 || mapSize.height() > kMaxMapDim
        || mapSize == _mapSize) {
        return;
    }
    _undoStack->push(new SetMapSizeCommand(this, mapSize));
}

void State::_setMapSize(const QSize& mapSize)
{
    if (mapSize.width() < 1 || mapSize.width() > kMaxMapDim
        || mapSize.height() < 1 || mapSize.height() > kMaxMapDim
        || _mapSize == mapSize) {
        return;
    }

    const size_t newSizeInBytes = static_cast<size_t>(mapSize.width())
        * static_cast<size_t>(mapSize.height());
    std::vector<uint8_t> newMap(newSizeInBytes, 0U);

    const int rowsToCopy = std::min(mapSize.height(), _mapSize.height());
    const int colsToCopy = std::min(mapSize.width(), _mapSize.width());
    for (int row = 0; row < rowsToCopy; ++row) {
        const size_t srcRowOffset = static_cast<size_t>(row) * static_cast<size_t>(_mapSize.width());
        const size_t dstRowOffset = static_cast<size_t>(row) * static_cast<size_t>(mapSize.width());
        std::copy_n(
            _map.data() + srcRowOffset,
            static_cast<size_t>(colsToCopy),
            newMap.data() + dstRowOffset);
    }

    _map = std::move(newMap);
    _mapSize = mapSize;

    emit mapSizeUpdated();
    emit contentsChanged();
}

bool State::isValidMapCoord(const QPoint& pt) const noexcept
{
    return pt.x() >= 0
        && pt.x() < _mapSize.width()
        && pt.y() >= 0
        && pt.y() < _mapSize.height();
}

int State::getTileIndexFromMap(const QPoint& mapCoord) const
{
    if (!isValidMapCoord(mapCoord)) {
        return 0;
    }
    const size_t idx = static_cast<size_t>(mapCoord.y()) * static_cast<size_t>(_mapSize.width())
        + static_cast<size_t>(mapCoord.x());
    const int rawTile = static_cast<int>(_map[idx]);
    return std::clamp(rawTile, 0, maxTiles() - 1);
}

State::FloodFillPlan State::collectFloodFillRegion(
    const QPoint& startCoord,
    uint8_t targetTile,
    uint8_t replacementTile) const
{
    FloodFillPlan plan;
    if (targetTile == replacementTile || !isValidMapCoord(startCoord) || _map.empty()) {
        return plan;
    }

    const int width = _mapSize.width();
    const auto indexOf = [width](int x, int y) noexcept -> size_t {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    };

    const size_t startIdx = indexOf(startCoord.x(), startCoord.y());
    if (startIdx >= _map.size() || _map[startIdx] != targetTile) {
        return plan;
    }

    std::vector<uint8_t> visited(_map.size(), 0U);
    std::vector<QPoint> worklist;
    worklist.reserve(256);

    visited[startIdx] = 1U;
    worklist.push_back(startCoord);

    int minX = startCoord.x();
    int maxX = startCoord.x();
    int minY = startCoord.y();
    int maxY = startCoord.y();

    while (!worklist.empty()) {
        const QPoint pt = worklist.back();
        worklist.pop_back();
        plan.coords.push_back(pt);

        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());

        static constexpr std::array<QPoint, 4> kNeighbors = {
            QPoint { -1, 0 },
            QPoint { 1, 0 },
            QPoint { 0, -1 },
            QPoint { 0, 1 }
        };
        for (const QPoint& delta : kNeighbors) {
            // Guard against INT_MIN / INT_MAX signed overflow when adding delta.
            const int nx = pt.x() + delta.x();
            const int ny = pt.y() + delta.y();
            const QPoint next(nx, ny);
            if (!isValidMapCoord(next)) {
                continue;
            }
            const size_t nextIdx = indexOf(nx, ny);
            if (visited[nextIdx] != 0U || _map[nextIdx] != targetTile) {
                continue;
            }
            visited[nextIdx] = 1U;
            worklist.push_back(next);
        }
    }

    plan.bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    return plan;
}

void State::mapFill(const QPoint& coord, int tileIdx)
{
    if (!isValidMapCoord(coord)) {
        return;
    }
    const uint8_t replacement = static_cast<uint8_t>(static_cast<unsigned int>(tileIdx) & 0xFFU);
    const size_t startIdx = static_cast<size_t>(coord.y()) * static_cast<size_t>(_mapSize.width())
        + static_cast<size_t>(coord.x());
    const uint8_t target = _map[startIdx];

    FloodFillPlan plan = collectFloodFillRegion(coord, target, replacement);
    if (plan.coords.empty()) {
        return;
    }

    _undoStack->push(new FillMapCommand(this, std::move(plan), replacement));
}

void State::_applyFloodFillCoords(const std::vector<QPoint>& coords, uint8_t replacementTile)
{
    if (coords.empty()) {
        return;
    }
    const int width = _mapSize.width();
    for (const QPoint& pt : coords) {
        if (isValidMapCoord(pt)) {
            const size_t idx = static_cast<size_t>(pt.y()) * static_cast<size_t>(width)
                + static_cast<size_t>(pt.x());
            _map[idx] = replacementTile;
        }
    }
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::mapPaint(const QPoint& coord, int tileIdx, bool mergeable)
{
    if (!isValidMapCoord(coord)) {
        return;
    }
    _undoStack->push(new PaintMapCommand(this, coord, tileIdx, mergeable));
}

void State::_mapPaint(const QPoint& coord, int tileIdx)
{
    if (isValidMapCoord(coord)) {
        const size_t idx = static_cast<size_t>(coord.y()) * static_cast<size_t>(_mapSize.width())
            + static_cast<size_t>(coord.x());
        _map[idx] = static_cast<uint8_t>(static_cast<unsigned int>(tileIdx) & 0xFFU);
        emit mapContentUpdated();
        emit contentsChanged();
    }
}

void State::_applyMapPoints(const QList<QPoint>& coords, uint8_t tileIdx)
{
    bool anyWritten = false;
    const int width = _mapSize.width();
    for (const QPoint& coord : coords) {
        if (isValidMapCoord(coord)) {
            const size_t idx = static_cast<size_t>(coord.y()) * static_cast<size_t>(width)
                + static_cast<size_t>(coord.x());
            _map[idx] = tileIdx;
            anyWritten = true;
        }
    }
    if (anyWritten) {
        emit mapContentUpdated();
        emit contentsChanged();
    }
}

void State::mapClear(int tileIdx)
{
    _undoStack->push(new ClearMapCommand(this, tileIdx));
}

void State::_mapClear(int tileIdx)
{
    const uint8_t fillVal = static_cast<uint8_t>(static_cast<unsigned int>(tileIdx) & 0xFFU);
    std::fill(_map.begin(), _map.end(), fillVal);
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_setMap(const std::vector<uint8_t>& mapCells, const QSize& size)
{
    if (_mapSize != size) {
        return;
    }
    const size_t expected = static_cast<size_t>(size.width()) * static_cast<size_t>(size.height());
    if (mapCells.size() != expected) {
        return;
    }
    _map = mapCells;
    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_fillMapRect(const QRect& bounds, uint8_t value)
{
    const QRect mapBounds(0, 0, _mapSize.width(), _mapSize.height());
    const QRect clipped = bounds.intersected(mapBounds);
    if (clipped.isEmpty()) {
        return;
    }

    const int mapW = _mapSize.width();
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(mapW)
            + static_cast<size_t>(clipped.left());
        std::fill_n(_map.data() + rowBase, static_cast<size_t>(clipped.width()), value);
    }
    emit mapContentUpdated();
    emit contentsChanged();
}

State::MapRegionPatch State::captureMapPatch(const QRect& bounds) const
{
    MapRegionPatch patch;
    const QRect mapBounds(0, 0, _mapSize.width(), _mapSize.height());
    const QRect clipped = bounds.intersected(mapBounds);
    if (clipped.isEmpty()) {
        return patch;
    }

    patch.bounds = clipped;
    const size_t patchW = static_cast<size_t>(clipped.width());
    const size_t patchH = static_cast<size_t>(clipped.height());
    patch.cells.resize(patchW * patchH);

    const size_t mapW = static_cast<size_t>(_mapSize.width());
    for (int row = 0; row < clipped.height(); ++row) {
        const size_t srcOffset = static_cast<size_t>(clipped.top() + row) * mapW
            + static_cast<size_t>(clipped.left());
        const size_t dstOffset = static_cast<size_t>(row) * patchW;
        std::copy_n(_map.data() + srcOffset, patchW, patch.cells.data() + dstOffset);
    }
    return patch;
}

void State::restoreMapPatch(const MapRegionPatch& patch)
{
    if (patch.bounds.isEmpty()) {
        return;
    }
    const QRect mapBounds(0, 0, _mapSize.width(), _mapSize.height());
    if (!mapBounds.contains(patch.bounds)) {
        return;
    }
    const size_t patchW = static_cast<size_t>(patch.bounds.width());
    const size_t patchH = static_cast<size_t>(patch.bounds.height());
    if (patch.cells.size() != patchW * patchH) {
        return;
    }

    const size_t mapW = static_cast<size_t>(_mapSize.width());
    for (int row = 0; row < patch.bounds.height(); ++row) {
        const size_t srcOffset = static_cast<size_t>(row) * patchW;
        const size_t dstOffset = static_cast<size_t>(patch.bounds.top() + row) * mapW
            + static_cast<size_t>(patch.bounds.left());
        std::copy_n(patch.cells.data() + srcOffset, patchW, _map.data() + dstOffset);
    }

    emit mapContentUpdated();
    emit contentsChanged();
}

void State::restoreCharsetAndColors(
    const CharsetBuffer& charsetBuf,
    const TileColorsBuffer& tileColorsBuf)
{
    _charset = charsetBuf;
    for (size_t i = 0; i < _tileColors.size(); ++i) {
        _tileColors[i] = static_cast<uint8_t>(tileColorsBuf[i] & 0x0FU);
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    emit multicolorModeToggled(shouldBeDisplayedInMulticolor2(_tileIndex));
    emit contentsChanged();
}

void State::resetCharsetBuffer() noexcept
{
    _charset.fill(0U);
}

bool State::replaceMap(const QSize& size, std::vector<uint8_t> cells)
{
    const int w = size.width();
    const int h = size.height();
    if (w < 1 || w > kMaxMapDim || h < 1 || h > kMaxMapDim) {
        return false;
    }
    const size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (expected > kMaxMapCells || cells.size() != expected) {
        return false;
    }

    _mapSize = size;
    _map = std::move(cells);
    return true;
}

void State::setCharsetBytes(const uint8_t* data, size_t byteCount) noexcept
{
    _charset.fill(0U);
    if (data != nullptr && byteCount > 0U) {
        const size_t toCopy = std::min(byteCount, _charset.size());
        std::copy_n(data, toCopy, _charset.data());
    }
}

void State::setTileColorsDirect(const uint8_t* data, size_t count) noexcept
{
    _tileColors.fill(TILE_COLORS_DEFAULT);
    if (data != nullptr && count > 0U) {
        const size_t toCopy = std::min(count, _tileColors.size());
        for (size_t i = 0; i < toCopy; ++i) {
            _tileColors[i] = static_cast<uint8_t>(data[i] & 0x0FU);
        }
    }
}

void State::setTileColorDirect(int tileIdx, uint8_t color) noexcept
{
    if (tileIdx >= 0 && tileIdx < TILE_COLORS_BUFFER_SIZE) {
        _tileColors[static_cast<size_t>(tileIdx)] = static_cast<uint8_t>(color & 0x0FU);
    }
}

void State::setPenColorDirect(int pen, uint8_t color) noexcept
{
    if (pen >= 0 && pen < PEN_MAX) {
        _penColors[static_cast<size_t>(pen)] = static_cast<uint8_t>(color & 0x0FU);
    }
}

bool State::assignImportedBuffers(
    const CharsetBuffer& charsetBuf,
    const TileColorsBuffer& tileColorsBuf,
    const QSize& newMapSize,
    std::vector<uint8_t> mapCells)
{
    if (!replaceMap(newMapSize, std::move(mapCells))) {
        return false;
    }
    _charset = charsetBuf;
    for (size_t i = 0; i < _tileColors.size(); ++i) {
        _tileColors[i] = static_cast<uint8_t>(tileColorsBuf[i] & 0x0FU);
    }
    return true;
}

int State::maxTiles() const noexcept
{
    const int area = std::max(1, _tileProperties.size.width() * _tileProperties.size.height());
    return std::max(1, 256 / area);
}

bool State::isValidTileIndex(int tileIndex) const noexcept
{
    return tileIndex >= 0 && tileIndex < maxTiles();
}

void State::copyTileFromIndex(int tileIndex, uint8_t* buffer, int bufferSize) const
{
    if (buffer == nullptr || bufferSize <= 0 || !isValidTileIndex(tileIndex)) {
        return;
    }

    const int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    if (bufferSize < tileSize * 8) {
        return;
    }

    if (_tileProperties.interleaved == 1) {
        const size_t srcBase = static_cast<size_t>(tileIndex * tileSize * 8);
        const size_t bytesToCopy = std::min(
            static_cast<size_t>(tileSize * 8),
            _charset.size() - std::min(srcBase, _charset.size()));
        std::copy_n(_charset.data() + srcBase, bytesToCopy, buffer);
    } else {
        for (int i = 0; i < tileSize; ++i) {
            const int charIdx = tileIndex + i * _tileProperties.interleaved;
            if (charIdx >= 0 && charIdx < 256) {
                std::memcpy(
                    buffer + static_cast<size_t>(i) * 8U,
                    _charset.data() + static_cast<size_t>(charIdx) * 8U,
                    8U);
            }
        }
    }
}

void State::copyTileToIndex(int tileIndex, const uint8_t* buffer, int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 0 || !isValidTileIndex(tileIndex)) {
        return;
    }

    const int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    if (bufferSize < tileSize * 8) {
        return;
    }

    if (_tileProperties.interleaved == 1) {
        const size_t dstBase = static_cast<size_t>(tileIndex * tileSize * 8);
        const size_t bytesToCopy = std::min(
            static_cast<size_t>(tileSize * 8),
            _charset.size() - std::min(dstBase, _charset.size()));
        std::copy_n(buffer, bytesToCopy, _charset.data() + dstBase);
    } else {
        for (int i = 0; i < tileSize; ++i) {
            const int charIdx = tileIndex + i * _tileProperties.interleaved;
            if (charIdx >= 0 && charIdx < 256) {
                std::memcpy(
                    _charset.data() + static_cast<size_t>(charIdx) * 8U,
                    buffer + static_cast<size_t>(i) * 8U,
                    8U);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

int State::getCharIndexFromTileIndex(int tileIndex) const noexcept
{
    if (_tileProperties.interleaved == 1) {
        return tileIndex * (_tileProperties.size.width() * _tileProperties.size.height());
    }
    return tileIndex;
}

int State::getTileIndexFromCharIndex(int charIndex) const noexcept
{
    const int clampedChar = std::clamp(charIndex, 0, 255);
    if (_tileProperties.interleaved == 1) {
        const int area = std::max(1, _tileProperties.size.width() * _tileProperties.size.height());
        return clampedChar / area;
    }
    const int step = std::max(1, _tileProperties.interleaved);
    return clampedChar % step;
}

const uint8_t* State::getCharAtIndex(int charIndex) const
{
    const int clamped = std::clamp(charIndex, 0, 255);
    return _charset.data() + static_cast<size_t>(clamped) * 8U;
}

uint8_t* State::mutableCharAtIndex(int charIndex)
{
    const int clamped = std::clamp(charIndex, 0, 255);
    return _charset.data() + static_cast<size_t>(clamped) * 8U;
}

void State::cut(const CopyRange& copyRange)
{
    if (copyRange.count <= 0 || copyRange.blockSize <= 0) {
        return;
    }
    _undoStack->push(new CutCommand(this, copyRange));
}

void State::paste(int offset, const CopyRange& copyRange, const uint8_t* origBuffer)
{
    if (origBuffer == nullptr || copyRange.count <= 0 || copyRange.blockSize <= 0) {
        return;
    }
    if (copyRange.type == CopyRange::TILES && copyRange.tileProperties.size != _tileProperties.size) {
        qWarning() << "Cannot paste tiles with mismatched dimensions:"
                   << copyRange.tileProperties.size << "vs" << _tileProperties.size;
        return;
    }
    _undoStack->push(new PasteCommand(this, offset, copyRange, origBuffer));
}

void State::_pasteChars(int charIndex, const CopyRange& copyRange, const uint8_t* origBuffer)
{
    if (origBuffer == nullptr || charIndex < 0 || charIndex >= 256
        || copyRange.offset < 0 || copyRange.offset >= 256
        || copyRange.blockSize <= 0 || copyRange.count <= 0) {
        return;
    }

    const bool oldMc = shouldBeDisplayedInMulticolor2(_tileIndex);
    const uint8_t* colorsBuffer = origBuffer + CHAR_BUFFER_SIZE;
    const int stride = copyRange.blockSize + std::max(0, copyRange.skip);

    int dstChar = charIndex;
    int srcChar = copyRange.offset;
    int remaining = copyRange.count;

    while (remaining > 0 && dstChar < 256 && srcChar < 256) {
        const int charsToCopy = std::min({ copyRange.blockSize, 256 - dstChar, 256 - srcChar });
        if (charsToCopy <= 0) {
            break;
        }
        const int bytesToCopy = charsToCopy * 8;
        std::memcpy(
            _charset.data() + static_cast<size_t>(dstChar) * 8U,
            origBuffer + static_cast<size_t>(srcChar) * 8U,
            static_cast<size_t>(bytesToCopy));

        for (int c = 0; c < charsToCopy; ++c) {
            _tileColors[static_cast<size_t>(dstChar + c)] = static_cast<uint8_t>(
                colorsBuffer[static_cast<size_t>(srcChar + c)] & 0x0FU);
        }

        emit bytesUpdated(dstChar * 8, bytesToCopy);

        dstChar += stride;
        srcChar += stride;
        --remaining;
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    const bool newMc = shouldBeDisplayedInMulticolor2(_tileIndex);
    if (oldMc != newMc) {
        emit multicolorModeToggled(newMc);
    }
}

void State::_pasteTiles(int charIndex, const CopyRange& copyRange, const uint8_t* origBuffer)
{
    if (origBuffer == nullptr || charIndex < 0 || charIndex >= 256
        || copyRange.offset < 0 || copyRange.blockSize <= 0 || copyRange.count <= 0) {
        return;
    }

    if (copyRange.tileProperties.size != _tileProperties.size) {
        qWarning() << "Tile paste skipped due to size mismatch:"
                   << copyRange.tileProperties.size << "vs" << _tileProperties.size;
        return;
    }

    const bool oldMc = shouldBeDisplayedInMulticolor2(_tileIndex);
    const uint8_t* colorsBuffer = origBuffer + CHAR_BUFFER_SIZE;

    const int tileSize = copyRange.tileProperties.size.width() * copyRange.tileProperties.size.height();
    if (tileSize <= 0 || tileSize > 256) {
        return;
    }

    const int tileSrcIdx = copyRange.offset;
    const int tileDstIdx = getTileIndexFromCharIndex(charIndex);
    const int interleavedFactorSrc = (copyRange.tileProperties.interleaved == 1) ? tileSize : 1;
    const int interleavedFactorDst = (_tileProperties.interleaved == 1) ? tileSize : 1;
    int srcSkip = 0;
    int dstSkip = 0;
    int remaining = copyRange.count;

    while (remaining > 0) {
        for (int i = 0; i < copyRange.blockSize; ++i) {
            const int srcTile = tileSrcIdx + i + srcSkip;
            const int dstTile = tileDstIdx + i + dstSkip;
            if (srcTile < 0 || srcTile >= 256 || dstTile < 0 || dstTile >= 256) {
                break;
            }

            const int srcIdx = srcTile * interleavedFactorSrc;
            const int dstIdx = dstTile * interleavedFactorDst;

            _tileColors[static_cast<size_t>(dstTile)] = static_cast<uint8_t>(
                colorsBuffer[static_cast<size_t>(srcTile)] & 0x0FU);

            if (_tileProperties.interleaved != 1 && dstIdx >= (256 / tileSize)) {
                remaining = 0;
                break;
            }

            for (int j = 0; j < tileSize; ++j) {
                const int charSrc = (srcIdx + j * copyRange.tileProperties.interleaved) * 8;
                const int charDst = (dstIdx + j * _tileProperties.interleaved) * 8;

                if (charDst >= 0 && (CHAR_BUFFER_SIZE - charDst) >= 8
                    && charSrc >= 0 && (CHAR_BUFFER_SIZE - charSrc) >= 8) {
                    std::memcpy(
                        _charset.data() + static_cast<size_t>(charDst),
                        origBuffer + static_cast<size_t>(charSrc),
                        8U);
                    emit bytesUpdated(charDst, 8);
                }
            }
        }
        srcSkip += std::max(0, copyRange.skip) + copyRange.blockSize;
        dstSkip += std::max(0, copyRange.skip) + copyRange.blockSize;
        --remaining;
    }

    emit charsetUpdated();
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    const bool newMc = shouldBeDisplayedInMulticolor2(_tileIndex);
    if (oldMc != newMc) {
        emit multicolorModeToggled(newMc);
    }
}

void State::_pasteMap(int dstOffset, const CopyRange& copyRange, const uint8_t* origBuffer)
{
    const int dstW = _mapSize.width();
    const int dstH = _mapSize.height();
    if (origBuffer == nullptr || dstW <= 0 || dstH <= 0
        || dstOffset < 0 || copyRange.offset < 0
        || copyRange.blockSize <= 0 || copyRange.count <= 0) {
        return;
    }

    const uint64_t dstTotal = static_cast<uint64_t>(dstW) * static_cast<uint64_t>(dstH);
    if (static_cast<uint64_t>(dstOffset) >= dstTotal) {
        return;
    }

    const int dstX = dstOffset % dstW;
    const int dstY = dstOffset / dstW;
    const int colsToCopy = std::clamp(copyRange.blockSize, 0, dstW - dstX);
    const int rowsToCopy = std::clamp(copyRange.count, 0, dstH - dstY);
    if (colsToCopy <= 0 || rowsToCopy <= 0) {
        return;
    }

    const uint64_t tightCells = static_cast<uint64_t>(copyRange.blockSize)
        * static_cast<uint64_t>(copyRange.count);
    const uint64_t rowWidthCells = (copyRange.sourceRowWidth >= copyRange.blockSize)
        ? (static_cast<uint64_t>(copyRange.sourceRowWidth) * static_cast<uint64_t>(copyRange.count))
        : 0ULL;
    const bool hasExplicitRowWidth = (copyRange.offset == 0 && copyRange.skip <= 0)
        && (rowWidthCells > 0ULL)
        && (copyRange.bufferSize > 0 && static_cast<uint64_t>(copyRange.bufferSize) == rowWidthCells);
    const bool isTightBuffer = (copyRange.offset == 0)
        && ((copyRange.bufferSize > 0 && static_cast<uint64_t>(copyRange.bufferSize) == tightCells)
            || (copyRange.bufferSize <= 0 && copyRange.skip <= 0));
    const int srcStride = hasExplicitRowWidth
        ? copyRange.sourceRowWidth
        : (isTightBuffer
                  ? copyRange.blockSize
                  : (copyRange.blockSize + std::max(0, copyRange.skip)));

    for (int row = 0; row < rowsToCopy; ++row) {
        const uint64_t srcRowOffset = static_cast<uint64_t>(copyRange.offset)
            + static_cast<uint64_t>(row) * static_cast<uint64_t>(srcStride);
        int validCols = colsToCopy;
        if (copyRange.bufferSize > 0) {
            if (srcRowOffset >= static_cast<uint64_t>(copyRange.bufferSize)) {
                break;
            }
            const uint64_t avail = static_cast<uint64_t>(copyRange.bufferSize) - srcRowOffset;
            validCols = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(colsToCopy), avail));
        }
        if (validCols <= 0) {
            break;
        }

        const size_t dstRowOffset = static_cast<size_t>(dstY + row) * static_cast<size_t>(dstW)
            + static_cast<size_t>(dstX);
        std::memcpy(
            _map.data() + dstRowOffset,
            origBuffer + static_cast<size_t>(srcRowOffset),
            static_cast<size_t>(validCols));
    }

    emit mapContentUpdated();
}

void State::_paste(int offset, const CopyRange& copyRange, const uint8_t* origBuffer)
{
    if (copyRange.count <= 0 || copyRange.blockSize <= 0 || origBuffer == nullptr) {
        return;
    }

    switch (copyRange.type) {
    case CopyRange::CHARS:
        _pasteChars(offset, copyRange, origBuffer);
        break;
    case CopyRange::TILES:
        _pasteTiles(offset, copyRange, origBuffer);
        break;
    case CopyRange::MAP:
        _pasteMap(offset, copyRange, origBuffer);
        break;
    }

    emit contentsChanged();
}

bool State::canRotateTile(int tileIndex) const noexcept
{
    if (!isValidTileIndex(tileIndex)) {
        return false;
    }
    // 90-degree in-place rotation requires an N x N character grid.
    if (_tileProperties.size.width() != _tileProperties.size.height()) {
        return false;
    }
    // VIC-II Multicolor glyphs are 4x8 double-wide pixels (2:1 aspect ratio);
    // rotating 4x8 into 8x4 cannot map losslessly back onto 2-bit pixel pairs.
    if (shouldBeDisplayedInMulticolor2(tileIndex)) {
        return false;
    }
    return true;
}

uint8_t State::reverseCharRowBits(uint8_t rowByte, bool isMulticolorTile) noexcept
{
    if (isMulticolorTile) {
        // Swap 2-bit pixel pairs [7:6]<->[1:0] and [5:4]<->[3:2] intact so
        // Multicolor pen codes 01 ($D022) and 10 ($D023) never transpose.
        return static_cast<uint8_t>(
            ((rowByte & 0x03U) << 6U)
            | ((rowByte & 0x0CU) << 2U)
            | ((rowByte & 0x30U) >> 2U)
            | ((rowByte & 0xC0U) >> 6U));
    }

    uint8_t reversed = 0U;
    for (unsigned int bit = 0U; bit < 8U; ++bit) {
        reversed = static_cast<uint8_t>(
            reversed | (((rowByte >> bit) & 0x01U) << (7U - bit)));
    }
    return reversed;
}

void State::tilePaint(int tileIndex, const QPoint& point, int pen, bool mergeable)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new PaintTileCommand(this, tileIndex, point, pen, mergeable));
}

void State::tileInvert(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new InvertTileCommand(this, tileIndex));
}

void State::_tileInvert(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                _charset[charBase + row] = static_cast<uint8_t>(~_charset[charBase + row]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileClear(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new ClearTileCommand(this, tileIndex));
}

void State::_tileClear(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            std::fill_n(_charset.data() + charBase, 8U, uint8_t { 0U });
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileFlipHorizontally(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new FlipTileHCommand(this, tileIndex));
}

void State::_tileFlipHorizontally(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const bool isMcTile = shouldBeDisplayedInMulticolor2(tileIndex);

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                _charset[charBase + row] = reverseCharRowBits(_charset[charBase + row], isMcTile);
            }
        }
    }

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw / 2; ++x) {
            const int leftChar = baseChar + (x + y * tw) * step;
            const int rightChar = baseChar + ((tw - 1 - x) + y * tw) * step;
            if (leftChar < 0 || leftChar >= 256 || rightChar < 0 || rightChar >= 256) {
                continue;
            }
            const size_t leftBase = static_cast<size_t>(leftChar) * 8U;
            const size_t rightBase = static_cast<size_t>(rightChar) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                std::swap(_charset[leftBase + row], _charset[rightBase + row]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileFlipVertically(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new FlipTileVCommand(this, tileIndex));
}

void State::_tileFlipVertically(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            for (size_t row = 0U; row < 4U; ++row) {
                std::swap(_charset[charBase + row], _charset[charBase + (7U - row)]);
            }
        }
    }

    for (int y = 0; y < th / 2; ++y) {
        for (int x = 0; x < tw; ++x) {
            const int topChar = baseChar + (x + y * tw) * step;
            const int bottomChar = baseChar + (x + (th - 1 - y) * tw) * step;
            if (topChar < 0 || topChar >= 256 || bottomChar < 0 || bottomChar >= 256) {
                continue;
            }
            const size_t topBase = static_cast<size_t>(topChar) * 8U;
            const size_t bottomBase = static_cast<size_t>(bottomChar) * 8U;
            for (size_t row = 0U; row < 8U; ++row) {
                std::swap(_charset[topBase + row], _charset[bottomBase + row]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileRotate(int tileIndex)
{
    if (!canRotateTile(tileIndex)) {
        return;
    }
    _undoStack->push(new RotateTileCommand(this, tileIndex));
}

void State::_tileRotate(int tileIndex)
{
    if (!canRotateTile(tileIndex)) {
        return;
    }

    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();

    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            const CharGlyph src = getCharFromTile(tileIndex, x, y);
            CharGlyph rotated {};

            for (unsigned int row = 0U; row < 8U; ++row) {
                for (unsigned int col = 0U; col < 8U; ++col) {
                    if ((src[row] & static_cast<uint8_t>(1U << (7U - col))) != 0U) {
                        rotated[col] = static_cast<uint8_t>(
                            rotated[col] | static_cast<uint8_t>(1U << row));
                    }
                }
            }

            setCharForTile(tileIndex, x, y, rotated);
        }
    }

    if (tw > 1) {
        std::vector<CharGlyph> tmpChars(static_cast<size_t>(tw * th));

        for (int y = 0; y < th; ++y) {
            for (int x = 0; x < tw; ++x) {
                const size_t dstIdx = static_cast<size_t>((tw - 1 - y) + x * tw);
                tmpChars[dstIdx] = getCharFromTile(tileIndex, x, y);
            }
        }

        for (int y = 0; y < th; ++y) {
            for (int x = 0; x < tw; ++x) {
                const size_t srcIdx = static_cast<size_t>(x + y * tw);
                setCharForTile(tileIndex, x, y, tmpChars[srcIdx]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftLeft(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new ShiftLeftTileCommand(this, tileIndex));
}

void State::_tileShiftLeft(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const int shiftPasses = shouldBeDisplayedInMulticolor2(tileIndex) ? 2 : 1;

    for (int pass = 0; pass < shiftPasses; ++pass) {
        for (int y = 0; y < th; ++y) {
            for (size_t row = 0U; row < 8U; ++row) {
                uint8_t leftBit = 0U;
                uint8_t prevLeftBit = 0U;

                for (int x = tw - 1; x >= 0; --x) {
                    const int charIdx = baseChar + (x + y * tw) * step;
                    if (charIdx < 0 || charIdx >= 256) {
                        continue;
                    }
                    uint8_t& cell = _charset[static_cast<size_t>(charIdx) * 8U + row];
                    leftBit = static_cast<uint8_t>((static_cast<unsigned int>(cell) >> 7U) & 0x01U);
                    cell = static_cast<uint8_t>(
                        ((static_cast<unsigned int>(cell) << 1U) & 0xFEU)
                        | static_cast<unsigned int>(prevLeftBit));
                    prevLeftBit = leftBit;
                }

                const int rightmostChar = baseChar + ((tw - 1) + y * tw) * step;
                if (rightmostChar >= 0 && rightmostChar < 256) {
                    uint8_t& cell = _charset[static_cast<size_t>(rightmostChar) * 8U + row];
                    cell = static_cast<uint8_t>(
                        (static_cast<unsigned int>(cell) & 0xFEU)
                        | static_cast<unsigned int>(leftBit));
                }
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftRight(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new ShiftRightTileCommand(this, tileIndex));
}

void State::_tileShiftRight(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;
    const int shiftPasses = shouldBeDisplayedInMulticolor2(tileIndex) ? 2 : 1;

    for (int pass = 0; pass < shiftPasses; ++pass) {
        for (int y = 0; y < th; ++y) {
            for (size_t row = 0U; row < 8U; ++row) {
                uint8_t rightBit = 0U;
                uint8_t prevRightBit = 0U;

                for (int x = 0; x < tw; ++x) {
                    const int charIdx = baseChar + (x + y * tw) * step;
                    if (charIdx < 0 || charIdx >= 256) {
                        continue;
                    }
                    uint8_t& cell = _charset[static_cast<size_t>(charIdx) * 8U + row];
                    rightBit = static_cast<uint8_t>(static_cast<unsigned int>(cell) & 0x01U);
                    cell = static_cast<uint8_t>(
                        ((static_cast<unsigned int>(cell) >> 1U) & 0x7FU)
                        | (static_cast<unsigned int>(prevRightBit) << 7U));
                    prevRightBit = rightBit;
                }

                const int leftmostChar = baseChar + (0 + y * tw) * step;
                if (leftmostChar >= 0 && leftmostChar < 256) {
                    uint8_t& cell = _charset[static_cast<size_t>(leftmostChar) * 8U + row];
                    cell = static_cast<uint8_t>(
                        (static_cast<unsigned int>(cell) & 0x7FU)
                        | (static_cast<unsigned int>(rightBit) << 7U));
                }
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftUp(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new ShiftUpTileCommand(this, tileIndex));
}

void State::_tileShiftUp(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int x = 0; x < tw; ++x) {
        uint8_t prevTopByte = 0U;

        for (int y = th - 1; y >= 0; --y) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            const uint8_t topByte = _charset[charBase];

            for (size_t row = 0U; row < 7U; ++row) {
                _charset[charBase + row] = _charset[charBase + row + 1U];
            }

            _charset[charBase + 7U] = prevTopByte;
            prevTopByte = topByte;
        }

        const int bottomChar = baseChar + (x + (th - 1) * tw) * step;
        if (bottomChar >= 0 && bottomChar < 256) {
            _charset[static_cast<size_t>(bottomChar) * 8U + 7U] = prevTopByte;
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftDown(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    _undoStack->push(new ShiftDownTileCommand(this, tileIndex));
}

void State::_tileShiftDown(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }

    const int baseChar = getCharIndexFromTileIndex(tileIndex);
    const int tw = _tileProperties.size.width();
    const int th = _tileProperties.size.height();
    const int step = _tileProperties.interleaved;

    for (int x = 0; x < tw; ++x) {
        uint8_t prevBottomByte = 0U;

        for (int y = 0; y < th; ++y) {
            const int charIdx = baseChar + (x + y * tw) * step;
            if (charIdx < 0 || charIdx >= 256) {
                continue;
            }
            const size_t charBase = static_cast<size_t>(charIdx) * 8U;
            const uint8_t bottomByte = _charset[charBase + 7U];

            for (size_t row = 7U; row > 0U; --row) {
                _charset[charBase + row] = _charset[charBase + row - 1U];
            }

            _charset[charBase] = prevBottomByte;
            prevBottomByte = bottomByte;
        }

        const int topChar = baseChar + x * step;
        if (topChar >= 0 && topChar < 256) {
            _charset[static_cast<size_t>(topChar) * 8U] = prevBottomByte;
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::setCharIndex(int charIndex)
{
    if (charIndex < 0 || charIndex >= 256) {
        return;
    }
    if (_charIndex != charIndex) {
        _setCharIndex(charIndex);
        const int tileIdx = getTileIndexFromCharIndex(charIndex);
        _setTileIndex(tileIdx);
    }
}

void State::_setCharIndex(int charIndex)
{
    if (_charIndex != charIndex) {
        _charIndex = charIndex;
        emit charIndexUpdated(_charIndex);
    }
}

void State::setTileIndex(int tileIndex)
{
    if (!isValidTileIndex(tileIndex)) {
        return;
    }
    if (_tileIndex != tileIndex) {
        _setTileIndex(tileIndex);
        const int charIdx = getCharIndexFromTileIndex(tileIndex);
        _setCharIndex(charIdx);
    }
}

void State::_setTileIndex(int tileIndex)
{
    if (_tileIndex != tileIndex && tileIndex >= 0 && tileIndex < TILE_COLORS_BUFFER_SIZE) {
        const int oldTileIndex = std::clamp(_tileIndex, 0, TILE_COLORS_BUFFER_SIZE - 1);
        _tileIndex = tileIndex;

        if (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE
            && _tileColors[static_cast<size_t>(tileIndex)] != _tileColors[static_cast<size_t>(oldTileIndex)]) {
            emit colorPropertiesUpdated(PEN_FOREGROUND);
        }

        emit tileIndexUpdated(tileIndex);
    }
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

State::CharGlyph State::getCharFromTile(int tileIndex, int x, int y) const
{
    CharGlyph ret {};
    const int charIdx = getCharIndexFromTileIndex(tileIndex)
        + (x + y * _tileProperties.size.width()) * _tileProperties.interleaved;
    if (charIdx < 0 || charIdx >= 256) {
        return ret;
    }

    const size_t base = static_cast<size_t>(charIdx) * 8U;
    for (size_t row = 0U; row < 8U; ++row) {
        ret[row] = _charset[base + row];
    }
    return ret;
}

void State::setCharForTile(int tileIndex, int x, int y, const CharGlyph& chr)
{
    const int charIdx = getCharIndexFromTileIndex(tileIndex)
        + (x + y * _tileProperties.size.width()) * _tileProperties.interleaved;
    if (charIdx < 0 || charIdx >= 256) {
        return;
    }

    const size_t base = static_cast<size_t>(charIdx) * 8U;
    for (size_t row = 0U; row < 8U; ++row) {
        _charset[base + row] = chr[row];
    }
}

void State::setupDefaultMap()
{
    std::fill(_map.begin(), _map.end(), uint8_t { 0x20U });

    static constexpr char kHello64[] = "                                        "
                                       "    **** COMMODORE 64 BASIC V2 ****     "
                                       "                                        "
                                       " 64K RAM SYSTEM  38911 BASIC BYTES FREE ";

    static constexpr char kHello128[] = "                                        "
                                        " COMMODORE BASIC V7.0 122365 BYTES FREE "
                                        "   (C)1986 COMMODORE ELECTRONICS, LTD.  "
                                        "         (C)1977 MICROSOFT CORP.        "
                                        "           ALL RIGHTS RESERVED          ";

    struct Banner {
        const char* text;
        size_t length;
    };
    static constexpr std::array<Banner, 2> kHellos = { {
        { kHello64, sizeof(kHello64) - 1U },
        { kHello128, sizeof(kHello128) - 1U },
    } };

    QRandomGenerator prng(static_cast<quint32>(QTime::currentTime().msec()));
    const size_t helloIdx = static_cast<size_t>(prng.generate() % 2U);
    const size_t copyLen = std::min(kHellos[helloIdx].length, _map.size());

    // Convert ASCII banner characters to C64 screen codes.
    for (size_t i = 0U; i < copyLen; ++i) {
        const auto ch = static_cast<uint8_t>(kHellos[helloIdx].text[i]);
        _map[i] = static_cast<uint8_t>(ch & static_cast<uint8_t>(~0x40U));
    }
}
