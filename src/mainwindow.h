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

    static constexpr int MAX_RECENT_FILES=8;

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

    void updateRecentFiles();
    void setRecentFile(const QString& fileName);
    QStringList recentFiles() const;
    void openFile(const QString& fileName);

    // XXX If declared as "slot" it will raise a runtime warning. uh?
    void on_openRecentFile_triggered();

private slots:

    void on_actionExit_triggered();
    void on_actionOpen_triggered();
    void on_checkBox_toggled(bool checked);
    void on_radioButton_1_clicked();
    void on_radioButton_2_clicked();
    void on_radioButton_3_clicked();
    void on_radioButton_4_clicked();
    void on_actionSaveAs_triggered();
    void on_actionSave_triggered();
    void on_actionExport_triggered();
    void on_actionInvert_triggered();
    void on_actionFlipHorizontally_triggered();
    void on_actionFlipVertically_triggered();
    void on_actionRotate_triggered();
    void on_actionClearCharacter_triggered();
    void on_actionShiftLeft_triggered();
    void on_actionShiftRight_triggered();
    void on_actionShiftUp_triggered();
    void on_actionShiftDown_triggered();
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();
    void on_actionReportBug_triggered();
    void on_actionAbout_triggered();
    void on_actionAboutQt_triggered();
    void on_actionClearRecentFiles_triggered();

    void on_actionEmptyProject_triggered();

    void on_actionC64Default_triggered();

private:

    QAction* _recentFiles[MAX_RECENT_FILES];

    Ui::MainWindow *_ui;
    QString _lastDir;

    QSettings _settings;
};

