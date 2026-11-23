// MapPropertiesDialog implementation for editing the document's tile map dimensions.

#include "mappropertiesdialog.h"
#include "ui_mappropertiesdialog.h"

#include <QSize>

#include "state.h"

MapPropertiesDialog::MapPropertiesDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::MapPropertiesDialog)
    , _state(state)
{
    ui->setupUi(this);

    const QSize map_size = state->getMapSize();
    ui->spinBox_width->setValue(map_size.width());
    ui->spinBox_height->setValue(map_size.height());
}

MapPropertiesDialog::~MapPropertiesDialog()
{
    delete ui;
}

void MapPropertiesDialog::on_buttonBox_accepted()
{
    const int width = ui->spinBox_width->value();
    const int height = ui->spinBox_height->value();
    _state->setMapSize(QSize(width, height));
}
