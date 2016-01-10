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

#include "importkoaladialog.h"
#include "ui_importkoaladialog.h"

#include <QFileDialog>
#include <QSettings>

ImportKoalaDialog::ImportKoalaDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportKoalaDialog)
{
    ui->setupUi(this);

    auto lastDir = QSettings("RetroMoe","VChar64").value("dir/lastdir").toString();
    ui->lineEdit->setText(lastDir);
}

ImportKoalaDialog::~ImportKoalaDialog()
{
    delete ui;
}

void ImportKoalaDialog::on_pushButton_clicked()
{
    auto filter = tr("Koala files");
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select Koala File"),
                                           ui->lineEdit->text(),
                                           tr(
                                               "All files (*);;" \
                                               "Koala files (*.koa *.kla);;"
                                           ),
                                           &filter
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0)
    {
        if (fn != _filepath)
        {
            _filepath = fn;
            ui->lineEdit->setText(fn);

            _validKoalaFile = validateKoalaFile(fn);
        }
    }
}


// helpers


bool ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    QFileInfo info(filepath);

    if (info.exists() && info.isFile() && (info.size() == 10003 || info.size() == 10002))
    {
        ui->widget->loadKoala(filepath);
        return true;
    }
    return false;
}
