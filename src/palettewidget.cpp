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
 * @file palettewidget.cpp
 * @brief Implementation of `PaletteWidget` 16-color VIC-II swatch rendering and selection (`vchar64`).
 */

#include "palettewidget.h"

#include <algorithm>

#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

#include "mainwindow.h"
#include "palette.h"
#include "state.h"

namespace {

constexpr int kPixelSizeX = 24;
constexpr int kPixelSizeY = 16;
constexpr int kColumns = 8;
constexpr int kRows = 2;

} // namespace

PaletteWidget::PaletteWidget(QWidget* parent)
    : QWidget(parent)
    , _pixelSize(kPixelSizeX, kPixelSizeY)
    , _sizeHint(_pixelSize.width() * kColumns, _pixelSize.height() * kRows)
{
    setMinimumSize(_sizeHint);
    setMouseTracking(true);
}

void PaletteWidget::setActiveState(State* state)
{
    if (_activeState == state) {
        return;
    }

    _activeState = state;
    update();
}

void PaletteWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const QPoint pos = event->pos();
    const int safeW = std::max(1, _pixelSize.width());
    const int safeH = std::max(1, _pixelSize.height());

    const int x = std::clamp(pos.x() / safeW, 0, kColumns - 1);
    const int y = std::clamp(pos.y() / safeH, 0, kRows - 1);
    const int color = 8 * y + x;

    const int tileIndex = state->getTileIndex();
    const int pen = state->getSelectedPen();
    const int oldColor = state->getColorForPen(pen, tileIndex);

    if (oldColor != color) {
        state->setColorForPen(pen, color, tileIndex);
        update();
    }
}

void PaletteWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    if (event->buttons() == Qt::NoButton) {
        const QPoint pos = event->pos();
        const int safeW = std::max(1, _pixelSize.width());
        const int safeH = std::max(1, _pixelSize.height());

        const int x = std::clamp(pos.x() / safeW, 0, kColumns - 1);
        const int y = std::clamp(pos.y() / safeH, 0, kRows - 1);
        const int color = 8 * y + x;

        MainWindow::getInstance()->showMessageOnStatusBar(
            tr("%1 (%2)").arg(Palette::colorName(color)).arg(color));
    }
}

void PaletteWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    int currentColor = 0;
    int selectedPen = State::PEN_BACKGROUND;
    const State* state = _activeState.data();
    if (state != nullptr) {
        selectedPen = state->getSelectedPen();
        currentColor = state->getColorForPen(selectedPen, state->getTileIndex());
    }

    QPen pen;
    pen.setColor(Qt::red);
    pen.setWidth(3);
    pen.setStyle(Qt::PenStyle::DotLine);

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            int c = 8 * y + x;

            if (c == currentColor) {
                painter.setPen(pen);
            } else {
                painter.setPen(Qt::PenStyle::NoPen);
            }

            // In VIC-II Multicolor mode, Color RAM bit 3 selects Multicolor (8..15) vs Hi-Res (0..7)
            // while bits 0..2 select the actual foreground color (`0..7`).
            if (selectedPen == State::PEN_FOREGROUND && state != nullptr && state->isMulticolorMode()) {
                c %= 8;
            }
            painter.setBrush(Palette::getColor(c));

            painter.drawRect(
                x * _pixelSize.width(),
                y * _pixelSize.height(),
                _pixelSize.width() - 1,
                _pixelSize.height() - 1);
        }
    }
}

QSize PaletteWidget::sizeHint() const
{
    return _sizeHint;
}

void PaletteWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)

    const int pixelSizeX = std::max(1, size().width() / kColumns);
    const int pixelSizeY = std::max(1, size().height() / kRows);
    _pixelSize = { pixelSizeX, pixelSizeY };
}
