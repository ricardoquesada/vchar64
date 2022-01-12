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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QErrorMessage>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QToolBar>
#include <QToolButton>
#include <QUndoView>
#include <QWindow>

#include "aboutdialog.h"
#include "autoupdater.h"
#include "bigcharwidget.h"
#include "exportdialog.h"
#include "fileutils.h"
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
#include "xlinkpreview.h"

constexpr int MainWindow::MAX_RECENT_FILES;
static const int STATE_VERSION = 11;

MainWindow* MainWindow::getInstance()
{
    static MainWindow* _instance = nullptr;
    if (!_instance)
        _instance = new MainWindow;

    Q_ASSERT(_instance);
    return _instance;
}

State* MainWindow::getCurrentState()
{
    return getInstance()->getState();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _undoView(nullptr)
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
    checkForUpdates();
}

MainWindow::~MainWindow()
{
    delete _ui;
}

//
// public slots
//
void MainWindow::xlinkConnected()
{
    _ui->actionXlinkConnection->setText(tr("Disconnect"));
}

void MainWindow::xlinkDisconnected()
{
    _ui->actionXlinkConnection->setText(tr("Connect"));
}

void MainWindow::serverConnected()
{
    _ui->actionServerConnection->setText(tr("Disconnect"));
}

void MainWindow::serverDisconnected()
{
    _ui->actionServerConnection->setText(tr("Connect"));
}


void MainWindow::documentWasModified()
{
    // global flag
    bool modified = false;
    auto list = _ui->mdiArea->subWindowList();
    for (auto l: list)
    {
        modified |= qobject_cast<BigCharWidget*>(l->widget())->getState()->isModified();
    }
    setWindowModified(modified);

    auto state = getState();
    if (state)
    {
        _ui->actionUndo->setEnabled(state->getUndoStack()->canUndo());
        _ui->actionRedo->setEnabled(state->getUndoStack()->canRedo());
    }
}

void MainWindow::onTilePropertiesUpdated()
{
    // update max tile index
    auto state = getState();
    QSize s = state->getTileProperties().size;
    _ui->spinBox_tileIndex->setMaximum((256 / (s.width()*s.height()))-1);

    // rotate only enable if witdh==height
    _ui->actionRotate->setEnabled(s.width() == s.height());
}

void MainWindow::onMulticolorModeToggled(bool newvalue)
{
    Q_UNUSED(newvalue)
    // make sure the "multicolor" checkbox is in the correct state.
    // this is needed for the "undo" / "redos"...
    auto state = getState();
    _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());

    // enable / disable radios based on newvalue.
    // this event could be trigged by just changing the PEN_FOREGROUND color
    // when in multicolor mode.
    // but also, this event could be triggered by clicking on multicolor checkbox
    // so using "newvalue" is not enough
    bool enableradios = state->shouldBeDisplayedInMulticolor2(state->getTileIndex());
    _ui->radioButton_multicolor1->setEnabled(enableradios);
    _ui->radioButton_multicolor2->setEnabled(enableradios);
    _ui->actionMulti_Color_1->setEnabled(enableradios);
    _ui->actionMulti_Color_2->setEnabled(enableradios);

    // Update statusBar
    onColorPropertiesUpdated(state->getSelectedPen());
}

void MainWindow::onColorPropertiesUpdated(int pen)
{
    // update components in the "color widget"
    _ui->dockWidget_colors->update();

    // udpate the status bar
    auto state = getState();
    if (state)
    {
        int tileIndex = state->getTileIndex();
        int color = state->getColorForPen(pen, tileIndex);

        int number = color;
        bool multicolorEnabled = state->shouldBeDisplayedInMulticolor2(tileIndex);
        if (multicolorEnabled && pen == State::PEN_FOREGROUND)
            color = color % 8;
        _labelSelectedColor->setText(QString("Color: %1 (%2)")
                                     .arg(Palette::color_names[color])
                                     .arg(number));


        // update radio foreground mode
        auto foregroundColorMode = state->getForegroundColorMode();
        if (foregroundColorMode == State::FOREGROUND_COLOR_PER_TILE)
            _ui->radioButton_charColorPerChar->setChecked(true);
        else _ui->radioButton_charColorGlobal->setChecked(true);

        // update multicolor
        _ui->radioButton_multicolor1->setEnabled(multicolorEnabled);
        _ui->radioButton_multicolor2->setEnabled(multicolorEnabled);
        _ui->actionMulti_Color_1->setEnabled(multicolorEnabled);
        _ui->actionMulti_Color_2->setEnabled(multicolorEnabled);
    }
}

void MainWindow::refresh()
{
    auto state = getState();

    onTilePropertiesUpdated();
    onColorPropertiesUpdated(State::PEN_BACKGROUND);
    onColorPropertiesUpdated(State::PEN_FOREGROUND);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR1);
    onColorPropertiesUpdated(State::PEN_MULTICOLOR2);

    onMulticolorModeToggled(state->isMulticolorMode());
    onMapSizeUpdated();

    _undoView->setStack(state->getUndoStack());

    _ui->dockWidget_colors->update();
}

//
//
//
void MainWindow::closeEvent(QCloseEvent *event)
{
    setSessionFiles();
    _ui->mdiArea->closeAllSubWindows();
    if (!_ui->mdiArea->subWindowList().empty())
    {
        event->ignore();
    }
    else
    {
        event->accept();
        saveSettings();
    }
}

void MainWindow::createUndoView()
{
    auto undoDock = new QDockWidget(tr("Undo List"), this);
    undoDock->setObjectName(QStringLiteral("undoDock"));

    auto state = getState();

    if (state)
        _undoView = new QUndoView(state->getUndoStack(), undoDock);
    else
        _undoView = new QUndoView(undoDock);

    _undoView->setObjectName(QStringLiteral("undoView"));

    undoDock->setWidget(_undoView);
    undoDock->setFloating(true);
    undoDock->hide();

    _ui->menuView->insertAction(_ui->actionReset_Layout, undoDock->toggleViewAction());
    _ui->menuView->insertSeparator(_ui->actionReset_Layout);
}

