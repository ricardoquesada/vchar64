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

/**
 * @file ilivepreviewsink.cpp
 * @brief Bounds-checked `40x25` C64 Screen RAM and `$D800` Color RAM layout generators (`vchar64_net`).
 */

#include "preview/ilivepreviewsink.h"

namespace vchar64 {

namespace {

    /** @brief Validates that `props` describes a non-degenerate `1..8 x 1..8` tile grid (`<= 256` chars). */
    [[nodiscard]] bool hasValidTileGeometry(const State::TileProperties& props) noexcept
    {
        const int tw = props.size.width();
        const int th = props.size.height();
        if (tw < 1 || tw > State::MAX_TILE_WIDTH || th < 1 || th > State::MAX_TILE_HEIGHT) {
            return false;
        }
        const int charsPerTile = tw * th;
        if (charsPerTile < 1 || charsPerTile > 256) {
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
            const auto charIdx = static_cast<uint8_t>(row * kC64CharsetCols + col);
            const auto offset = static_cast<size_t>(row * kC64ScreenCols + col);
            screen[offset] = charIdx;
        }
    }

    if (!hasValidTileGeometry(props)) {
        return screen;
    }

    const int tw = props.size.width();
    const int th = props.size.height();
    const int charsPerTile = tw * th;
    const int maxTiles = 256 / charsPerTile;

    int col = 0;
    int row = kC64TileStartRow;

    for (int tileIdx = 0; tileIdx < maxTiles; ++tileIdx) {
        if (col + tw > kC64TileGridCols) {
            col = 0;
            row += th;
        }
        // Whole-tile fit guard: stop before placing any tile whose right or bottom
        // edge would exceed the 40x25 Screen RAM (e.g., 7x4 at row 24 or 7x7 at row 19).
        if (col + tw > kC64ScreenCols || row + th > kC64ScreenRows) {
            break;
        }

        int charIdx = (props.interleaved == 1) ? (tileIdx * charsPerTile) : tileIdx;
        for (int cell = 0; cell < charsPerTile; ++cell) {
            const int localCol = col + (cell % tw);
            const int localRow = row + (cell / tw);
            const auto offset = static_cast<size_t>(localRow * kC64ScreenCols + localCol);
            screen[offset] = static_cast<uint8_t>(static_cast<unsigned int>(charIdx) & 0xFFU);
            charIdx += props.interleaved;
        }

        col += tw;
    }

    return screen;
}

C64ScreenBuffer buildC64PreviewScreen(const State& state) noexcept
{
    return buildC64PreviewScreen(state.getTileProperties());
}

C64ScreenBuffer buildC64PreviewColorRam(const State& state) noexcept
{
    C64ScreenBuffer colorRam { };

    if (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL) {
        const auto globalFg = static_cast<uint8_t>(
            static_cast<unsigned int>(state.getGlobalColorForPen(State::PEN_FOREGROUND)) & 0x0FU);
        colorRam.fill(globalFg);
        return colorRam;
    }

    colorRam.fill(0x08U);
    const auto& tileColors = state.getTileColors();

    // Top 8 rows (columns 0..31): per-tile foreground color mapped from each character's tile.
    for (int row = 0; row < kC64CharsetRows; ++row) {
        for (int col = 0; col < kC64CharsetCols; ++col) {
            const int charIdx = row * kC64CharsetCols + col;
            const int tileIdx = state.getTileIndexFromCharIndex(charIdx);
            if (tileIdx >= 0 && tileIdx < State::TILE_COLORS_BUFFER_SIZE) {
                const auto offset = static_cast<size_t>(row * kC64ScreenCols + col);
                colorRam[offset] = static_cast<uint8_t>(
                    tileColors[static_cast<size_t>(tileIdx)] & 0x0FU);
            }
        }
    }

    const State::TileProperties props = state.getTileProperties();
    if (!hasValidTileGeometry(props)) {
        return colorRam;
    }

    const int tw = props.size.width();
    const int th = props.size.height();
    const int charsPerTile = tw * th;
    const int maxTiles = 256 / charsPerTile;

    int col = 0;
    int row = kC64TileStartRow;

    for (int tileIdx = 0; tileIdx < maxTiles; ++tileIdx) {
        if (col + tw > kC64TileGridCols) {
            col = 0;
            row += th;
        }
        if (col + tw > kC64ScreenCols || row + th > kC64ScreenRows) {
            break;
        }

        const auto tileColor = static_cast<uint8_t>(
            tileColors[static_cast<size_t>(tileIdx)] & 0x0FU);
        for (int cy = 0; cy < th; ++cy) {
            for (int cx = 0; cx < tw; ++cx) {
                const auto offset = static_cast<size_t>((row + cy) * kC64ScreenCols + (col + cx));
                colorRam[offset] = tileColor;
            }
        }

        col += tw;
    }

    return colorRam;
}

} // namespace vchar64

ILivePreviewSink::ILivePreviewSink(QObject* parent)
    : QObject(parent)
{
}

ILivePreviewSink::~ILivePreviewSink() = default;
