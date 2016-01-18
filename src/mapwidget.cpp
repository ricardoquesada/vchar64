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

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "utils.h"


MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
    , _displayGrid(false)
{
    // fixed size of 40 * 25... this should be customizable
//    setFixedSize(PIXEL_SIZE * 40 * 8 + OFFSET * 2,
//                 PIXEL_SIZE * 25 * 8 + OFFSET * 2);
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
    auto mapSize = state->getMapSize();

    QPainter painter;
    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    for (int y=0; y<mapSize.height(); y++)
    {
        for (int x=0; x<mapSize.height(); ++x)
        {
            auto c = screenRAM[y * mapSize.width() + x];

            utilsDrawChar(state, &painter, QSize(2,2), QPoint(0,0), x, y, c);
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DotLine);
        painter.setPen(pen);

        for (int y=0; y<=200; y=y+8)
            painter.drawLine(QPointF(0,y), QPointF(320,y));

        for (int x=0; x<=320; x=x+8)
            painter.drawLine(QPointF(x,0), QPointF(x,200));
    }

    painter.end();
}

void MapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        repaint();
    }
}
