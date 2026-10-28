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
 * @file commands.h
 * @brief Reversible `QUndoCommand` implementations for `State` (`vchar64_core`).
 *
 * Every interactive mutation in VChar64 pushes one of these command objects onto
 * `State::_undoStack`. Commands capture minimal, bounded pre-mutation snapshots
 * (`CharsetColorSnapshot`, `MapRegionPatch`, or fixed `512`-byte tile buffers)
 * lazily during their initial `redo()` execution and invoke `State`'s protected
 * direct-mutation helpers so undo/redo transitions never re-enter `_undoStack`.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <QList>
#include <QPoint>
#include <QSize>
#include <QUndoCommand>

#include "state.h"

using CharsetColorSnapshot = State::CharsetColorSnapshot;
using MapRegionPatch = State::MapRegionPatch;

/** @brief Command IDs returned by `QUndoCommand::id()` for continuous drag-stroke merging. */
enum UndoCommands {
    Cmd_PaintTile = 1,
    Cmd_PaintMap = 2,
};

/**
 * @brief Paints one or more pixels on a single tile with drag-stroke coalescing.
 *
 * Captures the tile's original `W * H * 8`-byte glyph data once (`_captured`) and
 * accumulates subsequent drag points via `mergeWith()` when `_mergeable` is true
 * and both commands target the same `_tileIndex` and `_pen`.
 */
