/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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

/**
 * @file state.h
 * @brief Headless C64 / VIC-II document state model (`vchar64_core`).
 *
 * Encapsulates the three coupled asset buffers of a VChar64 project:
 *
 * - **Charset (`_charset`):** Fixed `2,048`-byte (`256 x 8`) 1bpp/2bpp glyph RAM.
 * - **Tile Colors (`_tileColors`):** Fixed `256`-byte 4-bit Color RAM (`$D800`
 *   nibble semantics where bit 3 gates per-tile Multicolor decoding).
 * - **Map (`_map`):** Dynamically sized (`1..4096 x 1..4096`) tile-index grid.
 *
 * All interactive edits route through `QUndoCommand` friends onto `_undoStack`,
 * while binary importers and preview dialogs mutate validated state directly via
 * `*Direct`, `assignImportedBuffers()`, and `replaceMap()`.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUndoStack>

#include "core/result.h"

class PaintTileCommand;
class PasteCommand;
class CutCommand;
class FlipTileHCommand;
class FlipTileVCommand;
class ShiftDownTileCommand;
class ShiftUpTileCommand;
class ShiftLeftTileCommand;
class ShiftRightTileCommand;
class RotateTileCommand;
class InvertTileCommand;
class ClearTileCommand;
class SetMulticolorModeCommand;
class SetTilePropertiesCommand;
class SetExportPropertiesCommand;
class SetMapSizeCommand;
class SetColorCommand;
class SetForegroundColorMode;
class ClearMapCommand;
class PaintMapCommand;
class FillMapCommand;

/**
 * @brief Headless VIC-II / C64 character, tile, color, and map document model.
 *
 * Resides in `vchar64_core` (`Qt6::Core` + `Qt6::Gui` only, zero `Qt6::Widgets`
 * coupling) so all unit tests execute directly against the production `State`
 * and `QUndoStack` under `QTEST_GUILESS_MAIN`.
 */
class State : public QObject {
    Q_OBJECT

    // QUndoCommand subclasses invoke protected direct-mutation helpers so that
    // undo/redo transitions never recursively push new commands onto _undoStack.
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
    /** @brief Size in bytes of one 256-glyph C64 character set (`256 * 8 = 2,048`). */
    static constexpr int CHAR_BUFFER_SIZE = 8 * 256;
    /** @brief Maximum number of addressable tiles and per-tile Color RAM entries (`256`). */
    static constexpr int TILE_COLORS_BUFFER_SIZE = 256;
    /** @brief Default 4-bit C64 foreground color index (`11` = Dark Grey, Multicolor bit 3 set). */
    static constexpr uint8_t TILE_COLORS_DEFAULT = 11U;

    /** @brief Maximum tile width in 8x8 characters (`8`). */
    static constexpr int MAX_TILE_WIDTH = 8;
    /** @brief Maximum tile height in 8x8 characters (`8`). */
    static constexpr int MAX_TILE_HEIGHT = 8;

    /** @brief Hard upper bound on map width or height (`4,096` cells) to prevent OOM allocations. */
    static constexpr int kMaxMapDim = 4096;
    /** @brief Maximum total map cell count (`4,096 * 4,096 = 16,777,216` bytes). */
    static constexpr size_t kMaxMapCells = 4096ULL * 4096ULL;
    /** @brief Maximum depth of `_undoStack` (`128` commands) to bound snapshot memory per document. */
    static constexpr int kDefaultUndoLimit = 128;

    using CharsetBuffer = std::array<uint8_t, CHAR_BUFFER_SIZE>;
    using TileColorsBuffer = std::array<uint8_t, TILE_COLORS_BUFFER_SIZE>;
    using MapBuffer = std::vector<uint8_t>;
    using CharGlyph = std::array<uint8_t, 8>;

    using charset_t = CharsetBuffer;
    using tileColors_t = TileColorsBuffer;
    using map_t = MapBuffer;

