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

#pragma once

#include <QObject>
#include <QVector>

#include "state.h"

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

class ServerPreview : public QObject {
    Q_OBJECT

public:
    static ServerPreview* getInstance();
    static void deleteInstance();

    bool isConnected();
    bool connect(const QString& ipaddress);
    void disconnect();

signals:
    void previewConnected();
    void previewDisconnected();

public slots:
    void onBytesWritten(qint64 bytes);
    void onReadyRead();
    void onConnected();
    void onDisconnected();

    // file loaded, or new project
    void fileLoaded();

    // when a range of bytes in the charset changes (e.g. due to a paste)
    void bytesUpdated(int pos, int count);

    // when the whole tile changes
    void tileUpdated(int tileIndex);

    // multi-color / hires or new colors
    void colorPropertiesUpdated(int pen);

    void multicolorModeUpdated(bool toggled);
    void tilePropertiesUpdated();

protected:
    ServerPreview();
    virtual ~ServerPreview() Q_DECL_OVERRIDE;

    // Proto: generic
    void protoPoke(quint16 addr, quint8 value);
    void protoPeek(quint16 addr, const quint8* value);
    void protoFill(quint16 addr, quint8 value, quint16 count);
    // don't call it directly. It is used internally for syncing purposes
    void protoPing(quint8 pingValue);

    // Proto: chars related
    void protoSetByte(quint16 addr, quint8 value);
    void protoSetChar(int charIdx, const quint8* charBuf);
    void protoSetChars(int charIdx, const quint8* charBuf, int totalChars);

    void sendOrQueueData(char* buffer, int bufferSize);
    void sendData(char* buffer, int bufferSize);

    //

    void updateBackgroundColor();
    void updateForegroundColor();
    void updateForegroundColorForCharset();
    void updateForegroundColorForTileset();
    void updateMulticolor1();
    void updateMulticolor2();
    void updateColorMode();
    void updateCharset();
    void updateTiles();
    void updateColorProperties();

    class ServerCommand {
    public:
        explicit ServerCommand(char* data, int dataSize)
            : _data(data)
            , _dataSize(dataSize)
        {
        }
        char* _data;
        int _dataSize;
    };

    QTcpSocket* _socket;
    int _bytesSent;
    bool _alreadyQueued;
    bool _readOnlyQueue;
    State::TileProperties _prevTileProperties;

    QVector<ServerCommand*> _commands;
    QVector<ServerCommand*> _tmpCommands;

    static ServerPreview* _instance;
};
