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

#include "colorpalette.h"

#include <algorithm>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>

#include "constants.h"
#include "state.h"

static const int PIXEL_SIZE = 16;

ColorPalette::ColorPalette(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(PIXEL_SIZE * 8, PIXEL_SIZE * 2);
}

void ColorPalette::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE;
    int y = pos.y() / PIXEL_SIZE;

    int color = 8 * y + x;

    auto state = State::getInstance();

    int index = state->getSelectedColorIndex();
    int oldColor = state->getColorAtIndex(index);

    if (oldColor != color) {
        state->setColorAtIndex(index, color);
        emit colorSelected();

        repaint();
    }
}

void ColorPalette::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));


    for (int y=0; y<2; y++) {
        for (int x=0; x<8; x++) {
            painter.setBrush( Constants::CBMcolors[8 * y + x]);
            painter.drawRect(x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE, PIXEL_SIZE);
        }
    }

    painter.end();
}

