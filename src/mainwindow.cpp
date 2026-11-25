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

// Implementation of the MDI MainWindow shell, DocumentSession RAII lifecycle,
// and ClipboardCodec integration.
//
// Key MDI invariants:
// - O(1) Active-Document Relay: wireActiveDocumentRelay() binds all global
//   docks, color swatches, QUndoView, and ServerPreview once to _activeRelay,
//   eliminating cross-tab repaint pollution and N x M signal growth.
// - 6-Way actionRotate Synchronization: Re-evaluates State::canRotateTile()
//   across active-document switches, tile selection changes, tile dimension
//   changes, Multicolor toggles, foreground color changes (bit-3 threshold >=
//   8), and project loads.
// - Target-Explicit Save/Close: saveDocument(State*), saveAsDocument(State*),
//   and closeAllDocuments() operate explicitly on the target session rather
//   than assuming activeSubWindow().

#include "mainwindow.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>
#include <QUndoView>
#include <QUrl>
#include <QVariant>
#include <QWidget>

#include "aboutdialog.h"
#include "autoupdater.h"
#include "bigcharwidget.h"
#include "charsetwidget.h"
#include "colorrectwidget.h"
#include "core/clipboardcodec.h"
#include "core/result.h"
#include "exportdialog.h"
#include "exportimagedialog.h"
#include "fileutils.h"
#include "importbinarydialog.h"
#include "importkoaladialog.h"
#include "importvicedialog.h"
#include "mappropertiesdialog.h"
#include "mapwidget.h"
#include "palette.h"
#include "palettewidget.h"
#include "preferences.h"
#include "preferencesdialog.h"
#include "preview/ilivepreviewsink.h"
#include "serverconnectdialog.h"
#include "serverpreview.h"
#include "state.h"
#include "tilepropertiesdialog.h"
#include "tilesetwidget.h"
#include "ui/activedocumentrelay.h"
#include "ui_mainwindow.h"
#include "updatedialog.h"

namespace {

constexpr int kStateVersion = 11;
constexpr int kDefaultZoomIndex = 4;
constexpr std::array<int, 8> kZoomPercentages = {
    200, 175, 150, 125, 100, 75, 50, 25
};

void AddExpandingToolbarSpacer(QWidget* parent, QToolBar* toolbar)
{
    QWidget* const spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
}

template <typename TargetWidget>
QComboBox* CreateZoomComboBox(QWidget* parent, TargetWidget* target_widget, QToolBar* toolbar)
{
    QComboBox* const combo_box = new QComboBox(parent);
    for (const int percentage : kZoomPercentages) {
        combo_box->addItem(QStringLiteral("%1%").arg(percentage), QVariant(percentage));
    }
    combo_box->setCurrentIndex(kDefaultZoomIndex);
    toolbar->addWidget(combo_box);

    QObject::connect(combo_box, &QComboBox::activated, parent, [combo_box, target_widget](int index) {
        const QVariant zoom_value = combo_box->itemData(index);
        target_widget->setZoomLevel(zoom_value.toInt());
    });
    return combo_box;
}

void ApplySubWindowPathAndTitle(
    MainWindow* window,
    QMdiSubWindow* sub_window,
    const QString& file_path,
    const QString& title)
{
    window->setWindowFilePath(file_path);
    if (sub_window != nullptr) {
        sub_window->setWindowFilePath(file_path);
        sub_window->setWindowTitle(title);
    }
}

} // namespace

MainWindow* MainWindow::getInstance()
{
    static MainWindow* instance = nullptr;
    if (instance == nullptr) {
        instance = new MainWindow;
    }

    Q_ASSERT(instance != nullptr);
    return instance;
}

State* MainWindow::getCurrentState()
{
    return getInstance()->getState();
}

BigCharWidget* MainWindow::adoptDocument(std::unique_ptr<State> state)
{
    if (!state) {
        return nullptr;
    }

    State* const raw_state = state.get();
    raw_state->clearUndoStack();

    BigCharWidget* const bigchar_widget = new BigCharWidget(raw_state, this);
    QMdiSubWindow* const sub_window = _ui->mdiArea->addSubWindow(bigchar_widget, Qt::Widget);
    sub_window->setAttribute(Qt::WA_DeleteOnClose);

    DocumentSession session;
    session.state = std::move(state);
    session.subWindow = sub_window;
    session.rawSubWindow = sub_window;
    session.editorWidget = bigchar_widget;

    std::vector<QMetaObject::Connection>& connections = session.sessionConnections;
    connections.push_back(connect(raw_state, &State::fileLoaded, bigchar_widget, &BigCharWidget::onFileLoaded));
    connections.push_back(
        connect(
            raw_state,
            &State::tilePropertiesUpdated,
            bigchar_widget,
            &BigCharWidget::onTilePropertiesUpdated));
    connections.push_back(connect(raw_state, &State::tileUpdated, bigchar_widget, &BigCharWidget::onTileUpdated));
    connections.push_back(
        connect(raw_state, &State::charsetUpdated, bigchar_widget, &BigCharWidget::onCharsetUpdated));
    connections.push_back(connect(raw_state, &State::bytesUpdated, bigchar_widget, [bigchar_widget](int, int) {
        bigchar_widget->update();
    }));
    connections.push_back(
        connect(
            raw_state,
            &State::colorPropertiesUpdated,
            bigchar_widget,
            &BigCharWidget::onColorPropertiesUpdated));
    connections.push_back(
        connect(
            raw_state,
            &State::characterModeChanged,
            bigchar_widget,
            &BigCharWidget::onCharacterModeChanged));
    connections.push_back(
        connect(
            raw_state,
            &State::multicolorModeToggled,
            bigchar_widget,
            &BigCharWidget::onMulticolorModeToggled));
    connections.push_back(
        connect(raw_state, &State::tileIndexUpdated, bigchar_widget, &BigCharWidget::onTileIndexUpdated));
    connections.push_back(connect(raw_state->getUndoStack(), &QUndoStack::cleanChanged, this, [this](bool) {
        updateWindowModifiedFlag();
    }));
    connections.push_back(
        connect(sub_window, &QObject::destroyed, this, &MainWindow::teardownSessionForSubWindow));

    _sessions.push_back(std::move(session));

    sub_window->showMaximized();
    if (sub_window->layout() != nullptr) {
        sub_window->layout()->setContentsMargins(2, 2, 2, 2);
    }
    _ui->mdiArea->setActiveSubWindow(sub_window);

    // Ensure relay binds immediately even if QMdiArea coalesced
    // subWindowActivated in headless/test runs.
    if (_activeRelay->activeState() != raw_state) {
        _activeRelay->setActiveState(raw_state);
    }
    updateMenus();

    return bigchar_widget;
}

BigCharWidget* MainWindow::createDocument(State* state)
{
    return adoptDocument(std::unique_ptr<State>(state));
}

bool MainWindow::saveDocument(State* target_state)
{
    if (target_state == nullptr) {
        return false;
    }

    const QString filename = target_state->getSavedFilename();
    if (filename.isEmpty()) {
        return saveAsDocument(target_state);
    }

    const vchar64::Result<void> save_result = target_state->saveProject(filename);
    if (!save_result.isOk()) {
        statusBar()->showMessage(tr("Error saving file"), 2000);
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error saving project file: ") + filename + QStringLiteral("\n") + save_result.errorMessage(),
            QMessageBox::Ok);
        return false;
    }

    statusBar()->showMessage(tr("File saved to %1").arg(target_state->getSavedFilename()), 2000);
    updateWindowModifiedFlag();
    return true;
}

bool MainWindow::saveAsDocument(State* target_state)
{
    if (target_state == nullptr) {
        return false;
    }

    QString suggested_path = target_state->getSavedFilename();
    if (suggested_path.isEmpty()) {
        suggested_path = target_state->getLoadedFilename();
    }

    if (!suggested_path.isEmpty() && !suggested_path.startsWith(QLatin1String(":/"))) {
        const QFileInfo existing_info(suggested_path);
        if (existing_info.suffix() != QLatin1String("vchar64proj")) {
            suggested_path = FileUtils::replaceExtension(suggested_path, QStringLiteral("vchar64proj"));
        }
    } else {
        suggested_path = QDir(Preferences::getInstance().getLastUsedDirectory())
                             .filePath(QStringLiteral("untitled.vchar64proj"));
    }

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Save Project"),
        suggested_path,
        tr("VChar64 project(*.vchar64proj)"));
    if (filename.isEmpty()) {
        return false;
    }

    const vchar64::Result<void> save_result = target_state->saveProject(filename);
    if (!save_result.isOk()) {
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error saving project file: ") + filename + QStringLiteral("\n") + save_result.errorMessage(),
            QMessageBox::Ok);
        return false;
    }

    const QFileInfo saved_info(filename);
    Preferences::getInstance().setLastUsedDirectory(saved_info.absolutePath());

    if (DocumentSession* const session = findSessionByState(target_state)) {
        if (session->subWindow != nullptr) {
            session->subWindow->setWindowFilePath(saved_info.filePath());
            session->subWindow->setWindowTitle(saved_info.baseName());
        }
    }
    if (target_state == getState()) {
        setWindowFilePath(saved_info.filePath());
    }

    setRecentFile(filename);
    updateWindowModifiedFlag();
    return true;
}

