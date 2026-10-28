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
 * @file mainwindow.cpp
 * @brief Implementation of the MDI `MainWindow` shell, `DocumentSession` RAII lifecycle, and `ClipboardCodec` integration.
 *
 * Key MDI invariants:
 *
 * - **O(1) Active-Document Relay:** `wireActiveDocumentRelay()` binds all global docks,
 *   color swatches, `QUndoView`, and `ServerPreview` once to `_activeRelay`, eliminating
 *   cross-tab repaint pollution and `N x M` signal growth.
 * - **6-Way `actionRotate` Synchronization:** Re-evaluates `State::canRotateTile()` across
 *   active-document switches, tile selection changes, tile dimension changes, Multicolor
 *   toggles, foreground color changes (bit-3 threshold `>= 8`), and project loads.
 * - **Target-Explicit Save/Close:** `saveDocument(State*)`, `saveAsDocument(State*)`, and
 *   `closeAllDocuments()` operate explicitly on the target session rather than assuming
 *   `activeSubWindow()`.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <algorithm>
#include <array>
#include <utility>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QMimeData>
#include <QOverload>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolBar>
#include <QUndoStack>
#include <QUndoView>

#include "aboutdialog.h"
#include "autoupdater.h"
#include "bigcharwidget.h"
#include "core/clipboardcodec.h"
#include "exportdialog.h"
#include "exportimagedialog.h"
#include "fileutils.h"
#include "importbinarydialog.h"
#include "importkoaladialog.h"
#include "importvicedialog.h"
#include "mappropertiesdialog.h"
#include "mapwidget.h"
#include "palette.h"
#include "preferences.h"
#include "preferencesdialog.h"
#include "serverconnectdialog.h"
#include "serverpreview.h"
#include "state.h"
#include "tilepropertiesdialog.h"
#include "ui/activedocumentrelay.h"
#include "updatedialog.h"

namespace {

constexpr int kStateVersion = 11;

} // namespace

constexpr int MainWindow::MAX_RECENT_FILES;

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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _activeRelay(std::make_unique<ActiveDocumentRelay>(this))
    , _ui(new Ui::MainWindow)
{
    setUnifiedTitleAndToolBarOnMac(true);

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
    // Latch `_isShuttingDown` first so child `QMdiSubWindow::destroyed` signals fired
    // during `delete _ui` cannot re-enter `teardownSessionForSubWindow()`.
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

    for (auto& session : _sessions) {
        for (const auto& conn : session.sessionConnections) {
            QObject::disconnect(conn);
        }
        session.sessionConnections.clear();
    }
    _sessions.clear();

    delete _ui;
    _ui = nullptr;
}

void MainWindow::wireActiveDocumentRelay()
{
    auto* relay = _activeRelay.get();

    connect(relay, &ActiveDocumentRelay::activeStateChanged, ServerPreview::getInstance(), &ILivePreviewSink::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->charsetWidget, &CharsetWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->tilesetWidget, &TilesetWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->mapWidget, &MapWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->paletteWidget, &PaletteWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->colorRectWidget_0, &ColorRectWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->colorRectWidget_1, &ColorRectWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->colorRectWidget_2, &ColorRectWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, _ui->colorRectWidget_3, &ColorRectWidget::setActiveState);
    connect(relay, &ActiveDocumentRelay::activeStateChanged, this, &MainWindow::onActiveStateChanged);

    connect(relay, &ActiveDocumentRelay::fileLoaded, this, &MainWindow::refresh);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->tilesetWidget, &TilesetWidget::onFileLoaded);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->charsetWidget, &CharsetWidget::onCharsetUpdated);
    connect(relay, &ActiveDocumentRelay::fileLoaded, _ui->mapWidget, &MapWidget::onFileLoaded);

    connect(relay, &ActiveDocumentRelay::tilePropertiesUpdated, this, &MainWindow::onTilePropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::tilePropertiesUpdated, _ui->tilesetWidget, &TilesetWidget::onTilePropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::tilePropertiesUpdated, _ui->mapWidget, &MapWidget::onTilePropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::tilePropertiesUpdated, _ui->charsetWidget, &CharsetWidget::onTilePropertiesUpdated);

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
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, _ui->charsetWidget, &CharsetWidget::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, _ui->tilesetWidget, &TilesetWidget::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, _ui->mapWidget, &MapWidget::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, _ui->paletteWidget, [this](int) {
        _ui->paletteWidget->update();
    });
    connect(relay, &ActiveDocumentRelay::colorPropertiesUpdated, this, [this](int) {
        _ui->colorRectWidget_0->update();
        _ui->colorRectWidget_1->update();
        _ui->colorRectWidget_2->update();
        _ui->colorRectWidget_3->update();
    });

    connect(relay, &ActiveDocumentRelay::selectedPenChanged, this, &MainWindow::onColorPropertiesUpdated);
    connect(relay, &ActiveDocumentRelay::selectedPenChanged, _ui->paletteWidget, [this](int) {
        _ui->paletteWidget->update();
    });

    connect(relay, &ActiveDocumentRelay::multicolorModeToggled, _ui->charsetWidget, &CharsetWidget::onMulticolorModeToggled);
    connect(relay, &ActiveDocumentRelay::multicolorModeToggled, _ui->tilesetWidget, &TilesetWidget::onMulticolorModeToggled);
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

MainWindow::DocumentSession* MainWindow::findSessionByState(const State* targetState) noexcept
{
    if (targetState == nullptr) {
        return nullptr;
    }

    for (auto& session : _sessions) {
        if (session.state.get() == targetState) {
            return &session;
        }
    }
    return nullptr;
}

const MainWindow::DocumentSession* MainWindow::findSessionByState(const State* targetState) const noexcept
{
    if (targetState == nullptr) {
        return nullptr;
    }

    for (const auto& session : _sessions) {
        if (session.state.get() == targetState) {
            return &session;
        }
    }
    return nullptr;
}

MainWindow::DocumentSession* MainWindow::findSessionBySubWindow(const QMdiSubWindow* subWindow) noexcept
{
    if (subWindow == nullptr) {
        return nullptr;
    }

    for (auto& session : _sessions) {
        if (session.rawSubWindow == subWindow && !session.isClosing) {
            return &session;
        }
    }
    return nullptr;
}

