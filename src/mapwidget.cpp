/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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

// MapWidget implementation for viewport-clipped atlas rendering and brush/fill/select tools.

#include "mapwidget.h"

#include <algorithm>
#include <cmath>

#include <QByteArray>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "core/documentimagerenderer.h"
#include "core/indexformat.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"
#include "utils.h"

namespace {

constexpr qreal kBaseZoomLevel = 2.0;
constexpr int kOffset = 0;

// Formats status-bar text for a map cell, e.g. "x: 12, y: 5 | Tile: 17 ($11)".
[[nodiscard]] QString MapCellStatusMessage(const State& state, const QPoint& cell)
{
    return MapWidget::tr("x: %1, y: %2 | Tile: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(state.getTileIndexFromMap(cell)));
}

// Translates an ASCII character to a map screen code, preserving the currently
// selected tile's 2-bit background bank (bits 7..6) when ECM is active.
[[nodiscard]] quint8 ResolveTypedMapScreenCode(const State& state, quint8 ascii_code)
{
    quint8 screen_code = utilsAsciiToScreenCode(&state, ascii_code);
    if (state.isExtendedColorMode()) {
        const quint8 bg_bank_bits = static_cast<quint8>(static_cast<unsigned int>(state.getTileIndex()) & 0xC0U);
        screen_code = static_cast<quint8>((screen_code & 0x3FU) | bg_bank_bits);
    }
    return screen_code;
}

// Computes a signed rectangular selection size spanning from `cursor_pos` to `target_cell`,
// inclusive of `cursor_pos`, clamped within `map_size`.
[[nodiscard]] QSize ComputeDragSelectionSize(
    const QPoint& cursor_pos,
    const QPoint& target_cell,
    const QSize& map_size) noexcept
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
        std::clamp(end_col - cursor_pos.x(), -cursor_pos.x(), map_size.width() - cursor_pos.x()),
        std::clamp(end_row - cursor_pos.y(), -cursor_pos.y(), map_size.height() - cursor_pos.y()));
}

} // namespace

MapWidget::MapWidget(QWidget* parent)
    : QWidget(parent)
{
    updateGeometryMetrics();
    setMouseTracking(true);
}

void MapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void MapWidget::setMode(MapMode mode)
{
    if (_mode != mode) {
        _mode = mode;
    }
}

void MapWidget::setZoomLevel(int zoom_level)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoom_level)) * kBaseZoomLevel / 100.0;
    updateGeometryMetrics();
    update();
}

void MapWidget::getSelectionRange(State::CopyRange* copy_range) const
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

    copy_range->offset = normalized_origin.y() * _mapSize.width() + normalized_origin.x();
    copy_range->blockSize = normalized_size.width();
    copy_range->count = normalized_size.height();
    copy_range->skip = _mapSize.width() - normalized_size.width();
    copy_range->sourceRowWidth = _mapSize.width();
    copy_range->type = State::CopyRange::MAP;
    copy_range->tileProperties.size = { -1, -1 };
    copy_range->tileProperties.interleaved = -1;
    copy_range->bufferSize = _mapSize.width() * _mapSize.height();
}

int MapWidget::getCursorPos() const noexcept
{
    return _cursorPos.y() * _mapSize.width() + _cursorPos.x();
}

std::unique_ptr<QImage> MapWidget::renderToQImage()
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return nullptr;
    }

    return std::make_unique<QImage>(DocumentImageRenderer::renderMap(*state, false));
}

void MapWidget::setActiveState(State* state)
{
    if (_activeState == state) {
        return;
    }

    _activeState = state;
    _atlasCache.markAllDirty();
    _commandMergeable = false;
    _altValue = -1;

    if (state != nullptr) {
        _tileSize = state->getTileProperties().size;
        _mapSize = state->getMapSize();
        _cursorPos = QPoint(
            std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
            std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1)));
        if (_selecting) {
            _selecting = false;
            _selectingSize = { 1, 1 };
        }
        updateGeometryMetrics();
    }

    update();
}

void MapWidget::onTilePropertiesUpdated()
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    _tileSize = state->getTileProperties().size;
    _atlasCache.markAllDirty();
    updateGeometryMetrics();
    update();
}

