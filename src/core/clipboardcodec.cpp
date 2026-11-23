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

// Implements the "VC64CLIP" v2 little-endian clipboard wire format.
//
// Wire layout (all integers little-endian QDataStream::Qt_6_0):
// - Bytes [0..7]:   ASCII magic "VC64CLIP" (8 bytes).
// - Bytes [8..11]:  uint32_t format version (2).
// - Bytes [12..47]: Nine int32_t geometry fields (type, offset, blockSize,
//                   skip, count, sourceRowWidth, tw, th, interleaved).
// - Bytes [48..51]: uint32_t payload byte_count.
// - Bytes [52..]:   For CHARS/TILES, 2,304 contiguous bytes (2,048 B charset +
//                   256 B tile colors); for MAP, byte_count tile-index bytes.

#include "core/clipboardcodec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <QDataStream>
#include <QIODevice>
#include <QSize>
#include <QString>

namespace {

constexpr std::array<char, 8> kMagic = { 'V', 'C', '6', '4', 'C', 'L', 'I', 'P' };
constexpr uint32_t kCharsetAndColorsBytes = static_cast<uint32_t>(
    State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE);

// Validates map selection geometry and reads `byte_count` cell indices from `stream` into
// `payload`, normalizing `payload.sourceRowWidth`. Returns an error on invalid dimensions,
// buffer size mismatches, or stream read failures.
[[nodiscard]] vchar64::Result<void> ReadMapPayload(
    QDataStream& stream,
    quint32 byte_count,
    ClipboardPayload& payload)
{
    if (payload.blockSize > State::kMaxMapDim || payload.skip > State::kMaxMapDim
        || payload.count > State::kMaxMapDim || payload.sourceRowWidth > State::kMaxMapDim) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Clipboard map dimensions exceed maximum"));
    }

    // Use 64-bit unsigned arithmetic so adversarial 32-bit dimensions cannot wrap around.
    const uint64_t selected_cells = static_cast<uint64_t>(payload.blockSize) * static_cast<uint64_t>(payload.count);
    if (selected_cells == 0ULL || selected_cells > State::kMaxMapCells
        || byte_count == 0U || static_cast<size_t>(byte_count) > State::kMaxMapCells) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Clipboard map cell count out of bounds"));
    }

    const int effective_row_width = (payload.sourceRowWidth > 0) ? payload.sourceRowWidth : payload.blockSize;
    if (effective_row_width < payload.blockSize) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Clipboard sourceRowWidth smaller than blockSize"));
    }

    // Accept either a tight subgrid (`effective_row_width * count`) or a full strided buffer
    // large enough to cover the final selected row.
    const uint64_t tight_bytes = static_cast<uint64_t>(effective_row_width) * static_cast<uint64_t>(payload.count);
    const uint64_t strided_span = static_cast<uint64_t>(payload.offset)
        + static_cast<uint64_t>(payload.count - 1) * static_cast<uint64_t>(payload.blockSize + payload.skip)
        + static_cast<uint64_t>(payload.blockSize);

    if (static_cast<uint64_t>(byte_count) != tight_bytes
        && static_cast<uint64_t>(byte_count) < strided_span) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Clipboard map buffer too small for declared selection range"));
    }

    payload.sourceRowWidth = effective_row_width;
    payload.mapCells.resize(static_cast<size_t>(byte_count));
    const qint64 bytes_read = stream.readRawData(
        reinterpret_cast<char*>(payload.mapCells.data()),
        static_cast<int>(byte_count));
    if (bytes_read != static_cast<qint64>(byte_count)) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed reading clipboard map cells"));
    }

    return vchar64::Result<void>::ok();
}

// Validates char/tile selection bounds, reads the 2,048-byte charset and 256-byte color buffers
// from `stream` into `payload`, and masks Color RAM entries to 4 bits. Returns an error on
// range violations or stream read failures.
[[nodiscard]] vchar64::Result<void> ReadCharsetPayload(
    QDataStream& stream,
    quint32 byte_count,
    ClipboardPayload& payload)
{
    if (payload.offset >= 256 || payload.blockSize > 256 || payload.skip > 256
        || payload.count > 256 || byte_count != kCharsetAndColorsBytes) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Invalid char/tile range or byte count in clipboard payload"));
    }

    const uint64_t span = static_cast<uint64_t>(payload.offset)
        + static_cast<uint64_t>(payload.count - 1) * static_cast<uint64_t>(payload.blockSize + payload.skip)
        + static_cast<uint64_t>(payload.blockSize);
    if (span > 256ULL) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Clipboard char/tile selection exceeds 256 entries"));
    }

    const qint64 chars_read = stream.readRawData(
        reinterpret_cast<char*>(payload.charset.data()),
        static_cast<int>(payload.charset.size()));
    const qint64 colors_read = stream.readRawData(
        reinterpret_cast<char*>(payload.tileColors.data()),
        static_cast<int>(payload.tileColors.size()));
    if (chars_read != static_cast<qint64>(payload.charset.size())
        || colors_read != static_cast<qint64>(payload.tileColors.size())) {
        return vchar64::Result<void>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed reading clipboard charset/color buffers"));
    }

    // Mask C64 Color RAM values to 4 bits (0..15) so downstream palette lookups never go out of bounds.
    for (uint8_t& color_entry : payload.tileColors) {
        color_entry &= 0x0FU;
    }

    return vchar64::Result<void>::ok();
}

} // namespace

