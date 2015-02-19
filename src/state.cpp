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
    , _selectedColorIndex(3)
    , _colors{1,12,15,0}
    , _tileSize{1,1}
    , _charInterleaved(1)
    , _filename("")
{
    memset(_copyTile, 0, sizeof(_copyTile));
}

State::~State()
{

}

void State::reset()
{
    _totalChars = 0;
    _multiColor = false;
    _selectedColorIndex = 3;
    _colors[0] = 1;
    _colors[1] = 12;
    _colors[2] = 15;
    _colors[3] = 0;
    _tileSize = {1,1};
    _charInterleaved = 1;
    _filename = "";

    memset(_chars, 0, sizeof(_chars));
}

bool State::exportRaw(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;
    return StateExport::saveRaw(file, this);
}

bool State::exportPRG(const QString& filename, quint16 address)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;
    return StateExport::savePRG(file, this, address);
}

bool State::openFile(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    qint64 length=0;

    QFileInfo info(file);
    if (info.suffix() == "vchar64proj")
    {
        length = StateImport::loadVChar64(file, this);
        _filename = filename;
    }
    else if ((info.suffix() == "64c") || (info.suffix() == "prg"))
    {
        length = StateImport::loadPRG(file, this);
    }
    else if(info.suffix() == "ctm")
    {
        length = StateImport::loadCTM(file, this);
    }
    else
    {
        length = StateImport::loadRaw(file, this);
    }

    file.close();

    if(length<=0)
        return false;

    return true;
}

bool State::save(const QString &filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;

    qint64 length=0;

    length = StateExport::saveVChar64(file, this);

    file.close();

    if(length<=0)
        return false;

    _filename = filename;

    return true;
}

int State::getCharColor(int charIndex, int bitIndex) const
{
    Q_ASSERT(charIndex >=0 && charIndex < 256 && "Invalid charIndex. Valid range: 0,255");
    Q_ASSERT(bitIndex >=0 && bitIndex < 64 && "Invalid bit. Valid range: 0,63");

    char c = _chars[charIndex*8 + bitIndex/8];
    int b = bitIndex%8;
    int mask = 1 << (7-b);

   return (c & mask);
}

void State::setCharColor(int charIndex, int bitIndex, int colorIndex)
{
    Q_ASSERT(charIndex >=0 && charIndex < 256 && "Invalid charIndex. Valid range: 0,256");
    Q_ASSERT(bitIndex >=0 && bitIndex < 64 && "Invalid bit. Valid range: 0,64");
    Q_ASSERT(colorIndex >=0 && colorIndex < 4 && "Invalid colorIndex. range: 0,4");

//    if (_multiColor)
    int bits_to_mask = 1;
    int totalbits = 64;
    int modulus = 8;

    if (_multiColor) {
        bits_to_mask = 3;
        totalbits = 32;
        modulus = 4;

        // only care about even bits in multicolor
        bitIndex &= 0xfe;
    }


    // get the needed line ignoring whether it is multicolor or not
    char c = _chars[charIndex*8 + bitIndex/8];

    // for multicolor, we need to get the modulus
    // in different ways regarding it is multicolor or not

    int factor = 64/totalbits;
    int b = (bitIndex/factor) % modulus;
    int mask = bits_to_mask << ((modulus-1)-b) * factor;

    // turn off bits
    c &= ~mask;

    // and 'or' it with colorIndex
    c |= colorIndex << ((modulus-1)-b) * factor;

    _chars[charIndex*8 + bitIndex/8] = c;
}

void State::setTileSize(const QSize& tileSize)
{
    _tileSize = tileSize;
}

void State::setCharInterleaved(int charInterleaved)
{
    _charInterleaved = charInterleaved;
}

quint8* State::getCharsBuffer()
{
    return _chars;
}

void State::resetCharsBuffer()
{
    memset(_chars, 0, sizeof(_chars));
}

// helper functions
int State::charIndexFromTileIndex(int tileIndex) const
{
    int charIndex = tileIndex;
    if (_charInterleaved==1) {
        charIndex *= (_tileSize.width() * _tileSize.height());
    }

    return charIndex;
}

int State::tileIndexFromCharIndex(int charIndex) const
{
    int tileIndex = charIndex;
    if (_charInterleaved==1) {
        tileIndex /= (_tileSize.width() * _tileSize.height());
    }

    return tileIndex;
}

