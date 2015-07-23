/****************************************************************************
Copyright 2015 Henning Bekel

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

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include "preview.h"

static Preview *__instance = nullptr;

Preview* Preview::getInstance()
{
    if (!__instance)
        __instance = new Preview();

    return __instance;
}

Preview::Preview()
    : xlink_ping(nullptr)
    , xlink_load(nullptr)
    , xlink_peek(nullptr)
    , xlink_poke(nullptr)
    , xlink_fill(nullptr)
{
    _xlink = new QLibrary("xlink");
    _xlink->load();
    if(_xlink->isLoaded()) {
        xlink_ping = (xlink_ping_t) _xlink->resolve("xlink_ping");
        xlink_load = (xlink_load_t) _xlink->resolve("xlink_load");
        xlink_poke = (xlink_poke_t) _xlink->resolve("xlink_poke");
        xlink_peek = (xlink_peek_t) _xlink->resolve("xlink_peek");
        xlink_fill = (xlink_fill_t) _xlink->resolve("xlink_fill");
    }
}

void Preview::updateBackgroundColor()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();
    xlink_poke(0x37, 0x00, 0xd020, (uchar) state->getColorAtIndex(0));
    xlink_poke(0x37, 0x00, 0xd021, (uchar) state->getColorAtIndex(0));
}

void Preview::updateForegroundColor()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();
    uchar foreground = state->getColorAtIndex(3);
    foreground |= state->isMultiColor() ? 8 : 0;

    xlink_fill(0xb7, 0x00, 0xd800, foreground, 1000);
    xlink_poke(0x37, 0x00, 0x0286, foreground);
}

void Preview::updateMulticolor1()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();
    xlink_poke(0x37, 0x00, 0xd022, (uchar) state->getColorAtIndex(1));
}

void Preview::updateMulticolor2()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();
    xlink_poke(0x37, 0x00, 0xd023, (uchar) state->getColorAtIndex(2));
}

void Preview::updateColorMode()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();
    uchar control = 0x08;

    xlink_peek(0x37, 0x00, 0xd016, &control);
    xlink_poke(0x37, 0x00, 0xd016, state->isMultiColor() ? 0x18 : 0x08);

    updateForegroundColor();
}

void Preview::updateColorProperties()
{
    updateBackgroundColor();
    updateMulticolor1();
    updateMulticolor2();
    updateColorMode(); // also updates foreground color
}

void Preview::updateCharset()
{
    if(!_xlink->isLoaded()) return;

    auto state = State::getInstance();

    xlink_load(0xb7, 0x00, 0x3000, (uchar*) state->getChars(), State::CHAR_BUFFER_SIZE);
    xlink_poke(0x37, 0x00, 0xd018, 0x1c);
}

bool Preview::updateScreen(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    char *screen = (char*) calloc(1000, sizeof(char));
    if(screen == NULL) return false;

    int size = file.read(screen, 1000);
    file.close();

    xlink_load(0x37, 0x00, 0x0400, (uchar*) screen, size);

    free(screen);
    return true;
}

void Preview::fileLoaded()
{
    if(!_xlink->isLoaded()) return;
    if(!xlink_ping()) return;

    updateScreen(":/c64-screen.bin");
    updateCharset();
    updateColorProperties();
}

void Preview::tileUpdated(int tileIndex)
{
    if(!_xlink->isLoaded()) return;
    if(!xlink_ping()) return;

    auto state = State::getInstance();

    State::TileProperties properties = state->getTileProperties();

    int charIndex = state->getCharIndexFromTileIndex(tileIndex);
    int numChars = properties.size.width() * properties.size.height();

    if(properties.interleaved == 1) {
        xlink_load(0xb7, 0x00, 0x3000 + charIndex * 8, (uchar*) state->getCharAtIndex(charIndex), numChars*8);
    }
    else {
        for(int sent=0; sent<numChars; sent++) {
            xlink_load(0xb7, 0x00, 0x3000 + charIndex * 8, (uchar*) state->getCharAtIndex(charIndex), 8);
            charIndex += properties.interleaved;
        }
    }
}

void Preview::colorSelected()
{
    if(!_xlink->isLoaded()) return;
    if(!xlink_ping()) return;

    auto state = State::getInstance();

    switch(state->getSelectedColorIndex()) {
    case 0: updateBackgroundColor(); break;
    case 1: updateMulticolor1(); break;
    case 2: updateMulticolor2(); break;
    case 3: updateForegroundColor(); break;
    }
}

void Preview::colorPropertiesUpdated()
{
    if(!_xlink->isLoaded()) return;

    updateColorMode();
}

