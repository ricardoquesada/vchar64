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

#include <QSize>

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
    ui->spinBoxInterleaved->setValue(properties.interleaved);
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
    const int tile_width = ui->spinBoxSizeX->value();
    const int tile_height = ui->spinBoxSizeY->value();
    ui->spinBoxInterleaved->setMaximum(256 / (tile_width * tile_height));
}

void TilePropertiesDialog::on_spinBoxSizeY_editingFinished()
{
    on_spinBoxSizeX_editingFinished();
}
