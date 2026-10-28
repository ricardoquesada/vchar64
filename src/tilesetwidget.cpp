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
 * @file tilesetwidget.cpp
 * @brief Implementation of `TilesetWidget` dirty-atlas blitting, selection, and zoom geometry (`vchar64`).
 */

#include "tilesetwidget.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>

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

/**
 * @brief Status-bar text for a tileset cell, e.g. `"x: 1, y: 0 | Tile: 17 ($11)"`.
 *
 * Cells beyond the last valid tile (a partially filled final row) show coordinates only.
 */
[[nodiscard]] QString tileCellStatusMessage(const QPoint& cell, int tileIndex, int maxTiles)
{
    if (tileIndex < 0 || tileIndex >= maxTiles) {
        return TilesetWidget::tr("x: %1, y: %2").arg(cell.x()).arg(cell.y());
    }
    return TilesetWidget::tr("x: %1, y: %2 | Tile: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(tileIndex));
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

void TilesetWidget::updateGeometryMetrics()
{
    const qreal widthPx = static_cast<qreal>(_columns * 8 + kOffset * 2) * _zoomLevel;
    const qreal heightPx = static_cast<qreal>(_rows * 8 + kOffset * 2) * _zoomLevel;
    _sizeHint = QSize(
        std::max(1, static_cast<int>(std::ceil(widthPx))),
        std::max(1, static_cast<int>(std::ceil(heightPx))));
    setMinimumSize(_sizeHint);
}

QPoint TilesetWidget::mapMousePositionToCell(const QPoint& pos, int tw, int th) const noexcept
{
    const qreal safeZoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const qreal tileW = static_cast<qreal>(std::max(1, tw) * 8);
    const qreal tileH = static_cast<qreal>(std::max(1, th) * 8);

    const int rawX = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x()) / safeZoom - static_cast<qreal>(kOffset)) / tileW));
    const int rawY = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y()) / safeZoom - static_cast<qreal>(kOffset)) / tileH));

    return {
        std::clamp(rawX, 0, std::max(0, _tileColumns - 1)),
        std::clamp(rawY, 0, std::max(0, _tileRows - 1))
    };
}

void TilesetWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr || event->button() != Qt::LeftButton) {
        return;
    }

    const State::TileProperties tileProperties = state->getTileProperties();
    const int tw = std::max(1, tileProperties.size.width());
    const int th = std::max(1, tileProperties.size.height());

    const QPoint cell = mapMousePositionToCell(event->pos(), tw, th);
    int x = cell.x();
    int y = cell.y();

    const int tileIndex = x + y * _tileColumns;
    if (tileIndex < 0 || tileIndex >= _maxTiles) {
        return;
    }

    if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
        _selecting = true;

        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), _tileColumns - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), _tileRows - _cursorPos.y())
        };
        update();
    } else {
        const bool hadSelection = _selecting;
        _selecting = false;
        _selectingSize = { 1, 1 };

        if (_cursorPos != QPoint(x, y)) {
            _cursorPos = { x, y };
            state->setTileIndex(tileIndex);
            update();
        } else if (hadSelection) {
            update();
        }
    }
}

void TilesetWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const State::TileProperties tileProperties = state->getTileProperties();
    const int tw = std::max(1, tileProperties.size.width());
    const int th = std::max(1, tileProperties.size.height());

    const QPoint cell = mapMousePositionToCell(event->pos(), tw, th);
    int x = cell.x();
    int y = cell.y();

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(
            tileCellStatusMessage(cell, x + y * _tileColumns, _maxTiles));
    } else if (event->buttons() == Qt::LeftButton) {
        const int tileIndex = x + y * _tileColumns;
        if (tileIndex < 0 || tileIndex >= _maxTiles) {
            return;
        }

        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), _tileColumns - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), _tileRows - _cursorPos.y())
        };
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
            std::clamp(_selectingSize.width(), -_cursorPos.x(), _tileColumns - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), _tileRows - _cursorPos.y())
        };
    } else {
        QPoint pos = _cursorPos + point;
        pos = {
            std::clamp(pos.x(), 0, std::max(0, _tileColumns - 1)),
            std::clamp(pos.y(), 0, std::max(0, _tileRows - 1))
        };

        const int tileIdx = pos.y() * _tileColumns + pos.x();
        if (pos != _cursorPos && tileIdx >= 0 && tileIdx < _maxTiles) {
            _cursorPos = pos;
            state->setTileIndex(tileIdx);

            MainWindow::getInstance()->showMessageOnStatusBar(
                tileCellStatusMessage(_cursorPos, tileIdx, _maxTiles));
        }
    }

    _selecting = selecting;
    update();
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

    const State::TileProperties tileProperties = state->getTileProperties();
    const int tw = std::max(1, tileProperties.size.width());
    const int th = std::max(1, tileProperties.size.height());
    const int tilePixelW = tw * 8;
    const int tilePixelH = th * 8;
    const int maxTiles = 256 / (tw * th);

    const QImage& atlas = _atlasCache.atlasImage();
    for (int i = 0; i < maxTiles; ++i) {
        const int col = i % _tileColumns;
        const int row = i / _tileColumns;
        const QRectF dstRect(
            static_cast<qreal>(col * tilePixelW + kOffset),
            static_cast<qreal>(row * tilePixelH + kOffset),
            static_cast<qreal>(tilePixelW),
            static_cast<qreal>(tilePixelH));
        painter.drawImage(dstRect, atlas, QRectF(_atlasCache.sourceRectForTile(i)));
    }

    if (_displayGrid) {
        QPen gridPen = painter.pen();
        gridPen.setColor(Preferences::getInstance().getGridColor());
        gridPen.setStyle(Qt::DashLine);
        gridPen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(gridPen);

        for (int y = 0; y <= _tileRows; ++y) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(y * tilePixelH + kOffset)),
                QPointF(
                    static_cast<qreal>(_tileColumns * tilePixelW + kOffset),
                    static_cast<qreal>(y * tilePixelH + kOffset)));
        }

        for (int x = 0; x <= _tileColumns; ++x) {
            painter.drawLine(
                QPointF(static_cast<qreal>(x * tilePixelW + kOffset), static_cast<qreal>(kOffset)),
                QPointF(
                    static_cast<qreal>(x * tilePixelW + kOffset),
                    static_cast<qreal>(_tileRows * tilePixelH + kOffset)));
        }
    }

    painter.setPen(Qt::NoPen);

    paintFocus(painter);
    paintSelectedTile(painter);
}

std::unique_ptr<QImage> TilesetWidget::renderToQImage()
{
    const State* state = _activeState.data();
    if (state == nullptr) {
        return nullptr;
    }

    return std::make_unique<QImage>(DocumentImageRenderer::renderTileset(*state, false));
}

QSize TilesetWidget::sizeHint() const
{
    return _sizeHint;
}

void TilesetWidget::paintSelectedTile(QPainter& painter)
{
    const State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const State::TileProperties tileProperties = state->getTileProperties();
    const int tw = std::max(1, tileProperties.size.width());
    const int th = std::max(1, tileProperties.size.height());

    QPen pen;
    pen.setWidthF(2.0 / _zoomLevel);
    pen.setColor({ 149, 195, 244, 255 });
    pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(pen);

    if (_selecting) {
        const int plusOneX = _selectingSize.width() < 0 ? 1 : 0;
        const int plusOneY = _selectingSize.height() < 0 ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            (_cursorPos.x() + plusOneX) * 8 * tw + kOffset,
            (_cursorPos.y() + plusOneY) * 8 * th + kOffset,
            (_selectingSize.width() - plusOneX) * 8 * tw,
            (_selectingSize.height() - plusOneY) * 8 * th);
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(
            _cursorPos.x() * 8 * tw + kOffset,
            _cursorPos.y() * 8 * th + kOffset,
            8 * tw,
            8 * th);
    }
}

