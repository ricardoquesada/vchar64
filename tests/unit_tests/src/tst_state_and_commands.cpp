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
 * @file tst_state_and_commands.cpp
 * @brief Headless `QTest` suite verifying `State`, `QUndoCommand` subclasses, and `vchar64_core` services.
 *
 * Covers Hi-Res and Multicolor pixel painting/merging, interleaved vs contiguous
 * multi-char tile transforms (clear, flip, shift, rotate, invert), cross-width
 * clipboard paste clipping, `1024x1024` iterative flood-fill stack safety and
 * bounded undo patches, Colodore palette masking and runtime `QT_TR_NOOP`
 * translation, `TileAtlasCache` dirty-bitset invalidation, `DocumentImageRenderer`
 * selective PNG composition, `ClipboardCodec` adversarial wire validation,
 * `ActiveDocumentRelay` signal isolation, and `FileUtils` path-prefix checks.
 */

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

#include "commands.h"
#include "core/charsetimagecache.h"
#include "core/clipboardcodec.h"
#include "core/documentimagerenderer.h"
#include "core/indexformat.h"
#include "core/tileatlascache.h"
#include "fileutils.h"
#include "palette.h"
#include "state.h"
#include "ui/activedocumentrelay.h"

namespace {

/**
 * @brief In-memory QTranslator fixture verifying runtime `QT_TR_NOOP` lookup in `Palette::colorName`.
 */
class StubSpanishTranslator final : public QTranslator {
public:
    [[nodiscard]] bool isEmpty() const override
    {
        return false;
    }

    [[nodiscard]] QString translate(
        const char* context,
        const char* sourceText,
        const char* disambiguation,
        int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (context != nullptr && sourceText != nullptr
            && std::strcmp(context, "Palette") == 0
            && std::strcmp(sourceText, "Black") == 0) {
            return QStringLiteral("Negro");
        }
        return { };
    }
};

[[nodiscard]] std::vector<uint8_t> buildCombinedCharsAndColorsPayload(
    const State::CharsetBuffer& charset,
    const State::TileColorsBuffer& colors)
{
    std::vector<uint8_t> combined(charset.size() + colors.size(), 0U);
    std::memcpy(combined.data(), charset.data(), charset.size());
    std::memcpy(combined.data() + charset.size(), colors.data(), colors.size());
    return combined;
}

} // namespace

class TestStateAndCommands : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testPaintTileCommandHiResAndMerge();
    void testPaintTileCommandMulticolorBitPairs();
    void testClearTileCommandInterleavedAndContiguous();
    void testPasteCommandUndoPreservesTileColorsAndSignals();
    void testPasteCommandCrossWidthMapStrideAndClipping();
    void testCutCommandCharsTilesAndMapUndoRedo();
    void testFlipTileHorizontallyHiResVsMulticolor();
    void testFlipTileVerticallySingleAndMultiChar();
    void testShiftTileLeftAndRightHiResAndMulticolor();
    void testShiftTileUpAndDownMultiChar();
    void testRotateTileSquareHiResUndoRedo();
    void testRotateTileDisabledOnNonSquareAndMulticolorNoOp();
    void testInvertTileCommand();
    void testPaintMapCommandMergeAndBoundedPatch();
    void testFloodFill1024x1024StackSafety();
    void testFloodFillPreservesObstacleWallsOnUndo();
    void testFloodFillNegativeOobAndSameTileNoOp();
    void testClearMapCommandUndoRedo();
    void testResizeMapCommandGrowShrinkAndBounds();
    void testSetTilePropertiesCommandUndoRedo();
    void testSetExportPropertiesCommandEquality();
    void testSetColorMulticolorAndForegroundModeCommands();
    void testPaletteColodoreOobMaskingAndRuntimeTr();
    void testTileAtlasCacheDirtyTrackingAndFullInvalidation();
    void testCharsetImageCacheRebuildTriggersAndRendererParity();
    void testFormatIndexDecHex();
    void testDocumentImageRendererAndExportPngSelectiveFlags();
    void testClipboardCodecRoundTripAndAdversarialRejection();
    void testActiveDocumentRelayIsolationAndLifecycle();
    void testFileUtilsPathBoundaryAndReplaceExtension();
    void testFileUtilsClassifyDroppedFiles();
    void testEcmPenBackground3AndStateLifecycle();
    void testSetCharacterModeAndMulticolorUndoFromEcm();
    void testEcmGlyphAliasingAndWriteTilePixelPenBgClearing();
    void testEcmBankWrap63To64AcrossCopyPasteClearRotateAndShift();
    void testEcmSelfAliasedTileGuardAndInvertDeduplication();
    void testTileAtlasCacheEcmCrossBankDirtyAndContextInvalidation();
    void testDocumentImageRendererAndCharsetCacheEcm();
    void testActiveDocumentRelayCharacterModeChanged();
};

void TestStateAndCommands::initTestCase()
{
    QVERIFY(QCoreApplication::instance() != nullptr);
    QCOMPARE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()), nullptr);

    State state;
    QCOMPARE(state.charset().size(), static_cast<size_t>(State::CHAR_BUFFER_SIZE));
    QCOMPARE(state.tileColors().size(), static_cast<size_t>(State::TILE_COLORS_BUFFER_SIZE));
    for (const uint8_t color : state.tileColors()) {
        QCOMPARE(color, static_cast<uint8_t>(State::TILE_COLORS_DEFAULT));
    }
    QVERIFY(state.getUndoStack() != nullptr);
    QCOMPARE(state.getUndoStack()->undoLimit(), State::kDefaultUndoLimit);
    QCOMPARE(State::kDefaultUndoLimit, 128);
    QVERIFY(state.getUndoStack()->isClean());
    QVERIFY(!state.isModified());
}

void TestStateAndCommands::testPaintTileCommandHiResAndMerge()
{
    State state;
    state.resetCharsetBuffer();
    state.clearUndoStack();
    const auto originalCharset = state.charset();

    // Two mergeable strokes with identical tileIndex & pen coalesce into 1 command.
    state.tilePaint(3, QPoint(0, 0), State::PEN_FOREGROUND, true);
    state.tilePaint(3, QPoint(7, 0), State::PEN_FOREGROUND, true);
    QCOMPARE(state.getUndoStack()->count(), 1);
    QCOMPARE(state.charset()[3U * 8U + 0U], static_cast<uint8_t>(0x81U));

    // Non-mergeable stroke with different pen pushes a second command.
    state.tilePaint(3, QPoint(0, 0), State::PEN_BACKGROUND, false);
    QCOMPARE(state.getUndoStack()->count(), 2);
    QCOMPARE(state.charset()[3U * 8U + 0U], static_cast<uint8_t>(0x01U));

    state.undo();
    QCOMPARE(state.charset()[3U * 8U + 0U], static_cast<uint8_t>(0x81U));

    state.undo();
    QCOMPARE(state.charset(), originalCharset);
    QVERIFY(state.getUndoStack()->isClean());
    QVERIFY(!state.isModified());
}

void TestStateAndCommands::testPaintTileCommandMulticolorBitPairs()
{
    State state;
    state.resetCharsetBuffer();
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    state.setMulticolorModeDirect(true);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
    state.setPenColorDirect(State::PEN_FOREGROUND, 10U); // >= 8 -> VIC-II 4x8 multicolor mode
    state.clearUndoStack();

    const auto originalCharset = state.charset();
    QVERIFY(state.shouldBeDisplayedInMulticolor2(1));

    // Paint top-left char of 2x2 tile 1 (char index 4), row 0, double-width pixel pairs x = 0, 2, 4, 6:
    // BG (00), FG (11), MC1 (01), MC2 (10) => 0b00'11'01'10 (0x36).
    state.tilePaint(1, QPoint(0, 0), State::PEN_BACKGROUND, false);
    state.tilePaint(1, QPoint(2, 0), State::PEN_FOREGROUND, false);
    state.tilePaint(1, QPoint(4, 0), State::PEN_MULTICOLOR1, false);
    state.tilePaint(1, QPoint(6, 0), State::PEN_MULTICOLOR2, false);
    // Also paint bottom-right quadrant of 2x2 tile 1 (x=10 -> right char col 1, y=10 -> bottom char row 2).
    state.tilePaint(1, QPoint(10, 10), State::PEN_FOREGROUND, false);

    const size_t topLeftCharBase = static_cast<size_t>(state.getCharIndexFromTileIndex(1)) * 8U;
    QCOMPARE(state.charset()[topLeftCharBase], static_cast<uint8_t>(0x36U));
    QCOMPARE(state.tileGetPen(1, QPoint(0, 0)), static_cast<int>(State::PEN_BACKGROUND));
    QCOMPARE(state.tileGetPen(1, QPoint(2, 0)), static_cast<int>(State::PEN_FOREGROUND));
    QCOMPARE(state.tileGetPen(1, QPoint(4, 0)), static_cast<int>(State::PEN_MULTICOLOR1));
    QCOMPARE(state.tileGetPen(1, QPoint(6, 0)), static_cast<int>(State::PEN_MULTICOLOR2));
    QCOMPARE(state.tileGetPen(1, QPoint(10, 10)), static_cast<int>(State::PEN_FOREGROUND));

    const auto paintedCharset = state.charset();
    while (state.getUndoStack()->canUndo()) {
        state.undo();
    }
    QCOMPARE(state.charset(), originalCharset);

    while (state.getUndoStack()->canRedo()) {
        state.redo();
    }
    QCOMPARE(state.charset(), paintedCharset);
}

void TestStateAndCommands::testClearTileCommandInterleavedAndContiguous()
{
    State::CharsetBuffer seeded { };
    for (size_t i = 0; i < seeded.size(); ++i) {
        seeded[i] = static_cast<uint8_t>((i & 0xFFU) ^ 0x5AU);
    }

    // (A) Contiguous 2x2 tiles (interleaved = 1): tile 2 owns chars 8, 9, 10, 11.
    {
        State state;
        state.setCharsetBytes(seeded.data(), seeded.size());
        QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
        state.clearUndoStack();

        state.tileClear(2);
        for (int c = 0; c < 256; ++c) {
            const bool owned = (c >= 8 && c <= 11);
            for (size_t row = 0; row < 8U; ++row) {
                const size_t idx = static_cast<size_t>(c) * 8U + row;
                QCOMPARE(state.charset()[idx], owned ? static_cast<uint8_t>(0U) : seeded[idx]);
            }
        }
        state.undo();
        QCOMPARE(state.charset(), seeded);
    }

    // (B) Interleaved 2x2 tiles (interleaved = 64): tile 2 owns chars 2, 66, 130, 194.
    {
        State state;
        state.setCharsetBytes(seeded.data(), seeded.size());
        QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 64 }));
        state.clearUndoStack();

        state.tileClear(2);
        for (int c = 0; c < 256; ++c) {
            const bool owned = (c == 2 || c == 66 || c == 130 || c == 194);
            for (size_t row = 0; row < 8U; ++row) {
                const size_t idx = static_cast<size_t>(c) * 8U + row;
                QCOMPARE(state.charset()[idx], owned ? static_cast<uint8_t>(0U) : seeded[idx]);
            }
        }
        state.undo();
        QCOMPARE(state.charset(), seeded);
    }
}

void TestStateAndCommands::testPasteCommandUndoPreservesTileColorsAndSignals()
{
    State state;
    state.resetCharsetBuffer();
    state.setMulticolorModeDirect(true);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    State::TileColorsBuffer initialColors { };
    for (size_t i = 0; i < initialColors.size(); ++i) {
        initialColors[i] = static_cast<uint8_t>((i % 15U) + 1U); // Strictly 1..15 (never 0 / Black!)
    }
    initialColors[5] = 6U; // < 8 -> Hi-Res on active tile 5
    state.setTileColorsDirect(initialColors.data(), initialColors.size());
    state.setTileIndex(5);
    state.clearUndoStack();

    const auto initialCharset = state.charset();
    QVERIFY(!state.shouldBeDisplayedInMulticolor2(5));

    State::CharsetBuffer srcCharset { };
    srcCharset.fill(0xAAU);
    State::TileColorsBuffer srcColors = initialColors;
    srcColors[0] = 14U; // >= 8 -> Multicolor when pasted onto tile 5!
    const auto payload = buildCombinedCharsAndColorsPayload(srcCharset, srcColors);

    State::CopyRange copyRange { };
    copyRange.offset = 0;
    copyRange.blockSize = 1;
    copyRange.skip = 0;
    copyRange.count = 1;
    copyRange.type = State::CopyRange::TILES;
    copyRange.tileProperties = { QSize(1, 1), 1 };
    copyRange.bufferSize = static_cast<int>(payload.size());

    QSignalSpy spyCharset(&state, &State::charsetUpdated);
    QSignalSpy spyColor(&state, &State::colorPropertiesUpdated);
    QSignalSpy spyMc(&state, &State::multicolorModeToggled);
    QSignalSpy spyContents(&state, &State::contentsChanged);

    state.paste(5, copyRange, payload.data());
    QCOMPARE(state.tileColors()[5], static_cast<uint8_t>(14U));
    QVERIFY(state.shouldBeDisplayedInMulticolor2(5));
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), true);

    spyCharset.clear();
    spyColor.clear();
    spyContents.clear();

    // Undo MUST restore all 256 tile colors bit-identically (fixing Defect C3 black wipeout!).
    state.undo();
    QCOMPARE(state.charset(), initialCharset);
    QCOMPARE(state.tileColors(), initialColors);
    for (const uint8_t c : state.tileColors()) {
        QVERIFY(c != 0U);
    }
    QVERIFY(spyCharset.count() >= 1);
    QVERIFY(spyColor.count() >= 1);
    QCOMPARE(spyColor.first().at(0).toInt(), static_cast<int>(State::PEN_FOREGROUND));
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), false);
    QVERIFY(spyContents.count() >= 1);

    // 10x undo/redo cycles preserve exact sizes and values without std::back_inserter growth.
    for (int cycle = 0; cycle < 10; ++cycle) {
        state.redo();
        QCOMPARE(state.tileColors()[5], static_cast<uint8_t>(14U));
        QCOMPARE(state.tileColors().size(), static_cast<size_t>(256));
        state.undo();
        QCOMPARE(state.tileColors(), initialColors);
        QCOMPARE(state.charset(), initialCharset);
    }
}

