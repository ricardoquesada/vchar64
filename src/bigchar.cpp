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

    int index = 0;

    const char *charPtr = State::getInstance()->getCharAtIndex(index);

    for (int y=0; y<8; y++) {
        int letter = charPtr[y];
        for (int x=0; x<8; x++) {
            int mask = 1 << (7-x);
            if (letter & mask) {
                painter.setBrush(QColor(0,0,0));
                painter.drawRect(x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE-2, PIXEL_SIZE-2);
            }
        }
    }


    painter.end();
}
//! [2]
