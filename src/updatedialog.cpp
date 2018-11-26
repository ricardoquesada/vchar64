/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "updatedialog.h"
#include "ui_updatedialog.h"

#include <QDebug>
#include <QDesktopServices>

UpdateDialog::UpdateDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UpdateDialog)
{
    ui->setupUi(this);

    ui->labelCurrentVersion->setText(tr("Current version: %1").arg(VERSION));
}

UpdateDialog::~UpdateDialog()
{
    delete ui;
}

void UpdateDialog::setNewVersion(const QString &newVersion)
{
    ui->labelNewVersion->setText(tr("New version: %1").arg(newVersion));
}

void UpdateDialog::setChanges(const QString &changes)
{
    ui->textBrowserChanges->setHtml(changes);
}

void UpdateDialog::setUpdateURL(const QString &url)
{
    _url = QUrl::fromUserInput(url);
    if (!_url.isValid()) {
        qDebug() << "Invalid URL: " << url;
    }
}

void UpdateDialog::on_pushButtonDownload_clicked()
{
    QDesktopServices::openUrl(_url);
    accept();
}

void UpdateDialog::on_pushButtonLater_clicked()
{
    reject();
}
