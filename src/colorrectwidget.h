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
 * @file colorrectwidget.h
 * @brief Swatch preview widget displaying either an active VIC-II pen or a static C64 color (`vchar64`).
 */

#pragma once

#include <QPointer>
#include <QWidget>

class State;

/**
 * @brief Displays either the color associated with a VIC-II pen on the active State
 *        or a fixed C64 color index (used in Import Koala and Select Color dialogs).
 */
class ColorRectWidget : public QWidget {
    Q_OBJECT

public:
    /** @brief Selects whether the swatch tracks a live `State` pen or a static `0..15` color index. */
    enum Mode {
        SHOW_PEN_MODE,
        SHOW_COLORINDEX_MODE,
    };

    explicit ColorRectWidget(QWidget* parent = nullptr);
    ~ColorRectWidget() override;

    void setPen(int pen);
    [[nodiscard]] int getPen() const noexcept;

    void setColorIndex(int colorIndex);
    [[nodiscard]] int getColorIndex() const noexcept;

    /** @brief Draws a highlighted red border around the swatch when `selected` is true. */
    void setSelected(bool selected);

public slots:
    /** @brief Rebinds `SHOW_PEN_MODE` swatches to the active MDI `State` (or `nullptr`). */
    void setActiveState(State* state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<State> _activeState;
    Mode _mode = SHOW_PEN_MODE;
    int _pen = 0;
    int _colorIndex = -1;
    bool _selected = false;
};