void MainWindow::notifyEditorClosing(BigCharWidget* editor)
{
    if (_isShuttingDown || editor == nullptr) {
        return;
    }

    DocumentSession* const closing_session = findSessionByEditor(editor);
    if (closing_session == nullptr || closing_session->isClosing) {
        return;
    }

    closing_session->isClosing = true;

    if (_activeRelay->activeState() == closing_session->state.get()) {
        State* fallback_state = nullptr;
        QMdiSubWindow* fallback_sub_window = nullptr;
        const QList<QMdiSubWindow*> history = _ui->mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            DocumentSession* const candidate = findSessionBySubWindow(*it);
            if (candidate != nullptr && !candidate->isClosing && candidate->state != nullptr) {
                fallback_state = candidate->state.get();
                fallback_sub_window = candidate->subWindow.data();
                break;
            }
        }

        if (fallback_sub_window != nullptr) {
            setWindowFilePath(fallback_sub_window->windowFilePath());
        } else {
            setWindowFilePath(QString());
        }
        _activeRelay->setActiveState(fallback_state);
    }

    updateMenus();
    updateWindowModifiedFlag();
}

bool MainWindow::closeAllDocuments()
{
    const QList<QMdiSubWindow*> sub_windows = _ui->mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* sub_window : sub_windows) {
        if (sub_window == nullptr) {
            continue;
        }
        const DocumentSession* const session = findSessionBySubWindow(sub_window);
        if (session == nullptr || session->isClosing) {
            continue;
        }
        if (!sub_window->close()) {
            return false;
        }
    }
    return !hasOpenDocuments();
}

void MainWindow::openDefaultDocument()
{
    bool opened_any = false;

    if (Preferences::getInstance().getOpenLastFiles()) {
        const QStringList file_list = Preferences::getInstance().getSessionFiles();
        for (const QString& file_path : file_list) {
            opened_any = _openFile(file_path) || opened_any;
        }
    }

    if (!opened_any) {
        on_actionC64DefaultUppercase_triggered();
    }
}

void MainWindow::readSettings()
{
    Preferences& preferences = Preferences::getInstance();
    preferences.readSettings();
    preferences.setMainWindowDefaultGeometry(saveGeometry());
    preferences.setMainWindowDefaultState(saveState(kStateVersion));

    const QByteArray geometry = preferences.getMainWindowGeometry();
    const QByteArray window_state = preferences.getMainWindowState();

    restoreState(window_state, kStateVersion);
    restoreGeometry(geometry);

    const std::array<QAction*, Palette::kPaletteCount> actions = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5,
    };
    const int palette_index = std::clamp(preferences.getPalette(), 0, Palette::kPaletteCount - 1);
    actions[static_cast<size_t>(palette_index)]->trigger();
}

void MainWindow::serverConnected()
{
    _ui->actionServerConnection->setText(tr("Disconnect"));
    showMessageOnStatusBar(tr("Connected to live preview server"));
}

void MainWindow::serverDisconnected()
{
    _ui->actionServerConnection->setText(tr("Connect"));
    showMessageOnStatusBar(tr("Disconnected from live preview server"));
}

void MainWindow::serverPreviewError(const QString& message)
{
    _ui->actionServerConnection->setText(tr("Connect"));
    showMessageOnStatusBar(tr("Live preview error: %1").arg(message));
}

void MainWindow::onUpdateAvailable(const UpdateInfo& info)
{
    Preferences::getInstance().setLastUpdateCheckDate(QDateTime::currentDateTime());
    UpdateDialog dialog(info, this);
    dialog.exec();
}

void MainWindow::onUpdateUpToDate()
{
    Preferences::getInstance().setLastUpdateCheckDate(QDateTime::currentDateTime());
}

void MainWindow::onUpdateCheckFailed(const QString& reason)
{
    qWarning() << "Update check failed:" << reason;
}

void MainWindow::documentWasModified()
{
    updateWindowModifiedFlag();

    State* const state = getState();
    if (state != nullptr) {
        _ui->actionUndo->setEnabled(state->getUndoStack()->canUndo());
        _ui->actionRedo->setEnabled(state->getUndoStack()->canRedo());
    } else {
        _ui->actionUndo->setEnabled(false);
        _ui->actionRedo->setEnabled(false);
    }
}

void MainWindow::onActiveStateChanged(State* state)
{
    if (state == nullptr) {
        if (_undoView != nullptr) {
            _undoView->setStack(nullptr);
        }
        _ui->actionUndo->setEnabled(false);
        _ui->actionRedo->setEnabled(false);
        updateTileTransformActions(nullptr);
        setWindowFilePath(QString());
        updateMenus();
        updateWindowModifiedFlag();
        return;
    }

    refresh();
    onTileIndexUpdated(state->getTileIndex());
    onCharIndexUpdated(state->getCharIndex());
    syncPenSelectionControls(state);
    documentWasModified();
    updateMenus();
}

void MainWindow::onCharIndexUpdated(int char_index)
{
    const State* const state = getState();
    if (state == nullptr) {
        return;
    }

    if (state->isExtendedColorMode()) {
        const int physical_glyph = state->effectiveCharIndex(char_index);
        const int bg_bank = static_cast<int>((static_cast<unsigned int>(char_index) >> 6U) & 0x03U);
        _labelCharIdx->setText(
            tr("Char: %1 (G:%2 BG#%3)")
                .arg(char_index, 3, 10, QLatin1Char(' '))
                .arg(physical_glyph)
                .arg(bg_bank));
    } else {
        _labelCharIdx->setText(tr("Char: %1").arg(char_index, 3, 10, QLatin1Char(' ')));
    }

    const int tile_index = state->getTileIndexFromCharIndex(char_index);
    _labelTileIdx->setText(tr("Tile: %1").arg(tile_index, 3, 10, QLatin1Char(' ')));
}

void MainWindow::onTileIndexUpdated(int tile_index)
{
    const State* const state = getState();
    const QSignalBlocker blocker(_ui->spinBox_tileIndex);
    _ui->spinBox_tileIndex->setValue(tile_index);
    updateTileTransformActions(state);
}

void MainWindow::onCharacterModeChanged(State::CharacterMode mode)
{
    Q_UNUSED(mode)

    State* const state = getState();
    if (state == nullptr) {
        updateTileTransformActions(nullptr);
        return;
    }

    const State::CharacterMode active_mode = state->characterMode();
    {
        const QSignalBlocker hires_radio_blocker(_ui->radioButton_modeHiRes);
        const QSignalBlocker mcm_radio_blocker(_ui->radioButton_modeMulticolor);
        const QSignalBlocker ecm_radio_blocker(_ui->radioButton_modeExtendedColor);
        const QSignalBlocker hires_action_blocker(_ui->actionMode_HiRes);
        const QSignalBlocker mcm_action_blocker(_ui->actionMode_Multicolor);
        const QSignalBlocker ecm_action_blocker(_ui->actionMode_ExtendedColor);

        _ui->radioButton_modeHiRes->setChecked(active_mode == State::CHARACTER_MODE_HIRES);
        _ui->radioButton_modeMulticolor->setChecked(active_mode == State::CHARACTER_MODE_MULTICOLOR);
        _ui->radioButton_modeExtendedColor->setChecked(active_mode == State::CHARACTER_MODE_EXTENDED_COLOR);

        _ui->actionMode_HiRes->setChecked(active_mode == State::CHARACTER_MODE_HIRES);
        _ui->actionMode_Multicolor->setChecked(active_mode == State::CHARACTER_MODE_MULTICOLOR);
        _ui->actionMode_ExtendedColor->setChecked(active_mode == State::CHARACTER_MODE_EXTENDED_COLOR);
    }

    const bool is_ecm = state->isExtendedColorMode();
    if (is_ecm) {
        _ui->radioButton_background->setText(tr("Background #0"));
        _ui->radioButton_multicolor1->setText(tr("Background #1"));
        _ui->radioButton_multicolor2->setText(tr("Background #2"));
        _ui->radioButton_background3->setText(tr("Background #3"));

        _ui->actionBackground->setText(tr("Background #0"));
        _ui->actionMulti_Color_1->setText(tr("Background #1"));
        _ui->actionMulti_Color_2->setText(tr("Background #2"));
        _ui->actionBackground_3->setText(tr("Background #3"));
    } else {
        _ui->radioButton_background->setText(tr("Background"));
        _ui->radioButton_multicolor1->setText(tr("Multicolor #1"));
        _ui->radioButton_multicolor2->setText(tr("Multicolor #2"));
        _ui->radioButton_background3->setText(tr("Background #3"));

        _ui->actionBackground->setText(tr("Background"));
        _ui->actionMulti_Color_1->setText(tr("Multi Color #1"));
        _ui->actionMulti_Color_2->setText(tr("Multi Color #2"));
        _ui->actionBackground_3->setText(tr("Background #3"));
    }

    static_cast<void>(updatePenControls(state));
    updateTileTransformActions(state);
    onCharIndexUpdated(state->getCharIndex());
    onColorPropertiesUpdated(state->getSelectedPen());
    _ui->paletteWidget->update();
}

