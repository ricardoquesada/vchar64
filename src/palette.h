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

#pragma once

#include <QColor>

class State;

class Palette
{
public:
    enum {
        PEPTO,
        VICE,
        CCS64,
        FRODO,
        GRAYSCALE
    };

    static const QColor& getColor(int colorIndex);
    static const QColor& getColorForPen(State *state, int pen);
    static void setActivePalette(int paletteIndex);
    static int getActivePalette();

private:
    static int _paletteIndex;
};
