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

#include "state.h"

ImportVICEDialog::ImportVICEDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportVICEDialog),
    _validVICEFile(false)
{
    ui->setupUi(this);

    updateWidgets();
}

ImportVICEDialog::~ImportVICEDialog()
{
    delete ui;
}

void ImportVICEDialog::on_pushButton_import_clicked()
{
    auto state = State::getInstance();
//    state->importMemory();
    state->setMulticolorMode(ui->checkBox->checkState() == Qt::Checked);
    accept();
}

void ImportVICEDialog::on_pushButton_cancel_clicked()
{
    reject();
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

        QFileInfo fi(fn);
        _validVICEFile = fi.exists() && validVICEFile(fn);
        updateWidgets();
    }
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    QString filename = ui->lineEdit->text();

    QFileInfo fi(filename);

    _validVICEFile = fi.exists() && validVICEFile(filename);
    updateWidgets();
}

// helper
bool ImportVICEDialog::validVICEFile(const QString& filepath)
{
    Q_UNUSED(filepath);
    return true;
}

void ImportVICEDialog::updateWidgets()
{
    QWidget* widgets[] =
    {
        ui->checkBox,
        ui->spinBox,
        ui->widget,
        ui->pushButton_import
    };

    const int COUNT = sizeof(widgets) / sizeof(widgets[0]);

    for (int i=0; i<COUNT; i++)
    {
        widgets[i]->setEnabled(_validVICEFile);
    }
}
