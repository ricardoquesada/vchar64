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

//! [0]

class BigChar : public QWidget
{
    Q_OBJECT

public:
    BigChar(QWidget *parent);
    int getIndex() const {
        return _charIndex;
    }

    QSize getTileSize() const {
        return _tileSize;
    }

public slots:
    void setIndex(int tileIndex);
    void updateTileProperties();

signals:
    void indexChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

    void paintChar(QPainter& painter, const QPen& pen, u_int8_t* charPtr, const QPoint& tileToDraw);
    void paintPixel(int x, int y);

    int _charIndex;
    QPoint _cursorPos;

    // in chars (bytes), not bits
    QSize _tileSize;
    int _charInterleaved;
    QSize _pixelSize;
};
//! [0]

