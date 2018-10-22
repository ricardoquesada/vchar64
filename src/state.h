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

class BigCharWidget;

class State : public QObject
{
    Q_OBJECT

    friend class StateImport;
    friend class StateExport;

    friend class BigCharWidget;

    friend class ImportVICEDialog;

    // yep, all the Commands are friend of State
    // this is because State invokes the commands
    // and then the Commands calls the protected methods
    // that actually modify the State without calling the Commands
    // A Command must never trigger another Command
    friend class PaintTileCommand;
    friend class PasteCommand;
    friend class CutCommand;
    friend class FlipTileHCommand;
    friend class FlipTileVCommand;
    friend class ShiftDownTileCommand;
    friend class ShiftUpTileCommand;
    friend class ShiftLeftTileCommand;
    friend class ShiftRightTileCommand;
    friend class RotateTileCommand;
    friend class InvertTileCommand;
    friend class ClearTileCommand;
    friend class SetMulticolorModeCommand;
    friend class SetTilePropertiesCommand;
    friend class SetExportPropertiesCommand;
    friend class SetMapSizeCommand;
    friend class SetColorCommand;
    friend class SetForegroundColorMode;
    friend class ClearMapCommand;
    friend class PaintMapCommand;
    friend class FillMapCommand;

public:
    // only 256 chars at the time
    const static int CHAR_BUFFER_SIZE = 8 * 256;

    // char attributes: color (4-bit LSB)
    const static int TILE_COLORS_BUFFER_SIZE = 256;

    // Max Tile size: 8x8
    const static int MAX_TILE_WIDTH = 8;
    const static int MAX_TILE_HEIGHT = 8;

    enum Pen {
        PEN_BACKGROUND,     /* $d021 */
        PEN_MULTICOLOR1,    /* $d022 */
        PEN_MULTICOLOR2,    /* $d023 */
        PEN_FOREGROUND,     /* color RAM: $d800-... */

        PEN_MAX
    };

    enum ForegroundColorMode {
        FOREGROUND_COLOR_GLOBAL,
        FOREGROUND_COLOR_PER_TILE
    };

    // what to export
    enum ExportFeature {
        EXPORT_FEATURE_NONE = 0,
        EXPORT_FEATURE_CHARSET = 1 << 0,
        EXPORT_FEATURE_MAP = 1 << 1,
        EXPORT_FEATURE_COLORS = 1 << 2,

        EXPORT_FEATURE_ALL = (EXPORT_FEATURE_CHARSET | EXPORT_FEATURE_MAP | EXPORT_FEATURE_COLORS)
    };

    // format to export
    enum ExportFormat {
        EXPORT_FORMAT_RAW,
        EXPORT_FORMAT_PRG,
        EXPORT_FORMAT_ASM
    };

    union Char {
        quint64 _char64;
        quint8 _char8[8];
    };

    struct TileProperties {
        QSize size;
        int interleaved;
    };

    struct ExportProperties {
        quint16 addresses[3];   // 0: CHARSET, 1:MAP, 2:COLORS
        quint8 format;          // EXPORT_FORMAT
        quint8 features;        // EXPORT_FEATURE
    };

    /**
     * @brief The CopyRange struct
     */
    struct CopyRange {
        enum BufferType{
            CHARS,
            TILES,
            MAP
        };

        /** @brief offset in chars or tiles in the buffer */
        int offset;
        /** @brief blockSize in chars or tiles */
        int blockSize;
        /** @brief how many chars or tiles to skip before reaching the next block */
        int skip;
        /** how many blocks to copy */
        int count;
        /** what to copy: chars or tiles */
        BufferType type;
        /** tileProperties, only needed when type==TILES. */
        TileProperties tileProperties;
        /** size of the buffer appended */
        int bufferSize;
    };

    /**
     * @brief State the Target constructor
     * @param filename the loaded name. Used when save/export is called
     * @param charset charset to use or nullptr
     * @param tileColors tileColors to use or nullptr
     * @param map map to use or nullPtr
     * @param mapSize map size
     */
    State(const QString& filename, quint8* charset, quint8* tileColors, quint8* map, const QSize &mapSize);
    /* delegating constructors */
    State(const QString& filename);
    State();

    /**
     * @brief ~State destructor
     */
    virtual ~State();

    /**
     * @brief Copy the state from orig to self.
     * CopyConstructor doesn't work since QObject doesn't have a public one.
     * @param state the original state to copy from
     */
    void copyState(const State& state);

    /**
     * @brief reset resets the charsets. emits fileLoaded();
     */
    void reset();