void TestStateAndCommands::testPasteCommandCrossWidthMapStrideAndClipping()
{
    // Source 10x10 map: 3x2 block at (1, 1) has cells {11,12,13} and {21,22,23}.
    std::vector<uint8_t> srcMap(100U, 0U);
    for (size_t i = 0; i < srcMap.size(); ++i) {
        srcMap[i] = static_cast<uint8_t>(i);
    }

    State::CopyRange range { };
    range.offset = 11; // (1, 1) in width 10
    range.blockSize = 3;
    range.skip = 7; // 10 - 3
    range.count = 2;
    range.type = State::CopyRange::MAP;
    range.bufferSize = static_cast<int>(srcMap.size());

    State dst;
    std::vector<uint8_t> initialDstMap(40U * 25U, 0xFEU);
    QVERIFY(dst.replaceMap(QSize(40, 25), initialDstMap));
    dst.clearUndoStack();

    // Paste at (5, 4) -> offset 4 * 40 + 5 = 165. Must use destination stride 40 (NOT source 10!).
    dst.paste(4 * 40 + 5, range, srcMap.data());
    QCOMPARE(dst.getTileIndexFromMap(QPoint(5, 4)), 11);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(6, 4)), 12);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(7, 4)), 13);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(5, 5)), 21);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(6, 5)), 22);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(7, 5)), 23);
    // Verify cell at offset (165 + 10 = 175 -> (15, 4)) was NOT touched!
    QCOMPARE(dst.getTileIndexFromMap(QPoint(15, 4)), 0xFE);

    dst.undo();
    QCOMPARE(dst.map(), initialDstMap);

    // Paste at bottom-right corner (39, 24) -> offset 24 * 40 + 39 = 999: clips cleanly to 1x1 cell {11}.
    dst.paste(24 * 40 + 39, range, srcMap.data());
    QCOMPARE(dst.map().size(), static_cast<size_t>(40U * 25U));
    QCOMPARE(dst.getTileIndexFromMap(QPoint(39, 24)), 11);
    QCOMPARE(dst.getTileIndexFromMap(QPoint(38, 24)), 0xFE);

    dst.undo();
    QCOMPARE(dst.map(), initialDstMap);
}

void TestStateAndCommands::testCutCommandCharsTilesAndMapUndoRedo()
{
    State state;
    State::CharsetBuffer seededCharset { };
    for (size_t i = 0; i < seededCharset.size(); ++i) {
        seededCharset[i] = static_cast<uint8_t>((i % 251U) + 1U);
    }
    State::TileColorsBuffer seededColors { };
    for (size_t i = 0; i < seededColors.size(); ++i) {
        seededColors[i] = static_cast<uint8_t>((i % 15U) + 1U); // No 0s!
    }
    std::vector<uint8_t> seededMap(16U * 16U, 0U);
    for (size_t i = 0; i < seededMap.size(); ++i) {
        seededMap[i] = static_cast<uint8_t>((i % 200U) + 5U);
    }

    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    QVERIFY(state.assignImportedBuffers(seededCharset, seededColors, QSize(16, 16), seededMap));
    state.clearUndoStack();

    // 1. Cut CHARS (chars 2..3)
    State::CopyRange charCut { };
    charCut.offset = 2;
    charCut.blockSize = 2;
    charCut.skip = 0;
    charCut.count = 1;
    charCut.type = State::CopyRange::CHARS;
    charCut.tileProperties = state.tileProperties();
    state.cut(charCut);
    for (size_t b = 16U; b < 32U; ++b) {
        QCOMPARE(state.charset()[b], static_cast<uint8_t>(0U));
    }
    state.undo();
    QCOMPARE(state.charset(), seededCharset);
    QCOMPARE(state.tileColors(), seededColors);

    // 2. Cut TILES (2x2 tile index 3 -> chars 12..15)
    State::CopyRange tileCut { };
    tileCut.offset = 3;
    tileCut.blockSize = 1;
    tileCut.skip = 0;
    tileCut.count = 1;
    tileCut.type = State::CopyRange::TILES;
    tileCut.tileProperties = state.tileProperties();
    state.cut(tileCut);
    for (size_t b = 12U * 8U; b < 16U * 8U; ++b) {
        QCOMPARE(state.charset()[b], static_cast<uint8_t>(0U));
    }
    const auto afterTileCutCharset = state.charset();
    state.undo();
    QCOMPARE(state.charset(), seededCharset);
    QCOMPARE(state.tileColors(), seededColors);
    state.redo();
    QCOMPARE(state.charset(), afterTileCutCharset);
    state.undo();

    // 3. Cut MAP (4x3 rectangle starting at (3, 2))
    State::CopyRange mapCut { };
    mapCut.offset = 2 * 16 + 3;
    mapCut.blockSize = 4;
    mapCut.skip = 16 - 4;
    mapCut.count = 3;
    mapCut.type = State::CopyRange::MAP;
    state.cut(mapCut);
    for (int y = 2; y < 5; ++y) {
        for (int x = 3; x < 7; ++x) {
            QCOMPARE(state.getTileIndexFromMap(QPoint(x, y)), 0);
        }
    }
    const auto afterMapCut = state.map();
    state.undo();
    QCOMPARE(state.map(), seededMap);
    state.redo();
    QCOMPARE(state.map(), afterMapCut);
}

void TestStateAndCommands::testFlipTileHorizontallyHiResVsMulticolor()
{
    constexpr uint8_t kSeed = 0x1BU; // 0b00'01'10'11 -> 1-bit rev: 0xD8 (11011000); 2-bit rev: 0xE4 (11 10 01 00)

    // (A) Hi-Res (_multicolorMode = false, pen = 10)
    {
        State state;
        state.resetCharsetBuffer();
        State::CharsetBuffer buf = state.charset();
        buf[0] = kSeed;
        state.setCharsetBytes(buf.data(), buf.size());
        state.setMulticolorModeDirect(false);
        state.setPenColorDirect(State::PEN_FOREGROUND, 10U);
        state.clearUndoStack();

        QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));
        state.tileFlipHorizontally(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xD8U));
        state.undo();
        QCOMPARE(state.charset()[0], kSeed);
    }

    // (B) Multicolor enabled + FOREGROUND_COLOR_GLOBAL with PEN_FOREGROUND = 7 (< 8 -> VIC-II Hi-Res!)
    {
        State state;
        state.resetCharsetBuffer();
        State::CharsetBuffer buf = state.charset();
        buf[0] = kSeed;
        state.setCharsetBytes(buf.data(), buf.size());
        state.setMulticolorModeDirect(true);
        state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
        state.setPenColorDirect(State::PEN_FOREGROUND, 7U);
        state.clearUndoStack();

        QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));
        state.tileFlipHorizontally(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xD8U));
        state.undo();
        QCOMPARE(state.charset()[0], kSeed);
    }

    // (C) Multicolor enabled + effective pen = 10 (>= 8 -> VIC-II Multicolor!) on 1x1 and 2x2 tiles
    {
        State state;
        state.resetCharsetBuffer();
        State::CharsetBuffer buf = state.charset();
        buf[0] = kSeed;
        state.setCharsetBytes(buf.data(), buf.size());
        state.setMulticolorModeDirect(true);
        state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
        state.setPenColorDirect(State::PEN_FOREGROUND, 10U);
        state.clearUndoStack();

        QVERIFY(state.shouldBeDisplayedInMulticolor2(0));
        state.tileFlipHorizontally(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xE4U));
        state.undo();
        QCOMPARE(state.charset()[0], kSeed);

        // 2x2 multicolor tile swaps left/right character columns AND reverses 2-bit pairs.
        QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
        buf.fill(0U);
        buf[0] = kSeed; // top-left char row 0
        buf[8] = 0x30U; // top-right char row 0 (0b00'11'00'00 -> 2-bit rev 0b00'00'11'00 = 0x0C)
        state.setCharsetBytes(buf.data(), buf.size());
        state.clearUndoStack();

        state.tileFlipHorizontally(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0x0CU));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0xE4U));
        state.undo();
        QCOMPARE(state.charset()[0], kSeed);
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x30U));
    }
}

void TestStateAndCommands::testFlipTileVerticallySingleAndMultiChar()
{
    State state;
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 3), 1 }));
    State::CharsetBuffer buf { };
    // 2x3 tile 0 spans 6 chars: (x=0,y=0)->0, (x=1,y=0)->1, (x=0,y=1)->2, (x=1,y=1)->3, (x=0,y=2)->4, (x=1,y=2)->5
    for (int yChar = 0; yChar < 3; ++yChar) {
        for (int row = 0; row < 8; ++row) {
            const int scanline = yChar * 8 + row; // 0..23
            buf[static_cast<size_t>((yChar * 2 + 0) * 8 + row)] = static_cast<uint8_t>(scanline + 1);
            buf[static_cast<size_t>((yChar * 2 + 1) * 8 + row)] = static_cast<uint8_t>(101 + scanline);
        }
    }
    state.setCharsetBytes(buf.data(), buf.size());
    state.clearUndoStack();

    state.tileFlipVertically(0);
    for (int yChar = 0; yChar < 3; ++yChar) {
        for (int row = 0; row < 8; ++row) {
            const int scanline = yChar * 8 + row;
            const int expectedSrc = 23 - scanline;
            QCOMPARE(
                state.charset()[static_cast<size_t>((yChar * 2 + 0) * 8 + row)],
                static_cast<uint8_t>(expectedSrc + 1));
            QCOMPARE(
                state.charset()[static_cast<size_t>((yChar * 2 + 1) * 8 + row)],
                static_cast<uint8_t>(101 + expectedSrc));
        }
    }

    state.undo();
    QCOMPARE(state.charset(), buf);
}

void TestStateAndCommands::testShiftTileLeftAndRightHiResAndMulticolor()
{
    // (A) Hi-Res 2x1 tile: row 0 = [0x80, 0x01]
    {
        State state;
        QVERIFY(state.setTilePropertiesDirect({ QSize(2, 1), 1 }));
        state.setMulticolorModeDirect(false);
        State::CharsetBuffer buf { };
        buf[0] = 0x80U;
        buf[8] = 0x01U;
        state.setCharsetBytes(buf.data(), buf.size());
        state.clearUndoStack();

        state.tileShiftLeft(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0x00U));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x03U));

        state.undo();
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0x80U));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x01U));

        state.tileShiftRight(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xC0U));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x00U));

        state.undo();
        QCOMPARE(state.charset(), buf);
    }

    // (B) Multicolor 2x1 tile (pen >= 8): row 0 = [0xC0, 0x1B] (11 00 00 00 | 00 01 10 11)
    {
        State state;
        QVERIFY(state.setTilePropertiesDirect({ QSize(2, 1), 1 }));
        state.setMulticolorModeDirect(true);
        state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
        state.setPenColorDirect(State::PEN_FOREGROUND, 12U);
        State::CharsetBuffer buf { };
        buf[0] = 0xC0U;
        buf[8] = 0x1BU;
        state.setCharsetBytes(buf.data(), buf.size());
        state.clearUndoStack();

        // ShiftLeft wraps 2-bit pixel pair: [00 00 00 00 | 01 10 11 11] => [0x00, 0x6F]
        state.tileShiftLeft(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0x00U));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x6FU));

        state.undo();
        QCOMPARE(state.charset(), buf);

        // ShiftRight wraps rightmost 2-bit pair (11) into leftmost bits of char 0: [11 11 00 00 | 00 00 01 10] => [0xF0, 0x06]
        state.tileShiftRight(0);
        QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xF0U));
        QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x06U));

        state.undo();
        QCOMPARE(state.charset(), buf);
    }
}

