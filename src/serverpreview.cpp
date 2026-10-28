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
 * @file serverpreview.cpp
 * @brief Non-blocking Contiki `vchar64d` TCP framing, uIP window flow control, and backpressure (`vchar64_net`).
 *
 * Key wire invariants:
 *
 * - **Zero blocking socket waits:** Never calls `waitForConnected`, `waitForBytesWritten`,
 *   or `waitForReadyRead` on the UI event loop.
 * - **1,400-byte uIP window reservation:** `drainTxQueue()` reserves `kPingFrameBytes` (2 B)
 *   inside `VCHAR64_SERVER_BUFFER_SIZE` so appending `TYPE_PING` never overflows Contiki's
 *   single-MSS receive buffer, and chunks `TYPE_SET_CHARS` into `<= 128` glyphs (`1,027` B).
 * - **Explicit little-endian wire encoding:** Serializes all 16-bit C64 addresses and counts
 *   byte-by-byte via `appendUint16LE()` rather than dumping host structs containing pointers.
 */

#include "serverpreview.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <QTcpSocket>
#include <QtEndian>

namespace {

constexpr quint16 kVicBorderColorReg = 0xD020U;
constexpr quint16 kVicBackgroundColorReg = 0xD021U;
constexpr quint16 kVicMulticolor1Reg = 0xD022U;
constexpr quint16 kVicMulticolor2Reg = 0xD023U;
constexpr quint16 kVicControlReg2 = 0xD016U;

constexpr uint8_t kVicControlHiRes40Col = 0x08U;
constexpr uint8_t kVicControlMulticolor40Col = 0x18U;

// `server/vchar64d.c` places VIC-II Screen RAM at $A400 (Bank 2, +$2400) and Color RAM at $D800.
constexpr quint16 kC64RemappedScreenBase = 0xA400U;
constexpr quint16 kC64ColorRamBase = 0xD800U;

constexpr quint16 kC64TileScreenSliceAddr = static_cast<quint16>(
    kC64RemappedScreenBase + vchar64::kC64TileRegionOffset); // 0xA5E0
constexpr quint16 kC64TileColorSliceAddr = static_cast<quint16>(
    kC64ColorRamBase + vchar64::kC64TileRegionOffset); // 0xD9E0

/** @brief Appends a 16-bit integer to `packet` in 6510 little-endian byte order. */
void appendUint16LE(QByteArray& packet, quint16 value)
{
    const quint16 le = qToLittleEndian<quint16>(value);
    char bytes[sizeof(quint16)];
    std::memcpy(bytes, &le, sizeof(le));
    packet.append(bytes, static_cast<qsizetype>(sizeof(bytes)));
}

} // namespace

ServerPreview* ServerPreview::_instance = nullptr;

ServerPreview* ServerPreview::getInstance()
{
    if (_instance == nullptr) {
        _instance = new ServerPreview();
    }
    return _instance;
}

void ServerPreview::deleteInstance()
{
    delete _instance;
    _instance = nullptr;
}

ServerPreview::ServerPreview(QObject* parent)
    : ILivePreviewSink(parent)
{
    _pongTimer.setSingleShot(true);
    _pongTimer.setInterval(kPongTimeoutMs);
    QObject::connect(&_pongTimer, &QTimer::timeout, this, &ServerPreview::onPongTimeout);
}

ServerPreview::~ServerPreview()
{
    detachStateConnections();
    _pongTimer.stop();
    if (_socket) {
        _socket->disconnect(this);
        _socket->abort();
        _socket.reset();
    }
}

void ServerPreview::detachStateConnections()
{
    for (const QMetaObject::Connection& conn : _stateConnections) {
        QObject::disconnect(conn);
    }
    _stateConnections.clear();
}

void ServerPreview::resetTransportState() noexcept
{
    _pongTimer.stop();
    _txQueue.clear();
    _queuedBytes = 0;
    _rxBuffer.clear();
    _inFlightBytes = 0;
    _awaitingPong = false;
    _fullSyncPending = false;
    _enqueueingFullSync = false;
}