class PaintTileCommand : public QUndoCommand {
public:
    PaintTileCommand(
        State* state,
        int tileIndex,
        const QPoint& position,
        int pen,
        bool mergeable,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override { return Cmd_PaintTile; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    State* _state;
    int _tileIndex;
    int _pen;
    std::array<uint8_t, State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8> _buffer { };
    QList<QPoint> _points;
    bool _mergeable;
    bool _captured = false;
};

/**
 * @brief Pastes a `CHARS`, `TILES`, or `MAP` selection at `offset`.
 *
 * For `CHARS` and `TILES`, snapshots the full `2,304`-byte `CharsetColorSnapshot`
 * before pasting so `undo()` restores both glyph bytes and `_tileColors` intact.
 * For `MAP`, captures a tight `MapRegionPatch` covering only the destination
 * sub-grid intersected with `_mapSize`.
 */
class PasteCommand : public QUndoCommand {
public:
    PasteCommand(
        State* state,
        int offset,
        const State::CopyRange& copyRange,
        const uint8_t* buffer,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _offset;
    State::CopyRange _copyRange;
    std::vector<uint8_t> _copyBuffer;
    CharsetColorSnapshot _beforeChars { };
    MapRegionPatch _beforeMap { };
    bool _captured = false;
};

/**
 * @brief Zero-fills a `CHARS`, `TILES`, or `MAP` selection while preserving undo state.
 *
 * Uses `CharsetColorSnapshot` (instead of a zero-filled `PasteCommand` buffer) for
 * `CHARS`/`TILES` so cutting glyphs zeroes only `_charset` bytes without wiping
 * `_tileColors` entries to `0x00` (Black, Hi-Res) and without `std::back_inserter`
 * memory growth across repeated redo cycles.
 */
class CutCommand : public QUndoCommand {
public:
    CutCommand(
        State* state,
        const State::CopyRange& copyRange,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _offset = 0;
    State::CopyRange _copyRange;
    CharsetColorSnapshot _beforeChars { };
    MapRegionPatch _beforeMap { };
    bool _captured = false;
};

/** @brief Self-inverse horizontal mirror command for a tile (reverses bits or 2-bit Multicolor pairs). */
class FlipTileHCommand : public QUndoCommand {
public:
    FlipTileHCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Self-inverse vertical mirror command for a tile. */
class FlipTileVCommand : public QUndoCommand {
public:
    FlipTileVCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Circularly shifts a tile left by 1 pixel (1 bit in Hi-Res, 2 bits in Multicolor); `undo()` shifts right. */
class ShiftLeftTileCommand : public QUndoCommand {
public:
    ShiftLeftTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Circularly shifts a tile right by 1 pixel (1 bit in Hi-Res, 2 bits in Multicolor); `undo()` shifts left. */
class ShiftRightTileCommand : public QUndoCommand {
public:
    ShiftRightTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Circularly shifts a tile up by 1 pixel row; `undo()` shifts down. */
class ShiftUpTileCommand : public QUndoCommand {
public:
    ShiftUpTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Circularly shifts a tile down by 1 pixel row; `undo()` shifts up. */
class ShiftDownTileCommand : public QUndoCommand {
public:
    ShiftDownTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/**
 * @brief Rotates a square Hi-Res tile 90 degrees clockwise.
 *
 * Snapshots the pre-rotation tile buffer (`<= 512` bytes) on initial `redo()` so
 * `undo()` restores the original glyph bytes in `O(1)` rather than running three
 * sequential 90-degree rotations.
 */
class RotateTileCommand : public QUndoCommand {
public:
    RotateTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
    std::array<uint8_t, State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8> _buffer { };
    bool _captured = false;
};

/** @brief Self-inverse bitwise NOT (`~byte`) command for all character rows in a tile. */
class InvertTileCommand : public QUndoCommand {
public:
    InvertTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
};

/** @brief Zeroes all character rows of a tile after snapshotting its pre-clear bytes for `undo()`. */
class ClearTileCommand : public QUndoCommand {
public:
    ClearTileCommand(State* state, int tileIndex, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIndex;
    std::array<uint8_t, State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8> _buffer { };
    bool _captured = false;
};

/** @brief Updates tile dimensions (`1..8 x 1..8`) and character interleave factor. */
class SetTilePropertiesCommand : public QUndoCommand {
public:
    SetTilePropertiesCommand(
        State* state,
        const State::TileProperties& properties,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    State::TileProperties _new;
    State::TileProperties _old { };
    bool _captured = false;
};

/** @brief Updates persisted export addresses, format, and section bitmask. */
class SetExportPropertiesCommand : public QUndoCommand {
public:
    SetExportPropertiesCommand(
        State* state,
        const State::ExportProperties& properties,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    State::ExportProperties _new;
    State::ExportProperties _old { };
    bool _captured = false;
};

/** @brief Toggles document-wide VIC-II Multicolor mode (`$D016` bit 4). */
class SetMulticolorModeCommand : public QUndoCommand {
public:
    SetMulticolorModeCommand(
        State* state,
        bool multicolorEnabled,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    bool _old = false;
    bool _new;
    bool _captured = false;
};

/** @brief Sets the 4-bit C64 color (`0..15`) for a global pen or a specific tile's Color RAM entry. */
class SetColorCommand : public QUndoCommand {
public:
    SetColorCommand(
        State* state,
        int color,
        int pen,
        int tileIdx,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _pen;
    int _old = 0;
    int _new;
    int _tileIdx;
    bool _captured = false;
};

/** @brief Switches `PEN_FOREGROUND` between global register mode and per-tile Color RAM mode. */
class SetForegroundColorMode : public QUndoCommand {
public:
    SetForegroundColorMode(
        State* state,
        State::ForegroundColorMode mode,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    State::ForegroundColorMode _mode;
    State::ForegroundColorMode _oldMode = State::FOREGROUND_COLOR_GLOBAL;
    bool _captured = false;
};

using SetForegroundColorModeCommand = SetForegroundColorMode;

/** @brief Resizes the map grid while snapshotting `_oldMap` so `undo()` recovers cropped cells. */
class SetMapSizeCommand : public QUndoCommand {
public:
    SetMapSizeCommand(State* state, const QSize& mapSize, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    QSize _new;
    QSize _old;
    std::vector<uint8_t> _oldMap;
};

using ResizeMapCommand = SetMapSizeCommand;

/**
 * @brief Flood-fills a 4-connected map region while preserving non-filled interior obstacles on `undo()`.
 *
 * Captures `MapRegionPatch` over `_plan.bounds` *before* writing `_tileIdx` into `_plan.coords`.
 * Restoring `_before` on `undo()` preserves any interior wall tiles or enclosed pockets inside
 * the bounding box that had a different tile index than the flood-fill target.
 */
class FillMapCommand : public QUndoCommand {
public:
    FillMapCommand(
        State* state,
        const QPoint& coord,
        int tileIdx,
        QUndoCommand* parent = nullptr);
    FillMapCommand(
        State* state,
        State::FloodFillPlan plan,
        uint8_t replacementTile,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    QPoint _coord { };
    uint8_t _tileIdx = 0U;
    State::FloodFillPlan _plan { };
    MapRegionPatch _before { };
    bool _planReady = false;
    bool _captured = false;
};

/**
 * @brief Paints one or more map cells during click or continuous brush-drag strokes.
 *
 * When consecutive drag steps coalesce in `mergeWith()`, `_before` expands to the
 * union bounding box by sampling current `_state->map()` first and overlaying the
 * already-captured `_before` (`b1`) on top, ensuring cells painted earlier in the
 * same stroke retain their true pre-stroke tile indices.
 */
class PaintMapCommand : public QUndoCommand {
public:
    PaintMapCommand(
        State* state,
        const QPoint& position,
        int tileIdx,
        bool mergeable,
        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override { return Cmd_PaintMap; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    State* _state;
    uint8_t _tileIdx;
    bool _mergeable;
    QList<QPoint> _points;
    MapRegionPatch _before { };
    QSize _mapSize { };
    bool _captured = false;
};

/** @brief Fills the entire map with `tileIdx` after snapshotting `_oldMap` for `undo()`. */
class ClearMapCommand : public QUndoCommand {
public:
    ClearMapCommand(State* state, int tileIdx, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    State* _state;
    int _tileIdx;
    std::vector<uint8_t> _oldMap;
    QSize _mapSize { };
    bool _captured = false;
};