void TestStateAndCommands::testShiftTileUpAndDownMultiChar()
{
    State state;
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    State::CharsetBuffer buf { };
    for (int scanline = 0; scanline < 16; ++scanline) {
        const int yChar = scanline / 8;
        const int row = scanline % 8;
        buf[static_cast<size_t>((yChar * 2 + 0) * 8 + row)] = static_cast<uint8_t>(0x10 + scanline);
        buf[static_cast<size_t>((yChar * 2 + 1) * 8 + row)] = static_cast<uint8_t>(0x20 + scanline);
    }
    state.setCharsetBytes(buf.data(), buf.size());
    state.clearUndoStack();

    state.tileShiftUp(0);
    for (int scanline = 0; scanline < 16; ++scanline) {
        const int yChar = scanline / 8;
        const int row = scanline % 8;
        const int srcScan = (scanline + 1) % 16;
        QCOMPARE(
            state.charset()[static_cast<size_t>((yChar * 2 + 0) * 8 + row)],
            static_cast<uint8_t>(0x10 + srcScan));
        QCOMPARE(
            state.charset()[static_cast<size_t>((yChar * 2 + 1) * 8 + row)],
            static_cast<uint8_t>(0x20 + srcScan));
    }

    state.undo();
    QCOMPARE(state.charset(), buf);

    state.tileShiftDown(0);
    for (int scanline = 0; scanline < 16; ++scanline) {
        const int yChar = scanline / 8;
        const int row = scanline % 8;
        const int srcScan = (scanline + 15) % 16;
        QCOMPARE(
            state.charset()[static_cast<size_t>((yChar * 2 + 0) * 8 + row)],
            static_cast<uint8_t>(0x10 + srcScan));
        QCOMPARE(
            state.charset()[static_cast<size_t>((yChar * 2 + 1) * 8 + row)],
            static_cast<uint8_t>(0x20 + srcScan));
    }

    state.undo();
    QCOMPARE(state.charset(), buf);
}

void TestStateAndCommands::testRotateTileSquareHiResUndoRedo()
{
    // Test 2x2 square tile under _multicolorMode = true with effective pen = 5 (< 8 -> VIC-II Hi-Res!).
    State state;
    state.resetCharsetBuffer();
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    state.setMulticolorModeDirect(true);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
    state.setPenColorDirect(State::PEN_FOREGROUND, 5U);
    state.clearUndoStack();

    QVERIFY(state.canRotateTile(0));

    // Paint an asymmetric L-shape at (0, 0), (0, 1), (0, 2), (1, 2) inside 16x16 pixel space.
    state.tilePaint(0, QPoint(0, 0), State::PEN_FOREGROUND, false);
    state.tilePaint(0, QPoint(0, 1), State::PEN_FOREGROUND, false);
    state.tilePaint(0, QPoint(0, 2), State::PEN_FOREGROUND, false);
    state.tilePaint(0, QPoint(1, 2), State::PEN_FOREGROUND, false);
    const auto beforeRotate = state.charset();

    // 90-degree clockwise maps (x, y) -> (15 - y, x):
    // (0,0)->(15,0), (0,1)->(14,0), (0,2)->(13,0), (1,2)->(13,1).
    // Note: in Hi-Res mode, State::tileGetPen returns raw 1-bit pixel value (1 = set, 0 = background).
    state.tileRotate(0);
    QCOMPARE(state.tileGetPen(0, QPoint(15, 0)), 1);
    QCOMPARE(state.tileGetPen(0, QPoint(14, 0)), 1);
    QCOMPARE(state.tileGetPen(0, QPoint(13, 0)), 1);
    QCOMPARE(state.tileGetPen(0, QPoint(13, 1)), 1);
    QCOMPARE(state.tileGetPen(0, QPoint(0, 0)), 0);

    // Direct buffer-snapshot undo restores pre-rotation glyph immediately.
    state.undo();
    QCOMPARE(state.charset(), beforeRotate);

    // 4x clockwise rotations yield the exact original pattern.
    state.redo();
    state.tileRotate(0);
    state.tileRotate(0);
    state.tileRotate(0);
    QCOMPARE(state.charset(), beforeRotate);
}

void TestStateAndCommands::testRotateTileDisabledOnNonSquareAndMulticolorNoOp()
{
    State::CharsetBuffer seeded { };
    for (size_t i = 0; i < seeded.size(); ++i) {
        seeded[i] = static_cast<uint8_t>((i * 37U + 13U) & 0xFFU);
    }

    auto assertNoOpRotate = [&](State& st, int tileIdx) {
        const auto expected = st.charset();
        QSignalSpy spyTile(&st, &State::tileUpdated);
        QSignalSpy spyContents(&st, &State::contentsChanged);

        QVERIFY(!st.canRotateTile(tileIdx));
        st.tileRotate(tileIdx);

        QCOMPARE(st.charset(), expected);
        QCOMPARE(st.getUndoStack()->count(), 0);
        QVERIFY(st.getUndoStack()->isClean());
        QVERIFY(!st.isModified());
        QCOMPARE(spyTile.count(), 0);
        QCOMPARE(spyContents.count(), 0);
    };

    // (1) Non-square Hi-Res tiles: 2x1, 1x2, 2x3
    for (const QSize nonSquare : { QSize(2, 1), QSize(1, 2), QSize(2, 3) }) {
        State state;
        state.setCharsetBytes(seeded.data(), seeded.size());
        QVERIFY(state.setTilePropertiesDirect({ nonSquare, 1 }));
        state.setMulticolorModeDirect(false);
        state.clearUndoStack();
        assertNoOpRotate(state, 0);
    }

    // (2) Square 1x1 & 2x2 Multicolor tiles (effective pen 8..15) in GLOBAL and PER_TILE modes
    for (const QSize sq : { QSize(1, 1), QSize(2, 2) }) {
        State stateGlobal;
        stateGlobal.setCharsetBytes(seeded.data(), seeded.size());
        QVERIFY(stateGlobal.setTilePropertiesDirect({ sq, 1 }));
        stateGlobal.setMulticolorModeDirect(true);
        stateGlobal.setForegroundColorModeDirect(State::FOREGROUND_COLOR_GLOBAL);
        stateGlobal.setPenColorDirect(State::PEN_FOREGROUND, 8U);
        stateGlobal.clearUndoStack();
        assertNoOpRotate(stateGlobal, 0);

        State statePerTile;
        statePerTile.setCharsetBytes(seeded.data(), seeded.size());
        QVERIFY(statePerTile.setTilePropertiesDirect({ sq, 1 }));
        statePerTile.setMulticolorModeDirect(true);
        statePerTile.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);
        statePerTile.setPenColorDirect(State::PEN_FOREGROUND, 2U); // Global < 8, per-tile >= 8
        statePerTile.setTileColorDirect(0, 15U);
        statePerTile.clearUndoStack();
        assertNoOpRotate(statePerTile, 0);
    }
}

void TestStateAndCommands::testInvertTileCommand()
{
    State state;
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    State::CharsetBuffer seeded { };
    const std::array<uint8_t, 4> kPattern = { 0x00U, 0xFFU, 0xAAU, 0x55U };
    for (size_t i = 0; i < 32U; ++i) {
        seeded[i] = kPattern[i % kPattern.size()];
    }
    state.setCharsetBytes(seeded.data(), seeded.size());
    state.clearUndoStack();

    QSignalSpy spyTile(&state, &State::tileUpdated);
    state.tileInvert(0);
    QCOMPARE(spyTile.count(), 1);
    for (size_t i = 0; i < 32U; ++i) {
        QCOMPARE(state.charset()[i], static_cast<uint8_t>(~seeded[i]));
    }

    state.undo();
    QCOMPARE(state.charset(), seeded);
}

void TestStateAndCommands::testPaintMapCommandMergeAndBoundedPatch()
{
    State state;
    std::vector<uint8_t> zeroMap(128U * 128U, 0x00U);
    QVERIFY(state.replaceMap(QSize(128, 128), zeroMap));
    state.clearUndoStack();

    // Stroke (10,10) -> (11,10) -> (12,10) -> re-paint (10,10) within the same merged command.
    state.mapPaint(QPoint(10, 10), 0x42, true);
    state.mapPaint(QPoint(11, 10), 0x42, true);
    state.mapPaint(QPoint(12, 10), 0x42, true);
    state.mapPaint(QPoint(10, 10), 0x42, true);
    QCOMPARE(state.getUndoStack()->count(), 1);

    // Out-of-bounds paint is safely ignored without pushing commands or crashing.
    state.mapPaint(QPoint(-1, 0), 0x99, true);
    state.mapPaint(QPoint(128, 128), 0x99, false);
    QCOMPARE(state.getUndoStack()->count(), 1);

    QCOMPARE(state.getTileIndexFromMap(QPoint(10, 10)), 0x42);
    QCOMPARE(state.getTileIndexFromMap(QPoint(11, 10)), 0x42);
    QCOMPARE(state.getTileIndexFromMap(QPoint(12, 10)), 0x42);

    // Undo MUST restore (10,10)..(12,10) back to 0x00 (never overwritten by intermediate stroke!).
    state.undo();
    QCOMPARE(state.map(), zeroMap);
}

void TestStateAndCommands::testFloodFill1024x1024StackSafety()
{
    State state;
    constexpr size_t kTotalCells = 1024U * 1024U;
    QVERIFY(state.replaceMap(QSize(1024, 1024), std::vector<uint8_t>(kTotalCells, 0x00U)));
    state.clearUndoStack();

    QElapsedTimer timer;
    timer.start();
    state.mapFill(QPoint(512, 512), 0x07);
    const qint64 elapsedMs = timer.elapsed();
    // Under Debug + ASan/UBSan allow generous headroom, while guaranteeing iterative completion.
    QVERIFY2(elapsedMs < 5000, qPrintable(QStringLiteral("Flood fill took %1 ms").arg(elapsedMs)));

    for (const uint8_t cell : state.map()) {
        if (cell != 0x07U) {
            QFAIL("Expected all 1,048,576 cells to equal 0x07 after flood fill");
        }
    }

    state.undo();
    for (const uint8_t cell : state.map()) {
        if (cell != 0x00U) {
            QFAIL("Expected all 1,048,576 cells to restore to 0x00 after undo");
        }
    }
}

void TestStateAndCommands::testFloodFillPreservesObstacleWallsOnUndo()
{
    // 10x10 map filled with 0x01; 3x3 ring of 0x09 at (4..6, 4..6) enclosing pocket (5, 5) == 0x01.
    std::vector<uint8_t> initialMap(100U, 0x01U);
    for (int y = 4; y <= 6; ++y) {
        for (int x = 4; x <= 6; ++x) {
            initialMap[static_cast<size_t>(y * 10 + x)] = 0x09U;
        }
    }
    initialMap[static_cast<size_t>(5 * 10 + 5)] = 0x01U;

    State state;
    QVERIFY(state.replaceMap(QSize(10, 10), initialMap));
    state.clearUndoStack();

    // Flood-fill exterior (0, 0) with 0x09 (same value as the pre-existing obstacle ring!).
    state.mapFill(QPoint(0, 0), 0x09);

    QCOMPARE(state.getTileIndexFromMap(QPoint(0, 0)), 0x09);
    QCOMPARE(state.getTileIndexFromMap(QPoint(9, 9)), 0x09);
    // Enclosed pocket at (5, 5) must NOT be reached by 4-way flood fill.
    QCOMPARE(state.getTileIndexFromMap(QPoint(5, 5)), 0x01);

    // Undo MUST preserve the pre-existing 0x09 obstacle ring at (4..6, 4..6) (except (5,5) == 0x01)!
    state.undo();
    QCOMPARE(state.map(), initialMap);
    QCOMPARE(state.getTileIndexFromMap(QPoint(4, 4)), 0x09);
    QCOMPARE(state.getTileIndexFromMap(QPoint(6, 6)), 0x09);
    QCOMPARE(state.getTileIndexFromMap(QPoint(5, 5)), 0x01);
    QCOMPARE(state.getTileIndexFromMap(QPoint(0, 0)), 0x01);
}

void TestStateAndCommands::testFloodFillNegativeOobAndSameTileNoOp()
{
    State state;
    const std::vector<uint8_t> initialMap(64U, 0x03U);
    QVERIFY(state.replaceMap(QSize(8, 8), initialMap));
    state.clearUndoStack();

    const std::array<QPoint, 5> oobCoords = {
        QPoint(-1, 0),
        QPoint(0, -1),
        QPoint(8, 0),
        QPoint(0, 8),
        QPoint(INT_MIN, INT_MAX),
    };

    for (const QPoint pt : oobCoords) {
        const auto plan = state.collectFloodFillRegion(pt, 0x03U, 0x05U);
        QVERIFY(plan.coords.empty());
        state.mapFill(pt, 0x05);
    }

    // Same-tile replacement (target == replacement == 0x03) is a clean no-op.
    const auto samePlan = state.collectFloodFillRegion(QPoint(2, 2), 0x03U, 0x03U);
    QVERIFY(samePlan.coords.empty());
    state.mapFill(QPoint(2, 2), 0x03);

    QCOMPARE(state.getUndoStack()->count(), 0);
    QCOMPARE(state.map(), initialMap);
}

void TestStateAndCommands::testClearMapCommandUndoRedo()
{
    State state;
    std::vector<uint8_t> patterned(20U * 15U, 0U);
    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 20; ++x) {
            patterned[static_cast<size_t>(y * 20 + x)] = static_cast<uint8_t>((x + y * 20) & 0xFF);
        }
    }
    QVERIFY(state.replaceMap(QSize(20, 15), patterned));
    state.clearUndoStack();

    state.mapClear(0x20);
    for (const uint8_t cell : state.map()) {
        QCOMPARE(cell, static_cast<uint8_t>(0x20U));
    }

    state.undo();
    QCOMPARE(state.map(), patterned);
}

