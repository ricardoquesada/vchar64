/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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
 * @file importvicecharsetwidget.cpp
 * @brief `TileAtlasCache`-backed `16x16` charset preview painter for VICE `.vsf` import.
 *
 * Clears the exposed device-coordinate `event->rect()` prior to applying the
 * `1.5625x` logical-to-device `QPainter::scale()` transform, then blits all 256
 * character tiles from `ImportVICEDialog::atlasCache()`.
 */

#include "importvicecharsetwidget.h"

#include <cmath>

#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>

#include "importvicedialog.h"

namespace {

constexpr qreal kPixelSize = 1.5625;
constexpr int kColumns = 16;
constexpr int kRows = 16;
constexpr int kOffset = 0;

} // namespace

ImportVICECharsetWidget::ImportVICECharsetWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(
        static_cast<int>(std::lround(kPixelSize * static_cast<qreal>(kColumns * 8 + kOffset * 2))),
        static_cast<int>(std::lround(kPixelSize * static_cast<qreal>(kRows * 8 + kOffset * 2))));
}

void ImportVICECharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle before applying scale transform.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    if (_parentDialog == nullptr || _parentDialog->previewState() == nullptr) {
        return;
    }

    painter.scale(kPixelSize, kPixelSize);
    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    const TileAtlasCache& cache = _parentDialog->atlasCache();
    const QImage& atlas = cache.atlasImage();
    for (int x = 0; x < kColumns; ++x) {
        for (int y = 0; y < kRows; ++y) {
            const int tileIdx = x + y * kColumns;
            const QRectF target(
                static_cast<qreal>(x * 8 + kOffset),
                static_cast<qreal>(y * 8 + kOffset),
                8.0,
                8.0);
            painter.drawImage(target, atlas, QRectF(cache.sourceRectForTile(tileIdx)));
        }
    }

    if (_displayGrid) {
        QPen gridPen = painter.pen();
        gridPen.setColor(QColor(0, 128, 0));
        gridPen.setStyle(Qt::DashLine);
        gridPen.setWidthF(1.0 / kPixelSize);
        painter.setPen(gridPen);

        for (int y = 0; y <= kRows; ++y) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(y * 8 + kOffset)),
                QPointF(static_cast<qreal>(kColumns * 8 + kOffset), static_cast<qreal>(y * 8 + kOffset)));
        }

        for (int x = 0; x <= kColumns; ++x) {
            painter.drawLine(
                QPointF(static_cast<qreal>(x * 8 + kOffset), static_cast<qreal>(kOffset)),
                QPointF(static_cast<qreal>(x * 8 + kOffset), static_cast<qreal>(kRows * 8 + kOffset)));
        }
    }
}

void ImportVICECharsetWidget::setDisplayGrid(bool enabled)
{
    if (enabled != _displayGrid) {
        _displayGrid = enabled;
        update();
    }
}
