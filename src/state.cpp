#include "state.h"

#include <algorithm>
#include <QFile>
#include <QFileInfo>

#include "import.h"

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
    , _color(0)
    , _multiColor0(0)
    , _multiColor1(1)
{
    loadCharSet(":/c64-chargen.bin");
}

State::~State()
{

}

bool State::loadCharSet(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    qint64 length=0;

    QFileInfo info(file);
    if (info.suffix() == "64c")
    {
        length = Import::load64C(file, this);
    }
    else if(info.suffix() == "ctm")
    {
        length = Import::loadCTM(file, this);
    }
    else
    {
        length = Import::loadRaw(file, this);
    }

    file.close();

    if(length<=0)
        return false;

    return true;
}

void State::toggleBit(int charIndex, int bitIndex)
{
    Q_ASSERT(charIndex >=0 && charIndex < 256 && "Invalid charIndex. Valid range: 0,255");
    Q_ASSERT(bitIndex >=0 && bitIndex < 64 && "Invalid bit. Valid range: 0,63");

    bool bit = getBit(charIndex, bitIndex);
    bit = !bit;
    setBit(charIndex, bitIndex, bit);
}

bool State::getBit(int charIndex, int bitIndex) const
{
    Q_ASSERT(charIndex >=0 && charIndex < 256 && "Invalid charIndex. Valid range: 0,255");
    Q_ASSERT(bitIndex >=0 && bitIndex < 64 && "Invalid bit. Valid range: 0,63");

    char c = _chars[charIndex*8 + bitIndex/8];
    int b = bitIndex%8;
    int mask = 1 << (7-b);

   return (c & mask);
}

void State::setBit(int charIndex, int bitIndex, bool enabled)
{
    Q_ASSERT(charIndex >=0 && charIndex < 256 && "Invalid charIndex. Valid range: 0,255");
    Q_ASSERT(bitIndex >=0 && bitIndex < 64 && "Invalid bit. Valid range: 0,63");

    char c = _chars[charIndex*8 + bitIndex/8];
    int b = bitIndex%8;
    int mask = 1 << (7-b);

    if (enabled)
        c |= mask;
    else
        c &= ~mask;

    _chars[charIndex*8 + bitIndex/8] = c;
}

char* State::getCharsBuffer()
{
    return _chars;
}

void State::resetCharsBuffer()
{
    memset(_chars, 0, sizeof(_chars));
}