void TestStateAndCommands::testResizeMapCommandGrowShrinkAndBounds()
{
    const std::vector<uint8_t> initial = {
        1U, 2U, 3U, 4U,
        5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U
    };

    State state;
    QVERIFY(state.replaceMap(QSize(4, 3), initial));
    state.clearUndoStack();

    // (A) Grow to 6x5: preserves 4x3 top-left subgrid, zero-pads new columns & rows.
    state.setMapSize(QSize(6, 5));
    QCOMPARE(state.mapSize(), QSize(6, 5));
    QCOMPARE(state.map().size(), static_cast<size_t>(30));
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 6; ++x) {
            const int expected = (x < 4 && y < 3) ? static_cast<int>(initial[static_cast<size_t>(y * 4 + x)]) : 0;
            QCOMPARE(state.getTileIndexFromMap(QPoint(x, y)), expected);
        }
    }
    state.undo();
    QCOMPARE(state.mapSize(), QSize(4, 3));
    QCOMPARE(state.map(), initial);

    // (B) Shrink to 2x2: crops to {{1, 2}, {5, 6}}; undo restores full 4x3!
    state.setMapSize(QSize(2, 2));
    QCOMPARE(state.mapSize(), QSize(2, 2));
    const std::vector<uint8_t> expectedCropped = { 1U, 2U, 5U, 6U };
    QCOMPARE(state.map(), expectedCropped);

    state.undo();
    QCOMPARE(state.mapSize(), QSize(4, 3));
    QCOMPARE(state.map(), initial);

    // (C) Invalid dimensions rejected cleanly preserving map.size() == w * h invariant.
    state.clearUndoStack();
    for (const QSize invalid : { QSize(0, 10), QSize(-5, 5), QSize(4097, 1), QSize(1, 5000) }) {
        state.setMapSize(invalid);
        QVERIFY(!state.replaceMap(invalid, { }));
        QCOMPARE(state.mapSize(), QSize(4, 3));
        QCOMPARE(state.map(), initial);
        QCOMPARE(state.getUndoStack()->count(), 0);
    }
}

void TestStateAndCommands::testSetTilePropertiesCommandUndoRedo()
{
    State state;
    state.setTileIndex(200);
    QCOMPARE(state.getTileIndex(), 200);
    state.clearUndoStack();

    QSignalSpy spyProps(&state, &State::tilePropertiesUpdated);
    state.setTileProperties({ QSize(2, 2), 64 });

    QCOMPARE(spyProps.count(), 1);
    QCOMPARE(state.tileProperties().size, QSize(2, 2));
    QCOMPARE(state.tileProperties().interleaved, 64);
    QCOMPARE(state.maxTiles(), 64);
    QVERIFY(state.getTileIndex() >= 0 && state.getTileIndex() < 64);
    QCOMPARE(state.getCharIndexFromTileIndex(1), 1);

    state.undo();
    QCOMPARE(state.tileProperties().size, QSize(1, 1));
    QCOMPARE(state.tileProperties().interleaved, 1);
    QCOMPARE(state.maxTiles(), 256);
}

void TestStateAndCommands::testSetExportPropertiesCommandEquality()
{
    alignas(State::ExportProperties) std::array<uint8_t, sizeof(State::ExportProperties)> rawA { };
    alignas(State::ExportProperties) std::array<uint8_t, sizeof(State::ExportProperties)> rawB { };
    rawA.fill(0xAAU);
    rawB.fill(0x55U);

    auto* propsA = new (rawA.data()) State::ExportProperties();
    auto* propsB = new (rawB.data()) State::ExportProperties();

    propsA->addresses[0] = 0x2000U;
    propsA->addresses[1] = 0x2800U;
    propsA->addresses[2] = 0xD800U;
    propsA->format = State::EXPORT_FORMAT_ASM;
    propsA->features = State::EXPORT_FEATURE_ALL;

    propsB->addresses[0] = 0x2000U;
    propsB->addresses[1] = 0x2800U;
    propsB->addresses[2] = 0xD800U;
    propsB->format = State::EXPORT_FORMAT_ASM;
    propsB->features = State::EXPORT_FEATURE_ALL;

    QVERIFY(*propsA == *propsB);
    QVERIFY(!(*propsA != *propsB));

    State state;
    state.setExportPropertiesDirect(*propsA);
    state.clearUndoStack();

    // Identical ExportProperties is a no-op (zero undo command pushed).
    state.setExportProperties(*propsB);
    QCOMPARE(state.getUndoStack()->count(), 0);

    // Changing format pushes SetExportPropertiesCommand and round-trips undo/redo.
    propsB->format = State::EXPORT_FORMAT_C;
    state.setExportProperties(*propsB);
    QCOMPARE(state.getUndoStack()->count(), 1);
    QCOMPARE(state.exportProperties().format, State::EXPORT_FORMAT_C);

    state.undo();
    QCOMPARE(state.exportProperties().format, State::EXPORT_FORMAT_ASM);
    state.redo();
    QCOMPARE(state.exportProperties().format, State::EXPORT_FORMAT_C);
}

void TestStateAndCommands::testSetColorMulticolorAndForegroundModeCommands()
{
    State state;
    state.setTileIndex(0);
    state.setTileColorDirect(0, 12U); // >= 8
    state.setPenColorDirect(State::PEN_FOREGROUND, 3U); // < 8
    state.clearUndoStack();

    QSignalSpy spyColor(&state, &State::colorPropertiesUpdated);
    QSignalSpy spyMc(&state, &State::multicolorModeToggled);
    QSignalSpy spyPen(&state, &State::selectedPenChanged);

    state.setSelectedPen(State::PEN_MULTICOLOR2);
    QCOMPARE(state.getSelectedPen(), static_cast<int>(State::PEN_MULTICOLOR2));
    QCOMPARE(spyPen.count(), 1);
    QCOMPARE(spyPen.takeFirst().at(0).toInt(), static_cast<int>(State::PEN_MULTICOLOR2));

    // Masking & 0x0F across pens 0..3: 0x1F -> 0x0F (15 >= 8).
    state.setColorForPen(State::PEN_BACKGROUND, 0x16);
    QCOMPARE(state.getColorForPen(State::PEN_BACKGROUND), 6);
    QVERIFY(spyColor.count() >= 1);

    // Enable multicolor while GLOBAL FG is 3 (< 8): shouldBeDisplayedInMulticolor2(0) remains false.
    state.setMulticolorMode(true);
    QVERIFY(state.isMulticolorMode());
    QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));

    // Switch GLOBAL -> PER_TILE where tile 0 color is 12 (>= 8): crosses into multicolor display!
    spyMc.clear();
    state.setForegroundColorMode(State::FOREGROUND_COLOR_PER_TILE);
    QVERIFY(state.shouldBeDisplayedInMulticolor2(0));
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), true);

    // Set tile 0 color from 12 -> 0x15 (& 0x0F == 5 < 8): crosses back to Hi-Res display!
    state.setColorForPen(State::PEN_FOREGROUND, 0x15, 0);
    QCOMPARE(state.getColorForPen(State::PEN_FOREGROUND, 0), 5);
    QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), false);

    // Undo back through every command cleanly.
    state.undo();
    QCOMPARE(state.getColorForPen(State::PEN_FOREGROUND, 0), 12);
    QVERIFY(state.shouldBeDisplayedInMulticolor2(0));

    state.undo();
    QCOMPARE(state.getForegroundColorMode(), State::FOREGROUND_COLOR_GLOBAL);
    QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));

    state.undo();
    QVERIFY(!state.isMulticolorMode());

    state.undo();
    QCOMPARE(state.getColorForPen(State::PEN_BACKGROUND), 1);
}

