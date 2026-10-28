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
 * @file importvicescreenramwidget.cpp
 * @brief `TileAtlasCache`-backed `40x25` Screen RAM preview painter for VICE `.vsf` import.
 *
 * Clears the exposed device-coordinate `event->rect()` before applying any logical
 * scale transform, then blits each `(x, y)` map cell from `ImportVICEDialog::atlasCache()`.
 */

#include "importvicescreenramwidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>

#include "importvicedialog.h"
#include "state.h"

namespace {

constexpr int kPixelSize = 1;
constexpr int kColumns = 40;
constexpr int kRows = 25;
constexpr int kOffset = 0;

} // namespace

ImportVICEScreenRAMWidget::ImportVICEScreenRAMWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(
        kPixelSize * kColumns * 8 + kOffset * 2,
        kPixelSize * kRows * 8 + kOffset * 2);
}

void ImportVICEScreenRAMWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Fill the exposed device-coordinate rectangle before applying scale transform.
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    if (_parentDialog == nullptr) {
        return;
    }

    const State* state = _parentDialog->previewState();
    if (state == nullptr) {
        return;
    }

    painter.scale(kPixelSize, kPixelSize);
    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    const TileAtlasCache& cache = _parentDialog->atlasCache();
    const QImage& atlas = cache.atlasImage();
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const int tileIdx = state->getTileIndexFromMap(QPoint(x, y));
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
        gridPen.setWidthF(1.0 / static_cast<qreal>(kPixelSize));
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

void ImportVICEScreenRAMWidget::setDisplayGrid(bool enabled)
{
    if (enabled != _displayGrid) {
        _displayGrid = enabled;
        update();
    }
}
