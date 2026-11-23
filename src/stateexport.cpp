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

// Atomic (QSaveFile) binary, source-code, and PNG writers for StateExport.
//
// All disk writes stage through QSaveFile, explicitly convert multi-byte fields
// with qToLittleEndian, call cancelWriting() on partial write or stream errors,
// and atomically rename into place via commit().

#include "stateexport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <QCoreApplication>
#include <QFileDevice>
#include <QIODevice>
#include <QImage>
#include <QSaveFile>
#include <QSize>
#include <QString>
#include <QTextStream>
#include <QtEndian>

#include "core/result.h"
#include "state.h"
#include "stateimport.h"

namespace {

// Writes `byte_count` bytes from `src` to `save_file`, returning false on short
// writes or qint64 overflow.
[[nodiscard]] bool WriteAllBytes(QSaveFile& save_file, const void* src, size_t byte_count)
{
    if (byte_count == 0U) {
        return true;
    }
    if (src == nullptr
        || byte_count > static_cast<size_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    const qint64 expected_bytes = static_cast<qint64>(byte_count);
    return save_file.write(static_cast<const char*>(src), expected_bytes) == expected_bytes;
}

// Resolves the application version string in both GUI and headless unit-test
// contexts.
[[nodiscard]] QString EffectiveAppVersion()
{
    const QString runtime_version = QCoreApplication::applicationVersion();
    if (!runtime_version.isEmpty()) {
        return runtime_version;
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
    const QSize map_dims = state.mapSize();
    if (map_dims.width() < 1 || map_dims.width() > State::kMaxMapDim
        || map_dims.height() < 1 || map_dims.height() > State::kMaxMapDim) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Map dimensions out of exportable VChar64 range"));
    }

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for atomic save: %2")
                .arg(filename, save_file.errorString()));
    }

    // Value-initialize to guarantee reserved[2] == {0, 0} (zero stack-padding leak).
    StateImport::VChar64Header header { };
    std::memcpy(header.id, "VChar", 5);
    header.version = 3U;

    for (int pen = 0; pen < 4; ++pen) {
        header.colors[pen] = static_cast<uint8_t>(
            static_cast<unsigned int>(state.getGlobalColorForPen(pen)) & 0x0FU);
    }

    header.num_chars = qToLittleEndian<quint16>(
        static_cast<quint16>(State::CHAR_BUFFER_SIZE / 8));

    const State::TileProperties tile_props = state.tileProperties();
    header.tile_width = static_cast<uint8_t>(tile_props.size.width());
    header.tile_height = static_cast<uint8_t>(tile_props.size.height());
    header.char_interleaved = static_cast<uint8_t>(tile_props.interleaved);
    header.vic_res = state.isMulticolorMode() ? 1U : 0U;
    header.color_mode = static_cast<uint8_t>(state.getForegroundColorMode());
    header.map_width = qToLittleEndian<quint16>(static_cast<quint16>(map_dims.width()));
    header.map_height = qToLittleEndian<quint16>(static_cast<quint16>(map_dims.height()));

    const State::ExportProperties export_props = state.exportProperties();
    header.address_charset = qToLittleEndian<quint16>(export_props.addresses[0]);
    header.address_map = qToLittleEndian<quint16>(export_props.addresses[1]);
    header.address_attribs = qToLittleEndian<quint16>(export_props.addresses[2]);
    header.export_features = export_props.features;
    header.export_format = static_cast<uint8_t>(export_props.format);
    header.keyboard_mapping = static_cast<uint8_t>(state.getKeyboardMapping());

    const State::CharsetBuffer& charset_buf = state.charset();
    const State::TileColorsBuffer& colors_buf = state.tileColors();
    const State::MapBuffer& map_buf = state.map();

    if (!WriteAllBytes(save_file, &header, sizeof(header))
        || !WriteAllBytes(save_file, charset_buf.data(), charset_buf.size())
        || !WriteAllBytes(save_file, colors_buf.data(), colors_buf.size())
        || !WriteAllBytes(save_file, map_buf.data(), map_buf.size())) {
        const QString error_message = save_file.errorString();
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing '%1': %2").arg(filename, error_message));
    }

    const qint64 total_bytes = static_cast<qint64>(
        sizeof(header) + charset_buf.size() + colors_buf.size() + map_buf.size());

    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit '%1': %2").arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(total_bytes);
}

vchar64::Result<qint64> StateExport::saveRaw(
    const QString& filename,
    const uint8_t* buffer,
    size_t buffer_size)
{
    if (buffer_size > 0U && buffer == nullptr) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to saveRaw"));
    }

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for RAW export: %2")
                .arg(filename, save_file.errorString()));
    }

    if (!WriteAllBytes(save_file, buffer, buffer_size)) {
        const QString error_message = save_file.errorString();
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing RAW file '%1': %2").arg(filename, error_message));
    }

    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit RAW file '%1': %2")
                .arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(static_cast<qint64>(buffer_size));
}

vchar64::Result<qint64> StateExport::savePRG(
    const QString& filename,
    const uint8_t* buffer,
    size_t buffer_size,
    uint16_t address)
{
    if (buffer_size > 0U && buffer == nullptr) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to savePRG"));
    }

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for PRG export: %2")
                .arg(filename, save_file.errorString()));
    }

    std::array<uint8_t, 2> le_address { };
    qToLittleEndian<quint16>(address, le_address.data());

    if (!WriteAllBytes(save_file, le_address.data(), le_address.size())
        || !WriteAllBytes(save_file, buffer, buffer_size)) {
        const QString error_message = save_file.errorString();
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while writing PRG file '%1': %2").arg(filename, error_message));
    }

    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit PRG file '%1': %2")
                .arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(static_cast<qint64>(le_address.size() + buffer_size));
}

