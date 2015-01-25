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
    int selectedColor = state->getSelectedColor();
    state->setBit(_index, bitIndex, selectedColor);


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
    int selectedColor = state->getSelectedColor();

    state->setBit(_index, bitIndex, selectedColor);

    repaint();
}


void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    State *state = State::getInstance();

    // background
    painter.fillRect(event->rect(), Constants::CBMcolors[ state->getColor(0) ]);

    // selected color
    painter.setBrush( Constants::CBMcolors[ state->getColor(1) ] );

    const char *charPtr = State::getInstance()->getCharAtIndex(_index);

    for (int y=0; y<8; y++) {
        int letter = charPtr[y];
        for (int x=0; x<8; x++) {
            int mask = 1 << (7-x);
            if (letter & mask) {
                painter.drawRect(x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE-2, PIXEL_SIZE-2);
            }
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

