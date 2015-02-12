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

CharSetView::CharSetView(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8, PIXEL_SIZE * ROWS * 8);
}

void CharSetView::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE / 8;
    int y = pos.y() / PIXEL_SIZE / 8;
    int index = x + y * COLUMNS;

    emit charSelected(index);
}

void CharSetView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        _cursorPos += {-1,0};
        break;
    case Qt::Key_Right:
        _cursorPos += {+1,0};
        break;
    case Qt::Key_Down:
        _cursorPos += {0,+1};
        break;
    case Qt::Key_Up:
        _cursorPos += {0,-1};
        break;
    default:
        QWidget::keyPressEvent(event);
    }
    _cursorPos = {qBound(0, _cursorPos.x(), COLUMNS-1),
                  qBound(0, _cursorPos.y(), ROWS-1)};

    update();
    emit charSelected(_cursorPos.x() + _cursorPos.y() * COLUMNS);
}

void CharSetView::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
//    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));
    painter.fillRect(event->rect(), QColor(204,204,204));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

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

    QPen pen;
    pen.setColor({128,128,255});
    if (hasFocus())
        pen.setWidth(3);
    else
        pen.setWidth(1);
    pen.setStyle(Qt::PenStyle::SolidLine);

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
                    painter.drawRect((w*end_x+x) * pixel_size_x, (h*8+y) * PIXEL_SIZE, pixel_size_x, PIXEL_SIZE);
                }
            }

            if (w==_cursorPos.x() && h==_cursorPos.y()) {
                painter.setPen(pen);
                painter.setBrush(QColor(128,0,0,0));
                painter.drawRect(w*8*PIXEL_SIZE, h*8*PIXEL_SIZE, PIXEL_SIZE*8, PIXEL_SIZE*8);
            }
            painter.setPen(Qt::NoPen);
        }
    }

    painter.end();
}

void CharSetView::setIndex(int index)
{
    QPoint p;
    p.setX(index % COLUMNS);
    p.setY(index / COLUMNS);

    if (_cursorPos != p) {
        _cursorPos = p;
        update();
    }
}