vchar64::Result<qint64> StateExport::saveAsm(
    const QString& filename,
    const uint8_t* buffer,
    size_t buffer_size,
    const QString& label)
{
    if (buffer == nullptr && buffer_size > 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null buffer passed to saveAsm"));
    }

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for ASM export: %2")
                .arg(filename, save_file.errorString()));
    }

    const QString clean_label = label.isEmpty() ? QStringLiteral("vchar64_data") : label;

    QTextStream out(&save_file);
    out << "; Exported using VChar64 v" << EffectiveAppVersion() << "\n";
    out << "; Total bytes: " << buffer_size << "\n";
    out << clean_label << ":\n";

    for (size_t offset = 0; offset < buffer_size;) {
        out << ".byte ";
        size_t col = 0;
        for (; col < 16U && offset < buffer_size; ++col, ++offset) {
            if (col != 0U) {
                out << ",";
            }
            out << QStringLiteral("$%1").arg(buffer[offset], 2, 16, QLatin1Char('0'));
        }
        out << "\t; " << (offset - col) << "\n";
    }
    out << clean_label.toUpper() << "_COUNT = " << buffer_size << "\n";
    out.flush();

    if (out.status() != QTextStream::Ok || save_file.error() != QFileDevice::NoError) {
        const QString error_message = save_file.errorString();
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Stream error while writing ASM file '%1': %2")
                .arg(filename, error_message));
    }

    const qint64 bytes_written = save_file.pos();
    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit ASM file '%1': %2")
                .arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytes_written);
}

QString StateExport::sanitizeCIdentifier(const QString& raw_name)
{
    QString clean;
    clean.reserve(raw_name.size() + 1);

    for (const QChar ch : raw_name) {
        const char16_t code_unit = ch.unicode();
        const bool is_ascii_alnum_or_underscore = (code_unit >= u'A' && code_unit <= u'Z')
            || (code_unit >= u'a' && code_unit <= u'z')
            || (code_unit >= u'0' && code_unit <= u'9')
            || (code_unit == u'_');
        clean.append(is_ascii_alnum_or_underscore ? ch : QLatin1Char('_'));
    }

    if (clean.isEmpty()) {
        return QStringLiteral("vchar64_data");
    }

    const char16_t first_char = clean.front().unicode();
    if (first_char >= u'0' && first_char <= u'9') {
        clean.prepend(QLatin1Char('_'));
    }

    return clean;
}

vchar64::Result<qint64> StateExport::saveCSection(
    QTextStream& out,
    const uint8_t* buffer,
    size_t buffer_size,
    const QString& raw_identifier)
{
    if (buffer == nullptr || buffer_size == 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Cannot export empty C byte array"));
    }

    const QString identifier = sanitizeCIdentifier(raw_identifier);
    const QString macro_prefix = identifier.toUpper();

    // Emit flat ISO C17 `const uint8_t <id>[<N>]` without GNU/Clang `__attribute__`
    // syntax so 6502 C compilers (cc65, oscar64, vbcc, clang-mos) compile directly.
    out << "#define " << macro_prefix << "_COUNT " << buffer_size << "\n";
    out << "const uint8_t " << identifier << "[" << buffer_size << "] = {\n";

    for (size_t i = 0; i < buffer_size; ++i) {
        if ((i % 16U) == 0U) {
            out << "    ";
        }
        out << QStringLiteral("0x%1").arg(buffer[i], 2, 16, QLatin1Char('0'));
        if ((i + 1U) < buffer_size) {
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

    return vchar64::Result<qint64>::ok(static_cast<qint64>(buffer_size));
}

vchar64::Result<qint64> StateExport::saveC(
    const QString& filename,
    const uint8_t* buffer,
    size_t buffer_size,
    const QString& label)
{
    // Validate BEFORE opening QSaveFile so invalid inputs never touch the filesystem.
    if (buffer == nullptr || buffer_size == 0U) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Cannot export empty C byte array"));
    }

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for C export: %2")
                .arg(filename, save_file.errorString()));
    }

    QTextStream out(&save_file);
    out << "// Exported using VChar64 v" << EffectiveAppVersion() << "\n";
    out << "// Total bytes: " << buffer_size << "\n";
    out << "#include <stdint.h>\n\n";

    const vchar64::Result<qint64> section_result = saveCSection(out, buffer, buffer_size, label);
    if (!section_result.isOk() || save_file.error() != QFileDevice::NoError) {
        const QString error_message = section_result.isOk()
            ? save_file.errorString()
            : section_result.error().detail;
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            section_result.isOk() ? vchar64::ErrorCode::FileWriteFailed : section_result.error().code,
            error_message);
    }

    const qint64 bytes_written = save_file.pos();
    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit C file '%1': %2")
                .arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytes_written);
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

    QSaveFile save_file(filename);
    if (!save_file.open(QIODevice::WriteOnly)) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1' for PNG export: %2")
                .arg(filename, save_file.errorString()));
    }

    if (!image.save(&save_file, "PNG") || save_file.error() != QFileDevice::NoError) {
        const QString error_message = save_file.errorString();
        save_file.cancelWriting();
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileWriteFailed,
            QStringLiteral("Failed while encoding PNG '%1': %2").arg(filename, error_message));
    }

    const qint64 bytes_written = save_file.pos();
    if (!save_file.commit()) {
        return vchar64::Result<qint64>::err(
            vchar64::ErrorCode::FileCommitFailed,
            QStringLiteral("Failed to commit PNG '%1': %2")
                .arg(filename, save_file.errorString()));
    }

    return vchar64::Result<qint64>::ok(bytes_written);
}
