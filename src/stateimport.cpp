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

// Bounds-checked, endian-safe binary importers for StateImport (vchar64_core).
//
// Security and correctness invariants enforced across all parsers:
// - 256-entry clamping with stream seeks: CharPad CTM v4/v5 and VChar64 files
//   can declare up to 65,536 characters or tiles. ReadClampedCharset() and
//   ReadClampedTileColors() store at most 256 entries into fixed buffers and
//   seek() past excess records so subsequent map blocks remain stream-aligned
//   without overflowing _charset (2,048 B) or _tileColors (256 B).
// - Separate CTM header structs: loadCTM() peeks only the 4-byte "CTM" +
//   version prefix before reading CTMHeader4 (24 B) or CTMHeader5 (20 B)
//   separately, avoiding cross-struct pointer aliasing UB.
// - Hardened VICE .vsf parsing: Enforces moduleLength >= 22 to prevent infinite
//   zero-length module loops, heap-allocates 64 KiB / 256 KiB RAM structs, and
//   accepts both 35-byte CIA2 v1.0 and 39-byte CIA2 v1.1 $DD00 VIC bank
//   payloads.

#include "stateimport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <QFile>
#include <QIODevice>
#include <QSize>
#include <QString>
#include <QtEndian>

#include "core/result.h"
#include "state.h"

namespace {

constexpr std::array<char, 19> kViceHeaderMagic = {
    'V', 'I', 'C', 'E', ' ', 'S', 'n', 'a', 'p', 's', 'h', 'o', 't', ' ', 'F', 'i', 'l', 'e', '\032'
};
constexpr std::array<char, 13> kViceVersionMagic = {
    'V', 'I', 'C', 'E', ' ', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\032'
};
constexpr char kViceC64Mem[] = "C64MEM";
constexpr char kViceC128Mem[] = "C128MEM";
constexpr char kViceVicII[] = "VIC-II";
constexpr char kViceCia2[] = "CIA2";
constexpr char kSeuckSignature[] = "PRESS FIRE TO COMMENCE SUPADEATH";

// CIA2 v1.0 snapshots omit the trailing 4-byte v1.1 tastate/tbstate fields.
constexpr qint64 kMinCia2PayloadSize = static_cast<qint64>(
    sizeof(StateImport::VICESnapshotCIA2) - 4U);

// Opens `filename` read-only and invokes `parser(file)`, returning
// FileOpenFailed if the file cannot be opened.
template <typename ResultT, typename ParserFn>
vchar64::Result<ResultT> OpenAndParseFile(const QString& filename, ParserFn&& parser)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<ResultT>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return std::forward<ParserFn>(parser)(file);
}

// Ensures `device` is open for reading without reopening already-open QBuffer
// streams.
[[nodiscard]] bool EnsureOpenReadOnly(QIODevice& device)
{
    if (device.isOpen()) {
        return device.isReadable();
    }
    return device.open(QIODevice::ReadOnly);
}

// Reads exactly `byte_count` bytes into `dst`, returning false on EOF or short
// reads.
[[nodiscard]] bool ReadExactBytes(QIODevice& device, void* dst, qint64 byte_count)
{
    if (byte_count < 0) {
        return false;
    }
    if (byte_count == 0) {
        return true;
    }
    return device.read(static_cast<char*>(dst), byte_count) == byte_count;
}

// Advances `device` by `byte_count` bytes with overflow and EOF bounds
// validation.
[[nodiscard]] bool SkipExactBytes(QIODevice& device, qint64 byte_count)
{
    if (byte_count < 0) {
        return false;
    }
    if (byte_count == 0) {
        return true;
    }
    const qint64 target_pos = device.pos() + byte_count;
    if (target_pos < device.pos() || target_pos > device.size()) {
        return false;
    }
    return device.seek(target_pos);
}

// Reads up to 256 8-byte glyphs into `dst_charset` and seeks past any excess
// characters (> 256), recording a non-fatal warning on truncation.
[[nodiscard]] bool ReadClampedCharset(
    QIODevice& device,
    int num_chars_in_file,
    State::CharsetBuffer& dst_charset,
    vchar64::ImportReport& report,
    qint64& total_bytes_consumed)
{
    if (num_chars_in_file < 0) {
        return false;
    }

    dst_charset.fill(0U);
    const int chars_stored = std::min(num_chars_in_file, State::CHAR_BUFFER_SIZE / 8);
    const qint64 bytes_to_read = static_cast<qint64>(chars_stored) * 8LL;
    if (!ReadExactBytes(device, dst_charset.data(), bytes_to_read)) {
        return false;
    }
    total_bytes_consumed += bytes_to_read;

    if (num_chars_in_file > chars_stored) {
        const qint64 excess_bytes = static_cast<qint64>(num_chars_in_file - chars_stored) * 8LL;
        if (!SkipExactBytes(device, excess_bytes)) {
            return false;
        }
        total_bytes_consumed += excess_bytes;
        report.warnings.append(
            QStringLiteral("Charset clamped from %1 characters to 256 characters.")
                .arg(num_chars_in_file));
    }

    return true;
}

