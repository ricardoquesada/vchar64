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
 * @file tst_stateexport.cpp
 * @brief Headless `QTest` suite verifying `StateExport` formats, C17 validity, and `QSaveFile` atomicity.
 *
 * Tests `.vchar64proj` zeroed-padding and little-endian determinism (even with
 * `0xCD`-dirtied call stacks), raw binary and `.prg` 16-bit load-address headers,
 * 6502 `.s` assembly output, valid non-empty C array codegen (compiled via host C17
 * compiler when available), multi-dot suffix replacement, and `QSaveFile` transactional
 * rollback preserving existing target files on failure.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#include "state.h"
#include "stateexport.h"
#include "stateimport.h"

namespace {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void dirtyCallStack()
{
    volatile uint8_t trash[512];
    for (size_t i = 0; i < sizeof(trash); ++i) {
        trash[i] = 0xCDU;
    }
    (void)trash[0];
}

[[nodiscard]] QByteArray readWholeFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

[[nodiscard]] QString findC17Compiler()
{
    for (const QString& candidate : {
             QStringLiteral("cc"),
             QStringLiteral("clang"),
             QStringLiteral("gcc") }) {
        const QString found = QStandardPaths::findExecutable(candidate);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return { };
}

} // namespace

class TestStateExport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testSaveVChar64ZeroedReservedEndiannessAndRoundTrip();
    void testSaveRawExactBytes();
    void testSavePrgLittleEndianLoadAddress();
    void testSaveAsmFormatAndCoreAppVersion();
    void testSaveCEmptyBufferRejected();
    void testSaveCBoundarySizesAndIdentifierSanitization();
    void testQuickExportDispatchesExportFormatC();
    void testFilenameFixSuffixMultiDotPreservation();
    void testQSaveFileFailurePropagationRootSafe();
    void testQSaveFileAtomicityPreservesExistingFileOnAbort();
};

void TestStateExport::initTestCase()
{
    QVERIFY(QCoreApplication::instance() != nullptr);
    QCOMPARE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()), nullptr);
}

