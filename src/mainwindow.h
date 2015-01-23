#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:

    void on_actionExit_triggered();
    void on_actionOpen_triggered();

    void on_checkBox_toggled(bool checked);

private:
    Ui::MainWindow *_ui;
    QString _lastDir;
};

#endif // MAINWINDOW_H
