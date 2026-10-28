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
 * @file tilesetwidget.h
 * @brief `TileAtlasCache`-backed tileset picker and rectangular selection dock widget (`vchar64`).
 */

#pragma once

#include <memory>

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

#include "core/tileatlascache.h"
#include "state.h"

/**
 * @brief Displays all addressable tiles (`1..256`) of the active document in a zoomable grid.
 *
 * Uses `TileAtlasCache` to incrementally rasterize modified tiles into a `32 x 8` character
 * `QImage` sheet and blits visible tiles in `paintEvent()` rather than invoking per-pixel
 * `QPainter` draws on every expose event.
 */
class TilesetWidget : public QWidget {
    Q_OBJECT

public:
    explicit TilesetWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool hasSelection() const noexcept;
    /** @brief Populates `copyRange` (`CopyRange::TILES`) for the active tile or Shift-drag selection. */
    void getSelectionRange(State::CopyRange* copyRange) const;
    void enableGrid(bool enabled);
    void setZoomLevel(int zoomLevel);
    /** @brief Renders the full tileset sheet at 1:1 pixel scale via `DocumentImageRenderer::renderTileset()`. */
    [[nodiscard]] std::unique_ptr<QImage> renderToQImage();

public slots:
    /** @brief Rebinds the dock to `state` (or `nullptr`) and invalidates `_atlasCache`. */
    void setActiveState(State* state);
    void onTileIndexUpdated(int selectedTileIndex);
    void onTilePropertiesUpdated();
    void onMulticolorModeToggled(bool state);
    void onColorPropertiesUpdated(int pen);
    void onTileUpdated(int tileIndex);
    void onBytesUpdated(int pos, int count);
    void onCharsetUpdated();
    void onFileLoaded();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    [[nodiscard]] QSize sizeHint() const override;

    void paintFocus(QPainter& painter);
    void paintSelectedTile(QPainter& painter);

private:
    void updateGeometryMetrics();
    [[nodiscard]] QPoint mapMousePositionToCell(const QPoint& pos, int tw, int th) const noexcept;

    QPointer<State> _activeState;
    TileAtlasCache _atlasCache;

    QPoint _cursorPos { 0, 0 };
    bool _selecting = false;
    QSize _selectingSize { 1, 1 };
    int _columns = 32;
    int _tileColumns = 32;
    int _rows = 8;
    int _tileRows = 8;
    int _maxTiles = 256;

    QSize _sizeHint { 0, 0 };
    qreal _zoomLevel = 2.0;
    bool _displayGrid = false;
};
