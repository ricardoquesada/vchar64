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

// Implementation of the VICE .vsf snapshot interactive importer.
//
// Enforces hardware VIC-II alignment (2 KiB charset boundaries up to 0xF800,
// 1 KiB Screen RAM boundaries up to 0xFC00) unless arbitrary offsets are
// explicitly enabled, projects Color RAM nibbles onto referenced character tiles,
// and transfers _previewState ownership via MainWindow::adoptDocument().

#include "importvicedialog.h"
#include "ui_importvicedialog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>

#include "core/result.h"
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

    const QString last_dir = Preferences::getInstance().getLastUsedDirectory();
    ui->lineEdit->setText(last_dir);
    updateWidgets();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
}

ImportVICEDialog::~ImportVICEDialog() = default;

const QString& ImportVICEDialog::getFilepath() const noexcept
{
    return _filepath;
}

void ImportVICEDialog::setFilepath(const QString& filepath)
{
    if (filepath.isEmpty() || filepath == _filepath) {
        return;
    }

    _filepath = filepath;
    ui->lineEdit->setText(filepath);

    _validVICEFile = validateVICEFile(filepath);
    updateWidgets();
}

bool ImportVICEDialog::validateVICEFile(const QString& filepath)
{
    if (!_previewState) {
        return false;
    }

    QFile file(filepath);

    uint16_t charset_address = 0U;
    uint16_t screen_ram_address = 0U;
    std::array<uint8_t, 64> vic_registers { };
    const vchar64::Result<vchar64::ImportReport> parse_result = StateImport::parseVICESnapshot(
        file,
        _memoryRAM.data(),
        &charset_address,
        &screen_ram_address,
        _colorRAM.data(),
        vic_registers.data());

    if (!parse_result.isOk()) {
        return false;
    }

    _previewState->setPenColorDirect(State::PEN_BACKGROUND, vic_registers[0x21U] & 0x0FU);
    _previewState->setPenColorDirect(State::PEN_MULTICOLOR1, vic_registers[0x22U] & 0x0FU);
    _previewState->setPenColorDirect(State::PEN_MULTICOLOR2, vic_registers[0x23U] & 0x0FU);
    _previewState->setPenColorDirect(State::PEN_BACKGROUND3, vic_registers[0x24U] & 0x0FU);
    _vicColorsBackup[0] = static_cast<uint8_t>(vic_registers[0x21U] & 0x0FU);
    _vicColorsBackup[1] = static_cast<uint8_t>(vic_registers[0x22U] & 0x0FU);
    _vicColorsBackup[2] = static_cast<uint8_t>(vic_registers[0x23U] & 0x0FU);
    _vicColorsBackup[3] = static_cast<uint8_t>(vic_registers[0x24U] & 0x0FU);

    State::TileColorsBuffer default_colors { };
    default_colors.fill(State::TILE_COLORS_DEFAULT);
    _previewState->setTileColorsDirect(default_colors.data(), default_colors.size());

    const int old_charset = ui->spinBoxCharset->value();
    const int old_screen_ram = ui->spinBoxScreenRAM->value();
    const int next_charset = static_cast<int>(charset_address);
    const int next_screen_ram = static_cast<int>(screen_ram_address);
    ui->spinBoxCharset->setValue(next_charset);
    ui->spinBoxScreenRAM->setValue(next_screen_ram);

    const bool is_ecm = ((vic_registers[0x11U] >> 6U) & 0x01U) != 0U;
    const bool is_mcm = ((vic_registers[0x16U] >> 4U) & 0x01U) != 0U;
    if (is_ecm && is_mcm) {
        qWarning() << "VICE snapshot has both ECM ($D011 bit 6) and MCM ($D016 bit 4) set; defaulting preview to ECM.";
    }
    State::CharacterMode detected_mode = State::CHARACTER_MODE_HIRES;
    if (is_ecm) {
        detected_mode = State::CHARACTER_MODE_EXTENDED_COLOR;
    } else if (is_mcm) {
        detected_mode = State::CHARACTER_MODE_MULTICOLOR;
    }
    {
        const QSignalBlocker mode_blocker(ui->comboBoxCharMode);
        ui->comboBoxCharMode->setCurrentIndex(static_cast<int>(detected_mode));
    }
    _previewState->setCharacterModeDirect(detected_mode);

    if (old_charset == next_charset) {
        on_spinBoxCharset_valueChanged(next_charset);
    }
    if (old_screen_ram == next_screen_ram) {
        on_spinBoxScreenRAM_valueChanged(next_screen_ram);
    }

    syncPreviewAtlas();

    ui->widgetCharset->update();
    ui->widgetScreenRAM->update();
    return true;
}

