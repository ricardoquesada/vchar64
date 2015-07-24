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

#include "state.h"
#include "preview.h"
#include "aboutdialog.h"
#include "exportdialog.h"
#include "tilepropertiesdialog.h"
#include "bigchar.h"

constexpr int MainWindow::MAX_RECENT_FILES;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _lastDir(QDir::homePath())
    , _settings()
{
    _ui->setupUi(this);

    createActions();
    createDefaults();
}

MainWindow::~MainWindow()
{
    delete _ui;
}

void MainWindow::setTitle(const QString &title)
{
    emit setWindowTitle(title + " - VChar64");
}

void MainWindow::previewConnected()
{
    _ui->actionXlinkConnection->setText("Disconnect");
}

void MainWindow::previewDisconnected()
{
    _ui->actionXlinkConnection->setText("Connect");
}

void MainWindow::createActions()
{
    auto state = State::getInstance();
    connect(state, &State::tilePropertiesUpdated, _ui->bigchar, &BigChar::updateTileProperties);

    // Add recent file actions to the recent files menu
    for (int i=0; i<MAX_RECENT_FILES; ++i)
    {
         _recentFiles[i] = new QAction(this);
         _ui->menuRecentFiles->insertAction(_ui->actionClearRecentFiles, _recentFiles[i]);
         _recentFiles[i]->setVisible(false);
         connect(_recentFiles[i], &QAction::triggered, this, &MainWindow::on_openRecentFile_triggered);
    }
    _ui->menuRecentFiles->insertSeparator(_ui->actionClearRecentFiles);
    updateRecentFiles();


    // FIXME should be on a different method
    _ui->colorRect_0->setColorIndex(0);
    _ui->colorRect_1->setColorIndex(3);
    _ui->colorRect_2->setColorIndex(1);
    _ui->colorRect_3->setColorIndex(2);

    auto preview = Preview::getInstance();
    connect(state, SIGNAL(fileLoaded()), preview, SLOT(fileLoaded()));
    connect(state, SIGNAL(byteUpdated(int)), preview, SLOT(byteUpdated(int)));
    connect(state, SIGNAL(tileUpdated(int)), preview, SLOT(tileUpdated(int)));
    connect(state, SIGNAL(colorPropertiesUpdated()), preview, SLOT(colorPropertiesUpdated()));
    connect(_ui->colorPalette, SIGNAL(colorSelected()), preview, SLOT(colorSelected()));
    connect(preview, SIGNAL(previewConnected()), this, SLOT(previewConnected()));
    connect(preview, SIGNAL(previewDisconnected()), this, SLOT(previewDisconnected()));

    _ui->menuPreview->setEnabled(preview->isAvailable());

     if(preview->isConnected()) {
          previewConnected();
    }
}

void MainWindow::createDefaults()
{
    _lastDir = _settings.value("dir/lastdir", _lastDir).toString();
    setTitle("[untitled]");

    auto state = State::getInstance();
    state->openFile(":/c64-chargen.bin");

    State::TileProperties properties;
    properties.size = {1,1};
    properties.interleaved = 1;
    state->setTileProperties(properties);

    state->setMultiColor(false);
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

QStringList MainWindow::recentFiles() const
{
    QVariant v = _settings.value(QLatin1String("recentFiles/fileNames"));
    return v.toStringList();
}

void MainWindow::openFile(const QString& fileName)
{
    QFileInfo info(fileName);
    _lastDir = info.absolutePath();
    _settings.setValue("dir/lastdir", _lastDir);

    if (State::getInstance()->openFile(fileName)) {

        setRecentFile(fileName);

        update();
        auto state = State::getInstance();
        _ui->checkBox->setChecked(state->isMultiColor());

        setTitle(info.baseName());
    }
}

//
// MARK - Slots / Events / Callbacks
//
void MainWindow::on_actionExit_triggered()
{
    QApplication::exit();
}

void MainWindow::on_actionEmptyProject_triggered()
{
    auto state = State::getInstance();
    state->reset();
    update();
    setTitle("[untitled]");
}

void MainWindow::on_actionC64Default_triggered()
{
    auto state = State::getInstance();
    state->openFile(":/c64-chargen.bin");
    State::TileProperties properties;
    properties.size = {1,1};
    properties.interleaved = 1;
    state->setTileProperties(properties);
    state->setMultiColor(false);

    update();
    setTitle("[untitled]");
}

void MainWindow::on_actionOpen_triggered()
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

void MainWindow::on_checkBox_toggled(bool checked)
{
    _ui->radioButton_3->setEnabled(checked);
    _ui->radioButton_4->setEnabled(checked);

    State *state = State::getInstance();
    state->setMultiColor(checked);

    update();
}

void MainWindow::on_radioButton_1_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(0);
}

