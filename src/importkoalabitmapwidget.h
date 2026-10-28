/****************************************************************************
Copyright 2016 Ricardo Quesada

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
 * @file importkoalabitmapwidget.h
 * @brief Interactive preview and color-histogram analyzer for C64 Koala Painter bitmaps.
 *
 * Decodes 10,003-byte Koala Painter multicolor bitmaps into a `160x200` 4-bit C64
 * color index framebuffer, lets the user drag-select a character-aligned sub-region,
 * and builds `4x8` cell uniqueness histograms used by `ImportKoalaDialog` to
 * choose `$D021`/`$D022`/`$D023` shared registers during bitmap-to-charset conversion.
 */

#pragma once

#include <array>
#include <unordered_map>

#include <QRect>
#include <QWidget>

#include "state.h"
#include "stateimport.h"

/**
 * @brief Renders a C64 Koala Painter multicolor bitmap (`160x200` double-wide pixels)
 *        with interactive `8x8` character-cell region selection and color-usage analysis.
 */
class ImportKoalaBitmapWidget : public QWidget {
    Q_OBJECT

    friend class ImportKoalaDialog;

public:
    ImportKoalaBitmapWidget(QWidget* parent = nullptr);

    /**
     * @brief Loads and decodes a Koala Painter file from disk via `StateImport::loadKoala()`.
     * @param koalaFilepath Filesystem path to the `.koa` / `.kla` image.
     * @return `true` when the Koala payload was decoded and analyzed; `false` on I/O or size error.
     */
    bool loadKoala(const QString& koalaFilepath);

    /** @brief Toggles the `8x8` character-cell alignment grid overlay. */
    void enableGrid(bool enabled);

    /** @brief Recomputes color histograms and unique `4x8` multicolor cell signatures for the active selection. */
    void parseKoala();

signals:
    void selectedRegionUpdated(const QRect& rect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    void resetColors();
    void toFrameBuffer();
    void findUniqueCells();
    QRect getSelectedRegion() const;

    void reportResults();
    void strategyD02xAbove8();
    void strategyD02xAny();

    StateImport::KoalaBitmap _koala { };

    // one byte per pixel, although only the
    // 4 LSB will be used. Bits 7-4 are ignored
    // Bits 0-3 contains the C64 colors
    std::array<quint8, 160 * 200> _framebuffer;

    // key: color sequence
    // data: positions in screen ram
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> _uniqueCells;

    // first: count, second: color index
    std::vector<std::pair<int, int>> _colorsUsed;

    // colors to be used for d021, d022 and d023
    std::array<quint8, 3> _d02xColors;

    bool _displayGrid;

    // selecting ivars
    bool _selecting;
    QSize _selectingSize;
    QPoint _cursorPos;
};
