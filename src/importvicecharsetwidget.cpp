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

// TileAtlasCache-backed 16x16 charset preview painter for VICE .vsf snapshot import.
//
// Clears the exposed device-coordinate event->rect() prior to applying the 1.5625x
// logical-to-device QPainter::scale() transform, then blits all 256 character tiles
// from ImportVICEDialog::atlasCache().

#include "importvicecharsetwidget.h"

#include <cmath>

#include <QColor>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include "core/tileatlascache.h"
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

void ImportVICECharsetWidget::setDisplayGrid(bool enabled)
{
    if (enabled != _displayGrid) {
        _displayGrid = enabled;
        update();
    }
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

    const TileAtlasCache& atlas_cache = _parentDialog->atlasCache();
    const QImage& atlas = atlas_cache.atlasImage();
    for (int col = 0; col < kColumns; ++col) {
        for (int row = 0; row < kRows; ++row) {
            const int tile_index = col + row * kColumns;
            const QRectF dest_rect(
                static_cast<qreal>(col * 8 + kOffset),
                static_cast<qreal>(row * 8 + kOffset),
                8.0,
                8.0);
            painter.drawImage(dest_rect, atlas, QRectF(atlas_cache.sourceRectForTile(tile_index)));
        }
    }

    if (_displayGrid) {
        QPen grid_pen = painter.pen();
        grid_pen.setColor(QColor(0, 128, 0));
        grid_pen.setStyle(Qt::DashLine);
        grid_pen.setWidthF(1.0 / kPixelSize);
        painter.setPen(grid_pen);

        for (int row = 0; row <= kRows; ++row) {
            painter.drawLine(
                QPointF(static_cast<qreal>(kOffset), static_cast<qreal>(row * 8 + kOffset)),
                QPointF(static_cast<qreal>(kColumns * 8 + kOffset), static_cast<qreal>(row * 8 + kOffset)));
        }

        for (int col = 0; col <= kColumns; ++col) {
            painter.drawLine(
                QPointF(static_cast<qreal>(col * 8 + kOffset), static_cast<qreal>(kOffset)),
                QPointF(static_cast<qreal>(col * 8 + kOffset), static_cast<qreal>(kRows * 8 + kOffset)));
        }
    }
}
