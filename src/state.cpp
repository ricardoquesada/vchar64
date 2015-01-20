#include "state.h"

#include <algorithm>
#include <QFile>

static State *__instance = nullptr;

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
    memset(_chars, 0, sizeof(_chars));

    loadCharSet(":/c64-chargen.bin");
}

State::~State()
{

}

void State::loadCharSet(const std::string &filename)
{
    QFile file(QString::fromUtf8(filename.data(), filename.size()));

    if (!file.open(QIODevice::ReadOnly))
        return;

    auto size = file.size();
    int toRead = std::min((int)size, (int)sizeof(_chars));

    auto total = file.read(_chars, toRead);

    Q_ASSERT(total == toRead && "Failed to read file");

    file.close();
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
