/****************************************************************************
Copyright 2015 Ricardo Quesada

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

#include "palette.h"

/**
 * @file palette.cpp
 * @brief Static VIC-II RGB color tables and runtime-localized C64 color name lookup.
 */

#include <algorithm>
#include <array>

#include <QCoreApplication>

#include "state.h"

namespace {

const std::array<std::array<QColor, Palette::kColorCount>, Palette::kPaletteCount> kPalettes = { {
    { {
        // Pepto palette. Taken from: http://www.pepto.de/projects/colorvic/
        { 0x00, 0x00, 0x00 },
        { 0xFF, 0xFF, 0xFF },
        { 0x68, 0x37, 0x2B },
        { 0x70, 0xA4, 0xB2 },
        { 0x6F, 0x3D, 0x86 },
        { 0x58, 0x8D, 0x43 },
        { 0x35, 0x28, 0x79 },
        { 0xB8, 0xC7, 0x6F },
        { 0x6F, 0x4F, 0x25 },
        { 0x43, 0x39, 0x00 },
        { 0x9A, 0x67, 0x59 },
        { 0x44, 0x44, 0x44 },
        { 0x6C, 0x6C, 0x6C },
        { 0x9A, 0xD2, 0x84 },
        { 0x6C, 0x5E, 0xB5 },
        { 0x95, 0x95, 0x95 },
    } },
    { {
        // VICE palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64
        { 0x00, 0x00, 0x00 },
        { 0xff, 0xff, 0xff },
        { 0xbd, 0x18, 0x21 },
        { 0x31, 0xe7, 0xc6 },
        { 0xb5, 0x18, 0xe7 },
        { 0x18, 0xd6, 0x18 },
        { 0x21, 0x18, 0xad },
        { 0xde, 0xf7, 0x08 },
        { 0xbd, 0x42, 0x00 },
        { 0x6b, 0x31, 0x00 },
        { 0xff, 0x4a, 0x52 },
        { 0x42, 0x42, 0x42 },
        { 0x73, 0x73, 0x6b },
        { 0x5a, 0xff, 0x5a },
        { 0x5a, 0x52, 0xff },
        { 0xa5, 0xa5, 0xa5 },
    } },
    { {
        // CCS64 palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64
        { 0x10, 0x10, 0x10 },
        { 0xff, 0xff, 0xff },
        { 0xe0, 0x40, 0x40 },
        { 0x60, 0xff, 0xff },
        { 0xe0, 0x60, 0xe0 },
        { 0x40, 0xe0, 0x40 },
        { 0x40, 0x40, 0xe0 },
        { 0xff, 0xff, 0x40 },
        { 0xe0, 0xa0, 0x40 },
        { 0x9c, 0x74, 0x48 },
        { 0xff, 0xa0, 0xa0 },
        { 0x54, 0x54, 0x54 },
        { 0x88, 0x88, 0x88 },
        { 0xa0, 0xff, 0xa0 },
        { 0xa0, 0xa0, 0xff },
        { 0xc0, 0xc0, 0xc0 },
    } },
    { {
        // FRODO palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64
        { 0x00, 0x00, 0x00 },
        { 0xff, 0xff, 0xff },
        { 0xcc, 0x00, 0x00 },
        { 0x00, 0xff, 0xcc },
        { 0xff, 0x00, 0xff },
        { 0x00, 0xcc, 0x00 },
        { 0x00, 0x00, 0xcc },
        { 0xff, 0xff, 0x00 },
        { 0xff, 0x88, 0x00 },
        { 0x88, 0x44, 0x00 },
        { 0xff, 0x88, 0x88 },
        { 0x44, 0x44, 0x44 },
        { 0x88, 0x88, 0x88 },
        { 0x88, 0xff, 0x88 },
        { 0x88, 0x88, 0xff },
        { 0xcc, 0xcc, 0xcc },
    } },
    { {
        // Pepto to grayscale using: r * 0.299 + g * 0.587 + b * 0.114
        { 0x00, 0x00, 0x00 },
        { 0xff, 0xff, 0xff },
        { 0x44, 0x44, 0x44 },
        { 0x96, 0x96, 0x96 },
        { 0x54, 0x54, 0x54 },
        { 0x74, 0x74, 0x74 },
        { 0x35, 0x35, 0x35 },
        { 0xb8, 0xb8, 0xb8 },
        { 0x53, 0x53, 0x53 },
        { 0x35, 0x35, 0x35 },
        { 0x74, 0x74, 0x74 },
        { 0x44, 0x44, 0x44 },
        { 0x6b, 0x6b, 0x6b },
        { 0xb8, 0xb8, 0xb8 },
        { 0x6c, 0x6c, 0x6c },
        { 0x94, 0x94, 0x94 },
    } },
    { {
        // Colodore 50 / 100 / 50 palette. Taken from: http://www.colodore.com
        { 0x00, 0x00, 0x00 },
        { 0xFE, 0xFE, 0xFE },
        { 0x81, 0x33, 0x37 },
        { 0x75, 0xCE, 0xC8 },
        { 0x8D, 0x3B, 0x97 },
        { 0x55, 0xAC, 0x4C },
        { 0x2D, 0x2B, 0x9A },
        { 0xED, 0xF0, 0x71 },
        { 0x8D, 0x50, 0x29 },
        { 0x54, 0x37, 0x00 },
        { 0x63, 0x6C, 0x71 },
        { 0x49, 0x49, 0x49 },
        { 0x7B, 0x7B, 0x7B },
        { 0xA9, 0xFE, 0x9F },
        { 0x6F, 0x6D, 0xEB },
        { 0xB1, 0xB1, 0xB1 },
    } },
} };

// Cast through unsigned before `& 0x0FU` so negative signed values (such as `-1` from
// unmasked `char` bytes) fold cleanly into `[0, 15]` without implementation-defined behavior.
[[nodiscard]] constexpr size_t maskColorIndex(int colorIndex) noexcept
{
    return static_cast<size_t>(static_cast<unsigned int>(colorIndex) & 0x0FU);
}

} // namespace

