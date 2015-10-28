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

#include "state.h"
#include "xlinkpreview.h"
#include "aboutdialog.h"
#include "exportdialog.h"
#include "tilepropertiesdialog.h"
#include "bigcharwidget.h"
#include "commands.h"
#include "palette.h"
#include "importvicedialog.h"

constexpr int MainWindow::MAX_RECENT_FILES;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _lastDir(QDir::homePath())
    , _settings("RetroMoe","VChar64")
{
    setUnifiedTitleAndToolBarOnMac(true);

    _ui->setupUi(this);

    createActions();
    createDefaults();
    createUndoView();
    setupStatusBar();

    readSettings();
}

MainWindow::~MainWindow()
{
    delete _ui;
}

//
// public slots
//
void MainWindow::previewConnected()
{
    _ui->actionXlinkConnection->setText(tr("Disconnect"));
}

void MainWindow::previewDisconnected()
{
    _ui->actionXlinkConnection->setText(tr("Connect"));
}

void MainWindow::documentWasModified()
{
    auto state = State::getInstance();
    setWindowModified(state->isModified());
}

void MainWindow::updateWindow()
{
    update();

    // update event() does not get propagated if dockWidget are floating.
    // manual update it
    if (_ui->dockWidget_charset->isFloating())
        _ui->dockWidget_charset->update();
    if (_ui->dockWidget_colors->isFloating())
        _ui->dockWidget_colors->update();
    if (_ui->dockWidget_tileIndex->isFloating())
        _ui->dockWidget_tileIndex->update();
    if (_ui->dockWidget_tileset->isFloating())
        _ui->dockWidget_tileset->update();
}

void MainWindow::onTilePropertiesUpdated()
{
    // update max tile index
    auto state = State::getInstance();
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
    auto state = State::getInstance();
    _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());

    // enable / disable radios based on newvalue.
    // this event could be trigged by just changing the PEN_FOREGROUND color
    // when in multicolor mode.
    // but also, this event could be triggered by clicking on multicolor checkbox
    // so using "newvalue" is not enough
    bool enableradios = state->shouldBeDisplayedInMulticolor();
    _ui->radioButton_multicolor1->setEnabled(enableradios);
    _ui->radioButton_multicolor2->setEnabled(enableradios);

    updateWindow();
}

void MainWindow::onColorPropertiesUpdated(int pen)
{
    auto state = State::getInstance();
    int color = state->getColorForPen(pen);
    updateWindow();

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
        tr("Grey 1"),
        tr("Grey 2"),
        tr("Light green"),
        tr("Light blue"),
        tr("Grey 3")
    };

    int number = color;
    if (state->shouldBeDisplayedInMulticolor() && pen == State::PEN_FOREGROUND)
        color = color % 8;
    _labelSelectedColor->setText(QString("%1: %3 (%2)").arg(tr("Color")).arg(number).arg(colors[color]));
}

//
//
//
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        event->accept();

        saveSettings();
        QMainWindow::closeEvent(event);
    } else {
        event->ignore();
    }
}

void MainWindow::createUndoView()
{
    auto undoDock = new QDockWidget(tr("Undo List"), this);

    auto state = State::getInstance();
    auto undoView = new QUndoView(state->getUndoStack(), undoDock);

    undoDock->setWidget(undoView);
    undoDock->setFloating(true);
    undoDock->hide();

    _ui->menuViews->addAction(undoDock->toggleViewAction());
}

void MainWindow::readSettings()
{
    // before restoring settings, save the current layout
    // needed for "reset layout"
    _settings.setValue("MainWindow/defaultGeometry", saveGeometry());
    _settings.setValue("MainWindow/defaultWindowState", saveState());


    auto geom = _settings.value("MainWindow/geometry").toByteArray();
    auto state = _settings.value("MainWindow/windowState").toByteArray();

    restoreState(state);
    restoreGeometry(geom);

    QAction* actions[] = {
        _ui->actionPalette_0,
        _ui->actionPalette_1,
        _ui->actionPalette_2,
        _ui->actionPalette_3,
        _ui->actionPalette_4,
    };
    int index = _settings.value("palette").toInt();
    actions[index]->trigger();
}

void MainWindow::saveSettings()
{
    _settings.setValue("MainWindow/geometry", saveGeometry());
    _settings.setValue("MainWindow/windowState", saveState());
    _settings.setValue("palette", Palette::getActivePalette());
}

