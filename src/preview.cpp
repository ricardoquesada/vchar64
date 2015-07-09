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

}

void Preview::fileLoaded()
{
    State* state = State::getInstance();

    if(!xlink_ping()) return;

    xlink_load(0x37, 0x00, 0x3000, (uchar*) state->getChars(), State::CHAR_BUFFER_SIZE);
    xlink_poke(0x37, 0x00, 0xd018, 0x1c);
}

void Preview::tileUpdated(int tileIndex)
{
    State* state = State::getInstance();

    if(!xlink_ping()) return;

    xlink_load(0x37|0x80, 0x00, 0x3000 + tileIndex*8, (uchar*) state->getCharAtIndex(tileIndex), 8);
}