    /**
     * @brief Logical VIC-II color pens mapped to `$D021`–`$D023` and `$D800` Color RAM.
     *
     * In Multicolor mode, 2-bit pixel pairs `00`, `01`, `10`, and `11` map to
     * `PEN_BACKGROUND`, `PEN_MULTICOLOR1`, `PEN_MULTICOLOR2`, and `PEN_FOREGROUND`
     * (low 3 bits `0..7` of Color RAM), respectively.
     */
    enum Pen {
        PEN_BACKGROUND = 0, /* $d021 */
        PEN_MULTICOLOR1 = 1, /* $d022 */
        PEN_MULTICOLOR2 = 2, /* $d023 */
        PEN_FOREGROUND = 3, /* color RAM: $d800-... */

        PEN_MAX = 4
    };

    /** @brief Selects whether `PEN_FOREGROUND` reads a single global register or per-tile Color RAM. */
    enum ForegroundColorMode {
        FOREGROUND_COLOR_GLOBAL = 0,
        FOREGROUND_COLOR_PER_TILE = 1
    };

    /** @brief Bitmask flags selecting which asset sections an export operation writes. */
    enum ExportFeature {
        EXPORT_FEATURE_NONE = 0,
        EXPORT_FEATURE_CHARSET = 1 << 0,
        EXPORT_FEATURE_MAP = 1 << 1,
        EXPORT_FEATURE_COLORS = 1 << 2,

        EXPORT_FEATURE_ALL = (EXPORT_FEATURE_CHARSET | EXPORT_FEATURE_MAP | EXPORT_FEATURE_COLORS)
    };

    /** @brief Target file encoding for binary and source-code asset exports. */
    enum ExportFormat {
        EXPORT_FORMAT_RAW = 0,
        EXPORT_FORMAT_PRG = 1,
        EXPORT_FORMAT_ASM = 2,
        EXPORT_FORMAT_C = 3,
    };

    /** @brief Screen-code keyboard layout used when typing characters into the map or charset views. */
    enum KeyboardMapping {
        KEYBOARD_MAPPING_C64 = 0, // Commodore 8-bit screen code (not PETSCII)
        KEYBOARD_MAPPING_ATARI8 = 1, // Atari 8-bit internal screen code
    };

    /**
     * @brief Multi-character tile dimensions (`1..8 x 1..8`) and character memory stride.
     *
     * When `interleaved == 1`, a tile's `W * H` constituent characters occupy contiguous
     * indices `[tileIdx * W * H .. + W * H - 1]`. When `interleaved > 1` (must equal
     * `256 / (W * H)`), constituent characters step across charset banks by `interleaved`.
     */
    struct TileProperties {
        QSize size { 1, 1 };
        int interleaved = 1;

        [[nodiscard]] bool operator==(const TileProperties& other) const noexcept
        {
            return size == other.size && interleaved == other.interleaved;
        }

        [[nodiscard]] bool operator!=(const TileProperties& other) const noexcept
        {
            return !(*this == other);
        }
    };

    /**
     * @brief Persisted export configuration (16-bit PRG load addresses, format, and section mask).
     *
     * Uses explicit field-wise equality instead of `memcmp` so compiler-inserted struct
     * padding bytes never trigger undefined behavior or spurious undo entries.
     */
    struct ExportProperties {
        uint16_t addresses[3] { 0x3800U, 0x4000U, 0x4400U }; // 0: CHARSET, 1: MAP, 2: COLORS
        ExportFormat format = EXPORT_FORMAT_RAW;
        uint8_t features = EXPORT_FEATURE_CHARSET;

        [[nodiscard]] bool operator==(const ExportProperties& other) const noexcept
        {
            return addresses[0] == other.addresses[0]
                && addresses[1] == other.addresses[1]
                && addresses[2] == other.addresses[2]
                && format == other.format
                && features == other.features;
        }

        [[nodiscard]] bool operator!=(const ExportProperties& other) const noexcept
        {
            return !(*this == other);
        }
    };

    /**
     * @brief Strided rectangular selection descriptor for Copy, Cut, and Paste operations.
     *
     * Encodes `count` rows of `blockSize` elements separated by `skip` elements in a source
     * grid of width `sourceRowWidth`, allowing rectangular 2D selections across the 16-column
     * charset grid, variable-width tileset grid, or `W x H` map grid.
     */
    struct CopyRange {
        enum BufferType {
            CHARS = 0,
            TILES = 1,
            MAP = 2
        };

