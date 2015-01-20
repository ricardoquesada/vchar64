#include "state.h"

#include <algorithm>
#include <QFile.h>

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
    memset(_chars, sizeof(_chars),0);

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

    file.close();
}
