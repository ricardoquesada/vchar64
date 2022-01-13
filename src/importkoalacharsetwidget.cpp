/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "importkoalacharsetwidget.h"

#include <functional>

#include <QDebug>
#include <QFile>
#include <QPaintEvent>
#include <QPainter>

#include "mainwindow.h"
#include "palette.h"
#include "state.h"

static const int PIXEL_SIZE = 1;
static const int COLUMNS = 40;
static const int ROWS = 25;
static const int OFFSET = 0;

ImportKoalaCharsetWidget::ImportKoalaCharsetWidget(QWidget* parent)
    : QWidget(parent)
    , _displayGrid(false)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
        PIXEL_SIZE * ROWS * 8 + OFFSET * 2);

    clean();
}

void ImportKoalaCharsetWidget::clean()
{
    std::memset(_screenRAM, 0, sizeof(_screenRAM));
    std::memset(_colorRAMForChars, 0, sizeof(_colorRAMForChars));
    std::memset(_charset, 0, sizeof(_charset));
    _d02x[0] = 0;
    _d02x[1] = 1;
    _d02x[2] = 2;

    update();
}

void ImportKoalaCharsetWidget::populateScreenAndColorRAM(
    const std::vector<std::pair<int, int>>& coords, quint8 screenRAM, quint8 colorRAM)
{
    for (const auto& pair : coords) {
        int x = pair.first;
        int y = pair.second;
        _screenRAM[y * 40 + x] = screenRAM;
    }

    _colorRAMForChars[screenRAM] = colorRAM;
}

void ImportKoalaCharsetWidget::setCharset(int charIndex, const quint8* chardef)
{
    for (int i = 0; i < 8; i++) {
        _charset[charIndex * 8 + i] = chardef[i];
    }
}

//
// Overriden
//
void ImportKoalaCharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), Palette::getColor(_d02x[0]));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 40; ++x) {
            auto c = _screenRAM[y * 40 + x];
            auto chardef = &_charset[c * 8];

            for (int i = 0; i < 8; ++i) {
                static const quint8 masks[] = { 192, 48, 12, 3 };
                auto byte = chardef[i];

                for (int j = 0; j < 4; ++j) {
                    quint8 colorIndex = 0;
                    // get the two bits that reprent the color
                    quint8 color = byte & masks[j];
                    color >>= 6 - j * 2;

                    switch (color) {
                    // bitmask 00: background ($d021)
                    case 0x0:
                        colorIndex = _d02x[0];
                        break;

                    // bitmask 01: multicolor #1 ($d022)
                    case 0x1:
                        colorIndex = _d02x[1];
                        break;

                    // bitmask 10: multicolor #2 ($d023)
                    case 0x2:
                        colorIndex = _d02x[2];
                        break;

                    // bitmask 11: color RAM
                    case 0x3:
                        colorIndex = _colorRAMForChars[c] - 8;
                        break;
                    default:
                        qDebug()
                            << "ImportKoalaWidget::paintEvent Invalid color: "
                            << color
                            << " at x,y="
                            << x << y;
                        break;
                    }
                    painter.setBrush(Palette::getColor(colorIndex));
                    painter.drawRect((x * 8 + j * 2) * PIXEL_SIZE + OFFSET,
                        (y * 8 + i) * PIXEL_SIZE + OFFSET,
                        PIXEL_SIZE * 2,
                        PIXEL_SIZE);
                }
            }
        }
    }

    if (_displayGrid) {
        auto pen = painter.pen();
        pen.setColor(QColor(0, 128, 0));
        pen.setStyle(Qt::DotLine);
        painter.setPen(pen);

        for (int y = 0; y <= 200; y = y + 8)
            painter.drawLine(QPointF(0, y), QPointF(320, y));

        for (int x = 0; x <= 320; x = x + 8)
            painter.drawLine(QPointF(x, 0), QPointF(x, 200));
    }

    painter.end();
}

void ImportKoalaCharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled) {
        _displayGrid = enabled;
        update();
    }
}
