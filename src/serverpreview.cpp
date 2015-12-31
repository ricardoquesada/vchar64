/****************************************************************************
Copyright 2015 Ricardo Quesada
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

#include "serverpreview.h"

#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QtEndian>

#include <string.h>

#include "serverprotocol.h"
#include "mainwindow.h"

static ServerPreview *__instance = nullptr;

ServerPreview* ServerPreview::getInstance()
{
    if (!__instance)
        __instance = new ServerPreview();

    return __instance;
}

ServerPreview::ServerPreview()
    : _socket(nullptr)
{
}

ServerPreview::~ServerPreview()
{
}

bool ServerPreview::isConnected()
{
    return (_socket && _socket->state() == QTcpSocket::ConnectedState);
}

bool ServerPreview::connect(const QString &ipaddress)
{
    _socket = new QTcpSocket(this);

    QObject::connect(_socket, &QTcpSocket::connected, this, &ServerPreview::onConnected);
    QObject::connect(_socket, &QTcpSocket::disconnected, this, &ServerPreview::onDisconnected);
    QObject::connect(_socket, &QTcpSocket::bytesWritten, this, &ServerPreview::onBytesWritten);
    QObject::connect(_socket, &QTcpSocket::readyRead, this, &ServerPreview::onReadyRead);

    _socket->connectToHost(ipaddress, 6464);

    // we need to wait...
    if(!_socket->waitForConnected(5000))
    {
        qDebug() << "Error: " << _socket->errorString();
        return false;
    }
    return true;
}

void ServerPreview::disconnect()
{
    if (_socket)
        _socket->disconnectFromHost();
}

// SLOTS
void ServerPreview::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
}

void ServerPreview::onReadyRead()
{

}

void ServerPreview::onConnected()
{
    emit previewConnected();
}

void ServerPreview::onDisconnected()
{
    emit previewDisconnected();
}

//
void ServerPreview::updateBackgroundColor()
{
//    auto state = MainWindow::getCurrentState();
//    xlink_poke(0x37, 0x00, 0xd020, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
//    xlink_poke(0x37, 0x00, 0xd021, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
}

void ServerPreview::updateForegroundColor()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    uchar foreground = state->getColorForPen(State::PEN_FOREGROUND);
    foreground |= state->isMulticolorMode() ? 8 : 0;

//    xlink_fill(0xb7, 0x00, 0xd800, foreground, 1000);
//    xlink_poke(0x37, 0x00, 0x0286, foreground);
}

void ServerPreview::updateMulticolor1()
{
    if(!isConnected()) return;

//    auto state = MainWindow::getCurrentState();
//    xlink_poke(0x37, 0x00, 0xd022, (uchar) state->getColorForPen(State::PEN_MULTICOLOR1));
}

void ServerPreview::updateMulticolor2()
{
    if(!isConnected()) return;

//    auto state = MainWindow::getCurrentState();
//    xlink_poke(0x37, 0x00, 0xd023, (uchar) state->getColorForPen(State::PEN_MULTICOLOR2));
}

void ServerPreview::updateColorMode()
{
    if(!isConnected()) return;

//    auto state = MainWindow::getCurrentState();
//    uchar control = 0x08;

//    xlink_peek(0x37, 0x00, 0xd016, &control);
//    xlink_poke(0x37, 0x00, 0xd016, state->isMulticolorMode() ? 0x18 : 0x08);

    updateForegroundColor();
}

void ServerPreview::updateColorProperties()
{
    updateBackgroundColor();
    updateMulticolor1();
    updateMulticolor2();
    updateColorMode(); // also updates foreground color
}

void ServerPreview::updateCharset()
{
    if(!isConnected()) return;

//    auto state = MainWindow::getCurrentState();

//    xlink_load(0xb7, 0x00, 0x3000, (uchar*) state->getCharsetBuffer(), State::CHAR_BUFFER_SIZE);
//    xlink_poke(0x37, 0x00, 0xd018, 0x1c);
}

bool ServerPreview::updateScreen(const QString& filename)
{
    if(!isConnected()) return false;

    Q_UNUSED(filename);

//    QFile file(filename);

//    if (!file.open(QIODevice::ReadOnly))
//        return false;

//    char *screen = (char*) calloc(1000, sizeof(char));
//    if(screen == NULL) return false;

//    int size = file.read(screen, 1000);
//    file.close();

//    xlink_load(0x37, 0x00, 0x0400, (uchar*) screen, size);

//    free(screen);

        return true;
}

void ServerPreview::fileLoaded()
{
    if(!isConnected()) return;
}

void ServerPreview::byteUpdated(int byteIndex)
{
    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

#pragma pack(push)
#pragma pack(1)
    struct {
        struct vchar64d_proto_header header;
        struct vchar64d_proto_set_byte payload;
    } data;
#pragma pack(pop)

    data.header.type = TYPE_SET_BYTE;
    data.payload.idx = qToLittleEndian((uint16_t)byteIndex);
    data.payload.byte = state->getCharsetBuffer()[byteIndex];
    _socket->write((const char*)&data, sizeof(data));
    _socket->flush();
}

void ServerPreview::bytesUpdated(int pos, int count)
{
    Q_ASSERT(pos % 8 == 0 && "Invalid pos value");
    Q_ASSERT(count % 8 == 0 && "Invalid count value");

    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

    sendChars(pos/8, state->getCharAtIndex(pos/8), count/8);
}

void ServerPreview::tileUpdated(int tileIndex)
{
    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

    State::TileProperties properties = state->getTileProperties();

    int charIndex = state->getCharIndexFromTileIndex(tileIndex);
    int numChars = properties.size.width() * properties.size.height();

    if(properties.interleaved == 1) {
        sendChars(charIndex, state->getCharAtIndex(charIndex), numChars);
    }
    else {
        for(int sent=0; sent<numChars; sent++) {
            sendChars(charIndex, state->getCharAtIndex(charIndex), 1);
            charIndex += properties.interleaved;
        }
    }
}

void ServerPreview::colorSelected()
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

void ServerPreview::colorPropertiesUpdated()
{
    if(!isConnected()) return;

    updateColorMode();
}

// helper
void ServerPreview::sendChars(int charIdx, quint8* charBuf, int totalChars)
{
    struct vchar64d_proto_header* header;
    struct vchar64d_proto_set_chars* payload;

    int size = sizeof(*header) + (sizeof(*payload) - sizeof(payload->charsdata)) + totalChars * 8;
    char* data = (char*) malloc(size);

    header = (struct vchar64d_proto_header*) data;
    payload = (struct vchar64d_proto_set_chars*) (data + sizeof(*header));

    header->type = TYPE_SET_CHARS;
    payload->idx = charIdx;
    payload->count = totalChars;
    memcpy(&payload->charsdata, charBuf, totalChars * 8);

    _socket->write(data, size);
    _socket->flush();
    free(data);
}

