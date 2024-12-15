/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "utils.h"

#include <QDebug>
#include <QPainter>
#include <QPoint>
#include <QSize>
#include <QtCore/qmath.h>

#include "palette.h"
#include "state.h"

void utilsDrawCharInPainter(State* state, QPainter* painter, const QSizeF& pixelSize, const QPoint& offset, const QPoint& orig, int charIdx)
{
    Q_ASSERT(charIdx >= 0 && charIdx < 256 && "Invalid charIdx");

    static const quint8 mc_masks[] = { 192, 48, 12, 3 };
    static const quint8 hr_masks[] = { 128, 64, 32, 16, 8, 4, 2, 1 };

    const auto& charset = state->getCharsetBuffer();
    const auto& tileColors = state->getTileColors();
    int tileIdx = state->getTileIndexFromCharIndex(charIdx);
    auto ismc = state->shouldBeDisplayedInMulticolor2(tileIdx);

    const quint8* chardef = &charset[charIdx * 8];

    for (int i = 0; i < 8; ++i) {
        quint8 byte = chardef[i];

        int char_width = 8;
        int bit_width = 1; /* 8 = 8 * 1 */
        const quint8* masks = &hr_masks[0];

        if (ismc) {
            char_width = 4;
            bit_width = 2; /* 8 = 4 * 2 */
            masks = mc_masks;
        }

        for (int j = 0; j < char_width; ++j) {
            quint8 colorIndex = 0;
            // get the two bits that reprent the color
            quint8 color = byte & masks[j];
            color >>= (8 - bit_width) - j * bit_width;

            switch (color) {
            // bitmask 00: background ($d021)
            case 0x0:
                colorIndex = state->getColorForPen(State::PEN_BACKGROUND);
                break;

            // bitmask 01: multicolor #1 ($d022)
            case 0x1:
                if (ismc)
                    colorIndex = state->getColorForPen(State::PEN_MULTICOLOR1);
                else {
                    if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
                        colorIndex = state->getColorForPen(State::PEN_FOREGROUND);
                    else
                        colorIndex = tileColors[tileIdx];
                }
                break;

            // bitmask 10: multicolor #2 ($d023)
            case 0x2:
                Q_ASSERT(ismc && "error in logic");
                colorIndex = state->getColorForPen(State::PEN_MULTICOLOR2);
                break;

            // bitmask 11: color RAM
            case 0x3:
                Q_ASSERT(ismc && "error in logic");
                if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
                    colorIndex = state->getColorForPen(State::PEN_FOREGROUND) - 8;
                else
                    colorIndex = tileColors[tileIdx] - 8;
                break;
            default:
                qDebug() << "utilsDrawCharInPainter: Invalid color: " << color << " at x,y=" << orig;
                break;
            }
            painter->setBrush(Palette::getColor(colorIndex));
            painter->drawRect((orig.x() * 8 + j * bit_width) * pixelSize.width() + offset.x(),
                (orig.y() * 8 + i) * pixelSize.height() + offset.y(),
                qCeil(pixelSize.width() * bit_width),
                qCeil(pixelSize.height()));
        }
    }
}

