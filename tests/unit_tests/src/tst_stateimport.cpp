/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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
 * @file tst_stateimport.cpp
 * @brief Headless `QTest` suite verifying `StateImport` golden corpora and adversarial binary hardening.
 *
 * Validates lossless round-tripping across all 17 `.vchar64proj` repository fixtures,
 * CharPad `.ctm` v4/v5 streams, VICE C64/C128 `.vsf` snapshots, Koala `.koa`/`.kla`
 * bitmaps (`10002` and `10003` bytes), and raw `.bin`/`.prg` dumps, plus regression
 * defenses against CTM v5 `num_tiles == 65535` heap overflows, `num_chars > 256`
 * stream misalignment, unmasked `0xFF` color nibbles, and zero-length VICE `.vsf`
 * module header infinite loops.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "palette.h"
#include "state.h"
#include "stateexport.h"
#include "stateimport.h"

namespace {

[[nodiscard]] QString sourcePath(const QString& relativePath)
{
    return QDir(QStringLiteral(SOURCE_DIR)).filePath(relativePath);
}

[[nodiscard]] QByteArray buildSyntheticCtm4(
    uint8_t expanded,
    uint8_t colorMode,
    uint8_t vicRes,
    const std::array<uint8_t, 4>& colors,
    uint16_t numCharsMinusOne,
    uint8_t numTilesMinusOne,
    uint8_t tileWidth,
    uint8_t tileHeight,
    uint16_t mapWidth,
    uint16_t mapHeight,
    uint8_t first256CharFill,
    uint8_t excessCharFill,
    uint8_t attrFill,
    uint8_t mapFill)
{
    StateImport::CTMHeader4 hdr { };
    std::memcpy(hdr.id, "CTM", 3);
    hdr.version = 4U;
    for (size_t i = 0; i < 4U; ++i) {
        hdr.colors[i] = colors[i];
    }
    hdr.color_mode = colorMode;
    hdr.vic_res = vicRes;
    hdr.num_chars = qToLittleEndian<quint16>(numCharsMinusOne);
    hdr.num_tiles = numTilesMinusOne;
    hdr.tile_width = tileWidth;
    hdr.tile_height = tileHeight;
    hdr.map_width = qToLittleEndian<quint16>(mapWidth);
    hdr.map_height = qToLittleEndian<quint16>(mapHeight);
    hdr.expanded = expanded;

    QByteArray out;
    out.append(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));

    const int numChars = static_cast<int>(numCharsMinusOne) + 1;
    const int numTiles = static_cast<int>(numTilesMinusOne) + 1;
    const int storedChars = std::min(numChars, 256);
    out.append(storedChars * 8, static_cast<char>(first256CharFill));
    if (numChars > storedChars) {
        out.append((numChars - storedChars) * 8, static_cast<char>(excessCharFill));
    }

    // Char attributes (numChars bytes)
    out.append(numChars, static_cast<char>(attrFill));

    // Cell attributes (numTiles * tw * th bytes)
    const int cellAttribs = numTiles * static_cast<int>(tileWidth) * static_cast<int>(tileHeight);
    if (cellAttribs > 0) {
        out.append(cellAttribs, static_cast<char>(0x77U));
    }

    // In color_mode == 1, CTM v4 includes an additional per-tile attribute block (numTiles bytes).
    if (colorMode == 1U) {
        out.append(numTiles, static_cast<char>(attrFill));
    }

    const int mapCells = static_cast<int>(mapWidth) * static_cast<int>(mapHeight);
    if (mapCells > 0) {
        out.append(mapCells, static_cast<char>(mapFill));
    }
    return out;
}

[[nodiscard]] QByteArray buildSyntheticCtm5(
    uint8_t flags,
    uint8_t colorMode,
    const std::array<uint8_t, 4>& colors,
    uint16_t numCharsMinusOne,
    uint16_t numTilesMinusOne,
    uint8_t tileWidth,
    uint8_t tileHeight,
    uint16_t mapWidth,
    uint16_t mapHeight,
    uint8_t first256CharFill,
    uint8_t excessCharFill,
    uint8_t attrFill,
    const std::vector<uint16_t>& mapCells)
{
    StateImport::CTMHeader5 hdr { };
    std::memcpy(hdr.id, "CTM", 3);
    hdr.version = 5U;
    for (size_t i = 0; i < 4U; ++i) {
        hdr.colors[i] = colors[i];
    }
    hdr.color_mode = colorMode;
    hdr.flags = flags;
    hdr.num_chars = qToLittleEndian<quint16>(numCharsMinusOne);
    hdr.num_tiles = qToLittleEndian<quint16>(numTilesMinusOne);
    hdr.tile_width = tileWidth;
    hdr.tile_height = tileHeight;
    hdr.map_width = qToLittleEndian<quint16>(mapWidth);
    hdr.map_height = qToLittleEndian<quint16>(mapHeight);

    QByteArray out;
    out.append(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));

    const int numChars = static_cast<int>(numCharsMinusOne) + 1;
    const int numTiles = static_cast<int>(numTilesMinusOne) + 1;
    const int storedChars = std::min(numChars, 256);
    out.append(storedChars * 8, static_cast<char>(first256CharFill));
    if (numChars > storedChars) {
        out.append((numChars - storedChars) * 8, static_cast<char>(excessCharFill));
    }

    // Char attributes block (numChars bytes) is always present in CTM v5.
    out.append(numChars, static_cast<char>((colorMode == 2U) ? attrFill : 0x55U));

    // In color_mode == 1, CTM v5 follows char attributes with numTiles per-tile color bytes.
    if (colorMode == 1U) {
        out.append(numTiles, static_cast<char>(attrFill));
    }

    for (const uint16_t cell : mapCells) {
        uint8_t le[2] { };
        qToLittleEndian<quint16>(cell, le);
        out.append(reinterpret_cast<const char*>(le), 2);
    }

    return out;
}

} // namespace

class StateImportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // A. Real Repository Golden Corpus
    void testGoldenVChar64ExamplesCorpus();
    void testGoldenCtmV4AndV5Corpus();
    void testGoldenViceSnapshotC64AndC128Corpus();
    void testGoldenKoala10002And10003Corpus();
    void testGoldenRawBinAndPrgCorpus();

    // B. Adversarial Malformed Binary Corpus
    void testCtm4AndCtm5NumChars4096StreamAlignment();
    void testCtm5NumTiles65535HeapOverflowRegression();
    void testCtm4ZeroTileDimensionsAndEndianness();
    void testCtm5ZeroTileWidthClampAndMapCellOver255Warning();
    void testColorNibbleMasking0xFFAcrossAllImporters();
    void testVChar64InvalidHeadersAndMapDimensionOverflow();
    void testKoalaBoundaryLengthsRejection();
    void testViceSnapshotZeroModuleLengthInfiniteLoopRegression();
};

void StateImportTest::initTestCase()
{
    QVERIFY(QCoreApplication::instance() != nullptr);
    QCOMPARE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()), nullptr);
    QVERIFY(QDir(QStringLiteral(SOURCE_DIR)).exists());
}

void StateImportTest::testGoldenVChar64ExamplesCorpus()
{
    const std::array<QString, 17> kFixtures = {
        QStringLiteral("examples/font_astra_2x2.vchar64proj"),
        QStringLiteral("examples/font_boulderdash_1x2.vchar64proj"),
        QStringLiteral("examples/font_broadway_2x2.vchar64proj"),
        QStringLiteral("examples/font_caren_1x2.vchar64proj"),
        QStringLiteral("examples/font_cuneiform_2x2.vchar64proj"),
        QStringLiteral("examples/font_doppel_2x2.vchar64proj"),
        QStringLiteral("examples/font_hero_atari_1x1.vchar64proj"),
        QStringLiteral("examples/font_ianmic02_2x2.vchar64proj"),
        QStringLiteral("examples/font_rambo_2x2.vchar64proj"),
        QStringLiteral("examples/font_reset04_2x3.vchar64proj"),
        QStringLiteral("examples/font_reset06_3x3.vchar64proj"),
        QStringLiteral("examples/font_reset07_2x2.vchar64proj"),
        QStringLiteral("examples/font_subchrist_chrome_2x2.vchar64proj"),
        QStringLiteral("examples/font_unknown_2x2.vchar64proj"),
        QStringLiteral("examples/game_commando_l1.vchar64proj"),
        QStringLiteral("examples/c64_loader/pvm_logo.vchar64proj"),
        QStringLiteral("tests/caren_1x2_v1.vchar64proj"),
    };

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    int idx = 0;
    for (const QString& relPath : kFixtures) {
        const QString fullPath = sourcePath(relPath);
        QVERIFY2(QFileInfo::exists(fullPath), qPrintable(fullPath));

        State loaded;
        const auto res = StateImport::loadVChar64(loaded, fullPath);
        QVERIFY2(res.isOk(), qPrintable(QStringLiteral("%1: %2").arg(relPath, res.isOk() ? QString() : res.error().detail)));

        const auto tp = loaded.tileProperties();
        QVERIFY(tp.size.width() >= 1 && tp.size.width() <= State::MAX_TILE_WIDTH);
        QVERIFY(tp.size.height() >= 1 && tp.size.height() <= State::MAX_TILE_HEIGHT);
        QVERIFY(tp.size.width() * tp.size.height() <= 256);
        QCOMPARE(
            loaded.map().size(),
            static_cast<size_t>(loaded.mapSize().width()) * static_cast<size_t>(loaded.mapSize().height()));
        for (const uint8_t c : loaded.tileColors()) {
            QVERIFY(c <= 0x0FU);
        }
        QCOMPARE(loaded.getUndoStack()->count(), 0);
        QVERIFY(loaded.getUndoStack()->isClean());

        // Round-trip saveVChar64 -> loadVChar64 must preserve all buffers & metadata bit-identically.
        const QString roundTripPath = tempDir.filePath(QStringLiteral("rt_%1.vchar64proj").arg(idx++));
        const auto saveRes = StateExport::saveVChar64(loaded, roundTripPath);
        QVERIFY(saveRes.isOk());

        State reloaded;
        const auto reloadRes = StateImport::loadVChar64(reloaded, roundTripPath);
        QVERIFY(reloadRes.isOk());

        QCOMPARE(reloaded.charset(), loaded.charset());
        QCOMPARE(reloaded.tileColors(), loaded.tileColors());
        QCOMPARE(reloaded.mapSize(), loaded.mapSize());
        QCOMPARE(reloaded.map(), loaded.map());
        QCOMPARE(reloaded.tileProperties(), loaded.tileProperties());
        QCOMPARE(reloaded.isMulticolorMode(), loaded.isMulticolorMode());
        QCOMPARE(reloaded.getForegroundColorMode(), loaded.getForegroundColorMode());
        QCOMPARE(reloaded.exportProperties(), loaded.exportProperties());
        QCOMPARE(reloaded.getKeyboardMapping(), loaded.getKeyboardMapping());
        for (int pen = 0; pen < 4; ++pen) {
            QCOMPARE(reloaded.getGlobalColorForPen(pen), loaded.getGlobalColorForPen(pen));
        }
    }
}

