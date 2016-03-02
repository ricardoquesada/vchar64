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
#include "utils.h"
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
    _sizeHint = {_columns * 8 * 2,
                 _rows * 8 * 2};
    setMinimumSize(_sizeHint);

    setMouseTracking(true);
}

//
// overrides
//
void TilesetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

    if (event->button() == Qt::LeftButton)
    {

        auto pos = event->localPos();
        auto tileProperties = state->getTileProperties();
        int tw = tileProperties.size.width();
        int th = tileProperties.size.height();

        int x = (pos.x() - OFFSET) / _pixelSize.width() / 8 / tw;
        int y = (pos.y() - OFFSET) / _pixelSize.height() / 8 / th;

        // sanity check
        x = qBound(0, x, _tileColums - 1);
        y = qBound(0, y, _tileRows - 1);

        int tileIndex = x + y * (_columns / tw);

        // quick sanity check
        if (! (tileIndex >= 0 && tileIndex < _maxTiles))
            return;

        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        {
            // Click + Shift == select mode
            _selecting = true;

            if (x >= _cursorPos.x())
                x++;
            if (y >= _cursorPos.y())
                y++;

            _selectingSize = {x - _cursorPos.x(),
                              y - _cursorPos.y()};

            // sanity check
            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), _tileColums - _cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows - _cursorPos.y())
            };
            update();
        }
        else
        {
            // different and valid tileIndex?
            if (_cursorPos.x() != x || _cursorPos.y() != y)
            {
                _selecting = false;
                _selectingSize = {1,1};

                _cursorPos = {x,y};
                state->setTileIndex(tileIndex);
                update();
            }
        }
    }
}

void TilesetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();
    auto state = MainWindow::getCurrentState();
    auto tileProperties = state->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    int x = (pos.x() - OFFSET) / _pixelSize.width() / 8 / tw;
    int y = (pos.y() - OFFSET) / _pixelSize.height() / 8 / th;

    x = qBound(0, x, _tileColums-1);
    y = qBound(0, y, _tileRows-1);

    if (event->buttons() == Qt::NoButton)
    {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
    }
    else if (event->buttons() == Qt::LeftButton)
    {
        // sanity check
        int tileIndex = x + y * (_columns / tw);
        if (! (tileIndex >= 0 && tileIndex < _maxTiles))
            return;

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

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

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

                MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2")
                                                                  .arg(_cursorPos.x())
                                                                  .arg(_cursorPos.y()));
            }
        }
    }

    _selecting = selecting;
    update();
}

void TilesetWidget::paintEvent(QPaintEvent *event)
{
    auto state = MainWindow::getCurrentState();

    // no open documents?
    if (!state)
        return;

    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

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
        quint8 charIdx = tileProperties.interleaved == 1 ?
                                                    i * tw * th :
                                                    i;

        int w = (i * tw) % _columns;
        int h = th * ((i * tw) / _columns);

        for (int char_idx=0; char_idx < (tw * th); char_idx++)
        {
            int local_w = w + char_idx % tw;
            int local_h = h + char_idx / tw;

            utilsDrawChar(state, &painter, _pixelSize, QPoint(OFFSET, OFFSET), QPoint(local_w, local_h), charIdx);

            charIdx += tileProperties.interleaved;
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
    auto tileProperties = MainWindow::getCurrentState()->getTileProperties();
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
    // if the tileIndex is updated, cancel selection
    if (_selecting)
    {
        _selecting = false;
        _selectingSize = {1,1};
    }

    int x = selectedTileIndex % _tileColums;
    int y = selectedTileIndex / _tileColums;
    if (_cursorPos.x() != x || _cursorPos.y() != y)
    {
        _cursorPos = {x, y};
        update();
    }
}

void TilesetWidget::onTilePropertiesUpdated()
{
    auto state = MainWindow::getCurrentState();
    auto properties = state->getTileProperties();

    _columns = (COLUMNS / properties.size.width()) * properties.size.width();
    _tileColums = _columns / properties.size.width();

    _rows = qCeil((256.0f / _columns) / properties.size.height()) * properties.size.height();
    _tileRows = _rows / properties.size.height();

    _maxTiles = 256 / (properties.size.width() * properties.size.height());

    _sizeHint = QSize(_pixelSize.width() * _columns * 8 + OFFSET * 2,
                 _pixelSize.height() * _rows * 8 + OFFSET * 2);

    setMinimumSize(_sizeHint);
    update();
}

void TilesetWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state);
    update();
}

void TilesetWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen);
    update();
}

void TilesetWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex);
    update();
}

void TilesetWidget::onCharsetUpdated()
{
    update();
}

void TilesetWidget::onFileLoaded()
{
    onTilePropertiesUpdated();
}

// public
bool TilesetWidget::hasSelection() const
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void TilesetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    auto tileProperties = MainWindow::getCurrentState()->getTileProperties();

    // if charset has no selection, it works the same
    // since selectingSize will be {1,1}

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

    // copying tiles, intead of chars, even if interleaved==1
    copyRange->offset = fixed_origin.y() * _tileColums + fixed_origin.x();
    copyRange->blockSize = fixed_size.width();
    copyRange->count = fixed_size.height();
    copyRange->skip = _tileColums - fixed_size.width();

    copyRange->type = State::CopyRange::TILES;
    copyRange->tileProperties = tileProperties;
}


