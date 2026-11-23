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

// Koala Painter multicolor bitmap preview, 160x200 framebuffer decoder, and
// $D02x color analyzer.
//
// Unpacks 2-bit VIC-II multicolor pixel pairs against Koala background, Screen
// RAM upper/lower nibbles, and Color RAM nibbles, then hashes each 4x8 cell
// into a 32-nibble hex signature to measure unique character tiles and rank
// candidate $D021/$D022/$D023 register assignments.

#include "importkoalabitmapwidget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QColor>
#include <QDebug>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include "core/result.h"
#include "palette.h"
#include "state.h"

namespace {

constexpr int kPixelSize = 1;
constexpr int kColumns = 40;
constexpr int kRows = 25;
constexpr int kOffset = 0;
constexpr int kCharWidth = 8;
constexpr int kFramebufferWidth = 160;
constexpr int kFramebufferHeight = 200;

constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr std::array<quint8, 4> kMulticolorMasks = { 192U, 48U, 12U, 3U };

// Computes a clamped cell selection span from `cursor_pos` to
// (`target_col`, `target_row`), extending by one cell when dragging right or
// down so the pointed-to cell is included in the normalized rectangle.
QSize ComputeDragSelectionSize(const QPoint& cursor_pos, int target_col, int target_row)
{
    int end_col = target_col;
    int end_row = target_row;
    if (end_col >= cursor_pos.x()) {
        ++end_col;
    }
    if (end_row >= cursor_pos.y()) {
        ++end_row;
    }

    return {
        std::clamp(end_col - cursor_pos.x(), -cursor_pos.x(), kColumns - cursor_pos.x()),
        std::clamp(end_row - cursor_pos.y(), -cursor_pos.y(), kRows - cursor_pos.y()),
    };
}

} // namespace

ImportKoalaBitmapWidget::ImportKoalaBitmapWidget(QWidget* parent)
    : QWidget(parent)
    , _framebuffer { }
    , _d02xColors { }
    , _displayGrid(false)
    , _selecting(false)
    , _selectingSize({ 1, 1 })
    , _cursorPos({ 0, 0 })
{
    setFixedSize(
        kPixelSize * kColumns * kCharWidth + kOffset * 2,
        kPixelSize * kRows * kCharWidth + kOffset * 2);
}

bool ImportKoalaBitmapWidget::loadKoala(const QString& koala_filepath)
{
    vchar64::Result<StateImport::KoalaBitmap> result = StateImport::loadKoala(koala_filepath);
    if (!result.isOk()) {
        _koala = { };
        return false;
    }

    _koala = std::move(result).value();
    toFrameBuffer();
    parseKoala();
    return true;
}

void ImportKoalaBitmapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

void ImportKoalaBitmapWidget::parseKoala()
{
    resetColors();
    findUniqueCells();
}

void ImportKoalaBitmapWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int row = 0; row < kFramebufferHeight; ++row) {
        for (int col = 0; col < kFramebufferWidth; ++col) {
            const int color_index = static_cast<int>(
                _framebuffer[static_cast<size_t>(row * kFramebufferWidth + col)]);
            painter.setBrush(Palette::getColor(color_index));
            painter.drawRect(
                (col * 2) * kPixelSize + kOffset,
                row * kPixelSize + kOffset,
                kPixelSize * 2,
                kPixelSize);
        }
    }

    if (_displayGrid) {
        QPen grid_pen = painter.pen();
        grid_pen.setColor(QColor(0, 128, 0));
        grid_pen.setStyle(Qt::DotLine);
        painter.setPen(grid_pen);

        for (int y = 0; y <= kFramebufferHeight; y += kCharWidth) {
            painter.drawLine(
                QPointF(0.0, static_cast<qreal>(y)),
                QPointF(320.0, static_cast<qreal>(y)));
        }

        for (int x = 0; x <= 320; x += kCharWidth) {
            painter.drawLine(
                QPointF(static_cast<qreal>(x), 0.0),
                QPointF(static_cast<qreal>(x), 200.0));
        }
    }

    if (_selecting) {
        QPen selection_pen = painter.pen();
        selection_pen.setColor({ 149, 195, 244, 255 });
        painter.setPen(selection_pen);
        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            _cursorPos.x() * kCharWidth * kPixelSize + kOffset,
            _cursorPos.y() * kCharWidth * kPixelSize + kOffset,
            _selectingSize.width() * kCharWidth * kPixelSize,
            _selectingSize.height() * kCharWidth * kPixelSize);
    }
}

void ImportKoalaBitmapWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const QPoint pos = event->pos();
    const int cell_col = std::clamp((pos.x() - kOffset) / kPixelSize / kCharWidth, 0, kColumns - 1);
    const int cell_row = std::clamp((pos.y() - kOffset) / kPixelSize / kCharWidth, 0, kRows - 1);

    if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
        _selecting = true;
        _selectingSize = ComputeDragSelectionSize(_cursorPos, cell_col, cell_row);
    } else {
        if (_selecting) {
            _selecting = false;
            _selectingSize = { 1, 1 };
        }
        _cursorPos = { cell_col, cell_row };
    }

    emit selectedRegionUpdated(getSelectedRegion());
    update();
}

void ImportKoalaBitmapWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    if (event->buttons() != Qt::LeftButton) {
        return;
    }

    const QPoint pos = event->pos();
    const int cell_col = (pos.x() - kOffset) / kPixelSize / kCharWidth;
    const int cell_row = (pos.y() - kOffset) / kPixelSize / kCharWidth;

    _selectingSize = ComputeDragSelectionSize(_cursorPos, cell_col, cell_row);
    _selecting = true;

    emit selectedRegionUpdated(getSelectedRegion());
    update();
}

