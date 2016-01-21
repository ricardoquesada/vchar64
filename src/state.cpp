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

#include <string.h>

#include <vector>
#include <algorithm>

#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include "mainwindow.h"
#include "stateimport.h"
#include "stateexport.h"
#include "commands.h"

const int State::CHAR_BUFFER_SIZE;

// target constructor
State::State(quint8 *charset, quint8 *tileAttribs, quint8 *map, const QSize& mapSize)
    : _totalChars(0)
    , _mapSize(mapSize)
    , _multicolorMode(false)
    , _foregroundColorMode(FOREGROUND_COLOR_GLOBAL)
    , _selectedPen(PEN_FOREGROUND)
    , _penColors{1,5,7,11}
    , _tileProperties{{1,1},1}
    , _charIndex(0)
    , _tileIndex(0)
    , _loadedFilename("")
    , _savedFilename("")
    , _exportedFilename("")
    , _exportedAddresses{0,0,0}
    , _exportedFormat(EXPORT_FORMAT_RAW)
    , _exportedFeatures(EXPORT_FEATURE_CHARSET)
    , _undoStack(nullptr)
    , _bigCharWidget(nullptr)
{
    _undoStack = new QUndoStack;

    if (charset)
        memcpy(_charset, charset, sizeof(_charset));
    else memset(_charset, 0, sizeof(_charset));

    if (tileAttribs)
        memcpy(_tileAttribs, tileAttribs, sizeof(_tileAttribs));
    else memset(_tileAttribs, 11, sizeof(_tileAttribs));

    Q_ASSERT(_mapSize.width() * _mapSize.height() > 0 && "Invalid size");
    _map = (quint8*)malloc(_mapSize.width() * _mapSize.height());
    if (map)
        memcpy(_map, map, _mapSize.width() * _mapSize.height());
    else memset(_map, 0, _mapSize.width() * _mapSize.height());
}

// Delegating constructor
State::State()
    : State(nullptr, nullptr, nullptr, QSize(40,25))
{
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
    _tileProperties.size = {1,1};
    _tileProperties.interleaved = 1;
    _loadedFilename = "";
    _savedFilename = "";
    _exportedFilename = "";
    _exportedAddresses[0] = 0;
    _exportedAddresses[1] = 0;
    _exportedAddresses[2] = 0;
    _exportedFormat = EXPORT_FORMAT_RAW;
    _exportedFeatures = EXPORT_FEATURE_CHARSET;

    memset(_charset, 0, sizeof(_charset));
    memset(_tileAttribs, 11, sizeof(_tileAttribs));
    memset(_map, 0, _mapSize.width() * _mapSize.height());
}

void State::refresh()
{
    emit charsetUpdated();
    emit charIndexUpdated(_charIndex);
    emit tileIndexUpdated(_tileIndex);
    emit tilePropertiesUpdated();
    emit multicolorModeToggled(shouldBeDisplayedInMulticolor());
    emit colorPropertiesUpdated(_selectedPen);
}

void State::emitNewState()
{
    emit fileLoaded();
    emit contentsChanged();
}

bool State::isModified() const
{
    return (!_undoStack->isClean());
}

