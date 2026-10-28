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
 * @file selectcolordialog.h
 * @brief Modal 16-swatch C64 color picker dialog (`0..15`) used during Koala `$D02x` overrides.
 *
 * Displays 16 `ColorRectWidget` swatches rendered with the active VIC-II palette and
 * emits live `colorSelected(int)` previews whenever the user clicks a swatch.
 */

#pragma once

#include <array>

#include <QDialog>

namespace Ui {
class SelectColorDialog;
}

class ColorRectWidget;

/**
 * @brief Modal picker dialog presenting all 16 VIC-II palette colors (`0..15`).
 */
class SelectColorDialog : public QDialog {
    Q_OBJECT

public:
    explicit SelectColorDialog(QWidget* parent = nullptr);
    ~SelectColorDialog() override;

    /** @brief Highlights `colorIndex` (`0..15`) and emits `colorSelected()` when changed. */
    void setCurrentColor(int colorIndex);

    /** @brief Returns the currently highlighted C64 color index (`0..15`, or `-1` if unset). */
    [[nodiscard]] int getSelectedColor() const noexcept;

signals:
    void colorSelected(int colorIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    Ui::SelectColorDialog* ui = nullptr;
    int _selectedColor = -1;
    std::array<ColorRectWidget*, 16> _widgets { };
};
