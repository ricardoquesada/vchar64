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

#include "core/tileatlascache.h"

/**
 * @file tileatlascache.cpp
 * @brief CPU scanline rasterizer for the dirty-tracked 16-column VIC-II tile sheet cache.
 */

#include <algorithm>
#include <array>

#include "palette.h"

void TileAtlasCache::markTileDirty(int tileIndex) noexcept
{
    if (tileIndex >= 0 && tileIndex < kMaxTiles) {
        _dirtyTiles.set(static_cast<size_t>(tileIndex));
    }
}

void TileAtlasCache::markAllDirty() noexcept
{
    _allDirty = true;
    _dirtyTiles.set();
}

bool TileAtlasCache::isTileDirty(int tileIndex) const noexcept
{
    if (_allDirty) {
        return true;
    }
    if (tileIndex < 0 || tileIndex >= kMaxTiles) {
        return false;
    }
    return _dirtyTiles.test(static_cast<size_t>(tileIndex));
}

QRect TileAtlasCache::sourceRectForTile(int tileIndex) const noexcept
{
    const int safeMax = std::max(1, _maxTiles);
    const int clamped = std::clamp(tileIndex, 0, safeMax - 1);
    const int col = clamped % kColumns;
    const int row = clamped / kColumns;
    return QRect(col * _tilePixelWidth, row * _tilePixelHeight, _tilePixelWidth, _tilePixelHeight);
}

