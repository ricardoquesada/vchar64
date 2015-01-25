/****************************************************************************
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
//    auto pos = event->localPos();

    repaint();
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
