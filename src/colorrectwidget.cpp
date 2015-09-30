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
    , _colorIndex(0)
{
}

ColorRectWidget::~ColorRectWidget()
{
}

void ColorRectWidget::setColorIndex(int colorIndex)
{
    Q_ASSERT(colorIndex>=0 && colorIndex<16 && "Invalid colorIndex");
    if (_colorIndex != colorIndex) {
        _colorIndex = colorIndex;
        repaint();
    }
}

int ColorRectWidget::getColorIndex() const
{
    return _colorIndex;
}

void ColorRectWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    auto state = State::getInstance();

    painter.fillRect(event->rect(), Palette::getPalette()[state->getColorAtIndex(_colorIndex)]);

    painter.end();
}