const MainWindow::DocumentSession* MainWindow::findSessionBySubWindow(const QMdiSubWindow* subWindow) const noexcept
{
    if (subWindow == nullptr) {
        return nullptr;
    }

    for (const auto& session : _sessions) {
        if (session.rawSubWindow == subWindow && !session.isClosing) {
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

    for (auto& session : _sessions) {
        if (session.editorWidget.data() == editor) {
            return &session;
        }
    }
    return nullptr;
}

bool MainWindow::hasOpenDocuments() const noexcept
{
    for (const auto& session : _sessions) {
        if (!session.isClosing && session.state != nullptr) {
            return true;
        }
    }
    return false;
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

void MainWindow::updateWindowModifiedFlag()
{
    bool modified = false;
    for (const auto& session : _sessions) {
        if (!session.isClosing && session.state != nullptr && session.state->isModified()) {
            modified = true;
            break;
        }
    }
    setWindowModified(modified);
}

void MainWindow::documentWasModified()
{
    updateWindowModifiedFlag();

    State* state = getState();
    if (state != nullptr) {
        _ui->actionUndo->setEnabled(state->getUndoStack()->canUndo());
        _ui->actionRedo->setEnabled(state->getUndoStack()->canRedo());
    } else {
        _ui->actionUndo->setEnabled(false);
        _ui->actionRedo->setEnabled(false);
    }
}

void MainWindow::syncPenSelectionControls(const State* state)
{
    if (state == nullptr) {
        return;
    }

    const int pen = std::clamp(state->getSelectedPen(), 0, State::PEN_MAX - 1);

    const std::array<QAction*, 4> actions = {
        _ui->actionBackground,
        _ui->actionMulti_Color_1,
        _ui->actionMulti_Color_2,
        _ui->actionForeground,
    };
    const std::array<QRadioButton*, 4> radios = {
        _ui->radioButton_background,
        _ui->radioButton_multicolor1,
        _ui->radioButton_multicolor2,
        _ui->radioButton_foreground,
    };

    for (size_t i = 0U; i < actions.size(); ++i) {
        const QSignalBlocker actionBlocker(actions[i]);
        actions[i]->setChecked(static_cast<int>(i) == pen);
    }

    for (size_t i = 0U; i < radios.size(); ++i) {
        const QSignalBlocker radioBlocker(radios[i]);
        radios[i]->setChecked(static_cast<int>(i) == pen);
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
        _ui->actionRotate->setEnabled(false);
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

void MainWindow::onTileIndexUpdated(int tileIndex)
{
    const State* state = getState();
    const QSignalBlocker blocker(_ui->spinBox_tileIndex);
    _ui->spinBox_tileIndex->setValue(tileIndex);
    _ui->actionRotate->setEnabled(state != nullptr && state->canRotateTile(tileIndex));
}

void MainWindow::onTilePropertiesUpdated()
{
    const State* state = getState();
    if (state == nullptr) {
        _ui->actionRotate->setEnabled(false);
        return;
    }

    const QSize s = state->getTileProperties().size;
    const int tw = std::max(1, s.width());
    const int th = std::max(1, s.height());
    _ui->spinBox_tileIndex->setMaximum(std::max(0, (256 / (tw * th)) - 1));

    // Rotate is only lossless for square high-resolution tiles (Addendum #4).
    _ui->actionRotate->setEnabled(state->canRotateTile(state->getTileIndex()));
}

void MainWindow::onMulticolorModeToggled(bool newvalue)
{
    Q_UNUSED(newvalue)

    const State* state = getState();
    if (state == nullptr) {
        _ui->actionRotate->setEnabled(false);
        return;
    }

    {
        const QSignalBlocker checkBlocker(_ui->checkBox_multicolor);
        const QSignalBlocker actionBlocker(_ui->actionEnable_Multicolor);
        _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());
        _ui->actionEnable_Multicolor->setChecked(state->isMulticolorMode());
    }

    const bool enableRadios = state->shouldBeDisplayedInMulticolor2(state->getTileIndex());
    _ui->radioButton_multicolor1->setEnabled(enableRadios);
    _ui->radioButton_multicolor2->setEnabled(enableRadios);
    _ui->actionMulti_Color_1->setEnabled(enableRadios);
    _ui->actionMulti_Color_2->setEnabled(enableRadios);
    _ui->actionRotate->setEnabled(state->canRotateTile(state->getTileIndex()));

    onColorPropertiesUpdated(state->getSelectedPen());
}

void MainWindow::onColorPropertiesUpdated(int pen)
{
    _ui->dockWidget_colors->update();

    const State* state = getState();
    if (state == nullptr) {
        _ui->actionRotate->setEnabled(false);
        return;
    }

    const int tileIndex = state->getTileIndex();
    int color = state->getColorForPen(pen, tileIndex);
    const int number = color;
    const bool multicolorEnabled = state->shouldBeDisplayedInMulticolor2(tileIndex);
    if (multicolorEnabled && pen == State::PEN_FOREGROUND) {
        color %= 8;
    }

    _labelSelectedColor->setText(
        tr("Color: %1 (%2)").arg(Palette::colorName(color)).arg(number));

    {
        const QSignalBlocker perCharBlocker(_ui->radioButton_charColorPerChar);
        const QSignalBlocker globalBlocker(_ui->radioButton_charColorGlobal);
        if (state->getForegroundColorMode() == State::FOREGROUND_COLOR_PER_TILE) {
            _ui->radioButton_charColorPerChar->setChecked(true);
        } else {
            _ui->radioButton_charColorGlobal->setChecked(true);
        }
    }

    _ui->radioButton_multicolor1->setEnabled(multicolorEnabled);
    _ui->radioButton_multicolor2->setEnabled(multicolorEnabled);
    _ui->actionMulti_Color_1->setEnabled(multicolorEnabled);
    _ui->actionMulti_Color_2->setEnabled(multicolorEnabled);
    _ui->actionRotate->setEnabled(state->canRotateTile(tileIndex));
}

void MainWindow::refresh()
{
    State* state = getState();
    if (state == nullptr) {
        _ui->actionRotate->setEnabled(false);
        return;
    }

    onTilePropertiesUpdated();
    onColorPropertiesUpdated(State::PEN_BACKGROUND);
    onColorPropertiesUpdated(State::PEN_FOREGROUND);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR1);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR2);
    onColorPropertiesUpdated(state->getSelectedPen());

    onMulticolorModeToggled(state->isMulticolorMode());
    onMapSizeUpdated();

    if (_undoView != nullptr) {
        _undoView->setStack(state->getUndoStack());
    }

    _ui->dockWidget_colors->update();
}

bool MainWindow::closeAllDocuments()
{
    const auto subWindows = _ui->mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* subWindow : subWindows) {
        if (subWindow == nullptr) {
            continue;
        }
        const DocumentSession* session = findSessionBySubWindow(subWindow);
        if (session == nullptr || session->isClosing) {
            continue;
        }
        if (!subWindow->close()) {
            return false;
        }
    }
    return !hasOpenDocuments();
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn
        && (watched == _ui->charsetWidget || watched == _ui->tilesetWidget || watched == _ui->mapWidget)) {
        _lastFocusedEditorWidget = qobject_cast<QWidget*>(watched);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createUndoView()
{
    auto* undoDock = new QDockWidget(tr("Undo List"), this);
    undoDock->setObjectName(QStringLiteral("undoDock"));

    State* state = getState();
    if (state != nullptr) {
        _undoView = new QUndoView(state->getUndoStack(), undoDock);
    } else {
        _undoView = new QUndoView(undoDock);
    }

    _undoView->setObjectName(QStringLiteral("undoView"));

    undoDock->setWidget(_undoView);
    undoDock->setFloating(true);
    undoDock->hide();

    _ui->menuView->insertAction(_ui->actionReset_Layout, undoDock->toggleViewAction());
    _ui->menuView->insertSeparator(_ui->actionReset_Layout);
}

void MainWindow::readSettings()
{
    auto& preferences = Preferences::getInstance();
    preferences.readSettings();
    preferences.setMainWindowDefaultGeometry(saveGeometry());
    preferences.setMainWindowDefaultState(saveState(kStateVersion));

    const QByteArray geom = preferences.getMainWindowGeometry();
    const QByteArray winState = preferences.getMainWindowState();

    restoreState(winState, kStateVersion);
    restoreGeometry(geom);

    const std::array<QAction*, Palette::kPaletteCount> actions = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5,
    };
    const int index = std::clamp(preferences.getPalette(), 0, Palette::kPaletteCount - 1);
    actions[static_cast<size_t>(index)]->trigger();
}

void MainWindow::saveSettings()
{
    auto& preferences = Preferences::getInstance();
    preferences.setMainWindowGeometry(saveGeometry());
    preferences.setMainWindowState(saveState(kStateVersion));
    preferences.setPalette(Palette::getActivePalette());
}

void MainWindow::openDefaultDocument()
{
    bool success = false;

    if (Preferences::getInstance().getOpenLastFiles()) {
        const QStringList fileList = Preferences::getInstance().getSessionFiles();
        for (const QString& file : fileList) {
            success = _openFile(file) || success;
        }
    }

    if (!success) {
        on_actionC64DefaultUppercase_triggered();
    }
}

BigCharWidget* MainWindow::createDocument(State* state)
{
    return adoptDocument(std::unique_ptr<State>(state));
}

BigCharWidget* MainWindow::adoptDocument(std::unique_ptr<State> state)
{
    if (!state) {
        return nullptr;
    }

    State* rawState = state.get();
    rawState->clearUndoStack();

    auto* bigcharWidget = new BigCharWidget(rawState, this);
    QMdiSubWindow* subwindow = _ui->mdiArea->addSubWindow(bigcharWidget, Qt::Widget);
    subwindow->setAttribute(Qt::WA_DeleteOnClose);

    DocumentSession session;
    session.state = std::move(state);
    session.subWindow = subwindow;
    session.rawSubWindow = subwindow;
    session.editorWidget = bigcharWidget;

    auto& conns = session.sessionConnections;
    conns.push_back(connect(rawState, &State::fileLoaded, bigcharWidget, &BigCharWidget::onFileLoaded));
    conns.push_back(connect(rawState, &State::tilePropertiesUpdated, bigcharWidget, &BigCharWidget::onTilePropertiesUpdated));
    conns.push_back(connect(rawState, &State::tileUpdated, bigcharWidget, &BigCharWidget::onTileUpdated));
    conns.push_back(connect(rawState, &State::charsetUpdated, bigcharWidget, &BigCharWidget::onCharsetUpdated));
    conns.push_back(connect(rawState, &State::bytesUpdated, bigcharWidget, [bigcharWidget](int, int) {
        bigcharWidget->update();
    }));
    conns.push_back(connect(rawState, &State::colorPropertiesUpdated, bigcharWidget, &BigCharWidget::onColorPropertiesUpdated));
    conns.push_back(connect(rawState, &State::multicolorModeToggled, bigcharWidget, &BigCharWidget::onMulticolorModeToggled));
    conns.push_back(connect(rawState, &State::tileIndexUpdated, bigcharWidget, &BigCharWidget::onTileIndexUpdated));
    conns.push_back(connect(rawState->getUndoStack(), &QUndoStack::cleanChanged, this, [this](bool) {
        updateWindowModifiedFlag();
    }));
    conns.push_back(connect(subwindow, &QObject::destroyed, this, &MainWindow::teardownSessionForSubWindow));

    _sessions.push_back(std::move(session));

    subwindow->showMaximized();
    if (subwindow->layout() != nullptr) {
        subwindow->layout()->setContentsMargins(2, 2, 2, 2);
    }
    _ui->mdiArea->setActiveSubWindow(subwindow);

    // Ensure relay binds immediately even if QMdiArea coalesced subWindowActivated in headless/test runs.
    if (_activeRelay->activeState() != rawState) {
        _activeRelay->setActiveState(rawState);
    }
    updateMenus();

    return bigcharWidget;
}

void MainWindow::notifyEditorClosing(BigCharWidget* editor)
{
    if (_isShuttingDown || editor == nullptr) {
        return;
    }

    DocumentSession* closingSession = findSessionByEditor(editor);
    if (closingSession == nullptr || closingSession->isClosing) {
        return;
    }

    closingSession->isClosing = true;

    if (_activeRelay->activeState() == closingSession->state.get()) {
        State* fallbackState = nullptr;
        QMdiSubWindow* fallbackSubWindow = nullptr;
        const auto history = _ui->mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            DocumentSession* candidate = findSessionBySubWindow(*it);
            if (candidate != nullptr && !candidate->isClosing && candidate->state != nullptr) {
                fallbackState = candidate->state.get();
                fallbackSubWindow = candidate->subWindow.data();
                break;
            }
        }

        if (fallbackSubWindow != nullptr) {
            setWindowFilePath(fallbackSubWindow->windowFilePath());
        } else {
            setWindowFilePath(QString());
        }
        _activeRelay->setActiveState(fallbackState);
    }

    updateMenus();
    updateWindowModifiedFlag();
}