void MainWindow::readSettings()
{
    // before restoring settings, save the current layout
    // needed for "reset layout"
    auto& preferences = Preferences::getInstance();
    preferences.setMainWindowDefaultGeometry(saveGeometry());
    preferences.setMainWindowDefaultState(saveState(STATE_VERSION));

    auto geom = preferences.getMainWindowGeometry();
    auto state = preferences.getMainWindowState();

    restoreState(state, STATE_VERSION);
    restoreGeometry(geom);

    QAction* actions[] = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5,
    };
    int index = preferences.getPalette();
    actions[index]->trigger();
}

void MainWindow::saveSettings()
{
    auto& preferences = Preferences::getInstance();
    preferences.setMainWindowGeometry(saveGeometry());
    preferences.setMainWindowState(saveState(STATE_VERSION));
    preferences.setPalette(Palette::getActivePalette());
}

void MainWindow::openDefaultDocument()
{
    bool success = false;

    if (Preferences::getInstance().getOpenLastFiles())
    {
        auto fileList = Preferences::getInstance().getSessionFiles();

        for (const auto& file: fileList)
        {
            success |= _openFile(file);
        }
    }

    if (!success)
        on_actionC64DefaultUppercase_triggered();
}

BigCharWidget* MainWindow::createDocument(State* state)
{
    auto bigcharWidget = new BigCharWidget(state, this);

    auto xlinkpreview = XlinkPreview::getInstance();
    connect(state, &State::fileLoaded, xlinkpreview, &XlinkPreview::fileLoaded);
    connect(state, &State::bytesUpdated, xlinkpreview, &XlinkPreview::bytesUpdated);
    connect(state, &State::tileUpdated, xlinkpreview, &XlinkPreview::tileUpdated);
    connect(state, &State::colorPropertiesUpdated, xlinkpreview, &XlinkPreview::colorPropertiesUpdated);
    connect(state, &State::multicolorModeToggled, xlinkpreview, &XlinkPreview::colorPropertiesUpdated);

    auto serverpreview = ServerPreview::getInstance();
    connect(state, &State::fileLoaded, serverpreview, &ServerPreview::fileLoaded);
    connect(state, &State::bytesUpdated, serverpreview, &ServerPreview::bytesUpdated);
    connect(state, &State::tileUpdated, serverpreview, &ServerPreview::tileUpdated);
    connect(state, &State::colorPropertiesUpdated, serverpreview, &ServerPreview::colorPropertiesUpdated);
    connect(state, &State::multicolorModeToggled, serverpreview, &ServerPreview::multicolorModeUpdated);
    connect(state, &State::tilePropertiesUpdated, serverpreview, &ServerPreview::tilePropertiesUpdated);

    connect(state, &State::fileLoaded, this, &MainWindow::refresh);
    connect(state, &State::fileLoaded, bigcharWidget, &BigCharWidget::onFileLoaded);
    connect(state, &State::fileLoaded, _ui->tilesetWidget, &TilesetWidget::onFileLoaded);
    connect(state, &State::fileLoaded, _ui->charsetWidget, &CharsetWidget::onCharsetUpdated);
    connect(state, &State::fileLoaded, _ui->mapWidget, &MapWidget::onFileLoaded);

    connect(state, &State::tilePropertiesUpdated, this, &MainWindow::onTilePropertiesUpdated);
    connect(state, &State::tilePropertiesUpdated, bigcharWidget, &BigCharWidget::onTilePropertiesUpdated);
    connect(state, &State::tilePropertiesUpdated, _ui->tilesetWidget, &TilesetWidget::onTilePropertiesUpdated);
    connect(state, &State::tilePropertiesUpdated, _ui->mapWidget, &MapWidget::onTilePropertiesUpdated);

    connect(state, &State::mapSizeUpdated, _ui->mapWidget, &MapWidget::onMapSizeUpdated);
    connect(state, &State::mapSizeUpdated, this, &MainWindow::onMapSizeUpdated);
    connect(state, &State::mapContentUpdated, _ui->mapWidget, &MapWidget::onMapContentUpdated);

    connect(state, &State::tileUpdated, bigcharWidget, &BigCharWidget::onTileUpdated);
    connect(state, &State::tileUpdated, _ui->charsetWidget, &CharsetWidget::onTileUpdated);
    connect(state, &State::tileUpdated, _ui->tilesetWidget, &TilesetWidget::onTileUpdated);
    connect(state, &State::tileUpdated, _ui->mapWidget, &MapWidget::onTileUpdated);
    connect(state, &State::charsetUpdated, bigcharWidget, &BigCharWidget::onCharsetUpdated);
    connect(state, &State::charsetUpdated, _ui->charsetWidget, &CharsetWidget::onCharsetUpdated);
    connect(state, &State::charsetUpdated, _ui->tilesetWidget, &TilesetWidget::onCharsetUpdated);
    connect(state, &State::charsetUpdated, _ui->mapWidget, &MapWidget::onCharsetUpdated);
    connect(state, &State::charIndexUpdated, this, &MainWindow::onCharIndexUpdated);
    connect(state, &State::colorPropertiesUpdated, this, &MainWindow::onColorPropertiesUpdated);
    connect(state, &State::colorPropertiesUpdated, bigcharWidget, &BigCharWidget::onColorPropertiesUpdated);
    connect(state, &State::colorPropertiesUpdated, _ui->charsetWidget, &CharsetWidget::onColorPropertiesUpdated);
    connect(state, &State::colorPropertiesUpdated, _ui->tilesetWidget, &TilesetWidget::onColorPropertiesUpdated);
    connect(state, &State::colorPropertiesUpdated, _ui->mapWidget, &MapWidget::onColorPropertiesUpdated);
    connect(state, &State::selectedPenChaged, this, &MainWindow::onColorPropertiesUpdated);
    connect(state, &State::multicolorModeToggled, bigcharWidget, &BigCharWidget::onMulticolorModeToggled);
    connect(state, &State::multicolorModeToggled, _ui->charsetWidget, &CharsetWidget::onMulticolorModeToggled);
    connect(state, &State::multicolorModeToggled, _ui->tilesetWidget, &TilesetWidget::onMulticolorModeToggled);
    connect(state, &State::multicolorModeToggled, _ui->mapWidget, &MapWidget::onMulticolorModeToggled);
    connect(state, &State::multicolorModeToggled, this, &MainWindow::onMulticolorModeToggled);
    connect(state, &State::contentsChanged, this, &MainWindow::documentWasModified);

    connect(state, &State::tileIndexUpdated, _ui->tilesetWidget, &TilesetWidget::onTileIndexUpdated);
    connect(state, &State::tileIndexUpdated, bigcharWidget, &BigCharWidget::onTileIndexUpdated);
    connect(state, &State::charIndexUpdated, _ui->charsetWidget, &CharsetWidget::onCharIndexUpdated);
    connect(state, &State::tileIndexUpdated, _ui->spinBox_tileIndex, &QSpinBox::setValue);


    connect(state->getUndoStack(), &QUndoStack::indexChanged, this, &MainWindow::documentWasModified);
    connect(state->getUndoStack(), &QUndoStack::cleanChanged, this, &MainWindow::documentWasModified);

    state->clearUndoStack();

    // After connecting all slots, add subwindow and activate it.
    // No need to call "state->emitNewState();" since the MdiArea will trigger it.
    auto subwindow = _ui->mdiArea->addSubWindow(bigcharWidget, Qt::Widget);
    subwindow->showMaximized();
    subwindow->layout()->setContentsMargins(2, 2, 2, 2);
    _ui->mdiArea->setActiveSubWindow(subwindow);

    return bigcharWidget;
}