void MainWindow::onMulticolorModeToggled(bool new_value)
{
    Q_UNUSED(new_value)

    State* const state = getState();
    if (state == nullptr) {
        updateTileTransformActions(nullptr);
        return;
    }

    static_cast<void>(updatePenControls(state));
    updateTileTransformActions(state);
    onColorPropertiesUpdated(state->getSelectedPen());
}

void MainWindow::onTilePropertiesUpdated()
{
    const State* const state = getState();
    if (state == nullptr) {
        updateTileTransformActions(nullptr);
        return;
    }

    const QSize tile_size = state->getTileProperties().size;
    const int tile_width = std::max(1, tile_size.width());
    const int tile_height = std::max(1, tile_size.height());
    _ui->spinBox_tileIndex->setMaximum(std::max(0, (256 / (tile_width * tile_height)) - 1));

    updateTileTransformActions(state);
}

bool MainWindow::openFile(const QString& file_name)
{
    const QFileInfo info(file_name);
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    if (activateIfAlreadyOpen(info.canonicalFilePath())) {
        return true;
    }

    const bool opened = _openFile(file_name);
    if (!opened) {
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error loading '") + info.fileName() + QStringLiteral("'\n") + statusBar()->currentMessage(),
            QMessageBox::Ok);
    }

    return opened;
}

void MainWindow::showMessageOnStatusBar(const QString& message)
{
    statusBar()->showMessage(message, 3000);
}

void MainWindow::onColorPropertiesUpdated(int pen)
{
    _ui->dockWidget_colors->update();

    State* const state = getState();
    if (state == nullptr) {
        updateTileTransformActions(nullptr);
        return;
    }

    const bool pen_fell_back = updatePenControls(state);
    updateTileTransformActions(state);
    // Avoid overwriting the status label with the stale pre-fallback pen; setSelectedPen()
    // already emitted selectedPenChanged / triggered a fresh update if needed.
    if (pen_fell_back && pen != state->getSelectedPen()) {
        return;
    }

    const int tile_index = state->getTileIndex();
    int displayed_color = state->getColorForPen(pen, tile_index);
    const int raw_color_number = displayed_color;
    const bool multicolor_enabled = state->shouldBeDisplayedInMulticolor2(tile_index);
    if (multicolor_enabled && pen == State::PEN_FOREGROUND) {
        displayed_color %= 8;
    }

    _labelSelectedColor->setText(
        tr("Color: %1 (%2)").arg(Palette::colorName(displayed_color)).arg(raw_color_number));

    {
        const QSignalBlocker per_char_blocker(_ui->radioButton_charColorPerChar);
        const QSignalBlocker global_blocker(_ui->radioButton_charColorGlobal);
        if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_PER_TILE) {
            _ui->radioButton_charColorPerChar->setChecked(true);
        } else {
            _ui->radioButton_charColorGlobal->setChecked(true);
        }
    }
}

void MainWindow::onOpenRecentFileTriggered()
{
    const QAction* const action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    const QString path = action->data().toString();
    if (!openFile(path)) {
        Preferences& preferences = Preferences::getInstance();
        QStringList files = preferences.getRecentFiles();
        files.removeAll(path);
        preferences.setRecentFiles(files);
        updateRecentFiles();
    }
}

void MainWindow::onSubWindowActivated(QMdiSubWindow* subwindow)
{
    if (_isShuttingDown) {
        return;
    }

    DocumentSession* session = findSessionBySubWindow(subwindow);
    if (session == nullptr) {
        // Fallback to most recent non-closing session if any remain open.
        const QList<QMdiSubWindow*> history = _ui->mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            DocumentSession* const candidate = findSessionBySubWindow(*it);
            if (candidate != nullptr && !candidate->isClosing && candidate->state != nullptr) {
                session = candidate;
                break;
            }
        }
    }

    if (session != nullptr && session->state != nullptr) {
        if (session->subWindow != nullptr) {
            setWindowFilePath(session->subWindow->windowFilePath());
        }
        _activeRelay->setActiveState(session->state.get());
    } else {
        setWindowFilePath(QString());
        _activeRelay->setActiveState(nullptr);
    }

    updateMenus();
}

void MainWindow::onSpinBoxValueChanged(int tile_index)
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }

    state->setTileIndex(tile_index);

    if (_lastFocusedEditorWidget == _ui->mapWidget
        && _ui->mapWidget->getMode() == MapWidget::SELECT_MODE) {
        state->mapPaint(_ui->mapWidget->getCursorCoord(), tile_index, false);
    }
}

void MainWindow::onClipboardDataChanged()
{
    _ui->actionPaste->setEnabled(hasOpenDocuments() && hasValidClipboardPayload());
}

bool MainWindow::on_actionSave_triggered()
{
    return saveDocument(getState());
}

void MainWindow::refresh()
{
    State* const state = getState();
    if (state == nullptr) {
        updateTileTransformActions(nullptr);
        return;
    }

    onTilePropertiesUpdated();
    onColorPropertiesUpdated(State::PEN_BACKGROUND);
    onColorPropertiesUpdated(State::PEN_FOREGROUND);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR1);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR2);
    onColorPropertiesUpdated(State::PEN_BACKGROUND3);
    onColorPropertiesUpdated(state->getSelectedPen());

    onCharacterModeChanged(state->characterMode());
    onMapSizeUpdated();

    if (_undoView != nullptr) {
        _undoView->setStack(state->getUndoStack());
    }

    _ui->dockWidget_colors->update();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _activeRelay(std::make_unique<ActiveDocumentRelay>(this))
    , _ui(new Ui::MainWindow)
{
    setUnifiedTitleAndToolBarOnMac(true);
    // Child widgets that don't accept drops (MDI area, docks) forward drag
    // events up to this window.
    setAcceptDrops(true);

    _ui->setupUi(this);

    createActions();
    createDefaults();
    createUndoView();
    setupCharsetDock();
    setupTilesetDock();
    setupMapDock();
    setupStatusBar();
    wireActiveDocumentRelay();
    checkForUpdates();

    _ui->charsetWidget->installEventFilter(this);
    _ui->tilesetWidget->installEventFilter(this);
    _ui->mapWidget->installEventFilter(this);

    updateMenus();
}

MainWindow::~MainWindow()
{
    // Latch _isShuttingDown first so child QMdiSubWindow::destroyed signals
    // fired during delete _ui cannot re-enter teardownSessionForSubWindow().
    _isShuttingDown = true;

    if (_activeRelay) {
        _activeRelay->setActiveState(nullptr);
    }
    ServerPreview::getInstance()->setActiveState(nullptr);
    if (_undoView != nullptr) {
        _undoView->setStack(nullptr);
    }

    if (_ui != nullptr && _ui->mdiArea != nullptr) {
        _ui->mdiArea->disconnect(this);
    }

    for (DocumentSession& session : _sessions) {
        for (const QMetaObject::Connection& conn : session.sessionConnections) {
            QObject::disconnect(conn);
        }
        session.sessionConnections.clear();
    }
    _sessions.clear();

    delete _ui;
    _ui = nullptr;
}

bool MainWindow::_openFile(const QString& file_name)
{
    const QFileInfo info(file_name);
    auto state = std::make_unique<State>();
    const vchar64::Result<vchar64::ImportReport> result = state->openFile(file_name);
    if (!result.isOk()) {
        statusBar()->showMessage(result.errorMessage(), 5000);
        return false;
    }

    adoptDocument(std::move(state));
    setRecentFile(file_name);

    ApplySubWindowPathAndTitle(
        this,
        _ui->mdiArea->currentSubWindow(),
        info.filePath(),
        info.baseName());

    if (!result.value().warnings.isEmpty()) {
        statusBar()->showMessage(result.value().warnings.join(QStringLiteral("; ")), 5000);
    }
    return true;
}

bool MainWindow::activateIfAlreadyOpen(const QString& file_name)
{
    if (file_name.isEmpty()) {
        return false;
    }

    for (const DocumentSession& session : _sessions) {
        if (session.isClosing || session.state == nullptr || session.subWindow == nullptr) {
            continue;
        }
        const QString loaded_canonical = QFileInfo(session.state->getLoadedFilename()).canonicalFilePath();
        const QString saved_canonical = QFileInfo(session.state->getSavedFilename()).canonicalFilePath();
        if ((!loaded_canonical.isEmpty() && file_name == loaded_canonical)
            || (!saved_canonical.isEmpty() && file_name == saved_canonical)) {
            _ui->mdiArea->setActiveSubWindow(session.subWindow.data());
            session.subWindow->setFocus();
            return true;
        }
    }
    return false;
}

void MainWindow::saveSettings()
{
    Preferences& preferences = Preferences::getInstance();
    preferences.setMainWindowGeometry(saveGeometry());
    preferences.setMainWindowState(saveState(kStateVersion));
    preferences.setPalette(Palette::getActivePalette());
}