void TestStateExport::testSaveVChar64ZeroedReservedEndiannessAndRoundTrip()
{
    State state;
    State::CharsetBuffer charset { };
    for (size_t i = 0; i < charset.size(); ++i) {
        charset[i] = static_cast<uint8_t>((i * 29U + 7U) & 0xFFU);
    }
    State::TileColorsBuffer colors { };
    for (size_t i = 0; i < colors.size(); ++i) {
        colors[i] = static_cast<uint8_t>((i * 5U + 1U) & 0x0FU);
    }
    std::vector<uint8_t> mapCells(37U * 19U, 0U);
    for (size_t i = 0; i < mapCells.size(); ++i) {
        mapCells[i] = static_cast<uint8_t>((i * 11U + 3U) & 0xFFU);
    }

    QVERIFY(state.setTilePropertiesDirect({ QSize(2, 3), 42 }));
    QVERIFY(state.assignImportedBuffers(charset, colors, QSize(37, 19), mapCells));
    state.setPenColorDirect(0, 2U);
    state.setPenColorDirect(1, 5U);
    state.setPenColorDirect(2, 8U);
    state.setPenColorDirect(3, 14U);
    state.setMulticolorModeDirect(true);
    state.setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    State::ExportProperties exportProps { };
    exportProps.addresses[0] = 0x3800U;
    exportProps.addresses[1] = 0x4400U;
    exportProps.addresses[2] = 0xD800U;
    exportProps.format = State::EXPORT_FORMAT_C;
    exportProps.features = State::EXPORT_FEATURE_ALL;
    state.setExportPropertiesDirect(exportProps);
    state.setKeyboardMappingDirect(State::KEYBOARD_MAPPING_ATARI8);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("project.vchar64proj"));

    dirtyCallStack();
    const auto saveRes = StateExport::saveVChar64(state, path);
    QVERIFY(saveRes.isOk());

    constexpr qint64 kExpectedBytes = 32LL + 2048LL + 256LL + (37LL * 19LL); // 3,039 bytes
    QCOMPARE(saveRes.value(), kExpectedBytes);

    const QByteArray raw = readWholeFile(path);
    QCOMPARE(raw.size(), static_cast<qsizetype>(kExpectedBytes));

    const auto byteAt = [&](qsizetype idx) -> uint8_t {
        return static_cast<uint8_t>(raw.at(idx));
    };

    QCOMPARE(raw.left(5), QByteArray("VChar"));
    QCOMPARE(byteAt(5), static_cast<uint8_t>(3U));
    QCOMPARE(byteAt(6), static_cast<uint8_t>(2U));
    QCOMPARE(byteAt(7), static_cast<uint8_t>(5U));
    QCOMPARE(byteAt(8), static_cast<uint8_t>(8U));
    QCOMPARE(byteAt(9), static_cast<uint8_t>(14U));
    QCOMPARE(byteAt(10), static_cast<uint8_t>(1U)); // vic_res
    // num_chars = 256 LE -> {0x00, 0x01}
    QCOMPARE(byteAt(11), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(12), static_cast<uint8_t>(0x01U));
    QCOMPARE(byteAt(13), static_cast<uint8_t>(2U));
    QCOMPARE(byteAt(14), static_cast<uint8_t>(3U));
    QCOMPARE(byteAt(15), static_cast<uint8_t>(42U));
    QCOMPARE(byteAt(16), static_cast<uint8_t>(1U)); // FOREGROUND_COLOR_PER_TILE
    // map_width = 37 LE -> {0x25, 0x00}; map_height = 19 LE -> {0x13, 0x00}
    QCOMPARE(byteAt(17), static_cast<uint8_t>(0x25U));
    QCOMPARE(byteAt(18), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(19), static_cast<uint8_t>(0x13U));
    QCOMPARE(byteAt(20), static_cast<uint8_t>(0x00U));
    // addresses LE: 0x3800, 0x4400, 0xD800
    QCOMPARE(byteAt(21), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(22), static_cast<uint8_t>(0x38U));
    QCOMPARE(byteAt(23), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(24), static_cast<uint8_t>(0x44U));
    QCOMPARE(byteAt(25), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(26), static_cast<uint8_t>(0xD8U));
    QCOMPARE(byteAt(27), static_cast<uint8_t>(State::EXPORT_FEATURE_ALL));
    QCOMPARE(byteAt(28), static_cast<uint8_t>(State::EXPORT_FORMAT_C));
    QCOMPARE(byteAt(29), static_cast<uint8_t>(State::KEYBOARD_MAPPING_ATARI8));
    // reserved[2] MUST be zeroed (zero stack-padding leak!).
    QCOMPARE(byteAt(30), static_cast<uint8_t>(0x00U));
    QCOMPARE(byteAt(31), static_cast<uint8_t>(0x00U));

    State reloaded;
    const auto loadRes = StateImport::loadVChar64(reloaded, path);
    QVERIFY(loadRes.isOk());
    QCOMPARE(reloaded.charset(), state.charset());
    QCOMPARE(reloaded.tileColors(), state.tileColors());
    QCOMPARE(reloaded.mapSize(), state.mapSize());
    QCOMPARE(reloaded.map(), state.map());
    QCOMPARE(reloaded.tileProperties(), state.tileProperties());
    QCOMPARE(reloaded.isMulticolorMode(), state.isMulticolorMode());
    QCOMPARE(reloaded.getForegroundColorMode(), state.getForegroundColorMode());
    QCOMPARE(reloaded.exportProperties(), state.exportProperties());
    QCOMPARE(reloaded.getKeyboardMapping(), state.getKeyboardMapping());
    for (int pen = 0; pen < 4; ++pen) {
        QCOMPARE(reloaded.getGlobalColorForPen(pen), state.getGlobalColorForPen(pen));
    }
}