void MainWindow::closeState(State* state)
{
    Q_UNUSED(state)

    // What to do?
}

void MainWindow::createActions()
{
    // Add recent file actions to the recent files menu
    for (int i=0; i<MAX_RECENT_FILES; ++i)
    {
         auto action = new QAction(this);
         _ui->menuRecentFiles->insertAction(_ui->actionClearRecentFiles, action);
         action->setVisible(false);
         connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFileTriggered);

         _recentFiles.append(action);
    }
    _ui->menuRecentFiles->insertSeparator(_ui->actionClearRecentFiles);
    updateRecentFiles();


    // FIXME should be on a different method
    _ui->colorRectWidget_0->setPen(State::PEN_BACKGROUND);
    _ui->colorRectWidget_1->setPen(State::PEN_FOREGROUND);
    _ui->colorRectWidget_2->setPen(State::PEN_MULTICOLOR1);
    _ui->colorRectWidget_3->setPen(State::PEN_MULTICOLOR2);

    auto xlinkPreview = XlinkPreview::getInstance();
    connect(xlinkPreview, &XlinkPreview::previewConnected, this, &MainWindow::xlinkConnected);
    connect(xlinkPreview, &XlinkPreview::previewDisconnected, this, &MainWindow::xlinkDisconnected);
    _ui->menuXlink->setEnabled(xlinkPreview->isAvailable());

    auto serverPreview = ServerPreview::getInstance();
    connect(serverPreview, &ServerPreview::previewConnected, this, &MainWindow::serverConnected);
    connect(serverPreview, &ServerPreview::previewDisconnected, this, &MainWindow::serverDisconnected);

    connect(_ui->mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);

    connect(_ui->spinBox_tileIndex, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onSpinBoxValueChanged);


//
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_charset->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_tileset->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_map->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_colors->toggleViewAction());
    _ui->menuView->insertAction(_ui->actionReset_Layout, _ui->dockWidget_tileIndex->toggleViewAction());

    _ui->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _ui->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

     if(xlinkPreview->isConnected()) {
          xlinkConnected();
    }
}

void MainWindow::createDefaults()
{
    // tabify charsetWidget, tilesetWidget and mapWidget
    tabifyDockWidget(_ui->dockWidget_charset, _ui->dockWidget_tileset);
    tabifyDockWidget(_ui->dockWidget_charset, _ui->dockWidget_map);

    // select charsetWidget as the default one
    _ui->dockWidget_charset->raise();
}

void MainWindow::setupCharsetDock()
{
    auto toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16,16));
    _ui->verticalLayout_charset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_CharsetGrid);

    toolbar->addSeparator();

    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxCharsetZoom = new QComboBox(this);
    _comboBoxCharsetZoom->addItem("200%", QVariant(200));
    _comboBoxCharsetZoom->addItem("175%", QVariant(175));
    _comboBoxCharsetZoom->addItem("150%", QVariant(150));
    _comboBoxCharsetZoom->addItem("125%", QVariant(125));
    _comboBoxCharsetZoom->addItem("100%", QVariant(100));
    _comboBoxCharsetZoom->addItem("75%", QVariant(75));
    _comboBoxCharsetZoom->addItem("50%", QVariant(50));
    _comboBoxCharsetZoom->addItem("25%", QVariant(25));
    _comboBoxCharsetZoom->setCurrentIndex(4); // 100%
    toolbar->addWidget(_comboBoxCharsetZoom);

    connect(_comboBoxCharsetZoom, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        [&](int index)
        {
            auto variant = _comboBoxCharsetZoom->itemData(index);
            _ui->charsetWidget->setZoomLevel(variant.toInt());
        }
    );
}

