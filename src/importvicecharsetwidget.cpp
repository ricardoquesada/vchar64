/****************************************************************************
Copyright 2015 Ricardo Quesada

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

#include "importvicecharsetwidget.h"

#include <QPaintEvent>
#include <QPainter>

#include "importvicedialog.h"
#include "mainwindow.h"
#include "palette.h"
#include "state.h"

constexpr float PIXEL_SIZE = 1.5625f;
constexpr int COLUMNS = 16;
constexpr int ROWS = 16;
constexpr int OFFSET = 0;

ImportVICECharsetWidget::ImportVICECharsetWidget(QWidget* parent)
    : QWidget(parent)
    , _parentDialog(nullptr)
    , _displayGrid(false)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
        PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}

//
// Overriden
//
void ImportVICECharsetWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter;

    if (!_parentDialog)
        return;

    painter.begin(this);
    painter.scale(PIXEL_SIZE, PIXEL_SIZE);

    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0, 0, 0));
    painter.setPen(Qt::NoPen);

    for (int x = 0; x < COLUMNS; x++) {
        for (int y = 0; y < ROWS; y++) {

            int tileIdx = x + y * COLUMNS;
            QRectF target(x * 8, y * 8, 8, 8);
            painter.drawImage(target,
                *_parentDialog->_tileImages[tileIdx],
                _parentDialog->_tileImages[tileIdx]->rect());
        }
    }

    if (_displayGrid) {
        auto pen = painter.pen();
        pen.setColor(QColor(0, 128, 0));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0 / PIXEL_SIZE);
        painter.setPen(pen);

        for (int y = 0; y <= ROWS; ++y)
            painter.drawLine(QPointF(0 + OFFSET, y * 8 + OFFSET),
                QPointF(COLUMNS * 8 + OFFSET, y * 8 + OFFSET));

        for (int x = 0; x <= COLUMNS; ++x)
            painter.drawLine(QPointF(x * 8 + OFFSET, OFFSET),
                QPointF(x * 8 + OFFSET, ROWS * 8 + OFFSET));
    }

    painter.end();
}

void ImportVICECharsetWidget::setDisplayGrid(bool enabled)
{
    if (enabled != _displayGrid) {
        _displayGrid = enabled;
        update();
    }
}
