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

// CPU scanline rasterizer for the dirty-tracked 16-column VIC-II tile sheet cache.

#include "core/tileatlascache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <QColor>
#include <QRgb>

#include "palette.h"

void TileAtlasCache::markTileDirty(int tile_index) noexcept
{
    if (tile_index >= 0 && tile_index < kMaxTiles) {
        _dirtyTiles.set(static_cast<size_t>(tile_index));
    }
}

void TileAtlasCache::markAllDirty() noexcept
{
    _allDirty = true;
    _dirtyTiles.set();
}

void TileAtlasCache::sync(const State& state)
{
    const State::TileProperties tile_props = state.getTileProperties();
    const int tile_width = std::clamp(tile_props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int tile_height = std::clamp(tile_props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(tile_props.interleaved, 1, 256);
    const int max_tiles = std::max(1, 256 / (tile_width * tile_height));

    const int active_palette = Palette::activePalette();
    const bool is_multicolor_mode = state.isMulticolorMode();
    const State::ForegroundColorMode foreground_mode = state.getForegroundColorMode();
    const int background_color = state.getColorForPen(State::PEN_BACKGROUND);
    const int multicolor1_color = state.getColorForPen(State::PEN_MULTICOLOR1);
    const int multicolor2_color = state.getColorForPen(State::PEN_MULTICOLOR2);
    const int global_foreground_color = state.getColorForPen(State::PEN_FOREGROUND);

    // Detect MDI active-document switches (`_cachedState != &state`), global VIC-II palette
    // switches (`_cachedPaletteIndex != active_palette`), and document-wide geometry or register
    // changes so shared dock widgets never display stale tiles from a previous document or palette.
    const bool context_changed = (_cachedState != &state)
        || (_cachedPaletteIndex != active_palette)
        || (_cachedTileSize != QSize(tile_width, tile_height))
        || (_cachedInterleaved != interleaved)
        || (_cachedMulticolorMode != is_multicolor_mode)
        || (_cachedFgMode != foreground_mode)
        || (_cachedBgColor != background_color)
        || (_cachedMc1Color != multicolor1_color)
        || (_cachedMc2Color != multicolor2_color)
        || (foreground_mode == State::FOREGROUND_COLOR_GLOBAL && _cachedGlobalFgColor != global_foreground_color);

    _tilePixelWidth = tile_width * 8;
    _tilePixelHeight = tile_height * 8;
    _maxTiles = max_tiles;

    const int sheet_rows = std::max(1, (max_tiles + kColumns - 1) / kColumns);
    const QSize required_atlas_size(kColumns * _tilePixelWidth, sheet_rows * _tilePixelHeight);

    // Use Format_ARGB32_Premultiplied so Qt's raster paint engine blits sub-rectangles
    // directly without per-pixel format conversion during QPainter::drawImage().
    if (_atlas.size() != required_atlas_size || _atlas.format() != QImage::Format_ARGB32_Premultiplied) {
        _atlas = QImage(required_atlas_size, QImage::Format_ARGB32_Premultiplied);
        _atlas.fill(0U);
        _allDirty = true;
    }

    if (_allDirty || context_changed) {
        _dirtyTiles.set();
        _allDirty = false;
        _cachedState = &state;
        _cachedPaletteIndex = active_palette;
        _cachedTileSize = QSize(tile_width, tile_height);
        _cachedInterleaved = interleaved;
        _cachedMulticolorMode = is_multicolor_mode;
        _cachedFgMode = foreground_mode;
        _cachedBgColor = background_color;
        _cachedMc1Color = multicolor1_color;
        _cachedMc2Color = multicolor2_color;
        _cachedGlobalFgColor = global_foreground_color;
    }

    int synced_count = 0;
    for (int tile_index = 0; tile_index < max_tiles; ++tile_index) {
        if (!_dirtyTiles.test(static_cast<size_t>(tile_index))) {
            continue;
        }
        rasterizeTile(state, tile_index, tile_width, tile_height, interleaved);
        _dirtyTiles.reset(static_cast<size_t>(tile_index));
        ++synced_count;
    }
    _dirtyTiles.reset();
    _lastSyncedTileCount = synced_count;
}

QRect TileAtlasCache::sourceRectForTile(int tile_index) const noexcept
{
    const int safe_max_tiles = std::max(1, _maxTiles);
    const int clamped_tile_index = std::clamp(tile_index, 0, safe_max_tiles - 1);
    const int column = clamped_tile_index % kColumns;
    const int row = clamped_tile_index / kColumns;
    return QRect(column * _tilePixelWidth, row * _tilePixelHeight, _tilePixelWidth, _tilePixelHeight);
}

bool TileAtlasCache::isTileDirty(int tile_index) const noexcept
{
    if (_allDirty) {
        return true;
    }
    if (tile_index < 0 || tile_index >= kMaxTiles) {
        return false;
    }
    return _dirtyTiles.test(static_cast<size_t>(tile_index));
}

void TileAtlasCache::rasterizeTile(
    const State& state,
    int tile_index,
    int tile_width,
    int tile_height,
    int interleaved)
{
    const State::CharsetBuffer& charset = state.charset();
    const bool is_multicolor = state.shouldBeDisplayedInMulticolor2(tile_index);

    const QRgb background_rgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba());
    const QRgb multicolor1_rgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR1)).rgba());
    const QRgb multicolor2_rgb = qPremultiply(Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR2)).rgba());
    const int raw_foreground = state.getColorForPen(State::PEN_FOREGROUND, tile_index);
    const QRgb foreground_hires_rgb = qPremultiply(Palette::getColor(raw_foreground).rgba());
    // In VIC-II multicolor character mode ($D016 bit 4 == 1 and Color RAM bit 3 == 1),
    // bits 0..2 of Color RAM select one of the lower 8 foreground colors (0..7).
    const QRgb foreground_multicolor_rgb = qPremultiply(
        Palette::getColor(static_cast<int>(static_cast<unsigned int>(raw_foreground) & 0x07U)).rgba());
    const std::array<QRgb, 4> multicolor_palette = {
        background_rgb,
        multicolor1_rgb,
        multicolor2_rgb,
        foreground_multicolor_rgb,
    };

    const int base_origin_x = (tile_index % kColumns) * _tilePixelWidth;
    const int base_origin_y = (tile_index / kColumns) * _tilePixelHeight;

    int char_index = (interleaved == 1) ? (tile_index * tile_width * tile_height) : tile_index;
    const int total_sub_chars = tile_width * tile_height;

    for (int sub_index = 0; sub_index < total_sub_chars; ++sub_index) {
        if (char_index < 0 || char_index >= 256) {
            char_index += interleaved;
            continue;
        }

        const int sub_x = (sub_index % tile_width) * 8;
        const int sub_y = (sub_index / tile_width) * 8;
        const size_t char_byte_base = static_cast<size_t>(char_index) * 8U;

        for (int row = 0; row < 8; ++row) {
            const uint8_t row_byte = charset[char_byte_base + static_cast<size_t>(row)];
            QRgb* const scanline = static_cast<QRgb*>(
                static_cast<void*>(_atlas.scanLine(base_origin_y + sub_y + row)));
            QRgb* dest_pixels = scanline + base_origin_x + sub_x;

            if (is_multicolor) {
                // Multicolor row: 4 double-wide 2-bit pixels [b7b6 | b5b4 | b3b2 | b1b0].
                for (unsigned int pair_index = 0; pair_index < 4U; ++pair_index) {
                    const unsigned int shift = 6U - (pair_index * 2U);
                    const size_t pen_bits = static_cast<size_t>((row_byte >> shift) & 0x03U);
                    const QRgb pixel_rgb = multicolor_palette[pen_bits];
                    dest_pixels[pair_index * 2U] = pixel_rgb;
                    dest_pixels[pair_index * 2U + 1U] = pixel_rgb;
                }
            } else {
                // Hi-Res row: 8 single-wide 1-bit pixels [b7 .. b0].
                for (unsigned int bit = 0; bit < 8U; ++bit) {
                    const bool is_pixel_set = ((row_byte >> (7U - bit)) & 0x01U) != 0U;
                    dest_pixels[bit] = is_pixel_set ? foreground_hires_rgb : background_rgb;
                }
            }
        }

        char_index += interleaved;
    }
}
