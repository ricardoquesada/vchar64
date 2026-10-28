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

/**
 * @file mapwidget.cpp
 * @brief Implementation of `MapWidget` viewport-clipped atlas rendering and brush/fill/select tools (`vchar64`).
 */

#include "mapwidget.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QImage>
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
#include "utils.h"

namespace {

constexpr qreal kBaseZoomLevel = 2.0;
constexpr int kOffset = 0;

/** @brief Status-bar text for a map cell, e.g. `"x: 12, y: 5 | Tile: 17 ($11)"`. */
[[nodiscard]] QString mapCellStatusMessage(const State& state, const QPoint& cell)
{
    return MapWidget::tr("x: %1, y: %2 | Tile: %3")
        .arg(cell.x())
        .arg(cell.y())
        .arg(vchar64::formatIndexDecHex(state.getTileIndexFromMap(cell)));
}

} // namespace

MapWidget::MapWidget(QWidget* parent)
    : QWidget(parent)
{
    updateGeometryMetrics();
    setMouseTracking(true);
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
        _cursorPos = {
            std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
            std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1))
        };
        if (_selecting) {
            _selecting = false;
            _selectingSize = { 1, 1 };
        }
        updateGeometryMetrics();
    }

    update();
}

void MapWidget::updateGeometryMetrics()
{
    const qreal widthPx = static_cast<qreal>(_mapSize.width())
        * static_cast<qreal>(_tileSize.width())
        * _zoomLevel * 8.0;
    const qreal heightPx = static_cast<qreal>(_mapSize.height())
        * static_cast<qreal>(_tileSize.height())
        * _zoomLevel * 8.0;

    _sizeHint = QSize(
        std::max(1, static_cast<int>(std::ceil(widthPx))),
        std::max(1, static_cast<int>(std::ceil(heightPx))));
    setMinimumSize(_sizeHint);
}

QPoint MapWidget::mapMousePositionToCell(const QPoint& pos) const noexcept
{
    const qreal safeZoom = (_zoomLevel > 0.0) ? _zoomLevel : 1.0;
    const qreal tileW = static_cast<qreal>(std::max(1, _tileSize.width())) * 8.0;
    const qreal tileH = static_cast<qreal>(std::max(1, _tileSize.height())) * 8.0;

    const int rawX = static_cast<int>(
        std::floor((static_cast<qreal>(pos.x() - kOffset) / safeZoom) / tileW));
    const int rawY = static_cast<int>(
        std::floor((static_cast<qreal>(pos.y() - kOffset) / safeZoom) / tileH));

    return QPoint(
        std::clamp(rawX, 0, std::max(0, _mapSize.width() - 1)),
        std::clamp(rawY, 0, std::max(0, _mapSize.height() - 1)));
}

void MapWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle BEFORE applying zoom scaling
    // so zoomed-out views (_zoomLevel < 1.0) clear the entire viewport cleanly.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    _atlasCache.sync(*state);

    const QSize mapSize = state->getMapSize();
    _mapSize = mapSize;

    const State::TileProperties tileProperties = state->getTileProperties();
    _tileSize = tileProperties.size;
    const int tw = std::max(1, _tileSize.width());
    const int th = std::max(1, _tileSize.height());
    const int tilePixelW = tw * 8;
    const int tilePixelH = th * 8;

    painter.scale(_zoomLevel, _zoomLevel);
    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    // Compute visible logical coordinate bounds and clip both tile blits and grid lines.
    const QRectF exposedLogical = painter.transform().inverted().mapRect(QRectF(event->rect()));

    const int startX = std::clamp(
        static_cast<int>(std::floor(exposedLogical.left() / static_cast<qreal>(tilePixelW))),
        0,
        mapSize.width());
    const int endX = std::clamp(
        static_cast<int>(std::ceil(exposedLogical.right() / static_cast<qreal>(tilePixelW))),
        0,
        mapSize.width());
    const int startY = std::clamp(
        static_cast<int>(std::floor(exposedLogical.top() / static_cast<qreal>(tilePixelH))),
        0,
        mapSize.height());
    const int endY = std::clamp(
        static_cast<int>(std::ceil(exposedLogical.bottom() / static_cast<qreal>(tilePixelH))),
        0,
        mapSize.height());

    const QImage& atlas = _atlasCache.atlasImage();
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            const int tileIdx = state->getTileIndexFromMap(QPoint(x, y));
            const QRectF dstRect(
                static_cast<qreal>(x * tilePixelW + kOffset),
                static_cast<qreal>(y * tilePixelH + kOffset),
                static_cast<qreal>(tilePixelW),
                static_cast<qreal>(tilePixelH));
            painter.drawImage(dstRect, atlas, _atlasCache.sourceRectForTile(tileIdx));
        }
    }

    if (_displayGrid) {
        QPen gridPen(Preferences::getInstance().getGridColor());
        gridPen.setStyle(Qt::DashLine);
        // Compensate for `painter.scale(_zoomLevel, _zoomLevel)` so grid lines remain 1 device pixel wide.
        gridPen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(gridPen);

        const qreal lineStartX = static_cast<qreal>(startX * tilePixelW + kOffset);
        const qreal lineEndX = static_cast<qreal>(endX * tilePixelW + kOffset);
        for (int y = startY; y <= endY; ++y) {
            const qreal lineY = static_cast<qreal>(y * tilePixelH + kOffset);
            painter.drawLine(QPointF(lineStartX, lineY), QPointF(lineEndX, lineY));
        }

        const qreal lineStartY = static_cast<qreal>(startY * tilePixelH + kOffset);
        const qreal lineEndY = static_cast<qreal>(endY * tilePixelH + kOffset);
        for (int x = startX; x <= endX; ++x) {
            const qreal lineX = static_cast<qreal>(x * tilePixelW + kOffset);
            painter.drawLine(QPointF(lineX, lineStartY), QPointF(lineX, lineEndY));
        }
    }

    QPen cursorPen;
    cursorPen.setColor({ 149, 195, 244, 255 });
    cursorPen.setWidthF((hasFocus() ? 3.0 : 1.0) / _zoomLevel);
    cursorPen.setStyle(Qt::SolidLine);
    painter.setPen(cursorPen);

    if (_selecting) {
        const int plusOneX = (_selectingSize.width() < 0) ? 1 : 0;
        const int plusOneY = (_selectingSize.height() < 0) ? 1 : 0;

        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(QRectF(
            static_cast<qreal>((_cursorPos.x() + plusOneX) * tilePixelW + kOffset),
            static_cast<qreal>((_cursorPos.y() + plusOneY) * tilePixelH + kOffset),
            static_cast<qreal>((_selectingSize.width() - plusOneX) * tilePixelW),
            static_cast<qreal>((_selectingSize.height() - plusOneY) * tilePixelH)));
    } else {
        painter.setBrush(QColor(128, 0, 0, 0));
        painter.drawRect(QRectF(
            static_cast<qreal>(_cursorPos.x() * tilePixelW + kOffset),
            static_cast<qreal>(_cursorPos.y() * tilePixelH + kOffset),
            static_cast<qreal>(tilePixelW),
            static_cast<qreal>(tilePixelH)));
    }
}

std::unique_ptr<QImage> MapWidget::renderToQImage()
{
    State* state = _activeState.data();
    if (state == nullptr) {
        return nullptr;
    }

    return std::make_unique<QImage>(DocumentImageRenderer::renderMap(*state, false));
}

void MapWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    State* state = _activeState.data();
    if (state == nullptr) {
        return;
    }

    const QPoint cell = mapMousePositionToCell(event->pos());
    int x = cell.x();
    int y = cell.y();

    if ((event->button() == Qt::LeftButton && _mode == SELECT_MODE)
        || event->button() == Qt::RightButton) {
        if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
            _selecting = true;
            if (x >= _cursorPos.x()) {
                ++x;
            }
            if (y >= _cursorPos.y()) {
                ++y;
            }

            _selectingSize = {
                std::clamp(x - _cursorPos.x(), -_cursorPos.x(), _mapSize.width() - _cursorPos.x()),
                std::clamp(y - _cursorPos.y(), -_cursorPos.y(), _mapSize.height() - _cursorPos.y())
            };
        } else {
            if (_selecting) {
                _selecting = false;
                _selectingSize = { 1, 1 };
            }
            _cursorPos = { x, y };
            state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
        }
        update();
    } else if (event->button() == Qt::LeftButton && _mode == PAINT_MODE) {
        _cursorPos = { x, y };
        state->mapPaint(QPoint(x, y), state->getTileIndex(), _commandMergeable);
        _commandMergeable = true;
    } else if (event->button() == Qt::LeftButton && _mode == FILL_MODE) {
        _cursorPos = { x, y };
        state->mapFill(QPoint(x, y), state->getTileIndex());
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
    int x = cell.x();
    int y = cell.y();

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(mapCellStatusMessage(*state, cell));
    } else if ((event->buttons() == Qt::LeftButton && _mode == SELECT_MODE)
        || event->buttons() == Qt::RightButton) {
        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), _mapSize.width() - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), _mapSize.height() - _cursorPos.y())
        };
        _selecting = true;
        update();
    } else if (event->buttons() == Qt::LeftButton && _mode == PAINT_MODE) {
        _cursorPos = { x, y };
        state->mapPaint(QPoint(x, y), state->getTileIndex(), _commandMergeable);
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

    const QPoint oldCursorPos = _cursorPos;
    const bool oldSelecting = _selecting;
    const QSize oldSelectingSize = _selectingSize;
    bool spacePressed = false;
    bool typing = false;

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
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        state->mapPaint(_cursorPos, utilsAsciiToScreenCode(state, 0x20U), false);
        point = { -1, 0 };
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        point = { -_cursorPos.x(), +1 };
        break;
    case Qt::Key_Space:
        spacePressed = true;
        if (_mode != SELECT_MODE) {
            break;
        }
        Q_FALLTHROUGH();
    default:
        if (event->text().isEmpty()) {
            QWidget::keyPressEvent(event);
            return;
        }

        if ((event->modifiers() & Qt::AltModifier) != 0
            && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
            const int keyValue = event->key() - Qt::Key_0;
            _altValue = (_altValue == -1) ? keyValue : ((_altValue * 10 + keyValue) & 0xFFFF);
        } else if ((event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) != 0) {
            QWidget::keyPressEvent(event);
            return;
        } else {
            const QByteArray latin1 = event->text().toLatin1();
            if (latin1.isEmpty()) {
                return;
            }
            const auto asciiCode = static_cast<quint8>(static_cast<unsigned char>(latin1[0]));
            const quint8 code = utilsAsciiToScreenCode(state, asciiCode);
            state->mapPaint(_cursorPos, static_cast<int>(code), false);
            point = { +1, 0 };
            typing = true;
        }
        break;
    }

    bool selecting = false;
    if (_mode == SELECT_MODE) {
        selecting = ((event->modifiers() & Qt::ShiftModifier) != 0) && !typing;
    }

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
            std::clamp(_selectingSize.width(), -_cursorPos.x(), _mapSize.width() - _cursorPos.x()),
            std::clamp(_selectingSize.height(), -_cursorPos.y(), _mapSize.height() - _cursorPos.y())
        };
    } else {
        _cursorPos += point;
        _cursorPos = {
            std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
            std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1))
        };

        MainWindow::getInstance()->showMessageOnStatusBar(mapCellStatusMessage(*state, _cursorPos));
    }
    _selecting = selecting;

    if (_mode == SELECT_MODE && _cursorPos != oldCursorPos) {
        state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
    }

    if (_cursorPos != oldCursorPos || _selecting != oldSelecting || _selectingSize != oldSelectingSize) {
        update();
    }

    if (_mode == PAINT_MODE && spacePressed) {
        state->mapPaint(_cursorPos, state->getTileIndex(), false);
    } else if (_mode == FILL_MODE && spacePressed) {
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
            _cursorPos = {
                std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
                std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1))
            };
        }
        _altValue = -1;
    }
}

QSize MapWidget::sizeHint() const
{
    return _sizeHint;
}

void MapWidget::getSelectionRange(State::CopyRange* copyRange) const
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

    copyRange->offset = fixedOrigin.y() * _mapSize.width() + fixedOrigin.x();
    copyRange->blockSize = fixedSize.width();
    copyRange->count = fixedSize.height();
    copyRange->skip = _mapSize.width() - fixedSize.width();
    copyRange->sourceRowWidth = _mapSize.width();
    copyRange->type = State::CopyRange::MAP;
    copyRange->tileProperties.size = { -1, -1 };
    copyRange->tileProperties.interleaved = -1;
    copyRange->bufferSize = _mapSize.width() * _mapSize.height();
}

int MapWidget::getCursorPos() const noexcept
{
    return _cursorPos.y() * _mapSize.width() + _cursorPos.x();
}

void MapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void MapWidget::setZoomLevel(int zoomLevel)
{
    _zoomLevel = static_cast<qreal>(std::max(1, zoomLevel)) * kBaseZoomLevel / 100.0;
    updateGeometryMetrics();
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
    _cursorPos = {
        std::clamp(_cursorPos.x(), 0, std::max(0, _mapSize.width() - 1)),
        std::clamp(_cursorPos.y(), 0, std::max(0, _mapSize.height() - 1))
    };
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

void MapWidget::onTileUpdated(int tileIndex)
{
    _atlasCache.markTileDirty(tileIndex);
    update();
}

void MapWidget::onBytesUpdated(int pos, int count)
{
    State* state = _activeState.data();
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

void MapWidget::onCharsetUpdated()
{
    _atlasCache.markAllDirty();
    update();
}

void MapWidget::setMode(MapMode mode)
{
    if (_mode != mode) {
        _mode = mode;
    }
}

void MapWidget::onFileLoaded()
{
    _atlasCache.markAllDirty();
    onTilePropertiesUpdated();
    onMapSizeUpdated();
}