void TestStateExport::testSaveRawExactBytes()
{
    std::array<uint8_t, 2048> input { };
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>((i * 73U) & 0xFFU);
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("charset.raw"));

    const auto res = StateExport::saveRaw(path, input.data(), input.size());
    QVERIFY(res.isOk());
    QCOMPARE(res.value(), 2048LL);

    const QByteArray disk = readWholeFile(path);
    QCOMPARE(disk.size(), static_cast<qsizetype>(input.size()));
    QVERIFY(std::memcmp(disk.constData(), input.data(), input.size()) == 0);
}

void TestStateExport::testSavePrgLittleEndianLoadAddress()
{
    std::array<uint8_t, 256> payload { };
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("chars.prg"));

    const auto res = StateExport::savePRG(path, payload.data(), payload.size(), 0xC801U);
    QVERIFY(res.isOk());
    QCOMPARE(res.value(), 258LL);

    const QByteArray disk = readWholeFile(path);
    QCOMPARE(disk.size(), static_cast<qsizetype>(258));
    QCOMPARE(static_cast<uint8_t>(disk[0]), static_cast<uint8_t>(0x01U));
    QCOMPARE(static_cast<uint8_t>(disk[1]), static_cast<uint8_t>(0xC8U));
    QVERIFY(std::memcmp(disk.constData() + 2, payload.data(), payload.size()) == 0);

    State loaded;
    quint16 outAddr = 0;
    const auto loadRes = StateImport::loadPRG(loaded, path, &outAddr);
    QVERIFY(loadRes.isOk());
    QCOMPARE(outAddr, static_cast<quint16>(0xC801U));
    QVERIFY(std::memcmp(loaded.charset().data(), payload.data(), payload.size()) == 0);
}

void TestStateExport::testSaveAsmFormatAndCoreAppVersion()
{
    const QString previousVersion = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    std::array<uint8_t, 17> buf { };
    for (size_t i = 0; i < 16U; ++i) {
        buf[i] = static_cast<uint8_t>(i);
    }
    buf[16] = 0xFEU;

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("hero.s"));

    const auto res = StateExport::saveAsm(path, buf.data(), buf.size(), QStringLiteral("hero_chars"));
    QVERIFY(res.isOk());

    const QString text = QString::fromUtf8(readWholeFile(path));
    QVERIFY(text.contains(QStringLiteral("; Exported using VChar64 v1.0.0")));
    QVERIFY(text.contains(QStringLiteral("hero_chars:\n")));
    QVERIFY(text.contains(QStringLiteral(".byte $00,$01,$02,$03,$04,$05,$06,$07,$08,$09,$0a,$0b,$0c,$0d,$0e,$0f")));
    QVERIFY(text.contains(QStringLiteral(".byte $fe\t; 16")));
    QVERIFY(text.contains(QStringLiteral("HERO_CHARS_COUNT = 17")));

    QCoreApplication::setApplicationVersion(previousVersion);
}

void TestStateExport::testSaveCEmptyBufferRejected()
{
    const std::array<uint8_t, 1> dummy = { 0x00U };
    QString streamSink;
    QTextStream out(&streamSink, QIODevice::WriteOnly);

    const auto sectionRes = StateExport::saveCSection(out, dummy.data(), 0U, QStringLiteral("empty"));
    QVERIFY(!sectionRes.isOk());
    QCOMPARE(sectionRes.error().code, vchar64::ErrorCode::InvalidDimensions);
    QVERIFY(streamSink.isEmpty());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("should_not_exist.c"));
    const auto fileRes = StateExport::saveC(path, nullptr, 0U, QStringLiteral("empty"));
    QVERIFY(!fileRes.isOk());
    QCOMPARE(fileRes.error().code, vchar64::ErrorCode::InvalidDimensions);
    QVERIFY(!QFileInfo::exists(path));
}

