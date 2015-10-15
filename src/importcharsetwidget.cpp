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

#include "ImportCharsetWidget.h"

#include <QPainter>
#include <QPaintEvent>

#include "palette.h"
#include "state.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 0;

ImportCharsetWidget::ImportCharsetWidget(QWidget *parent)
    : QWidget(parent)
    , _memoryOffset(0)
    , _multicolor(false)
    , _charset(nullptr)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
                 PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}

//
// Overriden
//
void ImportCharsetWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    if (!_charset)
        return;

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    int end_x = 8;
    int pixel_size_x = PIXEL_SIZE;
    int increment_x = 1;
    int bits_to_mask = 1;

    if (_multicolor)
    {
        end_x = 4;
        pixel_size_x = PIXEL_SIZE * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    QPen pen;
    pen.setColor({149,195,244,255});
    pen.setWidth(1);
    pen.setStyle(Qt::PenStyle::SolidLine);

    for (int w=0; w<COLUMNS; w++) {
        for (int h=0; h<ROWS; h++) {

            int index = w + h * COLUMNS;
            quint8* charPtr = &_charset[_memoryOffset + index];

            for (int y=0; y<8; y++) {

                char letter = charPtr[y];

                for (int x=0; x<end_x; x++) {

                    // Warning: Don't use 'char'. Instead use 'unsigned char'.
                    // 'char' doesn't work Ok with << and >>
                    // only mask the bits are needed
                    unsigned char mask = bits_to_mask << (((end_x-1)-x) * increment_x);

                    unsigned char color = letter & mask;
                    // now transform those bits into values from 0-3 since those are the
                    // possible colors

                    int bits_to_shift = (((end_x-1)-x) * increment_x);
                    int color_pen = color >> bits_to_shift;

                    if (!_multicolor && color_pen )
                        color_pen = State::PEN_FOREGROUND;
                    painter.setBrush(Palette::getColorForPen(color_pen));
                    painter.drawRect((w*end_x+x) * pixel_size_x + OFFSET,
                                     (h*8+y) * PIXEL_SIZE + OFFSET,
                                     pixel_size_x,
                                     PIXEL_SIZE);
                }
            }

            painter.setPen(Qt::NoPen);
        }
    }

    painter.end();
}

//
// public
//
void ImportCharsetWidget::setCharset(quint8* charset)
{
    _charset = charset;
}

//
// public slots
//
void ImportCharsetWidget::multicolorToggled(bool toggled)
{
    _multicolor = toggled;
}

void ImportCharsetWidget::addressChanged(int offset)
{
    _memoryOffset = offset;
}
