/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "mapwidget.h"

#include <functional>

#include <QPainter>
#include <QPaintEvent>
#include <QFile>
#include <QDebug>
#include <QGuiApplication>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "utils.h"

static const int PIXEL_SIZE = 2;
static const int OFFSET = 0;

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
    , _displayGrid(false)
    , _mapSize({40,25})
    , _tileSize({1,1})
    , _mode(PAINT_MODE)
    , _commandMergeable(false)
{
    // FIXME: should be updated when the map size changes
    _sizeHint = {_mapSize.width() * _tileSize.width() * PIXEL_SIZE * 8,
                 _mapSize.height() * _tileSize.height() * PIXEL_SIZE * 8};
    setMinimumSize(_sizeHint);
}

//
// Overriden
//
void MapWidget::paintEvent(QPaintEvent *event)
{
    auto state = MainWindow::getCurrentState();

    // no open documents?
    if (!state)
        return;

    auto screenRAM = state->getMapBuffer();

    QPainter painter;
    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    auto tileProperties = state->getTileProperties();
    _tileSize = tileProperties.size;
    const int tw = _tileSize.width();
    const int th = _tileSize.height();

    for (int y=0; y<_mapSize.height(); y++)
    {
        for (int x=0; x<_mapSize.width(); ++x)
        {
            auto tileIdx = screenRAM[y * _mapSize.width() + x];
            quint8 charIdx = tileProperties.interleaved == 1 ?
                                                        tileIdx * tw * th :
                                                        tileIdx;

            for (int char_idx=0; char_idx < (tw * th); char_idx++)
            {
                int xx = x * tw + char_idx % tw;
                int yy = y * th + char_idx / tw;

                utilsDrawChar(state, &painter, QSize(PIXEL_SIZE, PIXEL_SIZE), QPoint(OFFSET, OFFSET), QPoint(xx, yy), charIdx);

                charIdx += tileProperties.interleaved;
            }
        }
    }


    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);

        for (int y=0; y<=_mapSize.height(); ++y)
            painter.drawLine(QPointF(0, y * PIXEL_SIZE * th * 8),
                             QPointF(_mapSize.width() * PIXEL_SIZE * tw * 8, y * PIXEL_SIZE * th * 8));

        for (int x=0; x<=_mapSize.width(); ++x)
            painter.drawLine(QPointF(x * PIXEL_SIZE * tw * 8, 0),
                             QPointF(x * PIXEL_SIZE * tw * 8, _mapSize.height() * PIXEL_SIZE * th *8));
    }

    QPen pen;
    pen.setColor({149,195,244,255});
    if (hasFocus())
        pen.setWidth(3);
    else
        pen.setWidth(1);
    pen.setStyle(Qt::PenStyle::SolidLine);

    if (_selecting) {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x() * 8 * tw * PIXEL_SIZE + OFFSET,
                         _cursorPos.y() * 8 * th * PIXEL_SIZE + OFFSET,
                         _selectingSize.width() * tw * 8 * PIXEL_SIZE,
                         _selectingSize.height() * th * 8 * PIXEL_SIZE);
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * tw * PIXEL_SIZE + OFFSET,
                         _cursorPos.y() * 8 * th * PIXEL_SIZE + OFFSET,
                         8 * tw * PIXEL_SIZE, 8 * th * PIXEL_SIZE);
    }

    painter.end();
}

void MapWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = (pos.x() - OFFSET) / PIXEL_SIZE / _tileSize.width() / 8;
    int y = (pos.y() - OFFSET) / PIXEL_SIZE / _tileSize.height() / 8;

    if (event->button() == Qt::LeftButton)
    {
        if (_mode == SELECT_MODE)
        {
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
                    qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width() - _cursorPos.x()),
                    qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height() - _cursorPos.y())
                };
            }
            else
            {
                // click without shift == select single char and clear select mode
                if (_selecting)
                {
                    _selecting = false;
                    _selectingSize = {1,1};
                }
                _cursorPos = {x,y};
                updateSelectedChar();
            }

            update();
        }
        else if (_mode == PAINT_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state) {
                state->mapPaint(QPoint(x,y), state->getTileIndex(), _commandMergeable);
                _commandMergeable = true;
            }
        }
        else if (_mode == FILL_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state)
                state->mapFill(QPoint(x,y), state->getTileIndex());
        }
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto pos = event->localPos();
        int x = (pos.x() - OFFSET) / PIXEL_SIZE / _tileSize.width() / 8;
        int y = (pos.y() - OFFSET) / PIXEL_SIZE / _tileSize.height() / 8;

        if (_mode == SELECT_MODE)
        {
            if (x >= _cursorPos.x())
                x++;
            if (y >= _cursorPos.y())
                y++;

            _selectingSize = {x - _cursorPos.x(),
                              y - _cursorPos.y()};

            // sanity check
            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width()-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height()-_cursorPos.y())
            };

            _selecting = true;
            update();
        }
        else if (_mode == PAINT_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state) {
                state->mapPaint(QPoint(x,y), state->getTileIndex(), _commandMergeable);
                _commandMergeable = true;
            }
        }
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent * event)
{
    event->accept();

    // don't merge "mapPaint" command if the mouse was released
    _commandMergeable = false;
}

void MapWidget::keyPressEvent(QKeyEvent *event)
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
                qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width()-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height()-_cursorPos.y())
            };
        }
        else
        {
            _cursorPos += point;
            _cursorPos = {qBound(0, _cursorPos.x(), _mapSize.width()-1),
                          qBound(0, _cursorPos.y(), _mapSize.height()-1)};

            updateSelectedChar();
        }
    }

    _selecting = selecting;
    update();
}

QSize MapWidget::sizeHint() const
{
    return _sizeHint;
}

//
//
//
void MapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        update();
    }
}

void MapWidget::updateColor()
{
    update();
}


void MapWidget::onTilePropertiesUpdated()
{
    _tileSize = MainWindow::getCurrentState()->getTileProperties().size;

    _sizeHint = QSize(_mapSize.width() * _tileSize.width() * PIXEL_SIZE * 8,
                      _mapSize.height() * _tileSize.height() * PIXEL_SIZE * 8);

    setMinimumSize(_sizeHint);
    update();
}

void MapWidget::onMapSizeUpdated()
{
    _mapSize = MainWindow::getCurrentState()->getMapSize();

    _sizeHint = QSize(_mapSize.width() * _tileSize.width() * PIXEL_SIZE * 8,
                      _mapSize.height() * _tileSize.height() * PIXEL_SIZE * 8);

    setMinimumSize(_sizeHint);
    update();
}

void MapWidget::onMapContentUpdated()
{
    update();
}

void MapWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state);
    update();
}

void MapWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen);
    update();
}

void MapWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex);
    update();
}

void MapWidget::updateSelectedChar()
{
    auto state = MainWindow::getCurrentState();

    if (state)
    {
        int index = _cursorPos.y() * _mapSize.width() + _cursorPos.x();
        auto map = state->getMapBuffer();

        state->setCharIndex(map[index]);
    }
}

void MapWidget::setMode(MapMode mode)
{
    if (_mode != mode)
        _mode = mode;
}
