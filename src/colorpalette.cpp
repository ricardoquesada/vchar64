/****************************************************************************
****************************************************************************/

#include "colorpalette.h"

#include <algorithm>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>

#include "state.h"

static const int PIXEL_SIZE = 16;

//! [0]
ColorPalette::ColorPalette(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(PIXEL_SIZE * 8, PIXEL_SIZE * 2);
}
//! [0]

void ColorPalette::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    repaint();
}

void ColorPalette::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));

    // Colors from here: http://www.blitzbasic.com/Community/posts.php?topic=52312
    QColor colors[] = {

        {0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF},
        {0x74, 0x43, 0x35},
        {0x7C, 0xAC, 0xBA},
        {0x7B, 0x48, 0x90},
        {0x64, 0x97, 0x4F},
        {0x40, 0x32, 0x85},
        {0xBF, 0xCD, 0x7A},
        {0x7B, 0x5B, 0x2F},
        {0x4f, 0x45, 0x00},
        {0xa3, 0x72, 0x65},
        {0x50, 0x50, 0x50},
        {0x78, 0x78, 0x78},
        {0xa4, 0xd7, 0x8e},
        {0x78, 0x6a, 0xbd},
        {0x9f, 0x9f, 0x9f},
    };

//        {0, 0, 0},              // black
//        {255, 255, 255},        // white
//        {140,77,57},            //        {136, 0, 0},            // red
//        {137,191,210},          //        {170, 255, 238},        // cyan
//        {147, 82, 173},         //        {204, 68, 204},         // violet
//        {113, 173, 89},         //        {0, 204, 85},           // green
//        {77, 58, 154},          //        {0, 0, 170},            // blue
//        {206, 219, 139},        //        {238, 238, 119},        // yellow
//        {147, 108, 43},         //        {221, 136, 85},         // orange
//        {95, 83, 0},            //        {102, 68, 0},           // brown
//        {187, 131,119},         //        {255, 119, 119},        // lightred
//        {95, 95, 95},           //        {51, 51, 51},           // grey 1
//        {137, 137, 137},        //        {119, 119, 119},        // grey 2
//        {178, 233, 157},        //        {170, 255, 102},        // lightgreen
//        {138, 120, 217},        //        {0, 136, 255},          // lightblue
//        {178, 178, 178},        //        {187, 187, 187}         // grey 3
//    };

    for (int y=0; y<2; y++) {
        for (int x=0; x<8; x++) {
            painter.setBrush(colors[8 * y + x]);
            painter.drawRect(x * PIXEL_SIZE, y * PIXEL_SIZE, PIXEL_SIZE, PIXEL_SIZE);
        }
    }

    painter.end();
}
