#include "updatedialog.h"
#include "ui_updatedialog.h"

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
    ui->plainTextEditChanges->appendPlainText(changes);
}

void UpdateDialog::on_pushButtonDownload_clicked()
{
    accept();
}

void UpdateDialog::on_pushButtonLater_clicked()
{
    reject();
}
