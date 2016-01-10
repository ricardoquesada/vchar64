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

#include "ImportKoalaWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFile>
#include <QDebug>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 40;
static const int ROWS = 25;
static const int OFFSET = 0;

ImportKoalaWidget::ImportKoalaWidget(QWidget *parent)
    : QWidget(parent)
{
    memset(_framebuffer, 0, sizeof(_framebuffer));
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
                 PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}

//
// Overriden
//
void ImportKoalaWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    for (int y=0; y<200; y++)
    {
        for (int x=0; x<160; ++x)
        {
            painter.setBrush(Palette::getColor(_framebuffer[y * 160 + x]));
            painter.drawRect((x*2) * PIXEL_SIZE + OFFSET,
                             y * PIXEL_SIZE + OFFSET,
                             PIXEL_SIZE * 2,
                             PIXEL_SIZE);
        }
    }
    painter.end();
}

//
// public
//
void ImportKoalaWidget::loadKoala(const QString& koalaFilepath)
{
    QFile file(koalaFilepath);
    file.open(QIODevice::ReadOnly);
    file.read((char*)&_koala, sizeof(_koala));

    toFrameBuffer();
}

void ImportKoalaWidget::toFrameBuffer()
{
    // 25 rows
    for (int y=0; y<ROWS; ++y)
    {
        // 40 cols
        for (int x=0; x<COLUMNS; ++x)
        {
            // 8 pixels Y
            for (int j=0; j<8; ++j)
            {
                quint8 byte = _koala.bitmap[(y * COLUMNS + x) * 8 + j];

                const quint8 masks[] = {192, 48, 12, 3};
                // 4 wide-pixels X
                for (int k=0; k<4; ++k)
                {
                    quint8 colorIndex = 0;
                    // get the two bits that reprent the color
                    quint8 color = byte & masks[k];
                    color >>= 6-k*2;

                    switch (color)
                    {
                    // bitmask 00: background ($d021)
                    case 0x0:
                        colorIndex = _koala.backgroundColor;
                        break;

                    // bitmask 01: #4-7 screen ram
                    case 0x1:
                        colorIndex = _koala.screenRAM[y * COLUMNS + x] >> 4;
                        break;

                    // bitmask 10: #0-3 screen ram
                    case 0x2:
                        colorIndex = _koala.screenRAM[y * COLUMNS + x] & 0xf;
                        break;

                    // bitmask 11: color ram
                    case 0x3:
                        colorIndex = _koala.colorRAM[y * COLUMNS + x] & 0xf;
                        break;
                    default:
                        qDebug() << "ImportKoalaWidget::paintEvent Invalid color: " << color << " at x,y=" << x << y;
                        break;
                    }

                    _framebuffer[(y * 8 + j) * 160 + (x * 4 + k)] = colorIndex;
                }
            }
        }
    }

    update();
}
