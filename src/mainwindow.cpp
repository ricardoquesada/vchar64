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
}

void MainWindow::on_radioButton_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColor(0);
}

void MainWindow::on_radioButton_2_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColor(1);
}

void MainWindow::on_radioButton_3_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColor(2);
}

void MainWindow::on_radioButton_4_clicked()
{
    State *state = State::getInstance();
    state->setSelectedColor(3);
}