void MainWindow::setupTilesetDock()
{
    auto toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16,16));
    _ui->verticalLayout_tileset->addWidget(toolbar);

    toolbar->addAction(_ui->actionToggle_TilesetGrid);

    toolbar->addSeparator();

    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxTilesetZoom = new QComboBox(this);
    _comboBoxTilesetZoom->addItem("200%", QVariant(200));
    _comboBoxTilesetZoom->addItem("175%", QVariant(175));
    _comboBoxTilesetZoom->addItem("150%", QVariant(150));
    _comboBoxTilesetZoom->addItem("125%", QVariant(125));
    _comboBoxTilesetZoom->addItem("100%", QVariant(100));
    _comboBoxTilesetZoom->addItem("75%", QVariant(75));
    _comboBoxTilesetZoom->addItem("50%", QVariant(50));
    _comboBoxTilesetZoom->addItem("25%", QVariant(25));
    _comboBoxTilesetZoom->setCurrentIndex(4); // 100%
    toolbar->addWidget(_comboBoxTilesetZoom);

    connect(_comboBoxTilesetZoom, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        [&](int index)
        {
            auto variant = _comboBoxTilesetZoom->itemData(index);
            _ui->tilesetWidget->setZoomLevel(variant.toInt());
        }
    );
}

void MainWindow::setupMapDock()
{
    auto toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16,16));
    _ui->verticalLayout_map->addWidget(toolbar);

    auto label = new QLabel(tr("Map Size"), this);
    toolbar->addWidget(label);
    _spinBoxMapX = new QSpinBox(this);
    _spinBoxMapY = new QSpinBox(this);
    QSpinBox* spins[] = {_spinBoxMapX, _spinBoxMapY};
    for (auto& spin: spins)
    {
        spin->setMinimum(1);
        spin->setMaximum(4096);
        toolbar->addWidget(spin);
        spin->setKeyboardTracking(false);
    }
    connect(_spinBoxMapX, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onSpinBoxMapSizeX_valueChanged);
    connect(_spinBoxMapY, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onSpinBoxMapSizeY_valueChanged);


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

    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbar->addWidget(empty);

    _comboBoxMapZoom = new QComboBox(this);
    _comboBoxMapZoom->addItem("200%", QVariant(200));
    _comboBoxMapZoom->addItem("175%", QVariant(175));
    _comboBoxMapZoom->addItem("150%", QVariant(150));
    _comboBoxMapZoom->addItem("125%", QVariant(125));
    _comboBoxMapZoom->addItem("100%", QVariant(100));
    _comboBoxMapZoom->addItem("75%", QVariant(75));
    _comboBoxMapZoom->addItem("50%", QVariant(50));
    _comboBoxMapZoom->addItem("25%", QVariant(25));
    _comboBoxMapZoom->setCurrentIndex(4); // 100%
    toolbar->addWidget(_comboBoxMapZoom);


    connect(_comboBoxMapZoom, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        [&](int index)
        {
            auto variant = _comboBoxMapZoom->itemData(index);
            _ui->mapWidget->setZoomLevel(variant.toInt());
        }
    );
}

void MainWindow::setupStatusBar()
{
    _labelSelectedColor = new QLabel(tr("Color: Black (0)"), this);
    statusBar()->addPermanentWidget(_labelSelectedColor);

    _labelCharIdx = new QLabel(tr("Char: 000"), this);
//    _labelCharIdx->setFrameStyle(QFrame::Panel | QFrame::Plain);
    statusBar()->addPermanentWidget(_labelCharIdx);

    _labelTileIdx = new QLabel(tr("Tile: 000"), this);
//    _labelCharIdx->setFrameStyle(QFrame::Panel | QFrame::Plain);
    statusBar()->addPermanentWidget(_labelTileIdx);

    // display correct selected color
    auto state = getState();
    if (state)
        onColorPropertiesUpdated(state->getSelectedPen());
}

void MainWindow::updateMenus()
{
    bool withDocuments = !_ui->mdiArea->subWindowList().empty();

    _ui->dockWidget_colors->setEnabled(withDocuments);
    _ui->dockWidget_charset->setEnabled(withDocuments);
    _ui->dockWidget_tileset->setEnabled(withDocuments);
    _ui->dockWidget_tileIndex->setEnabled(withDocuments);
    _undoView->setEnabled(withDocuments);

    _ui->menuEdit->setEnabled(withDocuments);
    _ui->menuTile->setEnabled(withDocuments);
    _ui->menuColors->setEnabled(withDocuments);

    _spinBoxMapX->setEnabled(withDocuments);
    _spinBoxMapY->setEnabled(withDocuments);

    QAction* actions[] =
    {
        _ui->actionExport,
        _ui->actionExportAs,
        _ui->actionSave,
        _ui->actionSaveAs,
        _ui->actionClose,
        _ui->actionClose_All,
        _ui->actionCloneCurrentProject,

        _ui->actionCopy,
        _ui->actionPaste,
        _ui->actionCut
    };
    for (auto& action: actions)
        action->setEnabled(withDocuments);
}

void MainWindow::updateRecentFiles()
{
    QStringList files = Preferences::getInstance().getRecentFiles();
    const int numRecentFiles = qMin(files.size(), MAX_RECENT_FILES);

    for (int i = 0; i < numRecentFiles; ++i)
    {
        _recentFiles[i]->setText(FileUtils::getShortNativePath(files[i]));
        _recentFiles[i]->setData(files[i]);
        _recentFiles[i]->setVisible(true);
        // enable only if the file exists
        _recentFiles[i]->setEnabled(QFileInfo(files[i]).exists());
    }
    for (int j=numRecentFiles; j<MAX_RECENT_FILES; ++j)
    {
        _recentFiles[j]->setVisible(false);
    }
    _ui->menuRecentFiles->setEnabled(numRecentFiles > 0);
}