        int offset = 0;
        int blockSize = 0;
        int skip = 0;
        int count = 0;
        int sourceRowWidth = 0;
        BufferType type = CHARS;
        TileProperties tileProperties { };
        int bufferSize = 0;
    };

    /** @brief Pre-computed 4-connected flood-fill target coordinates and tight bounding rectangle. */
    struct FloodFillPlan {
        QRect bounds;
        std::vector<QPoint> coords;
    };

    /**
     * @brief Fixed-size (`2,304`-byte) pre-mutation snapshot of `_charset` and `_tileColors`.
     *
     * Used by `PasteCommand` and `CutCommand` to restore exact glyph and color state in `O(1)`
     * space without dynamic heap growth.
     */
    struct CharsetColorSnapshot {
        CharsetBuffer charset { };
        TileColorsBuffer tileColors { };
    };

    /**
     * @brief Bounded rectangular map snapshot used by map undo commands.
     *
     * Stores the original tile indices inside `bounds` (`width * height` bytes) prior to
     * mutation so `undo()` restores interior untouched cells (such as flood-fill obstacle
     * islands or non-painted stroke gaps) cleanly.
     */
    struct MapRegionPatch {
        QRect bounds;
        std::vector<uint8_t> cells;
    };

    State(const QString& filename, const QSize& mapSize);
    explicit State(const QString& filename);
    State();
    ~State() override;

    /** @brief Deep-copies document buffers and properties from `state` and clears `_undoStack`. */
    void copyState(const State& state);
    /** @brief Resets the document to the default C64 Upper/Graphics ROM charset and `40x25` map. */
    void reset();
    /** @brief Broadcasts `fileLoaded()` and `contentsChanged()` to synchronize attached views. */
    void emitNewState();

    /** @brief Auto-detects format by extension (`.vchar64proj`, `.ctm`, `.prg`/`.64c`, or raw) and loads `filename`. */
    vchar64::Result<vchar64::ImportReport> openFile(const QString& filename);
    /** @brief Atomically writes a v3 `.vchar64proj` project file to `filename` and marks `_undoStack` clean. */
    vchar64::Result<void> saveProject(const QString& filename);
    /** @brief Exports selected sections (`CHARSET`, `MAP`, `COLORS`) as raw headerless binary files. */
    vchar64::Result<void> exportRaw(const QString& filename, const ExportProperties& properties);
    /** @brief Exports selected sections as 2-byte little-endian load-address `.prg` files. */
    vchar64::Result<void> exportPRG(const QString& filename, const ExportProperties& properties);
    /** @brief Exports selected sections as 6502 assembly `.byte` tables. */
    vchar64::Result<void> exportAsm(const QString& filename, const ExportProperties& properties);
    /** @brief Exports selected sections into a single ISO C17 `.c` source file (`const uint8_t <id>[<N>]`). */
    vchar64::Result<void> exportC(const QString& filename, const ExportProperties& properties);
    /** @brief Renders and atomically saves the tileset and/or map as PNG image(s). */
    vchar64::Result<void> exportPNG(
        const QString& filename,
        bool exportTileset = true,
        bool exportMap = true,
        bool transparentBackground = true);
    /** @brief Re-runs the most recent export using `_exportedFilename` and `_exportProperties`. */
    vchar64::Result<void> export_();

    /** @brief Inserts `-charset`, `-map`, or `-colors` before the file extension when exporting multi-file formats. */
    [[nodiscard]] static QString filenameFixSuffix(const QString& filename, ExportFeature suffix);

    /**
     * @brief Resolves the C64 color index (`0..15`, or `0..7` for Multicolor `PEN_FOREGROUND`) for `pen` on `tileIdx`.
     *
     * When `pen == PEN_FOREGROUND` and Multicolor decoding is active for `tileIdx`, masks out
     * bit 3 (`& 0x07`) to match VIC-II hardware Color RAM behavior.
     */
    [[nodiscard]] int getColorForPen(int pen, int tileIdx) const;
    [[nodiscard]] int getColorForPen(int pen) const;
    [[nodiscard]] int getGlobalColorForPen(int pen) const noexcept;
    void setColorForPen(int pen, int color, int tileIdx);
    void setColorForPen(int pen, int color);
    [[nodiscard]] int getCurrentColor() const;

