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

#include "state.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _lastDir(QDir::homePath())
{
    _ui->setupUi(this);

    createActions();
    createMenus();
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
}

void MainWindow::createMenus()
{
    _helpMenu = menuBar()->addMenu(tr("&Help"));
    _helpMenu->addAction(_aboutAct);
    _helpMenu->addAction(_aboutQtAct);
}

//
// Menu callbacks
//
void MainWindow::about()
{
   QMessageBox::about(this, tr("About Application"),
            tr("The <b>Application</b> example demonstrates how to "
               "write modern GUI applications using Qt, with a menu bar, "
               "toolbars, and a status bar."));
}


//
// Events
//
void MainWindow::on_actionExit_triggered()
{
    QApplication::exit();
}

void MainWindow::on_actionOpen_triggered()
{
    auto fn = QFileDialog::getOpenFileName(this, "Select File", _lastDir);

    if (fn.length()> 0) {
        QFileInfo info(fn);
        _lastDir = info.absolutePath();

        if (State::getInstance()->loadCharSet(fn)) {
            _ui->bigchar->repaint();
            _ui->charsetview->repaint();
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
    state->setSelectedColorIndex(1);
}

void MainWindow::on_radioButton_3_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(2);
}

void MainWindow::on_radioButton_4_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColorIndex(3);
}

