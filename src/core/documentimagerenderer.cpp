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

#include "core/documentimagerenderer.h"

/**
 * @file documentimagerenderer.cpp
 * @brief Headless `QImage::Format_ARGB32` scanline renderer for charset, tileset, and map PNGs.
 *
 * Uses straight (unpremultiplied) `Format_ARGB32` so transparent background pixels (`0x00000000`)
 * and opaque VIC-II palette colors serialize losslessly through Qt's PNG writer without
 * touching `QPixmap` or requiring an active `QGuiApplication` QPA plugin.
 */

#include <algorithm>
#include <array>

#include "palette.h"
#include "state.h"

namespace {

void drawCharToImage(
    const State& state,
    QImage& image,
    int destPixelX,
    int destPixelY,
    int charIndex,
    int tileIndex,
    bool transparentBackground)
{
    if (charIndex < 0 || charIndex >= 256) {
        return;
    }

    const auto& charset = state.charset();
    const bool isMc = state.shouldBeDisplayedInMulticolor2(tileIndex);

    const QRgb bgRgb = transparentBackground
        ? qRgba(0, 0, 0, 0)
        : Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba();
    const QRgb mc1Rgb = Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR1)).rgba();
    const QRgb mc2Rgb = Palette::getColor(state.getColorForPen(State::PEN_MULTICOLOR2)).rgba();

    const int rawFg = state.getColorForPen(State::PEN_FOREGROUND, tileIndex);
    const QRgb fgHiResRgb = Palette::getColor(rawFg).rgba();
    // VIC-II multicolor character mode restricts foreground Color RAM to pens 0..7.
    const QRgb fgMcRgb = Palette::getColor(static_cast<int>(static_cast<unsigned int>(rawFg) & 0x07U)).rgba();
    const std::array<QRgb, 4> mcLut = { bgRgb, mc1Rgb, mc2Rgb, fgMcRgb };

    const size_t charBase = static_cast<size_t>(charIndex) * 8U;
    for (int row = 0; row < 8; ++row) {
        const uint8_t rowByte = charset[charBase + static_cast<size_t>(row)];
        auto* scanline = static_cast<QRgb*>(static_cast<void*>(image.scanLine(destPixelY + row)));
        QRgb* dst = scanline + destPixelX;

        if (isMc) {
            for (unsigned int pairIdx = 0; pairIdx < 4U; ++pairIdx) {
                const unsigned int shift = 6U - (pairIdx * 2U);
                const size_t penBits = static_cast<size_t>((rowByte >> shift) & 0x03U);
                const QRgb px = mcLut[penBits];
                dst[pairIdx * 2U] = px;
                dst[pairIdx * 2U + 1U] = px;
            }
        } else {
            for (unsigned int bit = 0; bit < 8U; ++bit) {
                const bool set = ((rowByte >> (7U - bit)) & 0x01U) != 0U;
                dst[bit] = set ? fgHiResRgb : bgRgb;
            }
        }
    }
}

void drawTileToImage(
    const State& state,
    QImage& image,
    int destPixelX,
    int destPixelY,
    int tileIndex,
    int tw,
    int th,
    int interleaved,
    bool transparentBackground)
{
    int charIdx = (interleaved == 1) ? (tileIndex * tw * th) : tileIndex;
    const int subChars = tw * th;

    for (int sub = 0; sub < subChars; ++sub) {
        const int subX = (sub % tw) * 8;
        const int subY = (sub / tw) * 8;
        drawCharToImage(
            state,
            image,
            destPixelX + subX,
            destPixelY + subY,
            charIdx,
            tileIndex,
            transparentBackground);
        charIdx += interleaved;
    }
}

} // namespace

QImage DocumentImageRenderer::renderCharset(const State& state, bool transparentBackground)
{
    constexpr int kCols = 32;
    constexpr int kRows = 8;

    QImage image(kCols * 8, kRows * 8, QImage::Format_ARGB32);
    const QRgb fillRgb = transparentBackground
        ? qRgba(0, 0, 0, 0)
        : Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba();
    image.fill(fillRgb);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int charIdx = row * kCols + col;
            const int tileIdx = state.getTileIndexFromCharIndex(charIdx);
            drawCharToImage(state, image, col * 8, row * 8, charIdx, tileIdx, transparentBackground);
        }
    }
    return image;
}

QImage DocumentImageRenderer::renderTileset(const State& state, bool transparentBackground)
{
    constexpr int kBaseCharColumns = 32;
    const State::TileProperties props = state.getTileProperties();
    const int tw = std::clamp(props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int th = std::clamp(props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(props.interleaved, 1, 256);

    const int tileCols = std::max(1, kBaseCharColumns / tw);
    const int maxTiles = std::max(1, 256 / (tw * th));
    const int tileRows = std::max(1, (maxTiles + tileCols - 1) / tileCols);

    QImage image(tileCols * tw * 8, tileRows * th * 8, QImage::Format_ARGB32);
    const QRgb fillRgb = transparentBackground
        ? qRgba(0, 0, 0, 0)
        : Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba();
    image.fill(fillRgb);

    for (int tileIdx = 0; tileIdx < maxTiles; ++tileIdx) {
        const int tileX = (tileIdx % tileCols) * tw * 8;
        const int tileY = (tileIdx / tileCols) * th * 8;
        drawTileToImage(state, image, tileX, tileY, tileIdx, tw, th, interleaved, transparentBackground);
    }
    return image;
}

QImage DocumentImageRenderer::renderMap(const State& state, bool transparentBackground)
{
    const QSize mapSize = state.getMapSize();
    const int mapW = std::max(1, mapSize.width());
    const int mapH = std::max(1, mapSize.height());

    const State::TileProperties props = state.getTileProperties();
    const int tw = std::clamp(props.size.width(), 1, State::MAX_TILE_WIDTH);
    const int th = std::clamp(props.size.height(), 1, State::MAX_TILE_HEIGHT);
    const int interleaved = std::clamp(props.interleaved, 1, 256);
    const int tilePixelW = tw * 8;
    const int tilePixelH = th * 8;

    QImage image(mapW * tilePixelW, mapH * tilePixelH, QImage::Format_ARGB32);
    const QRgb fillRgb = transparentBackground
        ? qRgba(0, 0, 0, 0)
        : Palette::getColor(state.getColorForPen(State::PEN_BACKGROUND)).rgba();
    image.fill(fillRgb);

    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            const int tileIdx = state.getTileIndexFromMap(QPoint(x, y));
            drawTileToImage(
                state,
                image,
                x * tilePixelW,
                y * tilePixelH,
                tileIdx,
                tw,
                th,
                interleaved,
                transparentBackground);
        }
    }
    return image;
}
