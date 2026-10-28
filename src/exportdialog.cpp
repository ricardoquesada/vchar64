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
 * @file exportdialog.cpp
 * @brief Modal dialog configuring RAW, PRG, 6502 ASM, and ISO C17 asset exports (`vchar64`).
 *
 * Uses `FileUtils::replaceExtension()` to update file suffixes safely without corrupting
 * directory names containing dots, and surfaces typed `vchar64::Result<void>` export errors
 * directly on `MainWindow`'s status bar.
 */

#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <algorithm>
#include <array>

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QStatusBar>
#include <QWidget>

#include "fileutils.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

/** @brief Maps a `State::ExportFormat` enum value to its canonical filename extension (without leading dot). */
[[nodiscard]] QString extensionForFormat(int format)
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

    const QString lastDir = Preferences::getInstance().getLastUsedDirectory();
    const State::ExportProperties exportProperties = _state->getExportProperties();
    const int format = std::clamp(static_cast<int>(exportProperties.format), 0, 3);
    const QString defaultExt = extensionForFormat(format);

    QString fn = _state->getExportedFilename();
    if (fn.isEmpty()) {
        fn = state->getLoadedFilename();
        if (!fn.isEmpty()) {
            fn = FileUtils::replaceExtension(fn, defaultExt);
        }
    } else {
        fn = FileUtils::replaceExtension(fn, defaultExt);
    }

    if (fn.isEmpty()) {
        fn = FileUtils::replaceExtension(
            QDir(lastDir).filePath(QStringLiteral("untitled")),
            defaultExt);
    }

    ui->editFilename->setText(fn);

    connect(ui->radioButton_prg, &QRadioButton::toggled, this, [this](bool toggled) {
        ui->spinBox_attribAddress->setEnabled(toggled);
        ui->spinBox_charsetAddress->setEnabled(toggled);
        ui->spinBox_mapAddress->setEnabled(toggled);
    });

    _checkBox_clicked = static_cast<int>(exportProperties.features);
    ui->checkBox_charset->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_CHARSET) != 0);
    ui->checkBox_map->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_MAP) != 0);
    ui->checkBox_tileColors->setChecked((_checkBox_clicked & State::EXPORT_FEATURE_COLORS) != 0);

    // Must honor the order from State::ExportFormat.
    const std::array<QRadioButton*, 4> radios = {
        ui->radioButton_raw,
        ui->radioButton_prg,
        ui->radioButton_asm,
        ui->radioButton_c,
    };
    radios[static_cast<size_t>(format)]->setChecked(true);

    ui->spinBox_charsetAddress->setValue(static_cast<int>(exportProperties.addresses[0]));
    ui->spinBox_mapAddress->setValue(static_cast<int>(exportProperties.addresses[1]));
    ui->spinBox_attribAddress->setValue(static_cast<int>(exportProperties.addresses[2]));
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

    size_t filterIdx = 0U;
    if (ui->radioButton_asm->isChecked()) {
        filterIdx = 0U;
    } else if (ui->radioButton_c->isChecked()) {
        filterIdx = 1U;
    } else if (ui->radioButton_raw->isChecked()) {
        filterIdx = 2U;
    } else if (ui->radioButton_prg->isChecked()) {
        filterIdx = 3U;
    }

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Select filename"),
        ui->editFilename->text(),
        tr("Asm files (*.s *.a *.asm);;C files (*.c *.h);;Raw files (*.raw *.bin);;PRG files (*.prg *.64c);;Any file (*)"),
        &filters[filterIdx],
        QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty()) {
        ui->editFilename->setText(filename);
    }
}

void ExportDialog::accept()
{
    const QString filename = ui->editFilename->text();
    auto* mainWindow = qobject_cast<MainWindow*>(parent());

    State::ExportProperties properties = _state->getExportProperties();
    uint8_t featureMask = State::EXPORT_FEATURE_NONE;
    if (ui->checkBox_map->isChecked()) {
        featureMask = static_cast<uint8_t>(featureMask | State::EXPORT_FEATURE_MAP);
    }
    if (ui->checkBox_tileColors->isChecked()) {
        featureMask = static_cast<uint8_t>(featureMask | State::EXPORT_FEATURE_COLORS);
    }
    if (ui->checkBox_charset->isChecked()) {
        featureMask = static_cast<uint8_t>(featureMask | State::EXPORT_FEATURE_CHARSET);
    }
    properties.features = featureMask;

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

    if (result.isOk()) {
        const QFileInfo info(filename);
        Preferences::getInstance().setLastUsedDirectory(info.absolutePath());
        if (mainWindow != nullptr) {
            mainWindow->statusBar()->showMessage(
                tr("File exported to %1").arg(_state->getExportedFilename()),
                2000);
        }
    } else {
        if (mainWindow != nullptr) {
            mainWindow->statusBar()->showMessage(
                tr("Export failed: %1").arg(result.errorMessage()),
                3000);
        }
        qDebug() << "Error saving file:" << filename << result.errorMessage();
    }

    QDialog::accept();
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

void ExportDialog::on_radioButton_c_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("c")));
}

void ExportDialog::on_radioButton_prg_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    ui->editFilename->setText(FileUtils::replaceExtension(ui->editFilename->text(), QStringLiteral("prg")));
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

void ExportDialog::updateButtons()
{
    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(_checkBox_clicked != 0);
}