void MainWindow::createActions()
{
    auto state = State::getInstance();

    // Add recent file actions to the recent files menu
    for (int i=0; i<MAX_RECENT_FILES; ++i)
    {
         _recentFiles[i] = new QAction(this);
         _ui->menuRecentFiles->insertAction(_ui->actionClearRecentFiles, _recentFiles[i]);
         _recentFiles[i]->setVisible(false);
         connect(_recentFiles[i], &QAction::triggered, this, &MainWindow::onOpenRecentFileTriggered);
    }
    _ui->menuRecentFiles->insertSeparator(_ui->actionClearRecentFiles);
    updateRecentFiles();


    // FIXME should be on a different method
    _ui->colorRectWidget_0->setPen(State::PEN_BACKGROUND);
    _ui->colorRectWidget_1->setPen(State::PEN_FOREGROUND);
    _ui->colorRectWidget_2->setPen(State::PEN_MULTICOLOR1);
    _ui->colorRectWidget_3->setPen(State::PEN_MULTICOLOR2);

    auto preview = XlinkPreview::getInstance();
    connect(state, &State::fileLoaded, preview, &XlinkPreview::fileLoaded);
    connect(state, &State::byteUpdated, preview, &XlinkPreview::byteUpdated);
    connect(state, &State::bytesUpdated, preview, &XlinkPreview::bytesUpdated);
    connect(state, &State::tileUpdated, preview, &XlinkPreview::tileUpdated);
    connect(state, &State::colorPropertiesUpdated, preview, &XlinkPreview::colorPropertiesUpdated);
    connect(state, &State::multicolorModeToggled, preview, &XlinkPreview::colorPropertiesUpdated);

    connect(state, &State::tilePropertiesUpdated, this, &MainWindow::onTilePropertiesUpdated);
    connect(state, &State::tilePropertiesUpdated, _ui->bigcharWidget, &BigCharWidget::onUpdateTileProperties);
    connect(state, &State::tilePropertiesUpdated, _ui->tilesetWidget, &TilesetWidget::onTilePropertiesUpdated);

    connect(state, &State::byteUpdated, this, &MainWindow::updateWindow);
    connect(state, &State::tileUpdated, this, &MainWindow::updateWindow);
    connect(state, &State::charIndexUpdated, this, &MainWindow::onCharIndexUpdated);
    connect(state, &State::charsetUpdated, this, &MainWindow::updateWindow);
    connect(state, &State::fileLoaded, this, &MainWindow::updateWindow);
    connect(state, &State::colorPropertiesUpdated, this, &MainWindow::onColorPropertiesUpdated);
    connect(state, &State::multicolorModeToggled, this, &MainWindow::onMulticolorModeToggled);
    connect(state, &State::contentsChanged, this, &MainWindow::documentWasModified);

    connect(state, &State::errorMessageSet, this, &MainWindow::setErrorMessage);

    connect(state, &State::tileIndexUpdated, _ui->tilesetWidget, &TilesetWidget::onTileIndexUpdated);
    connect(state, &State::tileIndexUpdated, _ui->bigcharWidget, &BigCharWidget::onTileIndexUpdated);
    connect(state, &State::charIndexUpdated, _ui->charsetWidget, &CharSetWidget::onCharIndexUpdated);
    connect(state, &State::tileIndexUpdated, _ui->spinBox_tileIndex, &QSpinBox::setValue);
    connect(_ui->spinBox_tileIndex, SIGNAL(valueChanged(int)), state, SLOT(setTileIndex(int)));

    connect(state->getUndoStack(), &QUndoStack::indexChanged, this, &MainWindow::documentWasModified);
    connect(state->getUndoStack(), &QUndoStack::cleanChanged, this, &MainWindow::documentWasModified);

    connect(_ui->paletteWidget, &PaletteWidget::colorSelected, preview, &XlinkPreview::colorSelected);
    connect(preview, &XlinkPreview::previewConnected, this, &MainWindow::previewConnected);
    connect(preview, &XlinkPreview::previewDisconnected, this, &MainWindow::previewDisconnected);

    connect(_ui->paletteWidget, &PaletteWidget::colorSelected, _ui->bigcharWidget, &BigCharWidget::updateColor);
    connect(_ui->paletteWidget, &PaletteWidget::colorSelected, _ui->charsetWidget, &CharSetWidget::updateColor);
    connect(_ui->paletteWidget, &PaletteWidget::colorSelected, _ui->tilesetWidget, &TilesetWidget::updateColor);

    _ui->menuPreview->setEnabled(preview->isAvailable());

//
    _ui->menuViews->addAction(_ui->dockWidget_charset->toggleViewAction());
    _ui->menuViews->addAction(_ui->dockWidget_tileset->toggleViewAction());
    _ui->menuViews->addAction(_ui->dockWidget_colors->toggleViewAction());
    _ui->menuViews->addAction(_ui->dockWidget_tileIndex->toggleViewAction());

     if(preview->isConnected()) {
          previewConnected();
    }
}

