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
#include <QUndoStack>
#include <QPoint>

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

    // Max Tile size: 8x8
    const static int MAX_TILE_WIDTH = 8;
    const static int MAX_TILE_HEIGHT = 8;

    static State* getInstance();

    void reset();
    bool openFile(const QString& filename);
    bool saveProject(const QString& filename);
    bool exportRaw(const QString& filename);
    bool exportPRG(const QString& filename, quint16 address);
    // export is a defined keyword, so we use export_ instead
    bool export_();

    // returns 0 or 1 in normal mode
    // returns 0, 1, 2 or 3 in multicolor mode
    int getTileColorAt(int tileIndex, const QPoint& position);

    int getColorAtIndex(int index) const {
        Q_ASSERT(index >=0 && index < 4);
        return _colors[index];
    }

    // color should be 0 or 1 in normal mode
    // and 0, 1, 2 or 3 in multicolor mode
    void setColorAtIndex(int colorIndex, int color);

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

    void setMultiColor(bool enabled);

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
    // chars bufffer manipulation
    //
    int getCharIndexFromTileIndex(int tileIndex) const;
    int getTileIndexFromCharIndex(int charIndex) const;


    quint8* getCharAtIndex(int charIndex) {
        Q_ASSERT(charIndex>=0 && charIndex<256 && "Invalid index");
        return &_chars[charIndex*8];
    }

    quint8* getCharsBuffer();
    void resetCharsBuffer();

    // size-of-tile chars will be copied. bufferSize must be big enough
    void copyCharFromIndex(int tileIndex, quint8* buffer, int bufferSize);
    // size-of-tile chars will be copied. bufferSize must be big enough
    void copyCharToIndex(int tileIndex, quint8* buffer, int bufferSize);

    // is the state "dirty" ?
    bool isModified() const;

    QUndoStack* getUndoStack() const {
        return _undoStack;
    }

    //
    // tile manipulation
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
    void tilePaint(int tileIndex, const QPoint& position, int colorIndex);

signals:
    // file loaded, or new project
    void fileLoaded();

    // when tile size or interleaved changes
    void tilePropertiesUpdated();

    // when one byte in a part of the tile changes
    void byteUpdated(int);

    // when the whole tile changes
    void tileUpdated(int);    

    // multi-color / hires or new colors
    void colorPropertiesUpdated();

    // when the state is dirty, or non-dirty.
    // only emmited when the dirty-state changes
    void contentsChanged();

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
    quint8 _copyTile[MAX_TILE_HEIGHT * MAX_TILE_WIDTH * 8];

    QUndoStack* _undoStack;
};