void TestStateAndCommands::testPaletteColodoreOobMaskingAndRuntimeTr()
{
    const int savedPalette = Palette::activePalette();

    QCOMPARE(static_cast<int>(Palette::COLODORE), 5);
    Palette::setActivePalette(Palette::COLODORE);
    QCOMPARE(Palette::activePalette(), static_cast<int>(Palette::COLODORE));
    QCOMPARE(Palette::getColor(1), QColor(0xFE, 0xFE, 0xFE));

    // Out-of-bounds color indices mask via & 0x0F without crashing.
    QCOMPARE(Palette::getColor(-1), Palette::getColor(15));
    QCOMPARE(Palette::getColor(16), Palette::getColor(0));
    QCOMPARE(Palette::getColor(255), Palette::getColor(15));

    // Out-of-bounds palette indices clamp to [0, 5].
    Palette::setActivePalette(-3);
    QCOMPARE(Palette::activePalette(), static_cast<int>(Palette::PEPTO));
    Palette::setActivePalette(99);
    QCOMPARE(Palette::activePalette(), static_cast<int>(Palette::COLODORE));

    // Runtime QT_TR_NOOP translation verification via installed QTranslator.
    QCOMPARE(Palette::colorName(0), QStringLiteral("Black"));
    QCOMPARE(Palette::colorName(16), QStringLiteral("Black"));

    StubSpanishTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QCOMPARE(Palette::colorName(0), QStringLiteral("Negro"));
    QCOMPARE(Palette::colorName(16), QStringLiteral("Negro"));

    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QCOMPARE(Palette::colorName(0), QStringLiteral("Black"));

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testTileAtlasCacheDirtyTrackingAndFullInvalidation()
{
    const int savedPalette = Palette::activePalette();
    Palette::setActivePalette(Palette::PEPTO);

    State stateA;
    stateA.resetCharsetBuffer();
    stateA.setPenColorDirect(State::PEN_BACKGROUND, 0U); // Black
    stateA.setPenColorDirect(State::PEN_FOREGROUND, 1U); // White (#FFFFFF in Pepto)

    State stateB;
    stateB.resetCharsetBuffer();
    stateB.setPenColorDirect(State::PEN_BACKGROUND, 0U);
    stateB.setPenColorDirect(State::PEN_FOREGROUND, 1U);
    State::CharsetBuffer bufB = stateB.charset();
    bufB[0] = 0xFFU; // Tile 0 top row all white in stateB
    stateB.setCharsetBytes(bufB.data(), bufB.size());

    TileAtlasCache cache;
    cache.sync(stateA);
    QCOMPARE(cache.atlasImage().format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(cache.lastSyncedTileCount(), 256);

    const QRect tile0Rect = cache.sourceRectForTile(0);
    const QRect tile5Rect = cache.sourceRectForTile(5);
    QCOMPARE(cache.atlasImage().pixelColor(tile0Rect.topLeft()), QColor(0x00, 0x00, 0x00));
    QCOMPARE(cache.atlasImage().pixelColor(tile5Rect.topLeft()), QColor(0x00, 0x00, 0x00));

    // (1) Paint tile 5 on stateA and mark ONLY tile 5 dirty -> incremental 1-tile sync.
    stateA.tilePaint(5, QPoint(0, 0), State::PEN_FOREGROUND, false);
    cache.markTileDirty(5);
    cache.sync(stateA);
    QCOMPARE(cache.lastSyncedTileCount(), 1);
    QCOMPARE(cache.atlasImage().pixelColor(tile5Rect.topLeft()), QColor(0xFF, 0xFF, 0xFF));
    QCOMPARE(cache.atlasImage().pixelColor(tile0Rect.topLeft()), QColor(0x00, 0x00, 0x00));

    // (2) Switch bound State pointer to stateB WITHOUT calling markAllDirty() -> automatic full invalidation!
    cache.sync(stateB);
    QCOMPARE(cache.lastSyncedTileCount(), 256);
    QCOMPARE(cache.atlasImage().pixelColor(tile0Rect.topLeft()), QColor(0xFF, 0xFF, 0xFF));
    QCOMPARE(cache.atlasImage().pixelColor(tile5Rect.topLeft()), QColor(0x00, 0x00, 0x00));

    // (3) Switch global Palette::PEPTO -> Palette::COLODORE -> automatic full invalidation (#FFFFFF -> #FEFEFE)!
    Palette::setActivePalette(Palette::COLODORE);
    cache.sync(stateB);
    QCOMPARE(cache.lastSyncedTileCount(), 256);
    QCOMPARE(cache.atlasImage().pixelColor(tile0Rect.topLeft()), QColor(0xFE, 0xFE, 0xFE));

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testCharsetImageCacheRebuildTriggersAndRendererParity()
{
    const int savedPalette = Palette::activePalette();
    Palette::setActivePalette(Palette::PEPTO);

    State stateA;
    stateA.resetCharsetBuffer();
    stateA.setPenColorDirect(State::PEN_BACKGROUND, 0U); // Black
    stateA.setPenColorDirect(State::PEN_FOREGROUND, 1U); // White (#FFFFFF in Pepto)

    State stateB;
    stateB.resetCharsetBuffer();

    CharsetImageCache cache;

    // First sync rasterizes and matches the PNG-export renderer pixel-for-pixel.
    const QImage& first = cache.sync(stateA);
    QVERIFY(cache.lastSyncRebuilt());
    QCOMPARE(first.size(), QSize(256, 64));
    QCOMPARE(first.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(first,
        DocumentImageRenderer::renderCharset(stateA, false)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied));

    // (1) Nothing changed -> cached image is reused.
    (void)cache.sync(stateA);
    QVERIFY(!cache.lastSyncRebuilt());

    // (2) Charset bytes change: the cache relies on explicit invalidate() from widget slots.
    State::CharsetBuffer buf = stateA.charset();
    buf[0] = 0xFFU; // Char 0, row 0: all foreground pixels.
    stateA.setCharsetBytes(buf.data(), buf.size());
    (void)cache.sync(stateA);
    QVERIFY(!cache.lastSyncRebuilt());
    cache.invalidate();
    QCOMPARE(cache.sync(stateA).pixelColor(0, 0), QColor(0xFF, 0xFF, 0xFF));
    QVERIFY(cache.lastSyncRebuilt());

    // (3) Active document switch -> automatic rebuild.
    (void)cache.sync(stateB);
    QVERIFY(cache.lastSyncRebuilt());
    (void)cache.sync(stateB);
    QVERIFY(!cache.lastSyncRebuilt());

    // (4) Global palette switch -> automatic rebuild.
    Palette::setActivePalette(Palette::COLODORE);
    (void)cache.sync(stateB);
    QVERIFY(cache.lastSyncRebuilt());

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testFormatIndexDecHex()
{
    QCOMPARE(vchar64::formatIndexDecHex(0), QStringLiteral("0 ($00)"));
    QCOMPARE(vchar64::formatIndexDecHex(9), QStringLiteral("9 ($09)"));
    QCOMPARE(vchar64::formatIndexDecHex(66), QStringLiteral("66 ($42)"));
    QCOMPARE(vchar64::formatIndexDecHex(255), QStringLiteral("255 ($FF)"));
    QCOMPARE(vchar64::formatIndexDecHex(300), QStringLiteral("300 ($12C)"));
    QCOMPARE(vchar64::formatIndexDecHex(-1), QStringLiteral("-1"));
}

void TestStateAndCommands::testDocumentImageRendererAndExportPngSelectiveFlags()
{
    const int savedPalette = Palette::activePalette();
    Palette::setActivePalette(Palette::PEPTO);

    State state;
    state.resetCharsetBuffer();
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    state.setPenColorDirect(State::PEN_BACKGROUND, 6U); // Pepto Blue
    state.setPenColorDirect(State::PEN_FOREGROUND, 1U); // Pepto White (#FFFFFF)

    // Paint top-left pixel (0, 0) of tile 0 with foreground pen 1; leave (1, 0) as background pen 6.
    state.tilePaint(0, QPoint(0, 0), State::PEN_FOREGROUND, false);
    QVERIFY(state.replaceMap(QSize(4, 3), std::vector<uint8_t>(12U, 0U)));

    // (A) Verify QImage dimensions and transparent vs opaque background alpha.
    const QImage charsetOpaque = DocumentImageRenderer::renderCharset(state, false);
    QCOMPARE(charsetOpaque.size(), QSize(256, 64));
    QCOMPARE(qAlpha(charsetOpaque.pixel(1, 0)), 255);
    QCOMPARE(QColor::fromRgba(charsetOpaque.pixel(1, 0)), Palette::getColor(6));

    const QImage tilesetTrans = DocumentImageRenderer::renderTileset(state, true);
    QCOMPARE(tilesetTrans.size(), QSize(256, 64)); // 16 cols * 16px, 4 rows * 16px

    const QImage mapTrans = DocumentImageRenderer::renderMap(state, true);
    QCOMPARE(mapTrans.size(), QSize(64, 48)); // 4 * 16 x 3 * 16
    QCOMPARE(qAlpha(mapTrans.pixel(0, 0)), 255);
    QCOMPARE(QColor::fromRgba(mapTrans.pixel(0, 0)), QColor(0xFF, 0xFF, 0xFF));
    QCOMPARE(qAlpha(mapTrans.pixel(1, 0)), 0);

    // (B) Selective PNG export: tileset ONLY
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("scene.png"));
        QVERIFY(state.exportPNG(base, /*exportTileset=*/true, /*exportMap=*/false).isOk());
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("scene-charset.png"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("scene-map.png"))));
    }

    // (C) Selective PNG export: map ONLY
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("scene.png"));
        QVERIFY(state.exportPNG(base, /*exportTileset=*/false, /*exportMap=*/true).isOk());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("scene-charset.png"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("scene-map.png"))));
    }

    // (D) Selective PNG export: BOTH tileset & map readable back via QImage::load()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("scene.png"));
        QVERIFY(state.exportPNG(base, /*exportTileset=*/true, /*exportMap=*/true).isOk());

        QImage loadedTileset;
        QImage loadedMap;
        QVERIFY(loadedTileset.load(dir.filePath(QStringLiteral("scene-charset.png"))));
        QVERIFY(loadedMap.load(dir.filePath(QStringLiteral("scene-map.png"))));
        QCOMPARE(loadedTileset.size(), QSize(256, 64));
        QCOMPARE(loadedMap.size(), QSize(64, 48));
    }

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testClipboardCodecRoundTripAndAdversarialRejection()
{
    QCOMPARE(QString::fromLatin1(ClipboardCodec::kMimeType), QStringLiteral("application/x-vchar64-clip-v2"));

    State state;
    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 2), 1 }));
    State::CharsetBuffer seededCharset { };
    for (size_t i = 0; i < seededCharset.size(); ++i) {
        seededCharset[i] = static_cast<uint8_t>((i * 17U) & 0xFFU);
    }
    State::TileColorsBuffer seededColors { };
    for (size_t i = 0; i < seededColors.size(); ++i) {
        seededColors[i] = static_cast<uint8_t>(i & 0x0FU);
    }
    std::vector<uint8_t> seededMap(12U * 8U, 0U);
    for (size_t i = 0; i < seededMap.size(); ++i) {
        seededMap[i] = static_cast<uint8_t>(i + 1U);
    }
    QVERIFY(state.assignImportedBuffers(seededCharset, seededColors, QSize(12, 8), seededMap));

    // Round-trip MAP selection with sourceRowWidth
    State::CopyRange mapSel { };
    mapSel.type = State::CopyRange::MAP;
    mapSel.offset = 1 * 12 + 2;
    mapSel.blockSize = 4;
    mapSel.skip = 12 - 4;
    mapSel.count = 3;
    mapSel.tileProperties = { QSize(-1, -1), -1 };

    const ClipboardPayload extracted = ClipboardCodec::fromStateSelection(state, mapSel);
    const QByteArray encoded = ClipboardCodec::serialize(extracted);
    const auto decodedRes = ClipboardCodec::deserialize(encoded);
    QVERIFY(decodedRes.isOk());
    QCOMPARE(decodedRes.value().type, State::CopyRange::MAP);
    QCOMPARE(decodedRes.value().blockSize, 4);
    QCOMPARE(decodedRes.value().count, 3);
    QCOMPARE(decodedRes.value().sourceRowWidth, 4);
    QCOMPARE(decodedRes.value().mapCells.size(), static_cast<size_t>(12));

    const State::CopyRange roundTrippedRange = decodedRes.value().toCopyRange();
    QCOMPARE(roundTrippedRange.sourceRowWidth, 4);

    // Reject empty, bad-magic, and truncated payloads safely.
    QVERIFY(!ClipboardCodec::deserialize(QByteArray { }).isOk());
    QVERIFY(!ClipboardCodec::deserialize(QByteArray("VC64NOPE0123456789012345678901234567890123456789012345")).isOk());
    QVERIFY(!ClipboardCodec::deserialize(encoded.left(encoded.size() - 1)).isOk());
}

void TestStateAndCommands::testActiveDocumentRelayIsolationAndLifecycle()
{
    auto docA = std::make_unique<State>();
    auto docB = std::make_unique<State>();
    docA->resetCharsetBuffer();
    docB->resetCharsetBuffer();
    docA->clearUndoStack();
    docB->clearUndoStack();

    ActiveDocumentRelay relay;
    QCOMPARE(relay.activeState(), nullptr);

    std::vector<State*> observedTransitions;
    connect(&relay, &ActiveDocumentRelay::activeStateChanged, &relay, [&observedTransitions](State* st) {
        observedTransitions.push_back(st);
    });

    QSignalSpy spyTileUpdated(&relay, &ActiveDocumentRelay::tileUpdated);
    QSignalSpy spyBytesUpdated(&relay, &ActiveDocumentRelay::bytesUpdated);
    QSignalSpy spyColorUpdated(&relay, &ActiveDocumentRelay::colorPropertiesUpdated);
    QSignalSpy spyMulticolor(&relay, &ActiveDocumentRelay::multicolorModeToggled);
    QSignalSpy spyContents(&relay, &ActiveDocumentRelay::contentsChanged);
    QSignalSpy spyTileIndex(&relay, &ActiveDocumentRelay::tileIndexUpdated);
    QSignalSpy spyCanUndo(&relay, &ActiveDocumentRelay::canUndoChanged);
    QSignalSpy spyUndoIndex(&relay, &ActiveDocumentRelay::undoIndexChanged);
    QSignalSpy spyUndoClean(&relay, &ActiveDocumentRelay::undoCleanChanged);

    auto clearAllSpies = [&]() {
        spyTileUpdated.clear();
        spyBytesUpdated.clear();
        spyColorUpdated.clear();
        spyMulticolor.clear();
        spyContents.clear();
        spyTileIndex.clear();
        spyCanUndo.clear();
        spyUndoIndex.clear();
        spyUndoClean.clear();
    };

    // 1. Bind docA (and verify idempotent re-bind does not re-emit activeStateChanged).
    relay.setActiveState(docA.get());
    relay.setActiveState(docA.get());
    QCOMPARE(relay.activeState(), docA.get());
    QCOMPARE(observedTransitions.size(), static_cast<size_t>(1));
    QCOMPARE(observedTransitions[0], docA.get());

    // Mutate active docA -> signals forward through relay.
    State::CopyRange charCut { };
    charCut.offset = 0;
    charCut.blockSize = 1;
    charCut.skip = 0;
    charCut.count = 1;
    charCut.type = State::CopyRange::CHARS;
    charCut.tileProperties = docA->tileProperties();

    docA->tilePaint(2, QPoint(0, 0), State::PEN_FOREGROUND, false);
    docA->cut(charCut);
    docA->setColorForPen(State::PEN_BACKGROUND, 6);
    docA->setMulticolorMode(true);
    docA->setTileIndex(7);
    QVERIFY(spyTileUpdated.count() >= 1);
    QVERIFY(spyBytesUpdated.count() >= 1);
    QVERIFY(spyColorUpdated.count() >= 1);
    QCOMPARE(spyMulticolor.count(), 1);
    QVERIFY(spyContents.count() >= 1);
    QCOMPARE(spyTileIndex.count(), 1);
    QCOMPARE(spyTileIndex.first().at(0).toInt(), 7);
    QVERIFY(spyCanUndo.count() >= 1);
    QCOMPARE(spyCanUndo.last().at(0).toBool(), true);
    QVERIFY(spyUndoIndex.count() >= 4);
    QVERIFY(spyUndoClean.count() >= 1);
    QCOMPARE(spyUndoClean.last().at(0).toBool(), false);

    // 2. Mutate inactive docB while docA is active -> ZERO relay signals may fire!
    clearAllSpies();
    docB->tilePaint(4, QPoint(1, 1), State::PEN_FOREGROUND, false);
    docB->cut(charCut);
    docB->setColorForPen(State::PEN_FOREGROUND, 14);
    docB->setMulticolorMode(true);
    docB->setTileIndex(12);
    docB->undo();
    QCOMPARE(spyTileUpdated.count(), 0);
    QCOMPARE(spyBytesUpdated.count(), 0);
    QCOMPARE(spyColorUpdated.count(), 0);
    QCOMPARE(spyMulticolor.count(), 0);
    QCOMPARE(spyContents.count(), 0);
    QCOMPARE(spyTileIndex.count(), 0);
    QCOMPARE(spyCanUndo.count(), 0);
    QCOMPARE(spyUndoIndex.count(), 0);
    QCOMPARE(spyUndoClean.count(), 0);

    // 3. Switch active state to docB -> docA becomes completely isolated; docB forwards cleanly.
    relay.setActiveState(docB.get());
    QCOMPARE(relay.activeState(), docB.get());
    QCOMPARE(observedTransitions.size(), static_cast<size_t>(2));
    QCOMPARE(observedTransitions[1], docB.get());
    clearAllSpies();

    docA->tilePaint(3, QPoint(0, 0), State::PEN_FOREGROUND, false);
    docA->undo();
    QCOMPARE(spyTileUpdated.count(), 0);
    QCOMPARE(spyContents.count(), 0);
    QCOMPARE(spyUndoIndex.count(), 0);

    docB->tileInvert(4);
    QCOMPARE(spyTileUpdated.count(), 1);
    QCOMPARE(spyTileUpdated.first().at(0).toInt(), 4);
    QVERIFY(spyContents.count() >= 1);
    QCOMPARE(spyUndoIndex.count(), 1);

    // 4. Destroy currently active docB directly -> QObject::destroyed handler must detach & emit nullptr!
    State* rawDocB = docB.get();
    Q_UNUSED(rawDocB);
    docB.reset();
    QCOMPARE(relay.activeState(), nullptr);
    QCOMPARE(observedTransitions.size(), static_cast<size_t>(3));
    QCOMPARE(observedTransitions[2], nullptr);
}