void StateImportTest::testGoldenCtmV4AndV5Corpus()
{
    const std::array<QString, 8> kExpandedFixtures = {
        QStringLiteral("tests/Commando L1_v4.ctm"),
        QStringLiteral("tests/Subchrist - Demo - Gothic Bakery - 3x3_v4.ctm"),
        QStringLiteral("tests/Subchrist - Demo - Gothic Bakery - 3x3_v5.ctm"),
        QStringLiteral("tests/Subchrist - Demo - Parklife - 4x4_v5.ctm"),
        QStringLiteral("tests/Subchrist - Font - Chrome - 2x2_v4.ctm"),
        QStringLiteral("tests/Subchrist - Game - Cactus - 4x4_v4.ctm"),
        QStringLiteral("tests/Uridium L01 Zinc_v4.ctm"),
        QStringLiteral("tests/pvm_logo_blue_v5.ctm"),
    };

    for (const QString& relPath : kExpandedFixtures) {
        const QString fullPath = sourcePath(relPath);
        QVERIFY2(QFileInfo::exists(fullPath), qPrintable(fullPath));

        State state;
        const auto res = StateImport::loadCTM(state, fullPath);
        QVERIFY2(res.isOk(), qPrintable(QStringLiteral("%1: %2").arg(relPath, res.isOk() ? QString() : res.error().detail)));
        QCOMPARE(state.getUndoStack()->count(), 0);
        for (const uint8_t c : state.tileColors()) {
            QVERIFY(c <= 0x0FU);
        }
        QCOMPARE(
            state.map().size(),
            static_cast<size_t>(state.mapSize().width()) * static_cast<size_t>(state.mapSize().height()));
    }

    // Verify decoded header & payload properties of Gothic Bakery 3x3_v4.ctm (25x8) and 3x3_v5.ctm (25x6).
    State gothicV4;
    State gothicV5;
    QVERIFY(StateImport::loadCTM(gothicV4, sourcePath(QStringLiteral("tests/Subchrist - Demo - Gothic Bakery - 3x3_v4.ctm"))).isOk());
    QVERIFY(StateImport::loadCTM(gothicV5, sourcePath(QStringLiteral("tests/Subchrist - Demo - Gothic Bakery - 3x3_v5.ctm"))).isOk());
    QCOMPARE(gothicV4.tileProperties().size, QSize(3, 3));
    QCOMPARE(gothicV5.tileProperties().size, QSize(3, 3));
    QVERIFY(gothicV4.isMulticolorMode());
    QVERIFY(gothicV5.isMulticolorMode());
    QCOMPARE(gothicV4.getForegroundColorMode(), State::FOREGROUND_COLOR_GLOBAL);
    QCOMPARE(gothicV5.getForegroundColorMode(), State::FOREGROUND_COLOR_GLOBAL);
    QCOMPARE(gothicV4.getGlobalColorForPen(0), 12);
    QCOMPARE(gothicV5.getGlobalColorForPen(0), 12);
    QCOMPARE(gothicV4.mapSize(), QSize(25, 8));
    QCOMPARE(gothicV5.mapSize(), QSize(25, 6));
    // Both fixtures store 252 characters (2,016 bytes), leaving trailing 32 bytes [2016..2047] zero-padded.
    for (size_t i = 2016U; i < State::CHAR_BUFFER_SIZE; ++i) {
        QCOMPARE(gothicV4.charset()[i], static_cast<uint8_t>(0U));
        QCOMPARE(gothicV5.charset()[i], static_cast<uint8_t>(0U));
    }

    // Unexpanded CTM file must return UnexpandedCtmUnsupported without mutating State.
    State untouched;
    const auto beforeCharset = untouched.charset();
    const auto beforeMap = untouched.map();
    const auto unexpandedRes = StateImport::loadCTM(
        untouched,
        sourcePath(QStringLiteral("tests/Armalyte L1-unsuppported.ctm")));
    QVERIFY(!unexpandedRes.isOk());
    QCOMPARE(unexpandedRes.error().code, vchar64::ErrorCode::UnexpandedCtmUnsupported);
    QCOMPARE(untouched.charset(), beforeCharset);
    QCOMPARE(untouched.map(), beforeMap);
    QCOMPARE(untouched.getUndoStack()->count(), 0);
}