int Palette::_paletteIndex = Palette::PEPTO;

const QColor& Palette::getColorForPen(const State* state, int pen)
{
    constexpr std::array<int, 4> kDefaultColors = { 1, 5, 7, 11 };
    const int clampedPen = std::clamp(pen, 0, State::PEN_MAX - 1);
    int colorIndex = kDefaultColors[static_cast<size_t>(clampedPen)];

    if (state != nullptr) {
        const int tileIdx = state->getTileIndex();
        colorIndex = state->getColorForPen(clampedPen, tileIdx);

        // In VIC-II multicolor character mode, foreground Color RAM bit 3 selects
        // multicolor mode while bits 0..2 select the displayed foreground color (0..7).
        if (clampedPen == State::PEN_FOREGROUND && state->shouldBeDisplayedInMulticolor2(tileIdx)) {
            colorIndex = static_cast<int>(static_cast<unsigned int>(colorIndex) & 0x07U);
        }
    }

    return getColor(colorIndex);
}

const QColor& Palette::getColor(int colorIndex)
{
    const auto activeIdx = static_cast<size_t>(std::clamp(_paletteIndex, 0, kPaletteCount - 1));
    return kPalettes[activeIdx][maskColorIndex(colorIndex)];
}

QString Palette::colorName(int colorIndex)
{
    // Store raw untranslated literals via QT_TR_NOOP and invoke tr() dynamically on each call.
    // Static QString arrays initialized with tr() before main() would freeze English strings
    // prior to QTranslator installation.
    static constexpr std::array<const char*, kColorCount> kColorNames = {
        QT_TR_NOOP("Black"),
        QT_TR_NOOP("White"),
        QT_TR_NOOP("Red"),
        QT_TR_NOOP("Cyan"),
        QT_TR_NOOP("Violet"),
        QT_TR_NOOP("Green"),
        QT_TR_NOOP("Blue"),
        QT_TR_NOOP("Yellow"),
        QT_TR_NOOP("Orange"),
        QT_TR_NOOP("Brown"),
        QT_TR_NOOP("Light red"),
        QT_TR_NOOP("Dark grey"),
        QT_TR_NOOP("Grey"),
        QT_TR_NOOP("Light green"),
        QT_TR_NOOP("Light blue"),
        QT_TR_NOOP("Light grey"),
    };
    return tr(kColorNames[maskColorIndex(colorIndex)]);
}

void Palette::setActivePalette(int paletteIndex) noexcept
{
    _paletteIndex = std::clamp(paletteIndex, 0, kPaletteCount - 1);
}

int Palette::activePalette() noexcept
{
    return _paletteIndex;
}

int Palette::getActivePalette() noexcept
{
    return _paletteIndex;
}
