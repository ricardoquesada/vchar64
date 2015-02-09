#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <QFileInfo>

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
    bool ok = false;
    auto filename = ui->editFilename->text();
    auto state = State::getInstance();

    if (ui->radioRaw->isChecked())
    {
        ok = state->exportRaw(filename);
    }
    else
    {
        ok = state->exportPRG(filename, ui->spinPRGAddress->value());
    }

    if (ok) {
        QFileInfo info(filename);
        auto dir = info.absolutePath();
        _settings.setValue("dir/lastdir", dir);

        qDebug() << "File saved correctly file: " << filename;
    }
    else
    {
        qDebug() << "Error saving file: " << filename;
    }

    // do something
    QDialog::accept();
}

void ExportDialog::on_radioRaw_clicked()
{
    auto filename = ui->editFilename->text();

    QFileInfo finfo(filename);
    auto extension = finfo.suffix();

    filename.chop(extension.length()+1);
    filename += ".bin";
    ui->editFilename->setText(filename);
}

void ExportDialog::on_radioPRG_clicked()
{
    auto filename = ui->editFilename->text();

    QFileInfo finfo(filename);
    auto extension = finfo.suffix();

    filename.chop(extension.length()+1);
    filename += ".prg";
    ui->editFilename->setText(filename);
}
