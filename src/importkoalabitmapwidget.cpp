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
 * @file importkoalabitmapwidget.cpp
 * @brief Koala Painter multicolor bitmap preview, `160x200` framebuffer decoder, and `$D02x` color analyzer.
 *
 * Unpacks 2-bit VIC-II multicolor pixel pairs against Koala background, Screen RAM
 * upper/lower nibbles, and Color RAM nibbles, then hashes each `4x8` cell into a
 * 32-nibble hex signature to measure unique character tiles and rank candidate
 * `$D021`/`$D022`/`$D023` register assignments.
 */

#include "importkoalabitmapwidget.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QDebug>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QRect>

#include "palette.h"
#include "state.h"

namespace {

constexpr int kPixelSize = 1;
constexpr int kColumns = 40;
constexpr int kRows = 25;
constexpr int kOffset = 0;

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
        kPixelSize * kColumns * 8 + kOffset * 2,
        kPixelSize * kRows * 8 + kOffset * 2);
}

void ImportKoalaBitmapWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 160; ++x) {
            const int color = static_cast<int>(_framebuffer[static_cast<size_t>(y * 160 + x)]);
            painter.setBrush(Palette::getColor(color));
            painter.drawRect(
                (x * 2) * kPixelSize + kOffset,
                y * kPixelSize + kOffset,
                kPixelSize * 2,
                kPixelSize);
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

    if (_selecting) {
        QPen selPen = painter.pen();
        selPen.setColor({ 149, 195, 244, 255 });
        painter.setPen(selPen);
        painter.setBrush(QColor(149, 195, 244, 64));
        painter.drawRect(
            _cursorPos.x() * 8 * kPixelSize + kOffset,
            _cursorPos.y() * 8 * kPixelSize + kOffset,
            _selectingSize.width() * 8 * kPixelSize,
            _selectingSize.height() * 8 * kPixelSize);
    }
}

void ImportKoalaBitmapWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    const QPoint pos = event->pos();
    int x = std::clamp((pos.x() - kOffset) / kPixelSize / 8, 0, kColumns - 1);
    int y = std::clamp((pos.y() - kOffset) / kPixelSize / 8, 0, kRows - 1);

    if (event->button() == Qt::LeftButton) {
        if ((QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0) {
            _selecting = true;

            if (x >= _cursorPos.x()) {
                ++x;
            }
            if (y >= _cursorPos.y()) {
                ++y;
            }

            _selectingSize = {
                std::clamp(x - _cursorPos.x(), -_cursorPos.x(), kColumns - _cursorPos.x()),
                std::clamp(y - _cursorPos.y(), -_cursorPos.y(), kRows - _cursorPos.y())
            };
        } else {
            if (_selecting) {
                _selecting = false;
                _selectingSize = { 1, 1 };
            }
            _cursorPos = { x, y };
        }

        emit selectedRegionUpdated(getSelectedRegion());
        update();
    }
}

void ImportKoalaBitmapWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        int x = (pos.x() - kOffset) / kPixelSize / 8;
        int y = (pos.y() - kOffset) / kPixelSize / 8;

        if (x >= _cursorPos.x()) {
            ++x;
        }
        if (y >= _cursorPos.y()) {
            ++y;
        }

        _selectingSize = {
            std::clamp(x - _cursorPos.x(), -_cursorPos.x(), kColumns - _cursorPos.x()),
            std::clamp(y - _cursorPos.y(), -_cursorPos.y(), kRows - _cursorPos.y())
        };
        _selecting = true;

        emit selectedRegionUpdated(getSelectedRegion());
        update();
    }
}

bool ImportKoalaBitmapWidget::loadKoala(const QString& koalaFilepath)
{
    auto result = StateImport::loadKoala(koalaFilepath);
    if (!result.isOk()) {
        _koala = { };
        return false;
    }

    _koala = std::move(result).value();
    toFrameBuffer();
    parseKoala();
    return true;
}

void ImportKoalaBitmapWidget::parseKoala()
{
    resetColors();
    findUniqueCells();
}

void ImportKoalaBitmapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}

QRect ImportKoalaBitmapWidget::getSelectedRegion() const
{
    if (!_selecting) {
        return { 0, 0, kColumns, kRows };
    }

    return QRect(_cursorPos, _selectingSize).normalized();
}

void ImportKoalaBitmapWidget::resetColors()
{
    _colorsUsed.clear();
    for (int i = 0; i < 16; ++i) {
        _colorsUsed.emplace_back(0, i);
    }
    _uniqueCells.clear();
    _d02xColors.fill(0xffU);
}

