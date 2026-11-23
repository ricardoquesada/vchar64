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

// Synthesized C64 multicolor character-mode (40x25) preview canvas for Koala import.
//
// Renders the candidate 256-glyph multicolor charset, 40x25 Screen RAM indices,
// and 3-bit (0..7) per-character Color RAM nibbles produced by ImportKoalaDialog
// so the user can visually compare the quantized character-mode output side-by-side
// against the original Koala bitmap.

#include "importkoalacharsetwidget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <utility>

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>

#include "palette.h"

namespace {

constexpr int kPixelSize = 1;
constexpr int kColumns = 40;
constexpr int kRows = 25;
constexpr int kOffset = 0;
constexpr std::array<quint8, 4> kMulticolorMasks = { 192U, 48U, 12U, 3U };

} // namespace

ImportKoalaCharsetWidget::ImportKoalaCharsetWidget(QWidget* parent)
    : QWidget(parent)
    , _displayGrid(false)
{
    setFixedSize(
        kPixelSize * kColumns * 8 + kOffset * 2,
        kPixelSize * kRows * 8 + kOffset * 2);

    clean();
}

void ImportKoalaCharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Palette::getColor(static_cast<int>(_d02x[0])));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const quint8 char_code = _screenRAM[row * kColumns + col];
            const quint8* char_bytes = &_charset[static_cast<int>(char_code) * 8];

            for (int pixel_row = 0; pixel_row < 8; ++pixel_row) {
                const quint8 row_byte = char_bytes[pixel_row];

                for (size_t pair_index = 0U; pair_index < 4U; ++pair_index) {
                    const unsigned int shift = static_cast<unsigned int>(6U - pair_index * 2U);
                    const quint8 color_bits = static_cast<quint8>((row_byte & kMulticolorMasks[pair_index]) >> shift);
                    quint8 color_index = 0U;

                    switch (color_bits) {
                    case 0x0U:
                        color_index = _d02x[0];
                        break;
                    case 0x1U:
                        color_index = _d02x[1];
                        break;
                    case 0x2U:
                        color_index = _d02x[2];
                        break;
                    case 0x3U:
                        color_index = static_cast<quint8>(_colorRAMForChars[char_code] & 0x07U);
                        break;
                    default:
                        break;
                    }

                    painter.setBrush(Palette::getColor(static_cast<int>(color_index)));
                    painter.drawRect(
                        (col * 8 + static_cast<int>(pair_index) * 2) * kPixelSize + kOffset,
                        (row * 8 + pixel_row) * kPixelSize + kOffset,
                        kPixelSize * 2,
                        kPixelSize);
                }
            }
        }
    }

    if (_displayGrid) {
        QPen grid_pen = painter.pen();
        grid_pen.setColor(QColor(0, 128, 0));
        grid_pen.setStyle(Qt::DotLine);
        painter.setPen(grid_pen);

        for (int pixel_y = 0; pixel_y <= 200; pixel_y += 8) {
            painter.drawLine(
                QPointF(0.0, static_cast<qreal>(pixel_y)),
                QPointF(320.0, static_cast<qreal>(pixel_y)));
        }

        for (int pixel_x = 0; pixel_x <= 320; pixel_x += 8) {
            painter.drawLine(
                QPointF(static_cast<qreal>(pixel_x), 0.0),
                QPointF(static_cast<qreal>(pixel_x), 200.0));
        }
    }
}

void ImportKoalaCharsetWidget::populateScreenAndColorRAM(
    const std::vector<std::pair<int, int>>& coords,
    quint8 screen_ram,
    quint8 color_ram)
{
    for (const auto& [col, row] : coords) {
        if (col >= 0 && col < kColumns && row >= 0 && row < kRows) {
            _screenRAM[row * kColumns + col] = screen_ram;
        }
    }

    _colorRAMForChars[screen_ram] = color_ram;
}

void ImportKoalaCharsetWidget::setCharset(int char_index, const quint8* char_bytes)
{
    if (char_index < 0 || char_index >= 256 || char_bytes == nullptr) {
        return;
    }

    for (int byte_index = 0; byte_index < 8; ++byte_index) {
        _charset[char_index * 8 + byte_index] = char_bytes[byte_index];
    }
}

void ImportKoalaCharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void ImportKoalaCharsetWidget::clean()
{
    std::fill(std::begin(_screenRAM), std::end(_screenRAM), 0U);
    std::fill(std::begin(_colorRAMForChars), std::end(_colorRAMForChars), 0U);
    std::fill(std::begin(_charset), std::end(_charset), 0U);

    _d02x[0] = 0U;
    _d02x[1] = 1U;
    _d02x[2] = 2U;

    update();
}
