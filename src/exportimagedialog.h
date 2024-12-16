/****************************************************************************
Copyright 2024 Ricardo Quesada

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
class ExportImageDialog;
}

class State;

class ExportImageDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportImageDialog(State* state, QWidget* parent = nullptr);
    ~ExportImageDialog() Q_DECL_OVERRIDE;

private slots:
    void on_pushBrowse_clicked();

private:
    void accept() Q_DECL_OVERRIDE;

    Ui::ExportImageDialog* ui;
    State* _state; // weak ref
};
