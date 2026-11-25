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

// Interactive zoomed pixel-grid canvas and MDI document lifecycle implementation.
//
// Implements VIC-II Hi-Res (8x8) and Multicolor (4x8 double-wide) pixel hit-testing,
// keyboard cursor navigation, undoable PaintTileCommand stroke submission, and the
// QMdiSubWindow close/save confirmation prompt (maybeSave()).

#include "bigcharwidget.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include <QCloseEvent>
#include <QColor>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QSizeF>

#include "mainwindow.h"
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

void BigCharWidget::onTileIndexUpdated(int tile_index)
{
    if (_state == nullptr) {
        return;
    }
    if (_tileIndex != tile_index) {
        _tileIndex = tile_index;
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

void BigCharWidget::onTileUpdated(int tile_index)
{
    Q_UNUSED(tile_index)
    update();
}

void BigCharWidget::onCharacterModeChanged(State::CharacterMode mode)
{
    Q_UNUSED(mode)
    update();
}

void BigCharWidget::onMulticolorModeToggled(bool enabled)
{
    Q_UNUSED(enabled)
    update();
}

void BigCharWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
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

void BigCharWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    if (_state == nullptr) {
        return;
    }

    QPen border_pen;
    border_pen.setColor(QWidget::palette().color(QWidget::backgroundRole()));
    border_pen.setWidth(1);
    border_pen.setStyle(Qt::SolidLine);
    painter.setPen(border_pen);

    int char_index = _charIndex;
    const QSizeF pixel_size_f(
        static_cast<qreal>(_pixelSize.width()),
        static_cast<qreal>(_pixelSize.height()));

    for (int row = 0; row < _tileProperties.size.height(); ++row) {
        for (int col = 0; col < _tileProperties.size.width(); ++col) {
            if (char_index >= 0 && char_index < 256) {
                utilsDrawCharInPainter(
                    _state.data(),
                    &painter,
                    pixel_size_f,
                    QPoint(0, 0),
                    QPoint(col, row),
                    char_index);
            }
            char_index += _tileProperties.interleaved;
        }
    }

    paintCursor(painter);
    paintSeparators(painter);
    paintFocus(painter);
}

void BigCharWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (_state == nullptr) {
        return;
    }

    const QPoint pos = event->pos();
    const int pixel_x = pos.x() / std::max(1, _pixelSize.width());
    const int pixel_y = pos.y() / std::max(1, _pixelSize.height());
    if (pixel_x < 0 || pixel_x >= 8 * _tileProperties.size.width()
        || pixel_y < 0 || pixel_y >= 8 * _tileProperties.size.height()) {
        return;
    }

    _cursorPos = QPoint(pixel_x, pixel_y);
    const int selected_pen = _state->getSelectedPen();

    if (event->button() == Qt::LeftButton) {
        paintPixel(pixel_x, pixel_y, selected_pen);
    } else if (event->button() == Qt::RightButton) {
        paintPixel(pixel_x, pixel_y, State::PEN_BACKGROUND);
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
    const int pixel_x = std::clamp(
        pos.x() / std::max(1, _pixelSize.width()),
        0,
        8 * _tileProperties.size.width() - 1);
    const int pixel_y = std::clamp(
        pos.y() / std::max(1, _pixelSize.height()),
        0,
        8 * _tileProperties.size.height() - 1);

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(pixel_x).arg(pixel_y));
        return;
    }

    _cursorPos = QPoint(pixel_x, pixel_y);
    const int selected_pen = _state->getSelectedPen();
    const Qt::MouseButtons buttons = event->buttons();
    if (buttons == Qt::LeftButton) {
        paintPixel(pixel_x, pixel_y, selected_pen);
    } else if (buttons == Qt::RightButton) {
        paintPixel(pixel_x, pixel_y, State::PEN_BACKGROUND);
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

    bool needs_update = false;
    const bool is_multicolor = _state->shouldBeDisplayedInMulticolor2(_tileIndex);
    const bool is_ecm = _state->isExtendedColorMode();
    const QPoint old_cursor_pos = _cursorPos;
    const int step_x = is_multicolor ? 2 : 1;

    switch (event->key()) {
    case Qt::Key_Left:
        _cursorPos += { -step_x, 0 };
        break;
    case Qt::Key_Right:
        _cursorPos += { +step_x, 0 };
        break;
    case Qt::Key_Down:
        _cursorPos += { 0, +1 };
        break;
    case Qt::Key_Up:
        _cursorPos += { 0, -1 };
        break;
    case Qt::Key_1:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_BACKGROUND);
        needs_update = true;
        break;
    case Qt::Key_2:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_FOREGROUND);
        needs_update = true;
        break;
    case Qt::Key_3:
        if (is_multicolor || is_ecm) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR1);
            needs_update = true;
        }
        break;
    case Qt::Key_4:
        if (is_multicolor || is_ecm) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR2);
            needs_update = true;
        }
        break;
    case Qt::Key_5:
        if (is_ecm) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_BACKGROUND3);
            needs_update = true;
        }
        break;
    case Qt::Key_Space:
        cyclePixel(_cursorPos.x(), _cursorPos.y());
        needs_update = true;
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }

    _cursorPos = QPoint(
        std::clamp(_cursorPos.x(), 0, std::max(0, 8 * _tileProperties.size.width() - step_x)),
        std::clamp(_cursorPos.y(), 0, std::max(0, 8 * _tileProperties.size.height() - 1)));

    if (old_cursor_pos != _cursorPos) {
        needs_update = true;
    }

    if (needs_update) {
        update();
        MainWindow::getInstance()->showMessageOnStatusBar(
            tr("x: %1, y: %2").arg(_cursorPos.x()).arg(_cursorPos.y()));
    }
}

void BigCharWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    recalculatePixelSize();
}

void BigCharWidget::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    // Mark the owning DocumentSession as closing immediately so MainWindow
    // excludes this subwindow before Qt's deferred WA_DeleteOnClose runs.
    MainWindow::getInstance()->notifyEditorClosing(this);
    event->accept();
}

void BigCharWidget::paintCursor(QPainter& painter)
{
    if (_state == nullptr) {
        return;
    }

    painter.setBrush(QColor(0, 0, 255, 24));
    QPen cursor_pen;
    cursor_pen.setColor({ 149, 195, 244, 255 });
    cursor_pen.setStyle(Qt::SolidLine);
    cursor_pen.setWidth(hasFocus() ? 3 : 1);
    painter.setPen(cursor_pen);

    int bit_width = 1;
    int cursor_pixel_x = _cursorPos.x();
    if (_state->shouldBeDisplayedInMulticolor2(_tileIndex)) {
        bit_width = 2;
        cursor_pixel_x = (cursor_pixel_x / 2) * 2;
    }

    painter.drawRect(
        cursor_pixel_x * _pixelSize.width(),
        _cursorPos.y() * _pixelSize.height(),
        _pixelSize.width() * bit_width,
        _pixelSize.height());
}

void BigCharWidget::paintSeparators(QPainter& painter)
{
    QPen separator_pen;
    separator_pen.setColor({ 128, 128, 128, 196 });
    separator_pen.setWidth(2);
    separator_pen.setStyle(Qt::SolidLine);
    painter.setPen(separator_pen);

    const int total_height = _pixelSize.height() * _tileProperties.size.height() * 8;
    const int total_width = _pixelSize.width() * _tileProperties.size.width() * 8;

    for (int col = 1; col < _tileProperties.size.width(); ++col) {
        const int line_x = col * _pixelSize.width() * 8;
        painter.drawLine(line_x, 0, line_x, total_height);
    }
    for (int row = 1; row < _tileProperties.size.height(); ++row) {
        const int line_y = row * _pixelSize.height() * 8;
        painter.drawLine(0, line_y, total_width, line_y);
    }
}

void BigCharWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen focus_pen;
    focus_pen.setColor({ 149, 195, 244, 255 });
    focus_pen.setWidth(3);
    focus_pen.setStyle(Qt::SolidLine);
    painter.setPen(focus_pen);

    const int total_width = _tileProperties.size.width() * _pixelSize.width() * 8;
    const int total_height = _tileProperties.size.height() * _pixelSize.height() * 8;

    painter.drawLine(0, 0, 0, total_height);
    painter.drawLine(total_width, 0, total_width, total_height);
    painter.drawLine(0, 0, total_width, 0);
    painter.drawLine(0, total_height, total_width, total_height);
}

void BigCharWidget::recalculatePixelSize() noexcept
{
    const int max_tile_dim = std::max({ 1, _tileProperties.size.width(), _tileProperties.size.height() });
    const int min_widget_dim = std::min(size().width(), size().height());
    const int pixel_edge = std::max(1, min_widget_dim / (8 * max_tile_dim));
    _pixelSize = QSize(pixel_edge, pixel_edge);
}

bool BigCharWidget::maybeSave()
{
    if (_state == nullptr || !_state->isModified()) {
        return true;
    }

    const QMessageBox::StandardButton button_choice = QMessageBox::warning(
        this,
        tr("Application"),
        tr("There are unsaved changes.\n"
           "Do you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (button_choice == QMessageBox::Save) {
        // Pass _state.data() explicitly rather than relying on activeSubWindow() so
        // closing a background MDI tab never saves the wrong project.
        return MainWindow::getInstance()->saveDocument(_state.data());
    }
    if (button_choice == QMessageBox::Cancel) {
        return false;
    }
    return true;
}

void BigCharWidget::paintPixel(int pixel_x, int pixel_y, int pen)
{
    if (_state == nullptr) {
        return;
    }

    int effective_pen = pen;
    int target_tile_pen = pen;
    if (!_state->shouldBeDisplayedInMulticolor2(_tileIndex)) {
        const bool is_bg = _state->isBackgroundPen(pen);
        effective_pen = is_bg ? State::PEN_BACKGROUND : State::PEN_FOREGROUND;
        // State::tileGetPen() returns raw 1-bit pixel values (0 or 1) in 1bpp modes.
        target_tile_pen = is_bg ? 0 : 1;
    }

    if (_state->tileGetPen(_tileIndex, QPoint(pixel_x, pixel_y)) != target_tile_pen) {
        _state->tilePaint(_tileIndex, QPoint(pixel_x, pixel_y), effective_pen, _commandMergeable);
    }

    update();
}

void BigCharWidget::cyclePixel(int pixel_x, int pixel_y)
{
    if (_state == nullptr) {
        return;
    }

    const int current_pen = std::clamp(_state->tileGetPen(_tileIndex, QPoint(pixel_x, pixel_y)), 0, 3);

    static constexpr std::array<int, 4> kNextPenMulticolor = {
        State::PEN_FOREGROUND,
        State::PEN_MULTICOLOR2,
        State::PEN_BACKGROUND,
        State::PEN_MULTICOLOR1,
    };
    static constexpr std::array<int, 2> kNextPenHiRes = {
        State::PEN_FOREGROUND,
        State::PEN_BACKGROUND,
    };

    const int next_pen = _state->shouldBeDisplayedInMulticolor2(_tileIndex)
        ? kNextPenMulticolor[static_cast<size_t>(current_pen)]
        : kNextPenHiRes[static_cast<size_t>(current_pen & 1)];

    paintPixel(pixel_x, pixel_y, next_pen);
}
