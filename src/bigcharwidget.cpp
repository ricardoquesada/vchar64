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
 * @file bigcharwidget.cpp
 * @brief Interactive zoomed pixel-grid canvas and MDI document lifecycle implementation.
 *
 * Implements VIC-II Hi-Res (`8x8`) and Multicolor (`4x8` double-wide) pixel
 * hit-testing, keyboard cursor navigation, undoable `PaintTileCommand` stroke
 * submission, and the `QMdiSubWindow` close/save confirmation prompt (`maybeSave()`).
 */

#include "bigcharwidget.h"

#include <algorithm>
#include <array>

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSizeF>

#include "mainwindow.h"
#include "palette.h"
#include "state.h"
#include "utils.h"

BigCharWidget::BigCharWidget(State* state, QWidget* parent)
    : QWidget(parent)
    , _state(state)
{
    Q_ASSERT(state != nullptr && "Invalid State");

    _tileProperties = _state->getTileProperties();
    _tileIndex = _state->getTileIndex();
    _charIndex = _state->getCharIndexFromTileIndex(_tileIndex);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

BigCharWidget::~BigCharWidget() = default;

int BigCharWidget::getTileIndex() const noexcept
{
    return _tileIndex;
}

QSize BigCharWidget::getTileSize() const noexcept
{
    return _tileProperties.size;
}

State* BigCharWidget::getState() const noexcept
{
    return _state.data();
}

void BigCharWidget::recalculatePixelSize() noexcept
{
    const int maxTileDim = std::max({ 1, _tileProperties.size.width(), _tileProperties.size.height() });
    const int minWidgetDim = std::min(size().width(), size().height());
    const int px = std::max(1, minWidgetDim / (8 * maxTileDim));
    _pixelSize = QSize(px, px);
}

void BigCharWidget::paintPixel(int x, int y, int pen)
{
    if (_state == nullptr) {
        return;
    }

    if (_state->tileGetPen(_tileIndex, QPoint(x, y)) != pen) {
        int effectivePen = pen;
        if (!_state->shouldBeDisplayedInMulticolor2(_tileIndex) && effectivePen != 0) {
            effectivePen = 1;
        }
        _state->tilePaint(_tileIndex, QPoint(x, y), effectivePen, _commandMergeable);
    }

    update();
}

void BigCharWidget::cyclePixel(int x, int y)
{
    if (_state == nullptr) {
        return;
    }

    const int currentPen = std::clamp(_state->tileGetPen(_tileIndex, QPoint(x, y)), 0, 3);

    static constexpr std::array<int, 4> kNextPenMc = {
        State::PEN_FOREGROUND,
        State::PEN_MULTICOLOR2,
        State::PEN_BACKGROUND,
        State::PEN_MULTICOLOR1
    };
    static constexpr std::array<int, 2> kNextPenHr = {
        State::PEN_FOREGROUND,
        State::PEN_BACKGROUND
    };

    const int nextPen = _state->shouldBeDisplayedInMulticolor2(_tileIndex)
        ? kNextPenMc[static_cast<size_t>(currentPen)]
        : kNextPenHr[static_cast<size_t>(currentPen & 1)];

    paintPixel(x, y, nextPen);
}

void BigCharWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (_state == nullptr) {
        return;
    }

    const QPoint pos = event->pos();
    const int x = pos.x() / std::max(1, _pixelSize.width());
    const int y = pos.y() / std::max(1, _pixelSize.height());
    if (x < 0 || x >= 8 * _tileProperties.size.width()
        || y < 0 || y >= 8 * _tileProperties.size.height()) {
        return;
    }

    _cursorPos = { x, y };
    const int selectedPen = _state->getSelectedPen();

    if (event->button() == Qt::LeftButton) {
        paintPixel(x, y, selectedPen);
    } else if (event->button() == Qt::RightButton) {
        paintPixel(x, y, State::PEN_BACKGROUND);
    }

    _commandMergeable = true;
}

void BigCharWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    if (_state == nullptr) {
        return;
    }

    const QPoint pos = event->pos();
    const int x = std::clamp(
        pos.x() / std::max(1, _pixelSize.width()),
        0,
        8 * _tileProperties.size.width() - 1);
    const int y = std::clamp(
        pos.y() / std::max(1, _pixelSize.height()),
        0,
        8 * _tileProperties.size.height() - 1);

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
        return;
    }

    _cursorPos = { x, y };
    const int pen = _state->getSelectedPen();
    const Qt::MouseButtons buttons = event->buttons();
    if (buttons == Qt::LeftButton) {
        paintPixel(x, y, pen);
    } else if (buttons == Qt::RightButton) {
        paintPixel(x, y, State::PEN_BACKGROUND);
    }

    _commandMergeable = true;
}

void BigCharWidget::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    _commandMergeable = false;
}

void BigCharWidget::keyPressEvent(QKeyEvent* event)
{
    event->accept();
    if (_state == nullptr) {
        return;
    }

    bool updated = false;
    const bool ismc = _state->shouldBeDisplayedInMulticolor2(_tileIndex);
    const QPoint oldCursorPos = _cursorPos;
    const int incrementX = ismc ? 2 : 1;

    switch (event->key()) {
    case Qt::Key_Left:
        _cursorPos += { -incrementX, 0 };
        break;
    case Qt::Key_Right:
        _cursorPos += { +incrementX, 0 };
        break;
    case Qt::Key_Down:
        _cursorPos += { 0, +1 };
        break;
    case Qt::Key_Up:
        _cursorPos += { 0, -1 };
        break;
    case Qt::Key_1:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_BACKGROUND);
        updated = true;
        break;
    case Qt::Key_2:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_FOREGROUND);
        updated = true;
        break;
    case Qt::Key_3:
        if (ismc) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR1);
            updated = true;
        }
        break;
    case Qt::Key_4:
        if (ismc) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR2);
            updated = true;
        }
        break;
    case Qt::Key_Space:
        cyclePixel(_cursorPos.x(), _cursorPos.y());
        updated = true;
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }

    _cursorPos = {
        std::clamp(_cursorPos.x(), 0, std::max(0, 8 * _tileProperties.size.width() - incrementX)),
        std::clamp(_cursorPos.y(), 0, std::max(0, 8 * _tileProperties.size.height() - 1))
    };

    if (oldCursorPos != _cursorPos) {
        updated = true;
    }

    if (updated) {
        update();
        MainWindow::getInstance()->showMessageOnStatusBar(
            tr("x: %1, y: %2").arg(_cursorPos.x()).arg(_cursorPos.y()));
    }
}

void BigCharWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    if (_state == nullptr) {
        return;
    }

    QPen pen;
    pen.setColor(QWidget::palette().color(QWidget::backgroundRole()));
    pen.setWidth(1);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);

    int charIndex = _charIndex;
    const QSizeF pixelSizeF(
        static_cast<qreal>(_pixelSize.width()),
        static_cast<qreal>(_pixelSize.height()));

    for (int y = 0; y < _tileProperties.size.height(); ++y) {
        for (int x = 0; x < _tileProperties.size.width(); ++x) {
            if (charIndex >= 0 && charIndex < 256) {
                utilsDrawCharInPainter(
                    _state.data(),
                    &painter,
                    pixelSizeF,
                    QPoint(0, 0),
                    QPoint(x, y),
                    charIndex);
            }
            charIndex += _tileProperties.interleaved;
        }
    }

    paintCursor(painter);
    paintSeparators(painter);
    paintFocus(painter);
}

void BigCharWidget::paintCursor(QPainter& painter)
{
    if (_state == nullptr) {
        return;
    }

    painter.setBrush(QColor(0, 0, 255, 24));
    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setStyle(Qt::SolidLine);
    pen.setWidth(hasFocus() ? 3 : 1);
    painter.setPen(pen);

    int bitWidth = 1;
    int x = _cursorPos.x();
    if (_state->shouldBeDisplayedInMulticolor2(_tileIndex)) {
        bitWidth = 2;
        x = (x / 2) * 2;
    }

    painter.drawRect(
        x * _pixelSize.width(),
        _cursorPos.y() * _pixelSize.height(),
        _pixelSize.width() * bitWidth,
        _pixelSize.height());
}

void BigCharWidget::paintSeparators(QPainter& painter)
{
    QPen pen;
    pen.setColor({ 128, 128, 128, 196 });
    pen.setWidth(2);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);

    const int totalH = _pixelSize.height() * _tileProperties.size.height() * 8;
    const int totalW = _pixelSize.width() * _tileProperties.size.width() * 8;

    for (int x = 1; x < _tileProperties.size.width(); ++x) {
        const int lineX = x * _pixelSize.width() * 8;
        painter.drawLine(lineX, 0, lineX, totalH);
    }
    for (int y = 1; y < _tileProperties.size.height(); ++y) {
        const int lineY = y * _pixelSize.height() * 8;
        painter.drawLine(0, lineY, totalW, lineY);
    }
}

void BigCharWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setWidth(3);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);

    const int totalW = _tileProperties.size.width() * _pixelSize.width() * 8;
    const int totalH = _tileProperties.size.height() * _pixelSize.height() * 8;

    painter.drawLine(0, 0, 0, totalH);
    painter.drawLine(totalW, 0, totalW, totalH);
    painter.drawLine(0, 0, totalW, 0);
    painter.drawLine(0, totalH, totalW, totalH);
}

bool BigCharWidget::maybeSave()
{
    if (_state == nullptr || !_state->isModified()) {
        return true;
    }

    const QMessageBox::StandardButton ret = QMessageBox::warning(
        this,
        tr("Application"),
        tr("There are unsaved changes.\n"
           "Do you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save) {
        // Pass `_state.data()` explicitly rather than relying on `activeSubWindow()` so
        // closing a background MDI tab never saves the wrong project.
        return MainWindow::getInstance()->saveDocument(_state.data());
    }
    if (ret == QMessageBox::Cancel) {
        return false;
    }
    return true;
}

void BigCharWidget::onMulticolorModeToggled(bool enabled)
{
    Q_UNUSED(enabled)
    update();
}

void BigCharWidget::onTileIndexUpdated(int tileIndex)
{
    if (_state == nullptr) {
        return;
    }
    if (_tileIndex != tileIndex) {
        _tileIndex = tileIndex;
        _charIndex = _state->getCharIndexFromTileIndex(_tileIndex);
        update();
    }
}

void BigCharWidget::onTilePropertiesUpdated()
{
    if (_state == nullptr) {
        return;
    }
    _tileProperties = _state->getTileProperties();
    _tileIndex = _state->getTileIndex();
    _charIndex = _state->getCharIndexFromTileIndex(_tileIndex);
    recalculatePixelSize();
    update();
}

void BigCharWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
    update();
}

void BigCharWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex)
    update();
}

void BigCharWidget::onCharsetUpdated()
{
    update();
}

void BigCharWidget::onFileLoaded()
{
    onTilePropertiesUpdated();
}

void BigCharWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    recalculatePixelSize();
}

void BigCharWidget::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        // Mark the owning `DocumentSession` as closing immediately so `MainWindow`
        // excludes this subwindow before Qt's deferred `WA_DeleteOnClose` runs.
        MainWindow::getInstance()->notifyEditorClosing(this);
        event->accept();
    } else {
        event->ignore();
    }
}
