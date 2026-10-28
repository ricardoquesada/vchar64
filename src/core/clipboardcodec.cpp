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

#include "core/clipboardcodec.h"

/**
 * @file clipboardcodec.cpp
 * @brief Implementation of the `"VC64CLIP"` v2 little-endian clipboard wire format.
 *
 * Wire layout (all integers little-endian `QDataStream::Qt_6_0`):
 *
 * - Bytes `[0..7]`:   ASCII magic `"VC64CLIP"` (8 bytes)
 * - Bytes `[8..11]`:  `uint32_t` format version (`2`)
 * - Bytes `[12..47]`: Nine `int32_t` geometry fields (`type`, `offset`, `blockSize`,
 *                     `skip`, `count`, `sourceRowWidth`, `tw`, `th`, `interleaved`)
 * - Bytes `[48..51]`: `uint32_t` payload `byteCount`
 * - Bytes `[52..]`:   For `CHARS`/`TILES`, `2,304` contiguous bytes (`2,048`B charset +
 *                     `256`B tile colors); for `MAP`, `byteCount` tile-index bytes.
 */

#include <algorithm>
#include <array>
#include <cstring>

#include <QDataStream>
#include <QIODevice>

namespace {

constexpr std::array<char, 8> kMagic = { 'V', 'C', '6', '4', 'C', 'L', 'I', 'P' };
constexpr uint32_t kCharsetAndColorsBytes = static_cast<uint32_t>(
    State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE);

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

ClipboardPayload ClipboardCodec::fromStateSelection(
    const State& state,
    const State::CopyRange& copyRange)
{
    ClipboardPayload payload;
    payload.type = copyRange.type;
    payload.offset = copyRange.offset;
    payload.blockSize = copyRange.blockSize;
    payload.skip = copyRange.skip;
    payload.count = copyRange.count;
    if (copyRange.tileProperties.size.width() >= 1
        && copyRange.tileProperties.size.height() >= 1
        && copyRange.tileProperties.interleaved >= 1) {
        payload.tileProperties = copyRange.tileProperties;
    } else {
        payload.tileProperties = state.getTileProperties();
    }

    if (copyRange.type == State::CopyRange::MAP) {
        payload.sourceRowWidth = std::max(0, copyRange.blockSize);
        const auto& mapBuf = state.map();
        const int srcStride = copyRange.blockSize + copyRange.skip;
        if (copyRange.offset >= 0 && copyRange.blockSize > 0 && copyRange.count > 0 && srcStride >= copyRange.blockSize) {
            const size_t totalCells = static_cast<size_t>(copyRange.blockSize) * static_cast<size_t>(copyRange.count);
            if (totalCells <= State::kMaxMapCells) {
                payload.mapCells.assign(totalCells, 0U);
                for (int row = 0; row < copyRange.count; ++row) {
                    const size_t srcOffset = static_cast<size_t>(copyRange.offset)
                        + static_cast<size_t>(row) * static_cast<size_t>(srcStride);
                    if (srcOffset >= mapBuf.size()) {
                        break;
                    }
                    const size_t avail = std::min(static_cast<size_t>(copyRange.blockSize), mapBuf.size() - srcOffset);
                    std::copy_n(
                        mapBuf.data() + srcOffset,
                        avail,
                        payload.mapCells.data() + static_cast<size_t>(row) * static_cast<size_t>(copyRange.blockSize));
                }
                // Tight extracted sub-grid has zero inter-row skip and 0 offset inside mapCells.
                payload.offset = 0;
                payload.skip = 0;
            }
        }
    } else {
        payload.sourceRowWidth = std::max(0, copyRange.blockSize);
        payload.charset = state.charset();
        payload.tileColors = state.tileColors();
    }

    return payload;
}

QByteArray ClipboardCodec::serialize(const ClipboardPayload& payload)
{
    QByteArray outBytes;
    QDataStream stream(&outBytes, QIODevice::WriteOnly);
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
        const auto byteCount = static_cast<quint32>(
            std::min<size_t>(payload.mapCells.size(), State::kMaxMapCells));
        stream << byteCount;
        if (byteCount > 0U) {
            stream.writeRawData(
                reinterpret_cast<const char*>(payload.mapCells.data()),
                static_cast<int>(byteCount));
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

    return outBytes;
}

vchar64::Result<ClipboardPayload> ClipboardCodec::deserialize(const QByteArray& rawData)
{
    // Minimum header size: 8 (magic) + 4 (version) + 9 * 4 (metadata) + 4 (byteCount) = 52 bytes.
    constexpr qsizetype kMinHeaderBytes = 52;
    if (rawData.size() < kMinHeaderBytes) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Clipboard buffer smaller than v2 header"));
    }

    QDataStream stream(rawData);
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
    qint32 rawType = -1;
    qint32 offset = -1;
    qint32 blockSize = -1;
    qint32 skip = -1;
    qint32 count = -1;
    qint32 sourceRowWidth = -1;
    qint32 tileWidth = 0;
    qint32 tileHeight = 0;
    qint32 interleaved = 0;
    quint32 byteCount = 0;

    stream >> version >> rawType >> offset >> blockSize >> skip >> count
        >> sourceRowWidth >> tileWidth >> tileHeight >> interleaved >> byteCount;

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

    if (rawType < static_cast<qint32>(State::CopyRange::CHARS)
        || rawType > static_cast<qint32>(State::CopyRange::MAP)) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unknown clipboard buffer type %1").arg(rawType));
    }