    void setSelectedPen(int pen);
    [[nodiscard]] int getSelectedPen() const noexcept { return _selectedPen; }

    void setMulticolorMode(bool enabled);
    [[nodiscard]] bool isMulticolorMode() const noexcept { return _multicolorMode; }

    void setForegroundColorMode(ForegroundColorMode mode);
    [[nodiscard]] ForegroundColorMode getForegroundColorMode() const noexcept { return _foregroundColorMode; }

    /** @brief Checks whether the currently active `_tileIndex` decodes as `4x8` 2bpp VIC-II Multicolor. */
    [[nodiscard]] bool shouldBeDisplayedInMulticolor() const noexcept;
    /**
     * @brief Checks whether `tileIdx` decodes as `4x8` 2bpp VIC-II Multicolor (`_multicolorMode` AND foreground color `>= 8`).
     *
     * On the MOS 6567/6569 VIC-II, enabling `$D016` bit 4 puts characters with Color RAM bit 3
     * cleared (`0..7`) into standard `8x8` Hi-Res mode and only decodes characters with Color RAM
     * bit 3 set (`8..15`) as `4x8` 2-bit pixel pairs.
     */
    [[nodiscard]] bool shouldBeDisplayedInMulticolor2(int tileIdx) const noexcept;

    [[nodiscard]] QString getLoadedFilename() const { return _loadedFilename; }
    [[nodiscard]] QString getSavedFilename() const { return _savedFilename; }
    [[nodiscard]] QString getExportedFilename() const { return _exportedFilename; }
    void setLoadedFilenameDirect(const QString& filename) { _loadedFilename = filename; }
    void setSavedFilenameDirect(const QString& filename) { _savedFilename = filename; }
    void setExportedFilenameDirect(const QString& filename) { _exportedFilename = filename; }

    void setTileProperties(const TileProperties& properties);
    [[nodiscard]] TileProperties getTileProperties() const noexcept { return _tileProperties; }
    [[nodiscard]] TileProperties tileProperties() const noexcept { return _tileProperties; }

    void setExportProperties(const ExportProperties& properties);
    [[nodiscard]] ExportProperties getExportProperties() const noexcept { return _exportProperties; }
    [[nodiscard]] ExportProperties exportProperties() const noexcept { return _exportProperties; }

    void setMapSize(const QSize& mapSize);
    [[nodiscard]] const QSize& getMapSize() const noexcept { return _mapSize; }
    [[nodiscard]] const QSize& mapSize() const noexcept { return _mapSize; }

    [[nodiscard]] bool isValidMapCoord(const QPoint& pt) const noexcept;
    [[nodiscard]] int getTileIndexFromMap(const QPoint& mapCoord) const;

    /**
     * @brief Computes a 4-connected flood-fill region via iterative breadth-first search (BFS).
     *
     * Uses an explicit `std::vector<QPoint>` queue and flat visited bitmask rather than recursion
     * so `4096 x 4096` maps (`16.7M` cells) never overflow the call stack.
     */
    [[nodiscard]] FloodFillPlan collectFloodFillRegion(
        const QPoint& startCoord,
        uint8_t targetTile,
        uint8_t replacementTile) const;

    void mapFill(const QPoint& coord, int tileIdx);
    void mapPaint(const QPoint& coord, int tileIdx, bool mergeable);
    void mapClear(int tileIdx);

    [[nodiscard]] bool isModified() const;
    void undo();
    void redo();
    [[nodiscard]] QUndoStack* getUndoStack() const noexcept { return _undoStack.get(); }
    [[nodiscard]] QUndoStack* undoStack() const noexcept { return _undoStack.get(); }
    void clearUndoStack();

    /** @brief Returns the number of addressable tiles (`256 / (tileWidth * tileHeight)`). */
    [[nodiscard]] int maxTiles() const noexcept;
    [[nodiscard]] bool isValidTileIndex(int tileIndex) const noexcept;
    [[nodiscard]] int getCharIndexFromTileIndex(int tileIndex) const noexcept;
    [[nodiscard]] int getTileIndexFromCharIndex(int charIndex) const noexcept;
    [[nodiscard]] const uint8_t* getCharAtIndex(int charIndex) const;