void ImportKoalaBitmapWidget::findUniqueCells()
{
    static constexpr std::string_view kHex = "0123456789ABCDEF";

    const QRect region = getSelectedRegion();
    const int minX = std::clamp(region.x(), 0, kColumns);
    const int maxX = std::clamp(region.x() + region.width(), 0, kColumns);
    const int minY = std::clamp(region.y(), 0, kRows);
    const int maxY = std::clamp(region.y() + region.height(), 0, kRows);

    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            std::array<char, 33> key { };

            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 4; ++j) {
                    const size_t fbIdx = static_cast<size_t>((y * 8 + i) * 160 + (x * 4 + j));
                    const size_t colorIndex = static_cast<size_t>(_framebuffer[fbIdx] & 0x0FU);
                    key[static_cast<size_t>(i * 4 + j)] = kHex[colorIndex];
                    _colorsUsed[colorIndex].first++;
                }
            }
            std::string skey(key.data(), 32U);
            _uniqueCells[skey].emplace_back(x, y);
        }
    }

    std::sort(std::begin(_colorsUsed), std::end(_colorsUsed));
    std::reverse(std::begin(_colorsUsed), std::end(_colorsUsed));
}

void ImportKoalaBitmapWidget::toFrameBuffer()
{
    static constexpr std::array<quint8, 4> kMasks = { 192U, 48U, 12U, 3U };

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const size_t cellIdx = static_cast<size_t>(y * kColumns + x);
            for (int i = 0; i < 8; ++i) {
                const quint8 byte = _koala.bitmap[cellIdx * 8U + static_cast<size_t>(i)];

                for (size_t j = 0; j < 4U; ++j) {
                    const unsigned int shift = static_cast<unsigned int>(6U - j * 2U);
                    const quint8 colorBits = static_cast<quint8>((byte & kMasks[j]) >> shift);
                    quint8 colorIndex = 0U;

                    switch (colorBits) {
                    case 0x0U:
                        colorIndex = static_cast<quint8>(_koala.backgroundColor & 0x0FU);
                        break;
                    case 0x1U:
                        colorIndex = static_cast<quint8>((_koala.screenRam[cellIdx] >> 4U) & 0x0FU);
                        break;
                    case 0x2U:
                        colorIndex = static_cast<quint8>(_koala.screenRam[cellIdx] & 0x0FU);
                        break;
                    case 0x3U:
                        colorIndex = static_cast<quint8>(_koala.colorRam[cellIdx] & 0x0FU);
                        break;
                    default:
                        break;
                    }

                    const size_t fbIdx = static_cast<size_t>((y * 8 + i) * 160 + x * 4) + j;
                    _framebuffer[fbIdx] = colorIndex;
                }
            }
        }
    }

    update();
}

void ImportKoalaBitmapWidget::reportResults()
{
    int validCells = 0;
    int invalidCells = 0;
    int validUniqueCells = 0;
    int invalidUniqueCells = 0;

    for (const auto& [key, coords] : _uniqueCells) {
        bool keyIsValid = true;
        for (const char ch : key) {
            int c = ch - '0';
            if (c > 9) {
                c -= 7;
            }

            if (std::find(std::begin(_d02xColors), std::end(_d02xColors), c) != std::end(_d02xColors)) {
                continue;
            }

            if (c < 8) {
                keyIsValid = false;
                break;
            }
        }

        if (keyIsValid) {
            validCells += static_cast<int>(coords.size());
            ++validUniqueCells;
        } else {
            invalidCells += static_cast<int>(coords.size());
            ++invalidUniqueCells;
        }
    }

    qDebug() << "Valid cells:" << validCells << "Valid Unique cells:" << validUniqueCells;
    qDebug() << "Invalid cells:" << invalidCells << "Invalid Unique cells:" << invalidUniqueCells;
    qDebug() << "$d021,22,23=" << _d02xColors[0] << _d02xColors[1] << _d02xColors[2];
}

void ImportKoalaBitmapWidget::strategyD02xAbove8()
{
    size_t found = 0U;
    for (const auto& color : _colorsUsed) {
        if (color.second >= 8 && color.first > 0) {
            _d02xColors[found++] = static_cast<quint8>(color.second);
            if (found == 3U) {
                break;
            }
        }
    }

    for (size_t j = 0U; j < _colorsUsed.size() && found < 3U; ++j) {
        if (_colorsUsed[j].second < 8 && _colorsUsed[j].first > 0) {
            _d02xColors[found++] = static_cast<quint8>(_colorsUsed[j].second);
        }
    }
}

void ImportKoalaBitmapWidget::strategyD02xAny()
{
    for (size_t i = 0U; i < 3U && i < _colorsUsed.size(); ++i) {
        _d02xColors[i] = static_cast<quint8>(_colorsUsed[i].second);
    }
}
