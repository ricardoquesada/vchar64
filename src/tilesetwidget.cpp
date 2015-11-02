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
#include <QDebug>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;

TilesetWidget::TilesetWidget(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
    , _selecting(false)
    , _selectingSize({0,0})
    , _columns(COLUMNS)
    , _tileColums(COLUMNS)
    , _rows(ROWS)
    , _tileRows(ROWS)
    , _maxTiles(256)
    , _sizeHint({0,0})
    , _pixelSize({0,0})
{
    _sizeHint = {COLUMNS * 8 * 2,
                 ROWS * 8 * 2};
    setMinimumSize(_sizeHint);
}

//
// overrides
//
void TilesetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        auto state = State::getInstance();

        auto pos = event->localPos();
        auto tileProperties = state->getTileProperties();
        int tw = tileProperties.size.width();
        int th = tileProperties.size.height();

        int x = (pos.x() - OFFSET) / _pixelSize.width() / 8 / tw;
        int y = (pos.y() - OFFSET) / _pixelSize.height() / 8 / th;

        int tileIndex = x + y * (_columns / tw);

        // different and valid tileIndex?
        if ((_cursorPos.x() != x || _cursorPos.y() != y) && tileIndex >= 0 && tileIndex < _maxTiles)
        {
            _selecting = false;
            _selectingSize = {1,1};

            _cursorPos = {x,y};
            state->setTileIndex(tileIndex);
            update();
        }
    }
}

void TilesetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto state = State::getInstance();

        auto pos = event->localPos();
        auto tileProperties = state->getTileProperties();
        int tw = tileProperties.size.width();
        int th = tileProperties.size.height();

        int x = (pos.x() - OFFSET) / _pixelSize.width() / 8 / tw;
        int y = (pos.y() - OFFSET) / _pixelSize.height() / 8 / th;

        if (x >= _cursorPos.x())
            x++;
        if (y >= _cursorPos.y())
            y++;

        _selectingSize = {x - _cursorPos.x(),
                          y - _cursorPos.y()};

        // sanity check
        _selectingSize = {
            qBound(-_cursorPos.x(), _selectingSize.width(), _tileColums-_cursorPos.x()),
            qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows-_cursorPos.y())
        };

        _selecting = true;

        update();
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

    auto state = State::getInstance();

    bool selecting = (event->modifiers() & Qt::ShiftModifier);

    // disabling selecting?
    if (_selecting && !selecting) {
        _selectingSize = {1,1};
    }
    else
    {
        if (selecting)
        {
            _selectingSize += {point.x(), point.y()};

            if (_selectingSize.width() == 0)
                _selectingSize.setWidth(_selectingSize.width() + 1 * point.x());
            if (_selectingSize.height() == 0)
                _selectingSize.setHeight(_selectingSize.height() + 1 * point.y());

            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), _tileColums-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows-_cursorPos.y())
            };
        }
        else
        {
            auto pos = _cursorPos + point;
            pos = {qBound(0, pos.x(), _tileColums-1),
                   qBound(0, pos.y(), _tileRows-1)};

            int tileIdx =  pos.y() * _tileColums + pos.x();
            if (pos != _cursorPos && tileIdx >=0 && tileIdx < _maxTiles)
            {
                _cursorPos = pos;
                state->setTileIndex(tileIdx);
            }
        }
    }

    _selecting = selecting;
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

QSize TilesetWidget::sizeHint() const
{
    return _sizeHint;
}

void TilesetWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);

    int pixel_size_x = size().width() / (COLUMNS * 8);
    int pixel_size_y = size().height() / (ROWS * 8);

    // keep aspect ratio
    int pixel_size = qMin(pixel_size_x, pixel_size_y);
    _pixelSize = {pixel_size, pixel_size};
}


//
// helpers
//
void TilesetWidget::paintSelectedTile(QPainter& painter)
{
    auto tileProperties = State::getInstance()->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    QPen pen;
    pen.setWidth( hasFocus() ? 3 : 1);
    pen.setColor({149,195,244,255});
    pen.setStyle(Qt::PenStyle::SolidLine);

    if (_selecting)
    {
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize.width() * tw + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize.height() * th + OFFSET,
                         _selectingSize.width() * 8 * _pixelSize.width() * tw,
                         _selectingSize.height() * 8 * _pixelSize.height() * th);
    }
    else
    {
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize.width() * tw + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize.height() * th + OFFSET,
                         8 * _pixelSize.width() * tw,
                         8 * _pixelSize.height() * th);
    }
}

