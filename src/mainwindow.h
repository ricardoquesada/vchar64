/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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

/**
 * @file mainwindow.h
 * @brief Top-level MDI application shell and `DocumentSession` RAII owner (`vchar64` GUI target).
 *
 * Orchestrates multiple concurrent `State` documents inside `QMdiArea`, routes active-tab
 * signals exclusively through `ActiveDocumentRelay` (`O(1)` connections regardless of open
 * tab count), wires `ClipboardCodec` MIME Copy/Cut/Paste, and manages target-explicit save
 * and close lifecycles.
 */

#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include "fileutils.h"
#include "state.h"

QT_BEGIN_NAMESPACE
class QUndoView;
class QLabel;
class QMdiSubWindow;
class QSpinBox;
class QComboBox;
class QEvent;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
QT_END_NAMESPACE

namespace Ui {
class MainWindow;
}

class ActiveDocumentRelay;
class BigCharWidget;
class State;
struct UpdateInfo;

/**
 * @brief Primary MDI window owning open `DocumentSession` instances, docks, and actions.
 *
 * Ownership model:
 *
 * - Each open project lives in a `DocumentSession` owning `std::unique_ptr<State>`.
 * - `BigCharWidget` holds a non-owning `QPointer<State>` so widget destruction order
 *   (`Qt::WA_DeleteOnClose` / `DeferredDelete`) never double-frees or dangles `State`.
 * - `_isShuttingDown` short-circuits `QMdiSubWindow::destroyed` callbacks during
 *   `~MainWindow()` teardown to prevent half-destructed re-entry.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

    static constexpr int MAX_RECENT_FILES = 20;

public:
    static MainWindow* getInstance();
    static State* getCurrentState();

    [[nodiscard]] Ui::MainWindow* getUi() const noexcept
    {
        return _ui;
    }

    /**
     * @brief Adopts exclusive RAII ownership of @p state into an MDI DocumentSession.
     */
    BigCharWidget* adoptDocument(std::unique_ptr<State> state);

    /**
     * @brief Compatibility wrapper forwarding raw heap State* into adoptDocument.
     */
    BigCharWidget* createDocument(State* state);

    /**
     * @brief Saves @p targetState in place (or prompts Save As if never saved).
     *
     * Operates explicitly on @p targetState even when its MDI subwindow is inactive.
     * @return true if the document was saved cleanly; false on user cancel or I/O error.
     */
    bool saveDocument(State* targetState);

    /**
     * @brief Prompts for a destination path and saves @p targetState as a `.vchar64proj`.
     * @return true if saved cleanly; false on user cancel or I/O error.
     */
    bool saveAsDocument(State* targetState);

    /**
     * @brief Notifies MainWindow that @p editor accepted its closeEvent so closing
     *        subwindows are immediately excluded before Qt's DeferredDelete runs.
     */
    void notifyEditorClosing(BigCharWidget* editor);

    /**
     * @brief Iteratively closes all open MDI document subwindows, stopping immediately
     *        if any dirty subwindow cancels or fails its save prompt.
     * @return true if all documents closed cleanly; false if any subwindow remained open.
     */
    bool closeAllDocuments();

    [[nodiscard]] ActiveDocumentRelay* activeDocumentRelay() const noexcept
    {
        return _activeRelay.get();
    }

    void openDefaultDocument();
    void readSettings();

public slots:
    void serverConnected();
    void serverDisconnected();
    void serverPreviewError(const QString& message);
    void onUpdateAvailable(const UpdateInfo& info);
    void onUpdateUpToDate();
    void onUpdateCheckFailed(const QString& reason);
    void documentWasModified();
    void onActiveStateChanged(State* state);
    void onCharIndexUpdated(int charIndex);
    void onTileIndexUpdated(int tileIndex);
    void onMulticolorModeToggled(bool newvalue);
    void onTilePropertiesUpdated();
    bool openFile(const QString& fileName);
    void showMessageOnStatusBar(const QString& message);
    void onColorPropertiesUpdated(int pen);
    void onOpenRecentFileTriggered();
    void onSubWindowActivated(QMdiSubWindow* subwindow);
    void onSpinBoxValueChanged(int tileIndex);
    void onClipboardDataChanged();
    bool on_actionSave_triggered();
    void refresh();

