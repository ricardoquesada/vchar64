#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <QFileDialog>
#include <QSettings>
#include <QDir>

ExportDialog::ExportDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExportDialog),
    _settings()
{
    ui->setupUi(this);

    auto lastDir = _settings.value("dir/lastdir", QDir::homePath()).toString();
    lastDir += "/untitled.bin";
    ui->lineEdit->setText(lastDir);
}

ExportDialog::~ExportDialog()
{
    delete ui;
}

void ExportDialog::on_pushButton_clicked()
{
    auto filename = QFileDialog::getSaveFileName(this,
                                                 tr("Select filename"),
                                                 ui->lineEdit->text(),
                                                 tr("Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;Any file (*)"),
                                                 nullptr,
                                                 QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty())
        ui->lineEdit->setText(filename);
}

void ExportDialog::accept()
{
    auto filename = ui->lineEdit->text();

    // do something
    QDialog::accept();
}