void MainWindow::teardownSessionForSubWindow(QObject* subWindowObj)
{
    if (_isShuttingDown || subWindowObj == nullptr) {
        return;
    }

    const auto it = std::find_if(_sessions.begin(), _sessions.end(), [subWindowObj](const DocumentSession& s) {
        return static_cast<const QObject*>(s.rawSubWindow) == subWindowObj;
    });
    if (it == _sessions.end()) {
        return;
    }

    const bool wasActive = (_activeRelay->activeState() == it->state.get());
    for (const auto& conn : it->sessionConnections) {
        QObject::disconnect(conn);
    }
    it->sessionConnections.clear();

    if (wasActive) {
        _activeRelay->setActiveState(nullptr);
    }

    _sessions.erase(it);

    if (wasActive) {
        onSubWindowActivated(_ui->mdiArea->currentSubWindow());
    } else {
        updateMenus();
        updateWindowModifiedFlag();
    }
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

    auto* serverPreview = ServerPreview::getInstance();
    connect(serverPreview, &ILivePreviewSink::previewConnected, this, &MainWindow::serverConnected);
    connect(serverPreview, &ILivePreviewSink::previewDisconnected, this, &MainWindow::serverDisconnected);
    connect(serverPreview, &ILivePreviewSink::previewError, this, &MainWindow::serverPreviewError);

    auto& autoUpdater = AutoUpdater::getInstance();
    connect(&autoUpdater, &AutoUpdater::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(&autoUpdater, &AutoUpdater::upToDate, this, &MainWindow::onUpdateUpToDate);
    connect(&autoUpdater, &AutoUpdater::checkFailed, this, &MainWindow::onUpdateCheckFailed);

    connect(_ui->mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);
    connect(_ui->spinBox_tileIndex, &QSpinBox::valueChanged, this, &MainWindow::onSpinBoxValueChanged);

    if (QClipboard* clipboard = QApplication::clipboard()) {
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

void MainWindow::setupCharsetDock()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    _ui->verticalLayout_charset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_CharsetGrid);
    toolbar->addSeparator();

    auto* empty = new QWidget(toolbar);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxCharsetZoom = new QComboBox(this);
    _comboBoxCharsetZoom->addItem(QStringLiteral("200%"), QVariant(200));
    _comboBoxCharsetZoom->addItem(QStringLiteral("175%"), QVariant(175));
    _comboBoxCharsetZoom->addItem(QStringLiteral("150%"), QVariant(150));
    _comboBoxCharsetZoom->addItem(QStringLiteral("125%"), QVariant(125));
    _comboBoxCharsetZoom->addItem(QStringLiteral("100%"), QVariant(100));
    _comboBoxCharsetZoom->addItem(QStringLiteral("75%"), QVariant(75));
    _comboBoxCharsetZoom->addItem(QStringLiteral("50%"), QVariant(50));
    _comboBoxCharsetZoom->addItem(QStringLiteral("25%"), QVariant(25));
    _comboBoxCharsetZoom->setCurrentIndex(4);
    toolbar->addWidget(_comboBoxCharsetZoom);

    connect(_comboBoxCharsetZoom, &QComboBox::activated, this, [this](int index) {
        const QVariant variant = _comboBoxCharsetZoom->itemData(index);
        _ui->charsetWidget->setZoomLevel(variant.toInt());
    });
}

void MainWindow::setupTilesetDock()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    _ui->verticalLayout_tileset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_TilesetGrid);
    toolbar->addSeparator();

    auto* empty = new QWidget(toolbar);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxTilesetZoom = new QComboBox(this);
    _comboBoxTilesetZoom->addItem(QStringLiteral("200%"), QVariant(200));
    _comboBoxTilesetZoom->addItem(QStringLiteral("175%"), QVariant(175));
    _comboBoxTilesetZoom->addItem(QStringLiteral("150%"), QVariant(150));
    _comboBoxTilesetZoom->addItem(QStringLiteral("125%"), QVariant(125));
    _comboBoxTilesetZoom->addItem(QStringLiteral("100%"), QVariant(100));
    _comboBoxTilesetZoom->addItem(QStringLiteral("75%"), QVariant(75));
    _comboBoxTilesetZoom->addItem(QStringLiteral("50%"), QVariant(50));
    _comboBoxTilesetZoom->addItem(QStringLiteral("25%"), QVariant(25));
    _comboBoxTilesetZoom->setCurrentIndex(4);
    toolbar->addWidget(_comboBoxTilesetZoom);

    connect(_comboBoxTilesetZoom, &QComboBox::activated, this, [this](int index) {
        const QVariant variant = _comboBoxTilesetZoom->itemData(index);
        _ui->tilesetWidget->setZoomLevel(variant.toInt());
    });
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
    const std::array<QSpinBox*, 2> spins = { _spinBoxMapX, _spinBoxMapY };
    for (QSpinBox* spin : spins) {
        spin->setMinimum(1);
        spin->setMaximum(State::kMaxMapDim);
        toolbar->addWidget(spin);
        spin->setKeyboardTracking(false);
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

    auto* empty = new QWidget(toolbar);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxMapZoom = new QComboBox(this);
    _comboBoxMapZoom->addItem(QStringLiteral("200%"), QVariant(200));
    _comboBoxMapZoom->addItem(QStringLiteral("175%"), QVariant(175));
    _comboBoxMapZoom->addItem(QStringLiteral("150%"), QVariant(150));
    _comboBoxMapZoom->addItem(QStringLiteral("125%"), QVariant(125));
    _comboBoxMapZoom->addItem(QStringLiteral("100%"), QVariant(100));
    _comboBoxMapZoom->addItem(QStringLiteral("75%"), QVariant(75));
    _comboBoxMapZoom->addItem(QStringLiteral("50%"), QVariant(50));
    _comboBoxMapZoom->addItem(QStringLiteral("25%"), QVariant(25));
    _comboBoxMapZoom->setCurrentIndex(4);
    toolbar->addWidget(_comboBoxMapZoom);

    connect(_comboBoxMapZoom, &QComboBox::activated, this, [this](int index) {
        const QVariant variant = _comboBoxMapZoom->itemData(index);
        _ui->mapWidget->setZoomLevel(variant.toInt());
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

    if (const State* state = getState()) {
        onColorPropertiesUpdated(state->getSelectedPen());
    }
}

void MainWindow::onClipboardDataChanged()
{
    _ui->actionPaste->setEnabled(hasOpenDocuments() && hasValidClipboardPayload());
}

void MainWindow::updateMenus()
{
    const bool withDocuments = hasOpenDocuments();

    _ui->dockWidget_colors->setEnabled(withDocuments);
    _ui->dockWidget_charset->setEnabled(withDocuments);
    _ui->dockWidget_tileset->setEnabled(withDocuments);
    _ui->dockWidget_map->setEnabled(withDocuments);
    _ui->dockWidget_tileIndex->setEnabled(withDocuments);
    if (_undoView != nullptr) {
        _undoView->setEnabled(withDocuments);
    }

    _ui->menuEdit->setEnabled(withDocuments);
    _ui->menuTile->setEnabled(withDocuments);
    _ui->menuColors->setEnabled(withDocuments);

    if (_spinBoxMapX != nullptr) {
        _spinBoxMapX->setEnabled(withDocuments);
    }
    if (_spinBoxMapY != nullptr) {
        _spinBoxMapY->setEnabled(withDocuments);
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
        action->setEnabled(withDocuments);
    }

    _ui->actionPaste->setEnabled(withDocuments && hasValidClipboardPayload());
    if (!withDocuments) {
        _ui->actionRotate->setEnabled(false);
    }
}

void MainWindow::updateRecentFiles()
{
    const QStringList files = Preferences::getInstance().getRecentFiles();
    const int numRecentFiles = std::min(static_cast<int>(files.size()), MAX_RECENT_FILES);

    for (int i = 0; i < numRecentFiles; ++i) {
        const qsizetype qIdx = static_cast<qsizetype>(i);
        _recentFiles[qIdx]->setText(FileUtils::getShortNativePath(files[qIdx]));
        _recentFiles[qIdx]->setData(files[qIdx]);
        _recentFiles[qIdx]->setVisible(true);
        _recentFiles[qIdx]->setEnabled(QFileInfo::exists(files[qIdx]));
    }
    for (int j = numRecentFiles; j < MAX_RECENT_FILES; ++j) {
        _recentFiles[static_cast<qsizetype>(j)]->setVisible(false);
    }
    _ui->menuRecentFiles->setEnabled(numRecentFiles > 0);
}

void MainWindow::setRecentFile(const QString& fileName)
{
    const QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();
    if (canonicalFilePath.isEmpty()) {
        return;
    }

    auto& preferences = Preferences::getInstance();
    QStringList files = preferences.getRecentFiles();
    files.removeAll(canonicalFilePath);
    files.prepend(canonicalFilePath);
    while (files.size() > static_cast<qsizetype>(MAX_RECENT_FILES)) {
        files.removeLast();
    }

    preferences.setRecentFiles(files);
    updateRecentFiles();
}

void MainWindow::setSessionFiles()
{
    QStringList fileList;
    const auto mdiList = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::StackingOrder);
    for (QMdiSubWindow* item : mdiList) {
        const DocumentSession* session = findSessionBySubWindow(item);
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
            fileList.append(filename);
        }
    }

    Preferences::getInstance().setSessionFiles(fileList);
}

