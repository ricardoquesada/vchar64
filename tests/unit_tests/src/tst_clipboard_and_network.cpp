/****************************************************************************
Copyright 2026 Ricardo Quesada

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
 * @file tst_clipboard_and_network.cpp
 * @brief Headless `QTest` suite verifying `ClipboardCodec`, `ServerPreview` TCP wire framing, and `AutoUpdater`.
 *
 * Exercises `VC64CLIP` v1 clipboard fuzzing and bounds rejection, loopback TCP
 * `vchar64d` Contiki wire framing (`TYPE_HELLO`, `TYPE_SET_CHARS` `<= 64`-glyph
 * chunking, `TYPE_SET_MEM` `<= 512`-byte chunking, `TYPE_PING` ACK gating and
 * back-pressure flushing, active-document isolation, and socket teardown cleanup),
 * and `AutoUpdater` HTTPS enforcement plus semantic version ordering (`parseSemVer` / `isRemoteNewer`).
 */

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QGuiApplication>
#include <QHostAddress>
#include <QIODevice>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEndian>
#include <QtTest>

#include "autoupdater.h"
#include "core/clipboardcodec.h"
#include "preview/ilivepreviewsink.h"
#include "serverpreview.h"
#include "serverprotocol.h"
#include "state.h"

namespace {

constexpr char kClipMagic[8] = { 'V', 'C', '6', '4', 'C', 'L', 'I', 'P' };

struct ParsedWirePacket {
    uint8_t type = 0;
    uint16_t addrOrIdx = 0;
    uint16_t count = 0;
    uint8_t valueOrToken = 0;
    QByteArray payload;
    QByteArray rawFrame;
    int wireByteSize = 0;
};

[[nodiscard]] uint16_t readUint16LE(const QByteArray& buf, qsizetype offset) noexcept
{
    uint16_t raw = 0;
    std::memcpy(&raw, buf.constData() + offset, sizeof(raw));
    return qFromLittleEndian(raw);
}

/**
 * @brief Incrementally parses complete Contiki `vchar64d` frames from `rxStream`.
 *
 * Leaves any trailing partial frame in `rxStream` so TCP stream fragmentation
 * across `readyRead` chunks is handled deterministically.
 */
[[nodiscard]] bool consumeWirePackets(
    QByteArray& rxStream,
    std::vector<ParsedWirePacket>& outPackets,
    QString& outError)
{
    while (!rxStream.isEmpty()) {
        const auto type = static_cast<uint8_t>(rxStream.at(0));
        ParsedWirePacket pkt;
        pkt.type = type;

        switch (type) {
        case TYPE_HELLO: {
            if (rxStream.size() < 2) {
                return true;
            }
            pkt.valueOrToken = static_cast<uint8_t>(rxStream.at(1));
            pkt.wireByteSize = 2;
            break;
        }
        case TYPE_SET_BYTE_FOR_CHAR: {
            if (rxStream.size() < 4) {
                return true;
            }
            pkt.addrOrIdx = readUint16LE(rxStream, 1);
            pkt.valueOrToken = static_cast<uint8_t>(rxStream.at(3));
            pkt.wireByteSize = 4;
            break;
        }
        case TYPE_SET_CHAR: {
            if (rxStream.size() < 10) {
                return true;
            }
            pkt.addrOrIdx = static_cast<uint8_t>(rxStream.at(1));
            pkt.count = 1;
            pkt.payload = rxStream.mid(2, 8);
            pkt.wireByteSize = 10;
            break;
        }
        case TYPE_SET_CHARS: {
            if (rxStream.size() < 3) {
                return true;
            }
            const auto charIdx = static_cast<uint8_t>(rxStream.at(1));
            const auto charCount = static_cast<uint8_t>(rxStream.at(2));
            const qsizetype payloadBytes = static_cast<qsizetype>(charCount) * 8;
            if (rxStream.size() < 3 + payloadBytes) {
                return true;
            }
            pkt.addrOrIdx = charIdx;
            pkt.count = charCount;
            pkt.payload = rxStream.mid(3, payloadBytes);
            pkt.wireByteSize = static_cast<int>(3 + payloadBytes);
            break;
        }
        case TYPE_SET_MEM: {
            if (rxStream.size() < 5) {
                return true;
            }
            const uint16_t addr = readUint16LE(rxStream, 1);
            const uint16_t count = readUint16LE(rxStream, 3);
            if (rxStream.size() < 5 + static_cast<qsizetype>(count)) {
                return true;
            }
            pkt.addrOrIdx = addr;
            pkt.count = count;
            pkt.payload = rxStream.mid(5, static_cast<qsizetype>(count));
            pkt.wireByteSize = 5 + static_cast<int>(count);
            break;
        }
        case TYPE_POKE: {
            if (rxStream.size() < 4) {
                return true;
            }
            pkt.addrOrIdx = readUint16LE(rxStream, 1);
            pkt.valueOrToken = static_cast<uint8_t>(rxStream.at(3));
            pkt.wireByteSize = 4;
            break;
        }
        case TYPE_FILL: {
            if (rxStream.size() < 6) {
                return true;
            }
            pkt.addrOrIdx = readUint16LE(rxStream, 1);
            pkt.valueOrToken = static_cast<uint8_t>(rxStream.at(3));
            pkt.count = readUint16LE(rxStream, 4);
            pkt.wireByteSize = 6;
            break;
        }
        case TYPE_PING:
        case TYPE_PONG: {
            if (rxStream.size() < 2) {
                return true;
            }
            pkt.valueOrToken = static_cast<uint8_t>(rxStream.at(1));
            pkt.wireByteSize = 2;
            break;
        }
        case TYPE_BYEBYE: {
            pkt.wireByteSize = 1;
            break;
        }
        default: {
            outError = QStringLiteral("Unknown wire packet type: %1").arg(static_cast<int>(type));
            return false;
        }
        }

        pkt.rawFrame = rxStream.left(pkt.wireByteSize);
        rxStream.remove(0, pkt.wireByteSize);
        outPackets.push_back(std::move(pkt));
    }

    return true;
}

/**
 * @brief Loopback mock of Contiki `server/vchar64d.c` tracking 1,400-byte uIP windows.
 */
class MockContikiPreviewServer final : public QObject {
    Q_OBJECT

public:
    explicit MockContikiPreviewServer(QTcpSocket* socket, bool autoPong = true, QObject* parent = nullptr)
        : QObject(parent)
        , _socket(socket)
        , _autoPong(autoPong)
    {
        QObject::connect(_socket, &QIODevice::readyRead, this, &MockContikiPreviewServer::onReadyRead);
        if (_socket->bytesAvailable() > 0) {
            onReadyRead();
        }
    }

