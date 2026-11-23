/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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

// VIC-II character-cell rasterizers and 8-bit screen-code lookup tables.
//
// Uses std::ceil QRectF geometry in utilsDrawCharInPainter() so fractional BigCharWidget
// zoom scales leave zero hairline gaps between adjacent pixels, and masks multicolor 0b11
// foreground pens to 3 bits (& 0x07U) to match hardware VIC-II Color RAM behavior.

#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QSizeF>

#include "palette.h"
#include "state.h"

namespace {

constexpr std::array<uint8_t, 4> kMulticolorMasks = { 192U, 48U, 12U, 3U };
constexpr std::array<uint8_t, 8> kHiResMasks = { 128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U };

// Table taken from Contiki OS:
// https://github.com/contiki-os/contiki/blob/master/core/lib/petsciiconv.c
// clang-format off
constexpr std::array<uint8_t, 128> kAsciiToPetscii = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x14,0x09,0x0d,0x11,0x93,0x0a,0x0e,0x0f,
    0x10,0x0b,0x12,0x13,0x08,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
    0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
    0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
    0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
    0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
    0xd8,0xd9,0xda,0x5b,0x5c,0x5d,0x5e,0x5f,
    0xc0,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
    0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
    0x58,0x59,0x5a,0xdb,0xdd,0xdd,0x5e,0xdf,
};
// clang-format on

