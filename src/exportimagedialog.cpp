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

#include "exportimagedialog.h"
#include "ui_exportimagedialog.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QWidget>

#include "mainwindow.h"
#include "state.h"

ExportImageDialog::ExportImageDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ExportImageDialog)
    , _state(state)
{
    ui->setupUi(this);
}

ExportImageDialog::~ExportImageDialog()
{
    delete ui;
}