void TestStateAndCommands::testFileUtilsPathBoundaryAndReplaceExtension()
{
    QVERIFY(FileUtils::getShortNativePath(QString()).isEmpty());
    QVERIFY(FileUtils::replaceExtension(QString(), QStringLiteral("bin")).isEmpty());

#ifdef Q_OS_UNIX
    const QString homePath = QDir::cleanPath(QDir::homePath());
    if (!homePath.isEmpty() && homePath != QStringLiteral("/")) {
        QCOMPARE(FileUtils::getShortNativePath(homePath), QStringLiteral("~"));
        QCOMPARE(
            FileUtils::getShortNativePath(homePath + QStringLiteral("/progs/c64/demo.vchar64proj")),
            QStringLiteral("~/progs/c64/demo.vchar64proj"));

        // Sibling home prefix (e.g. /Users/alice vs /Users/alice_smith) MUST NOT match '~'!
        const QString siblingPrefixPath = homePath + QStringLiteral("_smith/demo.vchar64proj");
        const QString shortenedSibling = FileUtils::getShortNativePath(siblingPrefixPath);
        QVERIFY2(
            !shortenedSibling.startsWith(QLatin1Char('~')),
            qPrintable(QStringLiteral("Unexpected '~' shortening on sibling prefix: %1").arg(shortenedSibling)));
        QCOMPARE(shortenedSibling, QDir::toNativeSeparators(QDir::cleanPath(siblingPrefixPath)));
    }
#endif

    // Multi-dot filenames preserve completeBaseName() and normalize leading dots on newExtension.
    QCOMPARE(
        FileUtils::replaceExtension(QStringLiteral("/tmp/c64/level.1.vchar64proj"), QStringLiteral("bin")),
        QDir(QStringLiteral("/tmp/c64")).filePath(QStringLiteral("level.1.bin")));
    QCOMPARE(
        FileUtils::replaceExtension(QStringLiteral("/tmp/c64/level.1.vchar64proj"), QStringLiteral(".prg")),
        QDir(QStringLiteral("/tmp/c64")).filePath(QStringLiteral("level.1.prg")));

    // Extensionless path in directory appends extension without chopping last character.
    QCOMPARE(
        FileUtils::replaceExtension(QStringLiteral("/tmp/c64/untitled"), QStringLiteral("c")),
        QDir(QStringLiteral("/tmp/c64")).filePath(QStringLiteral("untitled.c")));

    // Bare filenames (no directory separator) do NOT prepend "./" and do NOT chop last character.
    QCOMPARE(
        FileUtils::replaceExtension(QStringLiteral("untitled"), QStringLiteral("s")),
        QStringLiteral("untitled.s"));
    QCOMPARE(
        FileUtils::replaceExtension(QStringLiteral("hero.v2.ctm"), QStringLiteral(".h")),
        QStringLiteral("hero.v2.h"));
}

void TestStateAndCommands::testFileUtilsClassifyDroppedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto touch = [&dir](const QString& name) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return QString();
        }
        file.write("x", 1);
        return QFileInfo(path).absoluteFilePath();
    };

    const QString koala = touch(QStringLiteral("picture.koa"));
    const QString ctm = touch(QStringLiteral("level.ctm"));
    const QString vsf = touch(QStringLiteral("snapshot.vsf"));
    const QString prgUpper = touch(QStringLiteral("FONT.PRG"));
    const QString klaUpper = touch(QStringLiteral("TITLE.KLA"));
    const QString project = touch(QStringLiteral("hero.v2.vchar64proj"));
    const QString text = touch(QStringLiteral("notes.txt"));
    const QString noSuffix = touch(QStringLiteral("README"));
    for (const QString& p : { koala, ctm, vsf, prgUpper, klaUpper, project, text, noSuffix }) {
        QVERIFY(!p.isEmpty());
    }
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder.bin")));

    const QList<QUrl> urls = {
        QUrl::fromLocalFile(koala), // import, dropped first
        QUrl::fromLocalFile(ctm),
        QUrl(QStringLiteral("https://example.com/remote.prg")), // not local
        QUrl::fromLocalFile(vsf),
        QUrl::fromLocalFile(text), // unsupported: would load as raw bytes
        QUrl::fromLocalFile(prgUpper), // uppercase suffix
        QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.bin"))), // does not exist
        QUrl::fromLocalFile(dir.filePath(QStringLiteral("folder.bin"))), // directory
        QUrl::fromLocalFile(klaUpper), // uppercase Koala suffix
        QUrl::fromLocalFile(noSuffix), // no suffix
        QUrl::fromLocalFile(ctm), // duplicate
        QUrl::fromLocalFile(project), // multi-dot stem
    };

    // Render as "action|path" strings so a failure prints a readable diff.
    const auto describe = [](const QList<FileUtils::DroppedFile>& files) {
        QStringList out;
        for (const FileUtils::DroppedFile& f : files) {
            const char* action = f.action == FileUtils::DropAction::Open ? "open"
                : f.action == FileUtils::DropAction::ImportKoala         ? "koala"
                                                                         : "vice";
            out.append(QStringLiteral("%1|%2").arg(QLatin1String(action), f.path));
        }
        return out;
    };

    // Opens first (drop order), then imports (drop order).
    const QStringList expected = {
        QStringLiteral("open|") + ctm,
        QStringLiteral("open|") + prgUpper,
        QStringLiteral("open|") + project,
        QStringLiteral("koala|") + koala,
        QStringLiteral("vice|") + vsf,
        QStringLiteral("koala|") + klaUpper,
    };
    QCOMPARE(describe(FileUtils::classifyDroppedFiles(urls)), expected);

    QVERIFY(FileUtils::classifyDroppedFiles({ }).isEmpty());
    QVERIFY(FileUtils::classifyDroppedFiles({ QUrl::fromLocalFile(text) }).isEmpty());
}

void TestStateAndCommands::testEcmPenBackground3AndStateLifecycle()
{
    State state;
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_HIRES);
    QVERIFY(!state.isExtendedColorMode());
    QVERIFY(!state.isMulticolorMode());
    QCOMPARE(static_cast<int>(State::PEN_MAX), 5);
    QCOMPARE(state.getGlobalColorForPen(State::PEN_BACKGROUND3), 6);
    QCOMPARE(state.exportCharsetByteCount(), static_cast<size_t>(State::CHAR_BUFFER_SIZE));

    const std::array<int, State::PEN_MAX> expectedDefaults = { 1, 5, 7, 11, 6 };
    for (int pen = 0; pen < State::PEN_MAX; ++pen) {
        QCOMPARE(Palette::getColorForPen(nullptr, pen), Palette::getColor(expectedDefaults[static_cast<size_t>(pen)]));
    }

    QSignalSpy spyColor(&state, &State::colorPropertiesUpdated);
    state.setColorForPen(State::PEN_BACKGROUND3, 0x1A);
    QCOMPARE(state.getGlobalColorForPen(State::PEN_BACKGROUND3), 10);
    QCOMPARE(spyColor.count(), 1);
    QCOMPARE(spyColor.takeFirst().at(0).toInt(), static_cast<int>(State::PEN_BACKGROUND3));
    QVERIFY(state.isModified());

    state.undo();
    QCOMPARE(state.getGlobalColorForPen(State::PEN_BACKGROUND3), 6);
    state.redo();
    QCOMPARE(state.getGlobalColorForPen(State::PEN_BACKGROUND3), 10);

    const int undoCountBeforeNoOp = state.getUndoStack()->count();
    spyColor.clear();
    state.setColorForPen(State::PEN_BACKGROUND3, 10);
    QCOMPARE(spyColor.count(), 0);
    QCOMPARE(state.getUndoStack()->count(), undoCountBeforeNoOp);

    state.setColorForPen(State::PEN_BACKGROUND, 1);
    state.setColorForPen(State::PEN_MULTICOLOR1, 2);
    state.setColorForPen(State::PEN_MULTICOLOR2, 3);
    state.setColorForPen(State::PEN_BACKGROUND3, 4);

    QCOMPARE(State::penForExtendedBgBank(0), static_cast<int>(State::PEN_BACKGROUND));
    QCOMPARE(State::penForExtendedBgBank(1), static_cast<int>(State::PEN_MULTICOLOR1));
    QCOMPARE(State::penForExtendedBgBank(2), static_cast<int>(State::PEN_MULTICOLOR2));
    QCOMPARE(State::penForExtendedBgBank(3), static_cast<int>(State::PEN_BACKGROUND3));

    // Outside ECM, every char uses PEN_BACKGROUND ($D021).
    QCOMPARE(state.getBackgroundColorForChar(0), 1);
    QCOMPARE(state.getBackgroundColorForChar(65), 1);
    QCOMPARE(state.getBackgroundColorForChar(129), 1);
    QCOMPARE(state.getBackgroundColorForChar(193), 1);

    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(state.exportCharsetByteCount(), static_cast<size_t>(State::ECM_CHAR_BUFFER_SIZE));
    QCOMPARE(state.getBackgroundColorForChar(0), 1);
    QCOMPARE(state.getBackgroundColorForChar(65), 2);
    QCOMPARE(state.getBackgroundColorForChar(129), 3);
    QCOMPARE(state.getBackgroundColorForChar(193), 4);

    state.setColorForPen(State::PEN_BACKGROUND3, 12);
    State copied;
    copied.copyState(state);
    QCOMPARE(copied.characterMode(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QVERIFY(copied.isExtendedColorMode());
    QCOMPARE(copied.getGlobalColorForPen(State::PEN_BACKGROUND3), 12);

    copied.reset();
    QCOMPARE(copied.characterMode(), State::CHARACTER_MODE_HIRES);
    QVERIFY(!copied.isExtendedColorMode());
    QCOMPARE(copied.getGlobalColorForPen(State::PEN_BACKGROUND3), 6);
}

void TestStateAndCommands::testSetCharacterModeAndMulticolorUndoFromEcm()
{
    State state;
    QSignalSpy spyCharMode(&state, &State::characterModeChanged);
    QSignalSpy spyMc(&state, &State::multicolorModeToggled);

    // HIRES -> EXTENDED_COLOR: emits characterModeChanged once, 0 multicolorModeToggled.
    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QVERIFY(state.isExtendedColorMode());
    QVERIFY(!state.isMulticolorMode());
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyCharMode.takeFirst().at(0).value<State::CharacterMode>(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyMc.count(), 0);

    // Setting already-active mode must be a complete no-op.
    const int countAfterEnterEcm = state.getUndoStack()->count();
    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyCharMode.count(), 0);
    QCOMPARE(spyMc.count(), 0);
    QCOMPARE(state.getUndoStack()->count(), countAfterEnterEcm);

    // EXTENDED_COLOR -> HIRES: emits characterModeChanged once, 0 multicolorModeToggled.
    state.setCharacterMode(State::CHARACTER_MODE_HIRES);
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_HIRES);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyCharMode.takeFirst().at(0).value<State::CharacterMode>(), State::CHARACTER_MODE_HIRES);
    QCOMPARE(spyMc.count(), 0);

    // HIRES -> EXTENDED_COLOR -> MULTICOLOR: crossing into MC emits multicolorModeToggled(true).
    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    spyCharMode.clear();
    spyMc.clear();
    state.setCharacterMode(State::CHARACTER_MODE_MULTICOLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyCharMode.takeFirst().at(0).value<State::CharacterMode>(), State::CHARACTER_MODE_MULTICOLOR);
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), true);

    // MULTICOLOR -> EXTENDED_COLOR: leaving MC emits multicolorModeToggled(false).
    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyCharMode.takeFirst().at(0).value<State::CharacterMode>(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), false);

    // SetMulticolorModeCommand undo from ECM must revert to EXTENDED_COLOR (not Hi-Res).
    state.clearUndoStack();
    spyCharMode.clear();
    spyMc.clear();
    state.setMulticolorMode(true);
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_MULTICOLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), true);
    spyCharMode.clear();

    state.undo();
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(spyCharMode.takeFirst().at(0).value<State::CharacterMode>(), State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyMc.count(), 1);
    QCOMPARE(spyMc.takeFirst().at(0).toBool(), false);

    state.redo();
    QCOMPARE(state.characterMode(), State::CHARACTER_MODE_MULTICOLOR);

    // Non-destructive mode switching & Multicolor display gating.
    state.setCharacterModeDirect(State::CHARACTER_MODE_HIRES);
    State::CharsetBuffer sentinel { };
    for (size_t i = 0; i < sentinel.size(); ++i) {
        sentinel[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);
    }
    state.setCharsetBytes(sentinel.data(), sentinel.size());
    state.setPenColorDirect(State::PEN_FOREGROUND, 15U);
    state.setTileColorDirect(0, 15U);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    state.setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QVERIFY(!state.shouldBeDisplayedInMulticolor());
    QVERIFY(!state.shouldBeDisplayedInMulticolor2(0));

    state.setCharacterMode(State::CHARACTER_MODE_HIRES);
    for (size_t i = static_cast<size_t>(State::ECM_CHAR_BUFFER_SIZE); i < sentinel.size(); ++i) {
        QCOMPARE(state.charset()[i], sentinel[i]);
    }
}