void StateImportTest::testGoldenViceSnapshotC64AndC128Corpus()
{
    const std::array<QString, 7> kSnapshots = {
        QStringLiteral("tests/bluemax.vsf"),
        QStringLiteral("tests/boulder_dash.vsf"),
        QStringLiteral("tests/rambo.vsf"),
        QStringLiteral("tests/seuck_game.vsf"),
        QStringLiteral("tests/super-mario-vice3.6.vsf"),
        QStringLiteral("tests/world_cup_carnival.vsf"),
        QStringLiteral("tests/the_race_128_vice3.vsf"),
    };

    auto ram64k = std::make_unique<std::array<uint8_t, 65536>>();
    std::array<uint8_t, 1024> colorRam { };
    std::array<uint8_t, 64> vicRegs { };

    for (const QString& relPath : kSnapshots) {
        const QString fullPath = sourcePath(relPath);
        QVERIFY2(QFileInfo::exists(fullPath), qPrintable(fullPath));

        ram64k->fill(0U);
        colorRam.fill(0xFFU);
        vicRegs.fill(0U);
        uint16_t charsetAddr = 0xFFFFU;
        uint16_t screenAddr = 0xFFFFU;

        const auto res = StateImport::parseVICESnapshot(
            fullPath,
            ram64k->data(),
            &charsetAddr,
            &screenAddr,
            colorRam.data(),
            vicRegs.data());
        QVERIFY2(res.isOk(), qPrintable(QStringLiteral("%1: %2").arg(relPath, res.isOk() ? QString() : res.error().detail)));

        const bool hasNonZeroRam = std::any_of(
            ram64k->begin(),
            ram64k->end(),
            [](uint8_t b) { return b != 0U; });
        QVERIFY(hasNonZeroRam);
        QCOMPARE(charsetAddr % 2048U, 0U);
        QCOMPARE(screenAddr % 1024U, 0U);
        for (const uint8_t nibble : colorRam) {
            QVERIFY(nibble <= 0x0FU);
        }

        if (relPath.endsWith(QStringLiteral("seuck_game.vsf"))) {
            QCOMPARE(charsetAddr, static_cast<uint16_t>(0xF800U));
            QCOMPARE(screenAddr, static_cast<uint16_t>(0xEC00U));
        }
    }
}

void StateImportTest::testGoldenKoala10002And10003Corpus()
{
    const std::array<QString, 6> kKoala10003 = {
        QStringLiteral("tests/athena-tob.koa"),
        QStringLiteral("tests/bcs_quest_for_tires.koa"),
        QStringLiteral("tests/creature.koa"),
        QStringLiteral("tests/pvm_logo_blue.koa"),
        QStringLiteral("tests/rambo-title.koa"),
        QStringLiteral("tests/the-muni-race.kla"),
    };

    const std::array<QString, 3> kKoala10002 = {
        QStringLiteral("tests/pvm_logo_gray.koa"),
        QStringLiteral("tests/pvm_logo_green.koa"),
        QStringLiteral("tests/pvm_logo_pink.koa"),
    };

    for (const QString& relPath : kKoala10003) {
        const QString fullPath = sourcePath(relPath);
        QFile rawFile(fullPath);
        QVERIFY(rawFile.open(QIODevice::ReadOnly));
        const QByteArray rawBytes = rawFile.readAll();
        QCOMPARE(rawBytes.size(), static_cast<qsizetype>(10003));

        const auto res = StateImport::loadKoala(fullPath);
        QVERIFY2(res.isOk(), qPrintable(relPath));
        const auto& koa = res.value();
        QCOMPARE(koa.backgroundColor, static_cast<uint8_t>(static_cast<uint8_t>(rawBytes[10002]) & 0x0FU));
        for (const uint8_t c : koa.colorRam) {
            QVERIFY(c <= 0x0FU);
        }
    }

    for (const QString& relPath : kKoala10002) {
        const QString fullPath = sourcePath(relPath);
        QCOMPARE(QFileInfo(fullPath).size(), 10002LL);

        const auto res = StateImport::loadKoala(fullPath);
        QVERIFY2(res.isOk(), qPrintable(relPath));
        QCOMPARE(res.value().backgroundColor, static_cast<uint8_t>(0U));
        for (const uint8_t c : res.value().colorRam) {
            QVERIFY(c <= 0x0FU);
        }
    }
}

void StateImportTest::testGoldenRawBinAndPrgCorpus()
{
    // 1. 2,048-byte C64 ROM charsets
    for (const QString& relBin : {
             QStringLiteral("src/res/c64-chargen-uppercase.bin"),
             QStringLiteral("src/res/c64-chargen-lowercase.bin") }) {
        const QString fullPath = sourcePath(relBin);
        QFile f(fullPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray expected = f.readAll();
        QCOMPARE(expected.size(), static_cast<qsizetype>(State::CHAR_BUFFER_SIZE));

        State state;
        const auto res = StateImport::loadRaw(state, fullPath);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().bytesRead, 2048LL);
        QVERIFY(std::memcmp(state.charset().data(), expected.constData(), State::CHAR_BUFFER_SIZE) == 0);
    }

    // 2. Short PRG: tests/issue_24.PRG (482 bytes = 2-byte address + 480-byte payload, trailing 1,568 bytes zeroed!)
    {
        const QString prgPath = sourcePath(QStringLiteral("tests/issue_24.PRG"));
        QFile f(prgPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QCOMPARE(raw.size(), static_cast<qsizetype>(482));

        State state;
        State::CharsetBuffer dirty { };
        dirty.fill(0xFEU);
        state.setCharsetBytes(dirty.data(), dirty.size());

        quint16 addr = 0;
        const auto res = StateImport::loadPRG(state, prgPath, &addr);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().bytesRead, 480LL);
        QCOMPARE(
            addr,
            qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(raw.constData())));
        QVERIFY(std::memcmp(state.charset().data(), raw.constData() + 2, 480U) == 0);
        for (size_t i = 480U; i < State::CHAR_BUFFER_SIZE; ++i) {
            QCOMPARE(state.charset()[i], static_cast<uint8_t>(0U));
        }
    }

    // 3. Oversized PRG: examples/c64_loader/loader.prg (3,455 bytes -> clamps cleanly to 2,048 bytes with warning)
    {
        const QString prgPath = sourcePath(QStringLiteral("examples/c64_loader/loader.prg"));
        State state;
        quint16 addr = 0;
        const auto res = StateImport::loadPRG(state, prgPath, &addr);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().bytesRead, 2048LL);
        QVERIFY(!res.value().warnings.isEmpty());
    }
}

