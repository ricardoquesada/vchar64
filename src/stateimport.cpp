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
 * @file stateimport.cpp
 * @brief Bounds-checked, endian-safe binary importers for `StateImport` (`vchar64_core`).
 *
 * Security and correctness invariants enforced across all parsers:
 *
 * - **256-entry clamping with stream seeks:** CharPad CTM v4/v5 and VChar64 files can
 *   declare up to `65,536` characters or tiles. `readClampedCharset()` and
 *   `readClampedTileColors()` store at most `256` entries into fixed stack buffers and
 *   `seek()` past excess records so subsequent map blocks remain stream-aligned without
 *   overflowing `_charset` (`2,048` B) or `_tileColors` (`256` B).
 * - **Separate CTM header structs:** `loadCTM()` peeks only the 4-byte `"CTM"` + version
 *   prefix before reading `CTMHeader4` (24 B) or `CTMHeader5` (20 B) separately, avoiding
 *   cross-struct pointer aliasing UB.
 * - **Hardened VICE `.vsf` parsing:** Enforces `moduleLength >= 22` to prevent infinite
 *   zero-length module loops, heap-allocates `64 KiB` / `256 KiB` RAM structs, and
 *   accepts both 35-byte CIA2 v1.0 and 39-byte CIA2 v1.1 `$DD00` VIC bank payloads.
 */

#include "stateimport.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <QFile>
#include <QtEndian>

#include "state.h"

namespace {

/** @brief Ensures `device` is open for reading without reopening already-open `QBuffer` streams. */
[[nodiscard]] bool ensureOpenReadOnly(QIODevice& device)
{
    if (device.isOpen()) {
        return device.isReadable();
    }
    return device.open(QIODevice::ReadOnly);
}

/** @brief Reads exactly `byteCount` bytes into `dst`, returning `false` on EOF or short reads. */
[[nodiscard]] bool readExactBytes(QIODevice& device, void* dst, qint64 byteCount)
{
    if (byteCount < 0) {
        return false;
    }
    if (byteCount == 0) {
        return true;
    }
    return device.read(static_cast<char*>(dst), byteCount) == byteCount;
}

/** @brief Advances `device` by `byteCount` bytes with overflow and EOF bounds validation. */
[[nodiscard]] bool skipExactBytes(QIODevice& device, qint64 byteCount)
{
    if (byteCount < 0) {
        return false;
    }
    if (byteCount == 0) {
        return true;
    }
    const qint64 targetPos = device.pos() + byteCount;
    if (targetPos < device.pos() || targetPos > device.size()) {
        return false;
    }
    return device.seek(targetPos);
}

/** @brief Reads up to 256 8-byte glyphs into `dstCharset` and seeks past any excess characters (> 256). */
[[nodiscard]] bool readClampedCharset(
    QIODevice& device,
    int numCharsInFile,
    State::CharsetBuffer& dstCharset,
    vchar64::ImportReport& report,
    qint64& totalBytesConsumed)
{
    if (numCharsInFile < 0) {
        return false;
    }

    dstCharset.fill(0U);
    const int charsStored = std::min(numCharsInFile, State::CHAR_BUFFER_SIZE / 8);
    const qint64 bytesToRead = static_cast<qint64>(charsStored) * 8LL;
    if (!readExactBytes(device, dstCharset.data(), bytesToRead)) {
        return false;
    }
    totalBytesConsumed += bytesToRead;

    if (numCharsInFile > charsStored) {
        const qint64 excessBytes = static_cast<qint64>(numCharsInFile - charsStored) * 8LL;
        if (!skipExactBytes(device, excessBytes)) {
            return false;
        }
        totalBytesConsumed += excessBytes;
        report.warnings.append(
            QStringLiteral("Charset clamped from %1 characters to 256 characters.")
                .arg(numCharsInFile));
    }

    return true;
}

/** @brief Reads up to 256 4-bit Color RAM nibbles into `dstColors` and seeks past excess entries. */
[[nodiscard]] bool readClampedTileColors(
    QIODevice& device,
    int countInFile,
    State::TileColorsBuffer& dstColors,
    vchar64::ImportReport& report,
    const QString& label,
    qint64& totalBytesConsumed)
{
    if (countInFile < 0) {
        return false;
    }

    const int stored = std::min(countInFile, State::TILE_COLORS_BUFFER_SIZE);
    std::array<uint8_t, State::TILE_COLORS_BUFFER_SIZE> rawAttrs { };
    if (!readExactBytes(device, rawAttrs.data(), static_cast<qint64>(stored))) {
        return false;
    }
    totalBytesConsumed += static_cast<qint64>(stored);

    for (int i = 0; i < stored; ++i) {
        const auto idx = static_cast<size_t>(i);
        dstColors[idx] = static_cast<uint8_t>(rawAttrs[idx] & 0x0FU);
    }

    if (countInFile > stored) {
        const qint64 excess = static_cast<qint64>(countInFile - stored);
        if (!skipExactBytes(device, excess)) {
            return false;
        }
        totalBytesConsumed += excess;
        report.warnings.append(
            QStringLiteral("Clamped %1 block from %2 entries to 256 entries.")
                .arg(label)
                .arg(countInFile));
    }

    return true;
}

} // namespace