void MainWindow::showMessageOnStatusBar(const QString& message)
{
    statusBar()->showMessage(message, 3000);
}

void MainWindow::onCharIndexUpdated(int charIndex)
{
    const State* state = getState();
    if (state == nullptr) {
        return;
    }

    _labelCharIdx->setText(tr("Char: %1").arg(charIndex, 3, 10, QLatin1Char(' ')));

    const int tileIndex = state->getTileIndexFromCharIndex(charIndex);
    _labelTileIdx->setText(tr("Tile: %1").arg(tileIndex, 3, 10, QLatin1Char(' ')));
}

void MainWindow::on_actionExit_triggered()
{
    setSessionFiles();
    if (closeAllDocuments()) {
        saveSettings();
        QApplication::exit();
    }
}

void MainWindow::on_actionEmptyProject_triggered()
{
    auto state = std::make_unique<State>();
    adoptDocument(std::move(state));

    setWindowFilePath(tr("(untitled)"));
    if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
        sub->setWindowFilePath(tr("(untitled)"));
        sub->setWindowTitle(tr("(untitled)"));
    }
}

void MainWindow::on_actionC64DefaultUppercase_triggered()
{
    auto state = std::make_unique<State>();
    if (state->openFile(QStringLiteral(":/res/c64-chargen-uppercase.bin")).isOk()) {
        adoptDocument(std::move(state));
        setWindowFilePath(tr("(untitled)"));
        if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
            sub->setWindowFilePath(tr("(untitled)"));
            sub->setWindowTitle(tr("(untitled)"));
        }
    }
}

