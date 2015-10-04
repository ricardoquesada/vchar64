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

ColorRectWidget::ColorRectWidget(QWidget *parent)
    : QWidget(parent)
    , _pen(0)
{
}

ColorRectWidget::~ColorRectWidget()
{
}

void ColorRectWidget::setPen(int pen)
{
    Q_ASSERT(pen>=0 && pen<State::PEN_MAX && "Invalid colorIndex");
    if (_pen != pen) {
        _pen = pen;
        repaint();
    }
}

int ColorRectWidget::getPen() const
{
    return _pen;
}

void ColorRectWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    painter.fillRect(event->rect(), Palette::getColorForPen(_pen));
    painter.end();
}
