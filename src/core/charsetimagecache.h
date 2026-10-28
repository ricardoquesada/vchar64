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
 * @file charsetimagecache.h
 * @brief Cached `256 x 64` image of all 256 glyphs for the charset dock.
 *
 * Replaces per-paint rasterization of 256 glyphs (`utilsDrawCharInPainter` x 256 on
 * every hover repaint) with a single cached image that is rebuilt only when needed.
 */

#include <QImage>

class State;

/**
 * @brief Lazily rebuilt `32 x 8` glyph sheet (`256 x 64` pixels, premultiplied ARGB32).
 *
 * `sync()` rebuilds the image only when one of these holds:
 * - `invalidate()` was called (charset bytes, colors, multicolor mode, or tile properties changed),
 * - the bound `State` differs from the previous call (MDI active-document switch),
 * - the global `Palette::activePalette()` changed.
 *
 * Example:
 * @code
 * CharsetImageCache cache;
 * painter.drawImage(QPointF(2, 2), cache.sync(state)); // first call rasterizes
 * painter.drawImage(QPointF(2, 2), cache.sync(state)); // reuses cached image
 * @endcode
 */
class CharsetImageCache {
public:
    /** @brief Forces a rebuild on the next `sync()`. */
    void invalidate() noexcept
    {
        _dirty = true;
    }

    /**
     * @brief Returns the glyph sheet for `state`, rebuilding it first if stale.
     * @param state Source document providing charset bytes and VIC-II pens.
     * @return Reference to the cached image (valid until the next `sync()`).
     */
    const QImage& sync(const State& state);

    /** @brief Returns the image produced by the most recent `sync()`. */
    [[nodiscard]] const QImage& image() const noexcept
    {
        return _image;
    }

    /** @brief Returns `true` if the most recent `sync()` rebuilt the image (for testing). */
    [[nodiscard]] bool lastSyncRebuilt() const noexcept
    {
        return _lastSyncRebuilt;
    }

private:
    QImage _image;
    const State* _cachedState = nullptr;
    int _cachedPaletteIndex = -1;
    bool _dirty = true;
    bool _lastSyncRebuilt = false;
};