void ServerPreview::setActiveState(const State* state)
{
    if (_activeState == state) {
        return;
    }

    detachStateConnections();
    _activeState = state;

    if (_activeState == nullptr) {
        _txQueue.clear();
        _queuedBytes = 0;
        _fullSyncPending = false;
        return;
    }

    _stateConnections.reserve(8);
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::fileLoaded, this, &ServerPreview::fileLoaded));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::charsetUpdated, this, &ServerPreview::charsetUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::bytesUpdated, this, &ServerPreview::bytesUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::tileUpdated, this, &ServerPreview::tileUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::colorPropertiesUpdated, this, &ServerPreview::colorPropertiesUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::multicolorModeToggled, this, &ServerPreview::multicolorModeUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &State::tilePropertiesUpdated, this, &ServerPreview::tilePropertiesUpdated));
    _stateConnections.push_back(
        QObject::connect(_activeState, &QObject::destroyed, this, [this, state]() {
            if (_activeState == state) {
                setActiveState(nullptr);
            }
        }));

    if (isConnected()) {
        requestFullSync();
    }
}

bool ServerPreview::isConnected() const
{
    return _socket != nullptr && _socket->state() == QAbstractSocket::ConnectedState;
}

bool ServerPreview::isConnecting() const
{
    if (_socket == nullptr) {
        return false;
    }
    const QAbstractSocket::SocketState st = _socket->state();
    return st == QAbstractSocket::HostLookupState || st == QAbstractSocket::ConnectingState;
}

void ServerPreview::connectPreview(const QString& host, quint16 port)
{
    const QString trimmedHost = host.trimmed();
    if (trimmedHost.isEmpty() || port == 0U) {
        emit previewError(tr("Invalid live preview host or port."));
        return;
    }

    bool notifyDisconnect = false;
    if (_socket) {
        auto oldSocket = std::move(_socket);
        oldSocket->disconnect(this);
        notifyDisconnect = (oldSocket->state() == QAbstractSocket::ConnectedState);
        oldSocket->abort();
        oldSocket.release()->deleteLater();
    }
    resetTransportState();
    if (notifyDisconnect) {
        emit previewDisconnected();
    }

    _socket = std::make_unique<QTcpSocket>();
    QObject::connect(_socket.get(), &QTcpSocket::connected, this, &ServerPreview::onConnected);
    QObject::connect(_socket.get(), &QTcpSocket::disconnected, this, &ServerPreview::onDisconnected);
    QObject::connect(_socket.get(), &QTcpSocket::readyRead, this, &ServerPreview::onReadyRead);
    QObject::connect(_socket.get(), &QAbstractSocket::errorOccurred, this, &ServerPreview::onSocketError);

    _socket->connectToHost(trimmedHost, port);
}

void ServerPreview::disconnectPreview()
{
    resetTransportState();
    if (!_socket) {
        return;
    }

    const QAbstractSocket::SocketState st = _socket->state();
    if (st == QAbstractSocket::ConnectedState) {
        _socket->disconnectFromHost();
    } else if (st != QAbstractSocket::UnconnectedState) {
        _socket->abort();
    }
}

void ServerPreview::onConnected()
{
    resetTransportState();
    emit previewConnected();

    if (_activeState != nullptr) {
        enqueueFullSyncPackets();
        drainTxQueue();
    }
}

void ServerPreview::onDisconnected()
{
    resetTransportState();
    emit previewDisconnected();
}

void ServerPreview::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    resetTransportState();
    const QString errMsg = _socket ? _socket->errorString() : tr("Unknown TCP socket error.");
    emit previewError(errMsg);
}

void ServerPreview::onPongTimeout()
{
    if (!_awaitingPong) {
        return;
    }
    emit previewError(tr("Timed out waiting for C64 live preview PONG acknowledgment."));
    disconnectPreview();
}

void ServerPreview::onReadyRead()
{
    if (!_socket) {
        return;
    }

    _rxBuffer.append(_socket->readAll());

    while (_rxBuffer.size() >= kPingFrameBytes) {
        const auto frameType = static_cast<uint8_t>(_rxBuffer.at(0));
        const auto token = static_cast<uint8_t>(_rxBuffer.at(1));

        if (frameType != static_cast<uint8_t>(TYPE_PONG)) {
            _rxBuffer.clear();
            emit previewError(tr("Unexpected protocol response from C64 live preview daemon."));
            disconnectPreview();
            return;
        }

        _rxBuffer.remove(0, kPingFrameBytes);
        if (_awaitingPong && token == _expectedPongToken) {
            _pongTimer.stop();
            _awaitingPong = false;
            _inFlightBytes = 0;
            drainTxQueue();
        }
    }
}

void ServerPreview::requestFullSync()
{
    if (!isConnected() || _activeState == nullptr) {
        return;
    }

    _txQueue.clear();
    _queuedBytes = 0;

    if (_awaitingPong) {
        _fullSyncPending = true;
        return;
    }

    _fullSyncPending = false;
    enqueueFullSyncPackets();
    drainTxQueue();
}

