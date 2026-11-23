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

// PaletteWidget implementation for 16-color VIC-II swatch rendering and selection.

#include "palettewidget.h"

#include <algorithm>

#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QResizeEvent>
#include <QWidget>

#include "mainwindow.h"
#include "palette.h"
#include "state.h"

namespace {

constexpr int kDefaultSwatchWidth = 24;
constexpr int kDefaultSwatchHeight = 16;
constexpr int kColumns = 8;
constexpr int kRows = 2;

// Maps widget-local coordinates `pos` into a clamped C64 color index (`0..15`)
// for the `kColumns x kRows` swatch grid.
[[nodiscard]] int ColorIndexAtPosition(const QPoint& pos, const QSize& swatch_size) noexcept
{
    const int safe_width = std::max(1, swatch_size.width());
    const int safe_height = std::max(1, swatch_size.height());
    const int column = std::clamp(pos.x() / safe_width, 0, kColumns - 1);
    const int row = std::clamp(pos.y() / safe_height, 0, kRows - 1);
    return kColumns * row + column;
}

} // namespace

PaletteWidget::PaletteWidget(QWidget* parent)
    : QWidget(parent)
    , _pixelSize(kDefaultSwatchWidth, kDefaultSwatchHeight)
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

void PaletteWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    int current_color = 0;
    int selected_pen = State::PEN_BACKGROUND;
    const State* state = _activeState.data();
    if (state != nullptr) {
        selected_pen = state->getSelectedPen();
        current_color = state->getColorForPen(selected_pen, state->getTileIndex());
    }

    QPen selection_pen;
    selection_pen.setColor(Qt::red);
    selection_pen.setWidth(3);
    selection_pen.setStyle(Qt::PenStyle::DotLine);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int swatch_color = kColumns * row + col;

            if (swatch_color == current_color) {
                painter.setPen(selection_pen);
            } else {
                painter.setPen(Qt::PenStyle::NoPen);
            }

            // In VIC-II Multicolor mode, Color RAM bit 3 selects Multicolor (8..15) vs Hi-Res (0..7)
            // while bits 0..2 select the actual foreground color (0..7).
            int displayed_color = swatch_color;
            if (selected_pen == State::PEN_FOREGROUND && state != nullptr && state->isMulticolorMode()) {
                displayed_color %= 8;
            }
            painter.setBrush(Palette::getColor(displayed_color));

            painter.drawRect(
                col * _pixelSize.width(),
                row * _pixelSize.height(),
                _pixelSize.width() - 1,
                _pixelSize.height() - 1);
        }
    }
}

void PaletteWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const int color_index = ColorIndexAtPosition(event->pos(), _pixelSize);
    const int tile_index = state->getTileIndex();
    const int selected_pen = state->getSelectedPen();
    const int old_color = state->getColorForPen(selected_pen, tile_index);

    if (old_color != color_index) {
        state->setColorForPen(selected_pen, color_index, tile_index);
        update();
    }
}

void PaletteWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    if (event->buttons() != Qt::NoButton) {
        return;
    }

    const int color_index = ColorIndexAtPosition(event->pos(), _pixelSize);
    MainWindow::getInstance()->showMessageOnStatusBar(
        tr("%1 (%2)").arg(Palette::colorName(color_index)).arg(color_index));
}

QSize PaletteWidget::sizeHint() const
{
    return _sizeHint;
}

void PaletteWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)

    const int swatch_width = std::max(1, size().width() / kColumns);
    const int swatch_height = std::max(1, size().height() / kRows);
    _pixelSize = QSize(swatch_width, swatch_height);
}
