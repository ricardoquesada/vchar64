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

#include <QFile>
#include <QFileInfo>

#include "xlinkpreview.h"
#include "mainwindow.h"

static XlinkPreview *__instance = nullptr;

XlinkPreview* XlinkPreview::getInstance()
{
    if (!__instance)
        __instance = new XlinkPreview();

    return __instance;
}

bool XlinkPreview::isConnected()
{
    if(!_available) return false;
    if(!_connected) return false;

    if(!xlink_ping()) {
        _connected = false;
        emit previewDisconnected();
        return false;
    }
    return true;
}

bool XlinkPreview::connect()
{
    if((_connected = xlink_ping())) {
        fileLoaded();
        emit previewConnected();
    }
    return _connected;
}

void XlinkPreview::disconnect()
{
    _connected = false;
    emit previewDisconnected();
}

XlinkPreview::XlinkPreview()
    : _available(false)
    , _connected(false)
    , xlink_ping(nullptr)
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

        _available = true;
        connect();
    }
}

void XlinkPreview::updateBackgroundColor()
{
    if(!_xlink->isLoaded()) return;

    auto state = MainWindow::getCurrentState();
    xlink_poke(0x37, 0x00, 0xd020, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
    xlink_poke(0x37, 0x00, 0xd021, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
}

void XlinkPreview::updateForegroundColor()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    uchar foreground = state->getColorForPen(State::PEN_FOREGROUND, state->getTileIndex());
    foreground |= state->isMulticolorMode() ? 8 : 0;

    xlink_fill(0xb7, 0x00, 0xd800, foreground, 1000);
    xlink_poke(0x37, 0x00, 0x0286, foreground);
}

void XlinkPreview::updateMulticolor1()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    xlink_poke(0x37, 0x00, 0xd022, (uchar) state->getColorForPen(State::PEN_MULTICOLOR1));
}

void XlinkPreview::updateMulticolor2()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    xlink_poke(0x37, 0x00, 0xd023, (uchar) state->getColorForPen(State::PEN_MULTICOLOR2));
}

void XlinkPreview::updateColorMode()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    uchar control = 0x08;

    xlink_peek(0x37, 0x00, 0xd016, &control);
    xlink_poke(0x37, 0x00, 0xd016, state->isMulticolorMode() ? 0x18 : 0x08);

    updateForegroundColor();
}

void XlinkPreview::updateColorProperties()
{
    updateBackgroundColor();
    updateMulticolor1();
    updateMulticolor2();
    updateColorMode(); // also updates foreground color
}

void XlinkPreview::updateCharset()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();

    xlink_load(0xb7, 0x00, 0x3000, (const uchar*) state->getCharsetBuffer(), State::CHAR_BUFFER_SIZE);
    xlink_poke(0x37, 0x00, 0xd018, 0x1c);
}

bool XlinkPreview::updateScreen(const QString& filename)
{
    if(!isConnected()) return false;

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

void XlinkPreview::install()
{
    updateScreen(":/c64-screen.bin");
    updateCharset();
    updateColorProperties();
}

void XlinkPreview::fileLoaded()
{
    if(!isConnected()) return;

    install();
}

void XlinkPreview::bytesUpdated(int pos, int count)
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    xlink_load(0xb7, 0x00, 0x3000+pos, state->getCharsetBuffer()+pos, count);
}

void XlinkPreview::tileUpdated(int tileIndex)
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();

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

void XlinkPreview::colorSelected()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();

    switch(state->getSelectedPen()) {
    case 0: updateBackgroundColor(); break;
    case 1: updateMulticolor1(); break;
    case 2: updateMulticolor2(); break;
    case 3: updateForegroundColor(); break;
    }
}

void XlinkPreview::colorPropertiesUpdated()
{
    if(!isConnected()) return;

    updateColorMode();
}
