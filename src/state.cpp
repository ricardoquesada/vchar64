/****************************************************************************
Copyright 2015 Ricardo Quesada

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

#include "state.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QTime>
#include <QtGlobal>

#include "commands.h"
#include "mainwindow.h"
#include "mapwidget.h"
#include "palette.h"
#include "stateexport.h"
#include "stateimport.h"
#include "tilesetwidget.h"

constexpr int State::CHAR_BUFFER_SIZE;
constexpr quint8 State::TILE_COLORS_DEFAULT;

// target constructor
State::State(const QString& filename,
    const QSize& mapSize)
    : _totalChars(0)
    , _mapSize(mapSize)
    , _multicolorMode(false)
    , _foregroundColorMode(FOREGROUND_COLOR_GLOBAL)
    , _selectedPen(PEN_FOREGROUND)
    , _penColors { 1, 5, 7, 11 }
    , _tileProperties { { 1, 1 }, 1 }
    , _charIndex(0)
    , _tileIndex(0)
    , _loadedFilename(filename)
    , _savedFilename("")
    , _exportedFilename("")
    , _exportProperties({ { 0x3800, 0x4000, 0x4400 }, EXPORT_FORMAT_RAW, EXPORT_FEATURE_CHARSET })
    , _undoStack(nullptr)
    , _bigCharWidget(nullptr)
    , _keyboardMapping(KEYBOARD_MAPPING_C64)
{
    _undoStack = new QUndoStack;

    Q_ASSERT(_mapSize.width() > 0 && _mapSize.height() > 0 && "Invalid size");
    _map.resize(_mapSize.width() * _mapSize.height());
    setupDefaultMap();
    Q_ASSERT(std::size(_map) == _mapSize.width() * _mapSize.height());

    std::fill_n(std::begin(_charset), std::size(_charset), 0);
    std::fill_n(std::begin(_tileColors), std::size(_tileColors), TILE_COLORS_DEFAULT);
}

// Delegating constructor

State::State(const QString& filename)
    : State(filename, QSize(40, 25))
{
}

State::State()
    : State("", QSize(40, 25))
{
}

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

    std::memcpy(_penColors, copyFromMe._penColors, std::size(_penColors));

    std::memcpy(_charset, copyFromMe._charset, std::size(_charset));
    std::memcpy(_tileColors, copyFromMe._tileColors, std::size(_tileColors));
    _map = copyFromMe._map;
}

State::~State()
{
    delete _undoStack;
}

void State::reset()
{
    _totalChars = 0;
    _multicolorMode = false;
    _foregroundColorMode = FOREGROUND_COLOR_GLOBAL;
    _selectedPen = PEN_FOREGROUND;
    _penColors[PEN_BACKGROUND] = 1;
    _penColors[PEN_MULTICOLOR1] = 5;
    _penColors[PEN_MULTICOLOR2] = 7;
    _penColors[PEN_FOREGROUND] = 11;
    _tileProperties.size = { 1, 1 };
    _tileProperties.interleaved = 1;
    _loadedFilename = "untitled";
    _savedFilename = "";
    _exportedFilename = "";
    _exportProperties.addresses[0] = 0x3800;
    _exportProperties.addresses[1] = 0x4000;
    _exportProperties.addresses[2] = 0x4400;
    _exportProperties.format = EXPORT_FORMAT_RAW;
    _exportProperties.features = EXPORT_FEATURE_CHARSET;
    _keyboardMapping = KEYBOARD_MAPPING_C64;

    std::fill_n(std::begin(_charset), std::size(_charset), 0);
    std::fill_n(std::begin(_tileColors), std::size(_tileColors), TILE_COLORS_DEFAULT);
    std::fill_n(std::begin(_map), std::size(_map), 0);
}

void State::emitNewState()
{
    emit fileLoaded();
    emit contentsChanged();
}

bool State::isModified() const
{
    return (!getUndoStack()->isClean());
}

bool State::openFile(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    qint64 length = 0;
    quint16 loadedAddress = 0;

    enum {
        FILETYPE_VCHAR64,
        FILETYPE_PRG,
        FILETYPE_RAW,
        FILETYPE_CTM
    };
    int filetype = FILETYPE_RAW;

    QFileInfo info(file);
    QString suffix = info.suffix().toLower();

    if (suffix == "vchar64proj") {
        length = StateImport::loadVChar64(this, file);
        filetype = FILETYPE_VCHAR64;
    } else if ((suffix == "64c") || (suffix == "prg")) {
        length = StateImport::loadPRG(this, file, &loadedAddress);
        filetype = FILETYPE_PRG;
    } else if (suffix == "ctm") {
        length = StateImport::loadCTM(this, file);
        filetype = FILETYPE_CTM;
    } else {
        if (suffix == "bin") {
            // do nothing
        } else if (suffix == "fnt") {
            qDebug() << "FNT extension. Assuming it uses Atari 8-bit keyboard mappings";
            _keyboardMapping = KEYBOARD_MAPPING_ATARI8;
        } else {
            qDebug() << "Unkwnow file extension: " << suffix << ". Treating it as binary.";
        }
        length = StateImport::loadRaw(this, file);
        filetype = FILETYPE_RAW;
    }

    file.close();

    if (length <= 0)
        return false;

    // built-in resources are not saved
    if (filename[0] != ':') {
        _loadedFilename = filename;

        if (filetype == FILETYPE_VCHAR64) {
            _savedFilename = filename;
        } else if (filetype == FILETYPE_RAW || filetype == FILETYPE_PRG) {
            _exportedFilename = filename;
            _exportProperties.features = EXPORT_FEATURE_CHARSET;
            if (filetype == FILETYPE_PRG) {
                _exportProperties.addresses[0] = loadedAddress;
                _exportProperties.format = EXPORT_FORMAT_PRG;
            } else
                _exportProperties.format = EXPORT_FORMAT_RAW;
        }
    }

    return true;
}

static QString filenameFixSuffix(const QString& filename, State::ExportFeature suffix)
{
    QString validDescriptions[] = {
        "-charset",
        "-map",
        "-colors"
    };

    QFileInfo info(filename);

    auto extension = info.suffix();
    auto filepath = info.path();
    auto name = info.baseName();

    // remove possible suffix
    for (const auto& validDescription : validDescriptions) {
        if (name.endsWith(validDescription)) {
            name = name.left(name.length() - validDescription.length());
            break;
        }
    }

    // add suffix
    if (suffix == State::EXPORT_FEATURE_CHARSET)
        name.append(validDescriptions[0]);
    else if (suffix == State::EXPORT_FEATURE_MAP)
        name.append(validDescriptions[1]);
    else /* map */
        name.append(validDescriptions[2]);

    return filepath + "/" + name + "." + extension;
}