void StateImportTest::testCtm4AndCtm5NumChars4096StreamAlignment()
{
    // CTM v4: num_chars - 1 = 4095 (4,096 chars = 32,768B: first 2,048B = 0xA5, excess 30,720B = 0xEE),
    // color_mode = 2 attributes = 0xF3 (4,096B), 4x2 map = 0x42.
    {
        QByteArray payload = buildSyntheticCtm4(
            /*expanded=*/1U,
            /*colorMode=*/2U,
            /*vicRes=*/1U,
            { 1U, 2U, 3U, 4U },
            /*numCharsMinusOne=*/4095U,
            /*numTilesMinusOne=*/7U,
            /*tileWidth=*/1U,
            /*tileHeight=*/1U,
            /*mapWidth=*/4U,
            /*mapHeight=*/2U,
            /*first256CharFill=*/0xA5U,
            /*excessCharFill=*/0xEEU,
            /*attrFill=*/0xF3U,
            /*mapFill=*/0x42U);

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY2(res.isOk(), qPrintable(res.isOk() ? QString() : res.error().detail));
        QVERIFY(res.value().warnings.size() >= 2);

        for (const uint8_t b : state.charset()) {
            QCOMPARE(b, static_cast<uint8_t>(0xA5U));
        }
        for (const uint8_t c : state.tileColors()) {
            QCOMPARE(c, static_cast<uint8_t>(0x03U)); // 0xF3 & 0x0F (NOT 0xEE & 0x0F!)
        }
        QCOMPARE(state.mapSize(), QSize(4, 2));
        for (const uint8_t m : state.map()) {
            QCOMPARE(m, static_cast<uint8_t>(0x42U));
        }
    }

    // CTM v5: same 4,096-char excess stream alignment check with 16-bit LE map cells = 0x0042.
    {
        QByteArray payload = buildSyntheticCtm5(
            /*flags=*/0x03U, // Tile System + Expanded
            /*colorMode=*/2U,
            { 1U, 2U, 3U, 4U },
            /*numCharsMinusOne=*/4095U,
            /*numTilesMinusOne=*/7U,
            /*tileWidth=*/1U,
            /*tileHeight=*/1U,
            /*mapWidth=*/4U,
            /*mapHeight=*/2U,
            /*first256CharFill=*/0xA5U,
            /*excessCharFill=*/0xEEU,
            /*attrFill=*/0xF3U,
            std::vector<uint16_t>(8U, 0x0042U));

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY2(res.isOk(), qPrintable(res.isOk() ? QString() : res.error().detail));
        QVERIFY(res.value().warnings.size() >= 2);

        for (const uint8_t b : state.charset()) {
            QCOMPARE(b, static_cast<uint8_t>(0xA5U));
        }
        for (const uint8_t c : state.tileColors()) {
            QCOMPARE(c, static_cast<uint8_t>(0x03U));
        }
        QCOMPARE(state.mapSize(), QSize(4, 2));
        for (const uint8_t m : state.map()) {
            QCOMPARE(m, static_cast<uint8_t>(0x42U));
        }
    }
}

void StateImportTest::testCtm5NumTiles65535HeapOverflowRegression()
{
    const std::vector<uint16_t> kMap2x2 = { 0x0001U, 0x0002U, 0x0003U, 0x0004U };
    const std::vector<uint8_t> kExpectedMap = { 1U, 2U, 3U, 4U };

    // Case 1: color_mode = 1 (Per-Tile) with num_tiles - 1 = 65534 (65,535 tiles) AND 0xFFFF (65,536 tiles)
    for (const uint16_t numTilesMinusOne : { static_cast<uint16_t>(65534U), static_cast<uint16_t>(0xFFFFU) }) {
        QByteArray payload = buildSyntheticCtm5(
            /*flags=*/0x03U,
            /*colorMode=*/1U,
            { 0U, 5U, 7U, 11U },
            /*numCharsMinusOne=*/255U,
            numTilesMinusOne,
            /*tileWidth=*/1U,
            /*tileHeight=*/1U,
            /*mapWidth=*/2U,
            /*mapHeight=*/2U,
            /*first256CharFill=*/0x11U,
            /*excessCharFill=*/0x00U,
            /*attrFill=*/0xFAU,
            kMap2x2);

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY(res.isOk());
        QVERIFY(!res.value().warnings.isEmpty());
        for (const uint8_t c : state.tileColors()) {
            QCOMPARE(c, static_cast<uint8_t>(0x0AU));
        }
        QCOMPARE(state.map(), kExpectedMap);
    }

    // Case 2: color_mode = 2 (Per-Char) with num_chars - 1 = 0xFFFF (65,536 chars & attributes!)
    {
        QByteArray payload = buildSyntheticCtm5(
            /*flags=*/0x03U,
            /*colorMode=*/2U,
            { 0U, 5U, 7U, 11U },
            /*numCharsMinusOne=*/0xFFFFU,
            /*numTilesMinusOne=*/255U,
            /*tileWidth=*/1U,
            /*tileHeight=*/1U,
            /*mapWidth=*/2U,
            /*mapHeight=*/2U,
            /*first256CharFill=*/0x22U,
            /*excessCharFill=*/0x99U,
            /*attrFill=*/0xFAU,
            kMap2x2);

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY(res.isOk());
        QVERIFY(res.value().warnings.size() >= 2);
        for (const uint8_t c : state.tileColors()) {
            QCOMPARE(c, static_cast<uint8_t>(0x0AU));
        }
        QCOMPARE(state.map(), kExpectedMap);
    }
}

