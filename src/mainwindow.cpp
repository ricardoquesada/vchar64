#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QObject::connect(ui->charsetview, &CharSetView::charSelected, ui->bigchar, &BigChar::setIndex);
    // FIXME: Qt5 API doesn't compile. Using Qt4 API
//    QObject::connect(ui->spinBox, &QSpinBox::valueChanged, ui->bigchar, &BigChar::setIndex);
    QObject::connect(ui->spinBox, SIGNAL(valueChanged(int)), ui->bigchar, SLOT(setIndex(int)));
    QObject::connect(ui->charsetview, &CharSetView::charSelected, ui->spinBox, &QSpinBox::setValue);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_bigchar_indexChanged(int index)
{
    //
}

void MainWindow::on_charsetview_charSelected(int index)
{

}
