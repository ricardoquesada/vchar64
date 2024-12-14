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
class ExportDialog;
}

class State;

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(State* state, QWidget* parent = nullptr);
    ~ExportDialog() Q_DECL_OVERRIDE;

private slots:
    void on_pushBrowse_clicked();

    void on_radioButton_raw_toggled(bool checked);

    void on_radioButton_asm_toggled(bool checked);

    void on_radioButton_prg_toggled(bool checked);

    void on_radioButton_c_toggled(bool checked);

    void on_radioButton_png_toggled(bool checked);

    void on_checkBox_charset_toggled(bool checked);
    void on_checkBox_map_toggled(bool checked);
    void on_checkBox_tileColors_toggled(bool checked);

private:
    void accept() Q_DECL_OVERRIDE;
    void updateButtons();

    Ui::ExportDialog* ui;
    State* _state; // weak ref
    int _checkBox_clicked;
};
