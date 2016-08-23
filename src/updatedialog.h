#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>

namespace Ui {
class UpdateDialog;
}

class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateDialog(QWidget *parent = 0);
    ~UpdateDialog();

    void setChanges(const QString& changes);
    void setNewVersion(const QString& newVersion);

private slots:
    void on_pushButtonDownload_clicked();

    void on_pushButtonLater_clicked();

private:
    Ui::UpdateDialog *ui;
};

#endif // UPDATEDIALOG_H