    void copyTileFromIndex(int tileIndex, uint8_t* buffer, int bufferSize) const;
    void copyTileToIndex(int tileIndex, const uint8_t* buffer, int bufferSize);

    [[nodiscard]] const CharsetBuffer& charset() const noexcept { return _charset; }
    [[nodiscard]] const CharsetBuffer& getCharsetBuffer() const noexcept { return _charset; }
    [[nodiscard]] const MapBuffer& map() const noexcept { return _map; }
    [[nodiscard]] const MapBuffer& getMapBuffer() const noexcept { return _map; }
    [[nodiscard]] const TileColorsBuffer& tileColors() const noexcept { return _tileColors; }
    [[nodiscard]] const TileColorsBuffer& getTileColors() const noexcept { return _tileColors; }

    void resetCharsetBuffer() noexcept;

    /**
     * @brief Atomically replaces `_charset`, `_tileColors`, `_mapSize`, and `_map` from validated importer buffers.
     *
     * Rejects out-of-range dimensions (`<= 0`, `> kMaxMapDim`, or `mapCells.size() != W * H`)
     * without mutating existing state. Masks every Color RAM entry to 4 bits (`& 0x0F`).
     */
    [[nodiscard]] bool assignImportedBuffers(
        const CharsetBuffer& charsetBuf,
        const TileColorsBuffer& tileColorsBuf,
        const QSize& newMapSize,
        std::vector<uint8_t> mapCells);
    /** @brief Replaces `_mapSize` and `_map` directly without pushing an undo command; returns `false` on dimension mismatch. */
    [[nodiscard]] bool replaceMap(const QSize& size, std::vector<uint8_t> cells);
    /** @brief Copies up to `CHAR_BUFFER_SIZE` bytes into `_charset` and zero-pads any trailing bytes. */
    void setCharsetBytes(const uint8_t* data, size_t byteCount) noexcept;
    /** @brief Copies up to `TILE_COLORS_BUFFER_SIZE` bytes into `_tileColors` (4-bit masked) and fills remainder with `TILE_COLORS_DEFAULT`. */
    void setTileColorsDirect(const uint8_t* data, size_t count) noexcept;
    void setTileColorDirect(int tileIdx, uint8_t color) noexcept;
    void setPenColorDirect(int pen, uint8_t color) noexcept;
    void setMulticolorModeDirect(bool enabled) noexcept { _multicolorMode = enabled; }
    void setForegroundColorModeDirect(ForegroundColorMode mode) noexcept { _foregroundColorMode = mode; }
    [[nodiscard]] bool setTilePropertiesDirect(const TileProperties& properties) noexcept;
    void setExportPropertiesDirect(const ExportProperties& properties) noexcept { _exportProperties = properties; }
    void setKeyboardMappingDirect(KeyboardMapping mapping) noexcept { _keyboardMapping = mapping; }

    void paste(int offset, const CopyRange& copyRange, const uint8_t* origBuffer);
    void cut(const CopyRange& copyRange);

    /**
     * @brief Checks whether `tileIndex` supports lossless 90-degree clockwise rotation.
     *
     * Rotation requires a square tile (`width == height`) in `8x8` Hi-Res mode. Non-square
     * tiles and `4x8` 2bpp Multicolor tiles (non-square `2:1` pixel aspect ratio) return `false`.
     */
    [[nodiscard]] bool canRotateTile(int tileIndex) const noexcept;
    /**
     * @brief Reverses pixels horizontally across one 8-bit character row.
     *
     * Reverses all 8 individual bits in Hi-Res mode, or swaps the four 2-bit pixel pairs
     * (`[7:6] <-> [1:0]`, `[5:4] <-> [3:2]`) in Multicolor mode so pen indices stay intact.
     */
    [[nodiscard]] static uint8_t reverseCharRowBits(uint8_t rowByte, bool isMulticolorTile) noexcept;

    void tilePaint(int tileIndex, const QPoint& point, int pen, bool mergeable = false);
    void tileInvert(int tileIndex);
    void tileClear(int tileIndex);
    void tileFlipHorizontally(int tileIndex);
    void tileFlipVertically(int tileIndex);
    void tileRotate(int tileIndex);
    void tileShiftLeft(int tileIndex);
    void tileShiftRight(int tileIndex);
    void tileShiftUp(int tileIndex);
    void tileShiftDown(int tileIndex);

