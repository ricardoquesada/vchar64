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

#include "importvicedialog.h"
#include "ui_importvicedialog.h"

#include <QDebug>
#include <QFileDialog>

#include "mainwindow.h"
#include "preferences.h"
#include "state.h"
#include "utils.h"

ImportVICEDialog::ImportVICEDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ImportVICEDialog)
    , _validVICEFile(false)
    , _filepath("")
    , _supportInvalidVICAddresses(false)
{
    ui->setupUi(this);

    std::memset(_memoryRAM, 0, sizeof(_memoryRAM));
    std::memset(_colorRAM, 0, sizeof(_colorRAM));

    // comes with a default map of 40*25
    _tmpState = new State();
    _tmpState->_setForegroundColorMode(
        ui->checkBoxGuessColors->checkState() == Qt::CheckState::Checked
            ? State::FOREGROUND_COLOR_PER_TILE
            : State::FOREGROUND_COLOR_GLOBAL);

    // default tiles
    for (auto& _tileImage : _tileImages) {
        _tileImage = new QImage(QSize(8, 8), QImage::Format_RGB888);
    }
    updateTileImages();

    // needed for the shared _memoryRAM / _colorRAM
    ui->widgetCharset->setParentDialog(this);
    ui->widgetScreenRAM->setParentDialog(this);

    auto lastDir = Preferences::getInstance().getLastUsedDirectory();
    ui->lineEdit->setText(lastDir);
    updateWidgets();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

ImportVICEDialog::~ImportVICEDialog()
{
    delete ui;
}

const QString& ImportVICEDialog::getFilepath() const
{
    return _filepath;
}

void ImportVICEDialog::updateTileImages()
{
    for (int tileIdx = 0; tileIdx < 256; ++tileIdx) {
        utilsDrawCharInImage(_tmpState, _tileImages[tileIdx], QPoint(0, 0), tileIdx);
    }
}

//
// slots
//
void ImportVICEDialog::on_pushButtonImport_clicked()
{
    _tmpState->_loadedFilename = _filepath;
    MainWindow::getInstance()->createDocument(_tmpState);
    QFileInfo info(ui->lineEdit->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    // don't free _tmpState;
    accept();
}

void ImportVICEDialog::on_pushButtonCancel_clicked()
{
    // only free the tmpState if import is rejected
    free(_tmpState);
    reject();
}

void ImportVICEDialog::on_spinBoxCharset_editingFinished()
{
    int newvalue = 0;
    int oldvalue = ui->spinBoxCharset->value();
    if (_supportInvalidVICAddresses) {
        newvalue = qMin(oldvalue, 0xf800); // no bigger than 0xf800 to prevent buffer overflow issues
    } else {
        // normalize number, in case it was edited manually
        int m = oldvalue / 2048;
        newvalue = m * 2048;

        if (newvalue != oldvalue)
            ui->spinBoxCharset->setValue(newvalue);
    }

    on_spinBoxCharset_valueChanged(newvalue);
}

void ImportVICEDialog::on_spinBoxCharset_valueChanged(int address)
{
    Q_ASSERT(address <= (65536 - 2048) && "invalid address");
    std::memcpy(_tmpState->_charset, &_memoryRAM[address], sizeof(_tmpState->_charset));
    updateTileImages();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

void ImportVICEDialog::on_spinBoxScreenRAM_editingFinished()
{
    int newvalue = 0;
    int oldvalue = ui->spinBoxScreenRAM->value();
    if (_supportInvalidVICAddresses) {
        // no bigger than 0xfc00 to prevent buffer overflow issues
        newvalue = qMin(oldvalue, 0xfc00);
    } else {
        // normalize number, in case it was edited manually
        int m = oldvalue / 1024;
        newvalue = m * 1024;

        if (newvalue != oldvalue)
            ui->spinBoxScreenRAM->setValue(newvalue);
    }

    on_spinBoxScreenRAM_valueChanged(newvalue);
}

void ImportVICEDialog::on_spinBoxScreenRAM_valueChanged(int address)
{
    Q_ASSERT(address <= (65536 - 1024) && "invalid address");

    std::memcpy(_tmpState->_map.data(), &_memoryRAM[address], 40 * 25);

    for (int i = 40 * 25 - 1; i >= 0; --i) {
        quint8 tileColor = _colorRAM[i];
        quint8 tileIdx = _tmpState->_map[i];

        _tmpState->_tileColors[tileIdx] = tileColor;
    }

    updateTileImages();

    ui->widgetScreenRAM->update();
    ui->widgetCharset->update();
}

void ImportVICEDialog::on_pushButtonBrowse_clicked()
{
    auto filter = tr("VICE snapshot files");
    auto fn = QFileDialog::getOpenFileName(this,
        tr("Select VICE Snapshot File"),
        ui->lineEdit->text(),
        tr(
            "All files (*);;"
            "VICE snapshot files (*.vsf);;"),
        &filter
        /*,QFileDialog::DontUseNativeDialog*/
    );

    if (fn.length() > 0) {
        if (fn != _filepath) {
            _filepath = fn;
            ui->lineEdit->setText(fn);

            _validVICEFile = validateVICEFile(fn);
            updateWidgets();
        }
    }
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    QString filename = ui->lineEdit->text();

    if (_filepath != filename) {
        QFileInfo fi(filename);
        _validVICEFile = fi.exists() && validateVICEFile(filename);
        updateWidgets();
        _filepath = filename;
    }
}

// helper
bool ImportVICEDialog::validateVICEFile(const QString& filepath)
{
    QFile file(filepath);

    quint16 charsetAddress, screenRAMOAddress;
    quint8 VICRegisters[64];
    auto ret = StateImport::parseVICESnapshot(
        file,
        _memoryRAM,
        &charsetAddress,
        &screenRAMOAddress,
        _colorRAM,
        (quint8*)&VICRegisters);

    if (ret >= 0) {
        // colors d021, d022, d023
        _tmpState->_penColors[0] = VICRegisters[0x21] & 0xf;
        _tmpState->_penColors[1] = VICRegisters[0x22] & 0xf;
        _tmpState->_penColors[2] = VICRegisters[0x23] & 0xf;

        std::memset(_tmpState->_tileColors, 11, sizeof(_tmpState->_tileColors));

        int oldCharset = ui->spinBoxCharset->value();
        int oldScreenRAM = ui->spinBoxScreenRAM->value();
        ui->spinBoxCharset->setValue(charsetAddress);
        ui->spinBoxScreenRAM->setValue(screenRAMOAddress);

        // d016 contains multicolor bit
        bool isMC = (VICRegisters[0x16] >> 4) & 0x1;
        ui->checkBoxMulticolor->setChecked(isMC);
        _tmpState->_setMulticolorMode(isMC);

        // force the update when the value is the same as the previous one
        if (oldCharset == charsetAddress)
            on_spinBoxCharset_valueChanged(charsetAddress);
        if (oldScreenRAM == screenRAMOAddress)
            on_spinBoxScreenRAM_valueChanged(screenRAMOAddress);

        updateTileImages();

        ui->widgetCharset->update();
        ui->widgetScreenRAM->update();
    }
    return (ret >= 0);
}

void ImportVICEDialog::updateWidgets()
{
    QWidget* widgets[] = {
        ui->checkBoxMulticolor,
        ui->checkBoxGuessColors,
        ui->checkBoxDisplayGrid,
        ui->checkBoxInvalidAddresses,
        ui->spinBoxCharset,
        ui->spinBoxScreenRAM,
        ui->pushButtonImport
    };

    for (auto& widget : widgets) {
        widget->setEnabled(_validVICEFile);
    }
}

void ImportVICEDialog::on_checkBoxMulticolor_toggled(bool checked)
{
    _tmpState->_setMulticolorMode(checked);
    updateTileImages();
    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

void ImportVICEDialog::on_checkBoxGuessColors_toggled(bool checked)
{
    _tmpState->_setForegroundColorMode(
        checked
            ? State::FOREGROUND_COLOR_PER_TILE
            : State::FOREGROUND_COLOR_GLOBAL);

    if (checked) {
        _tmpState->_penColors[0] = _vicColorsBackup[0];
        _tmpState->_penColors[1] = _vicColorsBackup[1];
        _tmpState->_penColors[2] = _vicColorsBackup[2];
    } else {
        _vicColorsBackup[0] = _tmpState->_penColors[0];
        _vicColorsBackup[1] = _tmpState->_penColors[1];
        _vicColorsBackup[2] = _tmpState->_penColors[2];

        _tmpState->_penColors[0] = 1; // white
        _tmpState->_penColors[1] = 5; // green
        _tmpState->_penColors[2] = 7; // yellow
        _tmpState->_penColors[3] = 11; // dark grey
    }

    updateTileImages();
    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

void ImportVICEDialog::on_checkBoxDisplayGrid_toggled(bool checked)
{
    ui->widgetCharset->setDisplayGrid(checked);
    ui->widgetScreenRAM->setDisplayGrid(checked);
}

void ImportVICEDialog::on_checkBoxInvalidAddresses_toggled(bool checked)
{
    if (_supportInvalidVICAddresses != checked) {
        _supportInvalidVICAddresses = checked;

        if (!_supportInvalidVICAddresses) {
            on_spinBoxCharset_editingFinished();
            on_spinBoxScreenRAM_editingFinished();
        }
    }
}