void StateImportTest::testCtm4ZeroTileDimensionsAndEndianness()
{
    // (A) tw = 0, th = 2; (B) tw = 2, th = 0; (C) tw = 17, th = 16 -> ErrorCode::InvalidDimensions, zero divide-by-zero!
    const std::array<std::pair<uint8_t, uint8_t>, 3> kInvalidTileDims = { {
        { 0U, 2U },
        { 2U, 0U },
        { 17U, 16U },
    } };

    for (const auto [tw, th] : kInvalidTileDims) {
        QByteArray payload = buildSyntheticCtm4(
            /*expanded=*/1U,
            /*colorMode=*/0U,
            /*vicRes=*/0U,
            { 1U, 2U, 3U, 4U },
            /*numCharsMinusOne=*/3U,
            /*numTilesMinusOne=*/3U,
            tw,
            th,
            /*mapWidth=*/4U,
            /*mapHeight=*/4U,
            0x33U,
            0x00U,
            0x01U,
            0x00U);

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, vchar64::ErrorCode::InvalidDimensions);
        QCOMPARE(state.tileProperties().size, QSize(1, 1));
        QCOMPARE(state.getUndoStack()->count(), 0);
    }

    // (D) Valid 24-byte CTM4 header with 256 chars (0x00FF LE), 256 tiles (0xFF), 40x25 map (0x0028 x 0x0019 LE)
    {
        QByteArray payload = buildSyntheticCtm4(
            /*expanded=*/1U,
            /*colorMode=*/1U,
            /*vicRes=*/1U,
            { 6U, 7U, 8U, 9U },
            /*numCharsMinusOne=*/0x00FFU,
            /*numTilesMinusOne=*/0xFFU,
            /*tileWidth=*/1U,
            /*tileHeight=*/1U,
            /*mapWidth=*/40U,
            /*mapHeight=*/25U,
            /*first256CharFill=*/0x7EU,
            /*excessCharFill=*/0x00U,
            /*attrFill=*/0x0CU,
            /*mapFill=*/0x19U);

        QBuffer buf(&payload);
        State state;
        const auto res = StateImport::loadCTM(state, buf);
        QVERIFY(res.isOk());
        QCOMPARE(state.mapSize(), QSize(40, 25));
        QCOMPARE(state.map().size(), static_cast<size_t>(1000));
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0x7EU));
        QCOMPARE(state.tileColors()[255], static_cast<uint8_t>(0x0CU));
        QCOMPARE(state.getUndoStack()->count(), 0);
    }
}

void StateImportTest::testCtm5ZeroTileWidthClampAndMapCellOver255Warning()
{
    // flags = 0x02 (Tile System disabled = bit 0 is 0, Expanded = bit 1 is 1),
    // tile_width = 0, tile_height = 0, map 3x1 with 16-bit LE cells {7, 298 (0x012A), 1023 (0x03FF)}.
    QByteArray payload = buildSyntheticCtm5(
        /*flags=*/0x02U,
        /*colorMode=*/0U,
        { 1U, 2U, 3U, 4U },
        /*numCharsMinusOne=*/15U,
        /*numTilesMinusOne=*/15U,
        /*tileWidth=*/0U,
        /*tileHeight=*/0U,
        /*mapWidth=*/3U,
        /*mapHeight=*/1U,
        /*first256CharFill=*/0x5AU,
        /*excessCharFill=*/0x00U,
        /*attrFill=*/0x01U,
        { 7U, 298U, 1023U });

    QBuffer buf(&payload);
    State state;
    const auto res = StateImport::loadCTM(state, buf);
    QVERIFY2(res.isOk(), qPrintable(res.isOk() ? QString() : res.error().detail));

    QCOMPARE(state.tileProperties().size, QSize(1, 1));
    QCOMPARE(state.mapSize(), QSize(3, 1));
    const std::vector<uint8_t> expectedMap = { 7U, 0x2AU, 0xFFU };
    QCOMPARE(state.map(), expectedMap);
    QVERIFY(!res.value().warnings.isEmpty());
}

