/****************************************************************************
Copyright 2024-2026 Ricardo Quesada

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

// Modal PNG image export dialog for tileset and screen-map rendering.
//
// Pre-populates target .png paths via FileUtils::replaceExtension(), invokes
// State::exportPNG() (delegating to headless DocumentImageRenderer), and
// reports Result<void> status or failure diagnostics on MainWindow::statusBar().

#include "exportimagedialog.h"
#include "ui_exportimagedialog.h"

#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QStatusBar>
#include <QString>
#include <QWidget>

#include "core/result.h"
#include "fileutils.h"
#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

ExportImageDialog::ExportImageDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ExportImageDialog)
    , _state(state)
{
    ui->setupUi(this);

    const QString last_dir = Preferences::getInstance().getLastUsedDirectory();

    QString initial_filename = _state->getExportedFilename();
    if (initial_filename.isEmpty()) {
        initial_filename = state->getLoadedFilename();
    }

    if (!initial_filename.isEmpty()) {
        initial_filename = FileUtils::replaceExtension(initial_filename, QStringLiteral("png"));
    } else {
        initial_filename = QDir(last_dir).filePath(QStringLiteral("untitled.png"));
    }

    ui->editFilename->setText(initial_filename);
}

ExportImageDialog::~ExportImageDialog()
{
    delete ui;
}

void ExportImageDialog::on_pushBrowse_clicked()
{
    QString png_filter = tr("PNG files (*.png)");

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Select filename"),
        ui->editFilename->text(),
        tr("PNG files (*.png);;Any file (*)"),
        &png_filter,
        QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty()) {
        ui->editFilename->setText(filename);
    }
}

void ExportImageDialog::accept()
{
    const QString filename = ui->editFilename->text();
    auto* main_window = qobject_cast<MainWindow*>(parent());

    const vchar64::Result<void> result = _state->exportPNG(
        filename,
        ui->checkBox_tileset->isChecked(),
        ui->checkBox_map->isChecked());

    if (result.isOk()) {
        const QFileInfo file_info(filename);
        Preferences::getInstance().setLastUsedDirectory(file_info.absolutePath());
        if (main_window != nullptr) {
            main_window->statusBar()->showMessage(
                tr("File exported to %1").arg(_state->getExportedFilename()),
                2000);
        }
    } else {
        if (main_window != nullptr) {
            main_window->statusBar()->showMessage(
                tr("Export failed: %1").arg(result.errorMessage()),
                3000);
        }
        qDebug() << "Error saving file:" << filename << result.errorMessage();
    }

    QDialog::accept();
}