void MainWindow::createDefaults()
{
    _lastDir = _settings.value("dir/lastdir", _lastDir).toString();

    auto state = State::getInstance();
    state->openFile(":/c64-chargen-uppercase.bin");

    State::TileProperties properties;
    properties.size = {1,1};
    properties.interleaved = 1;
    state->setTileProperties(properties);

    state->setMulticolorMode(false);

    setWindowFilePath("[untitled]");

    // tabify charsetWidget and tilesetWidget
    tabifyDockWidget(_ui->dockWidget_charset, _ui->dockWidget_tileset);
    // select charsetWidget as the default one
    _ui->dockWidget_charset->raise();
}

void MainWindow::setupStatusBar()
{
    _labelSelectedColor = new QLabel(tr("Color: Black (0)"), this);
    statusBar()->addPermanentWidget(_labelSelectedColor);

    statusBar()->addPermanentWidget(new QLabel(" | ", this));

    _labelCharIdx = new QLabel(tr("Char: #000  $00"), this);
//    _labelCharIdx->setFrameStyle(QFrame::Panel | QFrame::Plain);
    statusBar()->addPermanentWidget(_labelCharIdx);
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
        _recentFiles[i]->setText(QFileInfo(files[i]).fileName());
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
    _labelCharIdx->setText(tr("Char: #%1  $%2")
                           .arg(charIndex, 3, 10, QLatin1Char('0'))
                           .arg(charIndex, 2, 16, QLatin1Char('0')));
}

void MainWindow::on_actionExit_triggered()
{
    if (maybeSave()) {
        saveSettings();
        QApplication::exit();
    }
}

void MainWindow::on_actionEmptyProject_triggered()
{
    if (maybeSave())
    {
        auto state = State::getInstance();
        state->reset();
        updateWindow();
        setWindowFilePath(tr("[untitled]"));
    }
}

void MainWindow::on_actionC64DefaultUppercase_triggered()
{
    if (maybeSave())
    {
        auto state = State::getInstance();
        state->openFile(":/c64-chargen-uppercase.bin");
        State::TileProperties properties;
        properties.size = {1,1};
        properties.interleaved = 1;
        state->setTileProperties(properties);
        state->setMulticolorMode(false);

        updateWindow();
        setWindowFilePath(tr("[untitled]"));
    }
}

void MainWindow::on_actionC64DefaultLowercase_triggered()
{
    if (maybeSave())
    {
        auto state = State::getInstance();
        state->openFile(":/c64-chargen-lowercase.bin");
        State::TileProperties properties;
        properties.size = {1,1};
        properties.interleaved = 1;
        state->setTileProperties(properties);
        state->setMulticolorMode(false);

        updateWindow();
        setWindowFilePath(tr("[untitled]"));
    }
}

//
// MARK - Color + Palette callbacks
//
void MainWindow::on_checkBox_multicolor_toggled(bool checked)
{
    _ui->radioButton_multicolor1->setEnabled(checked);
    _ui->radioButton_multicolor2->setEnabled(checked);

    _ui->actionMulti_Color_1->setEnabled(checked);
    _ui->actionMulti_Color_2->setEnabled(checked);

    _ui->actionEnable_Multicolor->setChecked(checked);

    State *state = State::getInstance();

    state->getUndoStack()->push(new SetMulticolorModeCommand(state, checked));
}

