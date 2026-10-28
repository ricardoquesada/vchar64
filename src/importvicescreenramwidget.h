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
 * @file importvicescreenramwidget.h
 * @brief `40x25` Screen RAM (`320x200` px) preview widget inside the VICE snapshot import dialog.
 *
 * Renders the 1,000-cell C64 Screen RAM slice selected in `ImportVICEDialog`
 * by blitting `8x8` tiles from `ImportVICEDialog::atlasCache()`.
 */

#pragma once

#include <QWidget>

class ImportVICEDialog;

/**
 * @brief Live `40x25` C64 Screen RAM preview canvas backed by `ImportVICEDialog`'s `TileAtlasCache`.
 */
class ImportVICEScreenRAMWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImportVICEScreenRAMWidget(QWidget* parent = nullptr);

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