    if (offset < 0 || blockSize <= 0 || skip < 0 || count <= 0 || sourceRowWidth < 0) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Negative or zero range bounds in clipboard payload"));
    }

    // Default-constructed CopyRange initializes tileProperties to {{-1, -1}, -1} for CHARS/MAP
    // selections that do not depend on tile dimensions; normalize that sentinel to 1x1.
    const bool isNonTileSentinel = (rawType != static_cast<qint32>(State::CopyRange::TILES))
        && (tileWidth == -1 && tileHeight == -1 && interleaved == -1);
    if (isNonTileSentinel) {
        tileWidth = 1;
        tileHeight = 1;
        interleaved = 1;
    }

    if (tileWidth < 1 || tileWidth > State::MAX_TILE_WIDTH
        || tileHeight < 1 || tileHeight > State::MAX_TILE_HEIGHT
        || (tileWidth * tileHeight) > 256
        || interleaved < 1 || interleaved > 256) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid tile properties in clipboard payload"));
    }

    // Require an exact match between declared byteCount and remaining stream bytes
    // before allocating payload vectors (prevents OOM from forged 4 GiB byteCount headers).
    const qint64 streamPos = stream.device()->pos();
    const qint64 remainingBytes = static_cast<qint64>(rawData.size()) - streamPos;
    if (remainingBytes < 0 || static_cast<quint64>(remainingBytes) != static_cast<quint64>(byteCount)) {
        return vchar64::Result<ClipboardPayload>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("Clipboard payload byte length mismatch"));
    }

    ClipboardPayload payload;
    payload.type = static_cast<State::CopyRange::BufferType>(rawType);
    payload.offset = static_cast<int>(offset);
    payload.blockSize = static_cast<int>(blockSize);
    payload.skip = static_cast<int>(skip);
    payload.count = static_cast<int>(count);
    payload.sourceRowWidth = static_cast<int>(sourceRowWidth);
    payload.tileProperties = State::TileProperties {
        QSize(static_cast<int>(tileWidth), static_cast<int>(tileHeight)),
        static_cast<int>(interleaved)
    };

    if (payload.type == State::CopyRange::MAP) {
        if (blockSize > State::kMaxMapDim || skip > State::kMaxMapDim
            || count > State::kMaxMapDim || sourceRowWidth > State::kMaxMapDim) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("Clipboard map dimensions exceed maximum"));
        }

        // Use 64-bit unsigned arithmetic so adversarial 32-bit dimensions cannot wrap around.
        const uint64_t selectedCells = static_cast<uint64_t>(blockSize) * static_cast<uint64_t>(count);
        if (selectedCells == 0ULL || selectedCells > State::kMaxMapCells
            || byteCount == 0U || static_cast<size_t>(byteCount) > State::kMaxMapCells) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("Clipboard map cell count out of bounds"));
        }

        const int effectiveRowWidth = (sourceRowWidth > 0) ? static_cast<int>(sourceRowWidth) : static_cast<int>(blockSize);
        if (effectiveRowWidth < blockSize) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Clipboard sourceRowWidth smaller than blockSize"));
        }

        // Accept either a tight subgrid (`effectiveRowWidth * count`) or a full strided buffer
        // large enough to cover the final selected row.
        const uint64_t tightBytes = static_cast<uint64_t>(effectiveRowWidth) * static_cast<uint64_t>(count);
        const uint64_t stridedSpan = static_cast<uint64_t>(offset)
            + static_cast<uint64_t>(count - 1) * static_cast<uint64_t>(blockSize + skip)
            + static_cast<uint64_t>(blockSize);

        if (static_cast<uint64_t>(byteCount) != tightBytes
            && static_cast<uint64_t>(byteCount) < stridedSpan) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Clipboard map buffer too small for declared selection range"));
        }

        payload.sourceRowWidth = effectiveRowWidth;
        payload.mapCells.resize(static_cast<size_t>(byteCount));
        const qint64 readGot = stream.readRawData(
            reinterpret_cast<char*>(payload.mapCells.data()),
            static_cast<int>(byteCount));
        if (readGot != static_cast<qint64>(byteCount)) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Failed reading clipboard map cells"));
        }
    } else {
        if (offset >= 256 || blockSize > 256 || skip > 256 || count > 256
            || byteCount != kCharsetAndColorsBytes) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Invalid char/tile range or byte count in clipboard payload"));
        }

        const uint64_t span = static_cast<uint64_t>(offset)
            + static_cast<uint64_t>(count - 1) * static_cast<uint64_t>(blockSize + skip)
            + static_cast<uint64_t>(blockSize);
        if (span > 256ULL) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Clipboard char/tile selection exceeds 256 entries"));
        }

        const qint64 gotChars = stream.readRawData(
            reinterpret_cast<char*>(payload.charset.data()),
            static_cast<int>(payload.charset.size()));
        const qint64 gotColors = stream.readRawData(
            reinterpret_cast<char*>(payload.tileColors.data()),
            static_cast<int>(payload.tileColors.size()));
        if (gotChars != static_cast<qint64>(payload.charset.size())
            || gotColors != static_cast<qint64>(payload.tileColors.size())) {
            return vchar64::Result<ClipboardPayload>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Failed reading clipboard charset/color buffers"));
        }

        // Mask C64 Color RAM values to 4 bits (0..15) so downstream palette lookups never go OOB.
        for (uint8_t& c : payload.tileColors) {
            c &= 0x0FU;
        }
    }

    return vchar64::Result<ClipboardPayload>::ok(std::move(payload));
}