void MainWindow::setRecentFile(const QString& fileName)
{
    // Remember the file by its canonical file path
    const QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

    if (canonicalFilePath.isEmpty())
        return;

    auto& preferences = Preferences::getInstance();
    QStringList files = preferences.getRecentFiles();
    files.removeAll(canonicalFilePath);
    files.prepend(canonicalFilePath);
    while (files.size() > MAX_RECENT_FILES)
        files.removeLast();

    preferences.setRecentFiles(files);
    updateRecentFiles();
}

void MainWindow::setSessionFiles()
{
    QStringList fileList;
    const QStringList suffixList = {QString("vsf"), QString("koa"), QString("kla")};
    auto mdiList = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::StackingOrder);
    for (auto item: mdiList) {
        auto bigchar = qobject_cast<BigCharWidget*>(item->widget());
        auto bigcharState = bigchar->getState();
        auto filename = bigcharState->getLoadedFilename();
        // FIXME: Imported files are using getLoadedFilename(). Instead, they should use
        // getImportedFilename(), and the LoadedFilename should be empty.
        // skip .koa, kla, and .vsf files, otherwise VChar64 will try to "open" them instead
        // of "import" them
        auto fileSuffix = QFileInfo(filename).suffix();
        if (!suffixList.contains(fileSuffix))
            fileList.append(filename);
    }

    Preferences::getInstance().setSessionFiles(fileList);
}

//
// MARK - Slots / Events / Callbacks
//
void MainWindow::showMessageOnStatusBar(const QString& message)
{
    statusBar()->showMessage(message, 3000);
}

void MainWindow::onCharIndexUpdated(int charIndex)
{
    auto state = getState();

    _labelCharIdx->setText(tr("Char: %1")
                           .arg(charIndex, 3, 10, QLatin1Char(' ')));

    int tileIndex = state->getTileIndexFromCharIndex(charIndex);

    _labelTileIdx->setText(tr("Tile: %1")
                           .arg(tileIndex, 3, 10, QLatin1Char(' ')));
}

void MainWindow::on_actionExit_triggered()
{
    setSessionFiles();
    _ui->mdiArea->closeAllSubWindows();
    if (_ui->mdiArea->subWindowList().empty())
    {
        saveSettings();
        QApplication::exit();
    }
}

void MainWindow::on_actionEmptyProject_triggered()
{
    auto state = new State;

    createDocument(state);
    setWindowFilePath(tr("(untitled)"));
    _ui->mdiArea->currentSubWindow()->setWindowFilePath(tr("(untitled)"));
    _ui->mdiArea->currentSubWindow()->setWindowTitle(tr("(untitled)"));
}

void MainWindow::on_actionC64DefaultUppercase_triggered()
{
    auto state = new State;
    if (state->openFile(":/res/c64-chargen-uppercase.bin"))
    {
        createDocument(state);
        setWindowFilePath(tr("(untitled)"));
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(tr("(untitled)"));
        _ui->mdiArea->currentSubWindow()->setWindowTitle(tr("(untitled)"));
    }
    else
    {
        delete state;
    }
}

void MainWindow::on_actionC64DefaultLowercase_triggered()
{
    auto state = new State;
    if (state->openFile(":/res/c64-chargen-lowercase.bin"))
    {
        createDocument(state);
        setWindowFilePath(tr("(untitled)"));
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(tr("(untitled)"));
        _ui->mdiArea->currentSubWindow()->setWindowTitle(tr("(untitled)"));
    }
    else
    {
        delete state;
    }
}

void MainWindow::on_actionPreferences_triggered()
{
    PreferencesDialog dialog(this);
    dialog.exec();
}

//
// MARK - Color + Palette callbacks
//
void MainWindow::on_checkBox_multicolor_toggled(bool checked)
{
    // when switching from one State to another,
    // the multicolor checkbox might change, and it will generate
    // this event.
    // And we don't want to create a new Command if the state is the same
    State *state = getState();
    if (state && checked != state->isMulticolorMode())
    {
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
    State *state = getState();

    if (state)
    {
        state->setSelectedPen(pen);

        QAction* actions[] = {
            _ui->actionBackground,
            _ui->actionMulti_Color_1,
            _ui->actionMulti_Color_2,
            _ui->actionForeground
        };

        QRadioButton* radios[] = {
            _ui->radioButton_background,
            _ui->radioButton_multicolor1,
            _ui->radioButton_multicolor2,
            _ui->radioButton_foreground
        };

        for (int i=0; i<4; i++)
            actions[i]->setChecked(i==pen);

        radios[pen]->setChecked(true);
    }
}

void MainWindow::on_radioButton_background_toggled(bool checked)
{
    if (!checked)
        return;

    activateRadioButtonIndex(State::PEN_BACKGROUND);
}

void MainWindow::on_radioButton_foreground_toggled(bool checked)
{
    if (!checked)
        return;

    activateRadioButtonIndex(State::PEN_FOREGROUND);
}

void MainWindow::on_radioButton_multicolor1_toggled(bool checked)
{
    if (!checked)
        return;

    activateRadioButtonIndex(State::PEN_MULTICOLOR1);
}

void MainWindow::on_radioButton_multicolor2_toggled(bool checked)
{
    if (!checked)
        return;

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
    if (!checked)
        return;

    auto state = getState();
    if (state)
        state->setForegroundColorMode(State::FOREGROUND_COLOR_GLOBAL);
}

void MainWindow::on_radioButton_charColorPerChar_toggled(bool checked)
{
    if (!checked)
        return;

    auto state = getState();
    if (state)
        state->setForegroundColorMode(State::FOREGROUND_COLOR_PER_TILE);
}

void MainWindow::activatePalette(int paletteIndex)
{
    QAction* actions[] = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
        _ui->actionPalette_5
    };

    Palette::setActivePalette(paletteIndex);

    for (int i=0; i<6; i++)
        actions[i]->setChecked(i==paletteIndex);

    // FIXME: there should be an event to propage the palette changes...
    // in the meantime, do it manually
    _ui->dockWidget_colors->update();
    _ui->tilesetWidget->update();
    _ui->charsetWidget->update();
    _ui->mapWidget->update();
    auto state = getState();
    if (state)
        state->getBigCharWidget()->update();
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


//
// MARK - File IO callbacks + helper functions
//
bool MainWindow::openFile(const QString& path)
{
    QFileInfo info(path);
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    if (activateIfAlreadyOpen(info.canonicalFilePath()))
        return true;

    bool ret = _openFile(path);
    if (!ret)
    {
        QMessageBox::warning(this,
                             tr("Application"), tr("Error loading '") + info.fileName() + "'\n" + statusBar()->currentMessage(),
                             QMessageBox::Ok);
    }

    return ret;
}

bool MainWindow::_openFile(const QString& path)
{
    QFileInfo info(path);
    bool ret = false;
    auto state = new State;
    if ((ret=state->openFile(path)))
    {
        createDocument(state);
        setRecentFile(path);

        _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());

        _ui->mdiArea->currentSubWindow()->setWindowFilePath(info.filePath());
        _ui->mdiArea->currentSubWindow()->setWindowTitle(info.baseName());
        setWindowFilePath(info.filePath());
    }
    else
    {
        delete state;
    }
    return ret;
}