void MainWindow::createActions()
{
    for (int i = 0; i < MAX_RECENT_FILES; ++i) {
        auto* action = new QAction(this);
        _ui->menuRecentFiles->insertAction(_ui->actionClearRecentFiles, action);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFileTriggered);

        _recentFiles.append(action);
    }
    _ui->menuRecentFiles->insertSeparator(_ui->actionClearRecentFiles);
    updateRecentFiles();

    _ui->colorRectWidget_0->setPen(State::PEN_BACKGROUND);
    _ui->colorRectWidget_1->setPen(State::PEN_FOREGROUND);
    _ui->colorRectWidget_2->setPen(State::PEN_MULTICOLOR1);
    _ui->colorRectWidget_3->setPen(State::PEN_MULTICOLOR2);
    _ui->colorRectWidget_4->setPen(State::PEN_BACKGROUND3);

    auto* mode_action_group = new QActionGroup(this);
    mode_action_group->setExclusive(true);
    mode_action_group->addAction(_ui->actionMode_HiRes);
    mode_action_group->addAction(_ui->actionMode_Multicolor);
    mode_action_group->addAction(_ui->actionMode_ExtendedColor);

    ServerPreview* const server_preview = ServerPreview::getInstance();
    connect(server_preview, &ILivePreviewSink::previewConnected, this, &MainWindow::serverConnected);
    connect(server_preview, &ILivePreviewSink::previewDisconnected, this, &MainWindow::serverDisconnected);
    connect(server_preview, &ILivePreviewSink::previewError, this, &MainWindow::serverPreviewError);

    AutoUpdater& auto_updater = AutoUpdater::getInstance();
    connect(&auto_updater, &AutoUpdater::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(&auto_updater, &AutoUpdater::upToDate, this, &MainWindow::onUpdateUpToDate);
    connect(&auto_updater, &AutoUpdater::checkFailed, this, &MainWindow::onUpdateCheckFailed);

    connect(_ui->mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);
    connect(_ui->spinBox_tileIndex, &QSpinBox::valueChanged, this, &MainWindow::onSpinBoxValueChanged);

    if (QClipboard* const clipboard = QApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, &MainWindow::onClipboardDataChanged);
    }

    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_charset->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_tileset->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_map->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_colors->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_tileIndex->toggleViewAction());

    _ui->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _ui->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void MainWindow::createDefaults()
{
    tabifyDockWidget(_ui->dockWidget_charset, _ui->dockWidget_tileset);
    tabifyDockWidget(_ui->dockWidget_charset, _ui->dockWidget_map);
    _ui->dockWidget_charset->raise();
}

void MainWindow::createUndoView()
{
    auto* undo_dock = new QDockWidget(tr("Undo List"), this);
    undo_dock->setObjectName(QStringLiteral("undoDock"));

    State* const state = getState();
    if (state != nullptr) {
        _undoView = new QUndoView(state->getUndoStack(), undo_dock);
    } else {
        _undoView = new QUndoView(undo_dock);
    }

    _undoView->setObjectName(QStringLiteral("undoView"));

    undo_dock->setWidget(_undoView);
    undo_dock->setFloating(true);
    undo_dock->hide();

    _ui->menuView->insertAction(_ui->actionReset_Layout, undo_dock->toggleViewAction());
    _ui->menuView->insertSeparator(_ui->actionReset_Layout);
}

void MainWindow::wireActiveDocumentRelay()
{
    ActiveDocumentRelay* const relay = _activeRelay.get();

    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        ServerPreview::getInstance(),
        &ILivePreviewSink::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->charsetWidget, &CharsetWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->tilesetWidget, &TilesetWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->mapWidget, &MapWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->paletteWidget, &PaletteWidget::setActiveState);
    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        _ui->colorRectWidget_0,
        &ColorRectWidget::setActiveState);
    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        _ui->colorRectWidget_1,
        &ColorRectWidget::setActiveState);
    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        _ui->colorRectWidget_2,
        &ColorRectWidget::setActiveState);
    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        _ui->colorRectWidget_3,
        &ColorRectWidget::setActiveState);
    connect(
        relay,
        &ActiveDocumentRelay::activeStateChanged,
        _ui->colorRectWidget_4,
        &ColorRectWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, this, &MainWindow::onActiveStateChanged);

    connect(relay, &ActiveDocumentRelay::fileLoaded, this, &MainWindow::refresh);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->tilesetWidget, &TilesetWidget::onFileLoaded);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->charsetWidget, &CharsetWidget::onCharsetUpdated);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->mapWidget, &MapWidget::onFileLoaded);

    connect(relay, &ActiveDocumentRelay::tilePropertiesUpdated, this, &MainWindow::onTilePropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::tilePropertiesUpdated,
        _ui->tilesetWidget,
        &TilesetWidget::onTilePropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::tilePropertiesUpdated,
        _ui->mapWidget,
        &MapWidget::onTilePropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::tilePropertiesUpdated,
        _ui->charsetWidget,
        &CharsetWidget::onTilePropertiesUpdated);

    connect(relay, &ActiveDocumentRelay::mapSizeUpdated, _ui->mapWidget, &MapWidget::onMapSizeUpdated);
    connect(relay, &ActiveDocumentRelay::mapSizeUpdated, this, &MainWindow::onMapSizeUpdated);
    connect(relay, &ActiveDocumentRelay::mapContentUpdated, _ui->mapWidget, &MapWidget::onMapContentUpdated);

    connect(relay, &ActiveDocumentRelay::bytesUpdated, _ui->charsetWidget, &CharsetWidget::onBytesUpdated);
    connect(relay, &ActiveDocumentRelay::bytesUpdated, _ui->tilesetWidget, &TilesetWidget::onBytesUpdated);
    connect(relay, &ActiveDocumentRelay::bytesUpdated, _ui->mapWidget, &MapWidget::onBytesUpdated);

    connect(relay, &ActiveDocumentRelay::tileUpdated, _ui->charsetWidget, &CharsetWidget::onTileUpdated);
    connect(relay, &ActiveDocumentRelay::tileUpdated, _ui->tilesetWidget, &TilesetWidget::onTileUpdated);
    connect(relay, &ActiveDocumentRelay::tileUpdated, _ui->mapWidget, &MapWidget::onTileUpdated);

    connect(relay, &ActiveDocumentRelay::charsetUpdated, _ui->charsetWidget, &CharsetWidget::onCharsetUpdated);
    connect(relay, &ActiveDocumentRelay::charsetUpdated, _ui->tilesetWidget, &TilesetWidget::onCharsetUpdated);
    connect(relay, &ActiveDocumentRelay::charsetUpdated, _ui->mapWidget, &MapWidget::onCharsetUpdated);

    connect(relay, &ActiveDocumentRelay::charIndexUpdated, this, &MainWindow::onCharIndexUpdated);
    connect(relay, &ActiveDocumentRelay::charIndexUpdated, _ui->charsetWidget, &CharsetWidget::onCharIndexUpdated);

    connect(relay, &ActiveDocumentRelay::tileIndexUpdated, _ui->tilesetWidget, &TilesetWidget::onTileIndexUpdated);
    connect(relay, &ActiveDocumentRelay::tileIndexUpdated, this, &MainWindow::onTileIndexUpdated);

    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, this, &MainWindow::onColorPropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::colorPropertiesUpdated,
        _ui->charsetWidget,
        &CharsetWidget::onColorPropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::colorPropertiesUpdated,
        _ui->tilesetWidget,
        &TilesetWidget::onColorPropertiesUpdated);
    connect(
        relay,
        &ActiveDocumentRelay::colorPropertiesUpdated,
        _ui->mapWidget,
        &MapWidget::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, _ui->paletteWidget, [this](int) {
        _ui->paletteWidget->update();
    });
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, this, [this](int) {
        _ui->colorRectWidget_0->update();
        _ui->colorRectWidget_1->update();
        _ui->colorRectWidget_2->update();
        _ui->colorRectWidget_3->update();
        _ui->colorRectWidget_4->update();
    });

    connect(relay, &ActiveDocumentRelay::selectedPenChanged, this, &MainWindow::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::selectedPenChanged, _ui->paletteWidget, [this](int) {
        _ui->paletteWidget->update();
    });

    connect(
        relay,
        &ActiveDocumentRelay::characterModeChanged,
        _ui->charsetWidget,
        &CharsetWidget::onCharacterModeChanged);
    connect(
        relay,
        &ActiveDocumentRelay::characterModeChanged,
        _ui->tilesetWidget,
        &TilesetWidget::onCharacterModeChanged);
    connect(
        relay,
        &ActiveDocumentRelay::characterModeChanged,
        _ui->mapWidget,
        &MapWidget::onCharacterModeChanged);
    connect(
        relay,
        &ActiveDocumentRelay::characterModeChanged,
        _ui->paletteWidget,
        [this](State::CharacterMode) {
            _ui->paletteWidget->update();
        });
    connect(
        relay,
        &ActiveDocumentRelay::characterModeChanged,
        this,
        &MainWindow::onCharacterModeChanged);

    connect(
        relay,
        &ActiveDocumentRelay::multicolorModeToggled,
        _ui->charsetWidget,
        &CharsetWidget::onMulticolorModeToggled);
    connect(
        relay,
        &ActiveDocumentRelay::multicolorModeToggled,
        _ui->tilesetWidget,
        &TilesetWidget::onMulticolorModeToggled);
    connect(relay, &ActiveDocumentRelay::multicolorModeToggled, _ui->mapWidget, &MapWidget::onMulticolorModeToggled);
    connect(relay, &ActiveDocumentRelay::multicolorModeToggled, this, &MainWindow::onMulticolorModeToggled);

    connect(relay, &ActiveDocumentRelay::contentsChanged, this, &MainWindow::documentWasModified);
    connect(relay, &ActiveDocumentRelay::undoIndexChanged, this, [this](int) {
        documentWasModified();
    });
    connect(relay, &ActiveDocumentRelay::undoCleanChanged, this, [this](bool) {
        documentWasModified();
    });
    connect(relay, &ActiveDocumentRelay::canUndoChanged, this, [this](bool) {
        documentWasModified();
    });
    connect(relay, &ActiveDocumentRelay::canRedoChanged, this, [this](bool) {
        documentWasModified();
    });
}