    [[nodiscard]] int tileGetPen(int tileIndex, const QPoint& position) const;

    [[nodiscard]] int getTileIndex() const noexcept { return _tileIndex; }
    [[nodiscard]] int getCharIndex() const noexcept { return _charIndex; }
    [[nodiscard]] KeyboardMapping getKeyboardMapping() const noexcept { return _keyboardMapping; }

    /** @brief Captures a clamped rectangular sub-grid of `_map` for undo restoration. */
    [[nodiscard]] MapRegionPatch captureMapPatch(const QRect& bounds) const;
    /** @brief Writes a previously captured `MapRegionPatch` back into `_map`. */
    void restoreMapPatch(const MapRegionPatch& patch);
    /** @brief Restores `_charset` and 4-bit-masked `_tileColors` from a pre-mutation snapshot. */
    void restoreCharsetAndColors(const CharsetBuffer& charsetBuf, const TileColorsBuffer& tileColorsBuf);

signals:
    void fileLoaded();
    void tilePropertiesUpdated();
    void mapSizeUpdated();
    void mapContentUpdated();
    void bytesUpdated(int pos, int count);
    void tileUpdated(int tileIndex);
    void charsetUpdated();
    void colorPropertiesUpdated(int pen);
    void selectedPenChanged(int pen);
    void multicolorModeToggled(bool enabled);
    void contentsChanged();
    void charIndexUpdated(int charIndex);
    void tileIndexUpdated(int tileIndex);

public slots:
    void setCharIndex(int charIndex);
    void setTileIndex(int tileIndex);

protected:
    [[nodiscard]] uint8_t* mutableCharAtIndex(int charIndex);
    [[nodiscard]] CharGlyph getCharFromTile(int tileIndex, int x, int y) const;
    void setCharForTile(int tileIndex, int x, int y, const CharGlyph& chr);

    void setupDefaultMap();

    void _setCharIndex(int charIndex);
    void _setTileIndex(int tileIndex);

    void _pasteChars(int charIndex, const CopyRange& copyRange, const uint8_t* origBuffer);
    void _pasteTiles(int charIndex, const CopyRange& copyRange, const uint8_t* origBuffer);
    void _pasteMap(int dstOffset, const CopyRange& copyRange, const uint8_t* origBuffer);
    void _paste(int offset, const CopyRange& copyRange, const uint8_t* origBuffer);

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
    void _applyTilePoints(int tileIndex, const QList<QPoint>& points, int pen);
    void _setMulticolorMode(bool enabled);
    void _setForegroundColorMode(ForegroundColorMode mode);
    void _setColorForPen(int pen, int color, int tileIdx);
    void _setTileProperties(const TileProperties& properties);
    void _setExportProperties(const ExportProperties& properties);

    void _setMapSize(const QSize& mapSize);
    void _setMap(const std::vector<uint8_t>& mapCells, const QSize& size);
    void _mapClear(int tileIdx);
    void _mapPaint(const QPoint& coord, int tileIdx);
    void _applyMapPoints(const QList<QPoint>& coords, uint8_t tileIdx);
    void _applyFloodFillCoords(const std::vector<QPoint>& coords, uint8_t replacementTile);
    void _fillMapRect(const QRect& bounds, uint8_t value);

    int _totalChars = 0;

    CharsetBuffer _charset { };
    TileColorsBuffer _tileColors { };
    MapBuffer _map;
    QSize _mapSize { 40, 25 };

    bool _multicolorMode = false;
    ForegroundColorMode _foregroundColorMode = FOREGROUND_COLOR_GLOBAL;

    int _selectedPen = PEN_FOREGROUND;
    std::array<uint8_t, PEN_MAX> _penColors { 1U, 5U, 7U, 11U };

    TileProperties _tileProperties { QSize(1, 1), 1 };

    int _charIndex = 0;
    int _tileIndex = 0;

    QString _loadedFilename;
    QString _savedFilename;
    QString _exportedFilename;

    ExportProperties _exportProperties { };

    std::unique_ptr<QUndoStack> _undoStack;

    KeyboardMapping _keyboardMapping = KEYBOARD_MAPPING_C64;
};
