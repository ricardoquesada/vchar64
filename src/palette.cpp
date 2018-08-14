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

#include <QCoreApplication>

#include "palette.h"
#include "state.h"

static const QColor Palettes[][16] = {
    {
        // Pepto palette. Taken from: http://www.pepto.de/projects/colorvic/
        {0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF},
        {0x68, 0x37, 0x2B},
        {0x70, 0xA4, 0xB2},
        {0x6F, 0x3D, 0x86},
        {0x58, 0x8D, 0x43},
        {0x35, 0x28, 0x79},
        {0xB8, 0xC7, 0x6F},
        {0x6F, 0x4F, 0x25},
        {0x43, 0x39, 0x00},
        {0x9A, 0x67, 0x59},
        {0x44, 0x44, 0x44},
        {0x6C, 0x6C, 0x6C},
        {0x9A, 0xD2, 0x84},
        {0x6C, 0x5E, 0xB5},
        {0x95, 0x95, 0x95},
    },
    {
        /* VICE palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64
	 * Although VICE seems to be using the Pepto palette, so not sure
	 * from where this palette was taken
	 */
        {0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff},
        {0xbd, 0x18, 0x21},
        {0x31, 0xe7, 0xc6},
        {0xb5, 0x18, 0xe7},
        {0x18, 0xd6, 0x18},
        {0x21, 0x18, 0xad},
        {0xde, 0xf7, 0x08},
        {0xbd, 0x42, 0x00},
        {0x6b, 0x31, 0x00},
        {0xff, 0x4a, 0x52},
        {0x42, 0x42, 0x42},
        {0x73, 0x73, 0x6b},
        {0x5a, 0xff, 0x5a},
        {0x5a, 0x52, 0xff},
        {0xa5, 0xa5, 0xa5}
    },
    {
        /* CCS64 palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64 */
        {0x10, 0x10, 0x10},
        {0xff, 0xff, 0xff},
        {0xe0, 0x40, 0x40},
        {0x60, 0xff, 0xff},
        {0xe0, 0x60, 0xe0},
        {0x40, 0xe0, 0x40},
        {0x40, 0x40, 0xe0},
        {0xff, 0xff, 0x40},
        {0xe0, 0xa0, 0x40},
        {0x9c, 0x74, 0x48},
        {0xff, 0xa0, 0xa0},
        {0x54, 0x54, 0x54},
        {0x88, 0x88, 0x88},
        {0xa0, 0xff, 0xa0},
        {0xa0, 0xa0, 0xff},
        {0xc0, 0xc0, 0xc0}
    },
    {
        /* FRODO palette. Copied+Pasted from https://github.com/dirkwhoffmann/virtualc64 */
        {0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff},
        {0xcc, 0x00, 0x00},
        {0x00, 0xff, 0xcc},
        {0xff, 0x00, 0xff},
        {0x00, 0xcc, 0x00},
        {0x00, 0x00, 0xcc},
        {0xff, 0xff, 0x00},
        {0xff, 0x88, 0x00},
        {0x88, 0x44, 0x00},
        {0xff, 0x88, 0x88},
        {0x44, 0x44, 0x44},
        {0x88, 0x88, 0x88},
        {0x88, 0xff, 0x88},
        {0x88, 0x88, 0xff},
        {0xcc, 0xcc, 0xcc}
    },
    {
        // Pepto to grayscale using: r * 0.299 + g * 0.587 + b * 0.114
        {0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff},
        {0x44, 0x44, 0x44},
        {0x96, 0x96, 0x96},
        {0x54, 0x54, 0x54},
        {0x74, 0x74, 0x74},
        {0x35, 0x35, 0x35},
        {0xb8, 0xb8, 0xb8},
        {0x53, 0x53, 0x53},
        {0x35, 0x35, 0x35},
        {0x74, 0x74, 0x74},
        {0x44, 0x44, 0x44},
        {0x6b, 0x6b, 0x6b},
        {0xb8, 0xb8, 0xb8},
        {0x6c, 0x6c, 0x6c},
        {0x94, 0x94, 0x94},
    },
    {
        // Colodore 50 / 100 / 50 palette. Taken from: http://www.colodore.com
        {0x00, 0x00, 0x00},
        {0xFE, 0xFE, 0xFE},
        {0x81, 0x33, 0x37},
        {0x75, 0xCE, 0xC8},
        {0x8D, 0x3B, 0x97},
        {0x55, 0xAC, 0x4C},
        {0x2D, 0x2B, 0x9A},
        {0xED, 0xF0, 0x71},
        {0x8D, 0x50, 0x29},
        {0x54, 0x37, 0x00},
        {0x63, 0x6C, 0x71},
        {0x49, 0x49, 0x49},
        {0x7B, 0x7B, 0x7B},
        {0xA9, 0xFE, 0x9F},
        {0x6F, 0x6D, 0xEB},
        {0xB1, 0xB1, 0xB1},
    },
};
static const int MAX_PALETTES = sizeof(Palettes) / sizeof(Palettes[0]);

// Default is Pepto
int Palette::_paletteIndex = 0;

const QString Palette::color_names[] = {
     tr("Black"),
     tr("White"),
     tr("Red"),
     tr("Cyan"),
     tr("Violet"),
     tr("Green"),
     tr("Blue"),
     tr("Yellow"),

     tr("Orange"),
     tr("Brown"),
     tr("Light red"),
     tr("Dark grey"),
     tr("Grey"),
     tr("Light green"),
     tr("Light blue"),
     tr("Light grey")
};

const QColor& Palette::getColorForPen(State* state, int pen)
{
    Q_ASSERT(pen>=0 && pen<State::PEN_MAX && "Invalid pen value");

    // default colors are used when no state is present.
    // Import from VICE might use it if no tabs are open
    const int defaultColors[] = {1,5,7,11};
    int colorIndex = defaultColors[pen];

    // state could be nill if no States (documents) are open
    if (state)
    {
        colorIndex = state->getColorForPen(pen, state->getTileIndex());

        // upper colors should be the same a lower colors on multicolor mode in foreground pen
        if (pen == State::PEN_FOREGROUND && state->shouldBeDisplayedInMulticolor2(state->getTileIndex()))
            colorIndex -= 8;
    }

    return Palettes[_paletteIndex][colorIndex];
}

const QColor& Palette::getColor(int colorIndex)
{
    Q_ASSERT(colorIndex>=0 && colorIndex<16);
    return Palettes[_paletteIndex][colorIndex];
}

void Palette::setActivePalette(int paletteIndex)
{
    // to make happy the build in release mode. It will complain that MAX_PALETTES
    // is not being used
    Q_UNUSED(MAX_PALETTES);
    Q_ASSERT(paletteIndex >= 0 && paletteIndex < MAX_PALETTES);
    _paletteIndex = paletteIndex;
}

int Palette::getActivePalette()
{
    return _paletteIndex;
}
