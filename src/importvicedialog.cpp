/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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

/**
 * @file importvicedialog.cpp
 * @brief Implementation of the VICE `.vsf` snapshot interactive importer.
 *
 * Enforces hardware VIC-II alignment (2 KiB charset boundaries up to `0xF800`,
 * 1 KiB Screen RAM boundaries up to `0xFC00`) unless arbitrary offsets are
 * explicitly enabled, projects Color RAM nibbles onto referenced character tiles,
 * and transfers `_previewState` ownership via `MainWindow::adoptDocument()`.
 */

#include "importvicedialog.h"
#include "ui_importvicedialog.h"

#include <algorithm>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>

#include "mainwindow.h"
#include "preferences.h"
#include "state.h"
#include "stateimport.h"

namespace {

constexpr size_t kRamSizeBytes = 64U * 1024U;
constexpr int kMaxCharsetOffset = 0xF800; // 65536 - 2048
constexpr int kMaxScreenRamOffset = 0xFC00; // 65536 - 1024

} // namespace

ImportVICEDialog::ImportVICEDialog(QWidget* parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::ImportVICEDialog>())
    , _memoryRAM(kRamSizeBytes, 0U)
    , _previewState(std::make_unique<State>())
{
    ui->setupUi(this);

    _previewState->setForegroundColorModeDirect(
        ui->checkBoxGuessColors->checkState() == Qt::CheckState::Checked
            ? State::FOREGROUND_COLOR_PER_TILE
            : State::FOREGROUND_COLOR_GLOBAL);

    syncPreviewAtlas();

    ui->widgetCharset->setParentDialog(this);
    ui->widgetScreenRAM->setParentDialog(this);

    const QString lastDir = Preferences::getInstance().getLastUsedDirectory();
    ui->lineEdit->setText(lastDir);
    updateWidgets();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

ImportVICEDialog::~ImportVICEDialog() = default;

const QString& ImportVICEDialog::getFilepath() const noexcept
{
    return _filepath;
}

void ImportVICEDialog::syncPreviewAtlas()
{
    if (_previewState) {
        _atlasCache.markAllDirty();
        _atlasCache.sync(*_previewState);
    }
}

void ImportVICEDialog::on_pushButtonImport_clicked()
{
    if (_previewState) {
        MainWindow::getInstance()->adoptDocument(std::move(_previewState));
    }

    const QFileInfo info(ui->lineEdit->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());
    accept();
}

void ImportVICEDialog::on_pushButtonCancel_clicked()
{
    reject();
}

void ImportVICEDialog::on_spinBoxCharset_editingFinished()
{
    const int oldValue = ui->spinBoxCharset->value();
    int newValue = 0;
    if (_supportInvalidVICAddresses) {
        newValue = std::clamp(oldValue, 0, kMaxCharsetOffset);
    } else {
        newValue = std::clamp((oldValue / 2048) * 2048, 0, kMaxCharsetOffset);
        if (newValue != oldValue) {
            ui->spinBoxCharset->setValue(newValue);
        }
    }

    on_spinBoxCharset_valueChanged(newValue);
}

void ImportVICEDialog::on_spinBoxCharset_valueChanged(int address)
{
    if (!_previewState || address < 0 || address > kMaxCharsetOffset) {
        return;
    }

    _previewState->setCharsetBytes(
        _memoryRAM.data() + static_cast<size_t>(address),
        State::CHAR_BUFFER_SIZE);
    syncPreviewAtlas();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

void ImportVICEDialog::on_spinBoxScreenRAM_editingFinished()
{
    const int oldValue = ui->spinBoxScreenRAM->value();
    int newValue = 0;
    if (_supportInvalidVICAddresses) {
        newValue = std::clamp(oldValue, 0, kMaxScreenRamOffset);
    } else {
        newValue = std::clamp((oldValue / 1024) * 1024, 0, kMaxScreenRamOffset);
        if (newValue != oldValue) {
            ui->spinBoxScreenRAM->setValue(newValue);
        }
    }

    on_spinBoxScreenRAM_valueChanged(newValue);
}

void ImportVICEDialog::on_spinBoxScreenRAM_valueChanged(int address)
{
    if (!_previewState || address < 0 || address > kMaxScreenRamOffset) {
        return;
    }

    const uint8_t* screenBase = _memoryRAM.data() + static_cast<size_t>(address);
    std::vector<uint8_t> mapCells(screenBase, screenBase + (40 * 25));
    (void)_previewState->replaceMap(QSize(40, 25), std::move(mapCells));

    const auto& mapBuf = _previewState->map();
    for (int i = 40 * 25 - 1; i >= 0; --i) {
        const size_t idx = static_cast<size_t>(i);
        const uint8_t tileColor = _colorRAM[idx];
        const uint8_t tileIdx = mapBuf[idx];
        _previewState->setTileColorDirect(tileIdx, tileColor);
    }

    syncPreviewAtlas();

    ui->widgetScreenRAM->update();
    ui->widgetCharset->update();
}

void ImportVICEDialog::on_pushButtonBrowse_clicked()
{
    QString filter = tr("VICE snapshot files");
    const QString fn = QFileDialog::getOpenFileName(
        this,
        tr("Select VICE Snapshot File"),
        ui->lineEdit->text(),
        tr("All files (*);;VICE snapshot files (*.vsf);;"),
        &filter);

    if (!fn.isEmpty() && fn != _filepath) {
        _filepath = fn;
        ui->lineEdit->setText(fn);

        _validVICEFile = validateVICEFile(fn);
        updateWidgets();
    }
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    const QString filename = ui->lineEdit->text();

    if (_filepath != filename) {
        const QFileInfo fi(filename);
        _validVICEFile = fi.exists() && validateVICEFile(filename);
        updateWidgets();
        _filepath = filename;
    }
}

bool ImportVICEDialog::validateVICEFile(const QString& filepath)
{
    if (!_previewState) {
        return false;
    }

    QFile file(filepath);

    uint16_t charsetAddress = 0U;
    uint16_t screenRAMAddress = 0U;
    std::array<uint8_t, 64> vicRegisters { };
    const auto ret = StateImport::parseVICESnapshot(
        file,
        _memoryRAM.data(),
        &charsetAddress,
        &screenRAMAddress,
        _colorRAM.data(),
        vicRegisters.data());

    if (ret.isOk()) {
        _previewState->setPenColorDirect(State::PEN_BACKGROUND, vicRegisters[0x21U] & 0x0FU);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR1, vicRegisters[0x22U] & 0x0FU);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR2, vicRegisters[0x23U] & 0x0FU);
        _vicColorsBackup[0] = static_cast<uint8_t>(vicRegisters[0x21U] & 0x0FU);
        _vicColorsBackup[1] = static_cast<uint8_t>(vicRegisters[0x22U] & 0x0FU);
        _vicColorsBackup[2] = static_cast<uint8_t>(vicRegisters[0x23U] & 0x0FU);

        State::TileColorsBuffer defaultColors { };
        defaultColors.fill(State::TILE_COLORS_DEFAULT);
        _previewState->setTileColorsDirect(defaultColors.data(), defaultColors.size());

        const int oldCharset = ui->spinBoxCharset->value();
        const int oldScreenRAM = ui->spinBoxScreenRAM->value();
        const int nextCharset = static_cast<int>(charsetAddress);
        const int nextScreenRAM = static_cast<int>(screenRAMAddress);
        ui->spinBoxCharset->setValue(nextCharset);
        ui->spinBoxScreenRAM->setValue(nextScreenRAM);

        const bool isMC = ((vicRegisters[0x16U] >> 4U) & 0x01U) != 0U;
        ui->checkBoxMulticolor->setChecked(isMC);
        _previewState->setMulticolorModeDirect(isMC);

        if (oldCharset == nextCharset) {
            on_spinBoxCharset_valueChanged(nextCharset);
        }
        if (oldScreenRAM == nextScreenRAM) {
            on_spinBoxScreenRAM_valueChanged(nextScreenRAM);
        }

        syncPreviewAtlas();

        ui->widgetCharset->update();
        ui->widgetScreenRAM->update();
    }
    return ret.isOk();
}