bool MainWindow::activateIfAlreadyOpen(const QString& fileName)
{
    QStringList fileList;
    auto mdiList = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::StackingOrder);
    for (auto mdiSubWindow: mdiList) {
        auto bigchar = qobject_cast<BigCharWidget*>(mdiSubWindow->widget());
        auto bigcharState = bigchar->getState();
        qDebug() << bigcharState->getLoadedFilename();
        if (fileName.compare(bigcharState->getLoadedFilename()) == 0) {
            mdiSubWindow->setFocus();
            return true;
        }
    }
    return false;
}

void MainWindow::on_actionOpen_triggered()
{
    auto& preferences = Preferences::getInstance();
    QString filter = preferences.getLastUsedOpenFilter();
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select File"),
                                           preferences.getLastUsedDirectory(),
                                           tr(
                                               "All files (*);;" \
                                               "All supported files (*.vchar64proj *.raw *.bin *.prg *.64c *.ctm);;" \
                                               "VChar64 Project (*.vchar64proj);;" \
                                               "Raw (*.raw *.bin);;" \
                                               "PRG (*.prg *.64c);;" \
                                               "CharPad (*.ctm);;"
                                           ),
                                           &filter
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0) {
        preferences.setLastUsedOpenFilter(filter);
        openFile(fn);
    }
}

void MainWindow::on_actionCloneCurrentProject_triggered()
{
    auto currentState = getState();
    Q_ASSERT(currentState && "Invalid state");

    auto newState = new State();
    newState->copyState(*currentState);
    createDocument(newState);
    setWindowFilePath(tr("(untitled)"));
    _ui->mdiArea->currentSubWindow()->setWindowFilePath(tr("(untitled)"));
    _ui->mdiArea->currentSubWindow()->setWindowTitle(tr("(untitled)"));
}

void MainWindow::on_actionImportVICESnapshot_triggered()
{
    ImportVICEDialog dialog(this);
    if (dialog.exec())
    {
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(dialog.getFilepath());
        _ui->mdiArea->currentSubWindow()->setWindowTitle(QFileInfo(dialog.getFilepath()).baseName());
        setWindowFilePath(dialog.getFilepath());
    }
}

void MainWindow::on_actionImportKoalaImage_triggered()
{
    ImportKoalaDialog dialog(this);
    if (dialog.exec())
    {
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(dialog.getFilepath());
        _ui->mdiArea->currentSubWindow()->setWindowTitle(QFileInfo(dialog.getFilepath()).baseName());
        setWindowFilePath(dialog.getFilepath());
    }
}

bool MainWindow::on_actionSaveAs_triggered()
{
    bool ret = false;

    auto state = getState();
    auto fn = state->getSavedFilename();
    if (fn.length() == 0)
        fn = state->getLoadedFilename();
    if (fn.length() != 0)
    {
        QFileInfo fileInfo(fn);
        auto suffix = fileInfo.suffix();
        if (suffix != "vchar64proj") {
            fn = fileInfo.absolutePath() + "/" + fileInfo.completeBaseName() + ".vchar64proj";
        }
    }
    else
    {
        fn = Preferences::getInstance().getLastUsedDirectory() + "/untitled.vchar64proj";
    }
    auto filename = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                             fn,
                                             tr("VChar64 project(*.vchar64proj)"));

    if (filename.length() > 0)
    {
        auto state = getState();
        if ((ret=state->saveProject(filename)))
        {
            QFileInfo fi(filename);
            setWindowFilePath(fi.filePath());
            _ui->mdiArea->currentSubWindow()->setWindowFilePath(fi.filePath());
            _ui->mdiArea->currentSubWindow()->setWindowTitle(fi.baseName());

            // add it to recent files
            setRecentFile(filename);
        }
        else
        {
            QMessageBox::warning(this, tr("Application"), tr("Error saving project file: ") + filename, QMessageBox::Ok);
        }

    }

    return ret;
}

bool MainWindow::on_actionSave_triggered()
{
    bool ret;
    auto state = getState();
    auto filename = state->getSavedFilename();
    if (filename.length() > 0)
    {
        ret = state->saveProject(filename);
        if (ret)
        {
            statusBar()->showMessage(tr("File saved to %1").arg(state->getSavedFilename()), 2000);
        }
        else
        {
            statusBar()->showMessage(tr("Error saving file"), 2000);
            QMessageBox::warning(this, tr("Application"), tr("Error saving project file: ") + filename, QMessageBox::Ok);
        }
    }
    else
    {
        ret = on_actionSaveAs_triggered();
    }

    return ret;
}

