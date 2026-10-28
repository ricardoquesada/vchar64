/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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

/**
 * @file colorrectwidget.cpp
 * @brief Implementation of `ColorRectWidget` pen and static color swatch painting (`vchar64`).
 */

#include "colorrectwidget.h"

#include <QPaintEvent>
#include <QPainter>

#include "palette.h"
#include "state.h"

ColorRectWidget::ColorRectWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(60, 10);
}

ColorRectWidget::~ColorRectWidget() = default;

void ColorRectWidget::setActiveState(State* state)
{
    if (_activeState == state) {
        return;
    }

    _activeState = state;
    if (_mode == SHOW_PEN_MODE) {
        update();
    }
}

void ColorRectWidget::setPen(int pen)
{
    Q_ASSERT(pen >= 0 && pen < State::PEN_MAX && "Invalid pen");
    if (_pen != pen || _mode != SHOW_PEN_MODE) {
        _pen = pen;
        _mode = SHOW_PEN_MODE;
        update();
    }
}

int ColorRectWidget::getPen() const noexcept
{
    return _pen;
}

void ColorRectWidget::setColorIndex(int colorIndex)
{
    Q_ASSERT(colorIndex >= 0 && colorIndex < 16 && "Invalid color");
    if (_colorIndex != colorIndex || _mode != SHOW_COLORINDEX_MODE) {
        _colorIndex = colorIndex;
        _mode = SHOW_COLORINDEX_MODE;
        update();
    }
}

int ColorRectWidget::getColorIndex() const noexcept
{
    return _colorIndex;
}

void ColorRectWidget::setSelected(bool selected)
{
    if (_selected != selected) {
        _selected = selected;
        update();
    }
}

void ColorRectWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    if (_mode == SHOW_PEN_MODE) {
        painter.fillRect(event->rect(), Palette::getColorForPen(_activeState.data(), _pen));
    } else {
        painter.fillRect(event->rect(), Palette::getColor(_colorIndex));
    }

    if (_selected) {
        QPen pen;
        pen.setColor(Qt::red);
        pen.setWidth(3);
        pen.setStyle(Qt::PenStyle::SolidLine);
        painter.setPen(pen);
        painter.drawRect(0, 0, width() - 1, height() - 1);
    }
}
