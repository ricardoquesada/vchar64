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

// TilesetWidget implementation for dirty-atlas blitting, selection, and zoom geometry.

#include "tilesetwidget.h"

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
#include <QRectF>
#include <QString>

#include "core/documentimagerenderer.h"
#include "core/indexformat.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

constexpr int kColumns = 32;
constexpr int kRows = 8;
constexpr int kOffset = 2;
constexpr qreal kBaseZoomLevel = 2.0;

// Formats status-bar text for a tileset cell, e.g. "x: 1, y: 0 | Tile: 17 ($11)".
// Cells beyond the last valid tile (a partially filled final row) show coordinates only.
[[nodiscard]] QString TileCellStatusMessage(const QPoint& cell, int tile_index, int max_tiles)
{
    if (tile_index < 0 || tile_index >= max_tiles) {
        return TilesetWidget::tr("x: %1, y: %2").arg(cell.x()).arg(cell.y());
    }
    return TilesetWidget::tr("x: %1, y: %2 | Tile: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(tile_index));
}

// Computes a signed rectangular selection size spanning from `cursor_pos` to `target_cell`,
// inclusive of `cursor_pos`, clamped within the `tile_columns x tile_rows` tileset grid.
[[nodiscard]] QSize ComputeDragSelectionSize(
    const QPoint& cursor_pos,
    const QPoint& target_cell,
    int tile_columns,
    int tile_rows) noexcept
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
        std::clamp(end_col - cursor_pos.x(), -cursor_pos.x(), tile_columns - cursor_pos.x()),
        std::clamp(end_row - cursor_pos.y(), -cursor_pos.y(), tile_rows - cursor_pos.y()));
}

} // namespace

TilesetWidget::TilesetWidget(QWidget* parent)
    : QWidget(parent)
    , _columns(kColumns)
    , _tileColumns(kColumns)
    , _rows(kRows)
    , _tileRows(kRows)
    , _zoomLevel(kBaseZoomLevel)
{
    updateGeometryMetrics();
    setMouseTracking(true);
}

bool TilesetWidget::hasSelection() const noexcept
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void TilesetWidget::getSelectionRange(State::CopyRange* copy_range) const
{
    Q_ASSERT(copy_range != nullptr);

    const State* state = _activeState.data();
    const State::TileProperties tile_properties = (state != nullptr)
        ? state->getTileProperties()
        : State::TileProperties { { 1, 1 }, 1 };

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

    copy_range->offset = normalized_origin.y() * _tileColumns + normalized_origin.x();
    copy_range->blockSize = normalized_size.width();
    copy_range->count = normalized_size.height();
    copy_range->skip = _tileColumns - normalized_size.width();
    copy_range->sourceRowWidth = _tileColumns;
    copy_range->type = State::CopyRange::TILES;
    copy_range->tileProperties = tile_properties;
    copy_range->bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
}

void TilesetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void TilesetWidget::setZoomLevel(int zoom_level)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoom_level)) * kBaseZoomLevel / 100.0;
    updateGeometryMetrics();
    update();
}

std::unique_ptr<QImage> TilesetWidget::renderToQImage()
{
    const State* state = _activeState.data();
    if (state == nullptr) {
        return nullptr;
    }

    return std::make_unique<QImage>(DocumentImageRenderer::renderTileset(*state, false));
}

void TilesetWidget::setActiveState(State* state)
{
    if (_activeState == state) {
        return;
    }

    _activeState = state;
    _atlasCache.markAllDirty();
    _selecting = false;
    _selectingSize = { 1, 1 };

    if (state != nullptr) {
        onTilePropertiesUpdated();
        onTileIndexUpdated(state->getTileIndex());
    }

    update();
}

void TilesetWidget::onTileIndexUpdated(int selected_tile_index)
{
    if (_selecting) {
        _selecting = false;
        _selectingSize = { 1, 1 };
    }

    const int safe_columns = std::max(1, _tileColumns);
    const int col = selected_tile_index % safe_columns;
    const int row = selected_tile_index / safe_columns;
    if (_cursorPos != QPoint(col, row)) {
        _cursorPos = QPoint(col, row);
        update();
    }
}

void TilesetWidget::onTilePropertiesUpdated()
{
    const State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const State::TileProperties properties = state->getTileProperties();
    const int tile_width = std::max(1, properties.size.width());
    const int tile_height = std::max(1, properties.size.height());

    _columns = (kColumns / tile_width) * tile_width;
    _tileColumns = std::max(1, _columns / tile_width);

    _tileRows = std::max(
        1,
        static_cast<int>(std::ceil((256.0 / static_cast<double>(_columns)) / static_cast<double>(tile_height))));
    _rows = _tileRows * tile_height;

    _maxTiles = 256 / (tile_width * tile_height);
    _cursorPos = QPoint(
        std::clamp(_cursorPos.x(), 0, _tileColumns - 1),
        std::clamp(_cursorPos.y(), 0, _tileRows - 1));
    const int clamped_tile_index = _cursorPos.y() * _tileColumns + _cursorPos.x();
    if (clamped_tile_index >= _maxTiles && _maxTiles > 0) {
        const int valid_tile_index = _maxTiles - 1;
        _cursorPos = QPoint(valid_tile_index % _tileColumns, valid_tile_index / _tileColumns);
    }

    _atlasCache.markAllDirty();
    updateGeometryMetrics();
    update();
}