vchar64::Result<vchar64::ImportReport> StateImport::loadRaw(State& state, QIODevice& device)
{
    if (!ensureOpenReadOnly(device)) {
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

    const qint64 toRead = std::min<qint64>(remaining, State::CHAR_BUFFER_SIZE);
    State::CharsetBuffer stagedCharset { };
    if (!readExactBytes(device, stagedCharset.data(), toRead)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read raw charset payload"));
    }

    state.setCharsetBytes(stagedCharset.data(), stagedCharset.size());
    report.bytesRead = toRead;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadRaw(
    State& state,
    const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return loadRaw(state, file);
}

vchar64::Result<vchar64::ImportReport> StateImport::loadPRG(
    State& state,
    QIODevice& device,
    quint16* outAddress)
{
    if (!ensureOpenReadOnly(device)) {
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

    uint8_t addrBytes[2] { };
    if (!readExactBytes(device, addrBytes, 2)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read 2-byte PRG load address"));
    }

    const quint16 decodedAddress = qFromLittleEndian<quint16>(addrBytes);
    auto rawResult = loadRaw(state, device);
    if (!rawResult.isOk()) {
        return rawResult;
    }

    if (outAddress != nullptr) {
        *outAddress = decodedAddress;
    }
    return rawResult;
}

vchar64::Result<vchar64::ImportReport> StateImport::loadPRG(
    State& state,
    const QString& filename,
    quint16* outAddress)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return loadPRG(state, file, outAddress);
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM4(State& state, QIODevice& device)
{
    const qint64 remaining = device.size() - device.pos();
    if (remaining < static_cast<qint64>(sizeof(CTMHeader4))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("CTM v4 file smaller than 24-byte header"));
    }

    CTMHeader4 hdr { };
    if (!readExactBytes(device, &hdr, static_cast<qint64>(sizeof(hdr)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read CTM v4 header"));
    }

    if (hdr.expanded == 0U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnexpandedCtmUnsupported,
            QStringLiteral("Unexpanded CTM v4 files are not supported"));
    }

    if (hdr.color_mode > 2U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unsupported CTM v4 color mode %1").arg(hdr.color_mode));
    }

    const int tw = static_cast<int>(hdr.tile_width);
    const int th = static_cast<int>(hdr.tile_height);
    if (tw < 1 || tw > State::MAX_TILE_WIDTH
        || th < 1 || th > State::MAX_TILE_HEIGHT
        || (tw * th) > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v4 tile dimensions %1x%2").arg(tw).arg(th));
    }

    const int mapW = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_width));
    const int mapH = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_height));
    if (mapW < 1 || mapW > State::kMaxMapDim
        || mapH < 1 || mapH > State::kMaxMapDim) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v4 map dimensions %1x%2").arg(mapW).arg(mapH));
    }

    const size_t mapCellCount = static_cast<size_t>(mapW) * static_cast<size_t>(mapH);
    if (mapCellCount > State::kMaxMapCells) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("CTM v4 map cell count exceeds maximum"));
    }

    const int numChars = static_cast<int>(qFromLittleEndian<quint16>(hdr.num_chars)) + 1;
    const int numTiles = static_cast<int>(hdr.num_tiles) + 1;

    vchar64::ImportReport report;
    qint64 bytesConsumed = static_cast<qint64>(sizeof(hdr));

    State::CharsetBuffer stagedCharset { };
    if (!readClampedCharset(device, numChars, stagedCharset, report, bytesConsumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v4 character data"));
    }

    State::TileColorsBuffer stagedColors { };
    stagedColors.fill(State::TILE_COLORS_DEFAULT);

    const qint64 cellAttribsSize = static_cast<qint64>(numTiles)
        * static_cast<qint64>(tw)
        * static_cast<qint64>(th);

    if (hdr.color_mode == 0U) {
        if (!skipExactBytes(device, static_cast<qint64>(numChars))
            || !skipExactBytes(device, cellAttribsSize)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 attribute blocks"));
        }
        bytesConsumed += static_cast<qint64>(numChars) + cellAttribsSize;
    } else if (hdr.color_mode == 1U) {
        if (!skipExactBytes(device, static_cast<qint64>(numChars))
            || !skipExactBytes(device, cellAttribsSize)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 char/cell attribute blocks"));
        }
        bytesConsumed += static_cast<qint64>(numChars) + cellAttribsSize;

        if (!readClampedTileColors(
                device,
                numTiles,
                stagedColors,
                report,
                QStringLiteral("CTM v4 tile attribute"),
                bytesConsumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 tile attribute block"));
        }
    } else {
        if (!readClampedTileColors(
                device,
                numChars,
                stagedColors,
                report,
                QStringLiteral("CTM v4 char attribute"),
                bytesConsumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 char attribute block"));
        }
        if (!skipExactBytes(device, cellAttribsSize)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v4 cell attribute block"));
        }
        bytesConsumed += cellAttribsSize;
    }

    std::vector<uint8_t> stagedMap(mapCellCount, 0U);
    if (!readExactBytes(device, stagedMap.data(), static_cast<qint64>(mapCellCount))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v4 map data"));
    }
    bytesConsumed += static_cast<qint64>(mapCellCount);

    const State::TileProperties tp { QSize(tw, th), 1 };
    if (!state.setTilePropertiesDirect(tp)
        || !state.assignImportedBuffers(stagedCharset, stagedColors, QSize(mapW, mapH), std::move(stagedMap))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit CTM v4 buffers into State"));
    }

    for (int i = 0; i < 4; ++i) {
        state.setPenColorDirect(i, static_cast<uint8_t>(hdr.colors[i] & 0x0FU));
    }
    state.setMulticolorModeDirect(hdr.vic_res != 0U);
    state.setForegroundColorModeDirect(
        hdr.color_mode != 0U ? State::FOREGROUND_COLOR_PER_TILE : State::FOREGROUND_COLOR_GLOBAL);

    report.bytesRead = bytesConsumed;
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

    CTMHeader5 hdr { };
    if (!readExactBytes(device, &hdr, static_cast<qint64>(sizeof(hdr)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read CTM v5 header"));
    }

    // Must be expanded (bit 1 == 1) whenever Tile System is enabled (bit 0 == 1).
    if ((hdr.flags & 0x03U) == 0x01U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnexpandedCtmUnsupported,
            QStringLiteral("Unexpanded CTM v5 files are not supported"));
    }

    if (hdr.color_mode > 2U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::HeaderOutOfBounds,
            QStringLiteral("Unsupported CTM v5 color mode %1").arg(hdr.color_mode));
    }

    // CharPad v5 files with Tile System disabled may emit 0 for tile dimensions.
    const int tw = std::max<int>(static_cast<int>(hdr.tile_width), 1);
    const int th = std::max<int>(static_cast<int>(hdr.tile_height), 1);
    if (tw > State::MAX_TILE_WIDTH
        || th > State::MAX_TILE_HEIGHT
        || (tw * th) > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v5 tile dimensions %1x%2").arg(tw).arg(th));
    }

    const int mapW = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_width));
    const int mapH = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_height));
    if (mapW < 1 || mapW > State::kMaxMapDim
        || mapH < 1 || mapH > State::kMaxMapDim) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid CTM v5 map dimensions %1x%2").arg(mapW).arg(mapH));
    }

    const size_t mapCellCount = static_cast<size_t>(mapW) * static_cast<size_t>(mapH);
    if (mapCellCount > State::kMaxMapCells) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("CTM v5 map cell count exceeds maximum"));
    }

    const int numChars = static_cast<int>(qFromLittleEndian<quint16>(hdr.num_chars)) + 1;
    const int numTiles = static_cast<int>(qFromLittleEndian<quint16>(hdr.num_tiles)) + 1;

    vchar64::ImportReport report;
    qint64 bytesConsumed = static_cast<qint64>(sizeof(hdr));

    State::CharsetBuffer stagedCharset { };
    if (!readClampedCharset(device, numChars, stagedCharset, report, bytesConsumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v5 character data"));
    }

    State::TileColorsBuffer stagedColors { };
    stagedColors.fill(State::TILE_COLORS_DEFAULT);

    if (hdr.color_mode == 2U) {
        if (!readClampedTileColors(
                device,
                numChars,
                stagedColors,
                report,
                QStringLiteral("CTM v5 char attribute"),
                bytesConsumed)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v5 char attribute block"));
        }
    } else {
        if (!skipExactBytes(device, static_cast<qint64>(numChars))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated CTM v5 char attribute block"));
        }
        bytesConsumed += static_cast<qint64>(numChars);

        if (hdr.color_mode == 1U) {
            if (!readClampedTileColors(
                    device,
                    numTiles,
                    stagedColors,
                    report,
                    QStringLiteral("CTM v5 tile color"),
                    bytesConsumed)) {
                return vchar64::Result<vchar64::ImportReport>::err(
                    vchar64::ErrorCode::FileReadFailed,
                    QStringLiteral("Truncated CTM v5 tile color block"));
            }
        }
    }

    std::vector<uint16_t> rawMap(mapCellCount, 0U);
    const qint64 mapByteCount = static_cast<qint64>(mapCellCount * sizeof(uint16_t));
    if (!readExactBytes(device, rawMap.data(), mapByteCount)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated CTM v5 16-bit map data"));
    }
    bytesConsumed += mapByteCount;

    std::vector<uint8_t> stagedMap(mapCellCount, 0U);
    size_t clampedMapCells = 0;
    for (size_t i = 0; i < mapCellCount; ++i) {
        const uint16_t tileIdx = qFromLittleEndian<quint16>(rawMap[i]);
        if (tileIdx > 255U) {
            ++clampedMapCells;
        }
        stagedMap[i] = static_cast<uint8_t>(tileIdx & 0xFFU);
    }

    if (clampedMapCells > 0U) {
        report.warnings.append(
            QStringLiteral("Clamped %1 CTM v5 map cell(s) exceeding tile index 255.")
                .arg(clampedMapCells));
    }

    const State::TileProperties tp { QSize(tw, th), 1 };
    if (!state.setTilePropertiesDirect(tp)
        || !state.assignImportedBuffers(stagedCharset, stagedColors, QSize(mapW, mapH), std::move(stagedMap))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit CTM v5 buffers into State"));
    }

    for (int i = 0; i < 4; ++i) {
        state.setPenColorDirect(i, static_cast<uint8_t>(hdr.colors[i] & 0x0FU));
    }
    state.setMulticolorModeDirect((hdr.flags & 0x04U) != 0U);
    state.setForegroundColorModeDirect(
        hdr.color_mode != 0U ? State::FOREGROUND_COLOR_PER_TILE : State::FOREGROUND_COLOR_GLOBAL);

    report.bytesRead = bytesConsumed;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadCTM(State& state, QIODevice& device)
{
    if (!ensureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open CTM device for reading"));
    }

    const qint64 startPos = device.pos();
    const qint64 remaining = device.size() - startPos;
    if (remaining < static_cast<qint64>(sizeof(CTMHeader5))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("CTM file is smaller than minimum 20-byte header (%1 bytes)").arg(remaining));
    }

    uint8_t prefix[4] { };
    if (!readExactBytes(device, prefix, 4) || !device.seek(startPos)) {
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
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return loadCTM(state, file);
}

vchar64::Result<vchar64::ImportReport> StateImport::loadVChar64(State& state, QIODevice& device)
{
    if (!ensureOpenReadOnly(device)) {
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

    VChar64Header hdr { };
    if (!readExactBytes(device, &hdr, static_cast<qint64>(sizeof(hdr)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read VChar64 header"));
    }

    if (std::memcmp(hdr.id, "VChar", 5) != 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Missing 'VChar' magic header"));
    }

    if (hdr.version < 1U || hdr.version > 3U) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::UnsupportedVersion,
            QStringLiteral("Unsupported VChar64 version %1").arg(hdr.version));
    }

    const int tw = static_cast<int>(hdr.tile_width);
    const int th = static_cast<int>(hdr.tile_height);
    const int interleaved = (hdr.char_interleaved == 0U && tw == 1 && th == 1)
        ? 1
        : static_cast<int>(hdr.char_interleaved);
    if (tw < 1 || tw > State::MAX_TILE_WIDTH
        || th < 1 || th > State::MAX_TILE_HEIGHT
        || (tw * th) > 256
        || interleaved < 1 || interleaved > 256) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Invalid VChar64 tile properties %1x%2 (interleaved=%3)")
                .arg(tw)
                .arg(th)
                .arg(hdr.char_interleaved));
    }

    vchar64::ImportReport report;
    qint64 bytesConsumed = static_cast<qint64>(sizeof(hdr));

    const int numChars = static_cast<int>(qFromLittleEndian<quint16>(hdr.num_chars));
    State::CharsetBuffer stagedCharset { };
    if (!readClampedCharset(device, numChars, stagedCharset, report, bytesConsumed)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Truncated VChar64 charset payload"));
    }

    State::ForegroundColorMode stagedFgMode = State::FOREGROUND_COLOR_GLOBAL;
    State::TileColorsBuffer stagedColors { };
    stagedColors.fill(State::TILE_COLORS_DEFAULT);
    QSize stagedMapSize(40, 25);
    std::vector<uint8_t> stagedMap = (state.mapSize() == QSize(40, 25) && state.map().size() == 40U * 25U)
        ? state.map()
        : std::vector<uint8_t>(40U * 25U, 0x20U);

    if (hdr.version >= 2U) {
        if (hdr.color_mode > 1U) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Invalid VChar64 color mode %1").arg(hdr.color_mode));
        }
        stagedFgMode = (hdr.color_mode == 1U)
            ? State::FOREGROUND_COLOR_PER_TILE
            : State::FOREGROUND_COLOR_GLOBAL;

        const int mapW = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_width));
        const int mapH = static_cast<int>(qFromLittleEndian<quint16>(hdr.map_height));
        if (mapW < 1 || mapW > State::kMaxMapDim
            || mapH < 1 || mapH > State::kMaxMapDim) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("Invalid VChar64 map dimensions %1x%2").arg(mapW).arg(mapH));
        }

        const size_t mapCellCount = static_cast<size_t>(mapW) * static_cast<size_t>(mapH);
        if (mapCellCount > State::kMaxMapCells) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::InvalidDimensions,
                QStringLiteral("VChar64 map cell count exceeds maximum"));
        }

        if (!readExactBytes(device, stagedColors.data(), static_cast<qint64>(stagedColors.size()))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated VChar64 tile colors payload"));
        }
        bytesConsumed += static_cast<qint64>(stagedColors.size());
        for (uint8_t& c : stagedColors) {
            c &= 0x0FU;
        }

        stagedMapSize = QSize(mapW, mapH);
        stagedMap.assign(mapCellCount, 0U);
        if (!readExactBytes(device, stagedMap.data(), static_cast<qint64>(mapCellCount))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Truncated VChar64 map payload"));
        }
        bytesConsumed += static_cast<qint64>(mapCellCount);
    }

    State::ExportProperties stagedExportProps { };
    State::KeyboardMapping stagedKeyboardMapping = State::KEYBOARD_MAPPING_C64;

    if (hdr.version == 3U) {
        if (hdr.export_format > static_cast<uint8_t>(State::EXPORT_FORMAT_C)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::HeaderOutOfBounds,
                QStringLiteral("Invalid VChar64 export format %1").arg(hdr.export_format));
        }

        stagedExportProps.addresses[0] = qFromLittleEndian<quint16>(hdr.address_charset);
        stagedExportProps.addresses[1] = qFromLittleEndian<quint16>(hdr.address_map);
        stagedExportProps.addresses[2] = qFromLittleEndian<quint16>(hdr.address_attribs);
        stagedExportProps.format = static_cast<State::ExportFormat>(hdr.export_format);
        stagedExportProps.features = static_cast<uint8_t>(hdr.export_features & State::EXPORT_FEATURE_ALL);

        if (hdr.keyboard_mapping > static_cast<uint8_t>(State::KEYBOARD_MAPPING_ATARI8)) {
            report.warnings.append(
                QStringLiteral("Clamped unrecognized VChar64 keyboard_mapping (%1) to C64.")
                    .arg(hdr.keyboard_mapping));
            stagedKeyboardMapping = State::KEYBOARD_MAPPING_C64;
        } else {
            stagedKeyboardMapping = static_cast<State::KeyboardMapping>(hdr.keyboard_mapping);
        }
    }

    const State::TileProperties tp { QSize(tw, th), interleaved };
    if (!state.setTilePropertiesDirect(tp)
        || !state.assignImportedBuffers(stagedCharset, stagedColors, stagedMapSize, std::move(stagedMap))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Failed to commit VChar64 buffers into State"));
    }

    for (int i = 0; i < 4; ++i) {
        state.setPenColorDirect(i, static_cast<uint8_t>(hdr.colors[i] & 0x0FU));
    }
    state.setMulticolorModeDirect(hdr.vic_res != 0U);
    state.setForegroundColorModeDirect(stagedFgMode);
    state.setExportPropertiesDirect(stagedExportProps);
    state.setKeyboardMappingDirect(stagedKeyboardMapping);

    report.bytesRead = bytesConsumed;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::loadVChar64(
    State& state,
    const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return loadVChar64(state, file);
}

