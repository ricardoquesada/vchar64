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

// CharsetWidget implementation for 256-glyph rendering, zoom scaling, and selection.

#include "charsetwidget.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QString>

#include "core/indexformat.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

constexpr int kColumns = 32;
constexpr int kRows = 8;
constexpr int kOffset = 2;
constexpr qreal kBaseZoomLevel = 2.0;

// Computes a signed rectangular selection size spanning from `cursor_pos` to `target_cell`,
// inclusive of `cursor_pos`, clamped within the `kColumns x kRows` charset grid.
[[nodiscard]] QSize ComputeDragSelectionSize(const QPoint& cursor_pos, const QPoint& target_cell) noexcept
{
    int end_col = target_cell.x();
    int end_row = target_cell.y();
    if (end_col >= cursor_pos.x()) {
        ++end_col;
    }
    if (end_row >= cursor_pos.y()) {
        ++end_row;
    }

    return QSize(
        std::clamp(end_col - cursor_pos.x(), -cursor_pos.x(), kColumns - cursor_pos.x()),
        std::clamp(end_row - cursor_pos.y(), -cursor_pos.y(), kRows - cursor_pos.y()));
}

} // namespace

CharsetWidget::CharsetWidget(QWidget* parent)
    : QWidget(parent)
    , _zoomLevel(kBaseZoomLevel)
{
    _sizeHint = QSize(
        static_cast<int>((kColumns * 8 + kOffset) * kBaseZoomLevel + 1.0),
        static_cast<int>((kRows * 8 + kOffset) * kBaseZoomLevel + 1.0));
    setMinimumSize(_sizeHint);
    setMouseTracking(true);
}

bool CharsetWidget::hasSelection() const noexcept
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void CharsetWidget::getSelectionRange(State::CopyRange* copy_range) const
{
    Q_ASSERT(copy_range != nullptr);

    QPoint normalized_origin = _cursorPos;
    QSize normalized_size = _selecting ? _selectingSize : QSize(1, 1);

    if (normalized_size.width() < 0) {
        normalized_origin.setX(_cursorPos.x() + normalized_size.width());
        normalized_size.setWidth(-normalized_size.width() + 1);
    }

    if (normalized_size.height() < 0) {
        normalized_origin.setY(_cursorPos.y() + normalized_size.height());
        normalized_size.setHeight(-normalized_size.height() + 1);
    }

    copy_range->offset = normalized_origin.y() * kColumns + normalized_origin.x();
    copy_range->blockSize = normalized_size.width();
    copy_range->count = normalized_size.height();
    copy_range->skip = kColumns - normalized_size.width();
    copy_range->sourceRowWidth = kColumns;
    copy_range->type = State::CopyRange::CHARS;
    copy_range->tileProperties.size = { -1, -1 };
    copy_range->tileProperties.interleaved = -1;
    copy_range->bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
}

int CharsetWidget::getCursorPos() const noexcept
{
    return _cursorPos.y() * kColumns + _cursorPos.x();
}

void CharsetWidget::setActiveState(State* state)
{
    if (_activeState == state) {
        return;
    }

    _activeState = state;
    _imageCache.invalidate();
    _selecting = false;
    _selectingSize = { 1, 1 };

    if (state != nullptr) {
        onCharIndexUpdated(state->getCharIndex());
    } else {
        _charIndex = -1;
    }

    update();
}

void CharsetWidget::onCharIndexUpdated(int char_index)
{
    if (_selecting) {
        _selecting = false;
        _selectingSize = { 1, 1 };
    }

    const int clamped_char_index = std::clamp(char_index, 0, 255);
    _cursorPos.setX(clamped_char_index % kColumns);
    _cursorPos.setY(clamped_char_index / kColumns);
    _charIndex = clamped_char_index;
    update();
}

void CharsetWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state)
    invalidateCharsetImage();
}

void CharsetWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
    invalidateCharsetImage();
}

void CharsetWidget::onTileUpdated(int tile_index)
{
    Q_UNUSED(tile_index)
    invalidateCharsetImage();
}

void CharsetWidget::onBytesUpdated(int pos, int count)
{
    Q_UNUSED(pos)
    Q_UNUSED(count)
    invalidateCharsetImage();
}

void CharsetWidget::onCharsetUpdated()
{
    invalidateCharsetImage();
}

void CharsetWidget::onTilePropertiesUpdated()
{
    invalidateCharsetImage();
}

void CharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void CharsetWidget::setZoomLevel(int zoom_level)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoom_level)) * kBaseZoomLevel / 100.0;

    _sizeHint = QSize(
        static_cast<int>(std::ceil(static_cast<qreal>(kColumns * 8 + kOffset) * _zoomLevel + 1.0)),
        static_cast<int>(std::ceil(static_cast<qreal>(kRows * 8 + kOffset) * _zoomLevel + 1.0)));

    setMinimumSize(_sizeHint);
    update();
}

void CharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle before applying zoom scaling.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    const State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    painter.scale(_zoomLevel, _zoomLevel);

    // One blit of the cached 256x64 glyph sheet replaces 256 per-glyph rasterizations per repaint.
    painter.drawImage(
        QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(kOffset)),
        _imageCache.sync(*state));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    if (_displayGrid) {
        QPen grid_pen = painter.pen();
        grid_pen.setColor(Preferences::getInstance().getGridColor());
        grid_pen.setStyle(Qt::DashLine);
        grid_pen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(grid_pen);

        for (int row = 0; row <= kRows; ++row) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(row * 8 + kOffset)),
                QPointF(static_cast<qreal>(kColumns * 8 + kOffset), static_cast<qreal>(row * 8 + kOffset)));
        }

        for (int col = 0; col <= kColumns; ++col) {
            painter.drawLine(
                QPointF(static_cast<qreal>(col * 8 + kOffset), static_cast<qreal>(kOffset)),
                QPointF(static_cast<qreal>(col * 8 + kOffset), static_cast<qreal>(kRows * 8 + kOffset)));
        }
    }

    QPen selection_pen;
    selection_pen.setColor({ 149, 195, 244, 255 });
    selection_pen.setWidthF(2.0 / _zoomLevel);
    selection_pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(selection_pen);

    if (_selecting) {
        // Shift origin +1 cell when dragging left/up so the box stays anchored on _cursorPos.
        const int cursor_adjust_x = _selectingSize.width() < 0 ? 1 : 0;
        const int cursor_adjust_y = _selectingSize.height() < 0 ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            (_cursorPos.x() + cursor_adjust_x) * 8 + kOffset,
            (_cursorPos.y() + cursor_adjust_y) * 8 + kOffset,
            (_selectingSize.width() - cursor_adjust_x) * 8,
            (_selectingSize.height() - cursor_adjust_y) * 8);
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(_cursorPos.x() * 8 + kOffset, _cursorPos.y() * 8 + kOffset, 8, 8);
    }

    paintFocus(painter);
}

void CharsetWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    if (_activeState == nullptr || event->button() != Qt::LeftButton) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());
    const int char_index = cell.x() + cell.y() * kColumns;

    if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
        _selecting = true;
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell);
    } else {
        if (_selecting) {
            _selecting = false;
            _selectingSize = { 1, 1 };
        }
        updateCharIndex(char_index);
    }

    update();
}

void CharsetWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    if (_activeState == nullptr) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(cellStatusMessage(cell));
    } else if (event->buttons() == Qt::LeftButton) {
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell);
        _selecting = true;
        update();
    }
}

void CharsetWidget::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    if (_activeState == nullptr) {
        return;
    }

    QPoint cursor_delta;
    switch (event->key()) {
    case Qt::Key_Left:
        cursor_delta = { -1, 0 };
        break;
    case Qt::Key_Right:
        cursor_delta = { +1, 0 };
        break;
    case Qt::Key_Down:
        cursor_delta = { 0, +1 };
        break;
    case Qt::Key_Up:
        cursor_delta = { 0, -1 };
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    const bool is_selecting = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (_selecting && !is_selecting) {
        _selectingSize = { 1, 1 };
    } else if (is_selecting) {
        _selectingSize += { cursor_delta.x(), cursor_delta.y() };

        if (_selectingSize.width() == 0) {
            _selectingSize.setWidth(_selectingSize.width() + cursor_delta.x());
        }
        if (_selectingSize.height() == 0) {
            _selectingSize.setHeight(_selectingSize.height() + cursor_delta.y());
        }

        _selectingSize = QSize(
            std::clamp(_selectingSize.width(), -_cursorPos.x(), kColumns - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), kRows - _cursorPos.y()));
    } else {
        _cursorPos += cursor_delta;
        _cursorPos = QPoint(
            std::clamp(_cursorPos.x(), 0, kColumns - 1),
            std::clamp(_cursorPos.y(), 0, kRows - 1));

        const int char_index = _cursorPos.x() + _cursorPos.y() * kColumns;
        updateCharIndex(char_index);

        MainWindow::getInstance()->showMessageOnStatusBar(cellStatusMessage(_cursorPos));
    }

    _selecting = is_selecting;
    update();
}

QSize CharsetWidget::sizeHint() const
{
    return _sizeHint;
}

void CharsetWidget::updateCharIndex(int char_index)
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    if (_charIndex != char_index && char_index >= 0 && char_index < 256) {
        _charIndex = char_index;
        state->setCharIndex(char_index);
    }
}

void CharsetWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen focus_pen;
    focus_pen.setColor({ 149, 195, 244, 255 });
    focus_pen.setWidthF(3.0 / _zoomLevel);
    focus_pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(focus_pen);

    painter.drawLine(kOffset - 1, kOffset - 1, kOffset - 1, kRows * 8 + kOffset);
    painter.drawLine(kColumns * 8 + kOffset, kOffset - 1, kColumns * 8 + kOffset, kRows * 8 + 1);
    painter.drawLine(kOffset - 1, kOffset - 1, kColumns * 8 + 1, kOffset - 1);
    painter.drawLine(kOffset - 1, kRows * 8 + kOffset, kColumns * 8 + kOffset, kRows * 8 + kOffset);
}

QPoint CharsetWidget::mapMousePositionToCell(const QPoint& pos) const noexcept
{
    const qreal safe_zoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const int raw_col = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x()) / safe_zoom - static_cast<qreal>(kOffset)) / 8.0));
    const int raw_row = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y()) / safe_zoom - static_cast<qreal>(kOffset)) / 8.0));

    return QPoint(
        std::clamp(raw_col, 0, kColumns - 1),
        std::clamp(raw_row, 0, kRows - 1));
}

QString CharsetWidget::cellStatusMessage(const QPoint& cell) const
{
    const int char_index = cell.y() * kColumns + cell.x();
    return tr("x: %1, y: %2 | Char: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(char_index));
}

void CharsetWidget::invalidateCharsetImage()
{
    _imageCache.invalidate();
    update();
}
