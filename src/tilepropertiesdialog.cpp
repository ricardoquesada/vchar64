#include "tilepropertiesdialog.h"
#include "ui_tilepropertiesdialog.h"

#include "state.h"

TilePropertiesDialog::TilePropertiesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TilePropertiesDialog)
{
    ui->setupUi(this);

    auto state = State::getInstance();
    QSize tileSize = state->getTileSize();
    ui->spinBoxSizeX->setValue(tileSize.width());
    ui->spinBoxSizeY->setValue(tileSize.height());
    ui->spinBoxInterleaved->setValue(state->getCharInterleaved());
}

TilePropertiesDialog::~TilePropertiesDialog()
{
    delete ui;
}

void TilePropertiesDialog::on_buttonBox_accepted()
{
    int w = ui->spinBoxSizeX->value();
    int h = ui->spinBoxSizeY->value();
    int interleaved =  ui->spinBoxInterleaved->value();

    auto state = State::getInstance();
    state->setTileSize(QSize(w,h));
    state->setCharInterleaved(interleaved);

    emit tilePropertiesChanged();
}