vchar64::Result<KoalaBitmapData> StateImport::loadKoala(QIODevice& device)
{
    if (!ensureOpenReadOnly(device)) {
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
    uint8_t addrBytes[2] { };
    if (!readExactBytes(device, addrBytes, 2)
        || !readExactBytes(device, data.bitmap.data(), static_cast<qint64>(data.bitmap.size()))
        || !readExactBytes(device, data.screenRam.data(), static_cast<qint64>(data.screenRam.size()))
        || !readExactBytes(device, data.colorRam.data(), static_cast<qint64>(data.colorRam.size()))) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read Koala payload segments"));
    }

    data.loadAddress = qFromLittleEndian<quint16>(addrBytes);
    for (uint8_t& c : data.colorRam) {
        c &= 0x0FU;
    }

    if (remaining == KoalaBitmapData::kMaxFileSize) {
        uint8_t rawBg = 0U;
        if (!readExactBytes(device, &rawBg, 1)) {
            return vchar64::Result<KoalaBitmapData>::err(
                vchar64::ErrorCode::FileReadFailed,
                QStringLiteral("Failed to read trailing Koala background byte"));
        }
        data.backgroundColor = static_cast<uint8_t>(rawBg & 0x0FU);
    } else {
        data.backgroundColor = 0U;
    }

    return vchar64::Result<KoalaBitmapData>::ok(data);
}

