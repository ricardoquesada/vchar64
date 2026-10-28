/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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
 * @file stateexport.cpp
 * @brief Atomic (`QSaveFile`) binary, source-code, and PNG writers for `StateExport`.
 *
 * All disk writes stage through `QSaveFile`, explicitly convert multi-byte fields
 * with `qToLittleEndian`, call `cancelWriting()` on partial write or stream errors,
 * and atomically rename into place via `commit()`.
 */

#include "stateexport.h"

#include <cstring>
#include <limits>

#include <QCoreApplication>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>

#include "state.h"
#include "stateimport.h"

namespace {

/** @brief Writes `byteCount` bytes from `src` to `saveFile`, failing on short writes or `qint64` overflow. */
[[nodiscard]] bool writeAllBytes(QSaveFile& saveFile, const void* src, size_t byteCount)
{
    if (byteCount == 0U) {
        return true;
    }
    if (src == nullptr
        || byteCount > static_cast<size_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    const auto expected = static_cast<qint64>(byteCount);
    return saveFile.write(static_cast<const char*>(src), expected) == expected;
}

/** @brief Resolves the application version string in both GUI and headless unit-test contexts. */
[[nodiscard]] QString effectiveAppVersion()
{
    const QString runtimeVersion = QCoreApplication::applicationVersion();
    if (!runtimeVersion.isEmpty()) {
        return runtimeVersion;
    }
#ifdef VCHAR64_VERSION
    return QStringLiteral(VCHAR64_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

} // namespace

vchar64::Result<qint64> StateExport::saveVChar64(
    const State& state,
    const QString& filename)
{
    const QSize mapDims = state.mapSize();
    if (mapDims.width() < 1 || mapDims.width() > State::kMaxMapDim
        || mapDims.height() < 1 || mapDims.height() > State::kMaxMapDim) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Map dimensions out of exportable VChar64 range"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for atomic save: %2")
                .arg(filename, saveFile.errorString()));
    }

    // Value-initialize to guarantee reserved[2] == {0, 0} (zero stack-padding leak).
    StateImport::VChar64Header header { };
    std::memcpy(header.id, "VChar", 5);
    header.version = 3U;

    for (int i = 0; i < 4; ++i) {
        header.colors[i] = static_cast<uint8_t>(
            static_cast<unsigned int>(state.getGlobalColorForPen(i)) & 0x0FU);
    }

    header.num_chars = qToLittleEndian<quint16>(
        static_cast<quint16>(State::CHAR_BUFFER_SIZE / 8));

    const State::TileProperties tileProps = state.tileProperties();
    header.tile_width = static_cast<uint8_t>(tileProps.size.width());
    header.tile_height = static_cast<uint8_t>(tileProps.size.height());
    header.char_interleaved = static_cast<uint8_t>(tileProps.interleaved);
    header.vic_res = state.isMulticolorMode() ? 1U : 0U;
    header.color_mode = static_cast<uint8_t>(state.getForegroundColorMode());
    header.map_width = qToLittleEndian<quint16>(static_cast<quint16>(mapDims.width()));
    header.map_height = qToLittleEndian<quint16>(static_cast<quint16>(mapDims.height()));

    const State::ExportProperties exportProps = state.exportProperties();
    header.address_charset = qToLittleEndian<quint16>(exportProps.addresses[0]);
    header.address_map = qToLittleEndian<quint16>(exportProps.addresses[1]);
    header.address_attribs = qToLittleEndian<quint16>(exportProps.addresses[2]);
    header.export_features = exportProps.features;
    header.export_format = static_cast<uint8_t>(exportProps.format);
    header.keyboard_mapping = static_cast<uint8_t>(state.getKeyboardMapping());

    const auto& charsetBuf = state.charset();
    const auto& colorsBuf = state.tileColors();
    const auto& mapBuf = state.map();

    if (!writeAllBytes(saveFile, &header, sizeof(header))
        || !writeAllBytes(saveFile, charsetBuf.data(), charsetBuf.size())
        || !writeAllBytes(saveFile, colorsBuf.data(), colorsBuf.size())
        || !writeAllBytes(saveFile, mapBuf.data(), mapBuf.size())) {
        const QString errMsg = saveFile.errorString();
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing '%1': %2").arg(filename, errMsg));
    }

    const qint64 totalBytes = static_cast<qint64>(
        sizeof(header) + charsetBuf.size() + colorsBuf.size() + mapBuf.size());

    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit '%1': %2").arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(totalBytes);
}

vchar64::Result<qint64> StateExport::saveRaw(
    const QString& filename,
    const uint8_t* buffer,
    size_t bufferSize)
{
    if (bufferSize > 0U && buffer == nullptr) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to saveRaw"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for RAW export: %2")
                .arg(filename, saveFile.errorString()));
    }

    if (!writeAllBytes(saveFile, buffer, bufferSize)) {
        const QString errMsg = saveFile.errorString();
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing RAW file '%1': %2").arg(filename, errMsg));
    }

    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit RAW file '%1': %2")
                .arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(static_cast<qint64>(bufferSize));
}

vchar64::Result<qint64> StateExport::savePRG(
    const QString& filename,
    const uint8_t* buffer,
    size_t bufferSize,
    uint16_t address)
{
    if (bufferSize > 0U && buffer == nullptr) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to savePRG"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for PRG export: %2")
                .arg(filename, saveFile.errorString()));
    }

    uint8_t leAddress[2] { };
    qToLittleEndian<quint16>(address, leAddress);

    if (!writeAllBytes(saveFile, leAddress, sizeof(leAddress))
        || !writeAllBytes(saveFile, buffer, bufferSize)) {
        const QString errMsg = saveFile.errorString();
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing PRG file '%1': %2").arg(filename, errMsg));
    }

    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit PRG file '%1': %2")
                .arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(static_cast<qint64>(sizeof(leAddress) + bufferSize));
}

