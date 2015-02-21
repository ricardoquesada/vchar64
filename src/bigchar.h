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

#pragma once

#include <QWidget>
#include <QPoint>
#include <QSize>

#include <stdint.h>
#include "state.h"

//! [0]

class BigChar : public QWidget
{
    Q_OBJECT

public:
    BigChar(QWidget *parent);
    int getTileIndex() const {
        return _tileIndex;
    }

    QSize getTileSize() const {
        return _tileProperties.size;
    }

public slots:
    void setTileIndex(int tileIndex);
    void updateTileProperties();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;

    void paintChar(QPainter& painter, const QPen& pen, quint8* charPtr, const QPoint& tileToDraw);
    void paintSeparators(QPainter &painter);
    void paintFocus(QPainter &painter);

    void paintPixel(int x, int y);

    int _tileIndex;
    int _charIndex;
    QPoint _cursorPos;

    State::TileProperties _tileProperties;

    QSize _pixelSize;
};
//! [0]