bool State::export_()
{
    Q_ASSERT(_exportedFilename.length() > 0 && "Invalid filename");

    bool ret = false;
    if (_exportProperties.format == EXPORT_FORMAT_RAW)
        ret = exportRaw(_exportedFilename, _exportProperties);

    /* else */
    else if (_exportProperties.format == EXPORT_FORMAT_PRG)
        ret = exportPRG(_exportedFilename, _exportProperties);

    /* else ASM */
    else
        ret = exportAsm(_exportedFilename, _exportProperties);

    if (ret) {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("Export: Ok"));
    } else {
        // beep on error
        MainWindow::getInstance()->showMessageOnStatusBar(tr("Export: Error"));
        QApplication::beep();
    }

    return ret;
}

bool State::exportRaw(const QString& filename, const ExportProperties& properties)
{
    quint64 bytesWritten;

    if (properties.features & EXPORT_FEATURE_CHARSET) {
        bytesWritten = StateExport::saveRaw(filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
            _charset, std::size(_charset));
        if (bytesWritten <= 0) {
            qDebug() << "Failed to export Charset to Raw";
            return false;
        }
    }

    if (properties.features & EXPORT_FEATURE_MAP) {
        bytesWritten = StateExport::saveRaw(filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
            _map.data(), std::size(_map));
        if (bytesWritten <= 0) {
            qDebug() << "Failed to export Map to Raw";
            return false;
        }
    }

    if (properties.features & EXPORT_FEATURE_COLORS) {
        bytesWritten = StateExport::saveRaw(filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
            _tileColors, std::size(_tileColors));
        if (bytesWritten <= 0) {
            qDebug() << "Failed to export Colors to Raw";
            return false;
        }
    }

    _exportedFilename = filename;
    auto copy = properties;
    copy.format = EXPORT_FORMAT_RAW;
    setExportProperties(copy);

    return true;
}

bool State::exportPRG(const QString& filename, const ExportProperties& properties)
{
    bool ret = true;

    if (ret && (properties.features & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::savePRG(filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
                    _charset, std::size(_charset), properties.addresses[0])
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_MAP))
        ret &= (StateExport::savePRG(filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
                    _map.data(), std::size(_map), properties.addresses[1])
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_COLORS))
        ret &= (StateExport::savePRG(filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
                    _tileColors, std::size(_tileColors), properties.addresses[2])
            > 0);

    if (ret) {
        _exportedFilename = filename;
        auto copy = properties;
        copy.format = EXPORT_FORMAT_PRG;
        setExportProperties(copy);
        return true;
    }
    return ret;
}

bool State::exportAsm(const QString& filename, const ExportProperties& properties)
{
    bool ret = true;
    if (ret && (properties.features & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::saveAsm(filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
                    _charset, std::size(_charset), "charset")
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_MAP))
        ret &= (StateExport::saveAsm(filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
                    _map.data(), std::size(_map), "map")
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_COLORS))
        ret &= (StateExport::saveAsm(filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
                    _tileColors, std::size(_tileColors), "colors")
            > 0);

    if (ret) {
        _exportedFilename = filename;
        auto copy = properties;
        copy.format = EXPORT_FORMAT_ASM;
        setExportProperties(copy);
    }
    return ret;
}

bool State::exportC(const QString& filename, const ExportProperties& properties)
{
    bool ret = true;
    if (ret && (properties.features & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::saveC(filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET),
                    _charset, std::size(_charset), "charset")
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_MAP))
        ret &= (StateExport::saveC(filenameFixSuffix(filename, EXPORT_FEATURE_MAP),
                    _map.data(), std::size(_map), "map")
            > 0);

    if (ret && (properties.features & EXPORT_FEATURE_COLORS))
        ret &= (StateExport::saveC(filenameFixSuffix(filename, EXPORT_FEATURE_COLORS),
                    _tileColors, std::size(_tileColors), "colors")
            > 0);

    if (ret) {
        _exportedFilename = filename;
        auto copy = properties;
        // FIXME: Why is this needed?
        copy.format = EXPORT_FORMAT_C;
        setExportProperties(copy);
    }
    return ret;
}

