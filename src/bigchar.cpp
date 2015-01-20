/****************************************************************************
****************************************************************************/

#include "bigchar.h"

#include <QPainter>
#include <QPaintEvent>

#include "state.h"

static const int PIXEL_SIZE = 32;

//! [0]
BigChar::BigChar(QWidget *parent)
    : QWidget(parent)
    , _index(0)
{
    setFixedSize(PIXEL_SIZE * 8, PIXEL_SIZE * 8);
}
//! [0]

//! [2]
void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));

    painter.setBrush(QColor(0,0,0));

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

//! [2]