void MainWindow::setupStatusBar()
{
    _labelSelectedColor = new QLabel(tr("Color: Black (0)"), this);
    statusBar()->addPermanentWidget(_labelSelectedColor);

    _labelCharIdx = new QLabel(tr("Char: 000"), this);
    statusBar()->addPermanentWidget(_labelCharIdx);

    _labelTileIdx = new QLabel(tr("Tile: 000"), this);
    statusBar()->addPermanentWidget(_labelTileIdx);

    if (const State* const state = getState()) {
        onColorPropertiesUpdated(state->getSelectedPen());
    }
}

void MainWindow::setupCharsetDock()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    _ui->verticalLayout_charset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_CharsetGrid);
    toolbar->addSeparator();
    AddExpandingToolbarSpacer(toolbar, toolbar);

    _comboBoxCharsetZoom = CreateZoomComboBox(this, _ui->charsetWidget, toolbar);
}

void MainWindow::setupTilesetDock()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    _ui->verticalLayout_tileset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_TilesetGrid);
    toolbar->addSeparator();
    AddExpandingToolbarSpacer(toolbar, toolbar);

    _comboBoxTilesetZoom = CreateZoomComboBox(this, _ui->tilesetWidget, toolbar);
}

void MainWindow::setupMapDock()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    _ui->verticalLayout_map->addWidget(toolbar);

    auto* label = new QLabel(tr("Map Size"), this);
    toolbar->addWidget(label);
    _spinBoxMapX = new QSpinBox(this);
    _spinBoxMapY = new QSpinBox(this);
    const std::array<QSpinBox*, 2> spin_boxes = { _spinBoxMapX, _spinBoxMapY };
    for (QSpinBox* spin_box : spin_boxes) {
        spin_box->setMinimum(1);
        spin_box->setMaximum(State::kMaxMapDim);
        toolbar->addWidget(spin_box);
        spin_box->setKeyboardTracking(false);
    }
    connect(_spinBoxMapX, &QSpinBox::valueChanged, this, &MainWindow::onSpinBoxMapSizeX_valueChanged);
    connect(_spinBoxMapY, &QSpinBox::valueChanged, this, &MainWindow::onSpinBoxMapSizeY_valueChanged);

    toolbar->addSeparator();
    toolbar->addAction(_ui->actionToggle_MapGrid);
    toolbar->addSeparator();

    toolbar->addAction(_ui->actionSelect_Mode);
    toolbar->addAction(_ui->actionPaint_Mode);
    toolbar->addAction(_ui->actionFill_Map);
    _ui->actionSelect_Mode->setChecked(true);

    toolbar->addSeparator();
    toolbar->addAction(_ui->actionClear_Map);
    toolbar->addSeparator();

    AddExpandingToolbarSpacer(toolbar, toolbar);

    _comboBoxMapZoom = CreateZoomComboBox(this, _ui->mapWidget, toolbar);
}

void MainWindow::checkForUpdates()
{
    Preferences& preferences = Preferences::getInstance();
    if (preferences.getCheckUpdates() && preferences.getLastTimeUpdateCheck() >= 7) {
        AutoUpdater::getInstance().checkUpdate();
    }
}

void MainWindow::updateRecentFiles()
{
    const QStringList files = Preferences::getInstance().getRecentFiles();
    const int num_recent_files = std::min(static_cast<int>(files.size()), MAX_RECENT_FILES);

    for (int i = 0; i < num_recent_files; ++i) {
        const qsizetype list_index = static_cast<qsizetype>(i);
        _recentFiles[list_index]->setText(FileUtils::getShortNativePath(files[list_index]));
        _recentFiles[list_index]->setData(files[list_index]);
        _recentFiles[list_index]->setVisible(true);
        _recentFiles[list_index]->setEnabled(QFileInfo::exists(files[list_index]));
    }
    for (int j = num_recent_files; j < MAX_RECENT_FILES; ++j) {
        _recentFiles[static_cast<qsizetype>(j)]->setVisible(false);
    }
    _ui->menuRecentFiles->setEnabled(num_recent_files > 0);
}

void MainWindow::setRecentFile(const QString& file_name)
{
    const QString canonical_file_path = QFileInfo(file_name).canonicalFilePath();
    if (canonical_file_path.isEmpty()) {
        return;
    }

    Preferences& preferences = Preferences::getInstance();
    QStringList files = preferences.getRecentFiles();
    files.removeAll(canonical_file_path);
    files.prepend(canonical_file_path);
    while (files.size() > static_cast<qsizetype>(MAX_RECENT_FILES)) {
        files.removeLast();
    }

    preferences.setRecentFiles(files);
    updateRecentFiles();
}

void MainWindow::setSessionFiles()
{
    QStringList file_list;
    const QList<QMdiSubWindow*> mdi_list = _ui->mdiArea->subWindowList(
        QMdiArea::WindowOrder::StackingOrder);
    for (QMdiSubWindow* sub_window : mdi_list) {
        const DocumentSession* const session = findSessionBySubWindow(sub_window);
        if (session == nullptr || session->isClosing || session->state == nullptr) {
            continue;
        }

        QString filename = session->state->getSavedFilename();
        if (filename.isEmpty()) {
            filename = session->state->getLoadedFilename();
        }
        if (!filename.isEmpty()
            && filename != QLatin1String("untitled")
            && !filename.startsWith(QLatin1String(":/"))) {
            file_list.append(filename);
        }
    }

    Preferences::getInstance().setSessionFiles(file_list);
}

void MainWindow::activateRadioButtonIndex(int pen)
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }

    state->setSelectedPen(pen);
    syncPenSelectionControls(state);
}

void MainWindow::syncPenSelectionControls(const State* state)
{
    if (state == nullptr) {
        return;
    }

    const int pen = std::clamp(state->getSelectedPen(), 0, State::PEN_MAX - 1);

    const std::array<QAction*, State::PEN_MAX> actions = {
        _ui->actionBackground,
        _ui->actionMulti_Color_1,
        _ui->actionMulti_Color_2,
        _ui->actionForeground,
        _ui->actionBackground_3,
    };
    const std::array<QRadioButton*, State::PEN_MAX> radios = {
        _ui->radioButton_background,
        _ui->radioButton_multicolor1,
        _ui->radioButton_multicolor2,
        _ui->radioButton_foreground,
        _ui->radioButton_background3,
    };

    for (size_t i = 0U; i < actions.size(); ++i) {
        const QSignalBlocker action_blocker(actions[i]);
        actions[i]->setChecked(static_cast<int>(i) == pen);
    }

    for (size_t i = 0U; i < radios.size(); ++i) {
        const QSignalBlocker radio_blocker(radios[i]);
        radios[i]->setChecked(static_cast<int>(i) == pen);
    }
}

bool MainWindow::updatePenControls(State* state)
{
    if (state == nullptr) {
        return false;
    }

    const int tile_index = state->getTileIndex();
    const bool is_ecm = state->isExtendedColorMode();
    const bool enable_mc_or_ecm_pens = state->shouldBeDisplayedInMulticolor2(tile_index) || is_ecm;

    _ui->radioButton_multicolor1->setEnabled(enable_mc_or_ecm_pens);
    _ui->radioButton_multicolor2->setEnabled(enable_mc_or_ecm_pens);
    _ui->actionMulti_Color_1->setEnabled(enable_mc_or_ecm_pens);
    _ui->actionMulti_Color_2->setEnabled(enable_mc_or_ecm_pens);

    _ui->radioButton_background3->setEnabled(is_ecm);
    _ui->actionBackground_3->setEnabled(is_ecm);

    const int selected_pen = state->getSelectedPen();
    const bool selected_pen_invalid = ((selected_pen == State::PEN_MULTICOLOR1 || selected_pen == State::PEN_MULTICOLOR2)
                                          && !enable_mc_or_ecm_pens)
        || (selected_pen == State::PEN_BACKGROUND3 && !is_ecm);

    bool fell_back = false;
    if (selected_pen_invalid) {
        state->setSelectedPen(State::PEN_FOREGROUND);
        fell_back = true;
    }
    syncPenSelectionControls(state);
    return fell_back;
}

