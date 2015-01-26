/****************************************************************************
****************************************************************************/

#include "bigchar.h"

#include <algorithm>
#include <QPainter>
#include <QPaintEvent>

#include "state.h"
#include "constants.h"

static const int PIXEL_SIZE = 32;

//! [0]
BigChar::BigChar(QWidget *parent)
    : QWidget(parent)
    , _index(0)
{
    setFixedSize(PIXEL_SIZE * 8, PIXEL_SIZE * 8);
}
//! [0]

void BigChar::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE;
    int y = pos.y() / PIXEL_SIZE;
    if( x>=8 || y>=8)
        return;

    int bitIndex = x + y * 8;

    State *state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();
    state->setCharColor(_index, bitIndex, selectedColor);

    repaint();
}

void BigChar::mouseMoveEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE;
    int y = pos.y() / PIXEL_SIZE;
    if( x>=8 || y>=8)
        return;

    int bitIndex = x + y * 8;

    State *state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();

    state->setCharColor(_index, bitIndex, selectedColor);

    repaint();
}


void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    painter.setPen(Qt::PenStyle::NoPen);
    State *state = State::getInstance();

    // background
    painter.fillRect(event->rect(), QColor(204,204,204));


    const char *charPtr = State::getInstance()->getCharAtIndex(_index);

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

            painter.setBrush(Constants::CBMcolors[state->getColorAtIndex(color_index)]);
            painter.drawRect(x * pixel_size_x, y * PIXEL_SIZE, pixel_size_x-1, PIXEL_SIZE-1);
        }
    }

    painter.end();
}

void BigChar::setIndex(int index)
{
    if (_index != index) {
        _index = index;
        emit indexChanged(_index);
        repaint();
    }
}