    /**
     * @brief refresh resends all the signals about the current state
     * so that consumers can update its views
     */
    void refresh();
    /**
     * @brief openFile imports a file. emits fileLoaded();
     * @param filename
     * @return
     */
    bool openFile(const QString& filename);
    bool saveProject(const QString& filename);
    bool exportRaw(const QString& filename, const ExportProperties &properties);
    bool exportPRG(const QString& filename, const ExportProperties &properties);
    bool exportAsm(const QString& filename, const ExportProperties &properties);
    // export is a defined keyword, so we use export_ instead
    bool export_();

    /**
     * @brief emitNewState hackish way to notify that a new state has been created
     * FIXME: This is a refactoring. BigCharWidget should emit this.
     */
    void emitNewState();

    /**
     * @brief getColorForPen
     * @param pen PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1 or PEN_MULTICOLOR2
     * @param tileIdx the tile to obtaion the color from, in case the color is associated to the tile
     * @return the color being used by the pen
     */
    int getColorForPen(int pen, int tileIdx) const;
    int getColorForPen(int pen) const;

    /**
     * @brief setColorForPen set a color for a pen
     * @param pen PEN_BACKGROUND, PEN_FOREGROUND, PEN_MULTICOLOR1, PEN_MULTICOLOR2
     * @param color a color between 0 and 15
     * @param tileIdx the tile to be modified in case the color is associated to the tile
     */
    void setColorForPen(int pen, int color, int tileIdx);
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
     * @brief setForegroundColorMode sets FOREGROUND_COLOR_GLOBAL or FOREGROUND_COLOR_PER_TILE.
     * In GLOBAL mode, all chars share the same "foreground" color.
     * In PER_TILE, each tile has its own color.
     * @param mode
     */
    void setForegroundColorMode(ForegroundColorMode mode);

    /**
     * @brief getForegroundColorMode
     * @return
     */
    ForegroundColorMode getForegroundColorMode() const;

    /**
     * @brief shouldBeDisplayedInMulticolor whether or not the char should be displayed as multicolor.
     * Even if multicolor mode is enabled, if Foreground color is <= 7, then char should not be
     * displayed in multicolor mode
     * @return whether or not the char/tile should be displayed in multicolor mode
     */
    bool shouldBeDisplayedInMulticolor() const;

    /**
     * @brief shouldBeDisplayedInMulticolor whether or not the char should be displayed as multicolor.
     * Even if multicolor mode is enabled, if Foreground color is <= 7, then tile should not be
     * displayed in multicolor mode
     * @return whether or not the tile should be displayed in multicolor mode
     */
    bool shouldBeDisplayedInMulticolor2(int tileIdx) const;

    QString getLoadedFilename() const {
        return _loadedFilename;
    }

    QString getSavedFilename() const {
        return _savedFilename;
    }

    QString getExportedFilename() const {
        return _exportedFilename;
    }

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

    /**
     * @brief setExportProperties changes the export properties
     * @param properties the new ExportProperties struct
     */
    void setExportProperties(const ExportProperties& properties);
    /**
     * @brief getExportProperties
     * @return the ExportProperties being used
     */
    ExportProperties getExportProperties() const;


    /**
     * @brief seMapSize changes the map size
     * @param mapSize
     */
    void setMapSize(const QSize& mapSize);

    /**
     * @brief getMapSize returns the current map size
     * @return
     */
    const QSize& getMapSize() const;

    /**
     * @brief getTileIndexFromMap returns the tiled index at mapPosition.
     * @param mapPosition position in the map
     * @return a tile index
     */
    int getTileIndexFromMap(const QPoint& mapCoord) const;

    /**
     * @brief mapFill fills a certain region of the map
     * @param coord coordinates of the map
     * @param tileIdx the tile to use as filler
     */
    void mapFill(const QPoint& coord, int tileIdx);

    /**
     * @brief mapPaint paints coord with a certain tile
     * @param coords the position of the map to paint
     * @param tileIdx the tile to use to paint
     * @param mergeable whether or not this paint can be merged with other mapPaint calls
     */
    void mapPaint(const QPoint& coord, int tileIdx, bool mergeable);

    /**
     * @brief mapPaint clears the map with a given tile
     * @param tileIdx the tile that will be used to clear the map
     */
    void mapClear(int tileIdx);

    // is the state "dirty" ?
    bool isModified() const;

    /**
     * @brief undo undoes the last change to the state
     */
    void undo();

    /**
     * @brief redo redoes the previous undo
     */
    void redo();

    /**
     * @brief getUndoStack returns the QUndoStack
     * @return returns the QUndoStack
     */
    QUndoStack* getUndoStack() const;


