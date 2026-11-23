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

// Modal dialog for importing raw binary charset, screen-map, and per-tile color dumps.
//
// Assembles a new 40-column State document using std::unique_ptr<State> RAII,
// computes ceiling map row counts clamped to State::kMaxMapDim so trailing map
// bytes are preserved, masks imported color bytes to 4-bit C64 indices (& 0x0FU),
// and transfers document ownership via MainWindow::adoptDocument().

#include "importbinarydialog.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

namespace {

// Appends a labeled file-path input row with a Browse button to `form_layout`,
// wiring click and text-change signals back to `dialog`.
void AddFileRow(
    ImportBinaryDialog* dialog,
    QFormLayout* form_layout,
    const QString& label_text,
    const QString& browse_button_text,
    QLineEdit*& line_edit,
    void (ImportBinaryDialog::*browse_slot)(),
    void (ImportBinaryDialog::*text_changed_slot)())
{
    auto* row_layout = new QHBoxLayout();
    line_edit = new QLineEdit(dialog);
    auto* browse_button = new QPushButton(browse_button_text, dialog);
    row_layout->addWidget(line_edit);
    row_layout->addWidget(browse_button);
    form_layout->addRow(new QLabel(label_text, dialog), row_layout);

    QObject::connect(browse_button, &QPushButton::clicked, dialog, browse_slot);
    QObject::connect(line_edit, &QLineEdit::textChanged, dialog, text_changed_slot);
}

// Prompts the user to pick a file starting in the last used directory and stores
// any non-empty selection in `target_edit`.
void BrowseFileIntoLineEdit(
    QWidget* parent,
    const QString& dialog_title,
    const QString& file_filter,
    QLineEdit* target_edit)
{
    const QString selected_filename = QFileDialog::getOpenFileName(
        parent,
        dialog_title,
        Preferences::getInstance().getLastUsedDirectory(),
        file_filter);
    if (!selected_filename.isEmpty()) {
        target_edit->setText(selected_filename);
    }
}

} // namespace

ImportBinaryDialog::ImportBinaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import Binary Files"));

    auto* main_layout = new QVBoxLayout(this);
    auto* form_layout = new QFormLayout();
    const QString browse_text = tr("Browse...");

    AddFileRow(
        this,
        form_layout,
        tr("Charset File:"),
        browse_text,
        _lineEditCharset,
        &ImportBinaryDialog::onBrowseCharset,
        &ImportBinaryDialog::onLineEditsChanged);
    AddFileRow(
        this,
        form_layout,
        tr("Tile Map File:"),
        browse_text,
        _lineEditMap,
        &ImportBinaryDialog::onBrowseMap,
        &ImportBinaryDialog::onLineEditsChanged);
    AddFileRow(
        this,
        form_layout,
        tr("Tile Colors File (Optional):"),
        browse_text,
        _lineEditColors,
        &ImportBinaryDialog::onBrowseColors,
        &ImportBinaryDialog::onLineEditsChanged);

    main_layout->addLayout(form_layout);

    auto* button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this);
    main_layout->addWidget(button_box);

    _btnImport = button_box->button(QDialogButtonBox::Ok);
    _btnImport->setText(tr("Import"));

    connect(button_box, &QDialogButtonBox::accepted, this, &ImportBinaryDialog::onImport);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateImportButtonState();
}

ImportBinaryDialog::~ImportBinaryDialog() = default;

void ImportBinaryDialog::onBrowseCharset()
{
    BrowseFileIntoLineEdit(this, tr("Select Charset File"), tr("All files (*.*)"), _lineEditCharset);
}

void ImportBinaryDialog::onBrowseMap()
{
    BrowseFileIntoLineEdit(this, tr("Select Tile Map File"), tr("All files (*.*)"), _lineEditMap);
}

void ImportBinaryDialog::onBrowseColors()
{
    BrowseFileIntoLineEdit(this, tr("Select Tile Colors File"), tr("All files (*.*)"), _lineEditColors);
}

void ImportBinaryDialog::onImport()
{
    QFile charset_file(_lineEditCharset->text());
    if (!charset_file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open charset file."));
        return;
    }
    const QByteArray charset_data = charset_file.readAll();

    QFile map_file(_lineEditMap->text());
    if (!map_file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open map file."));
        return;
    }
    const QByteArray map_data = map_file.readAll();

    QByteArray colors_data;
    if (!_lineEditColors->text().isEmpty()) {
        QFile colors_file(_lineEditColors->text());
        if (!colors_file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not open colors file."));
            return;
        }
        colors_data = colors_file.readAll();
    }

    const QFileInfo file_info(_lineEditCharset->text());
    Preferences::getInstance().setLastUsedDirectory(file_info.absolutePath());

    // Compute ceiling row count so trailing map bytes are preserved and clamp against kMaxMapDim.
    const int rows = std::clamp(
        static_cast<int>((map_data.size() + 39) / 40),
        1,
        State::kMaxMapDim);
    const QSize map_size(40, rows);

    auto state = std::make_unique<State>(file_info.filePath(), map_size);

    state->resetCharsetBuffer();
    state->setCharsetBytes(
        reinterpret_cast<const uint8_t*>(charset_data.constData()),
        static_cast<size_t>(charset_data.size()));

    std::vector<uint8_t> map_cells(static_cast<size_t>(map_size.width() * map_size.height()), 0U);
    const size_t copy_map_bytes = std::min(static_cast<size_t>(map_data.size()), map_cells.size());
    std::copy_n(reinterpret_cast<const uint8_t*>(map_data.constData()), copy_map_bytes, map_cells.begin());
    (void)state->replaceMap(map_size, std::move(map_cells));

    State::TileColorsBuffer colors { };
    colors.fill(State::TILE_COLORS_DEFAULT);
    const size_t copy_color_bytes = std::min(static_cast<size_t>(colors_data.size()), colors.size());
    for (size_t i = 0U; i < copy_color_bytes; ++i) {
        colors[i] = static_cast<uint8_t>(static_cast<uint8_t>(colors_data[static_cast<qsizetype>(i)]) & 0x0FU);
    }
    state->setTileColorsDirect(colors.data(), colors.size());

    state->setMulticolorModeDirect(false);
    state->setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    MainWindow::getInstance()->adoptDocument(std::move(state));
    accept();
}

void ImportBinaryDialog::onLineEditsChanged()
{
    updateImportButtonState();
}

void ImportBinaryDialog::updateImportButtonState()
{
    const bool can_import = !_lineEditCharset->text().isEmpty() && !_lineEditMap->text().isEmpty();
    _btnImport->setEnabled(can_import);
}