State::Char State::getCharFromTile(int tileIndex, int x, int y) const
{
    Char ret;
    int charIndex = charIndexFromTileIndex(tileIndex);

    for (int i=0; i<8; i++) {
        ret._char8[i] = _chars[charIndex*8+i+(x+y*_tileSize.width())*8*_charInterleaved];
    }
    return ret;
}

void State::setCharForTile(int tileIndex, int x, int y, const Char& chr)
{
    int charIndex = charIndexFromTileIndex(tileIndex);

    for (int i=0; i<8; i++) {
        _chars[charIndex*8+i+(x+y*_tileSize.width())*8*_charInterleaved] = chr._char8[i];
    }
}


// tile manipulation
void State::tileCopy(int tileIndex)
{
    int tileSize = _tileSize.width() * _tileSize.height() * 8;
    Q_ASSERT(tileIndex>=0 && tileIndex<tileIndexFromCharIndex(256) && "invalid index value");
    memcpy(_copyTile, &_chars[tileIndex*tileSize], tileSize);
}

void State::tilePaste(int tileIndex)
{
    int tileSize = _tileSize.width() * _tileSize.height() * 8;
    Q_ASSERT(tileIndex>=0 && tileIndex<tileIndexFromCharIndex(256) && "invalid index value");
    memcpy(&_chars[tileIndex*tileSize], _copyTile, tileSize);
}

void State::tileInvert(int tileIndex)
{
    int charIndex = charIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {
            for (int i=0; i<8; i++) {
                charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved] = ~charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved];
            }
        }
    }
}

void State::tileClear(int tileIndex)
{
    int charIndex = charIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {
            for (int i=0; i<8; i++) {
                charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved] = 0;
            }
        }
    }
}

void State::tileFlipHorizontally(int tileIndex)
{
    int charIndex = charIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // flip bits
    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {

            for (int i=0; i<8; i++) {
                char tmp = 0;
                for (int j=0; j<8; j++) {
                    if (charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved] & (1<<j))
                        tmp |= 1 << (7-j);
                }
                charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved] = tmp;
            }
        }
    }

    // swap the chars
    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width()/2; x++) {
            for (int i=0; i<8; i++) {
                std::swap(charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved],
                        charPtr[i+(_tileSize.width()-1-x+y*_tileSize.width())*8*_charInterleaved]);
            }
        }
    }
}

void State::tileFlipVertically(int tileIndex)
{
    int charIndex = charIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);

    // flip bits
    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {

            for (int i=0; i<4; i++) {
                std::swap(charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved],
                        charPtr[7-i+(x+y*_tileSize.width())*8*_charInterleaved]);
            }
        }
    }

    // swap the chars
    for (int y=0; y<_tileSize.height()/2; y++) {
        for (int x=0; x<_tileSize.width(); x++) {
            for (int i=0; i<8; i++) {
                std::swap(charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved],
                        charPtr[i+(x+(_tileSize.height()-1-y)*_tileSize.width())*8*_charInterleaved]);
            }
        }
    }
}

void State::tileRotate(int tileIndex)
{
    Q_ASSERT(_tileSize.width() == _tileSize.height() && "Only square tiles can be rotated");

    int charIndex = charIndexFromTileIndex(tileIndex);
    quint8* charPtr = getCharAtIndex(charIndex);


    // rotate each char (its bits) individually
    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {

            Char tmpchr;
            memset(tmpchr._char8, 0, sizeof(tmpchr));

            for (int i=0; i<8; i++) {
                for (int j=0; j<8; j++) {
                    if (charPtr[i+(x+y*_tileSize.width())*8*_charInterleaved] & (1<<(7-j)))
                        tmpchr._char8[j] |= (1<<i);
                }
            }

            setCharForTile(tileIndex, x, y, tmpchr);
        }
    }


    // replace the chars in the correct order.
    if (_tileSize.width()>1) {
        Char tmpchars[_tileSize.width()*_tileSize.height()];

        // place the rotated chars in a rotated tmp buffer
        for (int y=0; y<_tileSize.height(); y++) {
            for (int x=0; x<_tileSize.width(); x++) {
                // rotate them: tmpchars[w-y-1,x] = tile[x,y];
                tmpchars[(_tileSize.width()-1-y) + x * _tileSize.width()] = getCharFromTile(tileIndex, x, y);
            }
        }

        // place the rotated tmp buffer in the final position
        for (int y=0; y<_tileSize.height(); y++) {
            for (int x=0; x<_tileSize.width(); x++) {
                    setCharForTile(tileIndex, x, y, tmpchars[x + _tileSize.width() * y]);
            }
        }
    }
}