vchar64::Result<KoalaBitmapData> StateImport::loadKoala(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<KoalaBitmapData>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return loadKoala(file);
}

vchar64::Result<vchar64::ImportReport> StateImport::parseVICESnapshot(
    QIODevice& device,
    uint8_t* buffer64k,
    uint16_t* outCharsetAddress,
    uint16_t* outScreenRAMAddress,
    uint8_t* outColorRAMBuf,
    uint8_t* outVICRegistersBuf)
{
    if (buffer64k == nullptr
        || outCharsetAddress == nullptr
        || outScreenRAMAddress == nullptr
        || outColorRAMBuf == nullptr
        || outVICRegistersBuf == nullptr) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidDimensions,
            QStringLiteral("Null output buffer passed to parseVICESnapshot"));
    }

    if (!ensureOpenReadOnly(device)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open VICE snapshot device for reading"));
    }

    const qint64 fileSize = device.size();
    if (fileSize < static_cast<qint64>(sizeof(VICESnapshotHeader))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileTooSmall,
            QStringLiteral("VICE snapshot smaller than 37-byte header"));
    }

    if (!device.seek(0)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to seek to beginning of VICE snapshot"));
    }

    static constexpr char kViceHeaderMagic[19] = {
        'V', 'I', 'C', 'E', ' ', 'S', 'n', 'a', 'p', 's', 'h', 'o', 't', ' ', 'F', 'i', 'l', 'e', '\032'
    };
    static constexpr char kViceVersionMagic[13] = {
        'V', 'I', 'C', 'E', ' ', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\032'
    };
    static constexpr char kViceC64Mem[] = "C64MEM";
    static constexpr char kViceC128Mem[] = "C128MEM";
    static constexpr char kViceVicII[] = "VIC-II";
    static constexpr char kViceCia2[] = "CIA2";

    VICESnapshotHeader header { };
    if (!readExactBytes(device, &header, static_cast<qint64>(sizeof(header)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileReadFailed,
            QStringLiteral("Failed to read VICE snapshot header"));
    }

    if (std::memcmp(header.id, kViceHeaderMagic, sizeof(kViceHeaderMagic)) != 0) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::InvalidMagic,
            QStringLiteral("Invalid VICE snapshot header magic"));
    }

    // Optional 21-byte VICE Version block immediately following the main header.
    const qint64 posAfterHeader = device.pos();
    if (fileSize - posAfterHeader >= static_cast<qint64>(sizeof(VICESnapshotVersion))) {
        VICESnapshotVersion version { };
        if (!readExactBytes(device, &version, static_cast<qint64>(sizeof(version)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to inspect VICE version header"));
        }
        if (std::memcmp(version.id, kViceVersionMagic, sizeof(kViceVersionMagic)) != 0) {
            if (!device.seek(posAfterHeader)) {
                return vchar64::Result<vchar64::ImportReport>::err(
                    vchar64::ErrorCode::FileReadFailed,
                    QStringLiteral("Failed to rewind after optional VICE version check"));
            }
        }
    }

    qint64 moduleStartOffset = device.pos();
    qint64 c64memOffset = -1;
    qint64 c64memPayloadSize = 0;
    qint64 c128memOffset = -1;
    qint64 c128memPayloadSize = 0;
    qint64 cia2Offset = -1;
    qint64 cia2PayloadSize = 0;
    qint64 vic2Offset = -1;
    qint64 vic2PayloadSize = 0;

    *outCharsetAddress = 0U;
    *outScreenRAMAddress = 0x0400U;

    while (moduleStartOffset < fileSize) {
        if (fileSize - moduleStartOffset < static_cast<qint64>(sizeof(VICESnapshotModule))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE module header at offset %1").arg(moduleStartOffset));
        }

        VICESnapshotModule module { };
        if (!readExactBytes(device, &module, static_cast<qint64>(sizeof(module)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE module header"));
        }

        const uint32_t moduleLength = qFromLittleEndian<quint32>(module.length);
        if (moduleLength < sizeof(VICESnapshotModule)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Corrupt VICE module length (%1 < %2)")
                    .arg(moduleLength)
                    .arg(sizeof(VICESnapshotModule)));
        }

        const qint64 nextModuleOffset = moduleStartOffset + static_cast<qint64>(moduleLength);
        if (nextModuleOffset > fileSize) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE module payload extends past EOF"));
        }

        const qint64 payloadOffset = device.pos();
        const qint64 payloadSize = nextModuleOffset - payloadOffset;

        if (c64memOffset == -1
            && std::memcmp(module.moduleName, kViceC64Mem, sizeof(kViceC64Mem)) == 0) {
            c64memOffset = payloadOffset;
            c64memPayloadSize = payloadSize;
        } else if (c128memOffset == -1
            && std::memcmp(module.moduleName, kViceC128Mem, sizeof(kViceC128Mem)) == 0) {
            c128memOffset = payloadOffset;
            c128memPayloadSize = payloadSize;
        } else if (cia2Offset == -1
            && std::memcmp(module.moduleName, kViceCia2, sizeof(kViceCia2)) == 0) {
            cia2Offset = payloadOffset;
            cia2PayloadSize = payloadSize;
        } else if (vic2Offset == -1
            && std::memcmp(module.moduleName, kViceVicII, sizeof(kViceVicII)) == 0) {
            vic2Offset = payloadOffset;
            vic2PayloadSize = payloadSize;
        }

        moduleStartOffset = nextModuleOffset;
        if (moduleStartOffset < fileSize && !device.seek(moduleStartOffset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to seek to next VICE module"));
        }
    }

    if ((c64memOffset == -1 && c128memOffset == -1) || cia2Offset == -1 || vic2Offset == -1) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Missing required VICE C64MEM/C128MEM, CIA2, or VIC-II module"));
    }

    // Heap-allocate the 64 KB / 256 KB RAM buffers only when needed so
    // parseVICESnapshot never places 320 KB on the call stack.
    if (c64memOffset != -1) {
        if (c64memPayloadSize < static_cast<qint64>(sizeof(VICESnapshotC64Mem))
            || !device.seek(c64memOffset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE C64MEM module"));
        }
        auto c64mem = std::make_unique<VICESnapshotC64Mem>();
        if (!readExactBytes(device, c64mem.get(), static_cast<qint64>(sizeof(VICESnapshotC64Mem)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE C64MEM payload"));
        }
        std::memcpy(buffer64k, c64mem->ram, sizeof(c64mem->ram));
    } else {
        if (c128memPayloadSize < static_cast<qint64>(sizeof(VICESnapshotC128Mem))
            || !device.seek(c128memOffset)) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Truncated VICE C128MEM module"));
        }
        auto c128mem = std::make_unique<VICESnapshotC128Mem>();
        if (!readExactBytes(device, c128mem.get(), static_cast<qint64>(sizeof(VICESnapshotC128Mem)))) {
            return vchar64::Result<vchar64::ImportReport>::err(
                vchar64::ErrorCode::CorruptViceSnapshot,
                QStringLiteral("Failed to read VICE C128MEM payload"));
        }
        std::memcpy(buffer64k, c128mem->ram, 65536U);
    }

    // CIA2 port A (`ora`, byte 0 of CIA2 payload) controls the active 16 KB VIC-II bank ($DD00).
    // Note: CIA2 v1.0 payloads omit the trailing 4-byte v1.1 `tastate`/`tbstate` fields,
    // so require at least the 35-byte v1.0 prefix (`sizeof(VICESnapshotCIA2) - 4`).
    static constexpr qint64 kMinCia2PayloadSize = static_cast<qint64>(sizeof(VICESnapshotCIA2) - 4U);
    if (cia2PayloadSize < kMinCia2PayloadSize || !device.seek(cia2Offset)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Truncated VICE CIA2 module"));
    }
    VICESnapshotCIA2 cia2 { };
    const qint64 cia2BytesToRead = std::min<qint64>(
        cia2PayloadSize,
        static_cast<qint64>(sizeof(VICESnapshotCIA2)));
    if (!readExactBytes(device, &cia2, cia2BytesToRead)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Failed to read VICE CIA2 payload"));
    }
    const uint16_t bankAddr = static_cast<uint16_t>((3U - (cia2.ora & 0x03U)) * 16384U);

    if (vic2PayloadSize < static_cast<qint64>(sizeof(VICESnapshotVICII))
        || !device.seek(vic2Offset)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Truncated VICE VIC-II module"));
    }
    VICESnapshotVICII vic2 { };
    if (!readExactBytes(device, &vic2, static_cast<qint64>(sizeof(VICESnapshotVICII)))) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::CorruptViceSnapshot,
            QStringLiteral("Failed to read VICE VIC-II payload"));
    }

    static constexpr char kSeuckSignature[] = "PRESS FIRE TO COMMENCE SUPADEATH";
    if (std::memcmp(kSeuckSignature, &buffer64k[0x3FDC], sizeof(kSeuckSignature) - 1U) == 0) {
        *outCharsetAddress = 0xF800U;
        *outScreenRAMAddress = 0xEC00U;
    } else {
        const uint16_t charsetOffset = static_cast<uint16_t>(((vic2.registers[0x18] & 0x0EU) >> 1U) * 2048U);
        *outCharsetAddress = static_cast<uint16_t>(bankAddr + charsetOffset);

        const uint16_t screenOffset = static_cast<uint16_t>((vic2.registers[0x18] >> 4U) * 1024U);
        *outScreenRAMAddress = static_cast<uint16_t>(bankAddr + screenOffset);
    }

    for (size_t i = 0; i < sizeof(vic2.color_ram); ++i) {
        outColorRAMBuf[i] = static_cast<uint8_t>(vic2.color_ram[i] & 0x0FU);
    }
    std::memcpy(outVICRegistersBuf, vic2.registers, sizeof(vic2.registers));

    vchar64::ImportReport report;
    report.bytesRead = fileSize;
    return vchar64::Result<vchar64::ImportReport>::ok(std::move(report));
}

vchar64::Result<vchar64::ImportReport> StateImport::parseVICESnapshot(
    const QString& filename,
    uint8_t* buffer64k,
    uint16_t* outCharsetAddress,
    uint16_t* outScreenRAMAddress,
    uint8_t* outColorRAMBuf,
    uint8_t* outVICRegistersBuf)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return vchar64::Result<vchar64::ImportReport>::err(
            vchar64::ErrorCode::FileOpenFailed,
            QStringLiteral("Failed to open '%1': %2").arg(filename, file.errorString()));
    }
    return parseVICESnapshot(
        file,
        buffer64k,
        outCharsetAddress,
        outScreenRAMAddress,
        outColorRAMBuf,
        outVICRegistersBuf);
}
