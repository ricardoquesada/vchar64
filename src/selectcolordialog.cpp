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

// Implementation of the 16-swatch C64 VIC-II color picker dialog.

#include "selectcolordialog.h"
#include "ui_selectcolordialog.h"

#include <cstddef>

#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPoint>

#include "colorrectwidget.h"

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

    for (size_t swatch_index = 0U; swatch_index < _widgets.size(); ++swatch_index) {
        _widgets[swatch_index]->setColorIndex(static_cast<int>(swatch_index));
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SelectColorDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SelectColorDialog::reject);
}

SelectColorDialog::~SelectColorDialog()
{
    delete ui;
}

void SelectColorDialog::setCurrentColor(int color_index)
{
    if (color_index < 0 || color_index >= static_cast<int>(_widgets.size()) || _selectedColor == color_index) {
        return;
    }

    if (_selectedColor >= 0 && _selectedColor < static_cast<int>(_widgets.size())) {
        _widgets[static_cast<size_t>(_selectedColor)]->setSelected(false);
    }

    _selectedColor = color_index;
    _widgets[static_cast<size_t>(_selectedColor)]->setSelected(true);

    emit colorSelected(color_index);
}

int SelectColorDialog::getSelectedColor() const noexcept
{
    return _selectedColor;
}

void SelectColorDialog::mousePressEvent(QMouseEvent* event)
{
    for (size_t swatch_index = 0U; swatch_index < _widgets.size(); ++swatch_index) {
        const QPoint local_pos = _widgets[swatch_index]->mapFromParent(event->pos());
        if (_widgets[swatch_index]->rect().contains(local_pos)) {
            setCurrentColor(static_cast<int>(swatch_index));
            event->accept();
            return;
        }
    }
    event->ignore();
}
