/****************************************************************************
Copyright 2015-2026 Ricardo Quesada
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

/**
 * @file serverpreview.h
 * @brief Asynchronous `QTcpSocket` client for C64 Contiki `vchar64d` live preview (`vchar64_net`).
 *
 * Bridges active `State` document mutations over TCP port `6464` to real Commodore 64
 * hardware (RR-Net / Ultimate64 / 64NIC+) or VICE using non-blocking event-loop I/O,
 * `1,400`-byte Contiki uIP window tracking, and `256 KiB` backpressure coalescing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <QAbstractSocket>
#include <QByteArray>
#include <QMetaObject>
#include <QString>
#include <QTimer>

#include "preview/ilivepreviewsink.h"
#include "serverprotocol.h"
#include "state.h"

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

/**
 * @brief Asynchronous TCP live-preview client for Contiki `server/vchar64d.c`.
 *
 * Enforces Contiki's 1,400-byte uIP receive window via 2-byte `TYPE_PING` /
 * `TYPE_PONG` barriers, splits `TYPE_SET_CHARS` transfers into `<= 128`-glyph
 * packets, and bounds queued delta memory to `256 KiB` under stalled-modem
 * backpressure by coalescing into `_fullSyncPending`.
 */
class ServerPreview : public ILivePreviewSink {
    Q_OBJECT

public:
    /** @brief Maximum bytes (`1,400`) transmitted per window, including the trailing 2-byte `TYPE_PING`. */
    static constexpr int kContikiWindowBytes = VCHAR64_SERVER_BUFFER_SIZE;
    /** @brief Wire size in bytes (`2`) of a `TYPE_PING` or `TYPE_PONG` barrier frame. */
    static constexpr int kPingFrameBytes = 2;
    /** @brief Maximum glyphs (`128`, `1,027` wire bytes) per `TYPE_SET_CHARS` packet so each packet fits in `1,398` B. */
    static constexpr int kMaxCharsPerPacket = 128;
    /** @brief Maximum queued delta bytes (`256 KiB`) before pending edits coalesce into a single `_fullSyncPending`. */
    static constexpr size_t kMaxBackpressureBytes = 256U * 1024U;
    /** @brief Watchdog timeout (`10,000` ms) waiting for `TYPE_PONG` before declaring the link stalled. */
    static constexpr int kPongTimeoutMs = 10000;

    static ServerPreview* getInstance();
    static void deleteInstance();

    explicit ServerPreview(QObject* parent = nullptr);
    ~ServerPreview() override;

    void setActiveState(const State* state) override;
    [[nodiscard]] const State* activeState() const noexcept override { return _activeState; }

    void connectPreview(
        const QString& host,
        quint16 port = static_cast<quint16>(VCHAR64_SERVER_LISTEN_PORT)) override;
    void disconnectPreview() override;
    [[nodiscard]] bool isConnected() const override;
    [[nodiscard]] bool isConnecting() const;

    /** @brief Returns total bytes currently buffered in `_txQueue` awaiting Contiki window credit. */
    [[nodiscard]] size_t queuedBytes() const noexcept { return _queuedBytes; }
    /** @brief Returns `true` when transmit is paused awaiting a 2-byte `TYPE_PONG` matching `_expectedPongToken`. */
    [[nodiscard]] bool isAwaitingPong() const noexcept { return _awaitingPong; }
    /** @brief Returns `true` when backpressure dropped intermediate deltas in favor of a coalesced full sync. */
    [[nodiscard]] bool isFullSyncPending() const noexcept { return _fullSyncPending; }

public slots:
    void fileLoaded();
    void charsetUpdated();
    void bytesUpdated(int pos, int count);
    void tileUpdated(int tileIndex);
    void colorPropertiesUpdated(int pen);
    void multicolorModeUpdated(bool toggled);
    void tilePropertiesUpdated();

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onPongTimeout();

private:
    void detachStateConnections();
    void resetTransportState() noexcept;
    void requestFullSync();
    void enqueueFullSyncPackets();

    void enqueuePacket(QByteArray packet);
    void drainTxQueue();
    void sendPingBarrier();
    void writeRawToSocket(const QByteArray& bytes);

    // Wire-packet builders (little-endian, zero host-pointer struct packing)
    [[nodiscard]] static QByteArray makePokePacket(quint16 addr, uint8_t value);
    [[nodiscard]] static QByteArray makeFillPacket(quint16 addr, uint8_t value, quint16 count);
    [[nodiscard]] static QByteArray makeSetMemPacket(quint16 addr, const uint8_t* data, quint16 count);
    [[nodiscard]] static QByteArray makeSetBytePacket(quint16 idx, uint8_t value);
    [[nodiscard]] static QByteArray makeSetCharPacket(uint8_t charIdx, const uint8_t* charData8);
    [[nodiscard]] static QByteArray makeSetCharsChunkPacket(
        uint8_t charIdx,
        const uint8_t* charBuf,
        uint8_t count);

    void protoPoke(quint16 addr, uint8_t value);
    void protoFill(quint16 addr, uint8_t value, quint16 count);
    void protoSetMem(quint16 addr, const uint8_t* data, quint16 count);
    void protoSetByte(quint16 idx, uint8_t value);
    void protoSetChar(int charIdx, const uint8_t* charBuf);
    void protoSetChars(int charIdx, const uint8_t* charBuf, int totalChars);

    void enqueueBackgroundColorPackets();
    void enqueueMulticolor1Packet();
    void enqueueMulticolor2Packet();
    void enqueueForegroundColorPackets();
    void enqueueColorModePacket();
    void enqueueAllColorPackets();
    void enqueueCharsetPackets();
    void enqueueTilesScreenPacket();

    const State* _activeState = nullptr;
    std::vector<QMetaObject::Connection> _stateConnections;

    std::unique_ptr<QTcpSocket> _socket;
    QTimer _pongTimer;

    std::deque<QByteArray> _txQueue;
    size_t _queuedBytes = 0;
    QByteArray _rxBuffer;

    int _inFlightBytes = 0;
    bool _awaitingPong = false;
    bool _fullSyncPending = false;
    bool _enqueueingFullSync = false;
    uint8_t _expectedPongToken = 0;
    uint8_t _nextPingToken = 0;

    static ServerPreview* _instance;
};
