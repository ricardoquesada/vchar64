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

#include "state.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _ui(new Ui::MainWindow)
    , _lastDir(QDir::homePath())
{
    _ui->setupUi(this);

    QObject::connect(_ui->charsetview, &CharSetView::charSelected, _ui->bigchar, &BigChar::setIndex);
    // FIXME: Qt5 API doesn't compile. Using Qt4 API
//    QObject::connect(ui->spinBox, &QSpinBox::valueChanged, _ui->bigchar, &BigChar::setIndex);
    QObject::connect(_ui->spinBox, SIGNAL(valueChanged(int)), _ui->bigchar, SLOT(setIndex(int)));
    QObject::connect(_ui->charsetview, &CharSetView::charSelected, _ui->spinBox, &QSpinBox::setValue);
}

MainWindow::~MainWindow()
{
    delete _ui;
}

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

