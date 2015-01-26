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

    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));

    painter.setBrush(QColor(0,0,0));

    for (int w=0; w<COLUMNS; w++) {
        for (int h=0; h<ROWS; h++) {
            for (int y=0; y<8; y++) {

                int index = w + h * COLUMNS;
                const char* letter = State::getInstance()->getCharAtIndex(index);

                for (int x=0; x<8; x++) {

                    int mask = 1 << (7-x);
                    if (letter[y] & mask) {
                        painter.drawRect((w*8+x) * PIXEL_SIZE, (h*8+y) * PIXEL_SIZE, PIXEL_SIZE, PIXEL_SIZE);
                    }
                }
            }
        }
    }


    painter.end();
}
//! [2]
