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
 * @file importvicecharsetwidget.h
 * @brief `16x16` character-set preview widget inside the VICE snapshot import dialog.
 *
 * Blits all 256 decoded `8x8` glyphs directly from `ImportVICEDialog::atlasCache()`
 * (`TileAtlasCache`) at `1.5625x` (`200x200` px) scale as the user inspects
 * different VIC-II bank/address candidates in a VICE `.vsf` snapshot.
 */

#pragma once

#include <QWidget>

class ImportVICEDialog;

/**
 * @brief Live `16x16` charset preview canvas backed by `ImportVICEDialog`'s `TileAtlasCache`.
 */
class ImportVICECharsetWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImportVICECharsetWidget(QWidget* parent = nullptr);

    /** @brief Binds the owning `ImportVICEDialog` supplying the preview `State` and `TileAtlasCache`. */
    void setParentDialog(ImportVICEDialog* parentDialog) noexcept { _parentDialog = parentDialog; }

    /** @brief Enables or disables the `8x8` character-cell dashed grid overlay. */
    void setDisplayGrid(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ImportVICEDialog* _parentDialog = nullptr;
    bool _displayGrid = false;
};