void TileAtlasCache::sync(const State& state)
{
    const State::TileProperties props = state.getTileProperties();
    const int tw = std::clamp(props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int th = std::clamp(props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(props.interleaved, 1, 256);
    const int maxTiles = std::max(1, 256 / (tw * th));

    const int activePal = Palette::activePalette();
    const bool multicolor = state.isMulticolorMode();
    const State::ForegroundColorMode fgMode = state.getForegroundColorMode();
    const int bgColor = state.getColorForPen(State::PEN_BACKGROUND);
    const int mc1Color = state.getColorForPen(State::PEN_MULTICOLOR1);
    const int mc2Color = state.getColorForPen(State::PEN_MULTICOLOR2);
    const int globalFgColor = state.getColorForPen(State::PEN_FOREGROUND);

    // Detect MDI active-document switches (`_cachedState != &state`), global VIC-II palette
    // switches (`_cachedPaletteIndex != activePal`), and document-wide geometry/register changes
    // so shared dock widgets never display stale tiles from a previous document or palette.
    const bool contextChanged = (_cachedState != &state)
        || (_cachedPaletteIndex != activePal)
        || (_cachedTileSize != QSize(tw, th))
        || (_cachedInterleaved != interleaved)
        || (_cachedMulticolorMode != multicolor)
        || (_cachedFgMode != fgMode)
        || (_cachedBgColor != bgColor)
        || (_cachedMc1Color != mc1Color)
        || (_cachedMc2Color != mc2Color)
        || (fgMode == State::FOREGROUND_COLOR_GLOBAL && _cachedGlobalFgColor != globalFgColor);

    _tilePixelWidth = tw * 8;
    _tilePixelHeight = th * 8;
    _maxTiles = maxTiles;

    const int sheetRows = std::max(1, (maxTiles + kColumns - 1) / kColumns);
    const QSize requiredAtlasSize(kColumns * _tilePixelWidth, sheetRows * _tilePixelHeight);

    // Use Format_ARGB32_Premultiplied so Qt's raster paint engine blits sub-rectangles
    // directly without per-pixel format conversion during QPainter::drawImage().
    if (_atlas.size() != requiredAtlasSize || _atlas.format() != QImage::Format_ARGB32_Premultiplied) {
        _atlas = QImage(requiredAtlasSize, QImage::Format_ARGB32_Premultiplied);
        _atlas.fill(0U);
        _allDirty = true;
    }

    if (_allDirty || contextChanged) {
        _dirtyTiles.set();
        _allDirty = false;
        _cachedState = &state;
        _cachedPaletteIndex = activePal;
        _cachedTileSize = QSize(tw, th);
        _cachedInterleaved = interleaved;
        _cachedMulticolorMode = multicolor;
        _cachedFgMode = fgMode;
        _cachedBgColor = bgColor;
        _cachedMc1Color = mc1Color;
        _cachedMc2Color = mc2Color;
        _cachedGlobalFgColor = globalFgColor;
    }

    int syncedCount = 0;
    for (int tileIdx = 0; tileIdx < maxTiles; ++tileIdx) {
        if (!_dirtyTiles.test(static_cast<size_t>(tileIdx))) {
            continue;
        }
        rasterizeTile(state, tileIdx, tw, th, interleaved);
        _dirtyTiles.reset(static_cast<size_t>(tileIdx));
        ++syncedCount;
    }
    _dirtyTiles.reset();
    _lastSyncedTileCount = syncedCount;
}

void TileAtlasCache::rasterizeTile(const State& state, int tileIndex, int tw, int th, int interleaved)
{
    const auto& charset = state.charset();
    const bool isMc = state.shouldBeDisplayedInMulticolor2(tileIndex);

    const QRgb bgRgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba());
    const QRgb mc1Rgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR1)).rgba());
    const QRgb mc2Rgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR2)).rgba());
    const int rawFg = state.getColorForPen(State::PEN_FOREGROUND, tileIndex);
    const QRgb fgHiResRgb = qPremultiply(Palette::getColor(rawFg).rgba());
    // In VIC-II multicolor character mode ($D016 bit 4 == 1 and Color RAM bit 3 == 1),
    // bits 0..2 of Color RAM select one of the lower 8 foreground colors (0..7).
    const QRgb fgMcRgb = qPremultiply(
        Palette::getColor(static_cast<int>(static_cast<unsigned int>(rawFg) & 0x07U)).rgba());
    const std::array<QRgb, 4> mcLut = { bgRgb, mc1Rgb, mc2Rgb, fgMcRgb };

    const int baseOriginX = (tileIndex % kColumns) * _tilePixelWidth;
    const int baseOriginY = (tileIndex / kColumns) * _tilePixelHeight;

    int charIdx = (interleaved == 1) ? (tileIndex * tw * th) : tileIndex;
    const int totalSubChars = tw * th;

    for (int sub = 0; sub < totalSubChars; ++sub) {
        const int subX = (sub % tw) * 8;
        const int subY = (sub / tw) * 8;

        if (charIdx >= 0 && charIdx < 256) {
            const size_t charByteBase = static_cast<size_t>(charIdx) * 8U;
            for (int row = 0; row < 8; ++row) {
                const uint8_t rowByte = charset[charByteBase + static_cast<size_t>(row)];
                auto* scanline = static_cast<QRgb*>(
                    static_cast<void*>(_atlas.scanLine(baseOriginY + subY + row)));
                QRgb* dst = scanline + baseOriginX + subX;

                if (isMc) {
                    // Multicolor row: 4 double-wide 2-bit pixels [b7b6 | b5b4 | b3b2 | b1b0].
                    for (unsigned int pairIdx = 0; pairIdx < 4U; ++pairIdx) {
                        const unsigned int shift = 6U - (pairIdx * 2U);
                        const size_t penBits = static_cast<size_t>((rowByte >> shift) & 0x03U);
                        const QRgb px = mcLut[penBits];
                        dst[pairIdx * 2U] = px;
                        dst[pairIdx * 2U + 1U] = px;
                    }
                } else {
                    // Hi-Res row: 8 single-wide 1-bit pixels [b7 .. b0].
                    for (unsigned int bit = 0; bit < 8U; ++bit) {
                        const bool set = ((rowByte >> (7U - bit)) & 0x01U) != 0U;
                        dst[bit] = set ? fgHiResRgb : bgRgb;
                    }
                }
            }
        }

        charIdx += interleaved;
    }
}
