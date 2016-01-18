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

/**
 * @brief The BigCharWidget class
 * It is the "Document" class. It contains a `State`
 * object, which could be seen as the Document object.
 * `BigCharWidget` could be seen a the "view" of the `State`.
 */
class BigCharWidget : public QWidget
{
    Q_OBJECT

public:
    BigCharWidget(State *state, QWidget *parent=nullptr);
    virtual ~BigCharWidget();

    int getTileIndex() const;
    QSize getTileSize() const;
    State* getState() const;

public slots:
    void onTileIndexUpdated(int tileIndex);
    void onTilePropertiesUpdated();
    void onTileUpdated(int tileIndex);
    void updateColor();
    void onMulticolorModeToggled(bool state);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

    void paintChar(QPainter& painter, const QPen& pen, quint8* charPtr, const QPoint& tileToDraw);
    void paintSeparators(QPainter &painter);
    void paintFocus(QPainter &painter);

    bool maybeSave();

    void paintPixel(int x, int y, int pen);
    void cyclePixel(int x, int y);

    int _tileIndex;
    int _charIndex;
    QPoint _cursorPos;

    State::TileProperties _tileProperties;
    State* _state;              // strong ref

    QSize _pixelSize;
    bool _commandMergeable;

};
