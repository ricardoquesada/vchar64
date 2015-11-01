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
        enum {
            CHARS,
            TILES
        };

        /** @brief offset in bytes */
        int offset;
        /** @brief blockSize in bytes */
        int blockSize;
        /** @brief how many bytes to skip before reaching the next block */
        int skip;
        /** how many blocks to copy */
        int count;
        /** what to copy: chars or tiles */
        int type;
    };

    static State* getInstance();

    /**
     * @brief reset resets the charsets. emits fileLoaded();
     */
    void reset();
    /**
     * @brief openFile imports a file. emits fileLoaded();
     * @param filename
     * @return
     */
    bool openFile(const QString& filename);
    bool saveProject(const QString& filename);
    bool exportRaw(const QString& filename);
    bool exportPRG(const QString& filename, quint16 address);
    // export is a defined keyword, so we use export_ instead
    bool export_();

    /**
     * @brief importCharset sets a new charset. emits fileLoaded();
     * @param filename filename to be associated with the import. No files are actually loaded
     * @param charset pointer to the charset
     * @param charsetSize size of the charset
     */
    void importCharset(const QString &filename, const quint8* charset, int charsetSize);

    /**
     * @brief getColorForPen
     * @param pen PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1 or PEN_MULTICOLOR2
     * @return the color being used by the pen
     */
    int getColorForPen(int pen) const {
        Q_ASSERT(pen >=0 && pen < PEN_MAX);
        return _penColors[pen];
    }

    /**
     * @brief setColorForPen set a color for a pen
     * @param pen PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1, PEN_MULTICOLOR2
     * @param color a color between 0 and 15
     */
    void setColorForPen(int pen, int color);

    /**
     * @brief getCurrentColor
     * @return the color being used by the selected pen
     */
    int getCurrentColor() const;

    /**
     * @brief setSelectedPen sets pen as the selected one
     * @param pen PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1, PEN_MULTICOLOR2
     */
    void setSelectedPen(int pen);

    /**
     * @brief getSelectedPen
     * @return the selected pen: PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1, PEN_MULTICOLOR2
     */
    int getSelectedPen() const;

    /**
     * @brief setMulticolorMode enable/display multicolor mode
     * @param enabled whether or not multicolor mode should be enabled
     */
    void setMulticolorMode(bool enabled);

    /**
     * @brief isMulticolorMode
     * @return whether or not multicolor mode is enabled
     */
    bool isMulticolorMode() const;

    /**
     * @brief shouldBeDisplayedInMulticolor whether or not the char should be displayed as multicolor.
     * Even if multicolor mode is enabled, if Foreground color is <= 7, then char should not be
     * displayed in multicolor mode
     * @return whether or not the char/tile should be displayed in multicolor mode
     */
    bool shouldBeDisplayedInMulticolor() const;

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
    /**
     * @brief setTileProperties changes the tile properties
     * emit tilePropertiesUpdated();
     * @param properties the new TileProperties struct
     */
    void setTileProperties(const TileProperties& properties);
    /**
     * @brief getTileProperties
     * @return the TileProperties being used
     */
    TileProperties getTileProperties() const;

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
    void copyTileFromIndex(int tileIndex, quint8* buffer, int bufferSize);
    // size-of-tile chars will be copied. bufferSize must be big enough
    void copyTileToIndex(int tileIndex, quint8* buffer, int bufferSize);

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


    /**
     * @brief setErrorMessage Sets the error message
     * @param errorMesg
     */
    void setErrorMessage(const QString& errorMesg);

    /**
     * @brief getErrorMessage returns the current error message
     * @return
     */
    const QString& getErrorMessage() const;

signals:
    // file loaded, or new project
    void fileLoaded();

    // when tile size or interleaved changes
    void tilePropertiesUpdated();

    // when one byte in a part of the tile changes
    void byteUpdated(int);

    // when a range of bytes in the charset changes (e.g. due to a paste)
    void bytesUpdated(int pos, int count);

    // when the whole tile changes
    void tileUpdated(int);

    // when the charbuffer was updated. Probably due to a copy & paste operation
    void charsetUpdated();

    // a color new color for a pen was selected
    void colorPropertiesUpdated(int);

    /**
     * @brief selectedPenChaged a new pen is selected
     */
    void selectedPenChaged(int);

    // multicolor mode was toggled
    void multicolorModeToggled(bool);

    // when the state is dirty, or non-dirty.
    // only emmited when the dirty-state changes
    void contentsChanged();

    /**
     * @brief charIndexUpdated emitted when the charIndex is updated. Could emit tileIndexUpdated(); as well
     */
    void charIndexUpdated(int);

    /**
     * @brief tileIndexUpdated emitted when the tileIndex is updated. Emits charIndexUpdated(); as well
     */
    void tileIndexUpdated(int);

    /**
     * @brief errorMessageSet signal emitted when a new error message is set
     * @param errorMsg
     */
    void errorMessageSet(const QString& errorMsg);

public slots:
    /**
     * @brief setCharIndex is called when a char is selected
     * @param charIndex Value between 0 and 255
     */
    void setCharIndex(int charIndex);
    /**
     * @brief setTileIndex is called when a tiled is selected
     * @param tileIndex Value between 0 and tileMax
     */
    void setTileIndex(int tileIndex);


protected:    
    State();
    virtual ~State();

    Char getCharFromTile(int tileIndex, int x, int y) const;
    void setCharForTile(int tileIndex, int x, int y, const Char& chr);

    void _setCharIndex(int charIndex);
    void _setTileIndex(int tileIndex);

    void _tileShiftLeft(int tileIndex);
    void _tileShiftRight(int tileIndex);

    int _totalChars;

    quint8 _charset[State::CHAR_BUFFER_SIZE];

    bool _multicolorMode;

    int _selectedPen;
    int _penColors[PEN_MAX];

    TileProperties _tileProperties;

    /**
     * @brief _charIndex selected char index from the charset. Value from 0 to 255
     */
    int _charIndex;

    /**
     * @brief _tileIndex selected tile index from the tileset. Value from 0 up to 255
     */
    int _tileIndex;

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

    QString _lastError;
};