void MapWidget::onMapSizeUpdated()
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    _mapSize = state->getMapSize();
    _cursorPos = QPoint(
        std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
        std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1)));
    if (_selecting) {
        _selecting = false;
        _selectingSize = { 1, 1 };
    }
    updateGeometryMetrics();
    update();
}

void MapWidget::onMapContentUpdated()
{
    update();
}

void MapWidget::onCharacterModeChanged(State::CharacterMode mode)
{
    Q_UNUSED(mode)
    _atlasCache.markAllDirty();
    update();
}

void MapWidget::onMulticolorModeToggled(bool enabled)
{
    Q_UNUSED(enabled)
    _atlasCache.markAllDirty();
    update();
}

void MapWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
    _atlasCache.markAllDirty();
    update();
}

void MapWidget::onTileUpdated(int tile_index)
{
    _atlasCache.markTileDirty(tile_index);
    update();
}

void MapWidget::onBytesUpdated(int pos, int count)
{
    State* state = _activeState.data();
    if (state == nullptr || count <= 0) {
        return;
    }

    const int first_char = std::clamp(pos / 8, 0, 255);
    const int last_char = std::clamp((pos + count - 1) / 8, 0, 255);
    if ((last_char - first_char) >= 32) {
        _atlasCache.markAllDirty();
    } else {
        for (int char_index = first_char; char_index <= last_char; ++char_index) {
            _atlasCache.markTileDirty(state->getTileIndexFromCharIndex(char_index));
        }
    }
    update();
}

void MapWidget::onCharsetUpdated()
{
    _atlasCache.markAllDirty();
    update();
}

void MapWidget::onFileLoaded()
{
    _atlasCache.markAllDirty();
    onTilePropertiesUpdated();
    onMapSizeUpdated();
}

void MapWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle before applying zoom scaling
    // so zoomed-out views (_zoomLevel < 1.0) clear the entire viewport cleanly.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    _atlasCache.sync(*state);

    const QSize map_size = state->getMapSize();
    _mapSize = map_size;

    const State::TileProperties tile_properties = state->getTileProperties();
    _tileSize = tile_properties.size;
    const int tile_width = std::max(1, _tileSize.width());
    const int tile_height = std::max(1, _tileSize.height());
    const int tile_pixel_width = tile_width * 8;
    const int tile_pixel_height = tile_height * 8;

    painter.scale(_zoomLevel, _zoomLevel);
    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    // Compute visible logical coordinate bounds and clip both tile blits and grid lines.
    const QRectF exposed_logical = painter.transform().inverted().mapRect(QRectF(event->rect()));

    const int start_col = std::clamp(
        static_cast<int>(std::floor(exposed_logical.left() / static_cast<qreal>(tile_pixel_width))),
        0,
        map_size.width());
    const int end_col = std::clamp(
        static_cast<int>(std::ceil(exposed_logical.right() / static_cast<qreal>(tile_pixel_width))),
        0,
        map_size.width());
    const int start_row = std::clamp(
        static_cast<int>(std::floor(exposed_logical.top() / static_cast<qreal>(tile_pixel_height))),
        0,
        map_size.height());
    const int end_row = std::clamp(
        static_cast<int>(std::ceil(exposed_logical.bottom() / static_cast<qreal>(tile_pixel_height))),
        0,
        map_size.height());

    const QImage& atlas = _atlasCache.atlasImage();
    for (int row = start_row; row < end_row; ++row) {
        for (int col = start_col; col < end_col; ++col) {
            const int tile_index = state->getTileIndexFromMap(QPoint(col, row));
            const QRectF dest_rect(
                static_cast<qreal>(col * tile_pixel_width + kOffset),
                static_cast<qreal>(row * tile_pixel_height + kOffset),
                static_cast<qreal>(tile_pixel_width),
                static_cast<qreal>(tile_pixel_height));
            painter.drawImage(dest_rect, atlas, _atlasCache.sourceRectForTile(tile_index));
        }
    }

    if (_displayGrid) {
        QPen grid_pen(Preferences::getInstance().getGridColor());
        grid_pen.setStyle(Qt::DashLine);
        // Compensate for painter.scale(_zoomLevel, _zoomLevel) so grid lines remain 1 device pixel wide.
        grid_pen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(grid_pen);

        const qreal line_start_x = static_cast<qreal>(start_col * tile_pixel_width + kOffset);
        const qreal line_end_x = static_cast<qreal>(end_col * tile_pixel_width + kOffset);
        for (int row = start_row; row <= end_row; ++row) {
            const qreal line_y = static_cast<qreal>(row * tile_pixel_height + kOffset);
            painter.drawLine(QPointF(line_start_x, line_y), QPointF(line_end_x, line_y));
        }

        const qreal line_start_y = static_cast<qreal>(start_row * tile_pixel_height + kOffset);
        const qreal line_end_y = static_cast<qreal>(end_row * tile_pixel_height + kOffset);
        for (int col = start_col; col <= end_col; ++col) {
            const qreal line_x = static_cast<qreal>(col * tile_pixel_width + kOffset);
            painter.drawLine(QPointF(line_x, line_start_y), QPointF(line_x, line_end_y));
        }
    }

    QPen cursor_pen;
    cursor_pen.setColor({ 149, 195, 244, 255 });
    cursor_pen.setWidthF((hasFocus() ? 3.0 : 1.0) / _zoomLevel);
    cursor_pen.setStyle(Qt::SolidLine);
    painter.setPen(cursor_pen);

    if (_selecting) {
        // Shift origin +1 cell when dragging left/up so the box stays anchored on _cursorPos.
        const int cursor_adjust_x = (_selectingSize.width() < 0) ? 1 : 0;
        const int cursor_adjust_y = (_selectingSize.height() < 0) ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(QRectF(
            static_cast<qreal>((_cursorPos.x() + cursor_adjust_x) * tile_pixel_width + kOffset),
            static_cast<qreal>((_cursorPos.y() + cursor_adjust_y) * tile_pixel_height + kOffset),
            static_cast<qreal>((_selectingSize.width() - cursor_adjust_x) * tile_pixel_width),
            static_cast<qreal>((_selectingSize.height() - cursor_adjust_y) * tile_pixel_height)));
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(QRectF(
            static_cast<qreal>(_cursorPos.x() * tile_pixel_width + kOffset),
            static_cast<qreal>(_cursorPos.y() * tile_pixel_height + kOffset),
            static_cast<qreal>(tile_pixel_width),
            static_cast<qreal>(tile_pixel_height)));
    }
}

void MapWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());

    if ((event->button() == Qt::LeftButton && _mode == SELECT_MODE)
        || event->button() == Qt::RightButton) {
        if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
            _selecting = true;
            _selectingSize = ComputeDragSelectionSize(_cursorPos, cell, _mapSize);
        } else {
            if (_selecting) {
                _selecting = false;
                _selectingSize = { 1, 1 };
            }
            _cursorPos = cell;
            state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
        }
        update();
    } else if (event->button() == Qt::LeftButton && _mode == PAINT_MODE) {
        _cursorPos = cell;
        state->mapPaint(cell, state->getTileIndex(), _commandMergeable);
        _commandMergeable = true;
    } else if (event->button() == Qt::LeftButton && _mode == FILL_MODE) {
        _cursorPos = cell;
        state->mapFill(cell, state->getTileIndex());
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(MapCellStatusMessage(*state, cell));
        return;
    }

    if ((event->buttons() == Qt::LeftButton && _mode == SELECT_MODE)
        || event->buttons() == Qt::RightButton) {
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell, _mapSize);
        _selecting = true;
        update();
    } else if (event->buttons() == Qt::LeftButton && _mode == PAINT_MODE) {
        _cursorPos = cell;
        state->mapPaint(cell, state->getTileIndex(), _commandMergeable);
        _commandMergeable = true;
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    _commandMergeable = false;
}

