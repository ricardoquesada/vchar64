#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QSettings>

namespace Ui {
class ExportDialog;
}

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(QWidget *parent = 0);
    ~ExportDialog();

private slots:
    void on_pushButton_clicked();

private:
    void accept();

    Ui::ExportDialog *ui;
    QSettings _settings;
};

#endif // EXPORTDIALOG_H