void TestStateExport::testSaveCBoundarySizesAndIdentifierSanitization()
{
    QCOMPARE(StateExport::sanitizeCIdentifier(QStringLiteral("64.level-1")), QStringLiteral("_64_level_1"));
    QCOMPARE(StateExport::sanitizeCIdentifier(QString { }), QStringLiteral("vchar64_data"));
    QCOMPARE(StateExport::sanitizeCIdentifier(QStringLiteral("charset$main")), QStringLiteral("charset_main"));

    const std::array<size_t, 5> kSizes = { 1U, 16U, 17U, 33U, 2048U };
    const std::array<std::pair<QString, QString>, 3> kLabels = { {
        { QStringLiteral("64.level-1"), QStringLiteral("_64_level_1") },
        { QString { }, QStringLiteral("vchar64_data") },
        { QStringLiteral("charset$main"), QStringLiteral("charset_main") },
    } };

    const QString cCompiler = findC17Compiler();
    const QRegularExpression kHexTokenRe(QStringLiteral("^0x([0-9a-f]{2})$"));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    int caseCounter = 0;
    for (const size_t n : kSizes) {
        std::vector<uint8_t> data(n, 0U);
        for (size_t i = 0; i < n; ++i) {
            // Exercise high bytes 0x80..0xFF to catch signed char narrowing bugs.
            data[i] = static_cast<uint8_t>(0x80U | ((i * 37U + 5U) & 0x7FU));
        }

        for (const auto& [rawLabel, expectedId] : kLabels) {
            const QString filePath = tempDir.filePath(
                QStringLiteral("export_%1_%2.c").arg(n).arg(caseCounter++));

            const auto res = StateExport::saveC(filePath, data.data(), data.size(), rawLabel);
            QVERIFY2(res.isOk(), qPrintable(res.isOk() ? QString() : res.error().detail));

            const QString cText = QString::fromUtf8(readWholeFile(filePath));
            QVERIFY(cText.contains(QStringLiteral("#include <stdint.h>\n")));
            QVERIFY(!cText.contains(QStringLiteral("const char")));
            QVERIFY(!cText.contains(QStringLiteral("[][16]")));

            const QString expectedDefine = QStringLiteral("#define %1_COUNT %2\n")
                                               .arg(expectedId.toUpper())
                                               .arg(n);
            const QString expectedDecl = QStringLiteral("const uint8_t %1[%2] = {")
                                             .arg(expectedId)
                                             .arg(n);
            QVERIFY2(cText.contains(expectedDefine), qPrintable(cText));
            QVERIFY2(cText.contains(expectedDecl), qPrintable(cText));

            // Unconditional in-process C initializer grammar & comma-boundary verification.
            const qsizetype openBrace = cText.indexOf(QLatin1Char('{'));
            const qsizetype closeBrace = cText.lastIndexOf(QStringLiteral("};"));
            QVERIFY(openBrace > 0 && closeBrace > openBrace);

            const QString initializerBody = cText.mid(openBrace + 1, closeBrace - openBrace - 1).trimmed();
            QVERIFY(!initializerBody.endsWith(QLatin1Char(',')));

            const QStringList rawTokens = initializerBody.split(QLatin1Char(','));
            QCOMPARE(rawTokens.size(), static_cast<qsizetype>(n));

            for (size_t i = 0; i < n; ++i) {
                const QString tok = rawTokens.at(static_cast<qsizetype>(i)).trimmed();
                const QRegularExpressionMatch match = kHexTokenRe.match(tok);
                QVERIFY2(
                    match.hasMatch(),
                    qPrintable(QStringLiteral("Malformed C hex token '%1' at index %2 (N=%3)")
                            .arg(tok)
                            .arg(i)
                            .arg(n)));
                bool ok = false;
                const uint parsed = match.captured(1).toUInt(&ok, 16);
                QVERIFY(ok);
                QCOMPARE(static_cast<uint8_t>(parsed), data[i]);
            }

            // External strict ISO C17 compiler verification whenever cc/clang/gcc is installed.
            if (!cCompiler.isEmpty()) {
                QProcess proc;
                proc.start(
                    cCompiler,
                    { QStringLiteral("-std=c17"),
                        QStringLiteral("-Wall"),
                        QStringLiteral("-Wextra"),
                        QStringLiteral("-Wpedantic"),
                        QStringLiteral("-Werror"),
                        QStringLiteral("-xc"),
                        QStringLiteral("-fsyntax-only"),
                        filePath });
                QVERIFY2(proc.waitForFinished(10000), "C17 compiler syntax check timed out");
                QVERIFY2(
                    proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0,
                    qPrintable(QString::fromUtf8(proc.readAllStandardError())));
            }
        }
    }
}

