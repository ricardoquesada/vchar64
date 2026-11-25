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

// Modal dialog configuring RAW, PRG, 6502 ASM, and ISO C17 asset exports.
//
// Uses FileUtils::replaceExtension() to update file suffixes safely without corrupting
// directory names containing dots, and surfaces typed vchar64::Result<void> export errors
// via a modal warning dialog or MainWindow's status bar.

#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QWidget>

#include "core/result.h"
#include "fileutils.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

// Maps a State::ExportFormat enum value to its canonical filename extension (without leading dot).
[[nodiscard]] QString ExtensionForFormat(int format)
{
    switch (format) {
    case State::EXPORT_FORMAT_ASM:
        return QStringLiteral("s");
    case State::EXPORT_FORMAT_RAW:
        return QStringLiteral("bin");
    case State::EXPORT_FORMAT_PRG:
        return QStringLiteral("prg");
    case State::EXPORT_FORMAT_C:
        return QStringLiteral("c");
    default:
        return QStringLiteral("bin");
    }
}

} // namespace

ExportDialog::ExportDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ExportDialog)
    , _state(state)
    , _checkBox_clicked(State::EXPORT_FEATURE_CHARSET)
{
    ui->setupUi(this);

    const QString last_dir = Preferences::getInstance().getLastUsedDirectory();
    const State::ExportProperties export_properties = _state->getExportProperties();
    const int format = std::clamp(static_cast<int>(export_properties.format), 0, 3);
    const QString default_extension = ExtensionForFormat(format);

    QString initial_filename = _state->getExportedFilename();
    if (initial_filename.isEmpty()) {
        initial_filename = state->getLoadedFilename();
        if (!initial_filename.isEmpty()) {
            initial_filename = FileUtils::replaceExtension(initial_filename, default_extension);
        }
    } else {
        initial_filename = FileUtils::replaceExtension(initial_filename, default_extension);
    }

    if (initial_filename.isEmpty()) {
        initial_filename = FileUtils::replaceExtension(
            QDir(last_dir).filePath(QStringLiteral("untitled")),
            default_extension);
    }

    ui->editFilename->setText(initial_filename);

    connect(ui->radioButton_prg, &QRadioButton::toggled, this, [this](bool toggled) {
        ui->spinBox_attribAddress->setEnabled(toggled);
        ui->spinBox_charsetAddress->setEnabled(toggled);
        ui->spinBox_mapAddress->setEnabled(toggled);
    });

    _checkBox_clicked = static_cast<int>(export_properties.features & State::EXPORT_FEATURE_ALL);
    ui->checkBox_charset->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_CHARSET) != 0);
    ui->checkBox_map->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_MAP) != 0);
    ui->checkBox_tileColors->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_COLORS) != 0);

    const bool export_full_ecm_charset = (export_properties.features & State::EXPORT_FEATURE_ECM_FULL_CHARSET) != 0U;
    ui->widget_ecmCharsetSize->setVisible(_state->isExtendedColorMode());
    ui->radioButton_ecm256Chars->setChecked(export_full_ecm_charset);
    ui->radioButton_ecm64Chars->setChecked(!export_full_ecm_charset);

    // Must honor the order from State::ExportFormat.
    const std::array<QRadioButton*, 4> format_radios = {
        ui->radioButton_raw,
        ui->radioButton_prg,
        ui->radioButton_asm,
        ui->radioButton_c,
    };
    format_radios[static_cast<size_t>(format)]->setChecked(true);

    ui->spinBox_charsetAddress->setValue(static_cast<int>(export_properties.addresses[0]));
    ui->spinBox_mapAddress->setValue(static_cast<int>(export_properties.addresses[1]));
    ui->spinBox_attribAddress->setValue(static_cast<int>(export_properties.addresses[2]));
    updateButtons();
}

ExportDialog::~ExportDialog()
{
    delete ui;
}

void ExportDialog::on_pushBrowse_clicked()
{
    std::array<QString, 4> filters = {
        tr("Asm files (*.s *.a *.asm)"),
        tr("C files (*.c *.h)"),
        tr("Raw files (*.raw *.bin)"),
        tr("PRG files (*.prg *.64c)"),
    };

    size_t filter_index = 0U;
    if (ui->radioButton_asm->isChecked()) {
        filter_index = 0U;
    } else if (ui->radioButton_c->isChecked()) {
        filter_index = 1U;
    } else if (ui->radioButton_raw->isChecked()) {
        filter_index = 2U;
    } else if (ui->radioButton_prg->isChecked()) {
        filter_index = 3U;
    }

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Select filename"),
        ui->editFilename->text(),
        tr("Asm files (*.s *.a *.asm);;C files (*.c *.h);;Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;Any file (*)"),
        &filters[filter_index],
        QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty()) {
        ui->editFilename->setText(filename);
    }
}

