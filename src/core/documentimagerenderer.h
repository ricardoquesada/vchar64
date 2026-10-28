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
 * @file documentimagerenderer.h
 * @brief Display-server-free `QImage` rasterizer for charset, tileset, and map PNG exports.
 *
 * Eliminates the legacy coupling where `State::exportPNG` grabbed pixels from live
 * `TilesetWidget*` and `MapWidget*` UI instances. Uses pure CPU `QImage::Format_ARGB32`
 * scanlines so PNG exports execute identically in GUI sessions and under `QTEST_GUILESS_MAIN`.
 */

#include <QImage>

class State;

/**
 * @brief Pure headless `QImage` rasterizer for charset, tileset, and map PNG exports.
 */
class DocumentImageRenderer {
public:
    /**
     * @brief Renders all 256 charset glyphs as a `32 x 8` character sheet (`256 x 64` pixels).
     * @param state Source document providing charset bytes and VIC-II color modes.
     * @param transparentBackground When `true`, background pen (`00` / `0`) pixels emit `alpha == 0`.
     */
    [[nodiscard]] static QImage renderCharset(const State& state, bool transparentBackground = false);

    /**
     * @brief Renders all active tiles arranged in `max(1, 32 / tw)` columns.
     * @param state Source document providing charset, tile dimensions/interleaving, and Color RAM.
     * @param transparentBackground When `true`, background pen pixels emit `alpha == 0`.
     */
    [[nodiscard]] static QImage renderTileset(const State& state, bool transparentBackground = false);

    /**
     * @brief Renders the full 2D level map at `(mapWidth * tw * 8) x (mapHeight * th * 8)` pixels.
     * @param state Source document providing map cells, tileset geometry, and VIC-II pens.
     * @param transparentBackground When `true`, background pen pixels emit `alpha == 0`.
     */
    [[nodiscard]] static QImage renderMap(const State& state, bool transparentBackground = false);
};

namespace vchar64 {
using ::DocumentImageRenderer;
} // namespace vchar64
