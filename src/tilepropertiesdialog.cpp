/****************************************************************************
Copyright 2015 Ricardo Quesada

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

// TilePropertiesDialog implementation for editing tile dimensions and interleave distance.

#include "tilepropertiesdialog.h"
#include "ui_tilepropertiesdialog.h"

#include <algorithm>

#include <QSize>
#include <QSpinBox>

#include "state.h"

TilePropertiesDialog::TilePropertiesDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::TilePropertiesDialog)
    , _state(state)
{
    ui->setupUi(this);

    const State::TileProperties properties = _state->getTileProperties();
    ui->spinBoxSizeX->setValue(properties.size.width());
    ui->spinBoxSizeY->setValue(properties.size.height());
    updateInterleavedMax();
    ui->spinBoxInterleaved->setValue(properties.interleaved);

    connect(ui->spinBoxSizeX, &QSpinBox::valueChanged, this, [this](int) {
        updateInterleavedMax();
    });
    connect(ui->spinBoxSizeY, &QSpinBox::valueChanged, this, [this](int) {
        updateInterleavedMax();
    });
}

TilePropertiesDialog::~TilePropertiesDialog()
{
    delete ui;
}

void TilePropertiesDialog::on_buttonBox_accepted()
{
    const int tile_width = ui->spinBoxSizeX->value();
    const int tile_height = ui->spinBoxSizeY->value();
    const int interleaved = ui->spinBoxInterleaved->value();

    State::TileProperties properties;
    properties.size = QSize(tile_width, tile_height);
    properties.interleaved = interleaved;

    _state->setTileProperties(properties);
}

void TilePropertiesDialog::on_spinBoxSizeX_editingFinished()
{
    updateInterleavedMax();
}

void TilePropertiesDialog::on_spinBoxSizeY_editingFinished()
{
    updateInterleavedMax();
}

void TilePropertiesDialog::updateInterleavedMax()
{
    const int tile_width = std::max(1, ui->spinBoxSizeX->value());
    const int tile_height = std::max(1, ui->spinBoxSizeY->value());
    const int char_budget = (_state != nullptr && _state->isExtendedColorMode())
        ? State::ECM_CHAR_COUNT
        : 256;
    ui->spinBoxInterleaved->setMaximum(std::max(1, char_budget / (tile_width * tile_height)));
}