bool State::exportPNG(const QString& filename, TilesetWidget* tilesetWidget, MapWidget* mapWidget)
{
    bool ret = true;

    if (tilesetWidget) {
        ret &= (StateExport::savePNG(filenameFixSuffix(filename, EXPORT_FEATURE_CHARSET), tilesetWidget->renderToQImage(), this) > 0);
    }

    if (mapWidget) {
        ret &= (StateExport::savePNG(filenameFixSuffix(filename, EXPORT_FEATURE_MAP), mapWidget->renderToQImage(), this) > 0);
    }

    return ret;
}

bool State::saveProject(const QString& filename)
{
    bool ret = false;

    // don't save it nothing has changed. Same behavior as Qt Creator
    if (getUndoStack()->isClean() && _savedFilename == filename) {
        // clean, nothing to save
        QApplication::beep();
        // hack: return true so that mainWindow doesn't treat it as an error
        return true;
    }

    QFile file(filename);
    ret = file.open(QIODevice::WriteOnly | QIODevice::Truncate);

    if (ret) {
        qint64 length = 0;
        length = StateExport::saveVChar64(this, file);
        file.close();

        ret = (length > 0);
        if (ret) {
            _loadedFilename = _savedFilename = filename;
            getUndoStack()->setClean();
            emit contentsChanged();
        }
    }

    if (ret) {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("Save: Ok"));
    } else {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("Save: Error"));
        // beep on error
        QApplication::beep();
    }

    return ret;
}

//
bool State::shouldBeDisplayedInMulticolor() const
{
    // display char as multicolor only if multicolor is enabled
    // and foreground color is >= 8
    // Use shouldBeDisplayedInMulticolor2() for a more reliable way to test it
    return (_multicolorMode && _penColors[State::PEN_FOREGROUND] >= 8);
}

bool State::shouldBeDisplayedInMulticolor2(int tileIdx) const
{
    Q_ASSERT(((tileIdx == -1 && _foregroundColorMode == FOREGROUND_COLOR_GLOBAL)
                 || (tileIdx >= 0 && tileIdx < 256))
        && "Invalid tileIdx");

    return (_multicolorMode
        && ((_foregroundColorMode == FOREGROUND_COLOR_GLOBAL && _penColors[PEN_FOREGROUND] >= 8)
            || (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE && (_tileColors[tileIdx] & 0xf) >= 8)));
}

bool State::isMulticolorMode() const
{
    return _multicolorMode;
}

void State::setMulticolorMode(bool enabled)
{
    getUndoStack()->push(new SetMulticolorModeCommand(this, enabled));
}

void State::_setMulticolorMode(bool enabled)
{
    if (_multicolorMode != enabled) {
        _multicolorMode = enabled;

        emit multicolorModeToggled(enabled);
        emit contentsChanged();
    }
}

void State::setForegroundColorMode(State::ForegroundColorMode mode)
{
    getUndoStack()->push(new SetForegroundColorMode(this, mode));
}

void State::_setForegroundColorMode(ForegroundColorMode mode)
{
    if (_foregroundColorMode != mode) {
        _foregroundColorMode = mode;
        emit colorPropertiesUpdated(PEN_FOREGROUND);
        emit contentsChanged();
    }
}

State::ForegroundColorMode State::getForegroundColorMode() const
{
    return _foregroundColorMode;
}

int State::getColorForPen(int pen) const
{
    return getColorForPen(pen, -1);
}

int State::getColorForPen(int pen, int tileIdx) const
{
    Q_ASSERT(pen >= 0 && pen < PEN_MAX);

    bool foregroundAndPerTile = (pen == PEN_FOREGROUND
        && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);

    Q_ASSERT((!foregroundAndPerTile || tileIdx != -1) && "Provide a valid tileIdx");

    if (foregroundAndPerTile)
        // making travis happy: -Werror=array-bounds
        return _tileColors[(quint8)tileIdx];
    return _penColors[pen];
}

void State::setColorForPen(int pen, int color)
{
    setColorForPen(pen, color, -1);
}

void State::setColorForPen(int pen, int color, int tileIdx)
{
    getUndoStack()->push(new SetColorCommand(this, color, pen, tileIdx));
}

void State::_setColorForPen(int pen, int color, int tileIdx)
{
    Q_ASSERT(pen >= 0 && pen < PEN_MAX);
    Q_ASSERT(color >= 0 && color < 16);

    bool foregroundAndPerTile = (pen == PEN_FOREGROUND && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);

    Q_ASSERT((tileIdx != -1 || !foregroundAndPerTile) && "Invalid Tile index");

    int currentColor = foregroundAndPerTile ? _tileColors[tileIdx] : _penColors[pen];

    if (currentColor != color) {
        bool oldvalue = shouldBeDisplayedInMulticolor2(tileIdx);

        if (foregroundAndPerTile)
            _tileColors[tileIdx] = color;
        else
            _penColors[pen] = color;

        bool newvalue = shouldBeDisplayedInMulticolor2(tileIdx);

        emit colorPropertiesUpdated(pen);

        if (oldvalue != newvalue)
            emit multicolorModeToggled(newvalue);

        emit contentsChanged();
    }
}

int State::getCurrentColor() const
{
    return getColorForPen(_selectedPen, -1);
}

int State::getSelectedPen() const
{
    return _selectedPen;
}

void State::setSelectedPen(int pen)
{
    Q_ASSERT(pen >= 0 && pen < PEN_MAX);

    if (pen != _selectedPen) {
        _selectedPen = pen;
        emit selectedPenChaged(pen);
    }
}