void ExportDialog::on_radioButton_raw_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("bin")));
}

void ExportDialog::on_radioButton_asm_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("s")));
}

void ExportDialog::on_radioButton_prg_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("prg")));
}

void ExportDialog::on_radioButton_c_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("c")));
}

void ExportDialog::on_checkBox_charset_toggled(bool checked)
{
    if (checked) {
        _checkBox_clicked |= State::EXPORT_FEATURE_CHARSET;
    } else {
        _checkBox_clicked &= ~State::EXPORT_FEATURE_CHARSET;
    }
    updateButtons();
}

void ExportDialog::on_checkBox_map_toggled(bool checked)
{
    if (checked) {
        _checkBox_clicked |= State::EXPORT_FEATURE_MAP;
    } else {
        _checkBox_clicked &= ~State::EXPORT_FEATURE_MAP;
    }
    updateButtons();
}

void ExportDialog::on_checkBox_tileColors_toggled(bool checked)
{
    if (checked) {
        _checkBox_clicked |= State::EXPORT_FEATURE_COLORS;
    } else {
        _checkBox_clicked &= ~State::EXPORT_FEATURE_COLORS;
    }
    updateButtons();
}

void ExportDialog::accept()
{
    const QString filename = ui->editFilename->text();
    auto* main_window = qobject_cast<MainWindow*>(parent());

    State::ExportProperties properties = _state->getExportProperties();
    uint8_t feature_mask = State::EXPORT_FEATURE_NONE;
    if (ui->checkBox_map->isChecked()) {
        feature_mask = static_cast<uint8_t>(feature_mask | State::EXPORT_FEATURE_MAP);
    }
    if (ui->checkBox_tileColors->isChecked()) {
        feature_mask = static_cast<uint8_t>(feature_mask | State::EXPORT_FEATURE_COLORS);
    }
    if (ui->checkBox_charset->isChecked()) {
        feature_mask = static_cast<uint8_t>(feature_mask | State::EXPORT_FEATURE_CHARSET);
    }
    const bool keep_full_ecm_charset = _state->isExtendedColorMode()
        ? ui->radioButton_ecm256Chars->isChecked()
        : ((properties.features & State::EXPORT_FEATURE_ECM_FULL_CHARSET) != 0U);
    if (keep_full_ecm_charset) {
        feature_mask = static_cast<uint8_t>(feature_mask | State::EXPORT_FEATURE_ECM_FULL_CHARSET);
    }
    properties.features = feature_mask;

    vchar64::Result<void> result = vchar64::Result<void>::err(
        vchar64::ErrorCode::InvalidExportFormat,
        tr("No export format selected."));

    if (ui->radioButton_raw->isChecked()) {
        result = _state->exportRaw(filename, properties);
    } else if (ui->radioButton_prg->isChecked()) {
        properties.addresses[0] = static_cast<quint16>(ui->spinBox_charsetAddress->value());
        properties.addresses[1] = static_cast<quint16>(ui->spinBox_mapAddress->value());
        properties.addresses[2] = static_cast<quint16>(ui->spinBox_attribAddress->value());
        result = _state->exportPRG(filename, properties);
    } else if (ui->radioButton_asm->isChecked()) {
        result = _state->exportAsm(filename, properties);
    } else if (ui->radioButton_c->isChecked()) {
        result = _state->exportC(filename, properties);
    }

    if (!result.isOk()) {
        // Keep the dialog open so the user can correct the path or options and retry.
        QMessageBox::warning(this, tr("Export Failed"), result.errorMessage());
        return;
    }

    const QFileInfo file_info(filename);
    Preferences::getInstance().setLastUsedDirectory(file_info.absolutePath());
    if (main_window != nullptr) {
        main_window->statusBar()->showMessage(
            tr("File exported to %1").arg(_state->getExportedFilename()),
            2000);
    }

    QDialog::accept();
}

void ExportDialog::updateButtons()
{
    ui->widget_ecmCharsetSize->setEnabled(ui->checkBox_charset->isChecked());
    ui->buttonBox->button(QDialogButtonBox::Save)
        ->setEnabled((_checkBox_clicked & State::EXPORT_FEATURE_ALL) != 0);
}
