/****************************************************************************
Copyright 2026 Ricardo Quesada

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
 * @file tileatlascache.h
 * @brief Dirty-tracked contiguous `QImage` tile sheet cache for O(1) viewport blitting.
 *
 * Replaces legacy `QImage* _tileImages[256]` arrays (which previously reallocated and
 * re-rasterized 256 heap `QImage`s on every `MapWidget::paintEvent`) with a single
 * `QImage::Format_ARGB32_Premultiplied` sprite sheet backed by a 256-bit dirty set.
 */

#include <bitset>

#include <QImage>
#include <QRect>
#include <QSize>

#include "state.h"

/**
 * @brief Dirty-tracked 16-column `QImage` tile sheet cache for headless and UI tile rendering.
 *
 * Automatically invalidates all 256 tile slots whenever the bound `const State*` pointer
 * (MDI active-document switch), global `Palette::activePalette()`, tile dimensions or
 * interleaving, or global VIC-II color/mode registers change.
 */
class TileAtlasCache {
public:
    static constexpr int kColumns = 16;
    static constexpr int kMaxTiles = 256;

    TileAtlasCache() = default;

    /** @brief Marks a single tile slot (`0..255`) for re-rasterization on the next `sync()`. */
    void markTileDirty(int tileIndex) noexcept;

    /** @brief Marks all 256 tile slots dirty after bulk charset, color, or mode updates. */
    void markAllDirty() noexcept;

    /**
     * @brief Synchronizes the backing `QImage` atlas against `state`, rasterizing only dirty tiles.
     * @param state Source document whose charset, tile properties, and VIC-II pens drive rendering.
     */
    void sync(const State& state);

    /** @brief Returns the premultiplied ARGB32 sprite sheet updated by the most recent `sync()`. */
    [[nodiscard]] const QImage& atlasImage() const noexcept
    {
        return _atlas;
    }

    /**
     * @brief Returns the pixel sub-rectangle inside `atlasImage()` for `tileIndex`
     *        (clamped safely to `[0, maxTiles - 1]`).
     */
    [[nodiscard]] QRect sourceRectForTile(int tileIndex) const noexcept;

    /** @brief Returns `true` if `tileIndex` is scheduled for re-rasterization on the next `sync()`. */
    [[nodiscard]] bool isTileDirty(int tileIndex) const noexcept;

    /** @brief Returns the number of tiles rasterized during the most recent `sync()` call (for testing). */
    [[nodiscard]] int lastSyncedTileCount() const noexcept
    {
        return _lastSyncedTileCount;
    }

private:
    void rasterizeTile(const State& state, int tileIndex, int tw, int th, int interleaved);

    QImage _atlas;
    std::bitset<kMaxTiles> _dirtyTiles { };
    bool _allDirty = true;

    const State* _cachedState = nullptr;
    int _cachedPaletteIndex = -1;
    QSize _cachedTileSize { 0, 0 };
    int _cachedInterleaved = 0;
    bool _cachedMulticolorMode = false;
    State::ForegroundColorMode _cachedFgMode = State::FOREGROUND_COLOR_GLOBAL;
    int _cachedBgColor = -1;
    int _cachedMc1Color = -1;
    int _cachedMc2Color = -1;
    int _cachedGlobalFgColor = -1;

    int _maxTiles = kMaxTiles;
    int _tilePixelWidth = 8;
    int _tilePixelHeight = 8;
    int _lastSyncedTileCount = 0;
};

namespace vchar64 {
using ::TileAtlasCache;
} // namespace vchar64
