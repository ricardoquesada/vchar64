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
 * @file charsetwidget.cpp
 * @brief Implementation of `CharsetWidget` 256-glyph rendering, zoom scaling, and selection (`vchar64`).
 */

#include "charsetwidget.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>

#include "core/indexformat.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

constexpr int kColumns = 32;
constexpr int kRows = 8;
constexpr int kOffset = 2;
constexpr qreal kBaseZoomLevel = 2.0;

} // namespace

CharsetWidget::CharsetWidget(QWidget* parent)
    : QWidget(parent)
    , _zoomLevel(kBaseZoomLevel)
{
    _sizeHint = {
        static_cast<int>((kColumns * 8 + kOffset) * kBaseZoomLevel + 1.0),
        static_cast<int>((kRows * 8 + kOffset) * kBaseZoomLevel + 1.0)
    };
    setMinimumSize(_sizeHint);
    setMouseTracking(true);
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

QPoint CharsetWidget::mapMousePositionToCell(const QPoint& pos) const noexcept
{
    const qreal safeZoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const int rawX = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x()) / safeZoom - static_cast<qreal>(kOffset)) / 8.0));
    const int rawY = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y()) / safeZoom - static_cast<qreal>(kOffset)) / 8.0));

    return {
        std::clamp(rawX, 0, kColumns - 1),
        std::clamp(rawY, 0, kRows - 1)
    };
}

QString CharsetWidget::cellStatusMessage(const QPoint& cell) const
{
    const int charIndex = cell.y() * kColumns + cell.x();
    return tr("x: %1, y: %2 | Char: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(charIndex));
}

void CharsetWidget::updateCharIndex(int charIndex)
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    if (_charIndex != charIndex && charIndex >= 0 && charIndex < 256) {
        _charIndex = charIndex;
        state->setCharIndex(charIndex);
    }
}

void CharsetWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    if (_activeState == nullptr || event->button() != Qt::LeftButton) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());
    int x = cell.x();
    int y = cell.y();
    const int charIndex = x + y * kColumns;

    if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
        _selecting = true;

        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), kColumns - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), kRows - _cursorPos.y())
        };
    } else {
        if (_selecting) {
            _selecting = false;
            _selectingSize = { 1, 1 };
        }
        updateCharIndex(charIndex);
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
    int x = cell.x();
    int y = cell.y();

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(cellStatusMessage(cell));
    } else if (event->buttons() == Qt::LeftButton) {
        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), kColumns - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), kRows - _cursorPos.y())
        };
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

    QPoint point;
    switch (event->key()) {
    case Qt::Key_Left:
        point = { -1, 0 };
        break;
    case Qt::Key_Right:
        point = { +1, 0 };
        break;
    case Qt::Key_Down:
        point = { 0, +1 };
        break;
    case Qt::Key_Up:
        point = { 0, -1 };
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    const bool selecting = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (_selecting && !selecting) {
        _selectingSize = { 1, 1 };
    } else if (selecting) {
        _selectingSize += { point.x(), point.y() };

        if (_selectingSize.width() == 0) {
            _selectingSize.setWidth(_selectingSize.width() + point.x());
        }
        if (_selectingSize.height() == 0) {
            _selectingSize.setHeight(_selectingSize.height() + point.y());
        }

        _selectingSize = {
            std::clamp(_selectingSize.width(), -_cursorPos.x(), kColumns - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), kRows - _cursorPos.y())
        };
    } else {
        _cursorPos += point;
        _cursorPos = {
            std::clamp(_cursorPos.x(), 0, kColumns - 1),
            std::clamp(_cursorPos.y(), 0, kRows - 1)
        };

        const int charIndex = _cursorPos.x() + _cursorPos.y() * kColumns;
        updateCharIndex(charIndex);

        MainWindow::getInstance()->showMessageOnStatusBar(cellStatusMessage(_cursorPos));
    }

    _selecting = selecting;
    update();
}

void CharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle BEFORE applying zoom scaling.
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
        QPen gridPen = painter.pen();
        gridPen.setColor(Preferences::getInstance().getGridColor());
        gridPen.setStyle(Qt::DashLine);
        gridPen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(gridPen);

        for (int y = 0; y <= kRows; ++y) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(y * 8 + kOffset)),
                QPointF(static_cast<qreal>(kColumns * 8 + kOffset), static_cast<qreal>(y * 8 + kOffset)));
        }

        for (int x = 0; x <= kColumns; ++x) {
            painter.drawLine(
                QPointF(static_cast<qreal>(x * 8 + kOffset), static_cast<qreal>(kOffset)),
                QPointF(static_cast<qreal>(x * 8 + kOffset), static_cast<qreal>(kRows * 8 + kOffset)));
        }
    }

    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setWidthF(2.0 / _zoomLevel);
    pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(pen);

    if (_selecting) {
        const int plusOneX = _selectingSize.width() < 0 ? 1 : 0;
        const int plusOneY = _selectingSize.height() < 0 ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            (_cursorPos.x() + plusOneX) * 8 + kOffset,
            (_cursorPos.y() + plusOneY) * 8 + kOffset,
            (_selectingSize.width() - plusOneX) * 8,
            (_selectingSize.height() - plusOneY) * 8);
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(_cursorPos.x() * 8 + kOffset, _cursorPos.y() * 8 + kOffset, 8, 8);
    }

    paintFocus(painter);
}

QSize CharsetWidget::sizeHint() const
{
    return _sizeHint;
}

void CharsetWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setWidthF(3.0 / _zoomLevel);
    pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(pen);

    painter.drawLine(kOffset - 1, kOffset - 1, kOffset - 1, kRows * 8 + kOffset);
    painter.drawLine(kColumns * 8 + kOffset, kOffset - 1, kColumns * 8 + kOffset, kRows * 8 + 1);
    painter.drawLine(kOffset - 1, kOffset - 1, kColumns * 8 + 1, kOffset - 1);
    painter.drawLine(kOffset - 1, kRows * 8 + kOffset, kColumns * 8 + kOffset, kRows * 8 + kOffset);
}

void CharsetWidget::onCharIndexUpdated(int charIndex)
{
    if (_selecting) {
        _selecting = false;
        _selectingSize = { 1, 1 };
    }

    const int clamped = std::clamp(charIndex, 0, 255);
    _cursorPos.setX(clamped % kColumns);
    _cursorPos.setY(clamped / kColumns);
    _charIndex = clamped;
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

void CharsetWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex)
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

void CharsetWidget::invalidateCharsetImage()
{
    _imageCache.invalidate();
    update();
}

bool CharsetWidget::hasSelection() const noexcept
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void CharsetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange != nullptr);

    QPoint fixedOrigin = _cursorPos;
    QSize fixedSize = _selecting ? _selectingSize : QSize(1, 1);

    if (fixedSize.width() < 0) {
        fixedOrigin.setX(_cursorPos.x() + fixedSize.width());
        fixedSize.setWidth(-fixedSize.width() + 1);
    }

    if (fixedSize.height() < 0) {
        fixedOrigin.setY(_cursorPos.y() + fixedSize.height());
        fixedSize.setHeight(-fixedSize.height() + 1);
    }

    copyRange->offset = fixedOrigin.y() * kColumns + fixedOrigin.x();
    copyRange->blockSize = fixedSize.width();
    copyRange->count = fixedSize.height();
    copyRange->skip = kColumns - fixedSize.width();
    copyRange->sourceRowWidth = kColumns;
    copyRange->type = State::CopyRange::CHARS;
    copyRange->tileProperties.size = { -1, -1 };
    copyRange->tileProperties.interleaved = -1;
    copyRange->bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
}

int CharsetWidget::getCursorPos() const noexcept
{
    return _cursorPos.y() * kColumns + _cursorPos.x();
}

void CharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void CharsetWidget::setZoomLevel(int zoomLevel)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoomLevel)) * kBaseZoomLevel / 100.0;

    _sizeHint = QSize(
        static_cast<int>(std::ceil(static_cast<qreal>(kColumns * 8 + kOffset) * _zoomLevel + 1.0)),
        static_cast<int>(std::ceil(static_cast<qreal>(kRows * 8 + kOffset) * _zoomLevel + 1.0)));

    setMinimumSize(_sizeHint);
    update();
}
