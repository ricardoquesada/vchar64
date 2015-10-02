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

#include <algorithm>
#include <QFile>
#include <QFileInfo>

#include "stateimport.h"
#include "stateexport.h"

static State *__instance = nullptr;

const int State::CHAR_BUFFER_SIZE;

State* State::getInstance()
{
    if (!__instance)
        __instance = new State();

    return __instance;
}

State::State()
    : _totalChars(0)
    , _multiColor(false)
    , _selectedPen(PEN_FOREGROUND)
    , _penColors{1,12,15,0}
    , _tileProperties{{1,1},1}
    , _loadedFilename("")
    , _savedFilename("")
    , _exportedFilename("")
    , _exportedAddress(-1)
    , _undoStack(nullptr)
{
    memset(_copyCharset, 0, sizeof(_copyCharset));
    _undoStack = new QUndoStack;
}

State::~State()
{
    delete _undoStack;
}

void State::reset()
{
    _totalChars = 0;
    _multiColor = false;
    _selectedPen = 3;
    _penColors[0] = 1;
    _penColors[1] = 12;
    _penColors[2] = 15;
    _penColors[3] = 0;
    _tileProperties.size = {1,1};
    _tileProperties.interleaved = 1;
    _loadedFilename = "";
    _savedFilename = "";
    _exportedFilename = "";
    _exportedAddress = -1;

    memset(_charset, 0, sizeof(_charset));

    _undoStack->clear();

    emit fileLoaded();
    emit contentsChanged();
}

bool State::isModified() const
{
    return (!_undoStack->isClean());
}

bool State::export_()
{
    Q_ASSERT(_exportedFilename.length()>0 && "Invalid filename");

    if (_exportedAddress == -1)
        return exportRaw(_exportedFilename);
    else
        return exportPRG(_exportedFilename, (quint16)_exportedAddress);
}

bool State::exportRaw(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;

    if (StateExport::saveRaw(this, file) > 0)
    {
        _exportedAddress = -1;
        _exportedFilename = filename;
        return true;
    }
    return false;
}

bool State::exportPRG(const QString& filename, quint16 address)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;

    if (StateExport::savePRG(this, file, address) > 0)
    {
        _exportedAddress = address;
        _exportedFilename = filename;
        return true;
    }
    return false;
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

    // if a new file is loaded, then reset the exported and saved values
    _savedFilename = "";
    _exportedFilename = "";
    _exportedAddress = -1;

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
            if (filetype == FILETYPE_PRG) {
                _exportedAddress = loadedAddress;
            }
        }
    }

    _undoStack->clear();

    emit fileLoaded();
    emit contentsChanged();

    return true;
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
void State::setMultiColor(bool enabled)
{
    if (_multiColor != enabled)
    {
        _multiColor = enabled;

        emit colorPropertiesUpdated();
        emit contentsChanged();
    }
}

void State::setColorForPen(int pen, int color)
{
    Q_ASSERT(pen >=0 && pen < PEN_MAX);
    Q_ASSERT(color >=0 && color < 16);
    _penColors[pen] = color;

    emit colorPropertiesUpdated();
    emit contentsChanged();
}