// Reads up to 256 4-bit Color RAM nibbles into `dst_colors` and seeks past any
// excess entries, recording a non-fatal warning on truncation.
[[nodiscard]] bool ReadClampedTileColors(
    QIODevice& device,
    int count_in_file,
    State::TileColorsBuffer& dst_colors,
    vchar64::ImportReport& report,
    const QString& label,
    qint64& total_bytes_consumed)
{
    if (count_in_file < 0) {
        return false;
    }

    const int stored = std::min(count_in_file, State::TILE_COLORS_BUFFER_SIZE);
    std::array<uint8_t, State::TILE_COLORS_BUFFER_SIZE> raw_attrs { };
    if (!ReadExactBytes(device, raw_attrs.data(), static_cast<qint64>(stored))) {
        return false;
    }
    total_bytes_consumed += static_cast<qint64>(stored);

    for (int i = 0; i < stored; ++i) {
        const size_t idx = static_cast<size_t>(i);
        dst_colors[idx] = static_cast<uint8_t>(raw_attrs[idx] & 0x0FU);
    }

    if (count_in_file > stored) {
        const qint64 excess = static_cast<qint64>(count_in_file - stored);
        if (!SkipExactBytes(device, excess)) {
            return false;
        }
        total_bytes_consumed += excess;
        report.warnings.append(
            QStringLiteral("Clamped %1 block from %2 entries to 256 entries.")
                .arg(label)
                .arg(count_in_file));
    }

    return true;
}

} // namespace

vchar64::Result<vchar64::ImportReport> StateImport::loadRaw(State& state, QIODevice& device)
{
    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open raw charset device for reading"));
    }

    const qint64 remaining = device.size() - device.pos();
    if (remaining < 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Invalid stream offset in raw charset device"));
    }
    if (remaining == 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Raw charset file is empty (0 bytes)"));
    }

    vchar64::ImportReport report;
    if ((remaining % 8LL) != 0LL) {
        report.warnings.append(
            QStringLiteral("Warning: raw file size (%1 bytes) is not a multiple of 8.")
                .arg(remaining));
    }
    if (remaining > State::CHAR_BUFFER_SIZE) {
        report.warnings.append(
            QStringLiteral("Warning: raw file size (%1 bytes) truncated to 2048 bytes.")
                .arg(remaining));
    }

    const qint64 bytes_to_read = std::min<qint64>(remaining, State::CHAR_BUFFER_SIZE);
    State::CharsetBuffer staged_charset { };
    if (!ReadExactBytes(device, staged_charset.data(), bytes_to_read)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read raw charset payload"));
    }

    state.setCharsetBytes(staged_charset.data(), staged_charset.size());
    report.bytesRead = bytes_to_read;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadRaw(
    State& state,
    const QString& filename)
{
    return OpenAndParseFile<vchar64::ImportReport>(filename, [&state](QFile& file) {
        return loadRaw(state, file);
    });
}

vchar64::Result<vchar64::ImportReport> StateImport::loadPRG(
    State& state,
    QIODevice& device,
    quint16* out_address)
{
    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open PRG device for reading"));
    }

    const qint64 remaining = device.size() - device.pos();
    if (remaining < 10LL) { // 2-byte load address + at least one 8-byte glyph
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("PRG file is smaller than minimum 10 bytes (%1 bytes)").arg(remaining));
    }

    std::array<uint8_t, 2> addr_bytes { };
    if (!ReadExactBytes(device, addr_bytes.data(), static_cast<qint64>(addr_bytes.size()))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read 2-byte PRG load address"));
    }

    const quint16 decoded_address = qFromLittleEndian<quint16>(addr_bytes.data());
    vchar64::Result<vchar64::ImportReport> raw_result = loadRaw(state, device);
    if (!raw_result.isOk()) {
        return raw_result;
    }

    if (out_address != nullptr) {
        *out_address = decoded_address;
    }
    return raw_result;
}

