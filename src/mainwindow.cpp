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

#include "state.h"
#include "aboutdialog.h"
#include "exportdialog.h"

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

void MainWindow::createActions()
{
    QObject::connect(_ui->charsetview, &CharSetView::charSelected, _ui->bigchar, &BigChar::setIndex);
    // FIXME: Qt5 API doesn't compile. Using Qt4 API
//    QObject::connect(ui->spinBox, &QSpinBox::valueChanged, _ui->bigchar, &BigChar::setIndex);
    QObject::connect(_ui->spinBox, SIGNAL(valueChanged(int)), _ui->bigchar, SLOT(setIndex(int)));
    QObject::connect(_ui->charsetview, &CharSetView::charSelected, _ui->spinBox, &QSpinBox::setValue);


    // Manually adding "about" menus
    _aboutAct = new QAction(tr("&About"), this);
    _aboutAct->setStatusTip(tr("Show the application's About box"));
    connect(_aboutAct, &QAction::triggered, this, &MainWindow::about);

    _aboutQtAct = new QAction(tr("About &Qt"), this);
    _aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(_aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);

    // FIXME should be on a different method
    _ui->colorRect_0->setColorIndex(0);
    _ui->colorRect_1->setColorIndex(3);
    _ui->colorRect_2->setColorIndex(1);
    _ui->colorRect_3->setColorIndex(2);
}

void MainWindow::createMenus()
{
    _helpMenu = menuBar()->addMenu(tr("&Help"));
    _helpMenu->addAction(_aboutAct);
    _helpMenu->addAction(_aboutQtAct);
}

void MainWindow::createDefaults()
{
    _lastDir = _settings.value("dir/lastdir", _lastDir).toString();
}

//
// Menu callbacks
//
void MainWindow::about()
{
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}


//
// Events
//
void MainWindow::on_actionExit_triggered()
{
    QApplication::exit();
}

void MainWindow::on_actionNew_Project_triggered()
{
    auto state = State::getInstance();
    state->reset();

    update();
}

void MainWindow::on_actionOpen_triggered()
{
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select File"),
                                           _lastDir,
                                           tr("VChar64 Project (*.vchar64proj")
                                           );

    if (fn.length()> 0) {
        QFileInfo info(fn);
        _lastDir = info.absolutePath();
        _settings.setValue("dir/lastdir", _lastDir);

        if (State::getInstance()->loadCharSet(fn)) {
            update();
            auto state = State::getInstance();
            _ui->checkBox->setChecked(state->isMultiColor());
        }
    }
}

void MainWindow::on_actionImport_triggered()
{
    QString file = "Any files";
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select File"),
                                           _lastDir,
                                           tr("Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;CharPad files (*.ctm);;Any file (*)"),
                                           &file
                                           /*,QFileDialog::DontUseNativeDialog*/);

    if (fn.length()> 0) {
        QFileInfo info(fn);
        _lastDir = info.absolutePath();
        _settings.setValue("dir/lastdir", _lastDir);

        if (State::getInstance()->loadCharSet(fn)) {
            update();

            auto state = State::getInstance();
            _ui->checkBox->setChecked(state->isMultiColor());
        }
    }
}


void MainWindow::on_checkBox_toggled(bool checked)
{
    _ui->radioButton_3->setEnabled(checked);
    _ui->radioButton_4->setEnabled(checked);

    State *state = State::getInstance();
    state->setMultiColor(checked);

    _ui->bigchar->repaint();
}

void MainWindow::on_radioButton_clicked()
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

void MainWindow::on_actionSave_As_triggered()
{
    QString dir = _lastDir + "/untitled.vchar64proj";
    auto filename = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                             dir,
                                             tr("VChar64 project(*.vchar64proj)"));

    if (filename.length() > 0) {
        auto state = State::getInstance();
        state->save(filename);
    }
}

void MainWindow::on_actionSave_triggered()
{
    auto state = State::getInstance();
}

void MainWindow::on_actionExport_triggered()
{
    ExportDialog dialog(this);
    dialog.exec();
}