void TestStateExport::testQuickExportDispatchesExportFormatC()
{
    State state;
    state.resetCharsetBuffer();

    State::ExportProperties props { };
    props.addresses[0] = 0x2000U;
    props.addresses[1] = 0x2800U;
    props.addresses[2] = 0xD800U;
    props.format = State::EXPORT_FORMAT_C;
    props.features = State::EXPORT_FEATURE_ALL;

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString baseFile = tempDir.filePath(QStringLiteral("game.c"));

    QVERIFY(state.exportC(baseFile, props).isOk());

    // Mutate charset byte 0 from 0x00 -> 0x7E and invoke quick-export State::export_() (Ctrl+E path).
    State::CharsetBuffer mutated = state.charset();
    mutated[0] = 0x7EU;
    state.setCharsetBytes(mutated.data(), mutated.size());

    const auto quickRes = state.export_();
    QVERIFY2(quickRes.isOk(), qPrintable(quickRes.isOk() ? QString() : quickRes.error().detail));

    const std::array<QString, 3> kGenerated = {
        tempDir.filePath(QStringLiteral("game-charset.c")),
        tempDir.filePath(QStringLiteral("game-map.c")),
        tempDir.filePath(QStringLiteral("game-colors.c")),
    };

    for (const QString& cFile : kGenerated) {
        QVERIFY2(QFileInfo::exists(cFile), qPrintable(cFile));
        const QString content = QString::fromUtf8(readWholeFile(cFile));
        QVERIFY(content.contains(QStringLiteral("#include <stdint.h>")));
        QVERIFY(content.contains(QStringLiteral("const uint8_t ")));
        QVERIFY(!content.contains(QStringLiteral(".byte $")));
    }

    const QString charsetC = QString::fromUtf8(readWholeFile(kGenerated[0]));
    QVERIFY(charsetC.contains(QStringLiteral("0x7e")));
}

void TestStateExport::testFilenameFixSuffixMultiDotPreservation()
{
    QCOMPARE(
        State::filenameFixSuffix(QStringLiteral("/tmp/dir.v1/level1.v2.raw"), State::EXPORT_FEATURE_CHARSET),
        QStringLiteral("/tmp/dir.v1/level1.v2-charset.raw"));
    QCOMPARE(
        State::filenameFixSuffix(QStringLiteral("/tmp/dir.v1/level1.v2.raw"), State::EXPORT_FEATURE_MAP),
        QStringLiteral("/tmp/dir.v1/level1.v2-map.raw"));
    QCOMPARE(
        State::filenameFixSuffix(QStringLiteral("/tmp/dir.v1/level1.v2.raw"), State::EXPORT_FEATURE_COLORS),
        QStringLiteral("/tmp/dir.v1/level1.v2-colors.raw"));

    // Idempotent suffix replacement when input already ends with -charset / -map / -colors.
    QCOMPARE(
        State::filenameFixSuffix(QStringLiteral("/tmp/dir.v1/level1.v2-charset.c"), State::EXPORT_FEATURE_MAP),
        QStringLiteral("/tmp/dir.v1/level1.v2-map.c"));
}