State::CopyRange ClipboardPayload::toCopyRange() const noexcept
{
    State::CopyRange range { };
    range.offset = offset;
    range.blockSize = blockSize;
    range.skip = skip;
    range.count = count;
    range.sourceRowWidth = sourceRowWidth;
    range.type = type;
    range.tileProperties = tileProperties;
    if (type == State::CopyRange::MAP) {
        range.bufferSize = static_cast<int>(mapCells.size());
    } else {
        range.bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
    }
    return range;
}

QByteArray ClipboardCodec::serialize(const ClipboardPayload& payload)
{
    QByteArray out_bytes;
    QDataStream stream(&out_bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream.writeRawData(kMagic.data(), static_cast<int>(kMagic.size()));
    stream << static_cast<quint32>(kFormatVersion);
    stream << static_cast<qint32>(payload.type);
    stream << static_cast<qint32>(payload.offset);
    stream << static_cast<qint32>(payload.blockSize);
    stream << static_cast<qint32>(payload.skip);
    stream << static_cast<qint32>(payload.count);
    stream << static_cast<qint32>(payload.sourceRowWidth);
    stream << static_cast<qint32>(payload.tileProperties.size.width());
    stream << static_cast<qint32>(payload.tileProperties.size.height());
    stream << static_cast<qint32>(payload.tileProperties.interleaved);

    if (payload.type == State::CopyRange::MAP) {
        const quint32 byte_count = static_cast<quint32>(
            std::min<size_t>(payload.mapCells.size(), State::kMaxMapCells));
        stream << byte_count;
        if (byte_count > 0U) {
            stream.writeRawData(
                reinterpret_cast<const char*>(payload.mapCells.data()),
                static_cast<int>(byte_count));
        }
    } else {
        stream << static_cast<quint32>(kCharsetAndColorsBytes);
        stream.writeRawData(
            reinterpret_cast<const char*>(payload.charset.data()),
            static_cast<int>(payload.charset.size()));
        stream.writeRawData(
            reinterpret_cast<const char*>(payload.tileColors.data()),
            static_cast<int>(payload.tileColors.size()));
    }

    return out_bytes;
}

vchar64::Result<ClipboardPayload> ClipboardCodec::deserialize(const QByteArray& raw_data)
{
    // Minimum header size: 8 (magic) + 4 (version) + 9 * 4 (metadata) + 4 (byte_count) = 52 bytes.
    constexpr qsizetype kMinHeaderBytes = 52;
    if (raw_data.size() < kMinHeaderBytes) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Clipboard buffer smaller than v2 header"));
    }

    QDataStream stream(raw_data);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);

    std::array<char, 8> magic { };
    if (stream.readRawData(magic.data(), static_cast<int>(magic.size())) != static_cast<int>(magic.size())
        || magic != kMagic) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Invalid clipboard magic header"));
    }

    quint32 version = 0;
    qint32 raw_type = -1;
    qint32 offset = -1;
    qint32 block_size = -1;
    qint32 skip = -1;
    qint32 count = -1;
    qint32 source_row_width = -1;
    qint32 tile_width = 0;
    qint32 tile_height = 0;
    qint32 interleaved = 0;
    quint32 byte_count = 0;

    stream >> version >> raw_type >> offset >> block_size >> skip >> count
        >> source_row_width >> tile_width >> tile_height >> interleaved >> byte_count;

    if (stream.status() != QDataStream::Ok) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed reading clipboard header fields"));
    }

    if (version != kFormatVersion) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::UnsupportedVersion,
            QStringLiteral("Unsupported clipboard payload version %1").arg(version));
    }

    if (raw_type < static_cast<qint32>(State::CopyRange::CHARS)
        || raw_type > static_cast<qint32>(State::CopyRange::MAP)) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unknown clipboard buffer type %1").arg(raw_type));
    }

    if (offset < 0 || block_size <= 0 || skip < 0 || count <= 0 || source_row_width < 0) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Negative or zero range bounds in clipboard payload"));
    }

    // Default-constructed CopyRange initializes tileProperties to {{-1, -1}, -1} for CHARS/MAP
    // selections that do not depend on tile dimensions; normalize that sentinel to 1x1.
    const bool is_non_tile_sentinel = (raw_type != static_cast<qint32>(State::CopyRange::TILES))
        && (tile_width == -1 && tile_height == -1 && interleaved == -1);
    if (is_non_tile_sentinel) {
        tile_width = 1;
        tile_height = 1;
        interleaved = 1;
    }

    if (tile_width < 1 || tile_width > State::MAX_TILE_WIDTH
        || tile_height < 1 || tile_height > State::MAX_TILE_HEIGHT
        || (tile_width * tile_height) > 256
        || interleaved < 1 || interleaved > 256) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid tile properties in clipboard payload"));
    }

    // Require an exact match between declared byte_count and remaining stream bytes
    // before allocating payload vectors (prevents OOM from forged 4 GiB byte_count headers).
    const qint64 stream_pos = stream.device()->pos();
    const qint64 remaining_bytes = static_cast<qint64>(raw_data.size()) - stream_pos;
    if (remaining_bytes < 0 || static_cast<quint64>(remaining_bytes) != static_cast<quint64>(byte_count)) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Clipboard payload byte length mismatch"));
    }

    ClipboardPayload payload;
    payload.type = static_cast<State::CopyRange::BufferType>(raw_type);
    payload.offset = static_cast<int>(offset);
    payload.blockSize = static_cast<int>(block_size);
    payload.skip = static_cast<int>(skip);
    payload.count = static_cast<int>(count);
    payload.sourceRowWidth = static_cast<int>(source_row_width);
    payload.tileProperties = State::TileProperties {
        QSize(static_cast<int>(tile_width), static_cast<int>(tile_height)),
        static_cast<int>(interleaved)
    };

    const vchar64::Result<void> payload_result = (payload.type == State::CopyRange::MAP)
        ? ReadMapPayload(stream, byte_count, payload)
        : ReadCharsetPayload(stream, byte_count, payload);
    if (!payload_result) {
        return vchar64::Result<ClipboardPayload>::err(
            payload_result.error().code,
            payload_result.error().detail);
    }

    return vchar64::Result<ClipboardPayload>::ok(std::move(payload));
}

