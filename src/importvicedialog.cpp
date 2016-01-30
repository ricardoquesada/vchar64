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
#include "mainwindow.h"

ImportVICEDialog::ImportVICEDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ImportVICEDialog)
    , _validVICEFile(false)
    , _filepath("")
{
    ui->setupUi(this);

    auto lastDir = QSettings("RetroMoe","VChar64").value("dir/lastdir").toString();
    ui->lineEdit->setText(lastDir);
    updateWidgets();
}

ImportVICEDialog::~ImportVICEDialog()
{
    delete ui;
}

const QString& ImportVICEDialog::getFilepath() const
{
    return _filepath;
}

//
// slots
//
void ImportVICEDialog::on_pushButton_import_clicked()
{
    auto state = new State(_filepath);
    state->setMulticolorMode(ui->checkBox->checkState() == Qt::Checked);

    // FIXME: must be called after 'setMulticolorMode' since it reset the undo stack
    state->importCharset(ui->widget->getBuffer() + ui->spinBox->value(), State::CHAR_BUFFER_SIZE);

    MainWindow::getInstance()->createDocument(state);
    QFileInfo info(ui->lineEdit->text());
    QSettings("RetroMoe","VChar64").setValue("dir/lastdir", info.absolutePath());

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

void ImportVICEDialog::on_pushButton_clicked()
{
    auto filter = tr("VICE snapshot files");
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select VICE Snapshot File"),
                                           ui->lineEdit->text(),
                                           tr(
                                               "All files (*);;" \
                                               "VICE snapshot files (*.vsf);;"
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

            _validVICEFile = validateVICEFile(fn);
            updateWidgets();
        }
    }
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    QString filename = ui->lineEdit->text();

    if (_filepath != filename)
    {
        QFileInfo fi(filename);
        _validVICEFile = fi.exists() && validateVICEFile(filename);
        updateWidgets();
        _filepath = filename;
    }
}

// helper
bool ImportVICEDialog::validateVICEFile(const QString& filepath)
{
    QFile file(filepath);

    quint8 buffer[65536];
    auto ret = StateImport::parseVICESnapshot(file, buffer);
    if (ret >= 0)
        ui->widget->setBuffer(buffer);
    return (ret >= 0);
}

void ImportVICEDialog::updateWidgets()
{
    QWidget* widgets[] =
    {
        ui->checkBox,
        ui->spinBox,
        ui->pushButton_import
    };

    const int COUNT = sizeof(widgets) / sizeof(widgets[0]);

    for (int i=0; i<COUNT; i++)
    {
        widgets[i]->setEnabled(_validVICEFile);
    }
}