vchar64::Result<qint64> StateExport::saveAsm(
    const QString& filename,
    const uint8_t* buffer,
    size_t bufferSize,
    const QString& label)
{
    if (buffer == nullptr && bufferSize > 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to saveAsm"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for ASM export: %2")
                .arg(filename, saveFile.errorString()));
    }

    const QString cleanLabel = label.isEmpty() ? QStringLiteral("vchar64_data") : label;

    QTextStream out(&saveFile);
    out << "; Exported using VChar64 v" << effectiveAppVersion() << "\n";
    out << "; Total bytes: " << bufferSize << "\n";
    out << cleanLabel << ":\n";

    for (size_t i = 0; i < bufferSize;) {
        out << ".byte ";
        size_t j = 0;
        for (; j < 16U && i < bufferSize; ++j, ++i) {
            if (j != 0U) {
                out << ",";
            }
            out << QStringLiteral("$%1").arg(buffer[i], 2, 16, QLatin1Char('0'));
        }
        out << "\t; " << (i - j) << "\n";
    }
    out << cleanLabel.toUpper() << "_COUNT = " << bufferSize << "\n";
    out.flush();

    if (out.status() != QTextStream::Ok || saveFile.error() != QFileDevice::NoError) {
        const QString errMsg = saveFile.errorString();
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Stream error while writing ASM file '%1': %2").arg(filename, errMsg));
    }

    const qint64 bytesWritten = saveFile.pos();
    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit ASM file '%1': %2")
                .arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytesWritten);
}

QString StateExport::sanitizeCIdentifier(const QString& rawName)
{
    QString clean;
    clean.reserve(rawName.size() + 1);

    for (const QChar ch : rawName) {
        const char16_t u = ch.unicode();
        const bool isAsciiAlnumOrUnderscore = (u >= u'A' && u <= u'Z')
            || (u >= u'a' && u <= u'z')
            || (u >= u'0' && u <= u'9')
            || (u == u'_');
        clean.append(isAsciiAlnumOrUnderscore ? ch : QLatin1Char('_'));
    }

    if (clean.isEmpty()) {
        return QStringLiteral("vchar64_data");
    }

    const char16_t first = clean.front().unicode();
    if (first >= u'0' && first <= u'9') {
        clean.prepend(QLatin1Char('_'));
    }

    return clean;
}

vchar64::Result<qint64> StateExport::saveCSection(
    QTextStream& out,
    const uint8_t* buffer,
    size_t bufferSize,
    const QString& rawIdentifier)
{
    if (buffer == nullptr || bufferSize == 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Cannot export empty C byte array"));
    }

    const QString identifier = sanitizeCIdentifier(rawIdentifier);
    const QString macroPrefix = identifier.toUpper();

    // Emit flat ISO C17 `const uint8_t <id>[<N>]` without GNU/Clang `__attribute__`
    // syntax so 6502 C compilers (cc65, oscar64, vbcc, clang-mos) compile directly.
    out << "#define " << macroPrefix << "_COUNT " << bufferSize << "\n";
    out << "const uint8_t " << identifier << "[" << bufferSize << "] = {\n";

    for (size_t i = 0; i < bufferSize; ++i) {
        if ((i % 16U) == 0U) {
            out << "    ";
        }
        out << QStringLiteral("0x%1").arg(buffer[i], 2, 16, QLatin1Char('0'));
        if ((i + 1U) < bufferSize) {
            out << (((i % 16U) == 15U) ? ",\n" : ", ");
        } else {
            out << "\n";
        }
    }
    out << "};\n";
    out.flush();

    if (out.status() != QTextStream::Ok) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Stream write failed while formatting C byte array"));
    }

    return vchar64::Result<qint64>::ok(static_cast<qint64>(bufferSize));
}

vchar64::Result<qint64> StateExport::saveC(
    const QString& filename,
    const uint8_t* buffer,
    size_t bufferSize,
    const QString& label)
{
    // Validate BEFORE opening QSaveFile so invalid inputs never touch the filesystem.
    if (buffer == nullptr || bufferSize == 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Cannot export empty C byte array"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for C export: %2")
                .arg(filename, saveFile.errorString()));
    }

    QTextStream out(&saveFile);
    out << "// Exported using VChar64 v" << effectiveAppVersion() << "\n";
    out << "// Total bytes: " << bufferSize << "\n";
    out << "#include <stdint.h>\n\n";

    const auto sectionRes = saveCSection(out, buffer, bufferSize, label);
    if (!sectionRes.isOk() || saveFile.error() != QFileDevice::NoError) {
        const QString errMsg = sectionRes.isOk() ? saveFile.errorString() : sectionRes.error().detail;
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            sectionRes.isOk() ? vchar64::ErrorCode::FileWriteFailed : sectionRes.error().code,
            errMsg);
    }

    const qint64 bytesWritten = saveFile.pos();
    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit C file '%1': %2")
                .arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytesWritten);
}

vchar64::Result<qint64> StateExport::savePNG(
    const QString& filename,
    const QImage& image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Cannot save null or empty QImage to PNG"));
    }

    QSaveFile saveFile(filename);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for PNG export: %2")
                .arg(filename, saveFile.errorString()));
    }

    if (!image.save(&saveFile, "PNG") || saveFile.error() != QFileDevice::NoError) {
        const QString errMsg = saveFile.errorString();
        saveFile.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while encoding PNG '%1': %2").arg(filename, errMsg));
    }

    const qint64 bytesWritten = saveFile.pos();
    if (!saveFile.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit PNG '%1': %2")
                .arg(filename, saveFile.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytesWritten);
}