void ServerPreview::enqueueFullSyncPackets()
{
    if (_activeState == nullptr) {
        return;
    }

    _enqueueingFullSync = true;
    enqueueCharsetPackets();
    enqueueAllColorPackets();
    enqueueTilesScreenPacket();
    _enqueueingFullSync = false;
}

void ServerPreview::enqueuePacket(QByteArray packet)
{
    if (!isConnected() || packet.isEmpty()) {
        return;
    }

    // While coalescing stalled-modem backpressure, drop redundant intermediate deltas;
    // the full active State will be materialized as soon as TYPE_PONG arrives.
    if (_fullSyncPending && !_enqueueingFullSync) {
        return;
    }

    const auto pktBytes = static_cast<size_t>(packet.size());
    if (!_enqueueingFullSync && (_queuedBytes + pktBytes > kMaxBackpressureBytes)) {
        _txQueue.clear();
        _queuedBytes = 0;
        _fullSyncPending = true;
        if (!_awaitingPong) {
            sendPingBarrier();
        }
        return;
    }

    _queuedBytes += pktBytes;
    _txQueue.push_back(std::move(packet));

    if (!_enqueueingFullSync) {
        drainTxQueue();
    }
}

void ServerPreview::drainTxQueue()
{
    if (!isConnected() || _awaitingPong) {
        return;
    }

    if (_fullSyncPending && _txQueue.empty()) {
        _fullSyncPending = false;
        enqueueFullSyncPackets();
    }

    while (!_txQueue.empty()) {
        const QByteArray& nextPacket = _txQueue.front();
        const int pktSize = static_cast<int>(nextPacket.size());

        // Reserve 2 bytes inside Contiki's 1,400-byte uIP receive window so the
        // TYPE_PING synchronization frame itself never pushes in-flight bytes > 1,400.
        if (_inFlightBytes > 0 && (_inFlightBytes + pktSize + kPingFrameBytes) > kContikiWindowBytes) {
            sendPingBarrier();
            return;
        }

        QByteArray packet = std::move(_txQueue.front());
        _txQueue.pop_front();
        _queuedBytes -= static_cast<size_t>(packet.size());

        writeRawToSocket(packet);

        if ((_inFlightBytes + kPingFrameBytes) >= kContikiWindowBytes
            && (!_txQueue.empty() || _fullSyncPending)) {
            sendPingBarrier();
            return;
        }
    }
}

void ServerPreview::sendPingBarrier()
{
    if (!isConnected() || _awaitingPong) {
        return;
    }

    const uint8_t token = _nextPingToken++;
    QByteArray pingPacket;
    pingPacket.reserve(kPingFrameBytes);
    pingPacket.append(static_cast<char>(static_cast<uint8_t>(TYPE_PING)));
    pingPacket.append(static_cast<char>(token));

    _expectedPongToken = token;
    _awaitingPong = true;
    writeRawToSocket(pingPacket);
    _pongTimer.start();
}

void ServerPreview::writeRawToSocket(const QByteArray& bytes)
{
    if (!_socket || bytes.isEmpty()) {
        return;
    }
    _socket->write(bytes);
    _inFlightBytes += static_cast<int>(bytes.size());
}

//
// Wire-packet builders
//

QByteArray ServerPreview::makePokePacket(quint16 addr, uint8_t value)
{
    QByteArray packet;
    packet.reserve(4);
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_POKE)));
    appendUint16LE(packet, addr);
    packet.append(static_cast<char>(value));
    return packet;
}

QByteArray ServerPreview::makeFillPacket(quint16 addr, uint8_t value, quint16 count)
{
    QByteArray packet;
    packet.reserve(6);
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_FILL)));
    appendUint16LE(packet, addr);
    packet.append(static_cast<char>(value));
    appendUint16LE(packet, count);
    return packet;
}

QByteArray ServerPreview::makeSetMemPacket(quint16 addr, const uint8_t* data, quint16 count)
{
    QByteArray packet;
    packet.reserve(5 + static_cast<qsizetype>(count));
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_SET_MEM)));
    appendUint16LE(packet, addr);
    appendUint16LE(packet, count);
    packet.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(count));
    return packet;
}

QByteArray ServerPreview::makeSetBytePacket(quint16 idx, uint8_t value)
{
    QByteArray packet;
    packet.reserve(4);
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_SET_BYTE_FOR_CHAR)));
    appendUint16LE(packet, idx);
    packet.append(static_cast<char>(value));
    return packet;
}