bool State::openFile(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    qint64 length=0;
    quint16 loadedAddress;

    enum {
        FILETYPE_VCHAR64,
        FILETYPE_PRG,
        FILETYPE_RAW,
        FILETYPE_CTM
    };
    int filetype = FILETYPE_RAW;


    QFileInfo info(file);
    if (info.suffix() == "vchar64proj")
    {
        length = StateImport::loadVChar64(this, file);
        filetype = FILETYPE_VCHAR64;
    }
    else if ((info.suffix() == "64c") || (info.suffix() == "prg"))
    {
        length = StateImport::loadPRG(this, file, &loadedAddress);
        filetype = FILETYPE_PRG;
    }
    else if(info.suffix() == "ctm")
    {
        length = StateImport::loadCTM(this, file);
        filetype = FILETYPE_CTM;
    }
    else
    {
        length = StateImport::loadRaw(this, file);
        filetype = FILETYPE_RAW;
    }

    file.close();

    if(length<=0)
        return false;

    // built-in resources are not saved
    if (filename[0] != ':')
    {
        _loadedFilename = filename;

        if (filetype == FILETYPE_VCHAR64)
        {
            _savedFilename = filename;
        }
        else if (filetype == FILETYPE_RAW || filetype == FILETYPE_PRG)
        {
            _exportedFilename = filename;
            _exportedFeatures = EXPORT_FEATURE_CHARSET;
            if (filetype == FILETYPE_PRG) {
                _exportedAddresses[0] = loadedAddress;
                _exportedFormat = EXPORT_FORMAT_PRG;
            }
            else _exportedFormat = EXPORT_FORMAT_RAW;
        }
    }

    return true;
}
void State::importCharset(const QString& filename, const quint8 *charset, int charsetSize)
{
    Q_ASSERT(charset);
    Q_ASSERT(charsetSize <= CHAR_BUFFER_SIZE);

    resetCharsetBuffer();
    memcpy(_charset, charset, qMin(CHAR_BUFFER_SIZE, charsetSize));
    _loadedFilename = filename;
}

bool State::export_()
{
    Q_ASSERT(_exportedFilename.length() > 0 && "Invalid filename");

    if (_exportedFormat == EXPORT_FORMAT_RAW)
        return exportRaw(_exportedFilename, _exportedFeatures);

    /* else */
    if(_exportedFormat == EXPORT_FORMAT_PRG)
        return exportPRG(_exportedFilename, _exportedAddresses, _exportedFeatures);

    /* else ASM */
    return exportAsm(_exportedFilename, _exportedFeatures);
}

bool State::exportRaw(const QString& filename, int whatToExport)
{
    bool ret = true;

    if (ret && (whatToExport & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::saveRaw(filename, _charset, sizeof(_charset)) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_MAP))
        ret &= (StateExport::saveRaw(filename, _map, _mapSize.width() * _mapSize.height()) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_ATTRIBS))
        ret &= (StateExport::saveRaw(filename, _tileAttribs, sizeof(_tileAttribs)) > 0);

    if (ret)
    {
        _exportedFormat = EXPORT_FORMAT_RAW;
        _exportedFeatures = whatToExport;
        _exportedFilename = filename;
    }
    return ret;
}

bool State::exportPRG(const QString& filename, quint16 addresses[3], int whatToExport)
{
    bool ret = true;

    if (ret && (whatToExport & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::savePRG(filename, _charset, sizeof(_charset), addresses[0]) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_MAP))
        ret &= (StateExport::savePRG(filename, _map, _mapSize.width() * _mapSize.height(), addresses[1]) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_ATTRIBS))
        ret &= (StateExport::savePRG(filename, _tileAttribs, sizeof(_tileAttribs), addresses[2]) > 0);

    if (ret)
    {
        _exportedFormat = EXPORT_FORMAT_PRG;
        _exportedAddresses[0] = addresses[0];
        _exportedAddresses[1] = addresses[1];
        _exportedAddresses[2] = addresses[2];

        _exportedFilename = filename;
        _exportedFeatures = whatToExport;
        return true;
    }
    return ret;
}

bool State::exportAsm(const QString& filename, int whatToExport)
{
    bool ret = true;

    if (ret && (whatToExport & EXPORT_FEATURE_CHARSET))
        ret &= (StateExport::saveAsm(filename, _charset, sizeof(_charset)) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_MAP))
        ret &= (StateExport::saveAsm(filename, _map, _mapSize.width() * _mapSize.height()) > 0);

    if (ret && (whatToExport & EXPORT_FEATURE_ATTRIBS))
        ret &= (StateExport::saveAsm(filename, _tileAttribs, sizeof(_tileAttribs)) > 0);

    if (ret)
    {
        _exportedFormat = EXPORT_FORMAT_ASM;
        _exportedFeatures = whatToExport;
        _exportedFilename = filename;
    }
    return ret;
}