void MainWindow::updateTileTransformActions(const State* state)
{
    if (state == nullptr) {
        _ui->actionRotate->setEnabled(false);
        _ui->actionFlipHorizontally->setEnabled(false);
        _ui->actionFlipVertically->setEnabled(false);
        _ui->actionShiftLeft->setEnabled(false);
        _ui->actionShiftRight->setEnabled(false);
        _ui->actionShiftUp->setEnabled(false);
        _ui->actionShiftDown->setEnabled(false);
        return;
    }

    const int tile_index = state->getTileIndex();
    const bool can_spatial_transform = !state->hasAliasedSubChars(tile_index);
    _ui->actionRotate->setEnabled(state->canRotateTile(tile_index));
    _ui->actionFlipHorizontally->setEnabled(can_spatial_transform);
    _ui->actionFlipVertically->setEnabled(can_spatial_transform);
    _ui->actionShiftLeft->setEnabled(can_spatial_transform);
    _ui->actionShiftRight->setEnabled(can_spatial_transform);
    _ui->actionShiftUp->setEnabled(can_spatial_transform);
    _ui->actionShiftDown->setEnabled(can_spatial_transform);
}

void MainWindow::activatePalette(int palette_index)
{
    const std::array<QAction*, Palette::kPaletteCount> actions = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5,
    };

    const int clamped_index = std::clamp(palette_index, 0, Palette::kPaletteCount - 1);
    Palette::setActivePalette(clamped_index);

    for (size_t i = 0U; i < actions.size(); ++i) {
        actions[i]->setChecked(static_cast<int>(i) == clamped_index);
    }

    _ui->dockWidget_colors->update();
    _ui->tilesetWidget->update();
    _ui->charsetWidget->update();
    _ui->mapWidget->update();
    for (const DocumentSession& session : _sessions) {
        if (!session.isClosing && session.editorWidget != nullptr) {
            session.editorWidget->update();
        }
    }
}

void MainWindow::updateMenus()
{
    const bool has_documents = hasOpenDocuments();

    _ui->dockWidget_colors->setEnabled(has_documents);
    _ui->dockWidget_charset->setEnabled(has_documents);
    _ui->dockWidget_tileset->setEnabled(has_documents);
    _ui->dockWidget_map->setEnabled(has_documents);
    _ui->dockWidget_tileIndex->setEnabled(has_documents);
    if (_undoView != nullptr) {
        _undoView->setEnabled(has_documents);
    }

    _ui->menuEdit->setEnabled(has_documents);
    _ui->menuTile->setEnabled(has_documents);
    _ui->menuColors->setEnabled(has_documents);

    if (_spinBoxMapX != nullptr) {
        _spinBoxMapX->setEnabled(has_documents);
    }
    if (_spinBoxMapY != nullptr) {
        _spinBoxMapY->setEnabled(has_documents);
    }

    const std::array<QAction*, 10> actions = {
        _ui->actionExport,
        _ui->actionExportAs,
        _ui->actionExportAsImage,
        _ui->actionSave,
        _ui->actionSaveAs,
        _ui->actionClose,
        _ui->actionClose_All,
        _ui->actionCloneCurrentProject,
        _ui->actionCopy,
        _ui->actionCut,
    };
    for (QAction* action : actions) {
        action->setEnabled(has_documents);
    }

    _ui->actionPaste->setEnabled(has_documents && hasValidClipboardPayload());
    if (!has_documents) {
        updateTileTransformActions(nullptr);
    }
}

void MainWindow::updateWindowModifiedFlag()
{
    bool any_modified = false;
    for (const DocumentSession& session : _sessions) {
        if (!session.isClosing && session.state != nullptr && session.state->isModified()) {
            any_modified = true;
            break;
        }
    }
    setWindowModified(any_modified);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    setSessionFiles();
    if (!closeAllDocuments()) {
        event->ignore();
        return;
    }

    event->accept();
    saveSettings();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // Only show the "copy" drop cursor when at least one dragged file can be
    // opened or imported.
    if (event->mimeData()->hasUrls()
        && !FileUtils::classifyDroppedFiles(event->mimeData()->urls()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<FileUtils::DroppedFile> files = FileUtils::classifyDroppedFiles(
        event->mimeData()->urls());
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();

    // Defer processing until the drop completes: error boxes and import
    // dialogs are modal, and running them inside dropEvent() blocks the drag
    // source (Finder/Explorer) until they close.
    QTimer::singleShot(0, this, [this, files]() {
        processDroppedFiles(files);
    });
}

void MainWindow::processDroppedFiles(const QList<FileUtils::DroppedFile>& files)
{
    // Bring the window forward so newly opened documents and import dialogs are
    // visible.
    raise();
    activateWindow();

    for (const FileUtils::DroppedFile& file : files) {
        switch (file.action) {
        case FileUtils::DropAction::Open:
            // Activates already-open documents, reports errors, and updates
            // recent files.
            openFile(file.path);
            break;
        case FileUtils::DropAction::ImportKoala:
            runImportKoalaDialog(file.path);
            break;
        case FileUtils::DropAction::ImportViceSnapshot:
            runImportVICEDialog(file.path);
            break;
        }
    }
}

void MainWindow::runImportVICEDialog(const QString& preselected_path)
{
    ImportVICEDialog dialog(this);
    dialog.setFilepath(preselected_path);
    if (dialog.exec() == QDialog::Accepted) {
        ApplySubWindowPathAndTitle(
            this,
            _ui->mdiArea->currentSubWindow(),
            dialog.getFilepath(),
            QFileInfo(dialog.getFilepath()).baseName());
    }
}

void MainWindow::runImportKoalaDialog(const QString& preselected_path)
{
    ImportKoalaDialog dialog(this);
    dialog.setFilepath(preselected_path);
    if (dialog.exec() == QDialog::Accepted) {
        ApplySubWindowPathAndTitle(
            this,
            _ui->mdiArea->currentSubWindow(),
            dialog.getFilepath(),
            QFileInfo(dialog.getFilepath()).baseName());
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn
        && (watched == _ui->charsetWidget
            || watched == _ui->tilesetWidget
            || watched == _ui->mapWidget)) {
        _lastFocusedEditorWidget = qobject_cast<QWidget*>(watched);
    }
    return QMainWindow::eventFilter(watched, event);
}

BigCharWidget* MainWindow::getBigcharWidget() const
{
    if (_activeRelay != nullptr && _activeRelay->activeState() != nullptr) {
        if (const DocumentSession* const session = findSessionByState(_activeRelay->activeState())) {
            if (!session->isClosing && session->editorWidget != nullptr) {
                return session->editorWidget.data();
            }
        }
    }

    const QList<QMdiSubWindow*> history = _ui->mdiArea->subWindowList(
        QMdiArea::WindowOrder::ActivationHistoryOrder);
    for (auto it = history.crbegin(); it != history.crend(); ++it) {
        const DocumentSession* const session = findSessionBySubWindow(*it);
        if (session != nullptr && !session->isClosing && session->editorWidget != nullptr) {
            return session->editorWidget.data();
        }
    }

    return nullptr;
}

State* MainWindow::getState() const
{
    if (_activeRelay != nullptr && _activeRelay->activeState() != nullptr) {
        if (const DocumentSession* const session = findSessionByState(_activeRelay->activeState())) {
            if (!session->isClosing) {
                return session->state.get();
            }
        }
    }

    if (BigCharWidget* const editor = getBigcharWidget()) {
        return editor->getState();
    }
    return nullptr;
}

State::CopyRange MainWindow::copySelectionToClipboard(const State* state)
{
    State::CopyRange copy_range { };
    copy_range.offset = -1;
    if (state == nullptr) {
        return copy_range;
    }

    if (_lastFocusedEditorWidget == _ui->charsetWidget) {
        _ui->charsetWidget->getSelectionRange(&copy_range);
    } else if (_lastFocusedEditorWidget == _ui->tilesetWidget) {
        _ui->tilesetWidget->getSelectionRange(&copy_range);
    } else if (_lastFocusedEditorWidget == _ui->mapWidget) {
        _ui->mapWidget->getSelectionRange(&copy_range);
    } else {
        return copy_range;
    }

    const ClipboardPayload payload = ClipboardCodec::fromStateSelection(*state, copy_range);
    const QByteArray encoded = ClipboardCodec::serialize(payload);

    auto* mime_data = new QMimeData();
    mime_data->setData(QString::fromLatin1(ClipboardCodec::kMimeType), encoded);
    if (QClipboard* const clipboard = QApplication::clipboard()) {
        clipboard->setMimeData(mime_data);
    } else {
        delete mime_data;
    }

    onClipboardDataChanged();
    return copy_range;
}

bool MainWindow::hasValidClipboardPayload() const
{
    const QClipboard* const clipboard = QApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }

    const QMimeData* const mime_data = clipboard->mimeData();
    return mime_data != nullptr
        && mime_data->hasFormat(QString::fromLatin1(ClipboardCodec::kMimeType));
}

void MainWindow::teardownSessionForSubWindow(QObject* sub_window_obj)
{
    if (_isShuttingDown || sub_window_obj == nullptr) {
        return;
    }

    const auto it = std::find_if(
        _sessions.begin(),
        _sessions.end(),
        [sub_window_obj](const DocumentSession& session) {
            return static_cast<const QObject*>(session.rawSubWindow) == sub_window_obj;
        });
    if (it == _sessions.end()) {
        return;
    }

    const bool was_active = (_activeRelay->activeState() == it->state.get());
    for (const QMetaObject::Connection& conn : it->sessionConnections) {
        QObject::disconnect(conn);
    }
    it->sessionConnections.clear();

    if (was_active) {
        _activeRelay->setActiveState(nullptr);
    }

    _sessions.erase(it);

    if (was_active) {
        onSubWindowActivated(_ui->mdiArea->currentSubWindow());
    } else {
        updateMenus();
        updateWindowModifiedFlag();
    }
}

void MainWindow::on_actionExit_triggered()
{
    setSessionFiles();
    if (closeAllDocuments()) {
        saveSettings();
        QApplication::exit();
    }
}

void MainWindow::on_actionOpen_triggered()
{
    Preferences& preferences = Preferences::getInstance();
    QString filter = preferences.getLastUsedOpenFilter();
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        tr("Select File"),
        preferences.getLastUsedDirectory(),
        tr(
            "All files (*);;"
            "All supported files (*.vchar64proj *.raw *.bin *.fnt *.prg *.64c *.ctm);;"
            "VChar64 Project (*.vchar64proj);;"
            "Raw (*.raw *.bin *.fnt);;"
            "PRG (*.prg *.64c);;"
            "CharPad (*.ctm);;"),
        &filter);

    if (!selected_file.isEmpty()) {
        preferences.setLastUsedOpenFilter(filter);
        openFile(selected_file);
    }
}

