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

#include <unordered_map>

#include <QRect>
#include <QWidget>

#include "state.h"

class ImportKoalaBitmapWidget : public QWidget {
    Q_OBJECT

    friend class ImportKoalaDialog;

public:
    ImportKoalaBitmapWidget(QWidget* parent = nullptr);
    void loadKoala(const QString& koalaFilepath);
    void enableGrid(bool enabled);
    void parseKoala();

signals:
    void selectedRegionUpdated(const QRect& rect);

protected:
    void paintEvent(QPaintEvent* event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent* event) Q_DECL_OVERRIDE;

    void resetColors();
    void resetOffset();
    void toFrameBuffer();
    void findUniqueCells();
    QRect getSelectedRegion() const;

    void reportResults();
    void strategyD02xAbove8();
    void strategyD02xAny();

#pragma pack(push)
#pragma pack(1)
    struct koala {
        quint8 addr[2]; // load address. ignore
        quint8 bitmap[40 * 25 * 8];
        quint8 screenRAM[40 * 25];
        quint8 colorRAM[40 * 25];
        quint8 backgroundColor;
    };
#pragma pack(pop)

    struct koala _koala;

    // one byte per pixel, although only the
    // 4 LSB will be used. Bits 7-4 are ignored
    // Bits 0-3 contains the C64 colors
    quint8 _framebuffer[160 * 200];

    // key: color sequence
    // data: positions in screen ram
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> _uniqueCells;

    // first: count, second: color index
    std::vector<std::pair<int, int>> _colorsUsed;

    // colors to be used for d021, d022 and d023
    quint8 _d02xColors[3];

    bool _displayGrid;

    // selecting ivars
    bool _selecting;
    QSize _selectingSize;
    QPoint _cursorPos;
};
