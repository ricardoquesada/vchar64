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

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QDebug>
#include <QDesktopServices>
#include <QCloseEvent>
#include <QUndoView>
#include <QErrorMessage>
#include <QLabel>
#include <QDesktopWidget>
#include <QMdiSubWindow>
#include <QGuiApplication>
#include <QWindow>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QToolBar>
#include <QToolButton>
#include <QComboBox>

#include "state.h"
#include "xlinkpreview.h"
#include "aboutdialog.h"
#include "exportdialog.h"
#include "tilepropertiesdialog.h"
#include "mappropertiesdialog.h"
#include "bigcharwidget.h"
#include "palette.h"
#include "importvicedialog.h"
#include "importkoaladialog.h"
#include "fileutils.h"
#include "serverconnectdialog.h"
#include "serverpreview.h"
#include "mapwidget.h"

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
    , _settings("RetroMoe","VChar64")
{
    setUnifiedTitleAndToolBarOnMac(true);

    _ui->setupUi(this);

    createActions();
    createDefaults();
    createUndoView();
    setupMapDock();
    setupStatusBar();
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
        modified |= static_cast<BigCharWidget*>(l->widget())->getState()->isModified();
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
    Q_UNUSED(newvalue);
    // make sure the "multicolor" checkbox is in the correct state.
    // this is needed for the "undo" / "redos"...
    auto state = getState();
    _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());

    // enable / disable radios based on newvalue.
    // this event could be trigged by just changing the PEN_FOREGROUND color
    // when in multicolor mode.
    // but also, this event could be triggered by clicking on multicolor checkbox
    // so using "newvalue" is not enough
    bool enableradios = state->shouldBeDisplayedInMulticolor();
    _ui->radioButton_multicolor1->setEnabled(enableradios);
    _ui->radioButton_multicolor2->setEnabled(enableradios);

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
        int color = state->getColorForPen(pen, state->getTileIndex());

        QString colors[] = {
            tr("Black"),
            tr("White"),
            tr("Red"),
            tr("Cyan"),
            tr("Violet"),
            tr("Green"),
            tr("Blue"),
            tr("Yellow"),

            tr("Orange"),
            tr("Brown"),
            tr("Light red"),
            tr("Dark grey"),
            tr("Grey"),
            tr("Light green"),
            tr("Light blue"),
            tr("Light grey")
        };

        int number = color;
        if (state->shouldBeDisplayedInMulticolor() && pen == State::PEN_FOREGROUND)
            color = color % 8;
        _labelSelectedColor->setText(QString("%1: %3 (%2)").arg(tr("Color")).arg(number).arg(colors[color]));

        // update radio foreground mode
        auto foregroundColorMode = state->getForegroundColorMode();
        if (foregroundColorMode == State::FOREGROUND_COLOR_PER_TILE)
            _ui->radioButton_charColorPerChar->setChecked(true);
        else _ui->radioButton_charColorGlobal->setChecked(true);
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
    _ui->mdiArea->closeAllSubWindows();
    if (_ui->mdiArea->subWindowList().size() > 0)
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
    _settings.setValue(QLatin1String("MainWindow/defaultGeometry"), saveGeometry());
    _settings.setValue(QLatin1String("MainWindow/defaultWindowState"), saveState(STATE_VERSION));

    auto geom = _settings.value(QLatin1String("MainWindow/geometry")).toByteArray();
    auto state = _settings.value(QLatin1String("MainWindow/windowState")).toByteArray();

    restoreState(state, STATE_VERSION);
    restoreGeometry(geom);

    QAction* actions[] = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
    };
    int index = _settings.value(QLatin1String("palette")).toInt();
    actions[index]->trigger();
}

void MainWindow::saveSettings()
{
    _settings.setValue(QLatin1String("MainWindow/geometry"), saveGeometry());
    _settings.setValue(QLatin1String("MainWindow/windowState"), saveState(STATE_VERSION));
    _settings.setValue(QLatin1String("palette"), Palette::getActivePalette());
}

void MainWindow::openDefaultDocument()
{
    on_actionC64DefaultUppercase_triggered();
}

BigCharWidget* MainWindow::createDocument(State* state)
{
    auto bigcharWidget = new BigCharWidget(state, this);
    auto subwindow = _ui->mdiArea->addSubWindow(bigcharWidget, Qt::Widget);
    _ui->mdiArea->setActiveSubWindow(subwindow);
    subwindow->showMaximized();
    subwindow->layout()->setContentsMargins(2, 2, 2, 2);

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

    // HACK:
    state->emitNewState();

    state->clearUndoStack();

    return bigcharWidget;
}