protected:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool _openFile(const QString& fileName);
    bool activateIfAlreadyOpen(const QString& fileName);
    void saveSettings();
    void createActions();
    void createDefaults();
    void createUndoView();
    void wireActiveDocumentRelay();
    void setupStatusBar();
    void setupCharsetDock();
    void setupTilesetDock();
    void setupMapDock();
    void checkForUpdates();

    void updateRecentFiles();
    void setRecentFile(const QString& fileName);
    void setSessionFiles();

    void activateRadioButtonIndex(int pen);
    void syncPenSelectionControls(const State* state);
    void activatePalette(int paletteIndex);

    void updateMenus();
    void updateWindowModifiedFlag();

    void closeEvent(QCloseEvent* event) override;
    /** @brief Accepts drags carrying at least one file `FileUtils::classifyDroppedFiles()` allows. */
    void dragEnterEvent(QDragEnterEvent* event) override;
    /** @brief Accepts the drop and queues `processDroppedFiles()` so modal UI never runs inside the drop. */
    void dropEvent(QDropEvent* event) override;
    /** @brief Opens or imports each classified dropped file, in order. */
    void processDroppedFiles(const QList<FileUtils::DroppedFile>& files);
    /** @brief Runs the VICE snapshot import dialog, pre-loaded with @p preselectedPath if non-empty. */
    void runImportVICEDialog(const QString& preselectedPath);
    /** @brief Runs the Koala import dialog, pre-loaded with @p preselectedPath if non-empty. */
    void runImportKoalaDialog(const QString& preselectedPath);
    bool eventFilter(QObject* watched, QEvent* event) override;

    [[nodiscard]] BigCharWidget* getBigcharWidget() const;
    [[nodiscard]] State* getState() const;

    [[nodiscard]] State::CopyRange copySelectionToClipboard(const State* state);
    [[nodiscard]] bool hasValidClipboardPayload() const;

private slots:
    void teardownSessionForSubWindow(QObject* subWindowObj);

    void on_actionExit_triggered();
    void on_actionOpen_triggered();
    bool on_actionSaveAs_triggered();
    void on_actionExport_triggered();
    void on_actionExportAs_triggered();
    void on_actionExportAsImage_triggered();
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
    void on_radioButton_background_toggled(bool checked);
    void on_radioButton_foreground_toggled(bool checked);
    void on_radioButton_multicolor1_toggled(bool checked);
    void on_radioButton_multicolor2_toggled(bool checked);
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
    void on_actionImportVICESnapshot_triggered();
    void on_actionReset_Layout_triggered();
    void on_actionClose_triggered();
    void on_actionClose_All_triggered();

    void on_actionServerConnection_triggered();

    void on_actionImportKoalaImage_triggered();
    void on_actionImportBinary_triggered();

    void on_radioButton_charColorGlobal_toggled(bool checked);
    void on_radioButton_charColorPerChar_toggled(bool checked);

    void on_actionMap_Properties_triggered();

    void onSpinBoxMapSizeX_valueChanged(int newValue);
    void onSpinBoxMapSizeY_valueChanged(int newValue);
    void onMapSizeUpdated();

    void on_actionFill_Map_triggered();
    void on_actionPaint_Mode_triggered();
    void on_actionSelect_Mode_triggered();
    void on_actionClear_Map_triggered();
    void on_actionToggle_MapGrid_triggered();
    void on_actionToggle_CharsetGrid_triggered();
    void on_actionToggle_TilesetGrid_triggered();
    void on_actionCloneCurrentProject_triggered();
    void on_actionPreferences_triggered();
    void on_actionPalette_5_triggered();

private:
    struct DocumentSession {
        std::unique_ptr<State> state;
        QPointer<QMdiSubWindow> subWindow;
        QMdiSubWindow* rawSubWindow = nullptr;
        QPointer<BigCharWidget> editorWidget;
        std::vector<QMetaObject::Connection> sessionConnections;
        bool isClosing = false;
    };

    [[nodiscard]] DocumentSession* findSessionByState(const State* targetState) noexcept;
    [[nodiscard]] const DocumentSession* findSessionByState(const State* targetState) const noexcept;
    [[nodiscard]] DocumentSession* findSessionBySubWindow(const QMdiSubWindow* subWindow) noexcept;
    [[nodiscard]] const DocumentSession* findSessionBySubWindow(const QMdiSubWindow* subWindow) const noexcept;
    [[nodiscard]] DocumentSession* findSessionByEditor(const BigCharWidget* editor) noexcept;
    [[nodiscard]] bool hasOpenDocuments() const noexcept;

    std::unique_ptr<ActiveDocumentRelay> _activeRelay;
    std::vector<DocumentSession> _sessions;
    bool _isShuttingDown = false;

    QVector<QAction*> _recentFiles;

    Ui::MainWindow* _ui = nullptr;
    QLabel* _labelCharIdx = nullptr;
    QLabel* _labelTileIdx = nullptr;
    QLabel* _labelSelectedColor = nullptr;
    QUndoView* _undoView = nullptr;

    QSpinBox* _spinBoxMapX = nullptr;
    QSpinBox* _spinBoxMapY = nullptr;
    QComboBox* _comboBoxMapZoom = nullptr;
    QComboBox* _comboBoxCharsetZoom = nullptr;
    QComboBox* _comboBoxTilesetZoom = nullptr;

    QWidget* _lastFocusedEditorWidget = nullptr;
};