QByteArray ServerPreview::makeSetCharPacket(uint8_t charIdx, const uint8_t* charData8)
{
    QByteArray packet;
    packet.reserve(10);
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_SET_CHAR)));
    packet.append(static_cast<char>(charIdx));
    packet.append(reinterpret_cast<const char*>(charData8), 8);
    return packet;
}

QByteArray ServerPreview::makeSetCharsChunkPacket(
    uint8_t charIdx,
    const uint8_t* charBuf,
    uint8_t count)
{
    const qsizetype payloadBytes = static_cast<qsizetype>(count) * 8;
    QByteArray packet;
    packet.reserve(3 + payloadBytes);
    packet.append(static_cast<char>(static_cast<uint8_t>(TYPE_SET_CHARS)));
    packet.append(static_cast<char>(charIdx));
    packet.append(static_cast<char>(count));
    packet.append(reinterpret_cast<const char*>(charBuf), payloadBytes);
    return packet;
}

void ServerPreview::protoPoke(quint16 addr, uint8_t value)
{
    enqueuePacket(makePokePacket(addr, value));
}

void ServerPreview::protoFill(quint16 addr, uint8_t value, quint16 count)
{
    enqueuePacket(makeFillPacket(addr, value, count));
}

void ServerPreview::protoSetMem(quint16 addr, const uint8_t* data, quint16 count)
{
    if (data == nullptr || count == 0U) {
        return;
    }
    enqueuePacket(makeSetMemPacket(addr, data, count));
}

void ServerPreview::protoSetByte(quint16 idx, uint8_t value)
{
    enqueuePacket(makeSetBytePacket(idx, value));
}

void ServerPreview::protoSetChar(int charIdx, const uint8_t* charBuf)
{
    if (charBuf == nullptr || charIdx < 0 || charIdx >= 256) {
        return;
    }
    enqueuePacket(makeSetCharPacket(static_cast<uint8_t>(charIdx), charBuf));
}

void ServerPreview::protoSetChars(int charIdx, const uint8_t* charBuf, int totalChars)
{
    if (charBuf == nullptr || charIdx < 0 || charIdx >= 256 || totalChars <= 0) {
        return;
    }

    int remaining = std::min(totalChars, 256 - charIdx);
    int currentIdx = charIdx;
    const uint8_t* currentPtr = charBuf;

    while (remaining > 0) {
        const int chunkChars = std::min(remaining, kMaxCharsPerPacket);
        enqueuePacket(makeSetCharsChunkPacket(
            static_cast<uint8_t>(currentIdx),
            currentPtr,
            static_cast<uint8_t>(chunkChars)));
        currentIdx += chunkChars;
        currentPtr += static_cast<ptrdiff_t>(chunkChars) * 8;
        remaining -= chunkChars;
    }
}

//
// High-level C64 VIC-II / RAM state synchronization
//

void ServerPreview::enqueueBackgroundColorPackets()
{
    if (_activeState == nullptr) {
        return;
    }
    const auto bg = static_cast<uint8_t>(
        static_cast<unsigned int>(_activeState->getGlobalColorForPen(State::PEN_BACKGROUND)) & 0x0FU);
    protoPoke(kVicBorderColorReg, bg);
    protoPoke(kVicBackgroundColorReg, bg);
}

void ServerPreview::enqueueMulticolor1Packet()
{
    if (_activeState == nullptr) {
        return;
    }
    const auto mc1 = static_cast<uint8_t>(
        static_cast<unsigned int>(_activeState->getGlobalColorForPen(State::PEN_MULTICOLOR1)) & 0x0FU);
    protoPoke(kVicMulticolor1Reg, mc1);
}

void ServerPreview::enqueueMulticolor2Packet()
{
    if (_activeState == nullptr) {
        return;
    }
    const auto mc2 = static_cast<uint8_t>(
        static_cast<unsigned int>(_activeState->getGlobalColorForPen(State::PEN_MULTICOLOR2)) & 0x0FU);
    protoPoke(kVicMulticolor2Reg, mc2);
}

