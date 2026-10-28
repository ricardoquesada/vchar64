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

/**
 * @file utils.cpp
 * @brief Implementation of VIC-II character-cell rasterizers and 8-bit screen-code lookup tables.
 *
 * Uses `std::ceil` `QRectF` geometry in `utilsDrawCharInPainter()` so fractional
 * `BigCharWidget` zoom scales leave zero hairline gaps between adjacent pixels,
 * and masks multicolor `0b11` foreground pens to 3 bits (`& 0x07U`) to match
 * hardware VIC-II Color RAM behavior.
 */

#include "utils.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QSizeF>

#include "palette.h"
#include "state.h"

namespace {

[[nodiscard]] int resolveCharPixelColorIndex(
    const State& state,
    int tileIdx,
    bool isMulticolor,
    uint8_t pixelBits)
{
    const auto& tileColors = state.getTileColors();
    const auto safeTileIdx = static_cast<size_t>(std::clamp(tileIdx, 0, 255));

    switch (pixelBits) {
    case 0x0U:
        return state.getColorForPen(State::PEN_BACKGROUND);
    case 0x1U:
        if (isMulticolor) {
            return state.getColorForPen(State::PEN_MULTICOLOR1);
        }
        return (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
            ? state.getColorForPen(State::PEN_FOREGROUND)
            : static_cast<int>(tileColors[safeTileIdx] & 0x0FU);
    case 0x2U:
        return state.getColorForPen(State::PEN_MULTICOLOR2);
    case 0x3U: {
        const int rawFg = (state.getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
            ? state.getColorForPen(State::PEN_FOREGROUND)
            : static_cast<int>(tileColors[safeTileIdx] & 0x0FU);
        return static_cast<int>(static_cast<unsigned int>(rawFg) & 0x07U);
    }
    default:
        return 0;
    }
}

} // namespace

void utilsDrawCharInPainter(
    const State* state,
    QPainter* painter,
    const QSizeF& pixelSize,
    const QPoint& offset,
    const QPoint& orig,
    int charIdx)
{
    if (state == nullptr || painter == nullptr || charIdx < 0 || charIdx >= 256) {
        return;
    }

    static constexpr std::array<uint8_t, 4> kMcMasks = { 192U, 48U, 12U, 3U };
    static constexpr std::array<uint8_t, 8> kHrMasks = { 128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U };

    const auto& charset = state->getCharsetBuffer();
    const int tileIdx = state->getTileIndexFromCharIndex(charIdx);
    const bool ismc = state->shouldBeDisplayedInMulticolor2(tileIdx);

    const uint8_t* chardef = &charset[static_cast<size_t>(charIdx) * 8U];
    const int charWidth = ismc ? 4 : 8;
    const int bitWidth = ismc ? 2 : 1;
    const uint8_t* masks = ismc ? kMcMasks.data() : kHrMasks.data();

    for (int row = 0; row < 8; ++row) {
        const uint8_t byte = chardef[row];
        for (int col = 0; col < charWidth; ++col) {
            const auto shift = static_cast<unsigned int>((8 - bitWidth) - col * bitWidth);
            const auto pixelBits = static_cast<uint8_t>((byte & masks[col]) >> shift);
            const int colorIndex = resolveCharPixelColorIndex(*state, tileIdx, ismc, pixelBits);

            painter->setBrush(Palette::getColor(colorIndex));
            const QRectF rect(
                static_cast<qreal>(orig.x() * 8 + col * bitWidth) * pixelSize.width()
                    + static_cast<qreal>(offset.x()),
                static_cast<qreal>(orig.y() * 8 + row) * pixelSize.height()
                    + static_cast<qreal>(offset.y()),
                std::ceil(pixelSize.width() * static_cast<qreal>(bitWidth)),
                std::ceil(pixelSize.height()));
            painter->drawRect(rect);
        }
    }
}

void utilsDrawCharInImage(
    const State* state,
    QImage* image,
    const QPoint& offset,
    int charIdx)
{
    if (state == nullptr || image == nullptr || charIdx < 0 || charIdx >= 256) {
        return;
    }

    static constexpr std::array<uint8_t, 4> kMcMasks = { 192U, 48U, 12U, 3U };
    static constexpr std::array<uint8_t, 8> kHrMasks = { 128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U };

    const auto& charset = state->getCharsetBuffer();
    const int tileIdx = state->getTileIndexFromCharIndex(charIdx);
    const bool ismc = state->shouldBeDisplayedInMulticolor2(tileIdx);

    const uint8_t* chardef = &charset[static_cast<size_t>(charIdx) * 8U];
    const int charWidth = ismc ? 4 : 8;
    const int bitWidth = ismc ? 2 : 1;
    const uint8_t* masks = ismc ? kMcMasks.data() : kHrMasks.data();

    for (int row = 0; row < 8; ++row) {
        const uint8_t byte = chardef[row];
        for (int col = 0; col < charWidth; ++col) {
            const auto shift = static_cast<unsigned int>((8 - bitWidth) - col * bitWidth);
            const auto pixelBits = static_cast<uint8_t>((byte & masks[col]) >> shift);
            const int colorIndex = resolveCharPixelColorIndex(*state, tileIdx, ismc, pixelBits);
            const QRgb rgb = Palette::getColor(colorIndex).rgb();

            if (ismc) {
                image->setPixel(col * 2 + offset.x(), row + offset.y(), rgb);
                image->setPixel(col * 2 + 1 + offset.x(), row + offset.y(), rgb);
            } else {
                image->setPixel(col + offset.x(), row + offset.y(), rgb);
            }
        }
    }
}

// Table taken from Contiki OS
// https://github.com/contiki-os/contiki/blob/master/core/lib/petsciiconv.c
// clang-format off
static constexpr std::array<uint8_t, 128> ascii2petscii = {
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

    uint8_t petscii = ascii2petscii[ascii];

    // Convert PETSCII to C64 screen code: http://sta.c64.org/cbm64pettoscr.html
    if (petscii <= 31U) {
        petscii = static_cast<uint8_t>(petscii + 128U);
    } else if (petscii <= 63U) {
        // unchanged
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
