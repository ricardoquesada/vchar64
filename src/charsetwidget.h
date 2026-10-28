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
 * @file charsetwidget.h
 * @brief `32 x 8` C64 character-set dock canvas and rectangular glyph selector (`vchar64`).
 */

#pragma once

#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

#include "state.h"

/**
 * @brief Displays all 256 raw 8x8 glyphs (`0..255`) of the active document in a `32 x 8` grid.
 *
 * Binds exclusively to the currently active `State` via `ActiveDocumentRelay::setActiveState()`.
 */
class CharsetWidget : public QWidget {
    Q_OBJECT

public:
    explicit CharsetWidget(QWidget* parent = nullptr);

    [[nodiscard]] bool hasSelection() const noexcept;
    /** @brief Populates `copyRange` (`CopyRange::CHARS`) for the active glyph or Shift-drag selection. */
    void getSelectionRange(State::CopyRange* copyRange) const;
    /** @brief Returns the linear character index (`0..255`) at the top-left corner of the current selection. */
    [[nodiscard]] int getCursorPos() const noexcept;

public slots:
    /** @brief Rebinds the dock to `state` (or `nullptr` when all MDI subwindows close). */
    void setActiveState(State* state);
    void onCharIndexUpdated(int charIndex);
    void onMulticolorModeToggled(bool state);
    void onColorPropertiesUpdated(int pen);
    void onTileUpdated(int tileIndex);
    void onBytesUpdated(int pos, int count);
    void onCharsetUpdated();
    void enableGrid(bool enabled);
    void setZoomLevel(int zoomLevel);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    [[nodiscard]] QSize sizeHint() const override;

    void updateCharIndex(int charIndex);
    void paintFocus(QPainter& painter);

private:
    [[nodiscard]] QPoint mapMousePositionToCell(const QPoint& pos) const noexcept;

    QPointer<State> _activeState;
    QPoint _cursorPos { 0, 0 };
    bool _selecting = false;
    QSize _selectingSize { 1, 1 };
    int _charIndex = -1;
    QSize _sizeHint { 0, 0 };
    qreal _zoomLevel = 2.0;
    bool _displayGrid = false;
};