    void setAutoPong(bool enabled)
    {
        _autoPong = enabled;
        if (_autoPong) {
            flushPendingPongs();
        }
    }

    void flushPendingPongs()
    {
        while (!_pendingPongTokens.empty()) {
            const uint8_t token = _pendingPongTokens.front();
            _pendingPongTokens.erase(_pendingPongTokens.begin());
            sendPong(token);
        }
    }

    void sendPong(uint8_t token)
    {
        QByteArray pong(2, '\0');
        pong[0] = static_cast<char>(static_cast<uint8_t>(TYPE_PONG));
        pong[1] = static_cast<char>(token);
        _currentWindowBytes = 0;
        _socket->write(pong);
        _socket->flush();
    }

    void clearRecordedPackets()
    {
        _packets.clear();
    }

    [[nodiscard]] bool hasPacket(uint8_t type, uint16_t addrOrIdx) const noexcept
    {
        return std::any_of(_packets.begin(), _packets.end(), [type, addrOrIdx](const ParsedWirePacket& p) {
            return p.type == type && p.addrOrIdx == addrOrIdx;
        });
    }

    [[nodiscard]] const std::vector<ParsedWirePacket>& packets() const noexcept { return _packets; }
    [[nodiscard]] int maxObservedWindowBytes() const noexcept { return _maxObservedWindowBytes; }
    [[nodiscard]] const QString& parseError() const noexcept { return _parseError; }
    [[nodiscard]] const std::vector<uint8_t>& pendingPongTokens() const noexcept { return _pendingPongTokens; }

private slots:
    void onReadyRead()
    {
        _rxStream.append(_socket->readAll());
        std::vector<ParsedWirePacket> newlyParsed;
        if (!consumeWirePackets(_rxStream, newlyParsed, _parseError)) {
            return;
        }

        for (ParsedWirePacket& pkt : newlyParsed) {
            _currentWindowBytes += pkt.wireByteSize;
            _maxObservedWindowBytes = std::max(_maxObservedWindowBytes, _currentWindowBytes);

            if (pkt.type == static_cast<uint8_t>(TYPE_PING)) {
                if (_autoPong) {
                    sendPong(pkt.valueOrToken);
                } else {
                    _pendingPongTokens.push_back(pkt.valueOrToken);
                }
            }
            _packets.push_back(std::move(pkt));
        }
    }

private:
    QTcpSocket* _socket = nullptr;
    bool _autoPong = true;
    QByteArray _rxStream;
    std::vector<ParsedWirePacket> _packets;
    std::vector<uint8_t> _pendingPongTokens;
    int _currentWindowBytes = 0;
    int _maxObservedWindowBytes = 0;
    QString _parseError;
};

[[nodiscard]] QByteArray buildRawClipEnvelope(
    const char magic8[8],
    uint32_t version,
    int32_t bufferType,
    int32_t offset,
    int32_t blockSize,
    int32_t skip,
    int32_t count,
    int32_t sourceRowWidth,
    int32_t tileWidth,
    int32_t tileHeight,
    int32_t interleaved,
    const QByteArray& body)
{
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.writeRawData(magic8, 8);
    stream << version
           << bufferType
           << offset
           << blockSize
           << skip
           << count
           << sourceRowWidth
           << tileWidth
           << tileHeight
           << interleaved;
    if (!body.isEmpty()) {
        stream.writeRawData(body.constData(), static_cast<int>(body.size()));
    }
    return out;
}

} // namespace

class TestClipboardAndNetwork : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testClipboardCodecRoundTripsCharsTilesAndMap();
    void testClipboardCodecAdversarialFuzzRejection();
    void testC64PreviewBuildersAll64TileSizesAnd7x4_7x7Regressions();
    void testServerPreviewAsyncConnectAnd1400BytePingPongBarrier();
    void testServerPreviewProtoFillEndianPens012AndTilePropsRefresh();
    void testServerPreview256KiBStalledModemBackpressureCoalescing();
    void testAutoUpdaterOfflineHttpsSchemeEnforcement();
    void testAutoUpdaterOffline64KiBStreamingCap();
    void testAutoUpdaterHtmlSanitizationStrictHttpsUrlAndSignals();

private:
    QTemporaryDir _tempSettingsDir;
};