bool State::saveProject(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;

    qint64 length=0;

    length = StateExport::saveVChar64(this, file);

    file.close();

    if(length<=0)
        return false;

    _savedFilename = filename;

    _undoStack->clear();

    emit contentsChanged();

    return true;
}

//
bool State::shouldBeDisplayedInMulticolor() const
{
    // display char as multicolor only if multicolor is enabled
    // and foreground color is >= 8
    return (_multicolorMode && _penColors[State::PEN_FOREGROUND] >= 8);
}

bool State::shouldBeDisplayedInMulticolor2(int tileIdx) const
{
    Q_ASSERT(((tileIdx == -1 && _foregroundColorMode == FOREGROUND_COLOR_GLOBAL)
             || (tileIdx >=0 && tileIdx < 256))
              && "Invalid tileIdx");

    return (_multicolorMode &&
            ((_foregroundColorMode == FOREGROUND_COLOR_GLOBAL && _penColors[PEN_FOREGROUND] >= 8) ||
            (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE && (_tileAttribs[tileIdx] & 0xf) >= 8))
            );
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
    if (_multicolorMode != enabled)
    {
        _multicolorMode = enabled;

        emit multicolorModeToggled(enabled);
        emit contentsChanged();
    }
}

void State::setForegroundColorMode(int mode)
{
    getUndoStack()->push(new SetForegroundColorMode(this, mode));
}

void State::_setForegroundColorMode(int mode)
{
    if (_foregroundColorMode != mode)
    {
        _foregroundColorMode = mode;
        emit colorPropertiesUpdated(PEN_FOREGROUND);
        emit contentsChanged();
    }
}

int State::getForegroundColorMode() const
{
    return _foregroundColorMode;
}

int State::getColorForPen(int pen) const
{
    return getColorForPen(pen, -1);
}

