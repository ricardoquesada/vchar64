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

#include "charsetview.h"

#include <QPainter>
#include <QPaintEvent>

#include "constants.h"
#include "state.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 32;
static const int ROWS = 8;

//! [0]
CharSetView::CharSetView(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8, PIXEL_SIZE * ROWS * 8);
}
//! [0]

void CharSetView::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE / 8;
    int y = pos.y() / PIXEL_SIZE / 8;
    int index = x + y * COLUMNS;

    emit charSelected(index);
}

//! [2]
void CharSetView::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
//    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));
    painter.fillRect(event->rect(), QColor(204,204,204));

    painter.setBrush(QColor(0,0,0));

    auto state = State::getInstance();

    int end_x = 8;
    int pixel_size_x = PIXEL_SIZE;
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->isMultiColor())
    {
        end_x = 4;
        pixel_size_x = PIXEL_SIZE * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    for (int w=0; w<COLUMNS; w++) {
        for (int h=0; h<ROWS; h++) {

            int index = w + h * COLUMNS;
            u_int8_t* charPtr = state->getCharAtIndex(index);

            for (int y=0; y<8; y++) {

                char letter = charPtr[y];

                for (int x=0; x<end_x; x++) {

                    // Warning: Don't use 'char'. Instead use 'unsigned char'
                    // to prevent a bug in the compiler (or in my code???)

                    // only mask the bits are needed
                    unsigned char mask = bits_to_mask << (((end_x-1)-x) * increment_x);

                    unsigned char color = letter & mask;
                    // now transform those bits into values from 0-3 since those are the
                    // possible colors

                    int bits_to_shift = (((end_x-1)-x) * increment_x);
                    int color_index = color >> bits_to_shift;

                    if (!state->isMultiColor() && color_index )
                        color_index = 3;
                    painter.setBrush(Constants::CBMcolors[state->getColorAtIndex(color_index)]);
                    painter.setPen(Qt::NoPen);
                    painter.drawRect((w*end_x+x) * pixel_size_x, (h*8+y) * PIXEL_SIZE, pixel_size_x, PIXEL_SIZE);
                }
            }
        }
    }

    painter.end();
}
//! [2]