void MainWindow::on_actionC64DefaultLowercase_triggered()
{
    auto state = std::make_unique<State>();
    if (state->openFile(QStringLiteral(":/res/c64-chargen-lowercase.bin")).isOk()) {
        adoptDocument(std::move(state));
        setWindowFilePath(tr("(untitled)"));
        if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
            sub->setWindowFilePath(tr("(untitled)"));
            sub->setWindowTitle(tr("(untitled)"));
        }
    }
}

void MainWindow::on_actionPreferences_triggered()
{
    PreferencesDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_checkBox_multicolor_toggled(bool checked)
{
    State* state = getState();
    if (state != nullptr && checked != state->isMulticolorMode()) {
        _ui->radioButton_multicolor1->setEnabled(checked);
        _ui->radioButton_multicolor2->setEnabled(checked);
        _ui->actionMulti_Color_1->setEnabled(checked);
        _ui->actionMulti_Color_2->setEnabled(checked);
        _ui->actionEnable_Multicolor->setChecked(checked);

        state->setMulticolorMode(checked);
    }
}

void MainWindow::activateRadioButtonIndex(int pen)
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }

    state->setSelectedPen(pen);
    syncPenSelectionControls(state);
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

void MainWindow::on_radioButton_charColorGlobal_toggled(bool checked)
{
    if (!checked) {
        return;
    }

    if (State* state = getState()) {
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

    if (State* state = getState()) {
        if (state->getForegroundColorMode() != State::FOREGROUND_COLOR_PER_TILE) {
            state->setForegroundColorMode(State::FOREGROUND_COLOR_PER_TILE);
        }
    }
}

void MainWindow::activatePalette(int paletteIndex)
{
    const std::array<QAction*, Palette::kPaletteCount> actions = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5,
    };

    const int clampedIndex = std::clamp(paletteIndex, 0, Palette::kPaletteCount - 1);
    Palette::setActivePalette(clampedIndex);

    for (size_t i = 0U; i < actions.size(); ++i) {
        actions[i]->setChecked(static_cast<int>(i) == clampedIndex);
    }

    _ui->dockWidget_colors->update();
    _ui->tilesetWidget->update();
    _ui->charsetWidget->update();
    _ui->mapWidget->update();
    for (const auto& session : _sessions) {
        if (!session.isClosing && session.editorWidget != nullptr) {
            session.editorWidget->update();
        }
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

void MainWindow::on_actionPalette_5_triggered()
{
    activatePalette(5);
}

bool MainWindow::openFile(const QString& path)
{
    const QFileInfo info(path);
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    if (activateIfAlreadyOpen(info.canonicalFilePath())) {
        return true;
    }

    const bool ret = _openFile(path);
    if (!ret) {
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error loading '") + info.fileName() + QStringLiteral("'\n") + statusBar()->currentMessage(),
            QMessageBox::Ok);
    }

    return ret;
}

bool MainWindow::_openFile(const QString& path)
{
    const QFileInfo info(path);
    auto state = std::make_unique<State>();
    const auto result = state->openFile(path);
    if (result.isOk()) {
        const bool isMulticolor = state->isMulticolorMode();
        adoptDocument(std::move(state));
        setRecentFile(path);

        {
            const QSignalBlocker blocker(_ui->checkBox_multicolor);
            _ui->checkBox_multicolor->setChecked(isMulticolor);
        }

        if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
            sub->setWindowFilePath(info.filePath());
            sub->setWindowTitle(info.baseName());
        }
        setWindowFilePath(info.filePath());

        if (!result.value().warnings.isEmpty()) {
            statusBar()->showMessage(result.value().warnings.join(QStringLiteral("; ")), 5000);
        }
    } else {
        statusBar()->showMessage(result.errorMessage(), 5000);
    }
    return result.isOk();
}

