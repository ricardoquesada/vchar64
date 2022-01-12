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

#pragma once

#include "state.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

// draws the map of the state (screen RAM + color RAM + charset)
class MapWidget : public QWidget {
    Q_OBJECT

public:
    enum MapMode {
        PAINT_MODE,
        SELECT_MODE,
        FILL_MODE
    };

    explicit MapWidget(QWidget* parent = nullptr);

    void enableGrid(bool enabled);
    void setMode(MapMode mode);
    void getSelectionRange(State::CopyRange* copyRange) const;
    int getCursorPos() const;
    void setZoomLevel(int zoomLevel);

signals:

public slots:
    void onTilePropertiesUpdated();
    void onMapSizeUpdated();
    void onMapContentUpdated();
    void onMulticolorModeToggled(bool state);
    void onColorPropertiesUpdated(int pen);
    void onTileUpdated(int tileIndex);
    void onCharsetUpdated();
    void onFileLoaded();

protected:
    void paintEvent(QPaintEvent* event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    QSize sizeHint() const Q_DECL_OVERRIDE;

    void updateTileImages();

private:
    bool _displayGrid;
    QSize _sizeHint;
    bool _selecting;
    QSize _selectingSize;
    QPoint _cursorPos;
    QSize _mapSize;
    QSize _tileSize;
    MapMode _mode;
    bool _commandMergeable;
    qreal _zoomLevel;
    int _altValue; // value entered pressing ALT + number

    // For gain speed, each tile will be pre-renderer in a QImage
    // a QImages will be renderer
    QImage* _tileImages[256];
};