ClipboardPayload ClipboardCodec::fromStateSelection(
    const State& state,
    const State::CopyRange& copy_range)
{
    ClipboardPayload payload;
    payload.type = copy_range.type;
    payload.offset = copy_range.offset;
    payload.blockSize = copy_range.blockSize;
    payload.skip = copy_range.skip;
    payload.count = copy_range.count;
    payload.sourceRowWidth = std::max(0, copy_range.blockSize);
    if (copy_range.tileProperties.size.width() >= 1
        && copy_range.tileProperties.size.height() >= 1
        && copy_range.tileProperties.interleaved >= 1) {
        payload.tileProperties = copy_range.tileProperties;
    } else {
        payload.tileProperties = state.getTileProperties();
    }

    if (copy_range.type != State::CopyRange::MAP) {
        payload.charset = state.charset();
        payload.tileColors = state.tileColors();
        return payload;
    }

    const State::MapBuffer& map_buffer = state.map();
    const int source_stride = copy_range.blockSize + copy_range.skip;
    if (copy_range.offset < 0 || copy_range.blockSize <= 0 || copy_range.count <= 0
        || source_stride < copy_range.blockSize) {
        return payload;
    }

    const size_t total_cells = static_cast<size_t>(copy_range.blockSize) * static_cast<size_t>(copy_range.count);
    if (total_cells > State::kMaxMapCells) {
        return payload;
    }

    payload.mapCells.assign(total_cells, 0U);
    for (int row = 0; row < copy_range.count; ++row) {
        const size_t source_offset = static_cast<size_t>(copy_range.offset)
            + static_cast<size_t>(row) * static_cast<size_t>(source_stride);
        if (source_offset >= map_buffer.size()) {
            break;
        }
        const size_t available_bytes = std::min(
            static_cast<size_t>(copy_range.blockSize),
            map_buffer.size() - source_offset);
        std::copy_n(
            map_buffer.data() + source_offset,
            available_bytes,
            payload.mapCells.data() + static_cast<size_t>(row) * static_cast<size_t>(copy_range.blockSize));
    }

    // Tight extracted subgrid has zero inter-row skip and zero offset inside mapCells.
    payload.offset = 0;
    payload.skip = 0;
    return payload;
}
