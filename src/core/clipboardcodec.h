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

#pragma once

/**
 * @file clipboardcodec.h
 * @brief Endian-safe, bounds-validated clipboard serialization for charset, tile, and map selections.
 *
 * Replaces raw `reinterpret_cast<State::CopyRange*>` MIME dumps with a versioned
 * `"VC64CLIP"` v2 little-endian `QDataStream` envelope (`application/x-vchar64-clip-v2`)
 * so untrusted or malformed system clipboard bytes cannot trigger out-of-bounds reads.
 */

#include <cstdint>
#include <vector>

#include <QByteArray>

#include "core/result.h"
#include "state.h"

/**
 * @brief Versioned, architecture-independent clipboard payload for `CHARS`, `TILES`, and `MAP` ranges.
 *
 * For `CHARS` and `TILES`, `charset` (`2,048` bytes) and `tileColors` (`256` 4-bit entries)
 * carry a full document snapshot indexed by `offset`, `blockSize`, `skip`, and `count`.
 * For `MAP`, `mapCells` stores either a tight `blockSize * count` subgrid (`offset == 0`, `skip == 0`)
 * or a strided slice bounded by `State::kMaxMapCells`.
 */
struct ClipboardPayload {
    State::CopyRange::BufferType type = State::CopyRange::CHARS;
    int offset = 0;
    int blockSize = 0;
    int skip = 0;
    int count = 0;
    int sourceRowWidth = 0;
    State::TileProperties tileProperties { { 1, 1 }, 1 };

    State::CharsetBuffer charset { };
    State::TileColorsBuffer tileColors { };
    std::vector<uint8_t> mapCells;

    /**
     * @brief Reconstructs a `State::CopyRange` descriptor matching this payload's buffer layout.
     */
    [[nodiscard]] State::CopyRange toCopyRange() const noexcept;
};

/**
 * @brief Bounds-checked little-endian `QDataStream` serializer and deserializer for clipboard MIME data.
 */
class ClipboardCodec {
public:
    static constexpr const char* kMimeType = "application/x-vchar64-clip-v2";
    static constexpr uint32_t kFormatVersion = 2U;

    /**
     * @brief Encodes `payload` into a `"VC64CLIP"` v2 little-endian byte stream.
     * @param payload Validated selection data to write to `QMimeData`.
     * @return Serialized byte buffer (52-byte header plus `2,304` char/color bytes or `mapCells.size()` map bytes).
     */
    [[nodiscard]] static QByteArray serialize(const ClipboardPayload& payload);

    /**
     * @brief Parses and validates untrusted system clipboard bytes from `kMimeType`.
     *
     * Verifies the 8-byte `"VC64CLIP"` magic, format version `2`, exact byte length,
     * 64-bit overflow-checked selection spans, `1 <= tw, th <= 8` tile geometry, and
     * `& 0x0FU` 4-bit C64 Color RAM masking before returning a `ClipboardPayload`.
     *
     * @param rawData Raw MIME payload retrieved from `QClipboard`.
     * @return Validated `ClipboardPayload` on success, or a structured `ErrorCode` on any format/bounds violation.
     */
    [[nodiscard]] static vchar64::Result<ClipboardPayload> deserialize(const QByteArray& rawData);

    /**
     * @brief Extracts a self-contained `ClipboardPayload` from `state` for `copyRange`.
     *
     * For `MAP` selections, packs the selected rectangle into a tight `blockSize * count`
     * row-major subgrid (`offset = 0`, `skip = 0`) so cross-document pastes into maps
     * of different widths use clean destination-stride addressing.
     */
    [[nodiscard]] static ClipboardPayload fromStateSelection(
        const State& state,
        const State::CopyRange& copyRange);
};

namespace vchar64 {
using ::ClipboardCodec;
using ::ClipboardPayload;
} // namespace vchar64
