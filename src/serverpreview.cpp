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
    , _bytesSent(0)
    , _alreadyQueued(false)
    , _readOnlyQueue(false)
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

    _socket->connectToHost(ipaddress, VCHAR64_SERVER_LISTEN_PORT);

    // we need to wait...
    if(!_socket->waitForConnected(3000))
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
#pragma pack(push)
#pragma pack(1)
    struct {
        struct vchar64d_proto_header header;
        struct vchar64d_proto_ping payload;
    } data;
#pragma pack(pop)

    // read pong or peek
    while (_socket->bytesAvailable() < (qint64) sizeof(data))
    {
        if (!_socket->waitForReadyRead(2000))
        {
            qDebug() << "Error waiting";
            return;
        }
    }

    auto r = _socket->read((char*)&data, sizeof(data));
    if (r<0)
    {
        qDebug() << "Error reading";
        return;
    }

    if (data.header.type == TYPE_PONG && data.payload.something == 0)
    {
        _bytesSent = 0;
        _alreadyQueued = false;
        _readOnlyQueue = true;
        auto it = _commands.begin();
        while (it != _commands.end())
        {
            auto command = *it;
            sendOrQueueData(command->_data, command->_dataSize);
            it = _commands.erase(it);
            delete command;
        }
        _readOnlyQueue = false;

        _commands.append(_tmpCommands);
        _tmpCommands.clear();
    }
    else
        qDebug() << "Error in ping";
}

void ServerPreview::onConnected()
{
    emit previewConnected();
    updateColorProperties();
}

void ServerPreview::onDisconnected()
{
    emit previewDisconnected();
}

//
void ServerPreview::updateBackgroundColor()
{
    auto state = MainWindow::getCurrentState();
    protoPoke(0xd020, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
    protoPoke(0xd021, (uchar) state->getColorForPen(State::PEN_BACKGROUND));
}

void ServerPreview::updateForegroundColor()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    uchar foreground = state->getColorForPen(State::PEN_FOREGROUND);
    protoFill(0xd800, foreground, 40 * 25);
}

void ServerPreview::updateMulticolor1()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    protoPoke(0xd022, (uchar) state->getColorForPen(State::PEN_MULTICOLOR1));
}

void ServerPreview::updateMulticolor2()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
    protoPoke(0xd023, (uchar) state->getColorForPen(State::PEN_MULTICOLOR2));
}

void ServerPreview::updateColorMode()
{
    if(!isConnected()) return;

    auto state = MainWindow::getCurrentState();
//    uchar control = 0x08;
//    xlink_peek(0x37, 0x00, 0xd016, &control);
    protoPoke(0xd016, state->isMulticolorMode() ? 0x18 : 0x08);

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
    auto state = MainWindow::getCurrentState();
    auto charset = state->getCharsetBuffer();

    // send the charset in 2 parts to avoid filling the C64 MTU buffer
    const int segments = 2;
    const int segmentSize = 256 / segments;
    for (int i=0; i<segments; i++)
    {
        protoSetChars(segmentSize*i, &charset[segmentSize*8*i], segmentSize);
    }
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
    updateCharset();
    updateColorProperties();
}

void ServerPreview::byteUpdated(int byteIndex)
{
    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

    protoSetByte(byteIndex, state->getCharsetBuffer()[byteIndex]);
    protoFlush();
}

void ServerPreview::bytesUpdated(int pos, int count)
{
    Q_ASSERT(pos % 8 == 0 && "Invalid pos value");
    Q_ASSERT(count % 8 == 0 && "Invalid count value");

    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

    protoSetChars(pos/8, state->getCharAtIndex(pos/8), count/8);
    protoFlush();
}

