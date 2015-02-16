#ifndef TILEPROPERTIESDIALOG_H
#define TILEPROPERTIESDIALOG_H

#include <QDialog>

namespace Ui {
class TilePropertiesDialog;
}

class TilePropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TilePropertiesDialog(QWidget *parent = 0);
    ~TilePropertiesDialog();

signals:
    void tilePropertiesChanged();

private slots:
    void on_buttonBox_accepted();

private:
    Ui::TilePropertiesDialog *ui;
};

#endif // TILEPROPERTIESDIALOG_H
