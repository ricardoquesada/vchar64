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

#include "serverconnectdialog.h"
#include "ui_serverconnectdialog.h"

#include <QRegExp>
#include <QRegExpValidator>
#include <QDebug>

ServerConnectDialog::ServerConnectDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConnectDialog)
{
    ui->setupUi(this);

    auto regexp = QRegExp ("^[0-2]?[0-9]?[0-9]\\.[0-2]?[0-9]?[0-9]\\.[0-2]?[0-9]?[0-9]\\.[0-2]?[0-9]?[0-9]$");
    auto validator = new QRegExpValidator(regexp, this);
    ui->lineEdit->setValidator(validator);
}

QString ServerConnectDialog::getIPAddress() const
{
    return ui->lineEdit->text();
}

ServerConnectDialog::~ServerConnectDialog()
{
    delete ui;
}