void StateImportTest::testColorNibbleMasking0xFFAcrossAllImporters()
{
    const std::array<uint8_t, 4> kDirtyColors = { 0xFFU, 0x8EU, 0xF0U, 0xFD };

    auto verifyStateColorsBounded = [](const State& st) {
        for (int pen = 0; pen < 4; ++pen) {
            const int c = st.getGlobalColorForPen(pen);
            QVERIFY(c >= 0 && c <= 15);
            QVERIFY(Palette::getColor(c).isValid());
        }
        for (const uint8_t tc : st.tileColors()) {
            QVERIFY(tc <= 0x0FU);
            QVERIFY(Palette::getColor(static_cast<int>(tc)).isValid());
        }
    };

    // 1. VChar64 v3 with 0xFF / 0x8E / 0xF0 colors & tileColors
    {
        StateImport::VChar64Header hdr { };
        std::memcpy(hdr.id, "VChar", 5);
        hdr.version = 3U;
        for (size_t i = 0; i < 4U; ++i) {
            hdr.colors[i] = kDirtyColors[i];
        }
        hdr.vic_res = 1U;
        hdr.num_chars = qToLittleEndian<quint16>(256U);
        hdr.tile_width = 1U;
        hdr.tile_height = 1U;
        hdr.char_interleaved = 1U;
        hdr.color_mode = 1U;
        hdr.map_width = qToLittleEndian<quint16>(2U);
        hdr.map_height = qToLittleEndian<quint16>(2U);

        QByteArray raw;
        raw.append(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        raw.append(2048, '\x11');
        raw.append(256, static_cast<char>(0x8EU));
        raw.append(4, '\x00');

        QBuffer buf(&raw);
        State st;
        QVERIFY(StateImport::loadVChar64(st, buf).isOk());
        verifyStateColorsBounded(st);
        QCOMPARE(st.getGlobalColorForPen(0), 0x0F);
        QCOMPARE(st.getGlobalColorForPen(1), 0x0E);
        QCOMPARE(st.getGlobalColorForPen(2), 0x00);
        QCOMPARE(st.tileColors()[0], static_cast<uint8_t>(0x0EU));
    }

    // 2. CTM4
    {
        QByteArray raw = buildSyntheticCtm4(
            1U, 1U, 1U, kDirtyColors, 15U, 15U, 1U, 1U, 2U, 2U, 0x00U, 0x00U, 0xFFU, 0x00U);
        QBuffer buf(&raw);
        State st;
        QVERIFY(StateImport::loadCTM(st, buf).isOk());
        verifyStateColorsBounded(st);
        QCOMPARE(st.tileColors()[0], static_cast<uint8_t>(0x0FU));
    }

    // 3. CTM5
    {
        QByteArray raw = buildSyntheticCtm5(
            0x03U, 1U, kDirtyColors, 15U, 15U, 1U, 1U, 2U, 2U, 0x00U, 0x00U, 0x8EU, { 0U, 0U, 0U, 0U });
        QBuffer buf(&raw);
        State st;
        QVERIFY(StateImport::loadCTM(st, buf).isOk());
        verifyStateColorsBounded(st);
        QCOMPARE(st.tileColors()[0], static_cast<uint8_t>(0x0EU));
    }

    // 4. Koala (10,003 bytes with 0xFF Color RAM & 0x8E background byte)
    {
        QByteArray raw(10003, '\x00');
        std::memset(raw.data() + 2 + 8000 + 1000, 0xFF, 1000U);
        raw[10002] = static_cast<char>(0x8EU);

        QBuffer buf(&raw);
        const auto res = StateImport::loadKoala(buf);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().backgroundColor, static_cast<uint8_t>(0x0EU));
        for (const uint8_t c : res.value().colorRam) {
            QCOMPARE(c, static_cast<uint8_t>(0x0FU));
        }
    }
}

