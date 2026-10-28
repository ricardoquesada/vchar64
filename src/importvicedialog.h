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
 * @file importvicedialog.h
 * @brief Interactive VICE C64/C128 emulator snapshot (`.vsf`) import dialog.
 *
 * Extracts 64 KiB C64 RAM, Color RAM, and VIC-II registers via `StateImport::parseVICESnapshot()`,
 * maintains an isolated `std::unique_ptr<State>` preview document and `TileAtlasCache`
 * for live address-offset browsing without leaking abandoned documents on cancel,
 * and hands ownership to `MainWindow::adoptDocument()` on accept.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QDialog>
#include <QString>

#include "core/tileatlascache.h"

namespace Ui {
class ImportVICEDialog;
}

class State;

/**
 * @brief Modal dialog for inspecting and importing VIC-II charsets and `40x25`
 *        Screen RAM from a VICE emulator `.vsf` snapshot.
 */
class ImportVICEDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportVICEDialog(QWidget* parent = nullptr);
    ~ImportVICEDialog() override;

    /** @brief Returns the currently selected `.vsf` snapshot path. */
    [[nodiscard]] const QString& getFilepath() const noexcept;

    /** @brief Exposes the in-dialog preview `State` to child charset/screen preview widgets. */
    [[nodiscard]] const State* previewState() const noexcept { return _previewState.get(); }

    /** @brief Exposes the pre-rasterized `TileAtlasCache` synchronized with `previewState()`. */
    [[nodiscard]] const TileAtlasCache& atlasCache() const noexcept { return _atlasCache; }

protected:
    bool validateVICEFile(const QString& filepath);
    void updateWidgets();
    void syncPreviewAtlas();

private slots:
    void on_pushButtonImport_clicked();
    void on_pushButtonCancel_clicked();
    void on_pushButtonBrowse_clicked();
    void on_lineEdit_editingFinished();
    void on_spinBoxCharset_editingFinished();
    void on_spinBoxScreenRAM_editingFinished();
    void on_spinBoxCharset_valueChanged(int address);
    void on_spinBoxScreenRAM_valueChanged(int address);
    void on_checkBoxMulticolor_toggled(bool checked);
    void on_checkBoxGuessColors_toggled(bool checked);
    void on_checkBoxDisplayGrid_toggled(bool checked);
    void on_checkBoxInvalidAddresses_toggled(bool checked);

private:
    std::unique_ptr<Ui::ImportVICEDialog> ui;
    bool _validVICEFile = false;
    QString _filepath;

    std::vector<uint8_t> _memoryRAM;
    std::array<uint8_t, 1024> _colorRAM { };
    std::array<uint8_t, 3> _vicColorsBackup { 1U, 5U, 7U };
    std::unique_ptr<State> _previewState;
    TileAtlasCache _atlasCache;

    bool _supportInvalidVICAddresses = false;
};