void TestClipboardAndNetwork::initTestCase()
{
    // Verify QTEST_GUILESS_MAIN instantiates QCoreApplication (never QGuiApplication / QApplication).
    QVERIFY(QCoreApplication::instance() != nullptr);
    QVERIFY(qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr);

    QVERIFY(_tempSettingsDir.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, _tempSettingsDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _tempSettingsDir.path());

    QCoreApplication::setOrganizationName(QStringLiteral("RetroMoeTestM3"));
    QCoreApplication::setApplicationName(QStringLiteral("VChar64HeadlessSuite4"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    qRegisterMetaType<UpdateInfo>();
}

void TestClipboardAndNetwork::cleanupTestCase()
{
    ServerPreview::deleteInstance();

    // Enforce zero QSettings side effects across all vchar64_net operations.
    const QDir settingsDir(_tempSettingsDir.path());
    QVERIFY2(
        settingsDir.isEmpty(),
        qPrintable(QStringLiteral("Expected zero QSettings writes in %1, found: %2")
                .arg(_tempSettingsDir.path(), settingsDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).join(QLatin1Char(',')))));
}

void TestClipboardAndNetwork::testClipboardCodecRoundTripsCharsTilesAndMap()
{
    QCOMPARE(
        QString::fromUtf8(ClipboardCodec::kMimeType),
        QStringLiteral("application/x-vchar64-clip-v2"));
    QCOMPARE(ClipboardCodec::kFormatVersion, 2U);

    // (A) CHARS round-trip (offset=5, blockSize=8, skip=8, count=4)
    {
        ClipboardPayload charsPayload;
        charsPayload.type = State::CopyRange::CHARS;
        charsPayload.offset = 5;
        charsPayload.blockSize = 8;
        charsPayload.skip = 8;
        charsPayload.count = 4;
        charsPayload.sourceRowWidth = 32;
        charsPayload.tileProperties = State::TileProperties { QSize(1, 1), 1 };
        for (size_t i = 0; i < charsPayload.charset.size(); ++i) {
            charsPayload.charset[i] = static_cast<uint8_t>((i * 29U + 7U) & 0xFFU);
        }
        for (size_t i = 0; i < charsPayload.tileColors.size(); ++i) {
            charsPayload.tileColors[i] = static_cast<uint8_t>((i + 3U) & 0x0FU);
        }

        const QByteArray raw = ClipboardCodec::serialize(charsPayload);
        const auto decodedRes = ClipboardCodec::deserialize(raw);
        QVERIFY2(decodedRes.isOk(), qPrintable(decodedRes.errorMessage()));

        const ClipboardPayload& decoded = decodedRes.value();
        QCOMPARE(decoded.type, State::CopyRange::CHARS);
        QCOMPARE(decoded.offset, 5);
        QCOMPARE(decoded.blockSize, 8);
        QCOMPARE(decoded.skip, 8);
        QCOMPARE(decoded.count, 4);
        QCOMPARE(decoded.sourceRowWidth, 32);
        QCOMPARE(decoded.tileProperties, charsPayload.tileProperties);
        QCOMPARE(decoded.charset, charsPayload.charset);
        QCOMPARE(decoded.tileColors, charsPayload.tileColors);
        QVERIFY(decoded.mapCells.empty());
    }

    // (B) TILES round-trip (tw=3, th=2, interleaved=42, non-black tileColors)
    {
        ClipboardPayload tilesPayload;
        tilesPayload.type = State::CopyRange::TILES;
        tilesPayload.offset = 2;
        tilesPayload.blockSize = 3;
        tilesPayload.skip = 5;
        tilesPayload.count = 4;
        tilesPayload.sourceRowWidth = 8;
        tilesPayload.tileProperties = State::TileProperties { QSize(3, 2), 42 };
        for (size_t i = 0; i < tilesPayload.charset.size(); ++i) {
            tilesPayload.charset[i] = static_cast<uint8_t>((255U - (i & 0xFFU)) ^ 0x5AU);
        }
        for (size_t i = 0; i < tilesPayload.tileColors.size(); ++i) {
            tilesPayload.tileColors[i] = static_cast<uint8_t>(1U + (i % 15U));
        }

        const QByteArray raw = ClipboardCodec::serialize(tilesPayload);
        const auto decodedRes = ClipboardCodec::deserialize(raw);
        QVERIFY2(decodedRes.isOk(), qPrintable(decodedRes.errorMessage()));

        const ClipboardPayload& decoded = decodedRes.value();
        QCOMPARE(decoded.type, State::CopyRange::TILES);
        QCOMPARE(decoded.offset, 2);
        QCOMPARE(decoded.blockSize, 3);
        QCOMPARE(decoded.skip, 5);
        QCOMPARE(decoded.count, 4);
        QCOMPARE(decoded.sourceRowWidth, 8);
        QCOMPARE(decoded.tileProperties.size, QSize(3, 2));
        QCOMPARE(decoded.tileProperties.interleaved, 42);
        QCOMPARE(decoded.charset, tilesPayload.charset);
        QCOMPARE(decoded.tileColors, tilesPayload.tileColors);
        QVERIFY(decoded.mapCells.empty());
    }

    // (C) MAP round-trip (sourceRowWidth=12, blockSize=12, skip=28, count=5, 60 map bytes)
    {
        ClipboardPayload mapPayload;
        mapPayload.type = State::CopyRange::MAP;
        mapPayload.offset = 0;
        mapPayload.blockSize = 12;
        mapPayload.skip = 28;
        mapPayload.count = 5;
        mapPayload.sourceRowWidth = 12;
        mapPayload.tileProperties = State::TileProperties { QSize(2, 2), 1 };
        mapPayload.mapCells.resize(200U);
        for (size_t i = 0; i < mapPayload.mapCells.size(); ++i) {
            mapPayload.mapCells[i] = static_cast<uint8_t>((i * 41U + 13U) & 0xFFU);
        }
        // Also test compact 60-byte contiguous selection (skip=0, offset=0, blockSize=12, count=5)
        ClipboardPayload compactMapPayload = mapPayload;
        compactMapPayload.skip = 0;
        compactMapPayload.mapCells.resize(60U);
        for (size_t i = 0; i < compactMapPayload.mapCells.size(); ++i) {
            compactMapPayload.mapCells[i] = static_cast<uint8_t>((i * 17U + 9U) & 0xFFU);
        }

        for (const ClipboardPayload& candidate : { compactMapPayload, mapPayload }) {
            const QByteArray raw = ClipboardCodec::serialize(candidate);
            const auto decodedRes = ClipboardCodec::deserialize(raw);
            QVERIFY2(decodedRes.isOk(), qPrintable(decodedRes.errorMessage()));

            const ClipboardPayload& decoded = decodedRes.value();
            QCOMPARE(decoded.type, State::CopyRange::MAP);
            QCOMPARE(decoded.offset, candidate.offset);
            QCOMPARE(decoded.blockSize, candidate.blockSize);
            QCOMPARE(decoded.skip, candidate.skip);
            QCOMPARE(decoded.count, candidate.count);
            QCOMPARE(decoded.sourceRowWidth, 12);
            QCOMPARE(decoded.tileProperties, candidate.tileProperties);
            QCOMPARE(decoded.mapCells, candidate.mapCells);
        }
    }
}

void TestClipboardAndNetwork::testClipboardCodecAdversarialFuzzRejection()
{
    const QByteArray dummyCharsBody(
        static_cast<qsizetype>(State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE),
        '\x11');

    // (1) Empty buffer
    QVERIFY(!ClipboardCodec::deserialize(QByteArray { }).isOk());

    // (2) Raw sizeof(State::CopyRange) C-struct bytes
    {
        State::CopyRange legacyStruct { };
        legacyStruct.offset = 0;
        legacyStruct.blockSize = 8;
        legacyStruct.count = 1;
        legacyStruct.type = State::CopyRange::CHARS;
        legacyStruct.tileProperties = State::TileProperties { QSize(1, 1), 1 };
        QByteArray rawStruct(reinterpret_cast<const char*>(&legacyStruct), sizeof(legacyStruct));
        QVERIFY(!ClipboardCodec::deserialize(rawStruct).isOk());
    }

    // (3) Bad magic "VC64NOPE"
    {
        constexpr char badMagic[8] = { 'V', 'C', '6', '4', 'N', 'O', 'P', 'E' };
        const QByteArray bad = buildRawClipEnvelope(
            badMagic, 2U, 0, 0, 1, 0, 1, 32, 1, 1, 1, dummyCharsBody);
        QVERIFY(!ClipboardCodec::deserialize(bad).isOk());
    }

    // (4) Unsupported versions 1 and 99
    for (uint32_t badVer : { 1U, 99U }) {
        const QByteArray bad = buildRawClipEnvelope(
            kClipMagic, badVer, 0, 0, 1, 0, 1, 32, 1, 1, 1, dummyCharsBody);
        QVERIFY(!ClipboardCodec::deserialize(bad).isOk());
    }

    // (5) Out-of-range BufferType = 77 and -1
    for (int32_t badType : { 77, -1 }) {
        const QByteArray bad = buildRawClipEnvelope(
            kClipMagic, 2U, badType, 0, 1, 0, 1, 32, 1, 1, 1, dummyCharsBody);
        QVERIFY(!ClipboardCodec::deserialize(bad).isOk());
    }

    // (6) Negative offset = -1, blockSize = -4, skip = -1, count = INT_MIN
    {
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 0, -1, 1, 0, 1, 32, 1, 1, 1, dummyCharsBody))
                .isOk());
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 0, 0, -4, 0, 1, 32, 1, 1, 1, dummyCharsBody))
                .isOk());
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 0, 0, 1, -1, 1, 32, 1, 1, 1, dummyCharsBody))
                .isOk());
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 0, 0, 1, 0, INT_MIN, 32, 1, 1, 1, dummyCharsBody))
                .isOk());
    }

    // (7) Invalid tile dimensions: (tw=0, th=2), (tw=9, th=9), and bad interleaved
    {
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 1, 0, 1, 0, 1, 8, 0, 2, 1, dummyCharsBody))
                .isOk());
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 1, 0, 1, 0, 1, 8, 9, 9, 1, dummyCharsBody))
                .isOk());
        QVERIFY(!ClipboardCodec::deserialize(
            buildRawClipEnvelope(kClipMagic, 2U, 1, 0, 1, 0, 1, 8, 2, 2, 0, dummyCharsBody))
                .isOk());
    }

    // (8) Integer-overflowing MAP dimensions: blockSize = 65536, count = 65536 (4 GiB > kMaxMapCells)
    {
        QByteArray mapBody;
        QDataStream bodyStream(&mapBody, QIODevice::WriteOnly);
        bodyStream.setByteOrder(QDataStream::LittleEndian);
        bodyStream.setVersion(QDataStream::Qt_6_0);
        bodyStream << static_cast<uint32_t>(64U);
        mapBody.append(QByteArray(64, '\x00'));

        const QByteArray hugeSpan = buildRawClipEnvelope(
            kClipMagic, 2U, 2, 0, 65536, 0, 65536, 65536, 1, 1, 1, mapBody);
        QVERIFY(!ClipboardCodec::deserialize(hugeSpan).isOk());

        // Also test declaredMapSize > 16 MiB cap before allocation
        QByteArray hugeDeclaredBody;
        QDataStream hugeBodyStream(&hugeDeclaredBody, QIODevice::WriteOnly);
        hugeBodyStream.setByteOrder(QDataStream::LittleEndian);
        hugeBodyStream.setVersion(QDataStream::Qt_6_0);
        hugeBodyStream << static_cast<uint32_t>(20U * 1024U * 1024U);
        hugeDeclaredBody.append(QByteArray(64, '\x00'));

        const QByteArray hugeDeclared = buildRawClipEnvelope(
            kClipMagic, 2U, 2, 0, 1, 0, 1, 1, 1, 1, 1, hugeDeclaredBody);
        QVERIFY(!ClipboardCodec::deserialize(hugeDeclared).isOk());
    }

    // (9) Truncated payload (declared = 2048, actual = 64 bytes)
    {
        QByteArray truncatedMapBody;
        QDataStream ts(&truncatedMapBody, QIODevice::WriteOnly);
        ts.setByteOrder(QDataStream::LittleEndian);
        ts.setVersion(QDataStream::Qt_6_0);
        ts << static_cast<uint32_t>(2048U);
        truncatedMapBody.append(QByteArray(64, '\x2A'));

        const QByteArray truncatedMap = buildRawClipEnvelope(
            kClipMagic, 2U, 2, 0, 32, 0, 64, 32, 1, 1, 1, truncatedMapBody);
        QVERIFY(!ClipboardCodec::deserialize(truncatedMap).isOk());

        const QByteArray truncatedChars = buildRawClipEnvelope(
            kClipMagic, 2U, 0, 0, 8, 0, 1, 32, 1, 1, 1, QByteArray(64, '\x2A'));
        QVERIFY(!ClipboardCodec::deserialize(truncatedChars).isOk());
    }

    // (10) Deterministic PRNG fuzz buffers (0..512 bytes, 500 iterations)
    {
        std::mt19937 rng(0x56433634U);
        std::uniform_int_distribution<int> lenDist(0, 512);
        std::uniform_int_distribution<int> byteDist(0, 255);

        for (int iter = 0; iter < 500; ++iter) {
            const int len = lenDist(rng);
            QByteArray fuzz(len, '\0');
            for (int b = 0; b < len; ++b) {
                fuzz[b] = static_cast<char>(static_cast<uint8_t>(byteDist(rng)));
            }
            const auto res = ClipboardCodec::deserialize(fuzz);
            QVERIFY(!res.isOk());
        }
    }
}

