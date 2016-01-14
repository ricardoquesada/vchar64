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

#include "colorrectwidget.h"

#include <QPainter>
#include <QPaintEvent>

#include "state.h"
#include "palette.h"
#include "mainwindow.h"

ColorRectWidget::ColorRectWidget(QWidget *parent)
    : QWidget(parent)
    , _mode(PEN_MODE)
    , _pen(0)
{
    setMinimumSize(60,10);
}

ColorRectWidget::~ColorRectWidget()
{
}


void ColorRectWidget::setPen(int pen)
{
    Q_ASSERT(pen>=0 && pen<State::PEN_MAX && "Invalid colorIndex");
    if (_pen != pen) {
        _pen = pen;
        _mode = PEN_MODE;
        repaint();
    }
}

int ColorRectWidget::getPen() const
{
    return _pen;
}

void ColorRectWidget::setColor(const QColor& color)
{
    _color = color;
    _mode = COLOR_MODE;
    repaint();
}

void ColorRectWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    if (_mode == PEN_MODE)
        painter.fillRect(event->rect(), Palette::getColorForPen(MainWindow::getCurrentState(), _pen));
    else
        painter.fillRect(event->rect(), _color);
    painter.end();
}
