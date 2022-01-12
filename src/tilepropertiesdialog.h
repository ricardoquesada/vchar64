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

#pragma once

#include <QDialog>

namespace Ui {
class TilePropertiesDialog;
}

class State;

class TilePropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TilePropertiesDialog(State *state, QWidget *parent = nullptr);
    ~TilePropertiesDialog() Q_DECL_OVERRIDE;

private slots:
    void on_buttonBox_accepted();

    void on_spinBoxSizeX_editingFinished();

    void on_spinBoxSizeY_editingFinished();

private:
    Ui::TilePropertiesDialog *ui;
    State* _state;  // weak ref
};