void TestClipboardAndNetwork::testC64PreviewBuildersAll64TileSizesAnd7x4_7x7Regressions()
{
    State state;

    // Sweep all 64 tile sizes (1x1..8x8) x interleaved in {1, maxTiles} x {GLOBAL, PER_TILE}
    for (int tw = 1; tw <= 8; ++tw) {
        for (int th = 1; th <= 8; ++th) {
            const int maxTiles = 256 / (tw * th);
            const std::array<int, 2> interleaves { 1, maxTiles };

            for (const int interleaved : interleaves) {
                const State::TileProperties props { QSize(tw, th), interleaved };
                QVERIFY(state.setTilePropertiesDirect(props));

                // (1) FOREGROUND_COLOR_GLOBAL (pen = 3)
                state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
                state.setPenColorDirect(State::PEN_FOREGROUND, 3U);

                const auto screen = buildC64PreviewScreen(state);
                const auto globalColors = buildC64PreviewColorRam(state);
                QCOMPARE(screen.size(), 1000U);
                QCOMPARE(globalColors.size(), 1000U);

                // Rows 0..7: cols 0..31 == 0..255, cols 32..39 == 0x20
                for (int r = 0; r < 8; ++r) {
                    for (int c = 0; c < 32; ++c) {
                        const auto idx = static_cast<size_t>(r * 40 + c);
                        QCOMPARE(screen[idx], static_cast<uint8_t>(r * 32 + c));
                    }
                    for (int c = 32; c < 40; ++c) {
                        const auto idx = static_cast<size_t>(r * 40 + c);
                        QCOMPARE(screen[idx], static_cast<uint8_t>(0x20U));
                    }
                }

                // Rows 8..11 ([320..480)): all 0x20
                for (size_t idx = 320U; idx < 480U; ++idx) {
                    QCOMPARE(screen[idx], static_cast<uint8_t>(0x20U));
                }

                for (size_t idx = 0; idx < 1000U; ++idx) {
                    QCOMPARE(globalColors[idx], static_cast<uint8_t>(3U));
                }

                // (2) FOREGROUND_COLOR_PER_TILE with high-nibble bits set before State masking
                state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);
                for (int t = 0; t < State::TILE_COLORS_BUFFER_SIZE; ++t) {
                    state.setTileColorDirect(t, static_cast<uint8_t>((static_cast<unsigned int>(t) % 16U) | 0xF0U));
                }

                const auto perTileColors = buildC64PreviewColorRam(state);
                for (size_t idx = 0; idx < 1000U; ++idx) {
                    QVERIFY(perTileColors[idx] <= 0x0FU);
                }
            }
        }
    }

    // Explicit regression check #1: 7x4 tiles (9 tiles = 4 + 4 + 1 across rows 12..23)
    {
        const State::TileProperties props7x4 { QSize(7, 4), 1 };
        const auto screen7x4 = buildC64PreviewScreen(props7x4);

        // Tile #0 starts at row 12, col 0 -> char 0; Tile #3 at row 12, col 21 -> char 84
        QCOMPARE(screen7x4[12U * 40U + 0U], static_cast<uint8_t>(0U));
        QCOMPARE(screen7x4[12U * 40U + 21U], static_cast<uint8_t>(3U * 28U));
        // Cols 28..39 in rows 12..19 remain 0x20 because 28 + 7 = 35 > 32 wraps to col 0!
        for (int r = 12; r < 20; ++r) {
            for (int c = 28; c < 40; ++c) {
                QCOMPARE(screen7x4[static_cast<size_t>(r * 40 + c)], static_cast<uint8_t>(0x20U));
            }
        }
        // Tile #8 (9th tile) lands at row 20..23, cols 0..6 (chars 224..251)
        QCOMPARE(screen7x4[20U * 40U + 0U], static_cast<uint8_t>(8U * 28U));
        QCOMPARE(screen7x4[23U * 40U + 6U], static_cast<uint8_t>(8U * 28U + 27U));
        for (int r = 20; r <= 23; ++r) {
            for (int c = 7; c < 40; ++c) {
                QCOMPARE(screen7x4[static_cast<size_t>(r * 40 + c)], static_cast<uint8_t>(0x20U));
            }
        }
        // Row 24 ([960..1000)) untouched (0x20)
        for (size_t idx = 960U; idx < 1000U; ++idx) {
            QCOMPARE(screen7x4[idx], static_cast<uint8_t>(0x20U));
        }
    }

    // Explicit regression check #2: 7x7 tiles (5 tiles exist, 4 fit in rows 12..18;
    // tile #4 at row 19 has 19 + 7 = 26 > 25 and is rejected whole without partial writes!)
    {
        QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(7, 7), 1 }));
        state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);
        for (int t = 0; t < 5; ++t) {
            state.setTileColorDirect(t, static_cast<uint8_t>(t + 1));
        }

        const auto screen7x7 = buildC64PreviewScreen(state);
        const auto color7x7 = buildC64PreviewColorRam(state);

        // Tiles 0..3 occupy rows 12..18, cols 0..27
        QCOMPARE(screen7x7[12U * 40U + 0U], static_cast<uint8_t>(0U));
        QCOMPARE(screen7x7[18U * 40U + 27U], static_cast<uint8_t>(3U * 49U + 48U));
        QCOMPARE(color7x7[12U * 40U + 0U], static_cast<uint8_t>(1U));
        QCOMPARE(color7x7[18U * 40U + 27U], static_cast<uint8_t>(4U));

        // Rows 19..24 ([760..1000)) must have ZERO partial tile glyphs (all 0x20, default color 0x08)!
        for (size_t idx = 760U; idx < 1000U; ++idx) {
            QCOMPARE(screen7x7[idx], static_cast<uint8_t>(0x20U));
            QCOMPARE(color7x7[idx], static_cast<uint8_t>(0x08U));
        }
    }
}

