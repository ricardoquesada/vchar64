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

#include <QString>
#include <QSize>

#include <string>

class State
{
public:
    // only 256 chars at the time
    const static int CHAR_BUFFER_SIZE = 8 * 256;

    static State* getInstance();

    void reset();

    bool openFile(const QString &filename);
    bool save(const QString &filename);
    bool exportRaw(const QString& filename);
    bool exportPRG(const QString& filename, u_int16_t address);

    u_int8_t* getCharAtIndex(int index) {
        Q_ASSERT(index>=0 && index<256 && "Invalid index");
        return &_chars[index*8];
    }

    void setCharColor(int charIndex, int bitIndex, int colorIndex);
    int getCharColor(int charIndex, int bitIndex) const;

    void resetCharsBuffer();
    u_int8_t* getCharsBuffer();

    int getColorAtIndex(int index) const {
        Q_ASSERT(index >=0 && index < 4);
        return _colors[index];
    }

    void setColorAtIndex(int index, int color) {
        Q_ASSERT(index >=0 && index < 4);
        Q_ASSERT(color >=0 && color < 16);
        _colors[index] = color;
    }

    int getCurrentColor() const {
        return _colors[_selectedColorIndex];
    }

    void setSelectedColorIndex(int index) {
        Q_ASSERT(index>=0 && index<4);
        _selectedColorIndex = index;
    }

    int getSelectedColorIndex() const {
        return _selectedColorIndex;
    }

    void setMultiColor(bool enabled) {
        _multiColor = enabled;
    }

    bool isMultiColor() const {
        return _multiColor;
    }

    QString getFilename() const {
        return _filename;
    }

    void setTileSize(const QSize& tileSize);
    QSize getTileSize() const {
        return _tileSize;
    }

    void setCharInterleaved(int interleaved);
    int getCharInterleaved() const {
        return _charInterleaved;
    }

    int charIndexFromTileIndex(int tileIndex) const;
    int tileIndexFromCharIndex(int charIndex) const;

    void tileCopy(int tileIndex);
    void tilePaste(int tileIndex);
    void tileInvert(int tileIndex);
    void tileClear(int tileIndex);
    void tileFlipHorizontally(int tileIndex);
    void tileFlipVertically(int tileIndex);
    void tileRotate(int tileIndex);
    void tileShiftLeft(int tileIndex);
    void tileShiftRight(int tileIndex);
    void tileShiftUp(int tileIndex);
    void tileShiftDown(int tileIndex);

protected:
    State();
    ~State();

    int _totalChars;

    u_int8_t _chars[State::CHAR_BUFFER_SIZE];

    bool _multiColor;

    int _selectedColorIndex;
    int _colors[4];
    QSize _tileSize;
    int _charInterleaved;

    QString _filename;

    // max size of tile: 5 x 5
    u_int8_t _copyTile[8 * 5 * 5];
};