vchar64::Result<vchar64::ImportReport> StateImport::loadPRG(
    State& state,
    const QString& filename,
    quint16* out_address)
{
    return OpenAndParseFile<vchar64::ImportReport>(filename, [&state, out_address](QFile& file) {
        return loadPRG(state, file, out_address);
    });
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM(State& state, QIODevice& device)
{
    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open CTM device for reading"));
    }

    const qint64 start_pos = device.pos();
    const qint64 remaining = device.size() - start_pos;
    if (remaining < static_cast<qint64>(sizeof(CTMHeader5))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("CTM file is smaller than minimum 20-byte header (%1 bytes)").arg(remaining));
    }

    std::array<uint8_t, 4> prefix { };
    if (!ReadExactBytes(device, prefix.data(), static_cast<qint64>(prefix.size()))
        || !device.seek(start_pos)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to inspect CTM magic prefix"));
    }

    if (prefix[0] != 'C' || prefix[1] != 'T' || prefix[2] != 'M') {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Missing 'CTM' magic header"));
    }

    const uint8_t version = prefix[3];
    if (version == 4U) {
        return loadCTM4(state, device);
    }
    if (version == 5U) {
        return loadCTM5(state, device);
    }

    return vchar64::Result<vchar64::ImportReport>::err(
        vchar64::ErrorCode::UnsupportedVersion,
        QStringLiteral("Unsupported CTM version %1").arg(version));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM(
    State& state,
    const QString& filename)
{
    return OpenAndParseFile<vchar64::ImportReport>(filename, [&state](QFile& file) {
        return loadCTM(state, file);
    });
}

vchar64::Result<vchar64::ImportReport> StateImport::loadVChar64(State& state, QIODevice& device)
{
    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open VChar64 project device for reading"));
    }

    const qint64 remaining = device.size() - device.pos();
    if (remaining < static_cast<qint64>(sizeof(VChar64Header))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("VChar64 file smaller than 32-byte header (%1 bytes)").arg(remaining));
    }

    VChar64Header header { };
    if (!ReadExactBytes(device, &header, static_cast<qint64>(sizeof(header)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read VChar64 header"));
    }

    if (std::memcmp(header.id, "VChar", 5) != 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Missing 'VChar' magic header"));
    }

    if (header.version < 1U || header.version > 3U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnsupportedVersion,
            QStringLiteral("Unsupported VChar64 version %1").arg(header.version));
    }

    const int tile_width = static_cast<int>(header.tile_width);
    const int tile_height = static_cast<int>(header.tile_height);
    const int interleaved = (header.char_interleaved == 0U && tile_width == 1 && tile_height == 1)
        ? 1
        : static_cast<int>(header.char_interleaved);
    if (tile_width < 1 || tile_width > State::MAX_TILE_WIDTH
        || tile_height < 1 || tile_height > State::MAX_TILE_HEIGHT
        || (tile_width * tile_height) > 256
        || interleaved < 1 || interleaved > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid VChar64 tile properties %1x%2 (interleaved=%3)")
                .arg(tile_width)
                .arg(tile_height)
                .arg(header.char_interleaved));
    }

    vchar64::ImportReport report;
    qint64 bytes_consumed = static_cast<qint64>(sizeof(header));

    const int num_chars = static_cast<int>(qFromLittleEndian<quint16>(header.num_chars));
    State::CharsetBuffer staged_charset { };
    if (!ReadClampedCharset(device, num_chars, staged_charset, report, bytes_consumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated VChar64 charset payload"));
    }

    State::ForegroundColorMode staged_fg_mode = State::FOREGROUND_COLOR_GLOBAL;
    State::TileColorsBuffer staged_colors { };
    staged_colors.fill(State::TILE_COLORS_DEFAULT);
    QSize staged_map_size(40, 25);
    std::vector<uint8_t> staged_map = (state.mapSize() == QSize(40, 25) && state.map().size() == 40U * 25U)
        ? state.map()
        : std::vector<uint8_t>(40U * 25U, 0x20U);

    if (header.version >= 2U) {
        if (header.color_mode > 1U) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Invalid VChar64 color mode %1").arg(header.color_mode));
        }
        staged_fg_mode = (header.color_mode == 1U)
            ? State::FOREGROUND_COLOR_PER_TILE
            : State::FOREGROUND_COLOR_GLOBAL;

        const int map_width = static_cast<int>(qFromLittleEndian<quint16>(header.map_width));
        const int map_height = static_cast<int>(qFromLittleEndian<quint16>(header.map_height));
        if (map_width < 1 || map_width > State::kMaxMapDim
            || map_height < 1 || map_height > State::kMaxMapDim) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("Invalid VChar64 map dimensions %1x%2")
                    .arg(map_width)
                    .arg(map_height));
        }

        const size_t map_cell_count = static_cast<size_t>(map_width) * static_cast<size_t>(map_height);
        if (map_cell_count > State::kMaxMapCells) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("VChar64 map cell count exceeds maximum"));
        }

        if (!ReadExactBytes(device, staged_colors.data(), static_cast<qint64>(staged_colors.size()))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated VChar64 tile colors payload"));
        }
        bytes_consumed += static_cast<qint64>(staged_colors.size());
        for (uint8_t& color_entry : staged_colors) {
            color_entry &= 0x0FU;
        }

        staged_map_size = QSize(map_width, map_height);
        staged_map.assign(map_cell_count, 0U);
        if (!ReadExactBytes(device, staged_map.data(), static_cast<qint64>(map_cell_count))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated VChar64 map payload"));
        }
        bytes_consumed += static_cast<qint64>(map_cell_count);
    }

    State::ExportProperties staged_export_props { };
    State::KeyboardMapping staged_keyboard_mapping = State::KEYBOARD_MAPPING_C64;

    if (header.version == 3U) {
        if (header.export_format > static_cast<uint8_t>(State::EXPORT_FORMAT_C)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Invalid VChar64 export format %1").arg(header.export_format));
        }

        staged_export_props.addresses[0] = qFromLittleEndian<quint16>(header.address_charset);
        staged_export_props.addresses[1] = qFromLittleEndian<quint16>(header.address_map);
        staged_export_props.addresses[2] = qFromLittleEndian<quint16>(header.address_attribs);
        staged_export_props.format = static_cast<State::ExportFormat>(header.export_format);
        staged_export_props.features = static_cast<uint8_t>(
            header.export_features & State::EXPORT_FEATURE_ALL);

        if (header.keyboard_mapping > static_cast<uint8_t>(State::KEYBOARD_MAPPING_ATARI8)) {
            report.warnings.append(
                QStringLiteral("Clamped unrecognized VChar64 keyboard_mapping (%1) to C64.")
                    .arg(header.keyboard_mapping));
            staged_keyboard_mapping = State::KEYBOARD_MAPPING_C64;
        } else {
            staged_keyboard_mapping = static_cast<State::KeyboardMapping>(header.keyboard_mapping);
        }
    }

    const State::TileProperties tile_props { QSize(tile_width, tile_height), interleaved };
    if (!state.setTilePropertiesDirect(tile_props)
        || !state.assignImportedBuffers(
            staged_charset,
            staged_colors,
            staged_map_size,
            std::move(staged_map))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit VChar64 buffers into State"));
    }

    for (int pen = 0; pen < 4; ++pen) {
        state.setPenColorDirect(pen, static_cast<uint8_t>(header.colors[pen] & 0x0FU));
    }
    state.setMulticolorModeDirect(header.vic_res != 0U);
    state.setForegroundColorModeDirect(staged_fg_mode);
    state.setExportPropertiesDirect(staged_export_props);
    state.setKeyboardMappingDirect(staged_keyboard_mapping);

    report.bytesRead = bytes_consumed;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadVChar64(
    State& state,
    const QString& filename)
{
    return OpenAndParseFile<vchar64::ImportReport>(filename, [&state](QFile& file) {
        return loadVChar64(state, file);
    });
}

