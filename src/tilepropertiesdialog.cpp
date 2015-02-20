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

#include "tilepropertiesdialog.h"
#include "ui_tilepropertiesdialog.h"

#include "state.h"

TilePropertiesDialog::TilePropertiesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TilePropertiesDialog)
{
    ui->setupUi(this);

    auto state = State::getInstance();
    auto properties = state->getTileProperties();
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
    int w = ui->spinBoxSizeX->value();
    int h = ui->spinBoxSizeY->value();
    int interleaved = ui->spinBoxInterleaved->value();

    auto state = State::getInstance();
    State::TileProperties properties;
    properties.size = {w,h};
    properties.interleaved = interleaved;
    state->setTileProperties(properties);
}

void TilePropertiesDialog::on_spinBoxSizeX_editingFinished()
{
    int x = ui->spinBoxSizeX->value();
    int y = ui->spinBoxSizeY->value();
    ui->spinBoxInterleaved->setMaximum(256/(x*y));
}

void TilePropertiesDialog::on_spinBoxSizeY_editingFinished()
{
    on_spinBoxSizeX_editingFinished();
}