void TilesetWidget::onCharacterModeChanged(State::CharacterMode mode)
{
    Q_UNUSED(mode)
    _atlasCache.markAllDirty();
    update();
}

void TilesetWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state)
    _atlasCache.markAllDirty();
    update();
}

void TilesetWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
    _atlasCache.markAllDirty();
    update();
}

void TilesetWidget::onTileUpdated(int tile_index)
{
    _atlasCache.markTileDirty(tile_index);
    update();
}

void TilesetWidget::onBytesUpdated(int pos, int count)
{
    const State* state = _activeState.data();
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

void TilesetWidget::onCharsetUpdated()
{
    _atlasCache.markAllDirty();
    update();
}

void TilesetWidget::onFileLoaded()
{
    _atlasCache.markAllDirty();
    onTilePropertiesUpdated();
}

void TilesetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Clear the exposed device rectangle before zoom scaling so zoomed-out views
    // and empty MDI states clear cleanly.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    _atlasCache.sync(*state);

    painter.scale(_zoomLevel, _zoomLevel);
    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    const State::TileProperties tile_properties = state->getTileProperties();
    const int tile_width = std::max(1, tile_properties.size.width());
    const int tile_height = std::max(1, tile_properties.size.height());
    const int tile_pixel_width = tile_width * 8;
    const int tile_pixel_height = tile_height * 8;
    const int max_tiles = 256 / (tile_width * tile_height);

    const QImage& atlas = _atlasCache.atlasImage();
    for (int tile_index = 0; tile_index < max_tiles; ++tile_index) {
        const int col = tile_index % _tileColumns;
        const int row = tile_index / _tileColumns;
        const QRectF dest_rect(
            static_cast<qreal>(col * tile_pixel_width + kOffset),
            static_cast<qreal>(row * tile_pixel_height + kOffset),
            static_cast<qreal>(tile_pixel_width),
            static_cast<qreal>(tile_pixel_height));
        painter.drawImage(dest_rect, atlas, QRectF(_atlasCache.sourceRectForTile(tile_index)));
    }

    if (_displayGrid) {
        QPen grid_pen = painter.pen();
        grid_pen.setColor(Preferences::getInstance().getGridColor());
        grid_pen.setStyle(Qt::DashLine);
        grid_pen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(grid_pen);

        for (int row = 0; row <= _tileRows; ++row) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(row * tile_pixel_height + kOffset)),
                QPointF(
                    static_cast<qreal>(_tileColumns * tile_pixel_width + kOffset),
                    static_cast<qreal>(row * tile_pixel_height + kOffset)));
        }

        for (int col = 0; col <= _tileColumns; ++col) {
            painter.drawLine(
                QPointF(static_cast<qreal>(col * tile_pixel_width + kOffset), static_cast<qreal>(kOffset)),
                QPointF(
                    static_cast<qreal>(col * tile_pixel_width + kOffset),
                    static_cast<qreal>(_tileRows * tile_pixel_height + kOffset)));
        }
    }

    painter.setPen(Qt::NoPen);

    paintFocus(painter);
    paintSelectedTile(painter);
}

void TilesetWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr || event->button() != Qt::LeftButton) {
        return;
    }

    const State::TileProperties tile_properties = state->getTileProperties();
    const int tile_width = std::max(1, tile_properties.size.width());
    const int tile_height = std::max(1, tile_properties.size.height());

    const QPoint cell = mapMousePositionToCell(event->pos(), tile_width, tile_height);
    const int tile_index = cell.x() + cell.y() * _tileColumns;
    if (tile_index < 0 || tile_index >= _maxTiles) {
        return;
    }

    if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
        _selecting = true;
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell, _tileColumns, _tileRows);
        update();
        return;
    }

    const bool had_selection = _selecting;
    _selecting = false;
    _selectingSize = { 1, 1 };

    if (_cursorPos != cell) {
        _cursorPos = cell;
        state->setTileIndex(tile_index);
        update();
    } else if (had_selection) {
        update();
    }
}

void TilesetWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const State::TileProperties tile_properties = state->getTileProperties();
    const int tile_width = std::max(1, tile_properties.size.width());
    const int tile_height = std::max(1, tile_properties.size.height());

    const QPoint cell = mapMousePositionToCell(event->pos(), tile_width, tile_height);
    const int tile_index = cell.x() + cell.y() * _tileColumns;

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(
            TileCellStatusMessage(cell, tile_index, _maxTiles));
        return;
    }

    if (event->buttons() == Qt::LeftButton) {
        if (tile_index < 0 || tile_index >= _maxTiles) {
            return;
        }
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell, _tileColumns, _tileRows);
        _selecting = true;
        update();
    }
}

void TilesetWidget::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
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
            std::clamp(_selectingSize.width(), -_cursorPos.x(), _tileColumns - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), _tileRows - _cursorPos.y()));
    } else {
        QPoint next_cursor_pos = _cursorPos + cursor_delta;
        next_cursor_pos = QPoint(
            std::clamp(next_cursor_pos.x(), 0, std::max(0, _tileColumns - 1)),
            std::clamp(next_cursor_pos.y(), 0, std::max(0, _tileRows - 1)));

        const int tile_index = next_cursor_pos.y() * _tileColumns + next_cursor_pos.x();
        if (next_cursor_pos != _cursorPos && tile_index >= 0 && tile_index < _maxTiles) {
            _cursorPos = next_cursor_pos;
            state->setTileIndex(tile_index);

            MainWindow::getInstance()->showMessageOnStatusBar(
                TileCellStatusMessage(_cursorPos, tile_index, _maxTiles));
        }
    }

    _selecting = is_selecting;
    update();
}

QSize TilesetWidget::sizeHint() const
{
    return _sizeHint;
}

void TilesetWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen focus_pen;
    focus_pen.setColor({ 149, 195, 244, 255 });
    focus_pen.setWidthF(3.0 / _zoomLevel);
    focus_pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(focus_pen);

    painter.drawLine(kOffset - 1, kOffset - 1, kOffset - 1, _rows * 8 + kOffset);
    painter.drawLine(_columns * 8 + kOffset, kOffset - 1, _columns * 8 + kOffset, _rows * 8 + kOffset);
    painter.drawLine(kOffset - 1, kOffset - 1, _columns * 8 + 1, kOffset - 1);
    painter.drawLine(kOffset - 1, _rows * 8 + kOffset, _columns * 8 + kOffset, _rows * 8 + kOffset);
}

void TilesetWidget::paintSelectedTile(QPainter& painter)
{
    const State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const State::TileProperties tile_properties = state->getTileProperties();
    const int tile_width = std::max(1, tile_properties.size.width());
    const int tile_height = std::max(1, tile_properties.size.height());

    QPen selection_pen;
    selection_pen.setWidthF(2.0 / _zoomLevel);
    selection_pen.setColor({ 149, 195, 244, 255 });
    selection_pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(selection_pen);

    if (_selecting) {
        // Shift origin +1 tile when dragging left/up so the box stays anchored on _cursorPos.
        const int cursor_adjust_x = _selectingSize.width() < 0 ? 1 : 0;
        const int cursor_adjust_y = _selectingSize.height() < 0 ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            (_cursorPos.x() + cursor_adjust_x) * 8 * tile_width + kOffset,
            (_cursorPos.y() + cursor_adjust_y) * 8 * tile_height + kOffset,
            (_selectingSize.width() - cursor_adjust_x) * 8 * tile_width,
            (_selectingSize.height() - cursor_adjust_y) * 8 * tile_height);
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(
            _cursorPos.x() * 8 * tile_width + kOffset,
            _cursorPos.y() * 8 * tile_height + kOffset,
            8 * tile_width,
            8 * tile_height);
    }
}

void TilesetWidget::updateGeometryMetrics()
{
    const qreal width_px = static_cast<qreal>(_columns * 8 + kOffset * 2) * _zoomLevel;
    const qreal height_px = static_cast<qreal>(_rows * 8 + kOffset * 2) * _zoomLevel;
    _sizeHint = QSize(
        std::max(1, static_cast<int>(std::ceil(width_px))),
        std::max(1, static_cast<int>(std::ceil(height_px))));
    setMinimumSize(_sizeHint);
}

QPoint TilesetWidget::mapMousePositionToCell(const QPoint& pos, int tile_width, int tile_height) const noexcept
{
    const qreal safe_zoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const qreal tile_pixel_width = static_cast<qreal>(std::max(1, tile_width) * 8);
    const qreal tile_pixel_height = static_cast<qreal>(std::max(1, tile_height) * 8);

    const int raw_col = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x()) / safe_zoom - static_cast<qreal>(kOffset)) / tile_pixel_width));
    const int raw_row = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y()) / safe_zoom - static_cast<qreal>(kOffset)) / tile_pixel_height));

    return QPoint(
        std::clamp(raw_col, 0, std::max(0, _tileColumns - 1)),
        std::clamp(raw_row, 0, std::max(0, _tileRows - 1)));
}
