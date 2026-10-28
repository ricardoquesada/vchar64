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
 * @file palettewidget.h
 * @brief Interactive `8 x 2` C64 16-color VIC-II palette picker widget (`vchar64`).
 */

#pragma once

#include <QPointer>
#include <QSize>
#include <QWidget>

class State;

/**
 * @brief Renders the 16 VIC-II colors (`0..15`) and assigns clicks to the active document's selected pen.
 *
 * When `PEN_FOREGROUND` is selected in Multicolor mode, bottom-row swatches (`8..15`) visually
 * mirror hardware colors `0..7` so artists can toggle Color RAM bit 3 (Hi-Res vs Multicolor)
 * while seeing the true rendered foreground hue.
 */
class PaletteWidget : public QWidget {
    Q_OBJECT

public:
    explicit PaletteWidget(QWidget* parent);

public slots:
    /** @brief Binds the palette picker to the currently active MDI `State` (or `nullptr`). */
    void setActiveState(State* state);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    [[nodiscard]] QSize sizeHint() const override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPointer<State> _activeState;
    QSize _pixelSize;
    QSize _sizeHint;
};
