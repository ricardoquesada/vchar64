#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QDebug>

#include "stateexport.h"
#include "state.h"

ExportDialog::ExportDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExportDialog),
    _settings()
{
    ui->setupUi(this);

    auto lastDir = _settings.value("dir/lastdir", QDir::homePath()).toString();
    lastDir += "/untitled.bin";
    ui->editFilename->setText(lastDir);
}

ExportDialog::~ExportDialog()
{
    delete ui;
}

void ExportDialog::on_pushButton_clicked()
{
    auto filename = QFileDialog::getSaveFileName(this,
                                                 tr("Select filename"),
                                                 ui->editFilename->text(),
                                                 tr("Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;Any file (*)"),
                                                 nullptr,
                                                 QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty())
        ui->editFilename->setText(filename);
}

void ExportDialog::accept()
{
    auto filename = ui->editFilename->text();

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
        qDebug() << "Error opening file: " << filename;
    } else {
        auto state = State::getInstance();
        if (ui->radioRaw->isChecked())
        {
            StateExport::saveRaw(file, state);
        }
        else
        {
            StateExport::savePRG(file, state, ui->spinPRGAddress->value());
        }

        QFileInfo info(filename);
        auto dir = info.absolutePath();
        _settings.setValue("dir/lastdir", dir);

        qDebug() << "File saved correctly file: " << filename;
    }

    // do something
    QDialog::accept();
}
