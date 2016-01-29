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
#include <QVector>

#include "state.h"

QT_BEGIN_NAMESPACE
class QUndoView;
class QLabel;
class QMdiSubWindow;
class QUndoView;
class QSpinBox;
class QComboBox;
QT_END_NAMESPACE

namespace Ui {
class MainWindow;
}

class BigCharWidget;
class State;

class MainWindow : public QMainWindow
{
    Q_OBJECT

    static constexpr int MAX_RECENT_FILES=20;

public:
    /**
     * @brief getInstance returns the singletone
     * @return the MainWindow
     */
    static MainWindow* getInstance();

    /**
     * @brief getCurrentState returns the current State
     * @return nullptr if none are present. Otherwise it will return the current State
     */
    static State* getCurrentState();

    Ui::MainWindow* getUi() const {
        return _ui;
    }

    /**
     * @brief createsDocument creates a document with a State
     * @return
     */
    BigCharWidget* createDocument(State* state);

    /**
     * @brief openDefaultDocument open the default document. Either the last one that was opened
     * or a new one
     * @return returns the newly created BigCharWidget
     */
    void openDefaultDocument();

    void readSettings();

public slots:
    void xlinkConnected();
    void xlinkDisconnected();
    void serverConnected();
    void serverDisconnected();
    void documentWasModified();
    void onCharIndexUpdated(int);
    void onMulticolorModeToggled(bool);
    void onTilePropertiesUpdated();
    bool openFile(const QString& fileName);
    void setErrorMessage(const QString& errorMessage);
    void onColorPropertiesUpdated(int pen);
    void onOpenRecentFileTriggered();
    void onSubWindowActivated(QMdiSubWindow* subwindow);
    void onSpinBoxValueChanged(int tileIndex);
    bool on_actionSave_triggered();


protected:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void saveSettings();
    void createActions();
    void createDefaults();
    void createUndoView();
    void setupStatusBar();
    void setupMapDock();
    void closeState(State* state);

    void updateRecentFiles();
    void setRecentFile(const QString& fileName);
    QStringList recentFiles() const;

    void activateRadioButtonIndex(int pen);
    void activatePalette(int paletteIndex);

    void updateMenus();

    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

    BigCharWidget* getBigcharWidget() const;
    State* getState() const;

    State::CopyRange bufferToClipboard(State* state) const;
    bool bufferFromClipboard(State::CopyRange **out_range, quint8 **out_buffer) const;

private slots:

    void on_actionExit_triggered();
    void on_actionOpen_triggered();
    bool on_actionSaveAs_triggered();
    void on_actionExport_triggered();
    void on_actionExportAs_triggered();
    void on_actionInvert_triggered();
    void on_actionFlipHorizontally_triggered();
    void on_actionFlipVertically_triggered();
    void on_actionRotate_triggered();
    void on_actionClearCharacter_triggered();
    void on_actionShiftLeft_triggered();
    void on_actionShiftRight_triggered();
    void on_actionShiftUp_triggered();
    void on_actionShiftDown_triggered();
    void on_actionCut_triggered();
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();
    void on_actionReportBug_triggered();
    void on_actionAbout_triggered();
    void on_actionAboutQt_triggered();
    void on_actionClearRecentFiles_triggered();
    void on_actionEmptyProject_triggered();
    void on_actionC64DefaultUppercase_triggered();
    void on_actionC64DefaultLowercase_triggered();
    void on_actionTilesProperties_triggered();
    void on_actionUndo_triggered();
    void on_actionRedo_triggered();
    void on_radioButton_background_clicked();
    void on_radioButton_foreground_clicked();
    void on_radioButton_multicolor1_clicked();
    void on_radioButton_multicolor2_clicked();
    void on_actionBackground_triggered();
    void on_actionForeground_triggered();
    void on_actionMulti_Color_1_triggered();
    void on_actionMulti_Color_2_triggered();
    void on_checkBox_multicolor_toggled(bool checked);
    void on_actionPalette_0_triggered();
    void on_actionPalette_1_triggered();
    void on_actionPalette_2_triggered();
    void on_actionPalette_3_triggered();
    void on_actionPalette_4_triggered();
    void on_actionDocumentation_triggered();
    void on_actionNext_Tile_triggered();
    void on_actionPrevious_Tile_triggered();
    void on_actionImport_VICE_snapshot_triggered();
    void on_actionReset_Layout_triggered();
    void on_actionClose_triggered();
    void on_actionClose_All_triggered();

    void on_actionXlinkConnection_triggered();
    void on_actionServerConnection_triggered();

    void on_actionImport_Koala_image_triggered();

    void on_radioButton_charColorGlobal_clicked();
    void on_radioButton_charColorPerChar_clicked();


    void on_actionMap_Properties_triggered();

    void onSpinBoxMapSize_valueChanged(int newValue);
    void onMapSizeUpdated();

    void on_actionFill_Map_triggered();

    void on_actionPaint_Mode_triggered();

    void on_actionSelect_Mode_triggered();

    void on_actionClear_Map_triggered();

    void on_actionToggle_Grid_triggered();

private:
    QVector<QAction*> _recentFiles;

    Ui::MainWindow* _ui;
    QLabel* _labelCharIdx;
    QLabel* _labelTileIdx;
    QLabel* _labelSelectedColor;
    QUndoView* _undoView;

    QSettings _settings;

    // FIXME: Should be moved to the "map dock" once it is implemented
    QSpinBox* _spinBoxMapX;
    QSpinBox* _spinBoxMapY;
    QComboBox* _comboBoxMapZoom;
};