void TestStateExport::testQSaveFileFailurePropagationRootSafe()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Create a regular file "not_a_directory" so "not_a_directory/impossible.out" fails deterministically even as root!
    const QString blockerPath = tempDir.filePath(QStringLiteral("not_a_directory"));
    {
        QFile blocker(blockerPath);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("x", 1), 1LL);
    }

    const QString impossibleTarget = blockerPath + QStringLiteral("/impossible.out");
    const std::array<uint8_t, 16> sample = { 1U, 2U, 3U, 4U };
    QImage validImg(8, 8, QImage::Format_ARGB32);
    validImg.fill(0xFFFFFFFFU);

    State dirtyState;
    dirtyState.tileInvert(0); // Make dirtyState.isModified() == true
    QVERIFY(dirtyState.isModified());

    const auto rVchar = StateExport::saveVChar64(dirtyState, impossibleTarget);
    QVERIFY(!rVchar.isOk());
    QCOMPARE(rVchar.error().code, vchar64::ErrorCode::FileOpenFailed);

    const auto rRaw = StateExport::saveRaw(impossibleTarget, sample.data(), sample.size());
    QVERIFY(!rRaw.isOk());
    QCOMPARE(rRaw.error().code, vchar64::ErrorCode::FileOpenFailed);

    const auto rPrg = StateExport::savePRG(impossibleTarget, sample.data(), sample.size(), 0x2000U);
    QVERIFY(!rPrg.isOk());
    QCOMPARE(rPrg.error().code, vchar64::ErrorCode::FileOpenFailed);

    const auto rAsm = StateExport::saveAsm(impossibleTarget, sample.data(), sample.size(), QStringLiteral("lbl"));
    QVERIFY(!rAsm.isOk());
    QCOMPARE(rAsm.error().code, vchar64::ErrorCode::FileOpenFailed);

    const auto rC = StateExport::saveC(impossibleTarget, sample.data(), sample.size(), QStringLiteral("lbl"));
    QVERIFY(!rC.isOk());
    QCOMPARE(rC.error().code, vchar64::ErrorCode::FileOpenFailed);

    const auto rPng = StateExport::savePNG(impossibleTarget, validImg);
    QVERIFY(!rPng.isOk());
    QCOMPARE(rPng.error().code, vchar64::ErrorCode::FileOpenFailed);

    State::ExportProperties props { };
    props.features = State::EXPORT_FEATURE_CHARSET;
    props.format = State::EXPORT_FORMAT_RAW;
    QVERIFY(!dirtyState.exportRaw(impossibleTarget, props).isOk());

    // Failed saveProject MUST NOT clear _undoStack->isClean() / isModified()!
    const auto saveProjRes = dirtyState.saveProject(impossibleTarget);
    QVERIFY(!saveProjRes.isOk());
    QVERIFY(dirtyState.isModified());
    QVERIFY(!dirtyState.getUndoStack()->isClean());
}

void TestStateExport::testQSaveFileAtomicityPreservesExistingFileOnAbort()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString existingPath = tempDir.filePath(QStringLiteral(" precious_project.vchar64proj"));
    const QByteArray kSentinel("EXISTING_USER_PROJECT_KEEP_SAFE!");
    QCOMPARE(kSentinel.size(), static_cast<qsizetype>(32));

    {
        QFile seed(existingPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        QCOMPARE(seed.write(kSentinel), static_cast<qint64>(kSentinel.size()));
    }

    const std::array<uint8_t, 4> buf = { 1U, 2U, 3U, 4U };

    // 1. Pre-open validation failures targeting existingPath must leave existingPath untouched.
    QVERIFY(!StateExport::saveC(existingPath, buf.data(), 0U, QStringLiteral("id")).isOk());
    QCOMPARE(readWholeFile(existingPath), kSentinel);

    QVERIFY(!StateExport::savePNG(existingPath, QImage { }).isOk());
    QCOMPARE(readWholeFile(existingPath), kSentinel);

    QVERIFY(!StateExport::saveRaw(existingPath, nullptr, 16U).isOk());
    QCOMPARE(readWholeFile(existingPath), kSentinel);

    // 2. Mid-stream QSaveFile write abort (cancelWriting before commit) leaves existingPath 100% intact.
    {
        QSaveFile abortedSave(existingPath);
        QVERIFY(abortedSave.open(QIODevice::WriteOnly));
        QVERIFY(abortedSave.write("TRUNCATED_PARTIAL_CORRUPTION") > 0);
        abortedSave.cancelWriting();
        QVERIFY(!abortedSave.commit());
    }
    QCOMPARE(readWholeFile(existingPath), kSentinel);
}

QTEST_GUILESS_MAIN(TestStateExport)
#include "tst_stateexport.moc"