int State::getColorForPen(int pen, int tileIdx) const
{
    Q_ASSERT(pen >=0 && pen < PEN_MAX);

    bool foregroundAndPerTile = (pen == PEN_FOREGROUND && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);

    Q_ASSERT(!(tileIdx == -1 && foregroundAndPerTile) && "Provide a valid tileIdx");

    if (foregroundAndPerTile)
        // making travis happy: -Werror=array-bounds
        return _tileAttribs[(quint8)tileIdx];
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
    Q_ASSERT(pen >=0 && pen < PEN_MAX);
    Q_ASSERT(color >=0 && color < 16);

    bool foregroundAndPerTile = (pen == PEN_FOREGROUND && _foregroundColorMode == FOREGROUND_COLOR_PER_TILE);

    Q_ASSERT((tileIdx != -1 || !foregroundAndPerTile) && "Invalid Tile index");

    int currentColor = foregroundAndPerTile ? _tileAttribs[tileIdx] : _penColors[pen];

    if (currentColor != color)
    {
        bool oldvalue = shouldBeDisplayedInMulticolor2(tileIdx);

        if (foregroundAndPerTile)
            _tileAttribs[tileIdx] = color;
        else _penColors[pen] = color;

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
    Q_ASSERT(pen>=0 && pen<PEN_MAX);

    if (pen != _selectedPen)
    {
        _selectedPen = pen;
        emit selectedPenChaged(pen);
    }
}

int State::tileGetPen(int tileIndex, const QPoint& position)
{
    Q_ASSERT(tileIndex>=0 && tileIndex<=getTileIndexFromCharIndex(255) && "invalid index value");
    Q_ASSERT(position.x()<State::MAX_TILE_WIDTH*8 && position.y()<State::MAX_TILE_HEIGHT*8 && "Invalid position");

    int x = position.x();
    int y = position.y();
    int bitIndex = (x%8) + (y%8) * 8;
    int charIndex = getCharIndexFromTileIndex(tileIndex)
            + (x/8) * _tileProperties.interleaved
            + (y/8) * _tileProperties.interleaved * _tileProperties.size.width();

    char c = _charset[charIndex*8 + bitIndex/8];
    int b = bitIndex%8;

    int ret = 0;

    // not multicolor: expected result: 0 or 1
    if (!_multicolorMode)
    {
        uint8_t mask = 1 << (7-b);
        ret = (c & mask) >> (7-b);
    }
    else
    {
        uint8_t masks[] = {192,192,48,48,12,12,3,3};
        uint8_t mask = masks[b];

        // ignore "odd" numbers when... valid bits to shift: 6,4,2,0
        ret = (c & mask) >> ((7-b) & 254);
    }

    Q_ASSERT(ret >=0 && ret < PEN_MAX);
    return ret;
}

void State::_tileSetPen(int tileIndex, const QPoint& position, int pen)
{
    Q_ASSERT(tileIndex>=0 && tileIndex<=getTileIndexFromCharIndex(255) && "invalid index value");
    Q_ASSERT(position.x()<State::MAX_TILE_WIDTH*8 && position.y()<State::MAX_TILE_HEIGHT*8 && "Invalid position");
    Q_ASSERT(pen >=0 && pen < PEN_MAX && "Invalid pen. range: 0,4");

    int x = position.x();
    int y = position.y();
    int bitIndex = (x%8) + (y%8) * 8;
    int charIndex = getCharIndexFromTileIndex(tileIndex)
            + (x/8) * _tileProperties.interleaved
            + (y/8) * _tileProperties.interleaved * _tileProperties.size.width();

    int byteIndex = charIndex*8 + bitIndex/8;


    uint8_t b = bitIndex%8;
    uint8_t and_mask = 0x00;
    uint8_t or_mask = 0x00;
    if (!shouldBeDisplayedInMulticolor())
    {
        and_mask = 1 << (7-b);
        or_mask = pen << (7-b);
    }
    else
    {
        uint8_t masks[] = {128+64, 128+64, 32+16, 32+16, 8+4, 8+4, 2+1, 2+1};
        and_mask = masks[b];
        or_mask = pen << ((7-b) & 254);
    }

    // get the neede byte
    quint8 c = _charset[byteIndex];

    c &= ~and_mask;
    c |= or_mask;

    if (c != _charset[byteIndex]) {
        _charset[byteIndex] = c;

        emit byteUpdated(byteIndex);
        emit contentsChanged();
    }
}

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

// charset methods
const quint8* State::getCharsetBuffer() const
{
    return _charset;
}

const quint8* State::getMapBuffer() const
{
    return _map;
}

const QSize& State::getMapSize() const
{
    return _mapSize;
}

const quint8* State::getTileAttribs() const
{
    return _tileAttribs;
}


void State::resetCharsetBuffer()
{
    memset(_charset, 0, sizeof(_charset));
}

// buffer must be at least 8x8*8 bytes big
void State::copyTileFromIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    Q_ASSERT(bufferSize >= (tileSize * 8) && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex >= 0 && tileIndex <= getTileIndexFromCharIndex(255) && "invalid index value");

    if (_tileProperties.interleaved == 1)
    {
        memcpy(buffer, &_charset[tileIndex * tileSize * 8], qMin(tileSize * 8, bufferSize));
    }
    else
    {
        for (int i=0; i < tileSize; i++)
        {
            memcpy(&buffer[i * 8], &_charset[(tileIndex + i * _tileProperties.interleaved) * 8], 8);
        }
    }
}

