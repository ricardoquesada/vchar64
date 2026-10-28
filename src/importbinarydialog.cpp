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

/**
 * @file importbinarydialog.cpp
 * @brief Modal dialog for importing raw binary charset, screen-map, and per-tile color dumps.
 *
 * Assembles a new 40-column `State` document using `std::unique_ptr<State>` RAII,
 * computes ceiling map row counts clamped to `State::kMaxMapDim` so trailing map
 * bytes are preserved, masks imported color bytes to 4-bit C64 indices (`& 0x0FU`),
 * and transfers document ownership via `MainWindow::adoptDocument()`.
 */

#include "importbinarydialog.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

ImportBinaryDialog::ImportBinaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import Binary Files"));

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    auto createRow = [this, formLayout](
                         const QString& labelText,
                         QLineEdit*& lineEdit,
                         QPushButton*& button,
                         void (ImportBinaryDialog::*slot)()) {
        auto* hLayout = new QHBoxLayout();
        lineEdit = new QLineEdit(this);
        button = new QPushButton(tr("Browse..."), this);
        hLayout->addWidget(lineEdit);
        hLayout->addWidget(button);
        formLayout->addRow(new QLabel(labelText, this), hLayout);

        connect(button, &QPushButton::clicked, this, slot);
        connect(lineEdit, &QLineEdit::textChanged, this, &ImportBinaryDialog::onLineEditsChanged);
    };

    QPushButton* btnCharset = nullptr;
    QPushButton* btnMap = nullptr;
    QPushButton* btnColors = nullptr;

    createRow(tr("Charset File:"), _lineEditCharset, btnCharset, &ImportBinaryDialog::onBrowseCharset);
    createRow(tr("Tile Map File:"), _lineEditMap, btnMap, &ImportBinaryDialog::onBrowseMap);
    createRow(tr("Tile Colors File (Optional):"), _lineEditColors, btnColors, &ImportBinaryDialog::onBrowseColors);

    mainLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this);
    mainLayout->addWidget(buttonBox);

    _btnImport = buttonBox->button(QDialogButtonBox::Ok);
    _btnImport->setText(tr("Import"));

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ImportBinaryDialog::onImport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateImportButtonState();
}

ImportBinaryDialog::~ImportBinaryDialog() = default;

void ImportBinaryDialog::onBrowseCharset()
{
    const QString filter = tr("All files (*.*)");
    const QString fn = QFileDialog::getOpenFileName(
        this,
        tr("Select Charset File"),
        Preferences::getInstance().getLastUsedDirectory(),
        filter);
    if (!fn.isEmpty()) {
        _lineEditCharset->setText(fn);
    }
}

void ImportBinaryDialog::onBrowseMap()
{
    const QString filter = tr("All files (*.*)");
    const QString fn = QFileDialog::getOpenFileName(
        this,
        tr("Select Tile Map File"),
        Preferences::getInstance().getLastUsedDirectory(),
        filter);
    if (!fn.isEmpty()) {
        _lineEditMap->setText(fn);
    }
}

void ImportBinaryDialog::onBrowseColors()
{
    const QString filter = tr("All files (*.*)");
    const QString fn = QFileDialog::getOpenFileName(
        this,
        tr("Select Tile Colors File"),
        Preferences::getInstance().getLastUsedDirectory(),
        filter);
    if (!fn.isEmpty()) {
        _lineEditColors->setText(fn);
    }
}

void ImportBinaryDialog::onLineEditsChanged()
{
    updateImportButtonState();
}

void ImportBinaryDialog::updateImportButtonState()
{
    const bool canImport = !_lineEditCharset->text().isEmpty() && !_lineEditMap->text().isEmpty();
    _btnImport->setEnabled(canImport);
}

void ImportBinaryDialog::onImport()
{
    QFile charsetFile(_lineEditCharset->text());
    if (!charsetFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open charset file."));
        return;
    }
    const QByteArray charsetData = charsetFile.readAll();

    QFile mapFile(_lineEditMap->text());
    if (!mapFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open map file."));
        return;
    }
    const QByteArray mapData = mapFile.readAll();

    QByteArray colorsData;
    if (!_lineEditColors->text().isEmpty()) {
        QFile colorsFile(_lineEditColors->text());
        if (!colorsFile.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not open colors file."));
            return;
        }
        colorsData = colorsFile.readAll();
    }

    const QFileInfo info(_lineEditCharset->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    // Compute ceiling row count so trailing map bytes are preserved and clamp against kMaxMapDim.
    const int rows = std::clamp(
        static_cast<int>((mapData.size() + 39) / 40),
        1,
        State::kMaxMapDim);
    const QSize mapSize(40, rows);

    auto state = std::make_unique<State>(info.filePath(), mapSize);

    state->resetCharsetBuffer();
    state->setCharsetBytes(
        reinterpret_cast<const uint8_t*>(charsetData.constData()),
        static_cast<size_t>(charsetData.size()));

    std::vector<uint8_t> mapCells(static_cast<size_t>(mapSize.width() * mapSize.height()), 0U);
    const size_t copyMapBytes = std::min(static_cast<size_t>(mapData.size()), mapCells.size());
    std::copy_n(reinterpret_cast<const uint8_t*>(mapData.constData()), copyMapBytes, mapCells.begin());
    (void)state->replaceMap(mapSize, std::move(mapCells));

    State::TileColorsBuffer colors { };
    colors.fill(State::TILE_COLORS_DEFAULT);
    const size_t copyColors = std::min(static_cast<size_t>(colorsData.size()), colors.size());
    for (size_t i = 0; i < copyColors; ++i) {
        colors[i] = static_cast<uint8_t>(static_cast<uint8_t>(colorsData[static_cast<qsizetype>(i)]) & 0x0FU);
    }
    state->setTileColorsDirect(colors.data(), colors.size());

    state->setMulticolorModeDirect(false);
    state->setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    MainWindow::getInstance()->adoptDocument(std::move(state));
    accept();
}
