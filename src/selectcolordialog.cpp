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
 * @file selectcolordialog.cpp
 * @brief Implementation of the 16-swatch C64 VIC-II color picker dialog.
 */

#include "selectcolordialog.h"
#include "ui_selectcolordialog.h"

#include <QMouseEvent>

SelectColorDialog::SelectColorDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::SelectColorDialog)
{
    ui->setupUi(this);

    _widgets = {
        ui->widget,
        ui->widget_2,
        ui->widget_3,
        ui->widget_4,
        ui->widget_5,
        ui->widget_6,
        ui->widget_7,
        ui->widget_8,
        ui->widget_9,
        ui->widget_10,
        ui->widget_11,
        ui->widget_12,
        ui->widget_13,
        ui->widget_14,
        ui->widget_15,
        ui->widget_16,
    };

    for (size_t i = 0U; i < _widgets.size(); ++i) {
        _widgets[i]->setColorIndex(static_cast<int>(i));
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SelectColorDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SelectColorDialog::reject);
}

int SelectColorDialog::getSelectedColor() const noexcept
{
    return _selectedColor;
}

void SelectColorDialog::setCurrentColor(int colorIndex)
{
    if (colorIndex < 0 || colorIndex >= static_cast<int>(_widgets.size())) {
        return;
    }

    if (_selectedColor != colorIndex) {
        if (_selectedColor >= 0 && _selectedColor < static_cast<int>(_widgets.size())) {
            _widgets[static_cast<size_t>(_selectedColor)]->setSelected(false);
        }

        _selectedColor = colorIndex;
        _widgets[static_cast<size_t>(_selectedColor)]->setSelected(true);

        emit colorSelected(colorIndex);
    }
}

SelectColorDialog::~SelectColorDialog()
{
    delete ui;
}

void SelectColorDialog::mousePressEvent(QMouseEvent* event)
{
    for (size_t i = 0U; i < _widgets.size(); ++i) {
        const QPoint localPos = _widgets[i]->mapFromParent(event->pos());
        if (_widgets[i]->rect().contains(localPos)) {
            setCurrentColor(static_cast<int>(i));
            event->accept();
            return;
        }
    }
    event->ignore();
}
