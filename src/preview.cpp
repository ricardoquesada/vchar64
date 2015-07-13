#include <QDebug>

#include "preview.h"

static Preview *__instance = nullptr;

Preview* Preview::getInstance()
{
    if (!__instance)
        __instance = new Preview();

    return __instance;
}

Preview::Preview()
{
    xlink = new QLibrary("xlink");
    xlink->load();

    if(xlink->isLoaded()) {
        xlink_ping = (xlink_ping_t) xlink->resolve("xlink_ping");
        xlink_load = (xlink_load_t) xlink->resolve("xlink_load");
        xlink_poke = (xlink_poke_t) xlink->resolve("xlink_poke");
    }
}

void Preview::fileLoaded()
{
    if(!xlink->isLoaded()) return;
    if(!xlink_ping()) return;

    State* state = State::getInstance();

    xlink_load(0x37, 0x00, 0x3000, (uchar*) state->getChars(), State::CHAR_BUFFER_SIZE);
    xlink_poke(0x37, 0x00, 0xd018, 0x1c);
}

void Preview::tileUpdated(int tileIndex)
{
    if(!xlink->isLoaded()) return;
    if(!xlink_ping()) return;

    State* state = State::getInstance();

    xlink_load(0x37|0x80, 0x00, 0x3000 + tileIndex*8, (uchar*) state->getCharAtIndex(tileIndex), 8);
}