void TestClipboardAndNetwork::testServerPreviewAsyncConnectAnd1400BytePingPongBarrier()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    State state;
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(2, 2), 1 }));
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);
    state.setPenColorDirect(State::PEN_BACKGROUND, 6U);
    state.setPenColorDirect(State::PEN_MULTICOLOR1, 9U);
    state.setPenColorDirect(State::PEN_MULTICOLOR2, 10U);

    State::CharsetBuffer customCharset { };
    for (size_t i = 0; i < customCharset.size(); ++i) {
        customCharset[i] = static_cast<uint8_t>((i * 19U + 5U) & 0xFFU);
    }
    state.setCharsetBytes(customCharset.data(), customCharset.size());
    for (int t = 0; t < 64; ++t) {
        state.setTileColorDirect(t, static_cast<uint8_t>((t + 2) & 0x0F));
    }

    ServerPreview preview;
    ILivePreviewSink* sink = &preview;
    sink->setActiveState(&state);
    QCOMPARE(sink->activeState(), &state);

    QSignalSpy connectedSpy(sink, &ILivePreviewSink::previewConnected);
    QSignalSpy errorSpy(sink, &ILivePreviewSink::previewError);

    sink->connectPreview(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY(server.hasPendingConnections());
    QTcpSocket* serverSocket = server.nextPendingConnection();
    QVERIFY(serverSocket != nullptr);
    QTRY_COMPARE(connectedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(sink->isConnected());

    // First window MUST pause after 128-char TYPE_SET_CHARS (1,027 B) + 2B TYPE_PING = 1,029 bytes!
    QTRY_COMPARE(serverSocket->bytesAvailable(), 1029);
    QVERIFY(preview.isAwaitingPong());

    MockContikiPreviewServer contiki(serverSocket, /*autoPong=*/false);
    QVERIFY2(contiki.parseError().isEmpty(), qPrintable(contiki.parseError()));
    QCOMPARE(contiki.packets().size(), 2U);
    QCOMPARE(contiki.maxObservedWindowBytes(), 1029);

    const ParsedWirePacket& chunk0 = contiki.packets()[0];
    QCOMPARE(chunk0.type, static_cast<uint8_t>(TYPE_SET_CHARS));
    QCOMPARE(chunk0.addrOrIdx, static_cast<uint16_t>(0U));
    QCOMPARE(chunk0.count, static_cast<uint16_t>(128U));
    QCOMPARE(chunk0.payload.size(), 1024);
    QCOMPARE(
        std::memcmp(chunk0.payload.constData(), customCharset.data(), 1024U),
        0);

    const ParsedWirePacket& ping0 = contiki.packets()[1];
    QCOMPARE(ping0.type, static_cast<uint8_t>(TYPE_PING));
    QCOMPARE(contiki.pendingPongTokens().size(), 1U);

    // Enable auto-PONG and verify the rest of the initial sync drains within <= 1,400B windows!
    contiki.setAutoPong(true);
    QTRY_VERIFY(
        !preview.isAwaitingPong()
        && preview.queuedBytes() == 0U
        && contiki.hasPacket(static_cast<uint8_t>(TYPE_SET_MEM), 0xA5E0U));
    QVERIFY2(contiki.parseError().isEmpty(), qPrintable(contiki.parseError()));
    QVERIFY2(
        contiki.maxObservedWindowBytes() <= VCHAR64_SERVER_BUFFER_SIZE,
        qPrintable(QStringLiteral("Window exceeded 1,400B: %1").arg(contiki.maxObservedWindowBytes())));

    bool sawSecondCharsChunk = false;
    bool sawScreenSliceA5E0 = false;
    bool sawColorSliceD9E0 = false;

    const auto expectedScreen = buildC64PreviewScreen(state);
    const auto expectedColorRam = buildC64PreviewColorRam(state);

    for (const ParsedWirePacket& pkt : contiki.packets()) {
        if (pkt.type == static_cast<uint8_t>(TYPE_SET_CHARS)
            && pkt.addrOrIdx == 128U
            && pkt.count == 128U) {
            sawSecondCharsChunk = true;
            QCOMPARE(pkt.payload.size(), 1024);
            QCOMPARE(
                std::memcmp(pkt.payload.constData(), customCharset.data() + 1024U, 1024U),
                0);
        } else if (pkt.type == static_cast<uint8_t>(TYPE_SET_MEM)
            && pkt.addrOrIdx == 0xA5E0U
            && pkt.count == 520U) {
            sawScreenSliceA5E0 = true;
            QCOMPARE(
                std::memcmp(pkt.payload.constData(), expectedScreen.data() + 480U, 520U),
                0);
        } else if (pkt.type == static_cast<uint8_t>(TYPE_SET_MEM)
            && pkt.addrOrIdx == 0xD9E0U
            && pkt.count == 520U) {
            sawColorSliceD9E0 = true;
            QCOMPARE(
                std::memcmp(pkt.payload.constData(), expectedColorRam.data() + 480U, 520U),
                0);
        }
    }

    QVERIFY(sawSecondCharsChunk);
    QVERIFY(sawScreenSliceA5E0);
    QVERIFY(sawColorSliceD9E0);

    // Also verify active-document switching via setActiveState(&stateB) detaches old state cleanly.
    // Note: because 1,054 unacknowledged bytes from Window #3 are still in flight, ServerPreview
    // rightly emits a 2-byte TYPE_PING barrier before stateB's 1,027-byte TYPE_SET_CHARS chunk 0!
    contiki.clearRecordedPackets();
    State stateB;
    const uint8_t sentinelB = 0xA5U;
    stateB.setCharsetBytes(&sentinelB, 1U);
    sink->setActiveState(&stateB);

    QTRY_VERIFY(
        !preview.isAwaitingPong()
        && preview.queuedBytes() == 0U
        && contiki.hasPacket(static_cast<uint8_t>(TYPE_SET_MEM), 0xA5E0U));
    QVERIFY(contiki.maxObservedWindowBytes() <= VCHAR64_SERVER_BUFFER_SIZE);

    bool sawStateBChars = false;
    for (const ParsedWirePacket& pkt : contiki.packets()) {
        if (pkt.type == static_cast<uint8_t>(TYPE_SET_CHARS)
            && pkt.addrOrIdx == 0U
            && pkt.count == 128U) {
            sawStateBChars = true;
            QCOMPARE(static_cast<uint8_t>(pkt.payload.at(0)), sentinelB);
        }
    }
    QVERIFY(sawStateBChars);

    contiki.clearRecordedPackets();
    state.setColorForPen(State::PEN_BACKGROUND, 3);
    QTest::qWait(20);
    QVERIFY(contiki.packets().empty());

    QSignalSpy disconnectedSpy(sink, &ILivePreviewSink::previewDisconnected);
    sink->disconnectPreview();
    QTRY_COMPARE(disconnectedSpy.count(), 1);
}

void TestClipboardAndNetwork::testServerPreviewProtoFillEndianPens012AndTilePropsRefresh()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    State state;
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
    // Initialize starting pen colors distinct from {2, 5, 7, 14} so setColorForPen mutates all 4 pens.
    state.setPenColorDirect(State::PEN_BACKGROUND, 0U);
    state.setPenColorDirect(State::PEN_MULTICOLOR1, 0U);
    state.setPenColorDirect(State::PEN_MULTICOLOR2, 0U);
    state.setPenColorDirect(State::PEN_FOREGROUND, 1U);

    ServerPreview preview;
    preview.setActiveState(&state);
    preview.connectPreview(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY(server.hasPendingConnections());
    QTcpSocket* serverSocket = server.nextPendingConnection();
    QVERIFY(serverSocket != nullptr);

    MockContikiPreviewServer contiki(serverSocket, /*autoPong=*/true);
    QTRY_VERIFY(
        preview.isConnected()
        && !preview.isAwaitingPong()
        && preview.queuedBytes() == 0U
        && contiki.hasPacket(static_cast<uint8_t>(TYPE_SET_MEM), 0xA5E0U));
    contiki.clearRecordedPackets();

    // (1) PEN_BACKGROUND (2), PEN_MULTICOLOR1 (5), PEN_MULTICOLOR2 (7), PEN_FOREGROUND (14)
    state.setColorForPen(State::PEN_BACKGROUND, 2);
    state.setColorForPen(State::PEN_MULTICOLOR1, 5);
    state.setColorForPen(State::PEN_MULTICOLOR2, 7);
    state.setColorForPen(State::PEN_FOREGROUND, 14);

    // (2) Toggle Multicolor ON -> pokes 0xD016 with 0x18
    state.setMulticolorMode(true);

    // (3) Change TileProperties from 1x1 to 7x4 -> 520-byte TYPE_SET_MEM to 0xA5E0
    state.setTileProperties(State::TileProperties { QSize(7, 4), 1 });

    QTRY_VERIFY(
        !preview.isAwaitingPong()
        && preview.queuedBytes() == 0U
        && contiki.hasPacket(static_cast<uint8_t>(TYPE_SET_MEM), 0xA5E0U));
    QVERIFY2(contiki.parseError().isEmpty(), qPrintable(contiki.parseError()));
    QVERIFY(contiki.maxObservedWindowBytes() <= VCHAR64_SERVER_BUFFER_SIZE);

    bool sawD020Bg = false;
    bool sawD021Bg = false;
    bool sawD022Mc1 = false;
    bool sawD023Mc2 = false;
    bool sawD800Fill1000LE = false;
    bool sawD016Multicolor = false;
    bool sawA5E0TileProps7x4 = false;

    const QByteArray expectedPokeD020("\x05\x20\xD0\x02", 4);
    const QByteArray expectedPokeD021("\x05\x21\xD0\x02", 4);
    const QByteArray expectedFillD800("\x06\x00\xD8\x0E\xE8\x03", 6);
    const auto expected7x4Screen = buildC64PreviewScreen(state);

    for (const ParsedWirePacket& pkt : contiki.packets()) {
        if (pkt.type == static_cast<uint8_t>(TYPE_POKE)) {
            if (pkt.addrOrIdx == 0xD020U && pkt.valueOrToken == 2U) {
                QCOMPARE(pkt.rawFrame, expectedPokeD020);
                sawD020Bg = true;
            } else if (pkt.addrOrIdx == 0xD021U && pkt.valueOrToken == 2U) {
                QCOMPARE(pkt.rawFrame, expectedPokeD021);
                sawD021Bg = true;
            } else if (pkt.addrOrIdx == 0xD022U && pkt.valueOrToken == 5U) {
                sawD022Mc1 = true;
            } else if (pkt.addrOrIdx == 0xD023U && pkt.valueOrToken == 7U) {
                sawD023Mc2 = true;
            } else if (pkt.addrOrIdx == 0xD016U && pkt.valueOrToken == 0x18U) {
                sawD016Multicolor = true;
            }
        } else if (pkt.type == static_cast<uint8_t>(TYPE_FILL)) {
            if (pkt.addrOrIdx == 0xD800U && pkt.valueOrToken == 14U && pkt.count == 1000U) {
                QCOMPARE(pkt.rawFrame, expectedFillD800);
                sawD800Fill1000LE = true;
            }
        } else if (pkt.type == static_cast<uint8_t>(TYPE_SET_MEM)) {
            if (pkt.addrOrIdx == 0xA5E0U && pkt.count == 520U) {
                QCOMPARE(static_cast<uint8_t>(pkt.rawFrame.at(1)), static_cast<uint8_t>(0xE0U));
                QCOMPARE(static_cast<uint8_t>(pkt.rawFrame.at(2)), static_cast<uint8_t>(0xA5U));
                QCOMPARE(static_cast<uint8_t>(pkt.rawFrame.at(3)), static_cast<uint8_t>(0x08U));
                QCOMPARE(static_cast<uint8_t>(pkt.rawFrame.at(4)), static_cast<uint8_t>(0x02U));
                sawA5E0TileProps7x4 = true;
                QCOMPARE(
                    std::memcmp(pkt.payload.constData(), expected7x4Screen.data() + 480U, 520U),
                    0);
            }
        }
    }

    QVERIFY(sawD020Bg);
    QVERIFY(sawD021Bg);
    QVERIFY(sawD022Mc1);
    QVERIFY(sawD023Mc2);
    QVERIFY(sawD800Fill1000LE);
    QVERIFY(sawD016Multicolor);
    QVERIFY(sawA5E0TileProps7x4);

    preview.disconnectPreview();
}

void TestClipboardAndNetwork::testServerPreview256KiBStalledModemBackpressureCoalescing()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    State state;
    ServerPreview preview;
    preview.setActiveState(&state);
    preview.connectPreview(QStringLiteral("127.0.0.1"), server.serverPort());

    QTRY_VERIFY(server.hasPendingConnections());
    QTcpSocket* serverSocket = server.nextPendingConnection();
    QVERIFY(serverSocket != nullptr);

    MockContikiPreviewServer contiki(serverSocket, /*autoPong=*/true);
    QTRY_VERIFY(
        preview.isConnected()
        && !preview.isAwaitingPong()
        && preview.queuedBytes() == 0U
        && contiki.hasPacket(static_cast<uint8_t>(TYPE_SET_MEM), 0xA5E0U));

    // Simulate stalled C64 Wi-Fi modem: withhold TYPE_PONG while firing 600 charsetUpdated() mutations (> 1.2 MiB)
    contiki.clearRecordedPackets();
    contiki.setAutoPong(false);

    for (int i = 1; i <= 600; ++i) {
        const uint8_t marker = (i == 600)
            ? static_cast<uint8_t>(0x99U)
            : static_cast<uint8_t>(static_cast<unsigned int>(i) & 0x7FU);
        state.setCharsetBytes(&marker, 1U);
        emit state.charsetUpdated();

        QVERIFY2(
            preview.queuedBytes() <= ServerPreview::kMaxBackpressureBytes,
            qPrintable(QStringLiteral("Backpressure cap exceeded at iter %1: %2 bytes")
                    .arg(i)
                    .arg(preview.queuedBytes())));
    }

    QVERIFY(preview.isFullSyncPending());
    QVERIFY(preview.isAwaitingPong());
    QVERIFY(preview.queuedBytes() <= ServerPreview::kMaxBackpressureBytes);

    // Clear initial stalled packet #1 so we observe ONLY the coalesced full sync sent after PONG recovery!
    contiki.clearRecordedPackets();
    contiki.setAutoPong(true);

    QTRY_VERIFY(!preview.isAwaitingPong() && !preview.isFullSyncPending() && preview.queuedBytes() == 0U);
    QVERIFY2(contiki.parseError().isEmpty(), qPrintable(contiki.parseError()));
    QVERIFY(contiki.maxObservedWindowBytes() <= VCHAR64_SERVER_BUFFER_SIZE);

    bool sawCoalescedSentinel0x99 = false;
    for (const ParsedWirePacket& pkt : contiki.packets()) {
        if (pkt.type == static_cast<uint8_t>(TYPE_SET_CHARS)
            && pkt.addrOrIdx == 0U
            && pkt.count == 128U
            && !pkt.payload.isEmpty()
            && static_cast<uint8_t>(pkt.payload.at(0)) == 0x99U) {
            sawCoalescedSentinel0x99 = true;
        }
    }
    QVERIFY(sawCoalescedSentinel0x99);

    // Disconnect and reconnect to confirm clean state reset and zero memory leaks under ASan
    QSignalSpy disconnectedSpy(&preview, &ILivePreviewSink::previewDisconnected);
    preview.disconnectPreview();
    QTRY_COMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(preview.queuedBytes(), 0U);
    QVERIFY(!preview.isFullSyncPending());
}