// Resolves a pixel bit pattern (`0..1` in Hi-Res, `0..3` in Multicolor) for `tile_index`
// to its 4-bit C64 palette color index (`0..15`), applying 3-bit hardware Color RAM
// masking for Multicolor foreground (`0x3U`) pixels.
[[nodiscard]] int ResolveCharPixelColorIndex(
    const State& state,
    int tile_index,
    bool is_multicolor,
    uint8_t pixel_bits)
{
    const State::TileColorsBuffer& tile_colors = state.getTileColors();
    const size_t safe_tile_index = static_cast<size_t>(
        std::clamp(tile_index, 0, State::TILE_COLORS_BUFFER_SIZE - 1));

    switch (pixel_bits) {
    case 0x0U:
        return state.getColorForPen(State::PEN_BACKGROUND);
    case 0x1U:
        if (is_multicolor) {
            return state.getColorForPen(State::PEN_MULTICOLOR1);
        }
        return (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
            ? state.getColorForPen(State::PEN_FOREGROUND)
            : static_cast<int>(tile_colors[safe_tile_index] & 0x0FU);
    case 0x2U:
        return state.getColorForPen(State::PEN_MULTICOLOR2);
    case 0x3U: {
        const int raw_foreground = (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
            ? state.getColorForPen(State::PEN_FOREGROUND)
            : static_cast<int>(tile_colors[safe_tile_index] & 0x0FU);
        return static_cast<int>(static_cast<unsigned int>(raw_foreground) & 0x07U);
    }
    default:
        return 0;
    }
}

} // namespace

void utilsDrawCharInPainter(
    const State* state,
    QPainter* painter,
    const QSizeF& pixel_size,
    const QPoint& offset,
    const QPoint& sub_char_origin,
    int char_index)
{
    if (state == nullptr || painter == nullptr || char_index < 0 || char_index >= 256) {
        return;
    }

    const State::CharsetBuffer& charset = state->getCharsetBuffer();
    const int tile_index = state->getTileIndexFromCharIndex(char_index);
    const bool is_multicolor = state->shouldBeDisplayedInMulticolor2(tile_index);

    const uint8_t* char_bytes = &charset[static_cast<size_t>(char_index) * 8U];
    const int char_width = is_multicolor ? 4 : 8;
    const int bit_width = is_multicolor ? 2 : 1;
    const uint8_t* masks = is_multicolor ? kMulticolorMasks.data() : kHiResMasks.data();

    for (int row = 0; row < 8; ++row) {
        const uint8_t row_byte = char_bytes[row];
        for (int col = 0; col < char_width; ++col) {
            const unsigned int shift = static_cast<unsigned int>((8 - bit_width) - col * bit_width);
            const uint8_t pixel_bits = static_cast<uint8_t>((row_byte & masks[col]) >> shift);
            const int color_index = ResolveCharPixelColorIndex(*state, tile_index, is_multicolor, pixel_bits);

            painter->setBrush(Palette::getColor(color_index));
            const QRectF rect(
                static_cast<qreal>(sub_char_origin.x() * 8 + col * bit_width) * pixel_size.width()
                    + static_cast<qreal>(offset.x()),
                static_cast<qreal>(sub_char_origin.y() * 8 + row) * pixel_size.height()
                    + static_cast<qreal>(offset.y()),
                std::ceil(pixel_size.width() * static_cast<qreal>(bit_width)),
                std::ceil(pixel_size.height()));
            painter->drawRect(rect);
        }
    }
}

void utilsDrawCharInImage(
    const State* state,
    QImage* image,
    const QPoint& offset,
    int char_index)
{
    if (state == nullptr || image == nullptr || char_index < 0 || char_index >= 256) {
        return;
    }

    const State::CharsetBuffer& charset = state->getCharsetBuffer();
    const int tile_index = state->getTileIndexFromCharIndex(char_index);
    const bool is_multicolor = state->shouldBeDisplayedInMulticolor2(tile_index);

    const uint8_t* char_bytes = &charset[static_cast<size_t>(char_index) * 8U];
    const int char_width = is_multicolor ? 4 : 8;
    const int bit_width = is_multicolor ? 2 : 1;
    const uint8_t* masks = is_multicolor ? kMulticolorMasks.data() : kHiResMasks.data();

    for (int row = 0; row < 8; ++row) {
        const uint8_t row_byte = char_bytes[row];
        for (int col = 0; col < char_width; ++col) {
            const unsigned int shift = static_cast<unsigned int>((8 - bit_width) - col * bit_width);
            const uint8_t pixel_bits = static_cast<uint8_t>((row_byte & masks[col]) >> shift);
            const int color_index = ResolveCharPixelColorIndex(*state, tile_index, is_multicolor, pixel_bits);
            const QRgb rgb = Palette::getColor(color_index).rgb();

            if (is_multicolor) {
                image->setPixel(col * 2 + offset.x(), row + offset.y(), rgb);
                image->setPixel(col * 2 + 1 + offset.x(), row + offset.y(), rgb);
            } else {
                image->setPixel(col + offset.x(), row + offset.y(), rgb);
            }
        }
    }
}

quint8 utilsAsciiToScreenCode(const State* state, quint8 ascii)
{
    if (state == nullptr) {
        return 0U;
    }

    switch (state->getKeyboardMapping()) {
    case State::KeyboardMapping::KEYBOARD_MAPPING_C64:
        return utilsAsciiToCommodore8Bit(ascii);
    case State::KeyboardMapping::KEYBOARD_MAPPING_ATARI8:
        return utilsAsciiToAtari8Bit(ascii);
    }
    return 0U;
}

quint8 utilsAsciiToCommodore8Bit(quint8 ascii)
{
    if (ascii >= 128U) {
        return ascii;
    }

    uint8_t petscii = kAsciiToPetscii[ascii];

    // Convert PETSCII to C64 screen code: http://sta.c64.org/cbm64pettoscr.html
    if (petscii <= 31U) {
        petscii = static_cast<uint8_t>(petscii + 128U);
    } else if (petscii <= 63U) {
        // PETSCII values 32..63 already match C64 screen codes without adjustment.
    } else if (petscii <= 95U) {
        petscii = static_cast<uint8_t>(petscii - 64U);
    } else if (petscii <= 127U) {
        petscii = static_cast<uint8_t>(petscii - 32U);
    } else if (petscii <= 159U) {
        petscii = static_cast<uint8_t>(petscii + 64U);
    } else if (petscii <= 191U) {
        petscii = static_cast<uint8_t>(petscii - 64U);
    } else if (petscii <= 254U) {
        petscii = static_cast<uint8_t>(petscii - 128U);
    } else {
        petscii = 94U;
    }

    return petscii;
}

quint8 utilsAsciiToAtari8Bit(quint8 ascii)
{
    if (ascii >= 128U) {
        return ascii;
    }

    if (ascii <= 0x1FU) {
        return 0U;
    }
    if (ascii <= 0x5FU) {
        return static_cast<quint8>(ascii - 0x20U);
    }
    return ascii;
}
