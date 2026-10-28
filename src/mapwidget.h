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
 * @file mapwidget.h
 * @brief Viewport-clipped, `TileAtlasCache`-backed C64 tile map dock canvas (`vchar64`).
 */

#pragma once

#include <memory>

#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

#include "core/tileatlascache.h"
#include "state.h"

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

/**
 * @brief Viewport-clipped, dirty-atlas-backed C64 tile map editor widget.
 *
 * Binds exclusively to the active document via `ActiveDocumentRelay`, synchronizes
 * a private `TileAtlasCache` in `O(dirty_tiles)`, and restricts `paintEvent()` tile
 * blits and grid lines to the exposed `QScrollArea` viewport (`O(visible_cells)` even
 * on `4096 x 4096` maps).
 */
class MapWidget : public QWidget {
    Q_OBJECT

public:
    /** @brief Interactive mouse tool active on the map canvas. */
    enum MapMode {
        PAINT_MODE,
        SELECT_MODE,
        FILL_MODE,
    };

    explicit MapWidget(QWidget* parent = nullptr);

    void enableGrid(bool enabled);
    void setMode(MapMode mode);
    [[nodiscard]] MapMode getMode() const noexcept { return _mode; }
    void setZoomLevel(int zoomLevel);

    /** @brief Populates `copyRange` (`CopyRange::MAP`) for the current cursor or rectangular selection. */
    void getSelectionRange(State::CopyRange* copyRange) const;
    /** @brief Returns the linear row-major map cell index (`y * mapWidth + x`) at the top-left of the selection. */
    [[nodiscard]] int getCursorPos() const noexcept;
    [[nodiscard]] QPoint getCursorCoord() const noexcept { return _cursorPos; }

    /** @brief Renders the full active map at 1:1 pixel scale via `DocumentImageRenderer::renderMap()`. */
    [[nodiscard]] std::unique_ptr<QImage> renderToQImage();

public slots:
    /** @brief Switches the observed `State` document (or `nullptr`) and invalidates `_atlasCache`. */
    void setActiveState(State* state);
    void onTilePropertiesUpdated();
    void onMapSizeUpdated();
    void onMapContentUpdated();
    void onMulticolorModeToggled(bool enabled);
    void onColorPropertiesUpdated(int pen);
    void onTileUpdated(int tileIndex);
    void onBytesUpdated(int pos, int count);
    void onCharsetUpdated();
    void onFileLoaded();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    [[nodiscard]] QSize sizeHint() const override;

private:
    void updateGeometryMetrics();
    [[nodiscard]] QPoint mapMousePositionToCell(const QPoint& pos) const noexcept;

    QPointer<State> _activeState;
    TileAtlasCache _atlasCache;

    bool _displayGrid = false;
    QSize _sizeHint { 640, 400 };
    bool _selecting = false;
    QSize _selectingSize { 1, 1 };
    QPoint _cursorPos { 0, 0 };
    QSize _mapSize { 40, 25 };
    QSize _tileSize { 1, 1 };
    MapMode _mode = SELECT_MODE;
    bool _commandMergeable = false;
    qreal _zoomLevel = 2.0;
    int _altValue = -1;
};