vchar64::Result<KoalaBitmapData> StateImport::loadKoala(QIODevice& device)
{
    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open Koala device for reading"));
    }

    const qint64 remaining = device.size() - device.pos();
    if (remaining < KoalaBitmapData::kMinFileSize) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Koala file smaller than 10,002 bytes (%1 bytes)").arg(remaining));
    }
    if (remaining > KoalaBitmapData::kMaxFileSize) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Koala file exceeds 10,003 bytes (%1 bytes)").arg(remaining));
    }

    KoalaBitmapData data { };
    std::array<uint8_t, 2> addr_bytes { };
    if (!ReadExactBytes(device, addr_bytes.data(), static_cast<qint64>(addr_bytes.size()))
        || !ReadExactBytes(device, data.bitmap.data(), static_cast<qint64>(data.bitmap.size()))
        || !ReadExactBytes(device, data.screenRam.data(), static_cast<qint64>(data.screenRam.size()))
        || !ReadExactBytes(device, data.colorRam.data(), static_cast<qint64>(data.colorRam.size()))) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read Koala payload segments"));
    }

    data.loadAddress = qFromLittleEndian<quint16>(addr_bytes.data());
    for (uint8_t& color_entry : data.colorRam) {
        color_entry &= 0x0FU;
    }

    if (remaining == KoalaBitmapData::kMaxFileSize) {
        uint8_t raw_bg = 0U;
        if (!ReadExactBytes(device, &raw_bg, 1)) {
            return vchar64::Result<KoalaBitmapData>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Failed to read trailing Koala background byte"));
        }
        data.backgroundColor = static_cast<uint8_t>(raw_bg & 0x0FU);
    } else {
        data.backgroundColor = 0U;
    }

    return vchar64::Result<KoalaBitmapData>::ok(data);
}

vchar64::Result<KoalaBitmapData> StateImport::loadKoala(const QString& filename)
{
    return OpenAndParseFile<KoalaBitmapData>(filename, [](QFile& file) {
        return loadKoala(file);
    });
}