void ImportKoalaBitmapWidget::resetColors()
{
    _colorsUsed.clear();
    for (int color_index = 0; color_index < 16; ++color_index) {
        _colorsUsed.emplace_back(0, color_index);
    }
    _uniqueCells.clear();
    _d02xColors.fill(0xffU);
}

void ImportKoalaBitmapWidget::toFrameBuffer()
{
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const size_t cell_index = static_cast<size_t>(row * kColumns + col);
            for (int pixel_row = 0; pixel_row < 8; ++pixel_row) {
                const quint8 bitmap_byte = _koala.bitmap[cell_index * 8U + static_cast<size_t>(pixel_row)];

                for (size_t pair_index = 0; pair_index < 4U; ++pair_index) {
                    const unsigned int shift = static_cast<unsigned int>(6U - pair_index * 2U);
                    const quint8 color_bits = static_cast<quint8>(
                        (bitmap_byte & kMulticolorMasks[pair_index]) >> shift);
                    quint8 color_index = 0U;

                    switch (color_bits) {
                    case 0x0U:
                        color_index = static_cast<quint8>(_koala.backgroundColor & 0x0FU);
                        break;
                    case 0x1U:
                        color_index = static_cast<quint8>((_koala.screenRam[cell_index] >> 4U) & 0x0FU);
                        break;
                    case 0x2U:
                        color_index = static_cast<quint8>(_koala.screenRam[cell_index] & 0x0FU);
                        break;
                    case 0x3U:
                        color_index = static_cast<quint8>(_koala.colorRam[cell_index] & 0x0FU);
                        break;
                    default:
                        break;
                    }

                    const size_t fb_index = static_cast<size_t>(
                                                (row * 8 + pixel_row) * kFramebufferWidth + col * 4)
                        + pair_index;
                    _framebuffer[fb_index] = color_index;
                }
            }
        }
    }

    update();
}

void ImportKoalaBitmapWidget::findUniqueCells()
{
    const QRect region = getSelectedRegion();
    const int min_col = std::clamp(region.x(), 0, kColumns);
    const int max_col = std::clamp(region.x() + region.width(), 0, kColumns);
    const int min_row = std::clamp(region.y(), 0, kRows);
    const int max_row = std::clamp(region.y() + region.height(), 0, kRows);

    for (int row = min_row; row < max_row; ++row) {
        for (int col = min_col; col < max_col; ++col) {
            std::array<char, 33> key_chars { };

            for (int pixel_row = 0; pixel_row < 8; ++pixel_row) {
                for (int pair_index = 0; pair_index < 4; ++pair_index) {
                    const size_t fb_index = static_cast<size_t>(
                        (row * 8 + pixel_row) * kFramebufferWidth + (col * 4 + pair_index));
                    const size_t color_index = static_cast<size_t>(_framebuffer[fb_index] & 0x0FU);
                    key_chars[static_cast<size_t>(pixel_row * 4 + pair_index)] = kHexDigits[color_index];
                    _colorsUsed[color_index].first++;
                }
            }
            const std::string cell_key(key_chars.data(), 32U);
            _uniqueCells[cell_key].emplace_back(col, row);
        }
    }

    std::sort(std::begin(_colorsUsed), std::end(_colorsUsed));
    std::reverse(std::begin(_colorsUsed), std::end(_colorsUsed));
}

QRect ImportKoalaBitmapWidget::getSelectedRegion() const
{
    if (!_selecting) {
        return { 0, 0, kColumns, kRows };
    }

    return QRect(_cursorPos, _selectingSize).normalized();
}

void ImportKoalaBitmapWidget::reportResults()
{
    int valid_cells = 0;
    int invalid_cells = 0;
    int valid_unique_cells = 0;
    int invalid_unique_cells = 0;

    for (const auto& [cell_key, coords] : _uniqueCells) {
        bool key_is_valid = true;
        for (const char ch : cell_key) {
            int color_index = ch - '0';
            if (color_index > 9) {
                color_index -= 7;
            }

            if (std::find(std::begin(_d02xColors), std::end(_d02xColors), color_index) != std::end(_d02xColors)) {
                continue;
            }

            if (color_index < 8) {
                key_is_valid = false;
                break;
            }
        }

        if (key_is_valid) {
            valid_cells += static_cast<int>(coords.size());
            ++valid_unique_cells;
        } else {
            invalid_cells += static_cast<int>(coords.size());
            ++invalid_unique_cells;
        }
    }

    qDebug() << "Valid cells:" << valid_cells << "Valid Unique cells:" << valid_unique_cells;
    qDebug() << "Invalid cells:" << invalid_cells << "Invalid Unique cells:" << invalid_unique_cells;
    qDebug() << "$d021,22,23=" << _d02xColors[0] << _d02xColors[1] << _d02xColors[2];
}

void ImportKoalaBitmapWidget::strategyD02xAbove8()
{
    size_t found = 0U;
    for (const std::pair<int, int>& color_count : _colorsUsed) {
        if (color_count.second >= 8 && color_count.first > 0) {
            _d02xColors[found++] = static_cast<quint8>(color_count.second);
            if (found == 3U) {
                break;
            }
        }
    }

    for (size_t index = 0U; index < _colorsUsed.size() && found < 3U; ++index) {
        if (_colorsUsed[index].second < 8 && _colorsUsed[index].first > 0) {
            _d02xColors[found++] = static_cast<quint8>(_colorsUsed[index].second);
        }
    }
}

void ImportKoalaBitmapWidget::strategyD02xAny()
{
    for (size_t index = 0U; index < 3U && index < _colorsUsed.size(); ++index) {
        _d02xColors[index] = static_cast<quint8>(_colorsUsed[index].second);
    }
}