int State::tileGetPen(int tileIndex, const QPoint& position)
{
    Q_ASSERT(tileIndex >= 0
        && tileIndex <= getTileIndexFromCharIndex(255)
        && "invalid index value");
    Q_ASSERT(position.x() < State::MAX_TILE_WIDTH * 8
        && position.y() < State::MAX_TILE_HEIGHT * 8
        && "Invalid position");

    int x = position.x();
    int y = position.y();
    int bitIndex = (x % 8) + (y % 8) * 8;
    int charIndex = getCharIndexFromTileIndex(tileIndex)
        + (x / 8) * _tileProperties.interleaved
        + (y / 8) * _tileProperties.interleaved * _tileProperties.size.width();

    quint8 c = _charset[charIndex * 8 + bitIndex / 8];
    quint8 b = bitIndex % 8;

    int ret = 0;

    // not multicolor: expected result: 0 or 1
    if (!shouldBeDisplayedInMulticolor2(tileIndex)) {
        quint8 mask = 1 << (7 - b);
        ret = (c & mask) >> (7 - b);
    } else {
        quint8 masks[] = { 192, 192, 48, 48, 12, 12, 3, 3 };
        quint8 mask = masks[b];

        // ignore "odd" numbers when... valid bits to shift: 6,4,2,0
        ret = (c & mask) >> ((7 - b) & 254);
    }

    Q_ASSERT(ret >= 0 && ret < PEN_MAX);
    return ret;
}

void State::_tileSetPen(int tileIndex, const QPoint& position, int pen)
{
    Q_ASSERT(tileIndex >= 0
        && tileIndex <= getTileIndexFromCharIndex(255)
        && "invalid index value");
    Q_ASSERT(position.x() < State::MAX_TILE_WIDTH * 8
        && position.y() < State::MAX_TILE_HEIGHT * 8
        && "Invalid position");
    Q_ASSERT(pen >= 0
        && pen < PEN_MAX
        && "Invalid pen. range: 0,4");

    int x = position.x();
    int y = position.y();
    int bitIndex = (x % 8) + (y % 8) * 8;
    int charIndex = getCharIndexFromTileIndex(tileIndex)
        + (x / 8) * _tileProperties.interleaved
        + (y / 8) * _tileProperties.interleaved * _tileProperties.size.width();

    int byteIndex = charIndex * 8 + bitIndex / 8;

    quint8 b = bitIndex % 8;
    quint8 and_mask = 0x00;
    quint8 or_mask = 0x00;
    if (!shouldBeDisplayedInMulticolor2(tileIndex)) {
        and_mask = 1 << (7 - b);
        or_mask = pen << (7 - b);
    } else {
        quint8 masks[] = { 128 + 64, 128 + 64, 32 + 16, 32 + 16, 8 + 4, 8 + 4, 2 + 1, 2 + 1 };
        and_mask = masks[b];
        or_mask = pen << ((7 - b) & 254);
    }

    // get the neede byte
    quint8 c = _charset[byteIndex];

    c &= ~and_mask;
    c |= or_mask;

    if (c != _charset[byteIndex]) {
        _charset[byteIndex] = c;

        emit tileUpdated(tileIndex);
        emit contentsChanged();
    }
}

// tile properties
void State::setTileProperties(const TileProperties& properties)
{
    getUndoStack()->push(new SetTilePropertiesCommand(this, properties));
}

void State::_setTileProperties(const TileProperties& properties)
{
    if (memcmp(&_tileProperties, &properties, sizeof(_tileProperties)) != 0) {
        _tileProperties = properties;

        emit tilePropertiesUpdated();
        emit contentsChanged();
    }
}
State::TileProperties State::getTileProperties() const
{
    return _tileProperties;
}

// export properties
void State::setExportProperties(const ExportProperties& properties)
{
    // Special case for export properties:
    // Only submit command to the Undo Stack if it is different that the current properties
    // This is in order to avoid generating a "dirty" signal, when in fact it is not
    if (memcmp(&_exportProperties, &properties, sizeof(_exportProperties)) != 0) {
        getUndoStack()->push(new SetExportPropertiesCommand(this, properties));
    }
}

void State::_setExportProperties(const ExportProperties& properties)
{
    if (memcmp(&_exportProperties, &properties, sizeof(_exportProperties)) != 0) {
        _exportProperties = properties;

        emit contentsChanged();
    }
}

State::ExportProperties State::getExportProperties() const
{
    return _exportProperties;
}

void State::setMapSize(const QSize& mapSize)
{
    getUndoStack()->push(new SetMapSizeCommand(this, mapSize));
}

void State::_setMapSize(const QSize& mapSize)
{
    if (_mapSize != mapSize) {
        const int newSizeInBytes = mapSize.width() * mapSize.height();
        std::vector<quint8> newMap(newSizeInBytes);

        for (int i = 0; i < newSizeInBytes; ++i)
            newMap[i] = _tileIndex;

        for (int row = 0; row < mapSize.height(); ++row) {
            // no more rows to copy
            if (row >= _mapSize.height())
                break;

            // bytes to copy per row
            int toCopy = qMin(mapSize.width(), _mapSize.width());
            std::memcpy(&newMap[mapSize.width() * row], &_map[_mapSize.width() * row], toCopy);
        }

        _map = newMap;
        _mapSize = mapSize;

        emit mapSizeUpdated();
        emit contentsChanged();
    }
}

const QSize& State::getMapSize() const
{
    return _mapSize;
}