bool MainWindow::activateIfAlreadyOpen(const QString& fileName)
{
    if (fileName.isEmpty()) {
        return false;
    }

    for (const auto& session : _sessions) {
        if (session.isClosing || session.state == nullptr || session.subWindow == nullptr) {
            continue;
        }
        const QString loadedCanonical = QFileInfo(session.state->getLoadedFilename()).canonicalFilePath();
        const QString savedCanonical = QFileInfo(session.state->getSavedFilename()).canonicalFilePath();
        if ((!loadedCanonical.isEmpty() && fileName == loadedCanonical)
            || (!savedCanonical.isEmpty() && fileName == savedCanonical)) {
            _ui->mdiArea->setActiveSubWindow(session.subWindow.data());
            session.subWindow->setFocus();
            return true;
        }
    }
    return false;
}

void MainWindow::on_actionOpen_triggered()
{
    auto& preferences = Preferences::getInstance();
    QString filter = preferences.getLastUsedOpenFilter();
    const QString fn = QFileDialog::getOpenFileName(
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

    if (!fn.isEmpty()) {
        preferences.setLastUsedOpenFilter(filter);
        openFile(fn);
    }
}

void MainWindow::on_actionCloneCurrentProject_triggered()
{
    const State* currentState = getState();
    if (currentState == nullptr) {
        return;
    }

    auto newState = std::make_unique<State>();
    newState->copyState(*currentState);
    adoptDocument(std::move(newState));

    setWindowFilePath(tr("(untitled)"));
    if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
        sub->setWindowFilePath(tr("(untitled)"));
        sub->setWindowTitle(tr("(untitled)"));
    }
}

void MainWindow::on_actionImportVICESnapshot_triggered()
{
    ImportVICEDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
            sub->setWindowFilePath(dialog.getFilepath());
            sub->setWindowTitle(QFileInfo(dialog.getFilepath()).baseName());
        }
        setWindowFilePath(dialog.getFilepath());
    }
}

void MainWindow::on_actionImportKoalaImage_triggered()
{
    ImportKoalaDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (QMdiSubWindow* sub = _ui->mdiArea->currentSubWindow()) {
            sub->setWindowFilePath(dialog.getFilepath());
            sub->setWindowTitle(QFileInfo(dialog.getFilepath()).baseName());
        }
        setWindowFilePath(dialog.getFilepath());
    }
}

void MainWindow::on_actionImportBinary_triggered()
{
    ImportBinaryDialog dialog(this);
    dialog.exec();
}

bool MainWindow::saveAsDocument(State* targetState)
{
    if (targetState == nullptr) {
        return false;
    }

    QString suggestedPath = targetState->getSavedFilename();
    if (suggestedPath.isEmpty()) {
        suggestedPath = targetState->getLoadedFilename();
    }

    if (!suggestedPath.isEmpty() && !suggestedPath.startsWith(QLatin1String(":/"))) {
        const QFileInfo fileInfo(suggestedPath);
        if (fileInfo.suffix() != QLatin1String("vchar64proj")) {
            suggestedPath = FileUtils::replaceExtension(suggestedPath, QStringLiteral("vchar64proj"));
        }
    } else {
        suggestedPath = QDir(Preferences::getInstance().getLastUsedDirectory())
                            .filePath(QStringLiteral("untitled.vchar64proj"));
    }

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Save Project"),
        suggestedPath,
        tr("VChar64 project(*.vchar64proj)"));
    if (filename.isEmpty()) {
        return false;
    }

    const auto saveResult = targetState->saveProject(filename);
    if (!saveResult.isOk()) {
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error saving project file: ") + filename + QStringLiteral("\n") + saveResult.errorMessage(),
            QMessageBox::Ok);
        return false;
    }

    const QFileInfo fi(filename);
    Preferences::getInstance().setLastUsedDirectory(fi.absolutePath());

    if (DocumentSession* session = findSessionByState(targetState)) {
        if (session->subWindow != nullptr) {
            session->subWindow->setWindowFilePath(fi.filePath());
            session->subWindow->setWindowTitle(fi.baseName());
        }
    }
    if (targetState == getState()) {
        setWindowFilePath(fi.filePath());
    }

    setRecentFile(filename);
    updateWindowModifiedFlag();
    return true;
}

