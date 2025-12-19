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

#include <array>
#include <vector>

#include <QWidget>

// draws a screen (40x25) with a charset, screen ram, color ram and multicolors
class ImportKoalaCharsetWidget : public QWidget {
    Q_OBJECT

    friend class ImportKoalaDialog;

public:
    explicit ImportKoalaCharsetWidget(QWidget* parent = nullptr);

    void paintEvent(QPaintEvent* event) override;

    void populateScreenAndColorRAM(const std::vector<std::pair<int, int>>& coords, quint8 screenRAM, quint8 colorRAM);
    void setCharset(int charIndex, const quint8* chardef);
    void enableGrid(bool enabled);

    void clean();

signals:

public slots:

private:
    quint8 _colorRAMForChars[256];
    quint8 _charset[256 * 8];
    quint8 _screenRAM[40 * 25];

    quint8 _d02x[3];
    bool _displayGrid;
};