void TestStateAndCommands::testEcmGlyphAliasingAndWriteTilePixelPenBgClearing()
{
    struct ExposedState final : public State {
        using State::getCharFromTile;
        using State::setCharForTile;
    };

    ExposedState state;
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);

    // 6-bit aliasing contract.
    QCOMPARE(state.effectiveCharIndex(-1), 0);
    QCOMPARE(state.effectiveCharIndex(0), 0);
    QCOMPARE(state.effectiveCharIndex(63), 63);
    QCOMPARE(state.effectiveCharIndex(64), 0);
    QCOMPARE(state.effectiveCharIndex(65), 1);
    QCOMPARE(state.effectiveCharIndex(129), 1);
    QCOMPARE(state.effectiveCharIndex(193), 1);
    QCOMPARE(state.effectiveCharIndex(255), 63);
    QCOMPARE(state.effectiveCharIndex(256), 63);

    // Out-of-range char_index >= 256 must be rejected BEFORE effectiveCharIndex() wraps it.
    State::CharsetBuffer seeded { };
    std::fill_n(seeded.data(), 16U, uint8_t { 0xFFU }); // glyphs 0 and 1 all-ones
    state.setCharsetBytes(seeded.data(), seeded.size());
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(2, 2), 256 }));
    // Tile 0 sub-char (0, 0) -> 0 (valid), sub-char (1, 0) -> 256 (OOB: must NOT wrap to 0!).
    QCOMPARE(state.tileGetPen(0, QPoint(0, 0)), 1);
    QCOMPARE(state.tileGetPen(0, QPoint(8, 0)), 0);
    const State::CharGlyph oobGlyph = state.getCharFromTile(0, 1, 0);
    QCOMPARE(oobGlyph, (State::CharGlyph { }));
    const State::CharGlyph zeroed { };
    state.setCharForTile(0, 1, 0, zeroed);
    QCOMPARE(state.charset()[0], static_cast<uint8_t>(0xFFU));

    // Reset to 1x1 blank ECM state for aliasing and pen-clearing tests.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(1, 1), 1 }));
    state.resetCharsetBuffer();
    state.clearUndoStack();

    // Painting pixel (0, 0) of tile 65 (bank 1, glyph 1) sets bit 7 of glyph 1 and aliases banks 0..3.
    state.tilePaint(65, QPoint(0, 0), State::PEN_FOREGROUND, false);
    QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x80U));
    QCOMPARE(state.charset()[65 * 8], static_cast<uint8_t>(0x00U));
    QCOMPARE(state.tileGetPen(1, QPoint(0, 0)), 1);
    QCOMPARE(state.tileGetPen(65, QPoint(0, 0)), 1);
    QCOMPARE(state.tileGetPen(129, QPoint(0, 0)), 1);
    QCOMPARE(state.tileGetPen(193, QPoint(0, 0)), 1);

    // Verify isBackgroundPen() across ECM, Hi-Res, and Multicolor.
    QVERIFY(state.isBackgroundPen(State::PEN_BACKGROUND));
    QVERIFY(state.isBackgroundPen(State::PEN_MULTICOLOR1));
    QVERIFY(state.isBackgroundPen(State::PEN_MULTICOLOR2));
    QVERIFY(state.isBackgroundPen(State::PEN_BACKGROUND3));
    QVERIFY(!state.isBackgroundPen(State::PEN_FOREGROUND));

    state.setCharacterModeDirect(State::CHARACTER_MODE_HIRES);
    QVERIFY(state.isBackgroundPen(State::PEN_BACKGROUND));
    QVERIFY(state.isBackgroundPen(State::PEN_MULTICOLOR1));
    QVERIFY(state.isBackgroundPen(State::PEN_MULTICOLOR2));
    QVERIFY(state.isBackgroundPen(State::PEN_BACKGROUND3));
    QVERIFY(!state.isBackgroundPen(State::PEN_FOREGROUND));

    state.setCharacterModeDirect(State::CHARACTER_MODE_MULTICOLOR);
    QVERIFY(state.isBackgroundPen(State::PEN_BACKGROUND));
    QVERIFY(!state.isBackgroundPen(State::PEN_MULTICOLOR1));
    QVERIFY(!state.isBackgroundPen(State::PEN_MULTICOLOR2));
    QVERIFY(!state.isBackgroundPen(State::PEN_BACKGROUND3));
    QVERIFY(!state.isBackgroundPen(State::PEN_FOREGROUND));

    // Seed pixels (0,0), (1,0), (2,0), (3,0) of glyph 1 to FG and clear each with BG0..BG3.
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);
    state.resetCharsetBuffer();
    state.clearUndoStack();
    state.tilePaint(1, QPoint(0, 0), State::PEN_FOREGROUND, false);
    state.tilePaint(1, QPoint(1, 0), State::PEN_FOREGROUND, false);
    state.tilePaint(1, QPoint(2, 0), State::PEN_FOREGROUND, false);
    state.tilePaint(1, QPoint(3, 0), State::PEN_FOREGROUND, false);
    QCOMPARE(state.charset()[8], static_cast<uint8_t>(0xF0U));

    state.tilePaint(65, QPoint(0, 0), State::PEN_BACKGROUND, false);
    state.tilePaint(65, QPoint(1, 0), State::PEN_MULTICOLOR1, false);
    state.tilePaint(65, QPoint(2, 0), State::PEN_MULTICOLOR2, false);
    state.tilePaint(65, QPoint(3, 0), State::PEN_BACKGROUND3, false);
    QCOMPARE(state.charset()[8], static_cast<uint8_t>(0x00U));

    for (int i = 0; i < 4; ++i) {
        state.undo();
    }
    QCOMPARE(state.charset()[8], static_cast<uint8_t>(0xF0U));
}

void TestStateAndCommands::testEcmBankWrap63To64AcrossCopyPasteClearRotateAndShift()
{
    State state;
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);

    // 1. Contiguous 3x3 tile 7 straddles chars 63..71 -> physical glyphs {63, 0, 1, 2, 3, 4, 5, 6, 7}.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(3, 3), 1 }));
    State::CharsetBuffer seed { };
    for (size_t g = 0U; g < 8U; ++g) {
        for (size_t row = 0U; row < 8U; ++row) {
            seed[g * 8U + row] = static_cast<uint8_t>(0x10U + g * 8U + row);
        }
    }
    for (size_t row = 0; row < 8; ++row) {
        seed[63U * 8U + row] = static_cast<uint8_t>(0xA0U + row);
    }
    for (int c = 64; c <= 71; ++c) {
        for (size_t row = 0; row < 8; ++row) {
            seed[static_cast<size_t>(c) * 8U + row] = static_cast<uint8_t>(0x5AU ^ static_cast<uint8_t>(c));
        }
    }
    state.setCharsetBytes(seed.data(), seed.size());
    state.clearUndoStack();

    state.tileClear(7);
    for (int g = 0; g < 8; ++g) {
        for (size_t row = 0; row < 8; ++row) {
            QCOMPARE(state.charset()[static_cast<size_t>(g) * 8U + row], uint8_t { 0U });
        }
    }
    for (size_t row = 0; row < 8; ++row) {
        QCOMPARE(state.charset()[63U * 8U + row], uint8_t { 0U });
    }
    // Raw bytes at chars 64..71 must remain untouched.
    for (int c = 64; c <= 71; ++c) {
        for (size_t row = 0; row < 8; ++row) {
            QCOMPARE(state.charset()[static_cast<size_t>(c) * 8U + row], seed[static_cast<size_t>(c) * 8U + row]);
        }
    }

    state.undo();
    QCOMPARE(state.charset(), seed);

    // 2. Non-self-aliased interleaved 2x2 tile straddling bank boundary:
    //    tile 62 with interleaved = 2 -> logical {62, 64, 66, 68} -> physical {62, 0, 2, 4}.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(2, 2), 2 }));
    QVERIFY(!state.hasAliasedSubChars(62));
    QVERIFY(state.canRotateTile(62));
    const std::array<int, 4> tile62Phys = { 62, 0, 2, 4 };
    for (size_t idx = 0; idx < tile62Phys.size(); ++idx) {
        const size_t base = static_cast<size_t>(tile62Phys[idx]) * 8U;
        for (size_t row = 0; row < 8; ++row) {
            seed[base + row] = static_cast<uint8_t>((idx + 1U) * 17U + row);
        }
    }
    state.setCharsetBytes(seed.data(), seed.size());
    state.clearUndoStack();

    // Rotate 90 deg clockwise + undo.
    state.tileRotate(62);
    QVERIFY(state.charset() != seed);
    for (int c = 64; c <= 71; ++c) {
        for (size_t row = 0; row < 8; ++row) {
            QCOMPARE(state.charset()[static_cast<size_t>(c) * 8U + row], seed[static_cast<size_t>(c) * 8U + row]);
        }
    }
    state.undo();
    QCOMPARE(state.charset(), seed);

    // Flip H/V and Shift L/R/U/D across bank wrap + undo.
    state.tileFlipHorizontally(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    state.tileFlipVertically(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    state.tileShiftLeft(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    state.tileShiftRight(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    state.tileShiftUp(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    state.tileShiftDown(62);
    QVERIFY(state.charset() != seed);
    state.undo();
    QCOMPARE(state.charset(), seed);

    // Raw 64..71 untouched after all spatial ops.
    for (int c = 64; c <= 71; ++c) {
        for (size_t row = 0; row < 8; ++row) {
            QCOMPARE(state.charset()[static_cast<size_t>(c) * 8U + row], seed[static_cast<size_t>(c) * 8U + row]);
        }
    }

    // 3. _pasteChars across 63 -> 64 and logical Color RAM preservation.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(1, 1), 1 }));
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    State::CharsetBuffer pasteCharsSrc { };
    State::TileColorsBuffer pasteColorsSrc { };
    pasteColorsSrc.fill(1U);
    for (size_t c = 0U; c < 4U; ++c) {
        std::fill_n(pasteCharsSrc.data() + c * 8U, 8U, static_cast<uint8_t>(0x30U + c));
        pasteColorsSrc[c] = static_cast<uint8_t>(10U + c);
    }

    state.setTileColorDirect(0, 5U);
    state.setTileColorDirect(1, 6U);
    state.setTileColorDirect(62, 1U);
    state.setTileColorDirect(63, 2U);
    state.setTileColorDirect(64, 3U);
    state.setTileColorDirect(65, 4U);
    state.clearUndoStack();

    const std::vector<uint8_t> combined = buildCombinedCharsAndColorsPayload(pasteCharsSrc, pasteColorsSrc);
    State::CopyRange charRange { };
    charRange.offset = 0;
    charRange.blockSize = 4;
    charRange.skip = 0;
    charRange.count = 1;
    charRange.sourceRowWidth = 16;
    charRange.type = State::CopyRange::CHARS;
    charRange.tileProperties = state.tileProperties();
    charRange.bufferSize = static_cast<int>(combined.size());

    QSignalSpy spyBytes(&state, &State::bytesUpdated);
    state.paste(62, charRange, combined.data());

    // Physical glyphs 62, 63, 0, 1 receive copied bytes.
    QCOMPARE(state.charset()[62 * 8], uint8_t { 0x30U });
    QCOMPARE(state.charset()[63 * 8], uint8_t { 0x31U });
    QCOMPARE(state.charset()[0 * 8], uint8_t { 0x32U });
    QCOMPARE(state.charset()[1 * 8], uint8_t { 0x33U });
    // Raw 64..65 remain untouched.
    QCOMPARE(state.charset()[64 * 8], seed[64 * 8]);
    QCOMPARE(state.charset()[65 * 8], seed[65 * 8]);
    // All bytesUpdated positions must be < 512.
    QCOMPARE(spyBytes.count(), 4);
    const std::array<int, 4> expectedBytePos = { 62 * 8, 63 * 8, 0 * 8, 1 * 8 };
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(spyBytes.at(i).at(0).toInt(), expectedBytePos[static_cast<size_t>(i)]);
        QCOMPARE(spyBytes.at(i).at(1).toInt(), 8);
    }
    // Logical Color RAM updates 62..65 without touching 0..1.
    QCOMPARE(state.tileColors()[62], uint8_t { 10U });
    QCOMPARE(state.tileColors()[63], uint8_t { 11U });
    QCOMPARE(state.tileColors()[64], uint8_t { 12U });
    QCOMPARE(state.tileColors()[65], uint8_t { 13U });
    QCOMPARE(state.tileColors()[0], uint8_t { 5U });
    QCOMPARE(state.tileColors()[1], uint8_t { 6U });

    state.undo();
    QCOMPARE(state.charset(), seed);
    QCOMPARE(state.tileColors()[62], uint8_t { 1U });
    QCOMPARE(state.tileColors()[63], uint8_t { 2U });
    QCOMPARE(state.tileColors()[64], uint8_t { 3U });
    QCOMPARE(state.tileColors()[65], uint8_t { 4U });

    // 4. _pasteTiles for 3x3 tile 7 straddling 63..71 -> physical {63, 0..7}.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(3, 3), 1 }));
    State::CharsetBuffer tilePasteSrc { };
    for (size_t g = 0U; g < 9U; ++g) {
        std::fill_n(tilePasteSrc.data() + g * 8U, 8U, static_cast<uint8_t>(0xC0U + g));
    }
    State::TileColorsBuffer tilePasteColors { };
    tilePasteColors[0] = 9U;
    state.setTileColorDirect(7, 2U);
    const std::vector<uint8_t> combinedTiles = buildCombinedCharsAndColorsPayload(tilePasteSrc, tilePasteColors);
    State::CopyRange tileRange { };
    tileRange.offset = 0;
    tileRange.blockSize = 1;
    tileRange.skip = 0;
    tileRange.count = 1;
    tileRange.sourceRowWidth = 1;
    tileRange.type = State::CopyRange::TILES;
    tileRange.tileProperties = state.tileProperties();
    tileRange.bufferSize = static_cast<int>(combinedTiles.size());

    spyBytes.clear();
    state.paste(63, tileRange, combinedTiles.data());
    QCOMPARE(state.charset()[63 * 8], uint8_t { 0xC0U });
    for (size_t g = 0U; g < 8U; ++g) {
        QCOMPARE(state.charset()[g * 8U], static_cast<uint8_t>(0xC1U + g));
    }
    for (int c = 64; c <= 71; ++c) {
        QCOMPARE(state.charset()[static_cast<size_t>(c) * 8U], seed[static_cast<size_t>(c) * 8U]);
    }
    for (const QList<QVariant>& emission : spyBytes) {
        QVERIFY(emission.at(0).toInt() < State::ECM_CHAR_BUFFER_SIZE);
    }
    QCOMPARE(state.tileColors()[7], uint8_t { 9U });

    state.undo();
    QCOMPARE(state.charset(), seed);
    QCOMPARE(state.tileColors()[7], uint8_t { 2U });
}

void TestStateAndCommands::testEcmSelfAliasedTileGuardAndInvertDeduplication()
{
    State state;
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);

    // Contiguous 8x8 tiles (interleaved == 1) never self-alias.
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(8, 8), 1 }));
    for (int t = 0; t < state.maxTiles(); ++t) {
        QVERIFY(!state.hasAliasedSubChars(t));
    }

    // 2x2 with interleaved = 32 -> {0, 32, 64, 96} -> {0, 32, 0, 32} (self-aliased).
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(2, 2), 32 }));
    QVERIFY(state.hasAliasedSubChars(0));
    QVERIFY(!state.canRotateTile(0));

    // 2x2 with interleaved = 64 -> {0, 64, 128, 192} -> {0, 0, 0, 0} (4x self-aliased).
    QVERIFY(state.setTilePropertiesDirect(State::TileProperties { QSize(2, 2), 64 }));
    QVERIFY(state.hasAliasedSubChars(0));
    QVERIFY(!state.canRotateTile(0));

    const State::CharGlyph initialGlyph { 0xAAU, 0x55U, 0x0FU, 0xF0U, 0x01U, 0x80U, 0x00U, 0xFFU };
    State::CharsetBuffer buf { };
    std::copy_n(initialGlyph.data(), 8U, buf.data());
    state.setCharsetBytes(buf.data(), buf.size());
    state.clearUndoStack();

    const int initialUndoCount = state.getUndoStack()->count();
    state.tileRotate(0);
    state.tileFlipHorizontally(0);
    state.tileFlipVertically(0);
    state.tileShiftLeft(0);
    state.tileShiftRight(0);
    state.tileShiftUp(0);
    state.tileShiftDown(0);
    QCOMPARE(state.getUndoStack()->count(), initialUndoCount);
    QCOMPARE(state.charset(), buf);

    // _tileInvert must invert physical glyph 0 exactly ONCE (not 4 times!).
    state.tileInvert(0);
    const State::CharGlyph expectedInverted { 0x55U, 0xAAU, 0xF0U, 0x0FU, 0xFEU, 0x7FU, 0xFFU, 0x00U };
    for (size_t row = 0; row < 8; ++row) {
        QCOMPARE(state.charset()[row], expectedInverted[row]);
    }

    state.undo();
    for (size_t row = 0; row < 8; ++row) {
        QCOMPARE(state.charset()[row], initialGlyph[row]);
    }

    state.redo();
    for (size_t row = 0; row < 8; ++row) {
        QCOMPARE(state.charset()[row], expectedInverted[row]);
    }
}