void MainWindow::on_actionExport_triggered()
{
    auto state = getState();
    auto exportedFilename = state->getExportedFilename();
    if (exportedFilename.length()==0)
    {
        on_actionExportAs_triggered();
    }
    else
    {
        if (!state->export_())
            QMessageBox::warning(this, tr("Application"), tr("Error exporting file: ") + exportedFilename, QMessageBox::Ok);
    }
}

void MainWindow::on_actionExportAs_triggered()
{
    ExportDialog dialog(getState(), this);
    dialog.exec();
}

void MainWindow::on_actionClose_triggered()
{
    _ui->mdiArea->closeActiveSubWindow();
}

void MainWindow::on_actionClose_All_triggered()
{
    _ui->mdiArea->closeAllSubWindows();
}

//
// MARK - Tile editing callbacks
//
void MainWindow::on_actionInvert_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();
    state->tileInvert(tileIndex);
}

void MainWindow::on_actionFlipHorizontally_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();
    state->tileFlipHorizontally(tileIndex);
}

void MainWindow::on_actionFlipVertically_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileFlipVertically(tileIndex);
}

void MainWindow::on_actionRotate_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileRotate(tileIndex);
}

void MainWindow::on_actionClearCharacter_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileClear(tileIndex);
}

void MainWindow::on_actionShiftLeft_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileShiftLeft(tileIndex);
}

void MainWindow::on_actionShiftRight_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileShiftRight(tileIndex);
}

void MainWindow::on_actionShiftUp_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileShiftUp(tileIndex);
}

void MainWindow::on_actionShiftDown_triggered()
{
    auto state = getState();
    int tileIndex = getBigcharWidget()->getTileIndex();

    state->tileShiftDown(tileIndex);
}

void MainWindow::on_actionCut_triggered()
{
    auto state = getState();
    auto range = bufferToClipboard(state);
    if (range.offset != -1)
        state->cut(range);
}

void MainWindow::on_actionCopy_triggered()
{
    auto state = getState();
    bufferToClipboard(state);
}

void MainWindow::on_actionPaste_triggered()
{
    auto state = getState();

    QByteArray bytearray = bufferFromClipboard();
    if (bytearray.length() <= 0) {
        qWarning() << "Invalid clipboard buffer";
        return;
    }
    auto data = bytearray.data();
    State::CopyRange* range = reinterpret_cast<State::CopyRange*>(data);
    data += sizeof(State::CopyRange);
    quint8* buffer = reinterpret_cast<quint8*>(data);

    {
        // sanity check #1
        // only paste it if the destination is compatible with the source
        // valid scenarios:
        // src: CHARS  dst: Charset / Tileset
        // src: TILES  dst: Charset / Tileset
        // src: MAP    dst: Map
        if (!
                (((range->type == State::CopyRange::CHARS || range->type == State::CopyRange::TILES) &&
                (_ui->charsetWidget->hasFocus() || _ui->tilesetWidget->hasFocus()))
                ||
                (range->type == State::CopyRange::MAP && _ui->mapWidget->hasFocus())
                ))
        {
            QApplication::beep();
            return;
        }

        // Sanity check #2
        // when copying tiles, make sure that they have the same size
        if (range->type == State::CopyRange::TILES && state->getTileProperties().size != range->tileProperties.size)
        {
            QApplication::beep();
            QMessageBox msgBox(QMessageBox::Warning,
                               tr("Application"),
                               tr("Could not paste tiles when their sizes are different. Change the tile properties to {%1, %2}")
                                .arg(range->tileProperties.size.width())
                                .arg(range->tileProperties.size.height()),
                               QMessageBox::Ok,
                               this);
            msgBox.exec();
            return;
        }

        int cursorPos = (range->type == State::CopyRange::CHARS || range->type == State::CopyRange::TILES)
                    ? _ui->charsetWidget->getCursorPos()
                    : _ui->mapWidget->getCursorPos();

        state->paste(cursorPos, *range, buffer);
    }
}

//
// MARK - Undo / Redo callbacks
//
void MainWindow::on_actionUndo_triggered()
{
    auto state = getState();
    state->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    auto state = getState();
    state->redo();
}

//
// MARK - Misc callbacks
//
void MainWindow::on_actionReportBug_triggered()
{
    QDesktopServices::openUrl(QUrl("http://github.com/ricardoquesada/vchar64/issues"));
}

void MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl("http://github.com/ricardoquesada/vchar64/wiki/Documentation"));
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
    auto action = qobject_cast<QAction*>(sender());
    if (action)
    {
        auto path = action->data().toString();
        if (!openFile(path))
        {
            auto& preferences = Preferences::getInstance();
            QStringList files = preferences.getRecentFiles();
            files.removeAll(path);
            preferences.setRecentFiles(files);
            updateRecentFiles();
        }
    }
}

void MainWindow::on_actionTilesProperties_triggered()
{
    TilePropertiesDialog dialog(getState(), this);
    dialog.exec();
}

void MainWindow::on_actionMap_Properties_triggered()
{
    MapPropertiesDialog dialog(getState(), this);
    dialog.exec();
}

void MainWindow::onSpinBoxMapSizeX_valueChanged(int newValue)
{
    auto state = getState();
    if (state)
    {
        // FIXME: onMapSizeUpdated() calls setValue()
        // and setValue() triggers this callback, but it is possible
        // that no value was changed. So in order to avoid
        // an action in the UndoView, we check first the size
        auto currentSize = state->getMapSize();

        QSize newSize(newValue, currentSize.height());
        if (newSize != currentSize)
            state->setMapSize(newSize);
    }
}

