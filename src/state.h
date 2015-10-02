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
    // only 256 chars at the time
    const static int CHAR_BUFFER_SIZE = 8 * 256;

    // Max Tile size: 8x8
    const static int MAX_TILE_WIDTH = 8;
    const static int MAX_TILE_HEIGHT = 8;

    enum {
        PEN_BACKGROUND,
        PEN_MULTICOLOR1,
        PEN_MULTICOLOR2,
        PEN_FOREGROUND,

        PEN_MAX
    };

    union Char {
        quint64 _char64;
        quint8 _char8[8];
    };

    struct TileProperties {
        QSize size;
        int interleaved;
    };

    /**
     * @brief The CopyRange struct
     */
    struct CopyRange {
        /** @brief offset in bytes */
        int offset;
        /** @brief blockSize in bytes */
        int blockSize;
        /** @brief how many bytes to skip before reaching the next block */
        int skip;
        /** how many blocks to copy */
        int count;
    };

    static State* getInstance();

    void reset();
    bool openFile(const QString& filename);
    bool saveProject(const QString& filename);
    bool exportRaw(const QString& filename);
    bool exportPRG(const QString& filename, quint16 address);
    // export is a defined keyword, so we use export_ instead
    bool export_();

    int getColorForPen(int pen) const {
        Q_ASSERT(pen >=0 && pen < PEN_MAX);
        return _penColors[pen];
    }

    // color should be 0 or 1 in normal mode
    // and 0, 1, 2 or 3 in multicolor mode
    void setColorForPen(int pen, int color);

    int getCurrentColor() const {
        return _penColors[_selectedPen];
    }

    void setSelectedPen(int pen) {
        Q_ASSERT(pen>=0 && pen<PEN_MAX);
        _selectedPen = pen;
    }

    int getSelectedPen() const {
        return _selectedPen;
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

    // is the state "dirty" ?
    bool isModified() const;

    QUndoStack* getUndoStack() const {
        return _undoStack;
    }

    //
    // chars buffer manipulation
    //
    int getCharIndexFromTileIndex(int tileIndex) const;
    int getTileIndexFromCharIndex(int charIndex) const;
    quint8* getCharAtIndex(int charIndex);

    // size-of-tile chars will be copied. bufferSize must be big enough
    void copyCharFromIndex(int tileIndex, quint8* buffer, int bufferSize);
    // size-of-tile chars will be copied. bufferSize must be big enough
    void copyCharToIndex(int tileIndex, quint8* buffer, int bufferSize);

    //
    // charset
    //
    quint8* getCharsetBuffer();
    quint8* getCopyCharsetBuffer();
    void resetCharsetBuffer();
    /**
     * @brief updateCharset replace current charset with a new one.
     * Emit `charsetUpdated()` and `contentsChanged()` signals
     * @param buffer that contains 256 chars
     */
    void updateCharset(quint8* buffer);

    /**
      @brief copy a range of bytes to the copy buffer
      @param copyRange the range of bytes to copy
     */
    void copy(const CopyRange& copyRange);

    /**
     * @brief paste paste previously copied range starting from charIndex
     * @param charIndex offset in bytes
     * @param copyRange range to paste
     * @param charsetBuffer buffer that has the 256 chars
     */
    void paste(int charIndex, const CopyRange& copyRange, const quint8* charsetBuffer);
    const CopyRange& getCopyRange() const;
    /**
     * @brief setCopyRange update the CopyRange ivar. Useful for undo/redo command
     * @param range the Copy Range
     */
    void setCopyRange(const CopyRange& range);

    //
    // tile manipulation
    //
    void tileInvert(int tileIndex);
    void tileClear(int tileIndex);
    void tileFlipHorizontally(int tileIndex);
    void tileFlipVertically(int tileIndex);
    void tileRotate(int tileIndex);
    void tileShiftLeft(int tileIndex);
    void tileShiftRight(int tileIndex);
    void tileShiftUp(int tileIndex);
    void tileShiftDown(int tileIndex);
    void tileSetPen(int tileIndex, const QPoint& position, int pen);

    /** Returns the used pen for a certain bit of a tile.
        returns 0 or 1 in normal mode
        returns 0, 1, 2 or 3 in multicolor mode
    */
    int tileGetPen(int tileIndex, const QPoint& position);


signals:
    // file loaded, or new project
    void fileLoaded();

    // when tile size or interleaved changes
    void tilePropertiesUpdated();

    // when one byte in a part of the tile changes
    void byteUpdated(int);

    // when the whole tile changes
    void tileUpdated(int);

    // when the charbuffer was updated. Probably due to a copy & paste operation
    void charsetUpdated();

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

    quint8 _charset[State::CHAR_BUFFER_SIZE];

    bool _multiColor;

    int _selectedPen;
    int _penColors[PEN_MAX];

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

    /** @brief buffer that will have a copy of all the chars */
    quint8 _copyCharset[CHAR_BUFFER_SIZE];
    CopyRange _copyRange;

    QUndoStack* _undoStack;
};

