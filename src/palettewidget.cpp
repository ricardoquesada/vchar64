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

#include "palettewidget.h"

#include <algorithm>

#include <QColor>
#include <QDebug>
#include <QPaintEvent>
#include <QPainter>

#include "mainwindow.h"
#include "palette.h"
#include "palette.h"
#include "state.h"

static const int PIXEL_SIZE_X = 24;
static const int PIXEL_SIZE_Y = 16;
static const int COLUMNS = 8;
static const int ROWS = 2;

PaletteWidget::PaletteWidget(QWidget *parent)
    : QWidget(parent)
    , _pixelSize(PIXEL_SIZE_X, PIXEL_SIZE_Y)
    , _sizeHint(_pixelSize.width() * COLUMNS, _pixelSize.height() * ROWS)
{
    setMinimumSize(_sizeHint);
    setMouseTracking(true);
}

void PaletteWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = pos.x() / _pixelSize.width();
    int y = pos.y() / _pixelSize.height();

    x = qBound(0, x, COLUMNS-1);
    y = qBound(0, y, ROWS-1);

    int color = 8 * y + x;

    auto state = MainWindow::getCurrentState();
    if (state)
    {
        int tileIndex = state->getTileIndex();
        int pen = state->getSelectedPen();
        int oldColor = state->getColorForPen(pen, tileIndex);

        if (oldColor != color) {
            state->setColorForPen(pen, color, tileIndex);
            update();
        }
    }
}

void PaletteWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::NoButton)
    {
        auto pos = event->localPos();

        int x = pos.x() / _pixelSize.width();
        int y = pos.y() / _pixelSize.height();

        x = qBound(0, x, COLUMNS-1);
        y = qBound(0, y, ROWS-1);

        int color = 8 * y + x;

        MainWindow::getInstance()->showMessageOnStatusBar(tr("%1 (%2)").arg(Palette::color_names[color]).arg(color));
    }
}


void PaletteWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    // paint with default background color
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    int currentColor = 0;
    int selectedPen = State::PEN_BACKGROUND;
    auto state = MainWindow::getCurrentState();
    if (state)
    {
        selectedPen = state->getSelectedPen();
        currentColor = state->getColorForPen(selectedPen, state->getTileIndex());
    }

    QPen pen;
    pen.setColor(Qt::red);
    pen.setWidth(3);
    pen.setStyle(Qt::PenStyle::DotLine);

    for (int y=0; y<2; y++) {
        for (int x=0; x<8; x++) {
            int c = 8 * y + x;

            if (c==currentColor) {
                painter.setPen(pen);
            } else {
                painter.setPen(Qt::PenStyle::NoPen);
            }

            if (selectedPen == State::PEN_FOREGROUND && state->isMulticolorMode())
                c %= 8;
            painter.setBrush(Palette::getColor(c));

            painter.drawRect(x * _pixelSize.width(),
                             y * _pixelSize.height(),
                             _pixelSize.width() - 1,
                             _pixelSize.height() - 1);
        }
    }

    painter.end();
}

QSize PaletteWidget::sizeHint() const
{
    return _sizeHint;
}

void PaletteWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);

    auto pixel_size_x = size().width() / COLUMNS;
    auto pixel_size_y = size().height() / ROWS;
    _pixelSize = {pixel_size_x, pixel_size_y};
}