void MainWindow::closeState(State* state)
{
    Q_UNUSED(state);

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
    for (int i=0; i<2; i++)
    {
        spins[i]->setMinimum(1);
        spins[i]->setMaximum(4096);
        toolbar->addWidget(spins[i]);
    }
    connect(_spinBoxMapX, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onSpinBoxMapSizeX_valueChanged);
    connect(_spinBoxMapY, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onSpinBoxMapSizeY_valueChanged);


    toolbar->addSeparator();

    toolbar->addAction(_ui->actionToggle_Grid);

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

    _labelCharIdx = new QLabel(tr("Char: 000  $00"), this);
//    _labelCharIdx->setFrameStyle(QFrame::Panel | QFrame::Plain);
    statusBar()->addPermanentWidget(_labelCharIdx);

    _labelTileIdx = new QLabel(tr("Tile: 000  $00"), this);
//    _labelCharIdx->setFrameStyle(QFrame::Panel | QFrame::Plain);
    statusBar()->addPermanentWidget(_labelTileIdx);

    // display correct selected color
    auto state = getState();
    if (state)
        onColorPropertiesUpdated(state->getSelectedPen());
}

void MainWindow::updateMenus()
{
    bool withDocuments = (_ui->mdiArea->subWindowList().size() > 0);

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

        _ui->actionCopy,
        _ui->actionPaste,
        _ui->actionCut
    };
    const int TOTAL_ACTIONS = sizeof(actions)/sizeof(actions[0]);
    for (int i=0; i<TOTAL_ACTIONS; i++)
        actions[i]->setEnabled(withDocuments);
}

QStringList MainWindow::recentFiles() const
{
    QVariant v = _settings.value(QLatin1String("recentFiles/fileNames"));
    return v.toStringList();
}

