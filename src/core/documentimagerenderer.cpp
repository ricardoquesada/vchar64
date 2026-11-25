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

// Headless QImage::Format_ARGB32 scanline renderer for charset, tileset, and map PNGs.
//
// Uses straight (unpremultiplied) Format_ARGB32 so transparent background pixels (0x00000000)
// and opaque VIC-II palette colors serialize losslessly through Qt's PNG writer without
// touching QPixmap or requiring an active QGuiApplication QPA plugin.

#include "core/documentimagerenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <QColor>
#include <QPoint>
#include <QRgb>
#include <QSize>

#include "palette.h"
#include "state.h"

namespace {

// Returns the straight ARGB32 fill color for pen 00 (transparent black or the state background).
[[nodiscard]] QRgb ResolveBackgroundRgb(const State& state, bool transparent_background)
{
    if (transparent_background) {
        return qRgba(0, 0, 0, 0);
    }
    return Palette::getColor(state.getGlobalColorForPen(State::PEN_BACKGROUND)).rgba();
}

// Returns the straight ARGB32 background color for `char_index`, keeping ECM
// background banks #1..#3 ($D022..$D024) opaque even when `transparent_background` is true.
[[nodiscard]] QRgb ResolveCharBackgroundRgb(
    const State& state,
    int char_index,
    bool transparent_background)
{
    const int bank = state.isExtendedColorMode()
        ? static_cast<int>((static_cast<unsigned int>(char_index) >> 6U) & 0x03U)
        : 0;
    if (transparent_background && bank == 0) {
        return qRgba(0, 0, 0, 0);
    }
    return Palette::getColor(state.getBackgroundColorForChar(char_index)).rgba();
}

// Rasterizes a single 8x8 glyph (`char_index` in 0..255) into `image` at `(dest_pixel_x, dest_pixel_y)`,
// applying the VIC-II Hi-Res, Multicolor, or Extended Color decoding mode of `tile_index`.
void DrawCharToImage(
    const State& state,
    QImage& image,
    int dest_pixel_x,
    int dest_pixel_y,
    int char_index,
    int tile_index,
    bool transparent_background)
{
    if (char_index < 0 || char_index >= 256) {
        return;
    }

    const State::CharsetBuffer& charset = state.charset();
    const bool is_ecm = state.isExtendedColorMode();
    const bool is_multicolor = state.shouldBeDisplayedInMulticolor2(tile_index);

    const QRgb background_rgb = ResolveCharBackgroundRgb(state, char_index, transparent_background);
    const QRgb multicolor1_rgb = Palette::getColor(state.getGlobalColorForPen(State::PEN_MULTICOLOR1)).rgba();
    const QRgb multicolor2_rgb = Palette::getColor(state.getGlobalColorForPen(State::PEN_MULTICOLOR2)).rgba();

    const int raw_foreground = state.getColorForPen(State::PEN_FOREGROUND, tile_index);
    const QRgb foreground_hires_rgb = Palette::getColor(raw_foreground).rgba();
    // VIC-II multicolor character mode restricts foreground Color RAM to pens 0..7.
    const QRgb foreground_multicolor_rgb = Palette::getColor(static_cast<int>(static_cast<unsigned int>(raw_foreground) & 0x07U)).rgba();
    const std::array<QRgb, 4> multicolor_palette = {
        background_rgb,
        multicolor1_rgb,
        multicolor2_rgb,
        foreground_multicolor_rgb,
    };

    const int physical_char = is_ecm
        ? static_cast<int>(static_cast<unsigned int>(char_index) & 0x3FU)
        : char_index;
    const size_t char_byte_base = static_cast<size_t>(physical_char) * 8U;
    for (int row = 0; row < 8; ++row) {
        const uint8_t row_byte = charset[char_byte_base + static_cast<size_t>(row)];
        QRgb* const scanline = static_cast<QRgb*>(static_cast<void*>(image.scanLine(dest_pixel_y + row)));
        QRgb* dest_pixels = scanline + dest_pixel_x;

        if (is_multicolor) {
            for (unsigned int pair_index = 0; pair_index < 4U; ++pair_index) {
                const unsigned int shift = 6U - (pair_index * 2U);
                const size_t pen_bits = static_cast<size_t>((row_byte >> shift) & 0x03U);
                const QRgb pixel_rgb = multicolor_palette[pen_bits];
                dest_pixels[pair_index * 2U] = pixel_rgb;
                dest_pixels[pair_index * 2U + 1U] = pixel_rgb;
            }
        } else {
            for (unsigned int bit = 0; bit < 8U; ++bit) {
                const bool is_pixel_set = ((row_byte >> (7U - bit)) & 0x01U) != 0U;
                dest_pixels[bit] = is_pixel_set ? foreground_hires_rgb : background_rgb;
            }
        }
    }
}

// Rasterizes all `tile_width x tile_height` sub-characters of `tile_index` into `image`
// starting at pixel coordinates `(dest_pixel_x, dest_pixel_y)`.
void DrawTileToImage(
    const State& state,
    QImage& image,
    int dest_pixel_x,
    int dest_pixel_y,
    int tile_index,
    int tile_width,
    int tile_height,
    int interleaved,
    bool transparent_background)
{
    int char_index = (interleaved == 1) ? (tile_index * tile_width * tile_height) : tile_index;
    const int sub_char_count = tile_width * tile_height;

    for (int sub_index = 0; sub_index < sub_char_count; ++sub_index) {
        const int sub_x = (sub_index % tile_width) * 8;
        const int sub_y = (sub_index / tile_width) * 8;
        DrawCharToImage(
            state,
            image,
            dest_pixel_x + sub_x,
            dest_pixel_y + sub_y,
            char_index,
            tile_index,
            transparent_background);
        char_index += interleaved;
    }
}

} // namespace