void MainWindow::activateRadioButtonIndex(int pen)
{
    State *state = State::getInstance();
    state->setSelectedPen(pen);

    QAction* actions[] = {
        _ui->actionBackground,
        _ui->actionMulti_Color_1,
        _ui->actionMulti_Color_2,
        _ui->actionForeground
    };

    for (int i=0; i<4; i++)
        actions[i]->setChecked(i==pen);
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
    updateWindow();
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
void MainWindow::openFile(const QString& path)
{
    QFileInfo info(path);
    _lastDir = info.absolutePath();
    _settings.setValue("dir/lastdir", _lastDir);

    if (State::getInstance()->openFile(path)) {

        setRecentFile(path);

        auto state = State::getInstance();
        _ui->checkBox_multicolor->setChecked(state->isMulticolorMode());

        setWindowFilePath(info.filePath());
    }
    else
    {
        QMessageBox::warning(this, tr("Application"), tr("Error loading file: ") + path, QMessageBox::Ok);
    }
}

bool MainWindow::maybeSave()
{
    auto state = State::getInstance();

    if (state->isModified()) {
        QMessageBox::StandardButton ret;
        ret = QMessageBox::warning(this, tr("Application"),
                     tr("The are unsaved changes.\n"
                        "Do you want to save your changes?"),
                     QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Save)
            return on_actionSave_triggered();
        else if (ret == QMessageBox::Cancel)
            return false;
    }
    return true;
}

void MainWindow::on_actionOpen_triggered()
{
    if (maybeSave())
    {
        QString filter = _settings.value("dir/lastUsedOpenFilter", "All supported files").toString();
        auto fn = QFileDialog::getOpenFileName(this,
                                               tr("Select File"),
                                               _lastDir,
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
            _settings.setValue("dir/lastUsedOpenFilter", filter);
            openFile(fn);
        }
    }
}

void MainWindow::on_actionImport_VICE_snapshot_triggered()
{
    if (maybeSave())
    {
        ImportVICEDialog dialog;
        if (dialog.exec())
        {
            setWindowFilePath(dialog.getFilepath());
        }
    }
}

bool MainWindow::on_actionSaveAs_triggered()
{
    bool ret = false;

    auto state = State::getInstance();
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
        fn = _lastDir + "/untitled.vchar64proj";
    }
    auto filename = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                             fn,
                                             tr("VChar64 project(*.vchar64proj)"));

    if (filename.length() > 0)
    {
        auto state = State::getInstance();
        if ((ret=state->saveProject(filename)))
        {
            QFileInfo fi(filename);
            setWindowFilePath(fi.filePath());
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
    auto state = State::getInstance();
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
    auto state = State::getInstance();
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
    ExportDialog dialog(this);
    dialog.exec();
}

//
// MARK - Tile editing callbacks
//
void MainWindow::on_actionInvert_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new InvertTileCommand(state, tileIndex));
}

void MainWindow::on_actionFlipHorizontally_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new FlipTileHCommand(state, tileIndex));
}

void MainWindow::on_actionFlipVertically_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new FlipTileVCommand(state, tileIndex));
}

void MainWindow::on_actionRotate_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new RotateTileCommand(state, tileIndex));
}

void MainWindow::on_actionClearCharacter_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new ClearTileCommand(state, tileIndex));
}

void MainWindow::on_actionShiftLeft_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new ShiftLeftTileCommand(state, tileIndex));
}

void MainWindow::on_actionShiftRight_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new ShiftRightTileCommand(state, tileIndex));
}

void MainWindow::on_actionShiftUp_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new ShiftUpTileCommand(state, tileIndex));
}

void MainWindow::on_actionShiftDown_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigcharWidget->getTileIndex();

    state->getUndoStack()->push(new ShiftDownTileCommand(state, tileIndex));
}

void MainWindow::on_actionCut_triggered()
{
    auto state = State::getInstance();
    int cursorPos = _ui->charsetWidget->getCursorPos();

    State::CopyRange copyRange;
    _ui->charsetWidget->getSelectionRange(&copyRange);
    state->copy(copyRange);
    state->getUndoStack()->push(new CutCommand(state, cursorPos, state->getCopyRange()));
}

void MainWindow::on_actionCopy_triggered()
{
    auto state = State::getInstance();

    State::CopyRange copyRange;
    _ui->charsetWidget->getSelectionRange(&copyRange);
    state->copy(copyRange);
}

void MainWindow::on_actionPaste_triggered()
{
    auto state = State::getInstance();
    int cursorPos = _ui->charsetWidget->getCursorPos();

    state->getUndoStack()->push(new PasteCommand(state, cursorPos, state->getCopyRange()));
}

//
// MARK - Undo / Redo callbacks
//
void MainWindow::on_actionUndo_triggered()
{
    auto state = State::getInstance();
    state->getUndoStack()->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    auto state = State::getInstance();
    state->getUndoStack()->redo();
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
        openFile(action->data().toString());
}

void MainWindow::on_actionTilesProperties_triggered()
{
    TilePropertiesDialog dialog(this);

    dialog.exec();
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
    auto geom = _settings.value("MainWindow/defaultGeometry").toByteArray();
    auto state = _settings.value("MainWindow/defaultWindowState").toByteArray();
    restoreState(state);
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
