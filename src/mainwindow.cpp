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
#include "aboutdialog.h"
#include "exportdialog.h"

constexpr int MainWindow::MAX_RECENT_FILES;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _lastDir(QDir::homePath())
    , _settings()
{
    _ui->setupUi(this);

    createActions();
    createMenus();
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

void MainWindow::createActions()
{
    connect(_ui->charsetview, &CharSetView::charSelected, _ui->bigchar, &BigChar::setIndex);
    connect(_ui->charsetview, &CharSetView::charSelected, _ui->spinBox, &QSpinBox::setValue);

    // FIXME should be on a different method
    _ui->colorRect_0->setColorIndex(0);
    _ui->colorRect_1->setColorIndex(3);
    _ui->colorRect_2->setColorIndex(1);
    _ui->colorRect_3->setColorIndex(2);
}

void MainWindow::createMenus()
{
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
}

void MainWindow::createDefaults()
{
    _lastDir = _settings.value("dir/lastdir", _lastDir).toString();
    setTitle("[untitled]");
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

void MainWindow::on_actionNewProject_triggered()
{
    auto state = State::getInstance();
    state->reset();

    update();

    setTitle("[untitled]");
}

void MainWindow::on_actionOpen_triggered()
{
    QString file = "VChar64 Project";
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select File"),
                                           _lastDir,
                                           tr("VChar64 Project (*.vchar64proj);;Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;CharPad files (*.ctm);;Any file (*)"),
                                           &file
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0) {
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
    auto fn = state->getFilename();
    if (fn.length() == 0)
        fn = _lastDir + "/untitled.vchar64proj";
    auto filename = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                             fn,
                                             tr("VChar64 project(*.vchar64proj)"));

    if (filename.length() > 0) {
        auto state = State::getInstance();
        state->save(filename);

        QFileInfo fi(filename);
        setTitle(fi.baseName());
    }
}

void MainWindow::on_actionSave_triggered()
{
    auto state = State::getInstance();
    auto filename = state->getFilename();
    if (filename.length() > 0)
        state->save(filename);
    else
        on_actionSaveAs_triggered();
}

void MainWindow::on_actionExport_triggered()
{
    ExportDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionInvert_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++) {
        buffer[i] = ~buffer[i];
    }

    update();
}

void MainWindow::on_actionFlipHorizontally_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++) {
        char tmp = 0;
        for (int j=0; j<8; j++) {
            if (buffer[i] & (1<<j))
                tmp |= 1 << (7-j);
        }
        buffer[i] = tmp;
    }

    update();
}

void MainWindow::on_actionFlipVertically_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<4; i++) {
        std::swap(buffer[i], buffer[7-i]);
    }

    update();
}

void MainWindow::on_actionRotate_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    u_int8_t tmp[8];
    memset(tmp, 0, sizeof(tmp));

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++) {
        for (int j=0; j<8; j++) {
            if (buffer[i] & (1<<(7-j)))
                tmp[j] |= (1<<i);
        }
    }

    for (int i=0; i<8; i++)
        buffer[i] = tmp[i];

    update();
}

void MainWindow::on_actionClearCharacter_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++)
        buffer[i] = 0;

    update();
}

void MainWindow::on_actionShiftLeft_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++) {
        bool highbit = buffer[i] & (1<<7);
        buffer[i] <<= 1;
        buffer[i] |= highbit;
    }

    update();
}

void MainWindow::on_actionShiftRight_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);
    for (int i=0; i<8; i++) {
        bool lowbit = buffer[i] & (1<<0);
        buffer[i] >>= 1;
        if (lowbit)
        buffer[i] |= (1<<7);
    }

    update();
}

void MainWindow::on_actionShiftUp_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);

    auto tmp = buffer[0];

    for (int i=0; i<7; i++) {
        buffer[i] = buffer[i+1];
    }

    buffer[7] = tmp;

    update();
}

void MainWindow::on_actionShiftDown_triggered()
{
    auto state = State::getInstance();

    int index = _ui->bigchar->getIndex();

    auto buffer = state->getCharAtIndex(index);

    auto tmp = buffer[7];

    for (int i=6; i>=0; i--) {
        buffer[i+1] = buffer[i];
    }

    buffer[0] = tmp;

    update();
}

void MainWindow::on_actionCopy_triggered()
{
    auto state = State::getInstance();
    state->copyChar(_ui->bigchar->getIndex());
}

void MainWindow::on_actionPaste_triggered()
{
    auto state = State::getInstance();
    state->pasteChar(_ui->bigchar->getIndex());
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