void ImportVICEDialog::updateWidgets()
{
    const std::array<QWidget*, 7> widgets = {
        ui->checkBoxMulticolor,
        ui->checkBoxGuessColors,
        ui->checkBoxDisplayGrid,
        ui->checkBoxInvalidAddresses,
        ui->spinBoxCharset,
        ui->spinBoxScreenRAM,
        ui->pushButtonImport,
    };

    for (QWidget* widget : widgets) {
        widget->setEnabled(_validVICEFile);
    }
}

void ImportVICEDialog::on_checkBoxMulticolor_toggled(bool checked)
{
    if (!_previewState) {
        return;
    }

    _previewState->setMulticolorModeDirect(checked);
    syncPreviewAtlas();
    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

void ImportVICEDialog::on_checkBoxGuessColors_toggled(bool checked)
{
    if (!_previewState) {
        return;
    }

    _previewState->setForegroundColorModeDirect(
        checked ? State::FOREGROUND_COLOR_PER_TILE : State::FOREGROUND_COLOR_GLOBAL);

    if (checked) {
        _previewState->setPenColorDirect(State::PEN_BACKGROUND, _vicColorsBackup[0]);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR1, _vicColorsBackup[1]);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR2, _vicColorsBackup[2]);
    } else {
        _vicColorsBackup[0] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_BACKGROUND));
        _vicColorsBackup[1] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_MULTICOLOR1));
        _vicColorsBackup[2] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_MULTICOLOR2));

        _previewState->setPenColorDirect(State::PEN_BACKGROUND, 1U);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR1, 5U);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR2, 7U);
        _previewState->setPenColorDirect(State::PEN_FOREGROUND, 11U);
    }

    syncPreviewAtlas();
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
