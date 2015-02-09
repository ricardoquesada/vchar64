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
    : _charIndex(0)
    , _totalChars(0)
    , _multiColor(false)
    , _selectedColorIndex(3)
    , _colors{1,12,15,0}
    , _filename("")
{
    import(":/c64-chargen.bin");
}

State::~State()
{

}

void State::reset()
{
    _charIndex = 0;
    _totalChars = 0;
    _multiColor = false;
    _selectedColorIndex = 3;
    _colors[0] = 1;
    _colors[1] = 12;
    _colors[2] = 15;
    _colors[3] = 0;
    _filename = "";

    import(":/c64-chargen.bin");
}

bool State::exportRaw(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;
    return StateExport::saveRaw(file, this);
}

bool State::exportPRG(const QString& filename, u_int16_t address)
{
    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;
    return StateExport::savePRG(file, this, address);
}

bool State::import(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    qint64 length=0;

    QFileInfo info(file);
    if ((info.suffix() == "64c") || (info.suffix() == "prg"))
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

bool State::load(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    auto length = StateImport::loadVChar64(file, this);

    file.close();

    if(length<=0)
        return false;

    _filename = filename;

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

u_int8_t* State::getCharsBuffer()
{
    return _chars;
}

void State::resetCharsBuffer()
{
    memset(_chars, 0, sizeof(_chars));
}
