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
{
    // FIXME: should be updated when the map size changes
    _sizeHint = {_mapSize.width() * PIXEL_SIZE * 8,
                 _mapSize.height() * PIXEL_SIZE * 2};
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

    for (int y=0; y<_mapSize.height(); y++)
    {
        for (int x=0; x<_mapSize.width(); ++x)
        {
            auto c = screenRAM[y * _mapSize.width() + x];

            utilsDrawChar(state, &painter, QSize(PIXEL_SIZE, PIXEL_SIZE), QPoint(OFFSET, OFFSET), QPoint(x, y), c);
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);

        for (int y=0; y<=_mapSize.height(); ++y)
            painter.drawLine(QPointF(0,y*PIXEL_SIZE*8), QPointF(_mapSize.width()*PIXEL_SIZE*8,y*PIXEL_SIZE*8));

        for (int x=0; x<=_mapSize.width(); ++x)
            painter.drawLine(QPointF(x*PIXEL_SIZE*8,0), QPointF(x*PIXEL_SIZE*8,_mapSize.height()*PIXEL_SIZE*8));

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
        painter.drawRect(_cursorPos.x() * 8 * PIXEL_SIZE + OFFSET,
                         _cursorPos.y() * 8 * PIXEL_SIZE + OFFSET,
                         _selectingSize.width() * 8 * PIXEL_SIZE,
                         _selectingSize.height() * 8 * PIXEL_SIZE);
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * PIXEL_SIZE + OFFSET,
                         _cursorPos.y() * 8 * PIXEL_SIZE + OFFSET,
                         8 * PIXEL_SIZE,
                         8 * PIXEL_SIZE);
    }

    painter.end();
}

void MapWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = (pos.x() - OFFSET) / PIXEL_SIZE / 8;
    int y = (pos.y() - OFFSET) / PIXEL_SIZE / 8;

    if (event->button() == Qt::LeftButton)
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
}

void MapWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto pos = event->localPos();
        int x = (pos.x() - OFFSET) / PIXEL_SIZE / 8;
        int y = (pos.y() - OFFSET) / PIXEL_SIZE / 8;

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