vchar64::Result<vchar64::ImportReport> StateImport::parseVICESnapshot(
    QIODevice& device,
    uint8_t* buffer_64k,
    uint16_t* out_charset_address,
    uint16_t* out_screen_ram_address,
    uint8_t* out_color_ram_buf,
    uint8_t* out_vic_registers_buf)
{
    if (buffer_64k == nullptr
        || out_charset_address == nullptr
        || out_screen_ram_address == nullptr
        || out_color_ram_buf == nullptr
        || out_vic_registers_buf == nullptr) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null output buffer passed to parseVICESnapshot"));
    }

    if (!EnsureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open VICE snapshot device for reading"));
    }

    const qint64 file_size = device.size();
    if (file_size < static_cast<qint64>(sizeof(VICESnapshotHeader))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("VICE snapshot smaller than 37-byte header"));
    }

    if (!device.seek(0)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to seek to beginning of VICE snapshot"));
    }

    VICESnapshotHeader header { };
    if (!ReadExactBytes(device, &header, static_cast<qint64>(sizeof(header)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read VICE snapshot header"));
    }

    if (std::memcmp(header.id, kViceHeaderMagic.data(), kViceHeaderMagic.size()) != 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Invalid VICE snapshot header magic"));
    }

    // Optional 21-byte VICE Version block immediately following the main header.
    const qint64 pos_after_header = device.pos();
    if (file_size - pos_after_header >= static_cast<qint64>(sizeof(VICESnapshotVersion))) {
        VICESnapshotVersion version { };
        if (!ReadExactBytes(device, &version, static_cast<qint64>(sizeof(version)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to inspect VICE version header"));
        }
        if (std::memcmp(version.id, kViceVersionMagic.data(), kViceVersionMagic.size()) != 0) {
            if (!device.seek(pos_after_header)) {
                return vchar64::Result<vchar64::ImportReport>::err(
                    vchar64::ErrorCode::FileReadFailed,
                    QStringLiteral("Failed to rewind after optional VICE version check"));
            }
        }
    }

    qint64 module_start_offset = device.pos();
    qint64 c64mem_offset = -1;
    qint64 c64mem_payload_size = 0;
    qint64 c128mem_offset = -1;
    qint64 c128mem_payload_size = 0;
    qint64 cia2_offset = -1;
    qint64 cia2_payload_size = 0;
    qint64 vic2_offset = -1;
    qint64 vic2_payload_size = 0;

    *out_charset_address = 0U;
    *out_screen_ram_address = 0x0400U;

    while (module_start_offset < file_size) {
        if (file_size - module_start_offset < static_cast<qint64>(sizeof(VICESnapshotModule))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE module header at offset %1").arg(module_start_offset));
        }

        VICESnapshotModule module { };
        if (!ReadExactBytes(device, &module, static_cast<qint64>(sizeof(module)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE module header"));
        }

        const uint32_t module_length = qFromLittleEndian<quint32>(module.length);
        if (module_length < sizeof(VICESnapshotModule)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Corrupt VICE module length (%1 < %2)")
                    .arg(module_length)
                    .arg(sizeof(VICESnapshotModule)));
        }

        const qint64 next_module_offset = module_start_offset + static_cast<qint64>(module_length);
        if (next_module_offset > file_size) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE module payload extends past EOF"));
        }

        const qint64 payload_offset = device.pos();
        const qint64 payload_size = next_module_offset - payload_offset;

        if (c64mem_offset == -1
            && std::memcmp(module.moduleName, kViceC64Mem, sizeof(kViceC64Mem)) == 0) {
            c64mem_offset = payload_offset;
            c64mem_payload_size = payload_size;
        } else if (c128mem_offset == -1
            && std::memcmp(module.moduleName, kViceC128Mem, sizeof(kViceC128Mem)) == 0) {
            c128mem_offset = payload_offset;
            c128mem_payload_size = payload_size;
        } else if (cia2_offset == -1
            && std::memcmp(module.moduleName, kViceCia2, sizeof(kViceCia2)) == 0) {
            cia2_offset = payload_offset;
            cia2_payload_size = payload_size;
        } else if (vic2_offset == -1
            && std::memcmp(module.moduleName, kViceVicII, sizeof(kViceVicII)) == 0) {
            vic2_offset = payload_offset;
            vic2_payload_size = payload_size;
        }

        module_start_offset = next_module_offset;
        if (module_start_offset < file_size && !device.seek(module_start_offset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to seek to next VICE module"));
        }
    }

    if ((c64mem_offset == -1 && c128mem_offset == -1) || cia2_offset == -1 || vic2_offset == -1) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Missing required VICE C64MEM/C128MEM, CIA2, or VIC-II module"));
    }

    // Heap-allocate the 64 KB / 256 KB RAM buffers only when needed so
    // parseVICESnapshot never places 320 KB on the call stack.
    if (c64mem_offset != -1) {
        if (c64mem_payload_size < static_cast<qint64>(sizeof(VICESnapshotC64Mem))
            || !device.seek(c64mem_offset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE C64MEM module"));
        }
        auto c64mem = std::make_unique<VICESnapshotC64Mem>();
        if (!ReadExactBytes(device, c64mem.get(), static_cast<qint64>(sizeof(VICESnapshotC64Mem)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE C64MEM payload"));
        }
        std::memcpy(buffer_64k, c64mem->ram, sizeof(c64mem->ram));
    } else {
        if (c128mem_payload_size < static_cast<qint64>(sizeof(VICESnapshotC128Mem))
            || !device.seek(c128mem_offset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE C128MEM module"));
        }
        auto c128mem = std::make_unique<VICESnapshotC128Mem>();
        if (!ReadExactBytes(device, c128mem.get(), static_cast<qint64>(sizeof(VICESnapshotC128Mem)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE C128MEM payload"));
        }
        std::memcpy(buffer_64k, c128mem->ram, 65536U);
    }

    // CIA2 port A (`ora`, byte 0 of CIA2 payload) controls the active 16 KB
    // VIC-II bank ($DD00). Require at least the 35-byte v1.0 prefix.
    if (cia2_payload_size < kMinCia2PayloadSize || !device.seek(cia2_offset)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Truncated VICE CIA2 module"));
    }
    VICESnapshotCIA2 cia2 { };
    const qint64 cia2_bytes_to_read = std::min<qint64>(
        cia2_payload_size,
        static_cast<qint64>(sizeof(VICESnapshotCIA2)));
    if (!ReadExactBytes(device, &cia2, cia2_bytes_to_read)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Failed to read VICE CIA2 payload"));
    }
    const uint16_t bank_addr = static_cast<uint16_t>((3U - (cia2.ora & 0x03U)) * 16384U);

    if (vic2_payload_size < static_cast<qint64>(sizeof(VICESnapshotVICII))
        || !device.seek(vic2_offset)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Truncated VICE VIC-II module"));
    }
    VICESnapshotVICII vic2 { };
    if (!ReadExactBytes(device, &vic2, static_cast<qint64>(sizeof(VICESnapshotVICII)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Failed to read VICE VIC-II payload"));
    }

    if (std::memcmp(kSeuckSignature, &buffer_64k[0x3FDC], sizeof(kSeuckSignature) - 1U) == 0) {
        *out_charset_address = 0xF800U;
        *out_screen_ram_address = 0xEC00U;
    } else {
        const uint16_t charset_offset = static_cast<uint16_t>(
            ((vic2.registers[0x18] & 0x0EU) >> 1U) * 2048U);
        *out_charset_address = static_cast<uint16_t>(bank_addr + charset_offset);

        const uint16_t screen_offset = static_cast<uint16_t>((vic2.registers[0x18] >> 4U) * 1024U);
        *out_screen_ram_address = static_cast<uint16_t>(bank_addr + screen_offset);
    }

    for (size_t i = 0; i < sizeof(vic2.color_ram); ++i) {
        out_color_ram_buf[i] = static_cast<uint8_t>(vic2.color_ram[i] & 0x0FU);
    }
    std::memcpy(out_vic_registers_buf, vic2.registers, sizeof(vic2.registers));

    vchar64::ImportReport report;
    report.bytesRead = file_size;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::parseVICESnapshot(
    const QString& filename,
    uint8_t* buffer_64k,
    uint16_t* out_charset_address,
    uint16_t* out_screen_ram_address,
    uint8_t* out_color_ram_buf,
    uint8_t* out_vic_registers_buf)
{
    return OpenAndParseFile<vchar64::ImportReport>(
        filename,
        [buffer_64k,
            out_charset_address,
            out_screen_ram_address,
            out_color_ram_buf,
            out_vic_registers_buf](QFile& file) {
            return parseVICESnapshot(
                file,
                buffer_64k,
                out_charset_address,
                out_screen_ram_address,
                out_color_ram_buf,
                out_vic_registers_buf);
        });
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM4(State& state, QIODevice& device)
{
    const qint64 remaining = device.size() - device.pos();
    if (remaining < static_cast<qint64>(sizeof(CTMHeader4))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("CTM v4 file smaller than 24-byte header"));
    }

    CTMHeader4 header { };
    if (!ReadExactBytes(device, &header, static_cast<qint64>(sizeof(header)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read CTM v4 header"));
    }

    if (header.expanded == 0U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnexpandedCtmUnsupported,
            QStringLiteral("Unexpanded CTM v4 files are not supported"));
    }

    if (header.color_mode > 2U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unsupported CTM v4 color mode %1").arg(header.color_mode));
    }

    const int tile_width = static_cast<int>(header.tile_width);
    const int tile_height = static_cast<int>(header.tile_height);
    if (tile_width < 1 || tile_width > State::MAX_TILE_WIDTH
        || tile_height < 1 || tile_height > State::MAX_TILE_HEIGHT
        || (tile_width * tile_height) > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v4 tile dimensions %1x%2")
                .arg(tile_width)
                .arg(tile_height));
    }

    const int map_width = static_cast<int>(qFromLittleEndian<quint16>(header.map_width));
    const int map_height = static_cast<int>(qFromLittleEndian<quint16>(header.map_height));
    if (map_width < 1 || map_width > State::kMaxMapDim
        || map_height < 1 || map_height > State::kMaxMapDim) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v4 map dimensions %1x%2").arg(map_width).arg(map_height));
    }

    const size_t map_cell_count = static_cast<size_t>(map_width) * static_cast<size_t>(map_height);
    if (map_cell_count > State::kMaxMapCells) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("CTM v4 map cell count exceeds maximum"));
    }

    const int num_chars = static_cast<int>(qFromLittleEndian<quint16>(header.num_chars)) + 1;
    const int num_tiles = static_cast<int>(header.num_tiles) + 1;

    vchar64::ImportReport report;
    qint64 bytes_consumed = static_cast<qint64>(sizeof(header));

    State::CharsetBuffer staged_charset { };
    if (!ReadClampedCharset(device, num_chars, staged_charset, report, bytes_consumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v4 character data"));
    }

    State::TileColorsBuffer staged_colors { };
    staged_colors.fill(State::TILE_COLORS_DEFAULT);

    const qint64 cell_attribs_size = static_cast<qint64>(num_tiles)
        * static_cast<qint64>(tile_width)
        * static_cast<qint64>(tile_height);

    if (header.color_mode == 0U) {
        if (!SkipExactBytes(device, static_cast<qint64>(num_chars))
            || !SkipExactBytes(device, cell_attribs_size)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 attribute blocks"));
        }
        bytes_consumed += static_cast<qint64>(num_chars) + cell_attribs_size;
    } else if (header.color_mode == 1U) {
        if (!SkipExactBytes(device, static_cast<qint64>(num_chars))
            || !SkipExactBytes(device, cell_attribs_size)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 char/cell attribute blocks"));
        }
        bytes_consumed += static_cast<qint64>(num_chars) + cell_attribs_size;

        if (!ReadClampedTileColors(
                device,
                num_tiles,
                staged_colors,
                report,
                QStringLiteral("CTM v4 tile attribute"),
                bytes_consumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 tile attribute block"));
        }
    } else {
        if (!ReadClampedTileColors(
                device,
                num_chars,
                staged_colors,
                report,
                QStringLiteral("CTM v4 char attribute"),
                bytes_consumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 char attribute block"));
        }
        if (!SkipExactBytes(device, cell_attribs_size)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 cell attribute block"));
        }
        bytes_consumed += cell_attribs_size;
    }

    std::vector<uint8_t> staged_map(map_cell_count, 0U);
    if (!ReadExactBytes(device, staged_map.data(), static_cast<qint64>(map_cell_count))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v4 map data"));
    }
    bytes_consumed += static_cast<qint64>(map_cell_count);

    const State::TileProperties tile_props { QSize(tile_width, tile_height), 1 };
    if (!state.setTilePropertiesDirect(tile_props)
        || !state.assignImportedBuffers(
            staged_charset,
            staged_colors,
            QSize(map_width, map_height),
            std::move(staged_map))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit CTM v4 buffers into State"));
    }

    for (int pen = 0; pen < 4; ++pen) {
        state.setPenColorDirect(pen, static_cast<uint8_t>(header.colors[pen] & 0x0FU));
    }
    state.setMulticolorModeDirect(header.vic_res != 0U);
    state.setForegroundColorModeDirect(
        header.color_mode != 0U ? State::FOREGROUND_COLOR_PER_TILE : State::FOREGROUND_COLOR_GLOBAL);

    report.bytesRead = bytes_consumed;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM5(State& state, QIODevice& device)
{
    const qint64 remaining = device.size() - device.pos();
    if (remaining < static_cast<qint64>(sizeof(CTMHeader5))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("CTM v5 file smaller than 20-byte header"));
    }

    CTMHeader5 header { };
    if (!ReadExactBytes(device, &header, static_cast<qint64>(sizeof(header)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read CTM v5 header"));
    }

    // Must be expanded (bit 1 == 1) whenever Tile System is enabled (bit 0 == 1).
    if ((header.flags & 0x03U) == 0x01U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnexpandedCtmUnsupported,
            QStringLiteral("Unexpanded CTM v5 files are not supported"));
    }

    if (header.color_mode > 2U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unsupported CTM v5 color mode %1").arg(header.color_mode));
    }

    // CharPad v5 files with Tile System disabled may emit 0 for tile dimensions.
    const int tile_width = std::max<int>(static_cast<int>(header.tile_width), 1);
    const int tile_height = std::max<int>(static_cast<int>(header.tile_height), 1);
    if (tile_width > State::MAX_TILE_WIDTH
        || tile_height > State::MAX_TILE_HEIGHT
        || (tile_width * tile_height) > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v5 tile dimensions %1x%2")
                .arg(tile_width)
                .arg(tile_height));
    }

    const int map_width = static_cast<int>(qFromLittleEndian<quint16>(header.map_width));
    const int map_height = static_cast<int>(qFromLittleEndian<quint16>(header.map_height));
    if (map_width < 1 || map_width > State::kMaxMapDim
        || map_height < 1 || map_height > State::kMaxMapDim) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v5 map dimensions %1x%2").arg(map_width).arg(map_height));
    }

    const size_t map_cell_count = static_cast<size_t>(map_width) * static_cast<size_t>(map_height);
    if (map_cell_count > State::kMaxMapCells) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("CTM v5 map cell count exceeds maximum"));
    }

    const int num_chars = static_cast<int>(qFromLittleEndian<quint16>(header.num_chars)) + 1;
    const int num_tiles = static_cast<int>(qFromLittleEndian<quint16>(header.num_tiles)) + 1;

    vchar64::ImportReport report;
    qint64 bytes_consumed = static_cast<qint64>(sizeof(header));

    State::CharsetBuffer staged_charset { };
    if (!ReadClampedCharset(device, num_chars, staged_charset, report, bytes_consumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v5 character data"));
    }

    State::TileColorsBuffer staged_colors { };
    staged_colors.fill(State::TILE_COLORS_DEFAULT);

    if (header.color_mode == 2U) {
        if (!ReadClampedTileColors(
                device,
                num_chars,
                staged_colors,
                report,
                QStringLiteral("CTM v5 char attribute"),
                bytes_consumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v5 char attribute block"));
        }
    } else {
        if (!SkipExactBytes(device, static_cast<qint64>(num_chars))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v5 char attribute block"));
        }
        bytes_consumed += static_cast<qint64>(num_chars);

        if (header.color_mode == 1U) {
            if (!ReadClampedTileColors(
                    device,
                    num_tiles,
                    staged_colors,
                    report,
                    QStringLiteral("CTM v5 tile color"),
                    bytes_consumed)) {
                return vchar64::Result<vchar64::ImportReport>::err(
                    vchar64::ErrorCode::FileReadFailed,
                    QStringLiteral("Truncated CTM v5 tile color block"));
            }
        }
    }

    std::vector<uint16_t> raw_map(map_cell_count, 0U);
    const qint64 map_byte_count = static_cast<qint64>(map_cell_count * sizeof(uint16_t));
    if (!ReadExactBytes(device, raw_map.data(), map_byte_count)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v5 16-bit map data"));
    }
    bytes_consumed += map_byte_count;

    std::vector<uint8_t> staged_map(map_cell_count, 0U);
    size_t clamped_map_cells = 0;
    for (size_t i = 0; i < map_cell_count; ++i) {
        const uint16_t tile_index = qFromLittleEndian<quint16>(raw_map[i]);
        if (tile_index > 255U) {
            ++clamped_map_cells;
        }
        staged_map[i] = static_cast<uint8_t>(tile_index & 0xFFU);
    }

    if (clamped_map_cells > 0U) {
        report.warnings.append(
            QStringLiteral("Clamped %1 CTM v5 map cell(s) exceeding tile index 255.")
                .arg(clamped_map_cells));
    }

    const State::TileProperties tile_props { QSize(tile_width, tile_height), 1 };
    if (!state.setTilePropertiesDirect(tile_props)
        || !state.assignImportedBuffers(
            staged_charset,
            staged_colors,
            QSize(map_width, map_height),
            std::move(staged_map))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit CTM v5 buffers into State"));
    }

    for (int pen = 0; pen < 4; ++pen) {
        state.setPenColorDirect(pen, static_cast<uint8_t>(header.colors[pen] & 0x0FU));
    }
    state.setMulticolorModeDirect((header.flags & 0x04U) != 0U);
    state.setForegroundColorModeDirect(
        header.color_mode != 0U ? State::FOREGROUND_COLOR_PER_TILE : State::FOREGROUND_COLOR_GLOBAL);

    report.bytesRead = bytes_consumed;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}