void ServerPreview::enqueueForegroundColorPackets()
{
    if (_activeState == nullptr) {
        return;
    }

    if (_activeState->getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL) {
        const auto fg = static_cast<uint8_t>(
            static_cast<unsigned int>(_activeState->getGlobalColorForPen(State::PEN_FOREGROUND)) & 0x0FU);
        protoFill(kC64ColorRamBase, fg, static_cast<quint16>(vchar64::kC64ScreenBytes));
        return;
    }

    const vchar64::C64ScreenBuffer colorRam = vchar64::buildC64PreviewColorRam(*_activeState);
    protoSetMem(
        kC64ColorRamBase,
        colorRam.data(),
        static_cast<quint16>(vchar64::kC64CharsetRegionBytes));
    protoSetMem(
        kC64TileColorSliceAddr,
        colorRam.data() + vchar64::kC64TileRegionOffset,
        static_cast<quint16>(vchar64::kC64TileRegionBytes));
}

void ServerPreview::enqueueColorModePacket()
{
    if (_activeState == nullptr) {
        return;
    }
    const uint8_t d016 = _activeState->isMulticolorMode()
        ? kVicControlMulticolor40Col
        : kVicControlHiRes40Col;
    protoPoke(kVicControlReg2, d016);
}

void ServerPreview::enqueueAllColorPackets()
{
    enqueueBackgroundColorPackets();
    enqueueMulticolor1Packet();
    enqueueMulticolor2Packet();
    enqueueForegroundColorPackets();
    enqueueColorModePacket();
}

void ServerPreview::enqueueCharsetPackets()
{
    if (_activeState == nullptr) {
        return;
    }
    const auto& charset = _activeState->getCharsetBuffer();
    protoSetChars(0, charset.data(), 256);
}

void ServerPreview::enqueueTilesScreenPacket()
{
    if (_activeState == nullptr) {
        return;
    }
    const vchar64::C64ScreenBuffer screen = vchar64::buildC64PreviewScreen(*_activeState);
    protoSetMem(
        kC64TileScreenSliceAddr,
        screen.data() + vchar64::kC64TileRegionOffset,
        static_cast<quint16>(vchar64::kC64TileRegionBytes));
}

//
// Public State notification slots
//

void ServerPreview::fileLoaded()
{
    requestFullSync();
}

void ServerPreview::charsetUpdated()
{
    if (!isConnected() || _activeState == nullptr) {
        return;
    }
    enqueueCharsetPackets();
}

void ServerPreview::bytesUpdated(int pos, int count)
{
    if (!isConnected() || _activeState == nullptr) {
        return;
    }
    if (pos < 0 || count <= 0 || pos >= State::CHAR_BUFFER_SIZE) {
        return;
    }

    const int clampedEnd = std::min(pos + count, State::CHAR_BUFFER_SIZE);
    const int firstChar = pos / 8;
    const int endCharExclusive = (clampedEnd + 7) / 8;
    const int charCount = endCharExclusive - firstChar;
    if (charCount <= 0) {
        return;
    }

    protoSetChars(firstChar, _activeState->getCharAtIndex(firstChar), charCount);
}

void ServerPreview::tileUpdated(int tileIndex)
{
    if (!isConnected() || _activeState == nullptr || !_activeState->isValidTileIndex(tileIndex)) {
        return;
    }

    const State::TileProperties props = _activeState->getTileProperties();
    const int numChars = props.size.width() * props.size.height();
    int charIndex = _activeState->getCharIndexFromTileIndex(tileIndex);

    if (props.interleaved == 1) {
        protoSetChars(charIndex, _activeState->getCharAtIndex(charIndex), numChars);
        return;
    }

    for (int i = 0; i < numChars; ++i) {
        if (charIndex >= 0 && charIndex < 256) {
            protoSetChar(charIndex, _activeState->getCharAtIndex(charIndex));
        }
        charIndex += props.interleaved;
    }
}

void ServerPreview::colorPropertiesUpdated(int pen)
{
    if (!isConnected() || _activeState == nullptr) {
        return;
    }

    switch (pen) {
    case State::PEN_BACKGROUND:
        enqueueBackgroundColorPackets();
        break;
    case State::PEN_MULTICOLOR1:
        enqueueMulticolor1Packet();
        break;
    case State::PEN_MULTICOLOR2:
        enqueueMulticolor2Packet();
        break;
    case State::PEN_FOREGROUND:
        enqueueForegroundColorPackets();
        break;
    default:
        break;
    }
}

void ServerPreview::multicolorModeUpdated(bool toggled)
{
    Q_UNUSED(toggled);
    if (!isConnected() || _activeState == nullptr) {
        return;
    }
    enqueueColorModePacket();
}

void ServerPreview::tilePropertiesUpdated()
{
    if (!isConnected() || _activeState == nullptr) {
        return;
    }
    enqueueTilesScreenPacket();
    enqueueForegroundColorPackets();
}