void TestClipboardAndNetwork::testAutoUpdaterOfflineHttpsSchemeEnforcement()
{
    const QUrl defaultUrl(QString::fromLatin1(AutoUpdater::kDefaultManifestUrl));
    QVERIFY(defaultUrl.isValid());
    QCOMPARE(defaultUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(
        defaultUrl.toString(),
        QStringLiteral("https://ricardoquesada.github.io/vchar64/LATEST_VERSION.txt"));
    QVERIFY(AutoUpdater::isStrictHttpsUrl(defaultUrl));

    AutoUpdater updater;
    QSignalSpy availableSpy(&updater, &AutoUpdater::updateAvailable);
    QSignalSpy upToDateSpy(&updater, &AutoUpdater::upToDate);
    QSignalSpy failedSpy(&updater, &AutoUpdater::checkFailed);

    const std::array<QUrl, 3> rejectedUrls {
        QUrl(QStringLiteral("http://127.0.0.1:9/LATEST_VERSION.txt")),
        QUrl(QStringLiteral("file:///etc/passwd")),
        QUrl(QStringLiteral("ftp://example.com/LATEST_VERSION.txt")),
    };

    int expectedFailures = 0;
    for (const QUrl& badUrl : rejectedUrls) {
        QVERIFY(!AutoUpdater::isStrictHttpsUrl(badUrl));
        updater.checkUpdate(badUrl);
        ++expectedFailures;
        QCOMPARE(failedSpy.count(), expectedFailures);
        QCOMPARE(availableSpy.count(), 0);
        QCOMPARE(upToDateSpy.count(), 0);
    }
}

void TestClipboardAndNetwork::testAutoUpdaterOffline64KiBStreamingCap()
{
    AutoUpdater updater;
    updater.setCurrentVersion(QStringLiteral("1.0.0"));

    QSignalSpy availableSpy(&updater, &AutoUpdater::updateAvailable);
    QSignalSpy failedSpy(&updater, &AutoUpdater::checkFailed);

    const QByteArray chunk32KiB(32 * 1024, 'A');
    const QByteArray chunk1Byte(1, 'B');

    updater.beginStreamingCheck();
    QVERIFY(updater.isInProgress());

    // 32 KiB + 32 KiB == 65,536 bytes (exact 64 KiB cap): still accepted
    QVERIFY(updater.ingestResponseChunk(chunk32KiB));
    QCOMPARE(updater.bufferedResponseBytes(), 32 * 1024);
    QVERIFY(updater.ingestResponseChunk(chunk32KiB));
    QCOMPARE(updater.bufferedResponseBytes(), 64 * 1024);
    QCOMPARE(failedSpy.count(), 0);

    // + 1 byte == 65,537 bytes (> 64 KiB): immediately aborts, clears buffer, emits checkFailed!
    QVERIFY(!updater.ingestResponseChunk(chunk1Byte));
    QVERIFY(!updater.isInProgress());
    QCOMPARE(updater.bufferedResponseBytes(), 0);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(availableSpy.count(), 0);
}

void TestClipboardAndNetwork::testAutoUpdaterHtmlSanitizationStrictHttpsUrlAndSignals()
{
    AutoUpdater updater;
    updater.setCurrentVersion(QStringLiteral("1.0.0"));

    // (A) Newer version 1.2.0 with HTTPS release URL and HTML changelog containing <script>
    {
        QSignalSpy availableSpy(&updater, &AutoUpdater::updateAvailable);
        QSignalSpy upToDateSpy(&updater, &AutoUpdater::upToDate);
        QSignalSpy failedSpy(&updater, &AutoUpdater::checkFailed);

        const QByteArray manifestA = QByteArrayLiteral(
            "stable_version: 1.2.0\n"
            "stable_url: https://github.com/ricardoquesada/vchar64/releases/tag/v1.2.0\n"
            "changes:\n"
            "<ul>\n"
            "<li>[NEW] Fast <b>Atlas</b></li>\n"
            "<li>[FIX] CTM5 bounds</li>\n"
            "</ul>\n"
            "<script>evil()</script>\n");

        updater.processManifestPayload(manifestA);

        QCOMPARE(availableSpy.count(), 1);
        QCOMPARE(upToDateSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);

        const auto info = availableSpy.at(0).at(0).value<UpdateInfo>();
        QCOMPARE(info.version, QStringLiteral("1.2.0"));
        QCOMPARE(
            info.httpsDownloadUrl,
            QUrl(QStringLiteral("https://github.com/ricardoquesada/vchar64/releases/tag/v1.2.0")));
        QCOMPARE(
            info.plainTextChanges,
            QStringLiteral("- [NEW] Fast Atlas\n- [FIX] CTM5 bounds"));
        QVERIFY(!info.plainTextChanges.contains(QStringLiteral("<ul>"), Qt::CaseInsensitive));
        QVERIFY(!info.plainTextChanges.contains(QStringLiteral("<li>"), Qt::CaseInsensitive));
        QVERIFY(!info.plainTextChanges.contains(QStringLiteral("<b>"), Qt::CaseInsensitive));
        QVERIFY(!info.plainTextChanges.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
        QVERIFY(!info.plainTextChanges.contains(QStringLiteral("evil"), Qt::CaseInsensitive));
    }

    // (B) Equal (1.0.0) and older (0.9.5) versions emit upToDate() and zero updateAvailable
    for (const QByteArray& ver : { QByteArrayLiteral("1.0.0"), QByteArrayLiteral("0.9.5") }) {
        QSignalSpy availableSpy(&updater, &AutoUpdater::updateAvailable);
        QSignalSpy upToDateSpy(&updater, &AutoUpdater::upToDate);
        QSignalSpy failedSpy(&updater, &AutoUpdater::checkFailed);

        const QByteArray manifestB = QByteArrayLiteral("stable_version: ") + ver
            + QByteArrayLiteral("\nstable_url: https://github.com/ricardoquesada/vchar64/releases\nchanges:\nNone\n");
        updater.processManifestPayload(manifestB);

        QCOMPARE(upToDateSpy.count(), 1);
        QCOMPARE(availableSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
    }

    // (C) Unsafe stable_url schemes/paths are strictly rejected (never emitting updateAvailable)
    const std::array<QByteArray, 4> unsafeUrls {
        QByteArrayLiteral("http://evil.com/v.exe"),
        QByteArrayLiteral("file:///Applications/Calculator.app"),
        QByteArrayLiteral("javascript:alert(1)"),
        QByteArrayLiteral("/tmp/local_binary"),
    };

    for (const QByteArray& unsafeUrl : unsafeUrls) {
        QVERIFY(!AutoUpdater::isStrictHttpsUrl(QUrl(QString::fromUtf8(unsafeUrl), QUrl::StrictMode)));

        QSignalSpy availableSpy(&updater, &AutoUpdater::updateAvailable);
        QSignalSpy upToDateSpy(&updater, &AutoUpdater::upToDate);
        QSignalSpy failedSpy(&updater, &AutoUpdater::checkFailed);

        const QByteArray manifestC = QByteArrayLiteral("stable_version: 1.2.0\nstable_url: ")
            + unsafeUrl
            + QByteArrayLiteral("\nchanges:\n<li>Update</li>\n");
        updater.processManifestPayload(manifestC);

        QCOMPARE(availableSpy.count(), 0);
        QCOMPARE(upToDateSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
    }
}

QTEST_GUILESS_MAIN(TestClipboardAndNetwork)
#include "tst_clipboard_and_network.moc"