int State::getTileIndexFromMap(const QPoint& mapCoord) const
{
    Q_ASSERT(mapCoord.x() < _mapSize.width() && mapCoord.y() < _mapSize.height() && "Invalid map size");
    int tileIndex = _map[_mapSize.width() * mapCoord.y() + mapCoord.x()];

    // safety check: map could have tiles bigger than the maximum supported if the tiles were resized.
    tileIndex = qBound(0, tileIndex, 256 / (_tileProperties.size.width() * _tileProperties.size.height()) - 1);
    return tileIndex;
}

void State::floodFillImpl(const QPoint& coord, int targetTile, int newTile)
{
    if (coord.x() < 0 || coord.x() >= _mapSize.width())
        return;
    if (coord.y() < 0 || coord.y() >= _mapSize.height())
        return;
    if (_map[coord.y() * _mapSize.width() + coord.x()] != targetTile)
        return;

    _map[coord.y() * _mapSize.width() + coord.x()] = newTile;

    floodFillImpl(QPoint(coord.x() - 1, coord.y()), targetTile, newTile);
    floodFillImpl(QPoint(coord.x() + 1, coord.y()), targetTile, newTile);
    floodFillImpl(QPoint(coord.x(), coord.y() - 1), targetTile, newTile);
    floodFillImpl(QPoint(coord.x(), coord.y() + 1), targetTile, newTile);
}

void State::mapFill(const QPoint& coord, int tileIdx)
{
    getUndoStack()->push(new FillMapCommand(this, coord, tileIdx));
}

void State::_mapFill(const QPoint& coord, int tileIdx)
{
    if (coord.x() < _mapSize.width() && coord.y() < _mapSize.height()) {
        int targetTile = _map[coord.y() * _mapSize.width() + coord.x()];

        if (targetTile != tileIdx) {
            floodFillImpl(coord, targetTile, tileIdx);
            emit mapContentUpdated();
            emit contentsChanged();
        }
    }
}

void State::mapPaint(const QPoint& coord, int tileIdx, bool mergeable)
{
    getUndoStack()->push(new PaintMapCommand(this, coord, tileIdx, mergeable));
}

void State::_mapPaint(const QPoint& coord, int tileIdx)
{
    if (coord.x() < _mapSize.width() && coord.y() < _mapSize.height()) {
        _map[coord.y() * _mapSize.width() + coord.x()] = tileIdx;
        emit mapContentUpdated();
        emit contentsChanged();
    }
}

void State::mapClear(int tileIdx)
{
    getUndoStack()->push(new ClearMapCommand(this, tileIdx));
}

void State::_mapClear(int tileIdx)
{
    std::fill(std::begin(_map), std::end(_map), tileIdx);

    emit mapContentUpdated();
    emit contentsChanged();
}

void State::_setMap(const std::vector<quint8>& map, const QSize& mapSize)
{
    Q_ASSERT(_mapSize == mapSize && "Invalid map size");
    _map = map;

    emit mapContentUpdated();
    emit contentsChanged();
}

// charset methods
const State::charset_t& State::getCharsetBuffer() const
{
    return _charset;
}

const State::map_t& State::getMapBuffer() const
{
    return _map;
}

const State::tileColors_t& State::getTileColors() const
{
    return _tileColors;
}

State::charset_t& State::getCharsetBuffer()
{
    return _charset;
}

State::map_t& State::getMapBuffer()
{
    return _map;
}

State::tileColors_t& State::getTileColors()
{
    return _tileColors;
}

void State::resetCharsetBuffer()
{
    std::fill_n(std::begin(_charset), std::size(_charset), 0);
}

// buffer must be at least 8x8*8 bytes big
void State::copyTileFromIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    Q_ASSERT(bufferSize >= (tileSize * 8) && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex >= 0 && tileIndex <= getTileIndexFromCharIndex(255) && "invalid index value");

    if (_tileProperties.interleaved == 1) {
        std::memcpy(buffer, &_charset[tileIndex * tileSize * 8], qMin(tileSize * 8, bufferSize));
    } else {
        for (int i = 0; i < tileSize; i++) {
            std::memcpy(&buffer[i * 8], &_charset[(tileIndex + i * _tileProperties.interleaved) * 8], 8);
        }
    }
}

