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

#include "importvicedialog.h"
#include "ui_importvicedialog.h"

#include <QFileDialog>
#include <QSettings>
#include <QDir>

ImportVICEDialog::ImportVICEDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportVICEDialog)
{
    ui->setupUi(this);
}

ImportVICEDialog::~ImportVICEDialog()
{
    delete ui;
}

void ImportVICEDialog::on_pushButton_import_clicked()
{

}

void ImportVICEDialog::on_pushButton_cancel_clicked()
{

}

void ImportVICEDialog::on_spinBox_editingFinished()
{
    // normalize number, in case it was edited manually
    int oldvalue = ui->spinBox->value();
    int m = oldvalue / 2048;
    int newvalue = m * 2048;

    if (newvalue != oldvalue)
        ui->spinBox->setValue(newvalue);
}

void ImportVICEDialog::on_spinBox_valueChanged(int value)
{
    Q_UNUSED(value);
}

void ImportVICEDialog::on_pushButton_clicked()
{
    QString filter("VICE snapshot files");
    QString lastdir = QSettings("RetroMoe","VChar64").value("dir/lastdir", QDir::homePath()).toString();
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select VICE Snapshot File"),
                                           lastdir,
                                           tr(
                                               "All files (*);;" \
                                               "VICE snapshot files (*.vsf);;"
                                           ),
                                           &filter
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0) {
        ui->lineEdit->setText(fn);
    }
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    QString filename = ui->lineEdit->text();

    QFileInfo fi(filename);
    if (fi.exists() && validVICEFile(filename))
    {

    }
}

// helper
bool ImportVICEDialog::validVICEFile(const QString& filepath)
{
    Q_UNUSED(filepath);
    return true;
}