    /**
     * @brief clearUndoStack clears the UndoStack
     */
    void clearUndoStack();

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
    // charset, map, and related
    //
    const quint8* getCharsetBuffer() const;
    const quint8* getMapBuffer() const;
    const quint8* getTileColors() const;

    void resetCharsetBuffer();

    /**
     * @brief paste paste previously copied range starting from charIndex
     * @param offset offset in bytes
     * @param copyRange range to paste
     * @param origBuffer buffer that contains the data to paste
     */
    void paste(int offset, const CopyRange &copyRange, const quint8* origBuffer);

    /**
     * @brief cut cut the selected region. Region is filled with "zeroes"
     * @param copyRange range to cut
     */
    void cut(const CopyRange& copyRange);

    //
    // tile manipulation
    //
    void tilePaint(int tileIndex, const QPoint& point, int pen, bool mergeable=false);
    void tileInvert(int tileIndex);
    void tileClear(int tileIndex);
    void tileFlipHorizontally(int tileIndex);
    void tileFlipVertically(int tileIndex);
    void tileRotate(int tileIndex);
    void tileShiftLeft(int tileIndex);
    void tileShiftRight(int tileIndex);
    void tileShiftUp(int tileIndex);
    void tileShiftDown(int tileIndex);

    /** Returns the used pen for a certain position of a tile.
        returns 0 or 1 in normal mode
        returns 0, 1, 2 or 3 in multicolor mode
    */
    int tileGetPen(int tileIndex, const QPoint& position);

    /**
     * @brief getBigCharWidget returns the parent, the widget that owns the state
     * @return BigCharWidget
     */
    BigCharWidget* getBigCharWidget() const;

    /**
     * @brief getTileIndex returns the current tile index
     * @return the current Tile Index
     */
    int getTileIndex() const;

    /**
     * @brief getCharIndex returns the current char index
     * @return the current char Index
     */
    int getCharIndex() const;


signals:
    // file loaded, or new project
    void fileLoaded();

    // when tile size or interleaved changes
    void tilePropertiesUpdated();

    // when the map sizes changes
    void mapSizeUpdated();

    // when the map content is updated
    void mapContentUpdated();

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
    Char getCharFromTile(int tileIndex, int x, int y) const;
    void setCharForTile(int tileIndex, int x, int y, const Char& chr);

    void setupDefaultMap();

    void floodFillImpl(const QPoint& coord, int targetTile, int newTile);

    void _setCharIndex(int charIndex);
    void _setTileIndex(int tileIndex);

    void _pasteChars(int charIndex, const CopyRange& copyRange, const quint8* origBuffer);
    void _pasteTiles(int charIndex, const CopyRange& copyRange, const quint8* origBuffer);
    void _pasteMap(int charIndex, const CopyRange& copyRange, const quint8* origBuffer);

    void _paste(int charIndex, const CopyRange& copyRange, const quint8* origBuffer);
    void _tileInvert(int tileIndex);
    void _tileClear(int tileIndex);
    void _tileFlipHorizontally(int tileIndex);
    void _tileFlipVertically(int tileIndex);
    void _tileRotate(int tileIndex);
    void _tileShiftLeft(int tileIndex);
    void _tileShiftRight(int tileIndex);
    void _tileShiftUp(int tileIndex);
    void _tileShiftDown(int tileIndex);

    void _tileSetPen(int tileIndex, const QPoint& position, int pen);
    void _setMulticolorMode(bool enabled);
    void _setForegroundColorMode(ForegroundColorMode mode);
    void _setColorForPen(int pen, int color, int tileIdx);
    void _setTileProperties(const TileProperties& properties);
    void _setExportProperties(const ExportProperties &properties);

    void _setMapSize(const QSize& mapSize);
    void _setMap(const quint8* buffer, const QSize& mapSize);
    void _mapClear(int tileIdx);
    void _mapPaint(const QPoint& coord, int tileIdx);
    void _mapFill(const QPoint& coord, int tileIdx);


    int _totalChars;

    quint8 _charset[State::CHAR_BUFFER_SIZE];
    quint8 _tileColors[State::TILE_COLORS_BUFFER_SIZE];
    quint8* _map;
    QSize _mapSize;

    bool _multicolorMode;
    ForegroundColorMode _foregroundColorMode;

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

    // filename of the exported file (.raw, .prg, .s)
    QString _exportedFilename;

    // export properties
    ExportProperties _exportProperties;

    QUndoStack* _undoStack;

    BigCharWidget* _bigCharWidget;          // weak ref to parent
};

