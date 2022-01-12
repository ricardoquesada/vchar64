#include "mappropertiesdialog.h"
#include "ui_mappropertiesdialog.h"

#include "state.h"

MapPropertiesDialog::MapPropertiesDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::MapPropertiesDialog)
    , _state(state)
{
    ui->setupUi(this);

    auto mapSize = state->getMapSize();
    ui->spinBox_width->setValue(mapSize.width());
    ui->spinBox_height->setValue(mapSize.height());
}

MapPropertiesDialog::~MapPropertiesDialog()
{
    delete ui;
}

void MapPropertiesDialog::on_buttonBox_accepted()
{
    int w = ui->spinBox_width->value();
    int h = ui->spinBox_height->value();
    _state->setMapSize(QSize(w, h));
}