// size-of-tile chars will be copied
void State::copyTileToIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    Q_ASSERT(bufferSize >= (tileSize * 8) && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex >= 0 && tileIndex <= getTileIndexFromCharIndex(255) && "invalid index value");

    if (_tileProperties.interleaved == 1) {
        std::memcpy(&_charset[tileIndex * tileSize * 8], buffer, qMin(tileSize * 8, bufferSize));
    } else {
        for (int i = 0; i < tileSize; i++) {
            std::memcpy(&_charset[(tileIndex + i * _tileProperties.interleaved) * 8], &buffer[i * 8], 8);
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

// helper functions
int State::getCharIndexFromTileIndex(int tileIndex) const
{
    int charIndex = -1;
    if (_tileProperties.interleaved == 1) {
        charIndex = tileIndex * (_tileProperties.size.width() * _tileProperties.size.height());
    } else {
        charIndex = tileIndex;
    }
    return charIndex;
}

int State::getTileIndexFromCharIndex(int charIndex) const
{
    Q_ASSERT(charIndex >= 0 && charIndex < 256);

    int tileIndex = -1;
    if (_tileProperties.interleaved == 1) {
        tileIndex = charIndex / (_tileProperties.size.width() * _tileProperties.size.height());
    } else {
        tileIndex = charIndex % _tileProperties.interleaved;
    }

    return tileIndex;
}

quint8* State::getCharAtIndex(int charIndex)
{
    Q_ASSERT(charIndex >= 0 && charIndex < 256 && "Invalid index");
    return &_charset[charIndex * 8];
}

void State::cut(const CopyRange& copyRange)
{
    getUndoStack()->push(new CutCommand(this, copyRange));
}

void State::paste(int offset, const CopyRange& copyRange, const quint8* origBuffer)
{
    getUndoStack()->push(new PasteCommand(this, offset, copyRange, origBuffer));
}

void State::_pasteChars(int charIndex, const CopyRange& copyRange, const quint8* origBuffer)
{
    Q_ASSERT(charIndex >= 0 && charIndex < CHAR_BUFFER_SIZE && "Invalid charIndex size");

    int count = copyRange.count;

    quint8* chrdst = _charset + (charIndex * 8);
    quint8* colordst = _tileColors + charIndex;

    const quint8* colorsBuffer = origBuffer + CHAR_BUFFER_SIZE;
    const quint8* chrsrc = origBuffer + (copyRange.offset * 8);
    const quint8* colorsrc = colorsBuffer + copyRange.offset;

    while (count > 0) {
        const quint8* lastByte = &_charset[std::size(_charset)];
        int bytesToCopy = qMin((qint64)copyRange.blockSize * 8, (qint64)(lastByte - chrdst));
        if (bytesToCopy < 0)
            break;
        std::memcpy(chrdst, chrsrc, bytesToCopy);
        std::memcpy(colordst, colorsrc, bytesToCopy / 8);
        emit bytesUpdated((chrdst - _charset), bytesToCopy);

        chrdst += (copyRange.blockSize + copyRange.skip) * 8;
        chrsrc += (copyRange.blockSize + copyRange.skip) * 8;
        colordst += copyRange.blockSize + copyRange.skip;
        colorsrc += copyRange.blockSize + copyRange.skip;
        count--;
    }
    emit charsetUpdated();

    // copying should also include updating the new colors and possible multi-color mode
    emit colorPropertiesUpdated(PEN_FOREGROUND);
    //    emit contentsChanged();
}

void State::_pasteTiles(int charIndex, const CopyRange& copyRange, const quint8* origBuffer)
{
    Q_ASSERT(charIndex >= 0 && charIndex < CHAR_BUFFER_SIZE && "Invalid charIndex size");

    int count = copyRange.count;
    const quint8* colorsBuffer = origBuffer + CHAR_BUFFER_SIZE;

    if (copyRange.tileProperties.size != _tileProperties.size) {
        qDebug() << "Error. Src:" << copyRange.tileProperties.size << " Dst:" << _tileProperties.size;
        MainWindow::getInstance()->showMessageOnStatusBar(tr("Error. Tile size different than src"));
        return;
    }

    int tileSize = copyRange.tileProperties.size.width() * copyRange.tileProperties.size.height();
    int tileSrcIdx = copyRange.offset;
    int tileDstIdx = getTileIndexFromCharIndex(charIndex);
    int interleavedFactorSrc = (copyRange.tileProperties.interleaved == 1) ? tileSize : 1;
    int interleavedFactorDst = (_tileProperties.interleaved == 1) ? tileSize : 1;
    int srcskip = 0;
    int dstskip = 0;
    while (count > 0) {
        for (int i = 0; i < copyRange.blockSize; i++) {
            int srcidx = (tileSrcIdx + i + srcskip) * interleavedFactorSrc;
            int dstidx = (tileDstIdx + i + dstskip) * interleavedFactorDst;

            int colorIdx = tileDstIdx + i + dstskip;
            // avoid overflow
            if (colorIdx < 256)
                _tileColors[colorIdx] = colorsBuffer[tileSrcIdx + i + srcskip];

            // when interleaved, break the copy to prevent ugly artifacts
            if (_tileProperties.interleaved != 1 && dstidx >= (256 / tileSize)) {
                count = 0;
                break;
            }

            for (int j = 0; j < tileSize; j++) {
                int charsrc = (srcidx + j * copyRange.tileProperties.interleaved) * 8;
                int chardst = (dstidx + j * _tileProperties.interleaved) * 8;

                // don't overflow, don't copy crappy chars
                if ((CHAR_BUFFER_SIZE - chardst) >= 8 && (CHAR_BUFFER_SIZE - charsrc) >= 8) {
                    std::memcpy(&_charset[chardst], &origBuffer[charsrc], 8);
                    emit bytesUpdated(chardst, 8);
                }
            }
        }
        srcskip += copyRange.skip + copyRange.blockSize;
        dstskip += copyRange.skip + copyRange.blockSize;
        count--;
    }
    emit charsetUpdated();
}

void State::_pasteMap(int charIndex, const CopyRange& copyRange, const quint8* origBuffer)
{
    int count = copyRange.count;
    quint8* dst = _map.data() + charIndex;
    const quint8* src = origBuffer + copyRange.offset;

    while (count > 0) {
        const auto lastByte = &_map[_mapSize.width() * _mapSize.height()];
        int bytesToCopy = qMin((qint64)copyRange.blockSize, (qint64)(lastByte - dst));
        if (bytesToCopy < 0)
            break;
        std::memcpy(dst, src, bytesToCopy);

        dst += copyRange.blockSize + copyRange.skip;
        src += copyRange.blockSize + copyRange.skip;
        count--;
    }
    emit mapContentUpdated();
}

void State::_paste(int charIndex, const CopyRange& copyRange, const quint8* origBuffer)
{
    if (!copyRange.count)
        return;

    if (copyRange.type == CopyRange::CHARS)
        _pasteChars(charIndex, copyRange, origBuffer);

    else if (copyRange.type == CopyRange::TILES)
        _pasteTiles(charIndex, copyRange, origBuffer);

    else if (copyRange.type == CopyRange::MAP)
        _pasteMap(charIndex, copyRange, origBuffer);

    emit contentsChanged();
}

//
// tile manipulation
//

void State::tilePaint(int tileIndex, const QPoint& point, int pen, bool mergeable)
{
    getUndoStack()->push(new PaintTileCommand(this, tileIndex, point, pen, mergeable));
}

void State::tileInvert(int tileIndex)
{
    getUndoStack()->push(new InvertTileCommand(this, tileIndex));
}

void State::_tileInvert(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {
            for (int i = 0; i < 8; i++) {
                charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = ~charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileClear(int tileIndex)
{
    getUndoStack()->push(new ClearTileCommand(this, tileIndex));
}

void State::_tileClear(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {
            for (int i = 0; i < 8; i++) {
                charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = 0;
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileFlipHorizontally(int tileIndex)
{
    getUndoStack()->push(new FlipTileHCommand(this, tileIndex));
}

void State::_tileFlipHorizontally(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // flip bits
    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {

            for (int i = 0; i < 8; i++) {
                char tmp = 0;
                for (int j = 0; j < 8; j++) {
                    if (charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] & (1 << j))
                        tmp |= 1 << (7 - j);
                }
                charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = tmp;
            }
        }
    }

    // swap the chars
    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width() / 2; x++) {
            for (int i = 0; i < 8; i++) {
                std::swap(charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved],
                    charPtr[i + (_tileProperties.size.width() - 1 - x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileFlipVertically(int tileIndex)
{
    getUndoStack()->push(new FlipTileVCommand(this, tileIndex));
}

void State::_tileFlipVertically(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // flip bits
    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {

            for (int i = 0; i < 4; i++) {
                std::swap(charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved],
                    charPtr[7 - i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved]);
            }
        }
    }

    // swap the chars
    for (int y = 0; y < _tileProperties.size.height() / 2; y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {
            for (int i = 0; i < 8; i++) {
                std::swap(charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved],
                    charPtr[i + (x + (_tileProperties.size.height() - 1 - y) * _tileProperties.size.width()) * 8 * _tileProperties.interleaved]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileRotate(int tileIndex)
{
    getUndoStack()->push(new RotateTileCommand(this, tileIndex));
}

void State::_tileRotate(int tileIndex)
{
    Q_ASSERT(_tileProperties.size.width() == _tileProperties.size.height() && "Only square tiles can be rotated");

    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // rotate each char (its bits) individually
    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {

            Char tmpchr { 0 };

            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 8; j++) {
                    if (charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] & (1 << (7 - j)))
                        tmpchr._char8[j] |= (1 << i);
                }
            }

            setCharForTile(tileIndex, x, y, tmpchr);
        }
    }

    // replace the chars in the correct order.
    if (_tileProperties.size.width() > 1) {
        std::vector<Char> tmpchars(_tileProperties.size.width() * _tileProperties.size.height());

        // place the rotated chars in a rotated tmp buffer
        for (int y = 0; y < _tileProperties.size.height(); y++) {
            for (int x = 0; x < _tileProperties.size.width(); x++) {
                // rotate them: tmpchars[w-y-1,x] = tile[x,y];
                tmpchars[(_tileProperties.size.width() - 1 - y) + x * _tileProperties.size.width()] = getCharFromTile(tileIndex, x, y);
            }
        }

        // place the rotated tmp buffer in the final position
        for (int y = 0; y < _tileProperties.size.height(); y++) {
            for (int x = 0; x < _tileProperties.size.width(); x++) {
                setCharForTile(tileIndex, x, y, tmpchars[x + _tileProperties.size.width() * y]);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftLeft(int tileIndex)
{
    getUndoStack()->push(new ShiftLeftTileCommand(this, tileIndex));
}

void State::_tileShiftLeft(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    int times = shouldBeDisplayedInMulticolor2(tileIndex) ? 2 : 1;

    // shift two times in multicolor mode
    for (int i = 0; i < times; ++i) {
        // top tile first
        for (int y = 0; y < _tileProperties.size.height(); y++) {

            // top byte of
            for (int i = 0; i < 8; i++) {

                bool leftBit = false;
                bool prevLeftBit = false;

                for (int x = _tileProperties.size.width() - 1; x >= 0; x--) {
                    leftBit = charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] & (1 << 7);

                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] <<= 1;

                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] &= 254;
                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] |= (quint8)prevLeftBit;

                    prevLeftBit = leftBit;
                }
                charPtr[i + (_tileProperties.size.width() - 1 + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] &= 254;
                charPtr[i + (_tileProperties.size.width() - 1 + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] |= (quint8)leftBit;
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftRight(int tileIndex)
{
    getUndoStack()->push(new ShiftRightTileCommand(this, tileIndex));
}

void State::_tileShiftRight(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // shift two times in multicolor mode
    int times = shouldBeDisplayedInMulticolor2(tileIndex) ? 2 : 1;
    for (int i = 0; i < times; i++) {
        // top tile first
        for (int y = 0; y < _tileProperties.size.height(); y++) {

            // top byte of
            for (int i = 0; i < 8; i++) {

                bool rightBit = false;
                bool prevRightBit = false;

                for (int x = 0; x < _tileProperties.size.width(); x++) {
                    rightBit = charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] & (1 << 0);

                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] >>= 1;

                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] &= 127;
                    charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] |= (prevRightBit << 7);

                    prevRightBit = rightBit;
                }
                charPtr[i + (0 + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] &= 127;
                charPtr[i + (0 + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] |= (rightBit << 7);
            }
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftUp(int tileIndex)
{
    getUndoStack()->push(new ShiftUpTileCommand(this, tileIndex));
}

void State::_tileShiftUp(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int x = 0; x < _tileProperties.size.width(); x++) {

        // bottom byte of bottom
        qint8 topByte, prevTopByte = 0;

        for (int y = _tileProperties.size.height() - 1; y >= 0; y--) {

            topByte = charPtr[0 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];

            for (int i = 0; i < 7; i++) {
                charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = charPtr[i + 1 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];
            }

            charPtr[7 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = prevTopByte;
            prevTopByte = topByte;
        }
        // replace bottom byte (y=height-1) with top byte
        charPtr[7 + (x + (_tileProperties.size.height() - 1) * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = prevTopByte;
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftDown(int tileIndex)
{
    getUndoStack()->push(new ShiftDownTileCommand(this, tileIndex));
}

void State::_tileShiftDown(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int x = 0; x < _tileProperties.size.width(); x++) {

        // bottom byte of bottom
        qint8 bottomByte, prevBottomByte = 0;

        for (int y = 0; y < _tileProperties.size.height(); y++) {

            bottomByte = charPtr[7 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];

            for (int i = 6; i >= 0; i--) {
                charPtr[i + 1 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = charPtr[i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];
            }

            charPtr[0 + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = prevBottomByte;
            prevBottomByte = bottomByte;
        }
        // replace top byte (y=0) with bottom byte
        charPtr[x * 8 * _tileProperties.interleaved] = prevBottomByte;
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

//
//
int State::getCharIndex() const
{
    return _charIndex;
}

void State::setCharIndex(int charIndex)
{
    Q_ASSERT(charIndex >= 0 && charIndex < 256);

    if (_charIndex != charIndex) {
        _setCharIndex(charIndex);

        int tileIndex = getTileIndexFromCharIndex(charIndex);
        _setTileIndex(tileIndex);
    }
}

void State::_setCharIndex(int charIndex)
{
    if (_charIndex != charIndex) {
        _charIndex = charIndex;
        emit charIndexUpdated(_charIndex);
    }
}

int State::getTileIndex() const
{
    return _tileIndex;
}

void State::setTileIndex(int tileIndex)
{
    Q_ASSERT(tileIndex >= 0 && tileIndex <= (256 / (_tileProperties.size.width() * _tileProperties.size.height())));

    if (_tileIndex != tileIndex) {
        _setTileIndex(tileIndex);

        int charIndex = getCharIndexFromTileIndex(tileIndex);
        _setCharIndex(charIndex);
    }
}

void State::_setTileIndex(int tileIndex)
{
    if (_tileIndex != tileIndex) {
        int oldTileIndex = _tileIndex;

        // set _tileIndex before emiting colorPropertiesUpdated
        // since from there they might call getTileIndex() and it will be the incorrect one
        _tileIndex = tileIndex;

        if (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE && _tileColors[tileIndex] != _tileColors[oldTileIndex])
            emit colorPropertiesUpdated(PEN_FOREGROUND);

        emit tileIndexUpdated(tileIndex);
    }
}

State::KeyboardMapping State::getKeyboardMapping() const
{
    return _keyboardMapping;
}

void State::undo()
{
    getUndoStack()->undo();
}

void State::redo()
{
    getUndoStack()->redo();
}

QUndoStack* State::getUndoStack() const
{
    return _undoStack;
}

void State::clearUndoStack()
{
    getUndoStack()->clear();
}

//
// Helpers
// They must not emit signals
//
State::Char State::getCharFromTile(int tileIndex, int x, int y) const
{
    Char ret {};
    int charIndex = getCharIndexFromTileIndex(tileIndex);

    for (int i = 0; i < 8; i++) {
        ret._char8[i] = _charset[charIndex * 8 + i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved];
    }
    return ret;
}

void State::setCharForTile(int tileIndex, int x, int y, const Char& chr)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);

    for (int i = 0; i < 8; i++) {
        _charset[charIndex * 8 + i + (x + y * _tileProperties.size.width()) * 8 * _tileProperties.interleaved] = chr._char8[i];
    }
}

BigCharWidget* State::getBigCharWidget() const
{
    return _bigCharWidget;
}

void State::setupDefaultMap()
{
    std::fill(std::begin(_map), std::end(_map), 0x20);
    // 1234567890123456789012345678901234567890
    constexpr char hello64[] = "                                        "
                           "    **** COMMODORE 64 BASIC V2 ****     "
                           "                                        "
                           " 64K RAM SYSTEM  38911 BASIC BYTES FREE ";

    constexpr char hello128[] = "                                        "
                            " COMMODORE BASIC V7.0 122365 BYTES FREE "
                            "   (C)1986 COMMODORE ELECTRONICS, LTD.  "
                            "         (C)1977 MICROSOFT CORP.        "
                            "           ALL RIGHTS RESERVED          ";
    struct {
        const char* hello;
        int helloSize;
    } hellos[] = {
        { hello64, sizeof(hello64) - 1 },
        { hello128, sizeof(hello128) - 1 }
    };

    QRandomGenerator prng(QTime::currentTime().msec());
    int helloidx = prng.generate() % 2;
    // ASCII to PETSCII screen codes
    for (int i = 0; i < hellos[helloidx].helloSize; ++i)
        _map[i] = hellos[helloidx].hello[i] & ~0x40;
}
