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

#include <QObject>
#include <QString>
#include <QSize>

#include <string>
#include "stateimport.h"

class State : public QObject
{
    Q_OBJECT

    friend class StateImport;

public:
    union Char {
        quint64 _char64;
        quint8 _char8[8];
    };

    struct TileProperties {
        QSize size;
        int interleaved;
    };

    // only 256 chars at the time
    const static int CHAR_BUFFER_SIZE = 8 * 256;

    static State* getInstance();

    void reset();
    bool openFile(const QString& filename);
    bool saveProject(const QString& filename);
    bool exportRaw(const QString& filename);
    bool exportPRG(const QString& filename, quint16 address);
    // export is a defined keyword, so we use export_ instead
    bool export_();

    quint8* getChars();

    quint8* getCharAtIndex(int charIndex) {
        Q_ASSERT(charIndex>=0 && charIndex<256 && "Invalid index");
        return &_chars[charIndex*8];
    }

    int getCharColor(int charIndex, int bitIndex) const;
    void setCharColor(int charIndex, int bitIndex, int colorIndex);

    int getColorAtIndex(int index) const {
        Q_ASSERT(index >=0 && index < 4);
        return _colors[index];
    }

    void setColorAtIndex(int colorIndex, int color) {
        Q_ASSERT(colorIndex >=0 && colorIndex < 4);
        Q_ASSERT(color >=0 && color < 16);
        _colors[colorIndex] = color;
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
        emit colorPropertiesUpdated();
    }

    bool isMultiColor() const {
        return _multiColor;
    }

    QString getLoadedFilename() const {
        return _loadedFilename;
    }

    QString getSavedFilename() const {
        return _savedFilename;
    }

    QString getExportedFilename() const {
        return _exportedFilename;
    }

    // tile properties
    void setTileProperties(const TileProperties& properties);
    TileProperties getTileProperties() const {
        return _tileProperties;
    }

    //
    int getCharIndexFromTileIndex(int tileIndex) const;
    int getTileIndexFromCharIndex(int charIndex) const;

    quint8* getCharsBuffer();
    void resetCharsBuffer();

    //
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

signals:
    // file loaded, or new project
    void fileLoaded();

    // when tile size or interleaved changes
    void tilePropertiesUpdated();

    // at least one pixel changes in the tile
    void tileUpdated(int);

    // multi-color / hires or new colors
    void colorPropertiesUpdated();

public slots:

protected:    
    State();
    virtual ~State();

    Char getCharFromTile(int tileIndex, int x, int y) const;
    void setCharForTile(int tileIndex, int x, int y, const Char& chr);


    int _totalChars;

    quint8 _chars[State::CHAR_BUFFER_SIZE];

    bool _multiColor;

    int _selectedColorIndex;
    int _colors[4];

    TileProperties _tileProperties;

    // filename of the loaded file
    // each time a new file is loaded, "exported" and "saved" are reset
    QString _loadedFilename;

    // filename of the saved file (.vcharproj)
    QString _savedFilename;

    // filename of the exported file (.raw, .prg)
    QString _exportedFilename;
    // -1 for "raw", otherwise it will be a "prg" and the value will have the address
    int _exportedAddress;

    // max size of tile: 8 x 8
    quint8 _copyTile[8 * 8 * 8];
};