void ServerPreview::tileUpdated(int tileIndex)
{
    if(!isConnected()) return;
    auto state = MainWindow::getCurrentState();

    State::TileProperties properties = state->getTileProperties();

    int charIndex = state->getCharIndexFromTileIndex(tileIndex);
    int numChars = properties.size.width() * properties.size.height();

    if(properties.interleaved == 1) {
        protoSetChars(charIndex, state->getCharAtIndex(charIndex), numChars);
    }
    else {
        for(int sent=0; sent<numChars; sent++) {
            protoSetChars(charIndex, state->getCharAtIndex(charIndex), 1);
            charIndex += properties.interleaved;
        }
    }
    protoFlush();
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

//
// Proto methods
//
void  ServerPreview::protoFlush()
{
    _socket->flush();
}

void  ServerPreview::protoPing(quint8 pingValue)
{
#pragma pack(push)
#pragma pack(1)
    struct _data {
        struct vchar64d_proto_header header;
        struct vchar64d_proto_ping payload;
    };
#pragma pack(pop)

    struct _data* data = (struct _data*) malloc(sizeof(*data));

    data->header.type = TYPE_PING;
    data->payload.something = pingValue;
    sendData((char*)data, sizeof(*data));

//    _socket->waitForBytesWritten();
//    while (_socket->bytesAvailable() < (qint64) sizeof(data))
//    {
//        if (!_socket->waitForReadyRead(2000))
//        {
//            qDebug() << "Error waiting";
//            return;
//        }
//    }

//    memset(&data, 0, sizeof(data));
//    auto r = _socket->read((char*)&data, sizeof(data));
//    if (r<0)
//    {
//        qDebug() << "Error reading";
//        return;
//    }

//    if (data.header.type == TYPE_PONG && data.payload.something == pingValue)
//        return;

//    qDebug() << "Error in ping";
}

void ServerPreview::protoPoke(quint16 addr, quint8 value)
{
#pragma pack(push)
#pragma pack(1)
    struct _data{
        struct vchar64d_proto_header header;
        struct vchar64d_proto_poke payload;
    };
#pragma pack(pop)

    struct _data* data = (struct _data*) malloc(sizeof(*data));

    data->header.type = TYPE_POKE;
    data->payload.addr = qToLittleEndian(addr);
    data->payload.value = value;
    sendOrQueueData((char*)data, sizeof(*data));
}

void ServerPreview::protoPeek(quint16 addr, quint8* value)
{
    Q_UNUSED(addr);
    Q_UNUSED(value);
}


void ServerPreview::protoFill(quint16 addr, quint8 value, quint16 count)
{
#pragma pack(push)
#pragma pack(1)
    struct _data{
        struct vchar64d_proto_header header;
        struct vchar64d_proto_fill payload;
    };
#pragma pack(pop)

    struct _data* data = (struct _data*) malloc(sizeof(*data));

    data->header.type = TYPE_FILL;
    data->payload.addr = qToLittleEndian(addr);
    data->payload.value = value;
    data->payload.count = count;
    sendOrQueueData((char*)data, sizeof(*data));
}

void ServerPreview::protoSetByte(quint16 addr, quint8 value)
{
#pragma pack(push)
#pragma pack(1)
    struct _data {
        struct vchar64d_proto_header header;
        struct vchar64d_proto_set_byte payload;
    };
#pragma pack(pop)

    struct _data* data = (struct _data*) malloc(sizeof(*data));

    data->header.type = TYPE_SET_BYTE;
    data->payload.idx = qToLittleEndian(addr);
    data->payload.byte = value;
    sendOrQueueData((char*)data, sizeof(*data));
}

void ServerPreview::protoSetChar(int charIdx, quint8 *charBuf)
{
#pragma pack(push)
#pragma pack(1)
    struct _data {
        struct vchar64d_proto_header header;
        struct vchar64d_proto_set_char payload;
    };
#pragma pack(pop)

    struct _data* data = (struct _data*) malloc(sizeof(*data));

    data->header.type = TYPE_SET_CHAR;
    data->payload.idx = charIdx;
    memcpy(data->payload.chardata, charBuf, sizeof(data->payload.chardata));

    sendOrQueueData((char*)data, sizeof(*data));
}

void ServerPreview::protoSetChars(int charIdx, quint8* charBuf, int totalChars)
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

    sendOrQueueData(data, size);
}

void ServerPreview::sendOrQueueData(char* buffer, int bufferSize)
{
    if (_alreadyQueued || _bytesSent + bufferSize > VCHAR64_SERVER_BUFFER_SIZE)
    {
        // won't enter into infinite loop since
        // it will go directly to sendData() and sendData() doesn't sync
        if (!_alreadyQueued)
            protoPing(0);
        if (_readOnlyQueue)
            _tmpCommands.append(new ServerCommand(buffer, bufferSize));
        else
            _commands.append(new ServerCommand(buffer, bufferSize));
        _alreadyQueued = true;
    }
    else
    {
        sendData(buffer, bufferSize);
    }
}

void ServerPreview::sendData(char* buffer, int bufferSize)
{
    _socket->write(buffer, bufferSize);
    free(buffer);
    _bytesSent += bufferSize;
}