bool MainWindow::on_actionSaveAs_triggered()
{
    return saveAsDocument(getState());
}

void MainWindow::on_actionExport_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }

    const QString exported_filename = state->getExportedFilename();
    if (exported_filename.isEmpty()) {
        on_actionExportAs_triggered();
        return;
    }

    const vchar64::Result<void> export_result = state->export_();
    if (export_result.isOk()) {
        statusBar()->showMessage(tr("Export: Ok"), 2000);
    } else {
        statusBar()->showMessage(tr("Export: Error"), 2000);
        QApplication::beep();
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error exporting file: ") + exported_filename + QStringLiteral("\n") + export_result.errorMessage(),
            QMessageBox::Ok);
    }
}

void MainWindow::on_actionExportAs_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }
    ExportDialog dialog(state, this);
    dialog.exec();
}

void MainWindow::on_actionExportAsImage_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }
    ExportImageDialog dialog(state, this);
    dialog.exec();
}

void MainWindow::on_actionInvert_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileInvert(editor->getTileIndex());
    }
}

void MainWindow::on_actionFlipHorizontally_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileFlipHorizontally(editor->getTileIndex());
    }
}

void MainWindow::on_actionFlipVertically_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileFlipVertically(editor->getTileIndex());
    }
}

void MainWindow::on_actionRotate_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileRotate(editor->getTileIndex());
    }
}

void MainWindow::on_actionClearCharacter_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileClear(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftLeft_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftLeft(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftRight_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftRight(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftUp_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftUp(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftDown_triggered()
{
    State* const state = getState();
    const BigCharWidget* const editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftDown(editor->getTileIndex());
    }
}

void MainWindow::on_actionCut_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }

    const State::CopyRange range = copySelectionToClipboard(state);
    if (range.offset != -1) {
        state->cut(range);
    }
}

void MainWindow::on_actionCopy_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }
    static_cast<void>(copySelectionToClipboard(state));
}

void MainWindow::on_actionPaste_triggered()
{
    State* const state = getState();
    if (state == nullptr) {
        return;
    }

    const QClipboard* const clipboard = QApplication::clipboard();
    const QMimeData* const mime_data = (clipboard != nullptr) ? clipboard->mimeData() : nullptr;
    if (mime_data == nullptr || !mime_data->hasFormat(QString::fromLatin1(ClipboardCodec::kMimeType))) {
        QApplication::beep();
        return;
    }

    const QByteArray raw_bytes = mime_data->data(QString::fromLatin1(ClipboardCodec::kMimeType));
    const vchar64::Result<ClipboardPayload> decoded = ClipboardCodec::deserialize(raw_bytes);
    if (!decoded.isOk()) {
        qWarning() << "Rejected clipboard payload:" << decoded.errorMessage();
        QApplication::beep();
        return;
    }

    const ClipboardPayload& payload = decoded.value();
    const State::CopyRange range = payload.toCopyRange();

    const bool target_accepts_chars_or_tiles = (_lastFocusedEditorWidget == _ui->charsetWidget
        || _lastFocusedEditorWidget == _ui->tilesetWidget);
    const bool target_accepts_map = (_lastFocusedEditorWidget == _ui->mapWidget);
    const bool compatible_destination = ((range.type == State::CopyRange::CHARS || range.type == State::CopyRange::TILES)
                                            && target_accepts_chars_or_tiles)
        || (range.type == State::CopyRange::MAP && target_accepts_map);
    if (!compatible_destination) {
        QApplication::beep();
        return;
    }

    if (range.type == State::CopyRange::TILES
        && state->getTileProperties().size != range.tileProperties.size) {
        QApplication::beep();
        QMessageBox warning_box(
            QMessageBox::Warning,
            tr("Application"),
            tr("Could not paste tiles when their sizes are different. Change the tile properties to {%1, %2}")
                .arg(range.tileProperties.size.width())
                .arg(range.tileProperties.size.height()),
            QMessageBox::Ok,
            this);
        warning_box.exec();
        return;
    }

    if (range.type == State::CopyRange::MAP) {
        if (payload.mapCells.empty()) {
            return;
        }
        state->paste(_ui->mapWidget->getCursorPos(), range, payload.mapCells.data());
        return;
    }

    std::array<uint8_t, State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE> combined_buffer { };
    std::copy(payload.charset.begin(), payload.charset.end(), combined_buffer.begin());
    std::copy(
        payload.tileColors.begin(),
        payload.tileColors.end(),
        combined_buffer.begin() + State::CHAR_BUFFER_SIZE);
    state->paste(_ui->charsetWidget->getCursorPos(), range, combined_buffer.data());
}

void MainWindow::on_actionReportBug_triggered()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ricardoquesada/vchar64/issues")));
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog about_dialog(this);
    about_dialog.exec();
}

void MainWindow::on_actionAboutQt_triggered()
{
    QApplication::aboutQt();
}

void MainWindow::on_actionClearRecentFiles_triggered()
{
    Preferences::getInstance().setRecentFiles(QStringList());
    updateRecentFiles();
}

void MainWindow::on_actionEmptyProject_triggered()
{
    auto state = std::make_unique<State>();
    adoptDocument(std::move(state));

    const QString untitled_title = tr("(untitled)");
    ApplySubWindowPathAndTitle(
        this,
        _ui->mdiArea->currentSubWindow(),
        untitled_title,
        untitled_title);
}

void MainWindow::on_actionC64DefaultUppercase_triggered()
{
    auto state = std::make_unique<State>();
    if (!state->openFile(QStringLiteral(":/res/c64-chargen-uppercase.bin")).isOk()) {
        return;
    }

    adoptDocument(std::move(state));
    const QString untitled_title = tr("(untitled)");
    ApplySubWindowPathAndTitle(
        this,
        _ui->mdiArea->currentSubWindow(),
        untitled_title,
        untitled_title);
}

void MainWindow::on_actionC64DefaultLowercase_triggered()
{
    auto state = std::make_unique<State>();
    if (!state->openFile(QStringLiteral(":/res/c64-chargen-lowercase.bin")).isOk()) {
        return;
    }

    adoptDocument(std::move(state));
    const QString untitled_title = tr("(untitled)");
    ApplySubWindowPathAndTitle(
        this,
        _ui->mdiArea->currentSubWindow(),
        untitled_title,
        untitled_title);
}

void MainWindow::on_actionTilesProperties_triggered()
{
    if (State* const state = getState()) {
        TilePropertiesDialog dialog(state, this);
        dialog.exec();
    }
}

void MainWindow::on_actionUndo_triggered()
{
    if (State* const state = getState()) {
        state->undo();
    }
}

void MainWindow::on_actionRedo_triggered()
{
    if (State* const state = getState()) {
        state->redo();
    }
}

void MainWindow::on_radioButton_background_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    activateRadioButtonIndex(State::PEN_BACKGROUND);
}

void MainWindow::on_radioButton_foreground_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    activateRadioButtonIndex(State::PEN_FOREGROUND);
}

void MainWindow::on_radioButton_multicolor1_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    activateRadioButtonIndex(State::PEN_MULTICOLOR1);
}

void MainWindow::on_radioButton_multicolor2_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    activateRadioButtonIndex(State::PEN_MULTICOLOR2);
}

void MainWindow::on_radioButton_background3_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    activateRadioButtonIndex(State::PEN_BACKGROUND3);
}

void MainWindow::on_actionBackground_triggered()
{
    activateRadioButtonIndex(State::PEN_BACKGROUND);
}

void MainWindow::on_actionForeground_triggered()
{
    activateRadioButtonIndex(State::PEN_FOREGROUND);
}

void MainWindow::on_actionMulti_Color_1_triggered()
{
    activateRadioButtonIndex(State::PEN_MULTICOLOR1);
}

void MainWindow::on_actionMulti_Color_2_triggered()
{
    activateRadioButtonIndex(State::PEN_MULTICOLOR2);
}

void MainWindow::on_actionBackground_3_triggered()
{
    activateRadioButtonIndex(State::PEN_BACKGROUND3);
}

void MainWindow::on_radioButton_modeHiRes_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_HIRES);
    }
}