int State::tileGetPen(int tileIndex, const QPoint& position)
{
    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
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
    if (!_multiColor)
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

void State::tileSetPen(int tileIndex, const QPoint& position, int pen)
{
    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
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
    if (!_multiColor)
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
    if (memcmp(&_tileProperties, &properties, sizeof(_tileProperties)) != 0) {
        _tileProperties = properties;

        emit tilePropertiesUpdated();
        emit contentsChanged();
    }
}

// charset methods
quint8* State::getCharsetBuffer()
{
    return _charset;
}

quint8* State::getCopyCharsetBuffer()
{
    return _copyCharset;
}

void State::resetCharsetBuffer()
{
    memset(_charset, 0, sizeof(_charset));
}

void State::updateCharset(quint8 *buffer)
{
    memcpy(_charset, buffer, sizeof(_charset));

    emit charsetUpdated();
    emit contentsChanged();
}

// buffer must be at least 8x8*8 bytes big
void State::copyCharFromIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height() * 8;
    Q_ASSERT(bufferSize >= tileSize && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
    memcpy(buffer, &_charset[tileIndex*tileSize], tileSize);
}

// size-of-tile chars will be copied
void State::copyCharToIndex(int tileIndex, quint8* buffer, int bufferSize)
{
    int tileSize = _tileProperties.size.width() * _tileProperties.size.height() * 8;
    Q_ASSERT(bufferSize >= tileSize && "invalid bufferSize. Too small");
    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
    memcpy(&_charset[tileIndex*tileSize], buffer, tileSize);

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}


// helper functions
int State::getCharIndexFromTileIndex(int tileIndex) const
{
    int charIndex = tileIndex;
    if (_tileProperties.interleaved==1) {
        charIndex *= (_tileProperties.size.width() * _tileProperties.size.height());
    }

    return charIndex;
}

int State::getTileIndexFromCharIndex(int charIndex) const
{
    int tileIndex = charIndex;
    if (_tileProperties.interleaved==1) {
        tileIndex /= (_tileProperties.size.width() * _tileProperties.size.height());
    }

    return tileIndex;
}

quint8* State::getCharAtIndex(int charIndex)
{
    Q_ASSERT(charIndex>=0 && charIndex<256 && "Invalid index");
    return &_charset[charIndex*8];
}

void State::copy(const CopyRange& copyRange)
{
    memcpy(_copyCharset, _charset, CHAR_BUFFER_SIZE);
    _copyRange = copyRange;
}

void State::paste(int charIndex, const CopyRange& copyRange, const quint8* charsetBuffer)
{
    Q_ASSERT(charIndex >=0 && charIndex< CHAR_BUFFER_SIZE && "Invalid charIndex size");

    if (!copyRange.count)
        return;

    int count = copyRange.count;

    quint8* dst = _charset + (charIndex * 8);
    const quint8* src = charsetBuffer + (copyRange.offset * 8);

    while (count>0)
    {
        memcpy(dst, src, copyRange.blockSize * 8);
        dst += (copyRange.blockSize + copyRange.skip) * 8;
        src += (copyRange.blockSize + copyRange.skip) * 8;
        count--;
    }

    emit charsetUpdated();
    emit contentsChanged();
}

const State::CopyRange& State::getCopyRange() const
{
    return _copyRange;
}

void State::setCopyRange(const State::CopyRange& copyRange)
{
    _copyRange = copyRange;
}

// tile manipulation
//void State::tileCopy(int tileIndex)
//{
//    int tileSize = _tileProperties.size.width() * _tileProperties.size.height() * 8;
//    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
//    memcpy(_copyCharset, &_charset[tileIndex*tileSize], tileSize);
//}

//void State::tilePaste(int tileIndex)
//{
//    int tileSize = _tileProperties.size.width() * _tileProperties.size.height() * 8;
//    Q_ASSERT(tileIndex>=0 && tileIndex<getTileIndexFromCharIndex(256) && "invalid index value");
//    memcpy(&_charset[tileIndex*tileSize], _copyCharset, tileSize);

//    emit tileUpdated(tileIndex);
//    emit contentsChanged();
//}

void State::tileInvert(int tileIndex)
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
        Char tmpchars[_tileProperties.size.width()*_tileProperties.size.height()];

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
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);


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
                charPtr[i+(x+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= prevLeftBit;

                prevLeftBit = leftBit;
            }
            charPtr[i+(_tileProperties.size.width()-1+y*_tileProperties.size.width())*8*_tileProperties.interleaved] &= 254;
            charPtr[i+(_tileProperties.size.width()-1+y*_tileProperties.size.width())*8*_tileProperties.interleaved] |= leftBit;
        }
    }

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftRight(int tileIndex)
{
    int charIndex = getCharIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);


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

    emit tileUpdated(tileIndex);
    emit contentsChanged();
}

void State::tileShiftUp(int tileIndex)
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