bool MainWindow::saveDocument(State* targetState)
{
    if (targetState == nullptr) {
        return false;
    }

    const QString filename = targetState->getSavedFilename();
    if (filename.isEmpty()) {
        return saveAsDocument(targetState);
    }

    const auto saveResult = targetState->saveProject(filename);
    if (!saveResult.isOk()) {
        statusBar()->showMessage(tr("Error saving file"), 2000);
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error saving project file: ") + filename + QStringLiteral("\n") + saveResult.errorMessage(),
            QMessageBox::Ok);
        return false;
    }

    statusBar()->showMessage(tr("File saved to %1").arg(targetState->getSavedFilename()), 2000);
    updateWindowModifiedFlag();
    return true;
}

bool MainWindow::on_actionSaveAs_triggered()
{
    return saveAsDocument(getState());
}

bool MainWindow::on_actionSave_triggered()
{
    return saveDocument(getState());
}

void MainWindow::on_actionExport_triggered()
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }

    const QString exportedFilename = state->getExportedFilename();
    if (exportedFilename.isEmpty()) {
        on_actionExportAs_triggered();
        return;
    }

    const auto exportResult = state->export_();
    if (exportResult.isOk()) {
        statusBar()->showMessage(tr("Export: Ok"), 2000);
    } else {
        statusBar()->showMessage(tr("Export: Error"), 2000);
        QApplication::beep();
        QMessageBox::warning(
            this,
            tr("Application"),
            tr("Error exporting file: ") + exportedFilename + QStringLiteral("\n") + exportResult.errorMessage(),
            QMessageBox::Ok);
    }
}

void MainWindow::on_actionExportAs_triggered()
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }
    ExportDialog dialog(state, this);
    dialog.exec();
}

void MainWindow::on_actionExportAsImage_triggered()
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }
    ExportImageDialog dialog(state, this);
    dialog.exec();
}

void MainWindow::on_actionClose_triggered()
{
    _ui->mdiArea->closeActiveSubWindow();
}

void MainWindow::on_actionClose_All_triggered()
{
    (void)closeAllDocuments();
}

void MainWindow::on_actionInvert_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileInvert(editor->getTileIndex());
    }
}

void MainWindow::on_actionFlipHorizontally_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileFlipHorizontally(editor->getTileIndex());
    }
}

void MainWindow::on_actionFlipVertically_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileFlipVertically(editor->getTileIndex());
    }
}

void MainWindow::on_actionRotate_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileRotate(editor->getTileIndex());
    }
}

void MainWindow::on_actionClearCharacter_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileClear(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftLeft_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftLeft(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftRight_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftRight(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftUp_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftUp(editor->getTileIndex());
    }
}

void MainWindow::on_actionShiftDown_triggered()
{
    State* state = getState();
    BigCharWidget* editor = getBigcharWidget();
    if (state != nullptr && editor != nullptr) {
        state->tileShiftDown(editor->getTileIndex());
    }
}

void MainWindow::on_actionCut_triggered()
{
    State* state = getState();
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
    State* state = getState();
    if (state == nullptr) {
        return;
    }
    (void)copySelectionToClipboard(state);
}

void MainWindow::on_actionPaste_triggered()
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }

    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = (clipboard != nullptr) ? clipboard->mimeData() : nullptr;
    if (mimeData == nullptr || !mimeData->hasFormat(QString::fromLatin1(ClipboardCodec::kMimeType))) {
        QApplication::beep();
        return;
    }

    const QByteArray rawBytes = mimeData->data(QString::fromLatin1(ClipboardCodec::kMimeType));
    const auto decoded = ClipboardCodec::deserialize(rawBytes);
    if (!decoded.isOk()) {
        qWarning() << "Rejected clipboard payload:" << decoded.errorMessage();
        QApplication::beep();
        return;
    }

    const ClipboardPayload& payload = decoded.value();
    const State::CopyRange range = payload.toCopyRange();

    const bool targetAcceptsCharsOrTiles = (_lastFocusedEditorWidget == _ui->charsetWidget
        || _lastFocusedEditorWidget == _ui->tilesetWidget);
    const bool targetAcceptsMap = (_lastFocusedEditorWidget == _ui->mapWidget);
    const bool compatibleDestination = ((range.type == State::CopyRange::CHARS || range.type == State::CopyRange::TILES) && targetAcceptsCharsOrTiles)
        || (range.type == State::CopyRange::MAP && targetAcceptsMap);
    if (!compatibleDestination) {
        QApplication::beep();
        return;
    }

    if (range.type == State::CopyRange::TILES && state->getTileProperties().size != range.tileProperties.size) {
        QApplication::beep();
        QMessageBox msgBox(
            QMessageBox::Warning,
            tr("Application"),
            tr("Could not paste tiles when their sizes are different. Change the tile properties to {%1, %2}")
                .arg(range.tileProperties.size.width())
                .arg(range.tileProperties.size.height()),
            QMessageBox::Ok,
            this);
        msgBox.exec();
        return;
    }

    if (range.type == State::CopyRange::MAP) {
        if (payload.mapCells.empty()) {
            return;
        }
        state->paste(_ui->mapWidget->getCursorPos(), range, payload.mapCells.data());
    } else {
        std::array<uint8_t, State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE> combinedBuffer { };
        std::copy(payload.charset.begin(), payload.charset.end(), combinedBuffer.begin());
        std::copy(
            payload.tileColors.begin(),
            payload.tileColors.end(),
            combinedBuffer.begin() + State::CHAR_BUFFER_SIZE);
        state->paste(_ui->charsetWidget->getCursorPos(), range, combinedBuffer.data());
    }
}

void MainWindow::on_actionUndo_triggered()
{
    if (State* state = getState()) {
        state->undo();
    }
}