void MainWindow::updateRecentFiles()
{
    QStringList files = recentFiles();
    const int numRecentFiles = qMin(files.size(), MAX_RECENT_FILES);

    for (int i = 0; i < numRecentFiles; ++i)
    {
        _recentFiles[i]->setText(FileUtils::getShortNativePath(files[i]));
        _recentFiles[i]->setData(files[i]);
        _recentFiles[i]->setVisible(true);
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

    QStringList files = recentFiles();
    files.removeAll(canonicalFilePath);
    files.prepend(canonicalFilePath);
    while (files.size() > MAX_RECENT_FILES)
        files.removeLast();

    _settings.setValue(QLatin1String("recentFiles/fileNames"), files);
    updateRecentFiles();
}

//
// MARK - Slots / Events / Callbacks
//
void MainWindow::setErrorMessage(const QString &errorMessage)
{
    statusBar()->showMessage(errorMessage, 6000);
}

void MainWindow::onCharIndexUpdated(int charIndex)
{
    auto state = getState();

    _labelCharIdx->setText(tr("Char: %1  $%2")
                           .arg(charIndex, 3, 10, QLatin1Char(' '))
                           .arg(charIndex, 2, 16, QLatin1Char('0')));

    int tileIndex = state->getTileIndexFromCharIndex(charIndex);

    _labelTileIdx->setText(tr("Tile: %1  $%2")
                           .arg(tileIndex, 3, 10, QLatin1Char(' '))
                           .arg(tileIndex, 2, 16, QLatin1Char('0')));

}

void MainWindow::on_actionExit_triggered()
{
    _ui->mdiArea->closeAllSubWindows();
    if (_ui->mdiArea->subWindowList().size() == 0)
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

void MainWindow::on_radioButton_background_clicked()
{
    activateRadioButtonIndex(State::PEN_BACKGROUND);
}

void MainWindow::on_radioButton_foreground_clicked()
{
    activateRadioButtonIndex(State::PEN_FOREGROUND);
}

void MainWindow::on_radioButton_multicolor1_clicked()
{
    activateRadioButtonIndex(State::PEN_MULTICOLOR1);
}

void MainWindow::on_radioButton_multicolor2_clicked()
{
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

void MainWindow::on_radioButton_charColorGlobal_clicked()
{
    auto state = getState();
    if (state)
        state->setForegroundColorMode(State::FOREGROUND_COLOR_GLOBAL);
}

void MainWindow::on_radioButton_charColorPerChar_clicked()
{
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
    };

    Palette::setActivePalette(paletteIndex);

    for (int i=0; i<5; i++)
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

//
// MARK - File IO callbacks + helper functions
//
bool MainWindow::openFile(const QString& path)
{
    QFileInfo info(path);
    _settings.setValue(QLatin1String("dir/lastdir"), info.absolutePath());

    bool ret = false;
    auto state = new State;
    if ( (ret=state->openFile(path)))
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
        QMessageBox::warning(this, tr("Application"), tr("Error loading file: ") + path, QMessageBox::Ok);
    }

    return ret;
}

void MainWindow::on_actionOpen_triggered()
{
    QString filter = _settings.value(QLatin1String("dir/lastUsedOpenFilter"), tr("All supported files")).toString();
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select File"),
                                           _settings.value(QLatin1String("dir/lastdir")).toString(),
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
        _settings.setValue(QLatin1String("dir/lastUsedOpenFilter"), filter);
        openFile(fn);
    }
}

void MainWindow::on_actionImport_VICE_snapshot_triggered()
{
    ImportVICEDialog dialog(this);
    if (dialog.exec())
    {
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(dialog.getFilepath());
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(QFileInfo(dialog.getFilepath()).baseName());
        setWindowFilePath(dialog.getFilepath());
    }
}

void MainWindow::on_actionImport_Koala_image_triggered()
{
    ImportKoalaDialog dialog(this);
    if (dialog.exec())
    {
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(dialog.getFilepath());
        _ui->mdiArea->currentSubWindow()->setWindowFilePath(QFileInfo(dialog.getFilepath()).baseName());
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
        fn = _settings.value(QLatin1String("dir/lastdir")).toString() + "/untitled.vchar64proj";
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
            _ui->mdiArea->currentSubWindow()->setWindowFilePath(fi.baseName());
            statusBar()->showMessage(tr("File saved to %1").arg(state->getSavedFilename()), 2000);
        }
        else
        {
            statusBar()->showMessage(tr("Error saving file"), 2000);
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
        if (state->export_())
        {
            statusBar()->showMessage(tr("File exported to %1").arg(state->getExportedFilename()), 2000);
        }
        else
        {
            statusBar()->showMessage(tr("Export failed"), 2000);
            QMessageBox::warning(this, tr("Application"), tr("Error exporting file: ") + exportedFilename, QMessageBox::Ok);
        }
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

    int cursorPos = (range.type == State::CopyRange::CHARS || range.type == State::CopyRange::TILES)
                ? _ui->charsetWidget->getCursorPos()
                : _ui->mapWidget->getCursorPos();

    state->cut(cursorPos, range);
}

void MainWindow::on_actionCopy_triggered()
{
    auto state = getState();
    bufferToClipboard(state);
}

void MainWindow::on_actionPaste_triggered()
{
    auto state = getState();

    quint8* buffer;
    State::CopyRange *range;
    if (bufferFromClipboard(&range, &buffer))
    {
        if (range->type == State::CopyRange::TILES && state->getTileProperties().size != range->tileProperties.size)
        {
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
    QDesktopServices::openUrl(QUrl("https://github.com/ricardoquesada/vchar64/issues"));
}

void MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/ricardoquesada/vchar64/wiki"));
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
    _settings.setValue(QLatin1String("recentFiles/fileNames"), QStringList());
    updateRecentFiles();
}

void MainWindow::onOpenRecentFileTriggered()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action)
    {
        auto path = action->data().toString();
        if (!openFile(path))
        {
            QStringList files = recentFiles();
            files.removeAll(path);
            _settings.setValue(QLatin1String("recentFiles/fileNames"), files);
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
            QMessageBox msgBox(QMessageBox::Warning, "", tr("Could not connect to remote C64"), 0, this);
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
                QMessageBox msgBox(QMessageBox::Warning, "", tr("Could not connect to remote server"), 0, this);
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
    auto geom = _settings.value(QLatin1String("MainWindow/defaultGeometry")).toByteArray();
    auto state = _settings.value(QLatin1String("MainWindow/defaultWindowState")).toByteArray();
    restoreState(state, STATE_VERSION);
    restoreGeometry(geom);

//    // center it
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignCenter,
            size(),
            qApp->desktop()->availableGeometry()
        )
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

void MainWindow::on_actionToggle_Grid_triggered()
{
    _ui->mapWidget->enableGrid(_ui->actionToggle_Grid->isChecked());
}

//
//
BigCharWidget* MainWindow::getBigcharWidget() const
{
    auto mdisubview = _ui->mdiArea->currentSubWindow();
    if (!mdisubview)
    {
        auto list = _ui->mdiArea->subWindowList(QMdiArea::WindowOrder::ActivationHistoryOrder);
        if (list.size() > 0)
            mdisubview = list.last();
        else
            return nullptr;
    }
    auto bigchar = static_cast<BigCharWidget*>(mdisubview->widget());

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
    if (_ui->charsetWidget->hasFocus())
        _ui->charsetWidget->getSelectionRange(&copyRange);
    else if (_ui->tilesetWidget->hasFocus())
        _ui->tilesetWidget->getSelectionRange(&copyRange);
    else if (_ui->mapWidget->hasFocus())
        _ui->mapWidget->getSelectionRange(&copyRange);

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

    clipboard->setMimeData(mimeData);

    return copyRange;
}

bool MainWindow::bufferFromClipboard(State::CopyRange **out_range, quint8** out_buffer) const
{
    QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    QByteArray bytearray = mimeData->data("vchar64/range");

    auto data = bytearray.data();
    *out_range = (State::CopyRange*) data;
    data += sizeof(State::CopyRange);
    *out_buffer = (quint8*) data;

    return true;
}