void MapWidget::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const QPoint old_cursor_pos = _cursorPos;
    const bool old_selecting = _selecting;
    const QSize old_selecting_size = _selectingSize;
    bool space_pressed = false;
    bool is_typing = false;

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
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        state->mapPaint(_cursorPos, static_cast<int>(ResolveTypedMapScreenCode(*state, 0x20U)), false);
        cursor_delta = { -1, 0 };
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        cursor_delta = { -_cursorPos.x(), +1 };
        break;
    case Qt::Key_Space:
        space_pressed = true;
        if (_mode != SELECT_MODE) {
            break;
        }
        Q_FALLTHROUGH();
    default: {
        if (event->text().isEmpty()) {
            QWidget::keyPressEvent(event);
            return;
        }

        if ((event->modifiers() & Qt::AltModifier) != 0
            && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
            const int digit_value = event->key() - Qt::Key_0;
            _altValue = (_altValue == -1) ? digit_value : ((_altValue * 10 + digit_value) & 0xFFFF);
            break;
        }

        if ((event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) != 0) {
            QWidget::keyPressEvent(event);
            return;
        }

        const QByteArray latin1_bytes = event->text().toLatin1();
        if (latin1_bytes.isEmpty()) {
            return;
        }
        const quint8 ascii_code = static_cast<quint8>(static_cast<unsigned char>(latin1_bytes[0]));
        const quint8 screen_code = ResolveTypedMapScreenCode(*state, ascii_code);
        state->mapPaint(_cursorPos, static_cast<int>(screen_code), false);
        cursor_delta = { +1, 0 };
        is_typing = true;
        break;
    }
    }

    bool is_selecting = false;
    if (_mode == SELECT_MODE) {
        is_selecting = ((event->modifiers() & Qt::ShiftModifier) != 0) && !is_typing;
    }

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
            std::clamp(_selectingSize.width(), -_cursorPos.x(), _mapSize.width() - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), _mapSize.height() - _cursorPos.y()));
    } else {
        _cursorPos += cursor_delta;
        _cursorPos = QPoint(
            std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
            std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1)));

        MainWindow::getInstance()->showMessageOnStatusBar(MapCellStatusMessage(*state, _cursorPos));
    }
    _selecting = is_selecting;

    if (_mode == SELECT_MODE && _cursorPos != old_cursor_pos) {
        state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
    }

    if (_cursorPos != old_cursor_pos || _selecting != old_selecting || _selectingSize != old_selecting_size) {
        update();
    }

    if (_mode == PAINT_MODE && space_pressed) {
        state->mapPaint(_cursorPos, state->getTileIndex(), false);
    } else if (_mode == FILL_MODE && space_pressed) {
        state->mapFill(_cursorPos, state->getTileIndex());
    }
}

void MapWidget::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();

    if (_altValue != -1 && (event->modifiers() & Qt::AltModifier) == 0) {
        if (State* state = _activeState.data()) {
            state->mapPaint(_cursorPos, _altValue, false);
            _cursorPos += { +1, 0 };
            _cursorPos = QPoint(
                std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
                std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1)));
        }
        _altValue = -1;
    }
}

QSize MapWidget::sizeHint() const
{
    return _sizeHint;
}

void MapWidget::updateGeometryMetrics()
{
    const qreal width_px = static_cast<qreal>(_mapSize.width())
        * static_cast<qreal>(_tileSize.width())
        * _zoomLevel * 8.0;
    const qreal height_px = static_cast<qreal>(_mapSize.height())
        * static_cast<qreal>(_tileSize.height())
        * _zoomLevel * 8.0;

    _sizeHint = QSize(
        std::max(1, static_cast<int>(std::ceil(width_px))),
        std::max(1, static_cast<int>(std::ceil(height_px))));
    setMinimumSize(_sizeHint);
}

QPoint MapWidget::mapMousePositionToCell(const QPoint& pos) const noexcept
{
    const qreal safe_zoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const qreal tile_pixel_width = static_cast<qreal>(std::max(1, _tileSize.width())) * 8.0;
    const qreal tile_pixel_height = static_cast<qreal>(std::max(1, _tileSize.height())) * 8.0;

    const int raw_col = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x() - kOffset) / safe_zoom) / tile_pixel_width));
    const int raw_row = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y() - kOffset) / safe_zoom) / tile_pixel_height));

    return QPoint(
        std::clamp(raw_col, 0, std::max(0, _mapSize.width() - 1)),
        std::clamp(raw_row, 0, std::max(0, _mapSize.height() - 1)));
}