void StateImportTest::testVChar64InvalidHeadersAndMapDimensionOverflow()
{
    auto makeBaseHeader = []() {
        StateImport::VChar64Header hdr { };
        std::memcpy(hdr.id, "VChar", 5);
        hdr.version = 3U;
        hdr.num_chars = qToLittleEndian<quint16>(256U);
        hdr.tile_width = 1U;
        hdr.tile_height = 1U;
        hdr.char_interleaved = 1U;
        hdr.color_mode = 0U;
        hdr.map_width = qToLittleEndian<quint16>(4U);
        hdr.map_height = qToLittleEndian<quint16>(4U);
        return hdr;
    };

    auto runExpectErr = [](QByteArray payload, vchar64::ErrorCode expectedCode) {
        QBuffer buf(&payload);
        State st;
        const auto beforeMapSize = st.mapSize();
        const auto res = StateImport::loadVChar64(st, buf);
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, expectedCode);
        QCOMPARE(st.mapSize(), beforeMapSize);
        QCOMPARE(
            st.map().size(),
            static_cast<size_t>(st.mapSize().width()) * static_cast<size_t>(st.mapSize().height()));
    };

    // 1. < 32 bytes -> FileTooSmall
    runExpectErr(QByteArray(16, 'V'), vchar64::ErrorCode::FileTooSmall);

    // 2. Bad magic "XChar" -> InvalidMagic
    {
        auto hdr = makeBaseHeader();
        hdr.id[0] = 'X';
        QByteArray raw(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        runExpectErr(raw, vchar64::ErrorCode::InvalidMagic);
    }

    // 3. Unsupported version 0 and 4 -> UnsupportedVersion
    for (const uint8_t ver : { static_cast<uint8_t>(0U), static_cast<uint8_t>(4U) }) {
        auto hdr = makeBaseHeader();
        hdr.version = ver;
        QByteArray raw(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        runExpectErr(raw, vchar64::ErrorCode::UnsupportedVersion);
    }

    // 4. Invalid tile_width = 0 and 9 -> InvalidDimensions
    for (const uint8_t tw : { static_cast<uint8_t>(0U), static_cast<uint8_t>(9U) }) {
        auto hdr = makeBaseHeader();
        hdr.tile_width = tw;
        QByteArray raw(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        runExpectErr(raw, vchar64::ErrorCode::InvalidDimensions);
    }

    // 5. Map dimension > kMaxMapDim (5000 > 4096) -> InvalidDimensions (without 4 GiB allocation!)
    {
        auto hdr = makeBaseHeader();
        hdr.map_width = qToLittleEndian<quint16>(5000U);
        hdr.map_height = qToLittleEndian<quint16>(5000U);
        QByteArray raw(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        raw.append(2048, '\x00');
        runExpectErr(raw, vchar64::ErrorCode::InvalidDimensions);
    }

    // 6. Truncated map body (EOF mid-map) -> FileReadFailed
    {
        auto hdr = makeBaseHeader();
        QByteArray raw(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        raw.append(2048, '\x00'); // charset
        raw.append(256, '\x0B'); // tile colors
        raw.append(8, '\x20'); // only 8 of 16 map bytes!
        runExpectErr(raw, vchar64::ErrorCode::FileReadFailed);
    }
}

void StateImportTest::testKoalaBoundaryLengthsRejection()
{
    for (const qsizetype tooShort : { static_cast<qsizetype>(0), static_cast<qsizetype>(2), static_cast<qsizetype>(10001) }) {
        QByteArray raw(tooShort, '\x00');
        QBuffer buf(&raw);
        const auto res = StateImport::loadKoala(buf);
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, vchar64::ErrorCode::FileTooSmall);
    }

    // Exact 10,002 bytes (valid without background byte -> bg == 0)
    {
        QByteArray raw(10002, '\x00');
        raw[0] = 0x00;
        raw[1] = 0x60;
        QBuffer buf(&raw);
        const auto res = StateImport::loadKoala(buf);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().loadAddress, static_cast<uint16_t>(0x6000U));
        QCOMPARE(res.value().backgroundColor, static_cast<uint8_t>(0U));
    }

    // Exact 10,003 bytes (valid with bg = 0xFB -> masked to 0x0B)
    {
        QByteArray raw(10003, '\x00');
        raw[10002] = static_cast<char>(0xFBU);
        QBuffer buf(&raw);
        const auto res = StateImport::loadKoala(buf);
        QVERIFY(res.isOk());
        QCOMPARE(res.value().backgroundColor, static_cast<uint8_t>(0x0BU));
    }

    // Oversized 10,004 and 65,536 bytes -> rejected cleanly
    for (const qsizetype tooLong : { static_cast<qsizetype>(10004), static_cast<qsizetype>(65536) }) {
        QByteArray raw(tooLong, '\x00');
        QBuffer buf(&raw);
        const auto res = StateImport::loadKoala(buf);
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, vchar64::ErrorCode::HeaderOutOfBounds);
    }
}

void StateImportTest::testViceSnapshotZeroModuleLengthInfiniteLoopRegression()
{
    auto buildSnapshotPrefix = []() {
        StateImport::VICESnapshotHeader hdr { };
        static constexpr char kMagic[19] = {
            'V', 'I', 'C', 'E', ' ', 'S', 'n', 'a', 'p', 's', 'h', 'o', 't', ' ', 'F', 'i', 'l', 'e', '\032'
        };
        std::memcpy(hdr.id, kMagic, sizeof(kMagic));
        hdr.major = 1U;
        hdr.minor = 1U;
        std::memcpy(hdr.machine, "C64", 4);

        StateImport::VICESnapshotVersion ver { };
        static constexpr char kVerMagic[13] = {
            'V', 'I', 'C', 'E', ' ', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\032'
        };
        std::memcpy(ver.id, kVerMagic, sizeof(kVerMagic));

        QByteArray out;
        out.append(reinterpret_cast<const char*>(&hdr), static_cast<qsizetype>(sizeof(hdr)));
        out.append(reinterpret_cast<const char*>(&ver), static_cast<qsizetype>(sizeof(ver)));
        return out;
    };

    auto ram64k = std::make_unique<std::array<uint8_t, 65536>>();
    std::array<uint8_t, 1024> colorRam { };
    std::array<uint8_t, 64> vicRegs { };
    uint16_t charsetAddr = 0;
    uint16_t screenAddr = 0;

    // 1. Module length = 0 (infinite-loop regression) and module length = 21 (< 22-byte header)
    for (const uint32_t badLen : { 0U, 21U }) {
        QByteArray snap = buildSnapshotPrefix();
        StateImport::VICESnapshotModule mod { };
        std::memcpy(mod.moduleName, "UNKNOWN", 8);
        mod.length = qToLittleEndian<quint32>(badLen);
        snap.append(reinterpret_cast<const char*>(&mod), static_cast<qsizetype>(sizeof(mod)));
        snap.append(64, '\x00');

        QBuffer buf(&snap);
        QElapsedTimer timer;
        timer.start();
        const auto res = StateImport::parseVICESnapshot(
            buf,
            ram64k->data(),
            &charsetAddr,
            &screenAddr,
            colorRam.data(),
            vicRegs.data());
        QVERIFY(timer.elapsed() < 200);
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, vchar64::ErrorCode::CorruptViceSnapshot);
    }

    // 2. Truncated C64MEM module (declares 22 + 32 bytes instead of 65,540 bytes)
    {
        QByteArray snap = buildSnapshotPrefix();

        auto appendDummyModule = [&](const char* name, size_t payloadBytes) {
            StateImport::VICESnapshotModule mod { };
            std::strncpy(mod.moduleName, name, sizeof(mod.moduleName) - 1U);
            mod.length = qToLittleEndian<quint32>(
                static_cast<quint32>(sizeof(mod) + payloadBytes));
            snap.append(reinterpret_cast<const char*>(&mod), static_cast<qsizetype>(sizeof(mod)));
            snap.append(static_cast<qsizetype>(payloadBytes), '\x00');
        };

        appendDummyModule("C64MEM", 32U); // Too small for VICESnapshotC64Mem!
        appendDummyModule("CIA2", sizeof(StateImport::VICESnapshotCIA2));
        appendDummyModule("VIC-II", sizeof(StateImport::VICESnapshotVICII));

        QBuffer buf(&snap);
        const auto res = StateImport::parseVICESnapshot(
            buf,
            ram64k->data(),
            &charsetAddr,
            &screenAddr,
            colorRam.data(),
            vicRegs.data());
        QVERIFY(!res.isOk());
        QCOMPARE(res.error().code, vchar64::ErrorCode::CorruptViceSnapshot);
    }
}

QTEST_GUILESS_MAIN(StateImportTest)
#include "tst_stateimport.moc"
