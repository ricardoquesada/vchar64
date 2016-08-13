/****************************************************************************
Copyright 2015, 2016 Ricardo Quesada

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

#include "importvicescreenramwidget.h"

#include <QPainter>
#include <QPaintEvent>

#include "importvicedialog.h"
#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "utils.h"
#include "state.h"

static const int PIXEL_SIZE = 1;
static const int COLUMNS = 40;
static const int ROWS = 25;
static const int OFFSET = 0;

ImportVICEScreenRAMWidget::ImportVICEScreenRAMWidget(QWidget *parent)
    : QWidget(parent)
    , _parentDialog(nullptr)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
                 PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}

//
// Overriden
//
void ImportVICEScreenRAMWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    if (!_parentDialog)
        return;

    painter.begin(this);
    painter.scale(PIXEL_SIZE, PIXEL_SIZE);

    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    State *state = _parentDialog->_tmpState;
    for (int y=0; y<ROWS; y++)
    {
        for (int x=0; x<COLUMNS; ++x)
        {
            auto tileIdx = state->getTileIndexFromMap(QPoint(x,y));
            QRectF target(x * 8, y * 8, 8, 8);
            painter.drawImage(target, * _parentDialog->_tileImages[tileIdx], _parentDialog->_tileImages[tileIdx]->rect());
        }
    }


    painter.end();
}