void MainWindow::on_actionRedo_triggered()
{
    if (State* state = getState()) {
        state->redo();
    }
}

void MainWindow::on_actionReportBug_triggered()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ricardoquesada/vchar64/issues")));
}

void MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ricardoquesada/vchar64/wiki/Documentation")));
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
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

void MainWindow::onOpenRecentFileTriggered()
{
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    const QString path = action->data().toString();
    if (!openFile(path)) {
        auto& preferences = Preferences::getInstance();
        QStringList files = preferences.getRecentFiles();
        files.removeAll(path);
        preferences.setRecentFiles(files);
        updateRecentFiles();
    }
}

void MainWindow::on_actionTilesProperties_triggered()
{
    if (State* state = getState()) {
        TilePropertiesDialog dialog(state, this);
        dialog.exec();
    }
}

void MainWindow::on_actionMap_Properties_triggered()
{
    if (State* state = getState()) {
        MapPropertiesDialog dialog(state, this);
        dialog.exec();
    }
}

void MainWindow::onSpinBoxMapSizeX_valueChanged(int newValue)
{
    if (State* state = getState()) {
        const QSize currentSize = state->getMapSize();
        const QSize newSize(newValue, currentSize.height());
        if (newSize != currentSize) {
            state->setMapSize(newSize);
        }
    }
}

void MainWindow::onSpinBoxMapSizeY_valueChanged(int newValue)
{
    if (State* state = getState()) {
        const QSize currentSize = state->getMapSize();
        const QSize newSize(currentSize.width(), newValue);
        if (newSize != currentSize) {
            state->setMapSize(newSize);
        }
    }
}

void MainWindow::onMapSizeUpdated()
{
    const State* state = getState();
    if (state == nullptr || _spinBoxMapX == nullptr || _spinBoxMapY == nullptr) {
        return;
    }

    const QSize mapSize = state->getMapSize();
    const QSignalBlocker blockX(_spinBoxMapX);
    const QSignalBlocker blockY(_spinBoxMapY);
    _spinBoxMapX->setValue(mapSize.width());
    _spinBoxMapY->setValue(mapSize.height());
}

void MainWindow::on_actionServerConnection_triggered()
{
    auto* preview = ServerPreview::getInstance();
    if (preview->isConnected() || preview->isConnecting()) {
        preview->disconnectPreview();
    } else {
        ServerConnectDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            const QString ipaddress = dialog.getIPAddress();
            preview->setActiveState(getState());
            showMessageOnStatusBar(tr("Connecting to %1...").arg(ipaddress));
            preview->connectPreview(ipaddress);
        }
    }
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

void MainWindow::on_actionReset_Layout_triggered()
{
    auto& preferences = Preferences::getInstance();
    const QByteArray geom = preferences.getMainWindowDefaultGeometry();
    const QByteArray winState = preferences.getMainWindowDefaultState();
    restoreState(winState, kStateVersion);
    restoreGeometry(geom);

    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), screen->geometry()));
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
        const auto history = _ui->mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            DocumentSession* candidate = findSessionBySubWindow(*it);
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

void MainWindow::onSpinBoxValueChanged(int tileIndex)
{
    State* state = getState();
    if (state == nullptr) {
        return;
    }

    state->setTileIndex(tileIndex);

    if (_lastFocusedEditorWidget == _ui->mapWidget && _ui->mapWidget->getMode() == MapWidget::SELECT_MODE) {
        state->mapPaint(_ui->mapWidget->getCursorCoord(), tileIndex, false);
    }
}

void MainWindow::on_actionClear_Map_triggered()
{
    if (State* state = getState()) {
        state->mapClear(state->getTileIndex());
    }
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

BigCharWidget* MainWindow::getBigcharWidget() const
{
    if (_activeRelay != nullptr && _activeRelay->activeState() != nullptr) {
        if (const DocumentSession* session = findSessionByState(_activeRelay->activeState())) {
            if (!session->isClosing && session->editorWidget != nullptr) {
                return session->editorWidget.data();
            }
        }
    }

    const auto history = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::ActivationHistoryOrder);
    for (auto it = history.crbegin(); it != history.crend(); ++it) {
        const DocumentSession* session = findSessionBySubWindow(*it);
        if (session != nullptr && !session->isClosing && session->editorWidget != nullptr) {
            return session->editorWidget.data();
        }
    }

    return nullptr;
}

State* MainWindow::getState() const
{
    if (_activeRelay != nullptr && _activeRelay->activeState() != nullptr) {
        if (const DocumentSession* session = findSessionByState(_activeRelay->activeState())) {
            if (!session->isClosing) {
                return session->state.get();
            }
        }
    }

    if (BigCharWidget* editor = getBigcharWidget()) {
        return editor->getState();
    }
    return nullptr;
}

bool MainWindow::hasValidClipboardPayload() const
{
    const QClipboard* clipboard = QApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }

    const QMimeData* mimeData = clipboard->mimeData();
    return mimeData != nullptr && mimeData->hasFormat(QString::fromLatin1(ClipboardCodec::kMimeType));
}

State::CopyRange MainWindow::copySelectionToClipboard(const State* state)
{
    State::CopyRange copyRange { };
    copyRange.offset = -1;
    if (state == nullptr) {
        return copyRange;
    }

    if (_lastFocusedEditorWidget == _ui->charsetWidget) {
        _ui->charsetWidget->getSelectionRange(&copyRange);
    } else if (_lastFocusedEditorWidget == _ui->tilesetWidget) {
        _ui->tilesetWidget->getSelectionRange(&copyRange);
    } else if (_lastFocusedEditorWidget == _ui->mapWidget) {
        _ui->mapWidget->getSelectionRange(&copyRange);
    } else {
        return copyRange;
    }

    const ClipboardPayload payload = ClipboardCodec::fromStateSelection(*state, copyRange);
    const QByteArray encoded = ClipboardCodec::serialize(payload);

    auto* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(ClipboardCodec::kMimeType), encoded);
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setMimeData(mimeData);
    } else {
        delete mimeData;
    }

    onClipboardDataChanged();
    return copyRange;
}

void MainWindow::checkForUpdates()
{
    auto& prefs = Preferences::getInstance();
    if (prefs.getCheckUpdates() && prefs.getLastTimeUpdateCheck() >= 7) {
        AutoUpdater::getInstance().checkUpdate();
    }
}
