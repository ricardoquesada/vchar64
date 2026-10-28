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
 * @file bigcharwidget.h
 * @brief Per-document MDI subwindow magnified tile pixel editor (`vchar64` GUI target).
 */

#pragma once

#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

#include "state.h"

/**
 * @brief Per-subwindow MDI tile pixel editor view.
 *
 * Holds a non-owning reference to its document `State` (owned via RAII inside
 * `MainWindow::DocumentSession`). Because each `BigCharWidget` binds directly
 * to its own `State`, side-by-side tiled MDI subwindows render their own
 * respective projects simultaneously.
 */
class BigCharWidget : public QWidget {
    Q_OBJECT

public:
    explicit BigCharWidget(State* state, QWidget* parent = nullptr);
    ~BigCharWidget() override;

    /** @brief Returns the tile index (`0 .. maxTiles() - 1`) currently magnified in this subwindow. */
    [[nodiscard]] int getTileIndex() const noexcept;
    /** @brief Returns the cached `[width x height]` character dimensions of a single tile. */
    [[nodiscard]] QSize getTileSize() const noexcept;
    /** @brief Returns the non-owning `State*` bound to this MDI subwindow (or `nullptr` after session teardown). */
    [[nodiscard]] State* getState() const noexcept;

public slots:
    void onTileIndexUpdated(int tileIndex);
    void onTilePropertiesUpdated();
    void onTileUpdated(int tileIndex);
    void onMulticolorModeToggled(bool enabled);
    void onColorPropertiesUpdated(int pen);
    void onCharsetUpdated();
    void onFileLoaded();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    void paintCursor(QPainter& painter);
    void paintSeparators(QPainter& painter);
    void paintFocus(QPainter& painter);
    void recalculatePixelSize() noexcept;

    [[nodiscard]] bool maybeSave();

    void paintPixel(int x, int y, int pen);
    void cyclePixel(int x, int y);

    int _tileIndex = 0;
    int _charIndex = 0;
    QPoint _cursorPos { 0, 0 };

    State::TileProperties _tileProperties { { 1, 1 }, 1 };
    QPointer<State> _state; // non-owning; owned by MainWindow::DocumentSession

    QSize _pixelSize { 32, 32 };
    bool _commandMergeable = false;
};