void MainWindow::onSpinBoxMapSizeY_valueChanged(int newValue)
{
    auto state = getState();
    if (state)
    {
        // FIXME: onMapSizeUpdated() calls setValue()
        // and setValue() triggers this callback, but it is possible
        // that no value was changed. So in order to avoid
        // an action in the UndoView, we check first the size
        auto currentSize = state->getMapSize();

        QSize newSize(currentSize.width(), newValue);
        if (newSize != currentSize)
            state->setMapSize(newSize);
    }
}

void MainWindow::onMapSizeUpdated()
{
    auto state = getState();
    if (state)
    {
        auto mapSize = state->getMapSize();
        _spinBoxMapX->setValue(mapSize.width());
        _spinBoxMapY->setValue(mapSize.height());
    }
}

void MainWindow::on_actionXlinkConnection_triggered()
{
    auto preview = XlinkPreview::getInstance();
    if(preview->isConnected())
        preview->disconnect();
    else
        if(!preview->connect()) {
            QMessageBox msgBox(QMessageBox::Warning, "", tr("Could not connect to remote C64"), QMessageBox::Ok, this);
            msgBox.exec();
        }
}

void MainWindow::on_actionServerConnection_triggered()
{
    auto preview = ServerPreview::getInstance();
    if (preview->isConnected())
    {
        preview->disconnect();
    }
    else
    {
        ServerConnectDialog dialog(this);
        if (dialog.exec())
        {
            auto ipaddress = dialog.getIPAddress();
            if (!preview->connect(ipaddress))
            {
                QMessageBox msgBox(QMessageBox::Warning, "", tr("Could not connect to remote server"), QMessageBox::Ok, this);
                msgBox.exec();
            }
        }
    }
}


void MainWindow::on_actionNext_Tile_triggered()
{
    int value = _ui->spinBox_tileIndex->value() + 1;
    if (value > _ui->spinBox_tileIndex->maximum())
        value = 0;
    _ui->spinBox_tileIndex->setValue(value);
}

void MainWindow::on_actionPrevious_Tile_triggered()
{
    int value = _ui->spinBox_tileIndex->value() - 1;
    if (value < 0)
        value = _ui->spinBox_tileIndex->maximum();
    _ui->spinBox_tileIndex->setValue(value);
}

void MainWindow::on_actionReset_Layout_triggered()
{
    auto& preferences = Preferences::getInstance();
    auto geom = preferences.getMainWindowDefaultGeometry();
    auto state = preferences.getMainWindowDefaultState();
    restoreState(state, STATE_VERSION);
    restoreGeometry(geom);

    // center it
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignCenter,
            size(),
            QGuiApplication::primaryScreen()->geometry())
    );
}

void MainWindow::onSubWindowActivated(QMdiSubWindow* subwindow)
{
    // subwindow can be nullptr when closing the app
    if (subwindow)
    {
        auto title = subwindow->windowFilePath();
        setWindowFilePath(title);

        auto state = getState();
        if (state)
            state->emitNewState();
    }
    updateMenus();
}

void MainWindow::onSpinBoxValueChanged(int tileIndex)
{
    auto state = getState();
    if (state)
    {
        state->setTileIndex(tileIndex);
    }
}

// Map callbacks: should be in its own class
void MainWindow::on_actionClear_Map_triggered()
{
    auto state = getState();
    if (state)
    {
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

//
//
BigCharWidget* MainWindow::getBigcharWidget() const
{
    auto mdisubview = _ui->mdiArea->currentSubWindow();
    if (!mdisubview)
    {
        auto list = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::ActivationHistoryOrder);
        if (!list.empty())
            mdisubview = list.last();
        else
            return nullptr;
    }
    auto bigchar = qobject_cast<BigCharWidget*>(mdisubview->widget());

    Q_ASSERT(bigchar && "bigchar not found");
    return bigchar;
}

State* MainWindow::getState() const
{
    auto bigchar = getBigcharWidget();
    if (bigchar)
        return bigchar->getState();
    return nullptr;
}

State::CopyRange MainWindow::bufferToClipboard(State* state) const
{
    State::CopyRange copyRange;
    if (_ui->charsetWidget->hasFocus()) {
        _ui->charsetWidget->getSelectionRange(&copyRange);
    } else if (_ui->tilesetWidget->hasFocus()) {
        _ui->tilesetWidget->getSelectionRange(&copyRange);
    } else if (_ui->mapWidget->hasFocus()) {
        _ui->mapWidget->getSelectionRange(&copyRange);
    } else {
        copyRange.offset = -1;
        return copyRange;
    }

    auto clipboard = QApplication::clipboard();
    auto mimeData = new QMimeData;
    QByteArray array((char*)&copyRange, sizeof(copyRange));
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES)
    {
        array.append((const char*)state->getCharsetBuffer(), State::CHAR_BUFFER_SIZE);
        array.append((const char*)state->getTileColors(), State::TILE_COLORS_BUFFER_SIZE);
    }
    else /* MAP */
    {
        array.append((const char*)state->getMapBuffer(), state->getMapSize().width() * state->getMapSize().height());
    }

    mimeData->setData("vchar64/range", array);

    Q_ASSERT(array.length() == copyRange.bufferSize + (int)sizeof(copyRange) && "Error while copying buffer");

    clipboard->setMimeData(mimeData);

    return copyRange;
}

QByteArray MainWindow::bufferFromClipboard() const
{
    QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    return mimeData->data("vchar64/range");
}

void MainWindow::checkForUpdates()
{
    auto& prefs = Preferences::getInstance();
    // check for updates only for more than 7 days passed since last check
    if (prefs.getCheckUpdates() && prefs.getLastTimeUpdateCheck() >= 7) {
        AutoUpdater::getInstance().checkUpdate();
    }
}