// size-of-tile chars will be copied
void State::copyTileToIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height();
    Q_ASSERT(bufferSize >= (tileSize * 8) && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex >= 0 && tileIndex <= getTileIndexFromCharIndex(255) && "invalid index value");

    if (_tileProperties.interleaved == 1)
    {
        memcpy(&_charset[tileIndex * tileSize * 8], buffer, qMin(tileSize * 8, bufferSize));
    }
    else
    {
        for (int i=0; i < tileSize; i++)
        {
            memcpy(&_charset[(tileIndex + i * _tileProperties.interleaved) * 8], &buffer[i * 8], 8);
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}


// helper functions
int State::getCharIndexFromTileIndex(int tileIndex) const
{
    int charIndex = -1;
    if (_tileProperties.interleaved == 1)
    {
        charIndex = tileIndex * (_tileProperties.size.width() * _tileProperties.size.height());
    }
    else
    {
        charIndex = tileIndex;
    }
    return charIndex;
}

int State::getTileIndexFromCharIndex(int charIndex) const
{
    int tileIndex = -1;
    if (_tileProperties.interleaved == 1) {
        tileIndex = charIndex / (_tileProperties.size.width() * _tileProperties.size.height());
    }
    else
    {
        tileIndex = charIndex % _tileProperties.interleaved;
    }

    return tileIndex;
}

quint8* State::getCharAtIndex(int charIndex)
{
    Q_ASSERT(charIndex>=0 && charIndex<256 && "Invalid index");
    return &_charset[charIndex*8];
}

void State::cut(int offset, const CopyRange &copyRange)
{
    getUndoStack()->push(new CutCommand(this, offset, copyRange));
}

void State::paste(int offset, const CopyRange* copyRange, const quint8* charsetBuffer)
{
    getUndoStack()->push(new PasteCommand(this, offset, copyRange, charsetBuffer));
}

void State::_paste(int charIndex, const CopyRange& copyRange, const quint8* charsetBuffer)
{
    Q_ASSERT(charIndex >=0 && charIndex< CHAR_BUFFER_SIZE && "Invalid charIndex size");

    const quint8* attribsBuffer = charsetBuffer + CHAR_BUFFER_SIZE;
    if (!copyRange.count)
        return;

    int count = copyRange.count;

    if (copyRange.type == CopyRange::CHARS)
    {
        quint8* chrdst = _charset + (charIndex * 8);
        quint8* attrdst = _tileAttribs + charIndex;

        const quint8* chrsrc = charsetBuffer + (copyRange.offset * 8);
        const quint8* attrsrc = attribsBuffer + copyRange.offset;

        while (count>0)
        {
            const auto lastByte = &_charset[sizeof(_charset)];
            int bytesToCopy = qMin((qint64)copyRange.blockSize * 8, (qint64)(lastByte - chrdst));
            if (bytesToCopy <0)
                break;
            memcpy(chrdst, chrsrc, bytesToCopy);
            memcpy(attrdst, attrsrc, bytesToCopy/8);
            emit bytesUpdated((chrdst - _charset), bytesToCopy);

            chrdst += (copyRange.blockSize + copyRange.skip) * 8;
            chrsrc += (copyRange.blockSize + copyRange.skip) * 8;
            attrdst += copyRange.blockSize + copyRange.skip;
            attrsrc += copyRange.blockSize + copyRange.skip;
            count--;
        }
    }
    else if (copyRange.type == CopyRange::TILES)
    {
        if (copyRange.tileProperties.size != _tileProperties.size)
        {
            qDebug() << "Error. Src:" << copyRange.tileProperties.size << " Dst:" << _tileProperties.size;
            MainWindow::getInstance()->setErrorMessage(tr("Error. Tile size different than src"));
            return;
        }

        int tileSize = copyRange.tileProperties.size.width() * copyRange.tileProperties.size.height();
        int tileSrcIdx = copyRange.offset;
        int tileDstIdx = getTileIndexFromCharIndex(charIndex);
        int interleavedFactorSrc = (copyRange.tileProperties.interleaved == 1) ? tileSize : 1;
        int interleavedFactorDst = (_tileProperties.interleaved == 1) ? tileSize : 1;
        int srcskip = 0;
        int dstskip = 0;
        while (count>0)
        {
            for (int i=0; i<copyRange.blockSize; i++)
            {
                int srcidx = (tileSrcIdx + i + srcskip) * interleavedFactorSrc;
                int dstidx = (tileDstIdx + i + dstskip) * interleavedFactorDst;

                // when interleaved, break the copy to prevent ugly artifacts
                if (_tileProperties.interleaved != 1 && dstidx >= (256 / tileSize))
                {
                    count = 0;
                    break;
                }

                for (int j=0; j < tileSize; j++)
                {
                    int charsrc = (srcidx + j * copyRange.tileProperties.interleaved) * 8;
                    int chardst = (dstidx + j * _tileProperties.interleaved) * 8;

                    // don't overflow, don't copy crappy chars
                    if ((CHAR_BUFFER_SIZE - chardst) >= 8 && (CHAR_BUFFER_SIZE - charsrc) >= 8) {
                        memcpy(&_charset[chardst], &charsetBuffer[charsrc], 8);
                        _tileAttribs[chardst/8] = attribsBuffer[charsrc/8];
                        emit bytesUpdated(chardst, 8);
                    }
                }
            }
            srcskip += copyRange.skip + copyRange.blockSize;
            dstskip += copyRange.skip + copyRange.blockSize;
            count--;
        }
    }

    emit charsetUpdated();
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

    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {
            for (int i=0; i<8; i++) {
                charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = ~charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];
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

    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {
            for (int i=0; i<8; i++) {
                charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = 0;
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
    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {

            for (int i=0; i<8; i++) {
                char tmp = 0;
                for (int j=0; j<8; j++) {
                    if (charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] & (1<<j))
                        tmp |= 1 << (7-j);
                }
                charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = tmp;
            }
        }
    }

    // swap the chars
    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width()/2; x++) {
            for (int i=0; i<8; i++) {
                std::swap(charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved],
                        charPtr[i+(_tileProperties.size.width()-1-x+y*_tileProperties.size.width())*8*_tileProperties.interleaved]);
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
    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {

            for (int i=0; i<4; i++) {
                std::swap(charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved],
                        charPtr[7-i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved]);
            }
        }
    }

    // swap the chars
    for (int y=0; y<_tileProperties.size.height()/2; y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {
            for (int i=0; i<8; i++) {
                std::swap(charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved],
                        charPtr[i+(x+(_tileProperties.size.height()-1-y)*_tileProperties.size.width())*8*_tileProperties.interleaved]);
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
    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {

            Char tmpchr;
            memset(tmpchr._char8, 0, sizeof(tmpchr));

            for (int i=0; i<8; i++) {
                for (int j=0; j<8; j++) {
                    if (charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] & (1<<(7-j)))
                        tmpchr._char8[j] |= (1<<i);
                }
            }

            setCharForTile(tileIndex, x, y, tmpchr);
        }
    }


    // replace the chars in the correct order.
    if (_tileProperties.size.width()>1) {
        std::vector<Char> tmpchars(_tileProperties.size.width()*_tileProperties.size.height());

        // place the rotated chars in a rotated tmp buffer
        for (int y=0; y<_tileProperties.size.height(); y++) {
            for (int x=0; x<_tileProperties.size.width(); x++) {
                // rotate them: tmpchars[w-y-1,x] = tile[x,y];
                tmpchars[(_tileProperties.size.width()-1-y) + x * _tileProperties.size.width()] = getCharFromTile(tileIndex, x, y);
            }
        }

        // place the rotated tmp buffer in the final position
        for (int y=0; y<_tileProperties.size.height(); y++) {
            for (int x=0; x<_tileProperties.size.width(); x++) {
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

    int times = shouldBeDisplayedInMulticolor() ? 2 : 1;

    // shift two times in multicolor mode
    for (int i=0; i<times; ++i)
    {
        // top tile first
        for (int y=0; y<_tileProperties.size.height(); y++) {

            // top byte of
            for (int i=0; i<8; i++) {

                bool leftBit = false;
                bool prevLeftBit = false;

                for (int x=_tileProperties.size.width()-1; x>=0; x--) {
                    leftBit = charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] & (1<<7);

                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] <<= 1;

                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] &= 254;
                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= (quint8)prevLeftBit;

                    prevLeftBit = leftBit;
                }
                charPtr[i+(_tileProperties.size.width()-1+y*_tileProperties.size.width())*8*_tileProperties.interleaved] &= 254;
                charPtr[i+(_tileProperties.size.width()-1+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= (quint8)leftBit;
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
    int times = shouldBeDisplayedInMulticolor() ? 2 : 1;
    for (int i=0; i<times; i++)
    {
        // top tile first
        for (int y=0; y<_tileProperties.size.height(); y++) {

            // top byte of
            for (int i=0; i<8; i++) {

                bool rightBit = false;
                bool prevRightBit = false;

                for (int x=0; x<_tileProperties.size.width(); x++) {
                    rightBit = charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] & (1<<0);

                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] >>= 1;

                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] &= 127;
                    charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= (prevRightBit<<7);

                    prevRightBit = rightBit;
                }
                charPtr[i+(0+y*_tileProperties.size.width())*8*_tileProperties.interleaved] &= 127;
                charPtr[i+(0+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= (rightBit<<7);
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

    for (int x=0; x<_tileProperties.size.width(); x++) {

        // bottom byte of bottom
        qint8 topByte, prevTopByte = 0;

        for (int y=_tileProperties.size.height()-1; y>=0; y--) {

            topByte = charPtr[0+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];

            for (int i=0; i<7; i++) {
                charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = charPtr[i+1+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];
            }

            charPtr[7+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = prevTopByte;
            prevTopByte = topByte;
        }
        // replace bottom byte (y=height-1) with top byte
        charPtr[7+(x+(_tileProperties.size.height()-1)*_tileProperties.size.width())*8*_tileProperties.interleaved] = prevTopByte;
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

    for (int x=0; x<_tileProperties.size.width(); x++) {

        // bottom byte of bottom
        qint8 bottomByte, prevBottomByte = 0;

        for (int y=0; y<_tileProperties.size.height(); y++) {

            bottomByte = charPtr[7+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];

            for (int i=6; i>=0; i--) {
                charPtr[i+1+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];
            }

            charPtr[0+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = prevBottomByte;
            prevBottomByte = bottomByte;
        }
        // replace top byte (y=0) with bottom byte
        charPtr[x*8*_tileProperties.interleaved] = prevBottomByte;
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

    if (_charIndex != charIndex)
    {
        _setCharIndex(charIndex);

        int tileIndex = getTileIndexFromCharIndex(charIndex);
        _setTileIndex(tileIndex);
    }
}

void State::_setCharIndex(int charIndex)
{
    if (_charIndex != charIndex)
    {
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

    if (_tileIndex != tileIndex)
    {
        _setTileIndex(tileIndex);

        int charIndex = getCharIndexFromTileIndex(tileIndex);
        _setCharIndex(charIndex);
    }
}

void State::_setTileIndex(int tileIndex)
{
    if (_tileIndex != tileIndex)
    {
        if (_foregroundColorMode == FOREGROUND_COLOR_PER_TILE && _tileAttribs[tileIndex] != _tileAttribs[_tileIndex])
            emit colorPropertiesUpdated(PEN_FOREGROUND);

        _tileIndex = tileIndex;
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

QUndoStack* State::getUndoStack() const
{
    return _undoStack;
}

void State::clearUndoStack()
{
    _undoStack->clear();
}

//
// Helpers
// They must not emit signals
//
State::Char State::getCharFromTile(int tileIndex, int x, int y) const
{
    Char ret;
    int charIndex = getCharIndexFromTileIndex(tileIndex);

    for (int i=0; i<8; i++) {
        ret._char8[i] = _charset[charIndex*8+i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved];
    }
    return ret;
}

void State::setCharForTile(int tileIndex, int x, int y, const Char& chr)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);

    for (int i=0; i<8; i++) {
        _charset[charIndex*8+i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] = chr._char8[i];
    }
}


BigCharWidget* State::getBigCharWidget() const
{
    return _bigCharWidget;
}
