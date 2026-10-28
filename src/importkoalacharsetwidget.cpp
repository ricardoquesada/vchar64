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
 * @file importkoalacharsetwidget.cpp
 * @brief Synthesized C64 multicolor character-mode (`40x25`) preview canvas for Koala import.
 *
 * Renders the candidate 256-glyph multicolor charset, `40x25` Screen RAM indices,
 * and 3-bit (`0..7`) per-character Color RAM nibbles produced by `ImportKoalaDialog`
 * so the user can visually compare the quantized character-mode output side-by-side
 * against the original Koala bitmap.
 */

#include "importkoalacharsetwidget.h"

#include <algorithm>
#include <array>

#include <QPaintEvent>
#include <QPainter>
#include <QPointF>

#include "palette.h"

namespace {

constexpr int kPixelSize = 1;
constexpr int kColumns = 40;
constexpr int kRows = 25;
constexpr int kOffset = 0;

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

void ImportKoalaCharsetWidget::populateScreenAndColorRAM(
    const std::vector<std::pair<int, int>>& coords,
    quint8 screenRAM,
    quint8 colorRAM)
{
    for (const auto& [x, y] : coords) {
        if (x >= 0 && x < kColumns && y >= 0 && y < kRows) {
            _screenRAM[y * kColumns + x] = screenRAM;
        }
    }

    _colorRAMForChars[screenRAM] = colorRAM;
}

void ImportKoalaCharsetWidget::setCharset(int charIndex, const quint8* chardef)
{
    if (charIndex < 0 || charIndex >= 256 || chardef == nullptr) {
        return;
    }

    for (int i = 0; i < 8; ++i) {
        _charset[charIndex * 8 + i] = chardef[i];
    }
}

void ImportKoalaCharsetWidget::paintEvent(QPaintEvent* event)
{
    static constexpr std::array<quint8, 4> kMasks = { 192U, 48U, 12U, 3U };

    QPainter painter(this);
    painter.fillRect(event->rect(), Palette::getColor(static_cast<int>(_d02x[0])));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const quint8 c = _screenRAM[y * kColumns + x];
            const quint8* chardef = &_charset[static_cast<int>(c) * 8];

            for (int i = 0; i < 8; ++i) {
                const quint8 byte = chardef[i];

                for (size_t j = 0U; j < 4U; ++j) {
                    const unsigned int shift = static_cast<unsigned int>(6U - j * 2U);
                    const quint8 colorBits = static_cast<quint8>((byte & kMasks[j]) >> shift);
                    quint8 colorIndex = 0U;

                    switch (colorBits) {
                    case 0x0U:
                        colorIndex = _d02x[0];
                        break;
                    case 0x1U:
                        colorIndex = _d02x[1];
                        break;
                    case 0x2U:
                        colorIndex = _d02x[2];
                        break;
                    case 0x3U:
                        colorIndex = static_cast<quint8>(_colorRAMForChars[c] & 0x07U);
                        break;
                    default:
                        break;
                    }

                    painter.setBrush(Palette::getColor(static_cast<int>(colorIndex)));
                    painter.drawRect(
                        (x * 8 + static_cast<int>(j) * 2) * kPixelSize + kOffset,
                        (y * 8 + i) * kPixelSize + kOffset,
                        kPixelSize * 2,
                        kPixelSize);
                }
            }
        }
    }

    if (_displayGrid) {
        QPen gridPen = painter.pen();
        gridPen.setColor(QColor(0, 128, 0));
        gridPen.setStyle(Qt::DotLine);
        painter.setPen(gridPen);

        for (int y = 0; y <= 200; y += 8) {
            painter.drawLine(QPointF(0.0, static_cast<qreal>(y)), QPointF(320.0, static_cast<qreal>(y)));
        }

        for (int x = 0; x <= 320; x += 8) {
            painter.drawLine(QPointF(static_cast<qreal>(x), 0.0), QPointF(static_cast<qreal>(x), 200.0));
        }
    }
}

void ImportKoalaCharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}
