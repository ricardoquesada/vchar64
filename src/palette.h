/****************************************************************************
Copyright 2015 Ricardo Quesada

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

#pragma once

/**
 * @file palette.h
 * @brief VIC-II 16-color palette registry (Pepto, VICE, CCS64, Frodo, Grayscale, Colodore).
 *
 * Lives in headless `vchar64_core` so `TileAtlasCache` and `DocumentImageRenderer` can
 * resolve C64 color indices without `Qt6::Widgets`. All color lookups mask indices with
 * `& 0x0F` and resolve localized color names dynamically at call time.
 */

#include <QColor>
#include <QCoreApplication>
#include <QString>

class State;

/**
 * @brief VIC-II 16-color palette registry with bounds-safe color and runtime-translated name lookups.
 */
class Palette {
    Q_DECLARE_TR_FUNCTIONS(Palette)

public:
    enum {
        PEPTO = 0,
        VICE = 1,
        CCS64 = 2,
        FRODO = 3,
        GRAYSCALE = 4,
        COLODORE = 5,
    };

    static constexpr int kPaletteCount = 6;
    static constexpr int kColorCount = 16;

    /** @brief Returns the active palette's `QColor` for `colorIndex & 0x0F`. */
    [[nodiscard]] static const QColor& getColor(int colorIndex);

    /**
     * @brief Resolves the effective display `QColor` for `pen` (`0..3`) in `state`,
     *        applying VIC-II multicolor 3-bit foreground masking (`& 0x07`) when active.
     */
    [[nodiscard]] static const QColor& getColorForPen(const State* state, int pen);

    /**
     * @brief Returns the localized display name for `colorIndex & 0x0F` using the
     *        currently installed `QTranslator` (`QT_TR_NOOP` table).
     */
    [[nodiscard]] static QString colorName(int colorIndex);

    /** @brief Sets the active global VIC-II palette index, clamped to `[0, kPaletteCount - 1]`. */
    static void setActivePalette(int paletteIndex) noexcept;
    [[nodiscard]] static int activePalette() noexcept;
    [[nodiscard]] static int getActivePalette() noexcept;

private:
    static int _paletteIndex;
};
