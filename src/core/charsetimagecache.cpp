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

/**
 * @file charsetimagecache.cpp
 * @brief Implementation of `CharsetImageCache` staleness checks and rebuild.
 */

#include "core/charsetimagecache.h"

#include "core/documentimagerenderer.h"
#include "palette.h"
#include "state.h"

const QImage& CharsetImageCache::sync(const State& state)
{
    const int activePalette = Palette::activePalette();
    const bool stale = _dirty
        || _image.isNull()
        || _cachedState != &state
        || _cachedPaletteIndex != activePalette;

    _lastSyncRebuilt = stale;
    if (!stale) {
        return _image;
    }

    // Reuse the export renderer so the dock and PNG export always agree pixel-for-pixel.
    // Convert once to premultiplied ARGB32 so every QPainter::drawImage() blit is conversion-free.
    _image = DocumentImageRenderer::renderCharset(state, false)
                 .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    _cachedState = &state;
    _cachedPaletteIndex = activePalette;
    _dirty = false;
    return _image;
}
