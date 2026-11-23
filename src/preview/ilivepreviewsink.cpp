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

// Bounds-checked 40x25 C64 Screen RAM and $D800 Color RAM layout generators (vchar64_net).

#include "preview/ilivepreviewsink.h"

#include <cstddef>
#include <cstdint>

namespace vchar64 {

namespace {

    // Returns true if `props` describes a non-degenerate 1..8 x 1..8 tile grid within 256 chars.
    [[nodiscard]] bool HasValidTileGeometry(const State::TileProperties& props) noexcept
    {
        const int tile_width = props.size.width();
        const int tile_height = props.size.height();
        if (tile_width < 1 || tile_width > State::MAX_TILE_WIDTH
            || tile_height < 1 || tile_height > State::MAX_TILE_HEIGHT) {
            return false;
        }
        const int chars_per_tile = tile_width * tile_height;
        if (chars_per_tile < 1 || chars_per_tile > 256) {
            return false;
        }
        return props.interleaved >= 1 && props.interleaved <= 256;
    }

} // namespace

C64ScreenBuffer buildC64PreviewScreen(const State::TileProperties& props) noexcept
{
    C64ScreenBuffer screen { };
    screen.fill(0x20U);

    // Top 8 rows (columns 0..31): raw 256-character set (0..255).
    for (int row = 0; row < kC64CharsetRows; ++row) {
        for (int col = 0; col < kC64CharsetCols; ++col) {
            const uint8_t char_index = static_cast<uint8_t>(row * kC64CharsetCols + col);
            const size_t offset = static_cast<size_t>(row * kC64ScreenCols + col);
            screen[offset] = char_index;
        }
    }

    if (!HasValidTileGeometry(props)) {
        return screen;
    }

    const int tile_width = props.size.width();
    const int tile_height = props.size.height();
    const int chars_per_tile = tile_width * tile_height;
    const int max_tiles = 256 / chars_per_tile;

    int col = 0;
    int row = kC64TileStartRow;

    for (int tile_index = 0; tile_index < max_tiles; ++tile_index) {
        if (col + tile_width > kC64TileGridCols) {
            col = 0;
            row += tile_height;
        }
        // Whole-tile fit guard: stop before placing any tile whose right or bottom
        // edge would exceed the 40x25 Screen RAM (e.g., 7x4 at row 24 or 7x7 at row 19).
        if (col + tile_width > kC64ScreenCols || row + tile_height > kC64ScreenRows) {
            break;
        }

        int char_index = (props.interleaved == 1) ? (tile_index * chars_per_tile) : tile_index;
        for (int cell = 0; cell < chars_per_tile; ++cell) {
            const int local_col = col + (cell % tile_width);
            const int local_row = row + (cell / tile_width);
            const size_t offset = static_cast<size_t>(local_row * kC64ScreenCols + local_col);
            screen[offset] = static_cast<uint8_t>(static_cast<unsigned int>(char_index) & 0xFFU);
            char_index += props.interleaved;
        }

        col += tile_width;
    }

    return screen;
}

C64ScreenBuffer buildC64PreviewScreen(const State& state) noexcept
{
    return buildC64PreviewScreen(state.getTileProperties());
}

C64ScreenBuffer buildC64PreviewColorRam(const State& state) noexcept
{
    C64ScreenBuffer color_ram { };

    if (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL) {
        const uint8_t global_foreground = static_cast<uint8_t>(
            static_cast<unsigned int>(state.getGlobalColorForPen(State::PEN_FOREGROUND)) & 0x0FU);
        color_ram.fill(global_foreground);
        return color_ram;
    }

    color_ram.fill(0x08U);
    const State::TileColorsBuffer& tile_colors = state.getTileColors();

    // Top 8 rows (columns 0..31): per-tile foreground color mapped from each character's tile.
    for (int row = 0; row < kC64CharsetRows; ++row) {
        for (int col = 0; col < kC64CharsetCols; ++col) {
            const int char_index = row * kC64CharsetCols + col;
            const int tile_index = state.getTileIndexFromCharIndex(char_index);
            if (tile_index >= 0 && tile_index < State::TILE_COLORS_BUFFER_SIZE) {
                const size_t offset = static_cast<size_t>(row * kC64ScreenCols + col);
                color_ram[offset] = static_cast<uint8_t>(
                    tile_colors[static_cast<size_t>(tile_index)] & 0x0FU);
            }
        }
    }

    const State::TileProperties tile_props = state.getTileProperties();
    if (!HasValidTileGeometry(tile_props)) {
        return color_ram;
    }

    const int tile_width = tile_props.size.width();
    const int tile_height = tile_props.size.height();
    const int chars_per_tile = tile_width * tile_height;
    const int max_tiles = 256 / chars_per_tile;

    int col = 0;
    int row = kC64TileStartRow;

    for (int tile_index = 0; tile_index < max_tiles; ++tile_index) {
        if (col + tile_width > kC64TileGridCols) {
            col = 0;
            row += tile_height;
        }
        if (col + tile_width > kC64ScreenCols || row + tile_height > kC64ScreenRows) {
            break;
        }

        const uint8_t tile_color = static_cast<uint8_t>(
            tile_colors[static_cast<size_t>(tile_index)] & 0x0FU);
        for (int cell_y = 0; cell_y < tile_height; ++cell_y) {
            for (int cell_x = 0; cell_x < tile_width; ++cell_x) {
                const size_t offset = static_cast<size_t>((row + cell_y) * kC64ScreenCols + (col + cell_x));
                color_ram[offset] = tile_color;
            }
        }

        col += tile_width;
    }

    return color_ram;
}

} // namespace vchar64

ILivePreviewSink::ILivePreviewSink(QObject* parent)
    : QObject(parent)
{
}

ILivePreviewSink::~ILivePreviewSink() = default;
