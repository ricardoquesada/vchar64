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

    void paintEvent(QPaintEvent* event) Q_DECL_OVERRIDE;

    void populateScreenAndColorRAM(const std::vector<std::pair<int, int>>& coords, quint8 screenRAM, quint8 colorRAM);
    void setCharset(int charIndex, const quint8* chardef);
    void enableGrid(bool enabled);

    void clean();

signals:

public slots:

private:
    std::array<quint8, 256> _colorRAMForChars;
    std::array<quint8, 256 * 8> _charset;
    std::vector<quint8> _screenRAM;

    quint8 _d02x[3];
    bool _displayGrid;
};