void TilesetWidget::paintFocus(QPainter& painter)
{
    if (!hasFocus()) {
        return;
    }

    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setWidthF(3.0 / _zoomLevel);
    pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(pen);

    painter.drawLine(kOffset - 1, kOffset - 1, kOffset - 1, _rows * 8 + kOffset);
    painter.drawLine(_columns * 8 + kOffset, kOffset - 1, _columns * 8 + kOffset, _rows * 8 + kOffset);
    painter.drawLine(kOffset - 1, kOffset - 1, _columns * 8 + 1, kOffset - 1);
    painter.drawLine(kOffset - 1, _rows * 8 + kOffset, _columns * 8 + kOffset, _rows * 8 + kOffset);
}

void TilesetWidget::onTileIndexUpdated(int selectedTileIndex)
{
    if (_selecting) {
        _selecting = false;
        _selectingSize = { 1, 1 };
    }

    const int safeCols = std::max(1, _tileColumns);
    const int x = selectedTileIndex % safeCols;
    const int y = selectedTileIndex / safeCols;
    if (_cursorPos != QPoint(x, y)) {
        _cursorPos = { x, y };
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
    const int tw = std::max(1, properties.size.width());
    const int th = std::max(1, properties.size.height());

    _columns = (kColumns / tw) * tw;
    _tileColumns = std::max(1, _columns / tw);

    _tileRows = std::max(
        1,
        static_cast<int>(std::ceil((256.0 / static_cast<double>(_columns)) / static_cast<double>(th))));
    _rows = _tileRows * th;

    _maxTiles = 256 / (tw * th);
    _cursorPos = {
        std::clamp(_cursorPos.x(), 0, _tileColumns - 1),
        std::clamp(_cursorPos.y(), 0, _tileRows - 1)
    };
    const int clampedTileIdx = _cursorPos.y() * _tileColumns + _cursorPos.x();
    if (clampedTileIdx >= _maxTiles && _maxTiles > 0) {
        const int validIdx = _maxTiles - 1;
        _cursorPos = { validIdx % _tileColumns, validIdx / _tileColumns };
    }

    _atlasCache.markAllDirty();
    updateGeometryMetrics();
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

void TilesetWidget::onTileUpdated(int tileIndex)
{
    _atlasCache.markTileDirty(tileIndex);
    update();
}

void TilesetWidget::onBytesUpdated(int pos, int count)
{
    const State* state = _activeState.data();
    if (state == nullptr || count <= 0) {
        return;
    }

    const int firstChar = std::clamp(pos / 8, 0, 255);
    const int lastChar = std::clamp((pos + count - 1) / 8, 0, 255);
    if ((lastChar - firstChar) >= 32) {
        _atlasCache.markAllDirty();
    } else {
        for (int c = firstChar; c <= lastChar; ++c) {
            _atlasCache.markTileDirty(state->getTileIndexFromCharIndex(c));
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

bool TilesetWidget::hasSelection() const noexcept
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void TilesetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange != nullptr);

    const State* state = _activeState.data();
    const State::TileProperties tileProperties = (state != nullptr)
        ? state->getTileProperties()
        : State::TileProperties { { 1, 1 }, 1 };

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

    copyRange->offset = fixedOrigin.y() * _tileColumns + fixedOrigin.x();
    copyRange->blockSize = fixedSize.width();
    copyRange->count = fixedSize.height();
    copyRange->skip = _tileColumns - fixedSize.width();
    copyRange->sourceRowWidth = _tileColumns;
    copyRange->type = State::CopyRange::TILES;
    copyRange->tileProperties = tileProperties;
    copyRange->bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
}

void TilesetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void TilesetWidget::setZoomLevel(int zoomLevel)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoomLevel)) * kBaseZoomLevel / 100.0;
    updateGeometryMetrics();
    update();
}
