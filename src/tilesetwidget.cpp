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

#include "TilesetWidget.h"

#include <QPainter>
#include <QPaintEvent>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;

TilesetWidget::TilesetWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
                 PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}


void TilesetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();
}

void TilesetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();
}

void TilesetWidget::keyPressEvent(QKeyEvent *event)
{
    event->accept();
}

void TilesetWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}
