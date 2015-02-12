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

#pragma once

#include <QMainWindow>
#include <QString>
#include <QSettings>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setTitle(const QString& title);
    Ui::MainWindow* getUi() const {
        return _ui;
    }

protected:
    void createActions();
    void createMenus();
    void createDefaults();
    void about();

private slots:

    void on_actionExit_triggered();
    void on_actionOpen_triggered();

    void on_checkBox_toggled(bool checked);

    void on_radioButton_clicked();

    void on_radioButton_2_clicked();

    void on_radioButton_3_clicked();

    void on_radioButton_4_clicked();

    void on_actionImport_triggered();

    void on_actionNew_Project_triggered();

    void on_actionSave_As_triggered();

    void on_actionSave_triggered();

    void on_actionExport_triggered();

    void on_actionInvert_triggered();

    void on_actionFlip_Horizontally_triggered();

    void on_actionFlip_Vertically_triggered();

    void on_actionRotate_triggered();

    void on_actionClear_Character_triggered();

    void on_actionShift_Left_triggered();

    void on_actionShift_Right_triggered();

    void on_actionShift_Up_triggered();

    void on_actionShift_Down_triggered();

    void on_actionCopy_triggered();

    void on_actionPaste_triggered();

private:
    Ui::MainWindow *_ui;
    QString _lastDir;

    QMenu *_helpMenu;
    QAction *_aboutAct;
    QAction *_aboutQtAct;

    QSettings _settings;
};