void TilesetWidget::paintPixel(QPainter &painter, int w, int h, quint8* charPtr)
{
    auto state = State::getInstance();

    int end_x = 8;
    int pixel_size_x = _pixelSize.width();
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->shouldBeDisplayedInMulticolor())
    {
        end_x = 4;
        pixel_size_x = _pixelSize.width() * 2;
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
                             (h * 8 + y) * _pixelSize.height() + OFFSET,
                             pixel_size_x,
                             _pixelSize.height());
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
                         0, _rows * _pixelSize.height() * 8 + OFFSET);
        painter.drawLine(_columns * _pixelSize.width() * 8 + OFFSET, 0,
                         _columns * _pixelSize.width() * 8 + OFFSET, _rows * _pixelSize.height() * 8 + OFFSET);

        // horizontal lines
        painter.drawLine(0, 0,
                         _columns * _pixelSize.width() * 8 + OFFSET, 0);
        painter.drawLine(0, _rows * _pixelSize.height() * 8 + OFFSET,
                         _columns * _pixelSize.width() * 8 + OFFSET, _rows * _pixelSize.height() * 8 + OFFSET);
    }
}

//
// SLOTS
//
void TilesetWidget::onTileIndexUpdated(int selectedTileIndex)
{
    int x = selectedTileIndex % _tileColums;
    int y = selectedTileIndex / _tileColums;
    if (_cursorPos.x() != x && _cursorPos.y() != y)
    {
        _cursorPos = {x, y};
        update();
    }
}

void TilesetWidget::onTilePropertiesUpdated()
{
    auto state = State::getInstance();
    auto properties = state->getTileProperties();

    _columns = (COLUMNS / properties.size.width()) * properties.size.width();
    _tileColums = _columns / properties.size.width();

    _rows = qCeil((256.0f / _columns) / properties.size.height()) * properties.size.height();
    _tileRows = _rows / properties.size.height();

    _maxTiles = 256 / (properties.size.width() * properties.size.height());

    _sizeHint = QSize(_pixelSize.width() * _columns * 8 + OFFSET * 2,
                 _pixelSize.height() * _rows * 8 + OFFSET * 2);
    update();
}

void TilesetWidget::updateColor()
{
    update();
}

// public
bool TilesetWidget::hasSelection() const
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void TilesetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    auto tileProperties = State::getInstance()->getTileProperties();
    int tileSize = tileProperties.size.width() * tileProperties.size.height();

    if (tileProperties.interleaved != 1)
    {
        qDebug() << "ERROR: Copy not supported on interleaved tiles yet";
        return;
    }

    if (hasSelection())
    {
        // calculate absolute values of origin/size
        QPoint fixed_origin = _cursorPos;
        QSize fixed_size = _selectingSize;

        if (_selectingSize.width() < 0)
        {
            fixed_origin.setX(_cursorPos.x() + _selectingSize.width());
            fixed_size.setWidth(-_selectingSize.width());
        }

        if (_selectingSize.height() < 0)
        {
            fixed_origin.setY(_cursorPos.y() + _selectingSize.height());
            fixed_size.setHeight(-_selectingSize.height());
        }


        // transform origin/size to offset, blockSize, ...

        copyRange->offset = (fixed_origin.y() * _tileColums + fixed_origin.x()) * tileSize;
        copyRange->blockSize = fixed_size.width() * tileSize;
        copyRange->count = fixed_size.height();
        copyRange->skip = (_tileColums - fixed_size.width()) * tileSize;
    }
    else
    {
        // No selection, so copy current char
        int tileIndex = _cursorPos.y() * _tileColums + _cursorPos.x();
        copyRange->offset = tileIndex * tileSize;
        copyRange->blockSize = tileSize;
        copyRange->count = 1;
        copyRange->skip = 0;
    }
    copyRange->type = State::CopyRange::TILES;
}