void MainWindow::on_radioButton_2_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(3);
}

void MainWindow::on_radioButton_3_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(1);
}

void MainWindow::on_radioButton_4_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(2);
}

void MainWindow::on_actionSaveAs_triggered()
{
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

    if (filename.length() > 0) {
        auto state = State::getInstance();
        state->saveProject(filename);

        QFileInfo fi(filename);
        setTitle(fi.baseName());
    }
}

void MainWindow::on_actionSave_triggered()
{
    auto state = State::getInstance();
    auto filename = state->getSavedFilename();
    if (filename.length() > 0)
        state->saveProject(filename);
    else
        on_actionSaveAs_triggered();
}

void MainWindow::on_actionExport_triggered()
{
    auto state = State::getInstance();
    auto exportedFilename = state->getExportedFilename();
    if (exportedFilename.length()==0)
    {
        ExportDialog dialog(this);
        dialog.exec();
    }
    else
    {
        state->export_();
    }
}

void MainWindow::on_actionExportAs_triggered()
{
    ExportDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionInvert_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileInvert(tileIndex);

    update();
}

//
// Tile editing callbacks
//

void MainWindow::on_actionFlipHorizontally_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileFlipHorizontally(tileIndex);

    update();
}

void MainWindow::on_actionFlipVertically_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileFlipVertically(tileIndex);

    update();
}

void MainWindow::on_actionRotate_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileRotate(tileIndex);

    update();
}

void MainWindow::on_actionClearCharacter_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();
    state->tileClear(tileIndex);

    update();
}

void MainWindow::on_actionShiftLeft_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileShiftLeft(tileIndex);

    update();
}

void MainWindow::on_actionShiftRight_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileShiftRight(tileIndex);

    update();
}

void MainWindow::on_actionShiftUp_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileShiftUp(tileIndex);

    update();
}

void MainWindow::on_actionShiftDown_triggered()
{
    auto state = State::getInstance();
    int tileIndex = _ui->bigchar->getTileIndex();

    state->tileShiftDown(tileIndex);

    update();
}

void MainWindow::on_actionCopy_triggered()
{
    auto state = State::getInstance();
    state->tileCopy(_ui->bigchar->getTileIndex());
}

void MainWindow::on_actionPaste_triggered()
{
    auto state = State::getInstance();
    state->tilePaste(_ui->bigchar->getTileIndex());
    update();
}

void MainWindow::on_actionReportBug_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/ricardoquesada/vchar64/issues"));
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

void MainWindow::on_openRecentFile_triggered()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action)
        openFile(action->data().toString());
}

void MainWindow::on_actionTilesProperties_triggered()
{
    TilePropertiesDialog dialog(this);

    dialog.exec();

    // update max tile index
    auto state = State::getInstance();
    QSize s = state->getTileProperties().size;
    _ui->spinBox->setMaximum((256 / (s.width()*s.height()))-1);

    // rotate only enable if witdh==height
    _ui->actionRotate->setEnabled(s.width() == s.height());
}

void MainWindow::on_actionXlinkConnection_triggered()
{
    auto preview = Preview::getInstance();
    if(preview->isConnected())
        preview->disconnect();
    else
        if(!preview->connect()) {
            QMessageBox msgBox(QMessageBox::Warning, "", "Could not connect to remote C64", 0, this);
            msgBox.exec();
        }
}