void utilsDrawCharInImage(State* state, QImage* image, const QPoint& offset, int charIdx)
{
    Q_ASSERT(charIdx >= 0 && charIdx < 256 && "Invalid charIdx");

    static const quint8 mc_masks[] = { 192, 48, 12, 3 };
    static const quint8 hr_masks[] = { 128, 64, 32, 16, 8, 4, 2, 1 };

    const auto& charset = state->getCharsetBuffer();
    auto tileColors = state->getTileColors();
    int tileIdx = state->getTileIndexFromCharIndex(charIdx);
    auto ismc = state->shouldBeDisplayedInMulticolor2(tileIdx);

    auto chardef = &charset[charIdx * 8];

    for (int i = 0; i < 8; ++i) {
        auto byte = chardef[i];

        int char_width = 8;
        int bit_width = 1; /* 8 = 8 * 1 */
        const quint8* masks = &hr_masks[0];

        if (ismc) {
            char_width = 4;
            bit_width = 2; /* 8 = 4 * 2 */
            masks = mc_masks;
        }

        for (int j = 0; j < char_width; ++j) {
            quint8 colorIndex = 0;
            // get the two bits that reprent the color
            quint8 color = byte & masks[j];
            color >>= (8 - bit_width) - j * bit_width;

            switch (color) {
            // bitmask 00: background ($d021)
            case 0x0:
                colorIndex = state->getColorForPen(State::PEN_BACKGROUND);
                break;

            // bitmask 01: multicolor #1 ($d022)
            case 0x1:
                if (ismc)
                    colorIndex = state->getColorForPen(State::PEN_MULTICOLOR1);
                else {
                    if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
                        colorIndex = state->getColorForPen(State::PEN_FOREGROUND);
                    else
                        colorIndex = tileColors[tileIdx];
                }
                break;

            // bitmask 10: multicolor #2 ($d023)
            case 0x2:
                Q_ASSERT(ismc && "error in logic");
                colorIndex = state->getColorForPen(State::PEN_MULTICOLOR2);
                break;

            // bitmask 11: color RAM
            case 0x3:
                Q_ASSERT(ismc && "error in logic");
                if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_GLOBAL)
                    colorIndex = state->getColorForPen(State::PEN_FOREGROUND) - 8;
                else
                    colorIndex = tileColors[tileIdx] - 8;
                break;
            default:
                qDebug() << "utilsDrawCharInImage: Invalid color: " << color;
                break;
            }
            auto rgb = Palette::getColor(colorIndex).rgb();
            if (ismc) {
                image->setPixel(j * 2 + offset.x(), i + offset.y(), rgb);
                image->setPixel(j * 2 + 1 + offset.x(), i + offset.y(), rgb);
            } else {
                image->setPixel(j + offset.x(), i + offset.y(), rgb);
            }
        }
    }
}

// Table taken from Contiki OS
// https://github.com/contiki-os/contiki/blob/master/core/lib/petsciiconv.c
// clang-format off
static unsigned char ascii2petscii[128] = {
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

/*-----------------------------------------------------------------------------------*/
quint8 utilsAsciiToScreenCode(State* state, quint8 ascii)
{
    quint8 ret = 0;

    switch (state->getKeyboardMapping()) {
    case State::KeyboardMapping::KEYBOARD_MAPPING_C64:
        ret = utilsAsciiToCommodore8Bit(ascii);
        break;
    case State::KeyboardMapping::KEYBOARD_MAPPING_ATARI8:
        ret = utilsAsciiToAtari8Bit(ascii);
        break;
    default:
        qDebug() << "Invalid state keyboard mapping: " << state->getKeyboardMapping();
        break;
    }
    return ret;
}

quint8 utilsAsciiToCommodore8Bit(quint8 ascii)
{
    if (ascii >= 128)
        return ascii; // invalid

    quint8 petscii = ascii2petscii[ascii];

    // convert it to screen code
    // http://sta.c64.org/cbm64pettoscr.html
    if (petscii <= 31)
        petscii += 128;
    else if (petscii <= 63)
        petscii += 0;
    else if (petscii <= 95)
        petscii -= 64;
    else if (petscii <= 127)
        petscii -= 32;
    else if (petscii <= 159)
        petscii += 64;
    else if (petscii <= 191)
        petscii -= 64;
    else if (petscii <= 223)
        petscii -= 128;
    else if (petscii <= 254)
        petscii -= 128;
    else /* petscii = 255 */
        petscii = 94;

    return petscii;
}

quint8 utilsAsciiToAtari8Bit(quint8 ascii)
{
    if (ascii >= 128)
        return ascii; // invalid

    quint8 ret = 0;  // space

    // Ascii and ATASCII are pretty similar.
    // See: https://atariwiki.org/wiki/attach/Atari%20ATASCII%20Table/ascii_atascii_table.pdf

    // Now we have to convert Ascii to Atari screen code:
    if (ascii <= 0x1f)
        ;
    else if (ascii <= 0x5f)
        ret = ascii - 0x20;
    else
        ret = ascii;

    return ret;
}