void TestStateAndCommands::testTileAtlasCacheEcmCrossBankDirtyAndContextInvalidation()
{
    const int savedPalette = Palette::activePalette();
    Palette::setActivePalette(Palette::PEPTO);

    State state;
    state.resetCharsetBuffer();
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);
    state.setPenColorDirect(State::PEN_BACKGROUND, 0U); // Black
    state.setPenColorDirect(State::PEN_MULTICOLOR1, 2U); // Red
    state.setPenColorDirect(State::PEN_MULTICOLOR2, 5U); // Green
    state.setPenColorDirect(State::PEN_BACKGROUND3, 6U); // Blue
    state.setPenColorDirect(State::PEN_FOREGROUND, 1U); // White (#FFFFFF in Pepto)

    TileAtlasCache cache;
    cache.sync(state);
    QCOMPARE(cache.lastSyncedTileCount(), 256);

    // Paint pixel (0, 0) of tile 1 ($01) and mark ONLY tile 1 dirty -> expands to {1, 65, 129, 193}.
    state.tilePaint(1, QPoint(0, 0), State::PEN_FOREGROUND, false);
    cache.markTileDirty(1);
    cache.sync(state);
    QCOMPARE(cache.lastSyncedTileCount(), 4);

    const std::array<std::pair<int, int>, 4> kBankTiles = { {
        { 1, 0 },
        { 65, 2 },
        { 129, 5 },
        { 193, 6 },
    } };
    for (const auto& [tileIdx, expectedBgPen] : kBankTiles) {
        const QRect rect = cache.sourceRectForTile(tileIdx);
        QCOMPARE(cache.atlasImage().pixelColor(rect.topLeft()), QColor(0xFF, 0xFF, 0xFF));
        QCOMPARE(
            cache.atlasImage().pixelColor(rect.topLeft() + QPoint(1, 0)),
            Palette::getColor(expectedBgPen));
    }

    const QRect tile2Rect = cache.sourceRectForTile(2);
    QCOMPARE(cache.atlasImage().pixelColor(tile2Rect.topLeft()), Palette::getColor(0));

    // Mutate $D024 without markAllDirty() -> automatic full context invalidation.
    state.setPenColorDirect(State::PEN_BACKGROUND3, 7U); // Yellow
    cache.sync(state);
    QCOMPARE(cache.lastSyncedTileCount(), 256);
    QCOMPARE(
        cache.atlasImage().pixelColor(cache.sourceRectForTile(193).topLeft() + QPoint(1, 0)),
        Palette::getColor(7));

    // Mode switch without markAllDirty() -> automatic full context invalidation.
    state.setCharacterModeDirect(State::CHARACTER_MODE_HIRES);
    cache.sync(state);
    QCOMPARE(cache.lastSyncedTileCount(), 256);

    // Multi-char cross-bank tile (3x3 tile 7 spanning chars 63..71 -> physical 63, 0..7):
    // dirtying tile 0 (glyphs 0..8) must expand to mark tile 7 dirty as well.
    state.resetCharsetBuffer();
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);
    QVERIFY(state.setTilePropertiesDirect({ QSize(3, 3), 1 }));
    cache.sync(state);
    QCOMPARE(cache.lastSyncedTileCount(), state.maxTiles());

    state.tilePaint(0, QPoint(0, 0), State::PEN_FOREGROUND, false);
    cache.markTileDirty(0);
    cache.sync(state);
    QVERIFY(cache.lastSyncedTileCount() > 1);
    const QRect tile7Rect = cache.sourceRectForTile(7);
    // Sub-char 1 of tile 7 is char 64 (bank 1, physical glyph 0), located at pixel offset (+8, 0).
    QCOMPARE(cache.atlasImage().pixelColor(tile7Rect.topLeft() + QPoint(8, 0)), QColor(0xFF, 0xFF, 0xFF));
    QCOMPARE(
        cache.atlasImage().pixelColor(tile7Rect.topLeft() + QPoint(9, 0)),
        Palette::getColor(2));

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testDocumentImageRendererAndCharsetCacheEcm()
{
    const int savedPalette = Palette::activePalette();
    Palette::setActivePalette(Palette::PEPTO);

    State state;
    state.resetCharsetBuffer();
    state.setCharacterModeDirect(State::CHARACTER_MODE_EXTENDED_COLOR);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);
    state.setPenColorDirect(State::PEN_BACKGROUND, 6U); // Blue
    state.setPenColorDirect(State::PEN_MULTICOLOR1, 2U); // Red
    state.setPenColorDirect(State::PEN_MULTICOLOR2, 5U); // Green
    state.setPenColorDirect(State::PEN_BACKGROUND3, 7U); // Yellow

    // Set foreground colors including >= 8 to verify bit 3 never forces 2bpp Multicolor in ECM.
    state.setTileColorDirect(1, 1U); // White
    state.setTileColorDirect(65, 14U); // Light Blue
    state.setTileColorDirect(129, 10U); // Light Red
    state.setTileColorDirect(193, 13U); // Light Green

    // Paint glyph 1 pixel (0, 0) = 1; (1, 0) = 0.
    state.tilePaint(1, QPoint(0, 0), State::PEN_FOREGROUND, false);
    QVERIFY(state.replaceMap(QSize(4, 1), std::vector<uint8_t> { 1U, 65U, 129U, 193U }));

    const QImage charsetOpaque = DocumentImageRenderer::renderCharset(state, false);
    QCOMPARE(charsetOpaque.format(), QImage::Format_ARGB32);

    CharsetImageCache charsetCache;
    const QImage& cachedCharset = charsetCache.sync(state);
    QVERIFY(charsetCache.lastSyncRebuilt());
    QCOMPARE(cachedCharset.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(cachedCharset, charsetOpaque.convertToFormat(QImage::Format_ARGB32_Premultiplied));

    struct BankCellSpec {
        int col;
        int row;
        int mapCellIndex;
        int fgColor;
        int bgColor;
        bool bg0Transparent;
    };
    const std::array<BankCellSpec, 4> kCells = { {
        { 1, 0, 0, 1, 6, true },
        { 1, 2, 1, 14, 2, false },
        { 1, 4, 2, 10, 5, false },
        { 1, 6, 3, 13, 7, false },
    } };

    for (const BankCellSpec& cell : kCells) {
        const int cx = cell.col * 8;
        const int cy = cell.row * 8;
        QCOMPARE(QColor::fromRgba(charsetOpaque.pixel(cx, cy)), Palette::getColor(cell.fgColor));
        QCOMPARE(QColor::fromRgba(charsetOpaque.pixel(cx + 1, cy)), Palette::getColor(cell.bgColor));
    }

    // Verify BG0-only transparency across renderCharset, renderTileset, and renderMap.
    const QImage charsetTrans = DocumentImageRenderer::renderCharset(state, true);
    const QImage tilesetTrans = DocumentImageRenderer::renderTileset(state, true);
    const QImage mapTrans = DocumentImageRenderer::renderMap(state, true);
    QCOMPARE(charsetTrans.format(), QImage::Format_ARGB32);
    QCOMPARE(tilesetTrans.format(), QImage::Format_ARGB32);
    QCOMPARE(mapTrans.format(), QImage::Format_ARGB32);

    for (const BankCellSpec& cell : kCells) {
        const int cx = cell.col * 8;
        const int cy = cell.row * 8;
        const int mx = cell.mapCellIndex * 8;

        for (const QImage* img : { &charsetTrans, &tilesetTrans }) {
            const QRgb fgPixel = img->pixel(cx, cy);
            const QRgb bgPixel = img->pixel(cx + 1, cy);
            QCOMPARE(qAlpha(fgPixel), 255);
            QCOMPARE(QColor::fromRgba(fgPixel), Palette::getColor(cell.fgColor));
            if (cell.bg0Transparent) {
                QCOMPARE(qAlpha(bgPixel), 0);
            } else {
                QCOMPARE(qAlpha(bgPixel), 255);
                QCOMPARE(QColor::fromRgba(bgPixel), Palette::getColor(cell.bgColor));
            }
        }

        const QRgb mapFgPixel = mapTrans.pixel(mx, 0);
        const QRgb mapBgPixel = mapTrans.pixel(mx + 1, 0);
        QCOMPARE(qAlpha(mapFgPixel), 255);
        QCOMPARE(QColor::fromRgba(mapFgPixel), Palette::getColor(cell.fgColor));
        if (cell.bg0Transparent) {
            QCOMPARE(qAlpha(mapBgPixel), 0);
        } else {
            QCOMPARE(qAlpha(mapBgPixel), 255);
            QCOMPARE(QColor::fromRgba(mapBgPixel), Palette::getColor(cell.bgColor));
        }
    }

    Palette::setActivePalette(savedPalette);
}

void TestStateAndCommands::testActiveDocumentRelayCharacterModeChanged()
{
    auto docA = std::make_unique<State>();
    auto docB = std::make_unique<State>();

    ActiveDocumentRelay relay;
    QSignalSpy spyCharMode(&relay, &ActiveDocumentRelay::characterModeChanged);
    QVERIFY(spyCharMode.isValid());

    relay.setActiveState(docA.get());
    docA->setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(
        spyCharMode.first().at(0).value<State::CharacterMode>(),
        State::CHARACTER_MODE_EXTENDED_COLOR);

    // Mutating inactive docB must emit 0 characterModeChanged signals through the relay.
    spyCharMode.clear();
    docB->setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    docB->setCharacterMode(State::CHARACTER_MODE_MULTICOLOR);
    QCOMPARE(spyCharMode.count(), 0);

    // Switching active document forwards docB's transitions and isolates docA.
    relay.setActiveState(docB.get());
    docA->setCharacterMode(State::CHARACTER_MODE_HIRES);
    QCOMPARE(spyCharMode.count(), 0);

    docB->setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    QCOMPARE(spyCharMode.count(), 1);
    QCOMPARE(
        spyCharMode.first().at(0).value<State::CharacterMode>(),
        State::CHARACTER_MODE_EXTENDED_COLOR);
}

QTEST_GUILESS_MAIN(TestStateAndCommands)
#include "tst_state_and_commands.moc"
