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

#include "tilesetwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtMath>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;

TilesetWidget::TilesetWidget(QWidget *parent)
    : QWidget(parent)
    , _selectedTile(0)
    , _columns(COLUMNS)
    , _rows(ROWS)
{
    setFixedSize(PIXEL_SIZE * _columns * 8 + OFFSET * 2,
                 PIXEL_SIZE * _rows * 8 + OFFSET * 2);
}


void TilesetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    if (event->button() == Qt::LeftButton)
    {

        auto pos = event->localPos();
        auto tileProperties = State::getInstance()->getTileProperties();

        int x = (pos.x() - OFFSET) / PIXEL_SIZE / 8 / tileProperties.size.width();
        int y = (pos.y() - OFFSET) / PIXEL_SIZE / 8 / tileProperties.size.height();
        int tileIndex = x + y * (_columns / tileProperties.size.width());

        if (_selectedTile != tileIndex)
        {
            _selectedTile = tileIndex;
            emit tileSelected(tileIndex);
            update();
        }
    }
}

void TilesetWidget::keyPressEvent(QKeyEvent *event)
{
    event->accept();
    QPoint point;

    switch (event->key()) {
    case Qt::Key_Left:
        point = {-1,0};
        break;
    case Qt::Key_Right:
        point = {+1,0};
        break;
    case Qt::Key_Down:
        point = {0,+1};
        break;
    case Qt::Key_Up:
        point = {0,-1};
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    const auto tileProperties = State::getInstance()->getTileProperties();

    _selectedTile += (point.x() + point.y() * (_columns / tileProperties.size.width()));
    _selectedTile %= 256 / (tileProperties.size.width() * tileProperties.size.height());
    if (_selectedTile < 0)
        _selectedTile += 256 / (tileProperties.size.width() * tileProperties.size.height());

    emit tileSelected(_selectedTile);
    update();
}

void TilesetWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    auto state = State::getInstance();

    QPen pen;
    pen.setColor({149,195,244,255});
    pen.setWidth(hasFocus() ? 3 : 1 );
    pen.setStyle(Qt::PenStyle::SolidLine);

    auto tileProperties = state->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    int max_tiles = 256 / (tw*th);

    for (int i=0; i<max_tiles;i++)
    {
        quint8* charPtr = state->getCharAtIndex(tileProperties.interleaved == 1 ?
                                                    i * tw * th :
                                                    i);

        int w = (i * tw) % _columns;
        int h = th * ((i * tw) / _columns);

        for (int char_idx=0; char_idx < (tw * th); char_idx++)
        {
            int local_w = w + char_idx % tw;
            int local_h = h + char_idx / tw;

            paintPixel(painter, local_w, local_h, charPtr);

            charPtr += (tileProperties.interleaved * 8);
        }
    }

    painter.setPen(Qt::NoPen);

    paintFocus(painter);
    paintSelectedTile(painter);

    painter.end();
}

void TilesetWidget::paintSelectedTile(QPainter& painter)
{
    auto tileProperties = State::getInstance()->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    QPen pen;
    pen.setWidth( hasFocus() ? 3 : 1);
    pen.setColor({149,195,244,255});
    pen.setStyle(Qt::PenStyle::SolidLine);

    int x = (_selectedTile * tw) % _columns;
    int y = th * ((_selectedTile * tw) / _columns);

    painter.setPen(pen);
    painter.setBrush(QColor(128,0,0,0));
    painter.drawRect(x * 8 * PIXEL_SIZE + OFFSET,
                     y * 8 * PIXEL_SIZE + OFFSET,
                     8 * PIXEL_SIZE * tw,
                     8 * PIXEL_SIZE * th);
}

void TilesetWidget::paintPixel(QPainter &painter, int w, int h, quint8* charPtr)
{
    auto state = State::getInstance();

    int end_x = 8;
    int pixel_size_x = PIXEL_SIZE;
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->shouldBeDisplayedInMulticolor())
    {
        end_x = 4;
        pixel_size_x = PIXEL_SIZE * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    for (int y=0; y<8; y++)
    {
        char letter = charPtr[y];

        for (int x=0; x<end_x; x++)
        {
            // Warning: Don't use 'char'. Instead use 'unsigned char'.
            // 'char' doesn't work Ok with << and >>
            // only mask the bits are needed
            unsigned char mask = bits_to_mask << (((end_x-1)-x) * increment_x);

            unsigned char color = letter & mask;
            // now transform those bits into values from 0-3 since those are the
            // possible colors

            int bits_to_shift = (((end_x-1)-x) * increment_x);
            int color_pen = color >> bits_to_shift;

            if (!state->shouldBeDisplayedInMulticolor() && color_pen )
                color_pen = State::PEN_FOREGROUND;
            painter.setBrush(Palette::getColorForPen(color_pen));
            painter.drawRect((w * end_x + x) * pixel_size_x + OFFSET,
                             (h * 8 + y) * PIXEL_SIZE + OFFSET,
                             pixel_size_x,
                             PIXEL_SIZE);
        }
    }
}

void TilesetWidget::paintFocus(QPainter &painter)
{
    if (hasFocus())
    {
        QPen pen;
        pen.setColor({149,195,244,255});
        pen.setWidth(3);
        pen.setStyle(Qt::PenStyle::SolidLine);

        painter.setPen(pen);

        // vertical lines
        painter.drawLine(0, 0,
                         0, _rows * PIXEL_SIZE * 8 + OFFSET * 2);
        painter.drawLine(_columns * PIXEL_SIZE * 8 + OFFSET, 0,
                         _columns * PIXEL_SIZE * 8 + OFFSET * 2, _rows * PIXEL_SIZE * 8 + OFFSET * 2);

        // horizontal lines
        painter.drawLine(0, 0,
                         _columns * PIXEL_SIZE * 8 + OFFSET * 2, 0);
        painter.drawLine(0, _rows * PIXEL_SIZE * 8 + OFFSET,
                         _columns * PIXEL_SIZE * 8 + OFFSET * 2, _rows * PIXEL_SIZE * 8 + OFFSET * 2);
    }
}

//
// SLOTS
//
void TilesetWidget::tileIndexUpdated(int selectedTileIndex)
{
    if (_selectedTile != selectedTileIndex) {
        _selectedTile = selectedTileIndex;
        update();
    }
}

void TilesetWidget::tilePropertiesUpdated()
{
    auto state = State::getInstance();
    auto properties = state->getTileProperties();

    _columns = (COLUMNS / properties.size.width()) * properties.size.width();

    _rows = qCeil(256.0f / _columns);

    setFixedSize(PIXEL_SIZE * _columns * 8 + OFFSET * 2,
                 PIXEL_SIZE * _rows * 8 + OFFSET * 2);

}