void ImportVICEDialog::updateWidgets()
{
    const std::array<QWidget*, 7> widgets = {
        ui->comboBoxCharMode,
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

    const QFileInfo file_info(ui->lineEdit->text());
    Preferences::getInstance().setLastUsedDirectory(file_info.absolutePath());
    accept();
}

void ImportVICEDialog::on_pushButtonCancel_clicked()
{
    reject();
}

void ImportVICEDialog::on_pushButtonBrowse_clicked()
{
    QString filter = tr("VICE snapshot files");
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        tr("Select VICE Snapshot File"),
        ui->lineEdit->text(),
        tr("All files (*);;VICE snapshot files (*.vsf);;"),
        &filter);

    setFilepath(selected_file);
}

void ImportVICEDialog::on_lineEdit_editingFinished()
{
    const QString filename = ui->lineEdit->text();

    if (_filepath != filename) {
        const QFileInfo file_info(filename);
        _validVICEFile = file_info.exists() && validateVICEFile(filename);
        updateWidgets();
        _filepath = filename;
    }
}

void ImportVICEDialog::on_spinBoxCharset_editingFinished()
{
    const int old_value = ui->spinBoxCharset->value();
    int new_value = 0;
    if (_supportInvalidVICAddresses) {
        new_value = std::clamp(old_value, 0, kMaxCharsetOffset);
    } else {
        new_value = std::clamp((old_value / 2048) * 2048, 0, kMaxCharsetOffset);
        if (new_value != old_value) {
            ui->spinBoxCharset->setValue(new_value);
        }
    }

    on_spinBoxCharset_valueChanged(new_value);
}

void ImportVICEDialog::on_spinBoxScreenRAM_editingFinished()
{
    const int old_value = ui->spinBoxScreenRAM->value();
    int new_value = 0;
    if (_supportInvalidVICAddresses) {
        new_value = std::clamp(old_value, 0, kMaxScreenRamOffset);
    } else {
        new_value = std::clamp((old_value / 1024) * 1024, 0, kMaxScreenRamOffset);
        if (new_value != old_value) {
            ui->spinBoxScreenRAM->setValue(new_value);
        }
    }

    on_spinBoxScreenRAM_valueChanged(new_value);
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

void ImportVICEDialog::on_spinBoxScreenRAM_valueChanged(int address)
{
    if (!_previewState || address < 0 || address > kMaxScreenRamOffset) {
        return;
    }

    const uint8_t* screen_base = _memoryRAM.data() + static_cast<size_t>(address);
    std::vector<uint8_t> map_cells(screen_base, screen_base + (40 * 25));
    (void)_previewState->replaceMap(QSize(40, 25), std::move(map_cells));

    const State::MapBuffer& map_buffer = _previewState->map();
    for (int cell_index = 40 * 25 - 1; cell_index >= 0; --cell_index) {
        const size_t offset = static_cast<size_t>(cell_index);
        const uint8_t tile_color = _colorRAM[offset];
        const uint8_t tile_index = map_buffer[offset];
        _previewState->setTileColorDirect(tile_index, tile_color);
    }

    syncPreviewAtlas();

    ui->widgetScreenRAM->update();
    ui->widgetCharset->update();
}

void ImportVICEDialog::on_comboBoxCharMode_currentIndexChanged(int index)
{
    if (!_previewState) {
        return;
    }

    State::CharacterMode mode = State::CHARACTER_MODE_HIRES;
    if (index == static_cast<int>(State::CHARACTER_MODE_MULTICOLOR)) {
        mode = State::CHARACTER_MODE_MULTICOLOR;
    } else if (index == static_cast<int>(State::CHARACTER_MODE_EXTENDED_COLOR)) {
        mode = State::CHARACTER_MODE_EXTENDED_COLOR;
    }
    _previewState->setCharacterModeDirect(mode);
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
        _previewState->setPenColorDirect(State::PEN_BACKGROUND3, _vicColorsBackup[3]);
    } else {
        _vicColorsBackup[0] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_BACKGROUND));
        _vicColorsBackup[1] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_MULTICOLOR1));
        _vicColorsBackup[2] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_MULTICOLOR2));
        _vicColorsBackup[3] = static_cast<uint8_t>(_previewState->getGlobalColorForPen(State::PEN_BACKGROUND3));

        _previewState->setPenColorDirect(State::PEN_BACKGROUND, 1U);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR1, 5U);
        _previewState->setPenColorDirect(State::PEN_MULTICOLOR2, 7U);
        _previewState->setPenColorDirect(State::PEN_FOREGROUND, 11U);
        _previewState->setPenColorDirect(State::PEN_BACKGROUND3, 6U);
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
    if (_supportInvalidVICAddresses == checked) {
        return;
    }

    _supportInvalidVICAddresses = checked;
    if (!_supportInvalidVICAddresses) {
        on_spinBoxCharset_editingFinished();
        on_spinBoxScreenRAM_editingFinished();
    }
}