void MainWindow::on_radioButton_modeMulticolor_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_MULTICOLOR);
    }
}

void MainWindow::on_radioButton_modeExtendedColor_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    }
}

void MainWindow::on_actionMode_HiRes_triggered()
{
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_HIRES);
    }
}

void MainWindow::on_actionMode_Multicolor_triggered()
{
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_MULTICOLOR);
    }
}

void MainWindow::on_actionMode_ExtendedColor_triggered()
{
    if (State* const state = getState()) {
        state->setCharacterMode(State::CHARACTER_MODE_EXTENDED_COLOR);
    }
}

void MainWindow::on_actionPalette_0_triggered()
{
    activatePalette(0);
}

void MainWindow::on_actionPalette_1_triggered()
{
    activatePalette(1);
}

void MainWindow::on_actionPalette_2_triggered()
{
    activatePalette(2);
}

void MainWindow::on_actionPalette_3_triggered()
{
    activatePalette(3);
}

void MainWindow::on_actionPalette_4_triggered()
{
    activatePalette(4);
}

void MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ricardoquesada/vchar64/wiki/Documentation")));
}

void MainWindow::on_actionNext_Tile_triggered()
{
    int value = _ui->spinBox_tileIndex->value() + 1;
    if (value > _ui->spinBox_tileIndex->maximum()) {
        value = 0;
    }
    _ui->spinBox_tileIndex->setValue(value);
}

void MainWindow::on_actionPrevious_Tile_triggered()
{
    int value = _ui->spinBox_tileIndex->value() - 1;
    if (value < 0) {
        value = _ui->spinBox_tileIndex->maximum();
    }
    _ui->spinBox_tileIndex->setValue(value);
}

void MainWindow::on_actionImportVICESnapshot_triggered()
{
    runImportVICEDialog(QString());
}

void MainWindow::on_actionReset_Layout_triggered()
{
    Preferences& preferences = Preferences::getInstance();
    const QByteArray geometry = preferences.getMainWindowDefaultGeometry();
    const QByteArray window_state = preferences.getMainWindowDefaultState();
    restoreState(window_state, kStateVersion);
    restoreGeometry(geometry);

    if (const QScreen* const screen = QGuiApplication::primaryScreen()) {
        setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), screen->geometry()));
    }
}

void MainWindow::on_actionClose_triggered()
{
    _ui->mdiArea->closeActiveSubWindow();
}

void MainWindow::on_actionClose_All_triggered()
{
    static_cast<void>(closeAllDocuments());
}

void MainWindow::on_actionServerConnection_triggered()
{
    ServerPreview* const preview = ServerPreview::getInstance();
    if (preview->isConnected() || preview->isConnecting()) {
        preview->disconnectPreview();
        return;
    }

    ServerConnectDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString ip_address = dialog.getIPAddress();
        preview->setActiveState(getState());
        showMessageOnStatusBar(tr("Connecting to %1...").arg(ip_address));
        preview->connectPreview(ip_address);
    }
}

void MainWindow::on_actionImportKoalaImage_triggered()
{
    runImportKoalaDialog(QString());
}

void MainWindow::on_actionImportBinary_triggered()
{
    ImportBinaryDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_radioButton_charColorGlobal_toggled(bool checked)
{
    if (!checked) {
        return;
    }

    if (State* const state = getState()) {
        if (state->getForegroundColorMode() != State::FOREGROUND_COLOR_GLOBAL) {
            state->setForegroundColorMode(State::FOREGROUND_COLOR_GLOBAL);
        }
    }
}

void MainWindow::on_radioButton_charColorPerChar_toggled(bool checked)
{
    if (!checked) {
        return;
    }

    if (State* const state = getState()) {
        if (state->getForegroundColorMode() != State::FOREGROUND_COLOR_PER_TILE) {
            state->setForegroundColorMode(State::FOREGROUND_COLOR_PER_TILE);
        }
    }
}

void MainWindow::on_actionMap_Properties_triggered()
{
    if (State* const state = getState()) {
        MapPropertiesDialog dialog(state, this);
        dialog.exec();
    }
}

void MainWindow::onSpinBoxMapSizeX_valueChanged(int new_value)
{
    if (State* const state = getState()) {
        const QSize current_size = state->getMapSize();
        const QSize new_size(new_value, current_size.height());
        if (new_size != current_size) {
            state->setMapSize(new_size);
        }
    }
}

void MainWindow::onSpinBoxMapSizeY_valueChanged(int new_value)
{
    if (State* const state = getState()) {
        const QSize current_size = state->getMapSize();
        const QSize new_size(current_size.width(), new_value);
        if (new_size != current_size) {
            state->setMapSize(new_size);
        }
    }
}

void MainWindow::onMapSizeUpdated()
{
    const State* const state = getState();
    if (state == nullptr || _spinBoxMapX == nullptr || _spinBoxMapY == nullptr) {
        return;
    }

    const QSize map_size = state->getMapSize();
    const QSignalBlocker block_x(_spinBoxMapX);
    const QSignalBlocker block_y(_spinBoxMapY);
    _spinBoxMapX->setValue(map_size.width());
    _spinBoxMapY->setValue(map_size.height());
}

void MainWindow::on_actionFill_Map_triggered()
{
    _ui->actionPaint_Mode->setChecked(false);
    _ui->actionFill_Map->setChecked(true);
    _ui->actionSelect_Mode->setChecked(false);

    _ui->mapWidget->setMode(MapWidget::FILL_MODE);
}

void MainWindow::on_actionPaint_Mode_triggered()
{
    _ui->actionPaint_Mode->setChecked(true);
    _ui->actionFill_Map->setChecked(false);
    _ui->actionSelect_Mode->setChecked(false);

    _ui->mapWidget->setMode(MapWidget::PAINT_MODE);
}

void MainWindow::on_actionSelect_Mode_triggered()
{
    _ui->actionPaint_Mode->setChecked(false);
    _ui->actionFill_Map->setChecked(false);
    _ui->actionSelect_Mode->setChecked(true);

    _ui->mapWidget->setMode(MapWidget::SELECT_MODE);
}

void MainWindow::on_actionClear_Map_triggered()
{
    if (State* const state = getState()) {
        state->mapClear(state->getTileIndex());
    }
}

void MainWindow::on_actionToggle_MapGrid_triggered()
{
    _ui->mapWidget->enableGrid(_ui->actionToggle_MapGrid->isChecked());
}

void MainWindow::on_actionToggle_CharsetGrid_triggered()
{
    _ui->charsetWidget->enableGrid(_ui->actionToggle_CharsetGrid->isChecked());
}

void MainWindow::on_actionToggle_TilesetGrid_triggered()
{
    _ui->tilesetWidget->enableGrid(_ui->actionToggle_TilesetGrid->isChecked());
}

void MainWindow::on_actionCloneCurrentProject_triggered()
{
    const State* const current_state = getState();
    if (current_state == nullptr) {
        return;
    }

    auto new_state = std::make_unique<State>();
    new_state->copyState(*current_state);
    adoptDocument(std::move(new_state));

    const QString untitled_title = tr("(untitled)");
    ApplySubWindowPathAndTitle(
        this,
        _ui->mdiArea->currentSubWindow(),
        untitled_title,
        untitled_title);
}

void MainWindow::on_actionPreferences_triggered()
{
    PreferencesDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionPalette_5_triggered()
{
    activatePalette(5);
}

MainWindow::DocumentSession* MainWindow::findSessionByState(const State* target_state) noexcept
{
    if (target_state == nullptr) {
        return nullptr;
    }

    for (DocumentSession& session : _sessions) {
        if (session.state.get() == target_state) {
            return &session;
        }
    }
    return nullptr;
}

const MainWindow::DocumentSession* MainWindow::findSessionByState(
    const State* target_state) const noexcept
{
    if (target_state == nullptr) {
        return nullptr;
    }

    for (const DocumentSession& session : _sessions) {
        if (session.state.get() == target_state) {
            return &session;
        }
    }
    return nullptr;
}

MainWindow::DocumentSession* MainWindow::findSessionBySubWindow(
    const QMdiSubWindow* sub_window) noexcept
{
    if (sub_window == nullptr) {
        return nullptr;
    }

    for (DocumentSession& session : _sessions) {
        if (session.rawSubWindow == sub_window && !session.isClosing) {
            return &session;
        }
    }
    return nullptr;
}

const MainWindow::DocumentSession* MainWindow::findSessionBySubWindow(
    const QMdiSubWindow* sub_window) const noexcept
{
    if (sub_window == nullptr) {
        return nullptr;
    }

    for (const DocumentSession& session : _sessions) {
        if (session.rawSubWindow == sub_window && !session.isClosing) {
            return &session;
        }
    }
    return nullptr;
}

MainWindow::DocumentSession* MainWindow::findSessionByEditor(const BigCharWidget* editor) noexcept
{
    if (editor == nullptr) {
        return nullptr;
    }

    for (DocumentSession& session : _sessions) {
        if (session.editorWidget.data() == editor) {
            return &session;
        }
    }
    return nullptr;
}

bool MainWindow::hasOpenDocuments() const noexcept
{
    for (const DocumentSession& session : _sessions) {
        if (!session.isClosing && session.state != nullptr) {
            return true;
        }
    }
    return false;
}