QImage DocumentImageRenderer::renderCharset(const State& state, bool transparent_background)
{
    constexpr int kColumns = 32;
    constexpr int kRows = 8;

    QImage image(kColumns * 8, kRows * 8, QImage::Format_ARGB32);
    image.fill(ResolveBackgroundRgb(state, transparent_background));

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int char_index = row * kColumns + col;
            const int tile_index = state.getTileIndexFromCharIndex(char_index);
            DrawCharToImage(state, image, col * 8, row * 8, char_index, tile_index, transparent_background);
        }
    }
    return image;
}

QImage DocumentImageRenderer::renderTileset(const State& state, bool transparent_background)
{
    constexpr int kBaseCharColumns = 32;
    const State::TileProperties tile_props = state.getTileProperties();
    const int tile_width = std::clamp(tile_props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int tile_height = std::clamp(tile_props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(tile_props.interleaved, 1, 256);

    const int tile_columns = std::max(1, kBaseCharColumns / tile_width);
    const int max_tiles = std::max(1, 256 / (tile_width * tile_height));
    const int tile_rows = std::max(1, (max_tiles + tile_columns - 1) / tile_columns);

    QImage image(tile_columns * tile_width * 8, tile_rows * tile_height * 8, QImage::Format_ARGB32);
    image.fill(ResolveBackgroundRgb(state, transparent_background));

    for (int tile_index = 0; tile_index < max_tiles; ++tile_index) {
        const int tile_x = (tile_index % tile_columns) * tile_width * 8;
        const int tile_y = (tile_index / tile_columns) * tile_height * 8;
        DrawTileToImage(
            state,
            image,
            tile_x,
            tile_y,
            tile_index,
            tile_width,
            tile_height,
            interleaved,
            transparent_background);
    }
    return image;
}

QImage DocumentImageRenderer::renderMap(const State& state, bool transparent_background)
{
    const QSize map_size = state.getMapSize();
    const int map_width = std::max(1, map_size.width());
    const int map_height = std::max(1, map_size.height());

    const State::TileProperties tile_props = state.getTileProperties();
    const int tile_width = std::clamp(tile_props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int tile_height = std::clamp(tile_props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(tile_props.interleaved, 1, 256);
    const int tile_pixel_width = tile_width * 8;
    const int tile_pixel_height = tile_height * 8;

    QImage image(map_width * tile_pixel_width, map_height * tile_pixel_height, QImage::Format_ARGB32);
    image.fill(ResolveBackgroundRgb(state, transparent_background));

    for (int map_y = 0; map_y < map_height; ++map_y) {
        for (int map_x = 0; map_x < map_width; ++map_x) {
            const int tile_index = state.getTileIndexFromMap(QPoint(map_x, map_y));
            DrawTileToImage(
                state,
                image,
                map_x * tile_pixel_width,
                map_y * tile_pixel_height,
                tile_index,
                tile_width,
                tile_height,
                interleaved,
                transparent_background);
        }
    }
    return image;
}
