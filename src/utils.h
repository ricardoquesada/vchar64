/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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
 * @file utils.h
 * @brief VIC-II character-cell painting helpers and 8-bit ASCII-to-screencode converters.
 *
 * Provides `QPainter` (`QRectF`) and `QImage` (`QRgb`) single-glyph rasterizers
 * honoring Hi-Res (`8x8`) and Multicolor (`4x8` double-wide, 3-bit Color RAM `& 0x07`)
 * VIC-II rules, alongside ASCII-to-Commodore/Atari screen-code mapping tables.
 */

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QPainter;
class QImage;
class QPoint;
class QSizeF;
QT_END_NAMESPACE

class State;

/**
 * @brief Paints a single `8x8` character glyph (`0..255`) onto a `QPainter` at arbitrary pixel scale.
 * @param state     Source document supplying charset bits, pen colors, and multicolor mode.
 * @param painter   Destination `QPainter` (brush is mutated per pixel).
 * @param pixelSize Logical pixel dimensions (e.g., zoomed `BigCharWidget` cell size).
 * @param offset    Device/logical origin offset in pixels.
 * @param orig      Sub-character grid coordinate within a multi-char tile (`(0, 0)` for `1x1`).
 * @param charIdx   Character index (`0..255`) in the 2 KiB charset buffer.
 */
void utilsDrawCharInPainter(
    const State* state,
    QPainter* painter,
    const QSizeF& pixelSize,
    const QPoint& offset,
    const QPoint& orig,
    int charIdx);

/**
 * @brief Rasterizes a single `8x8` character glyph (`0..255`) at `1:1` pixel scale into a `QImage`.
 */
void utilsDrawCharInImage(
    const State* state,
    QImage* image,
    const QPoint& offset,
    int charIdx);

/** @brief Maps a printable ASCII code to a C64 screen code, probing the active charset if needed. */
[[nodiscard]] quint8 utilsAsciiToScreenCode(const State* state, quint8 ascii);

/** @brief Maps a printable ASCII code to its standard Commodore 8-bit PETSCII screen code. */
[[nodiscard]] quint8 utilsAsciiToCommodore8Bit(quint8 ascii);

/** @brief Maps a printable ASCII code to its standard Atari 8-bit ATASCII internal screen code. */
[[nodiscard]] quint8 utilsAsciiToAtari8Bit(quint8 ascii);
