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
 * @file commands.cpp
 * @brief `QUndoCommand` undo/redo snapshot capture, stroke coalescing, and restoration.
 *
 * Key undo invariants enforced here:
 *
 * - **Fixed-size charset/color snapshots:** `PasteCommand` and `CutCommand` assign
 *   fixed `2,304`-byte buffers (`CharsetColorSnapshot`) rather than `std::back_inserter`,
 *   preventing unbounded vector growth across repeated redo cycles.
 * - **Pre-mutation flood-fill patches:** `FillMapCommand::redo()` captures `_before`
 *   over `_plan.bounds` *before* writing `_plan.coords`, preserving untouched obstacle
 *   walls and interior pockets on `undo()`.
 * - **Earliest-first brush union:** `PaintMapCommand::mergeWith()` checks `b1` before
 *   `b2` and `currentMap`, preserving pre-stroke cell values across self-overlapping drags.
 */

#include "commands.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <QObject>
#include <QRect>

namespace {

/**
 * @brief Intersects a destination `[blockWidth x rowCount]` map paste/cut region at `linearOffset` with `mapSize`.
 *
 * Clamps horizontal and vertical extents to `[0 .. mapW - dstX]` and `[0 .. mapH - dstY]`
 * so selections pasted near the right or bottom map edge never wrap onto the next row or
 * read/write past `_map.size()`.
 */
QRect computeClippedMapRect(
    const QSize& mapSize,
    int linearOffset,
    int blockWidth,
    int rowCount) noexcept
{
    const int mapW = mapSize.width();
    const int mapH = mapSize.height();
    if (mapW <= 0 || mapH <= 0 || linearOffset < 0 || blockWidth <= 0 || rowCount <= 0) {
        return { };
    }
    const uint64_t totalCells = static_cast<uint64_t>(mapW) * static_cast<uint64_t>(mapH);
    if (static_cast<uint64_t>(linearOffset) >= totalCells) {
        return { };
    }

    const int dstX = linearOffset % mapW;
    const int dstY = linearOffset / mapW;
    const int cols = std::clamp(blockWidth, 0, mapW - dstX);
    const int rows = std::clamp(rowCount, 0, mapH - dstY);
    if (cols <= 0 || rows <= 0) {
        return { };
    }
    return QRect(dstX, dstY, cols, rows);
}

QString copyBufferTypeLabel(State::CopyRange::BufferType type)
{
    switch (type) {
    case State::CopyRange::CHARS:
        return QObject::tr("Chars");
    case State::CopyRange::TILES:
        return QObject::tr("Tiles");
    case State::CopyRange::MAP:
        return QObject::tr("Map");
    }
    return QObject::tr("Chars");
}

} // namespace

// PaintTileCommand

PaintTileCommand::PaintTileCommand(
    State* state,
    int tileIndex,
    const QPoint& position,
    int pen,
    bool mergeable,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
    , _pen(pen)
    , _mergeable(mergeable)
{
    _points.append(position);
    setText(QObject::tr("Paint Tile #%1").arg(_tileIndex));
}

void PaintTileCommand::undo()
{
    if (_captured) {
        _state->copyTileToIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
    }
}

void PaintTileCommand::redo()
{
    if (!_captured && _state->isValidTileIndex(_tileIndex)) {
        _state->copyTileFromIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
        _captured = true;
    }

    _state->_applyTilePoints(_tileIndex, _points, _pen);
}

bool PaintTileCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* p = dynamic_cast<const PaintTileCommand*>(other);
    if (p == nullptr || _state != p->_state || _pen != p->_pen || _tileIndex != p->_tileIndex || !p->_mergeable) {
        return false;
    }

    _points.append(p->_points);
    return true;
}

// ClearTileCommand

ClearTileCommand::ClearTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Clear Tile #%1").arg(_tileIndex));
}

void ClearTileCommand::undo()
{
    if (_captured) {
        _state->copyTileToIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
    }
}

void ClearTileCommand::redo()
{
    if (!_captured && _state->isValidTileIndex(_tileIndex)) {
        _state->copyTileFromIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
        _captured = true;
    }
    _state->_tileClear(_tileIndex);
}

// PasteCommand

PasteCommand::PasteCommand(
    State* state,
    int offset,
    const State::CopyRange& copyRange,
    const uint8_t* buffer,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _offset(offset)
    , _copyRange(copyRange)
{
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES) {
        constexpr size_t kTotalBytes = static_cast<size_t>(
            State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE);
        _copyBuffer.assign(kTotalBytes, 0U);
        if (buffer != nullptr) {
            const size_t avail = (copyRange.bufferSize > 0)
                ? std::min(static_cast<size_t>(copyRange.bufferSize), kTotalBytes)
                : kTotalBytes;
            std::copy_n(buffer, avail, _copyBuffer.data());
        }
        _copyRange.bufferSize = static_cast<int>(kTotalBytes);
    } else {
        // Normalize extracted rectangular map slice into a tight [blockSize x count] subgrid
        // so PasteCommand never retains a whole source map copy on the undo stack.
        if (buffer != nullptr && copyRange.offset >= 0 && copyRange.blockSize > 0 && copyRange.count > 0) {
            const uint64_t tightCells = static_cast<uint64_t>(copyRange.blockSize)
                * static_cast<uint64_t>(copyRange.count);
            if (tightCells > 0ULL && tightCells <= State::kMaxMapCells) {
                const auto tightSize = static_cast<size_t>(tightCells);
                _copyBuffer.assign(tightSize, 0U);

                const uint64_t rowWidthCells = (copyRange.sourceRowWidth >= copyRange.blockSize)
                    ? (static_cast<uint64_t>(copyRange.sourceRowWidth)
                          * static_cast<uint64_t>(copyRange.count))
                    : 0ULL;
                const bool hasExplicitRowWidth = (copyRange.offset == 0 && copyRange.skip <= 0)
                    && (rowWidthCells > 0ULL)
                    && (copyRange.bufferSize == static_cast<int>(rowWidthCells));
                const bool isAlreadyTight = (copyRange.offset == 0)
                    && ((copyRange.bufferSize == static_cast<int>(tightSize))
                        || (copyRange.bufferSize <= 0 && copyRange.skip <= 0));
                const int srcStride = hasExplicitRowWidth
                    ? copyRange.sourceRowWidth
                    : (isAlreadyTight
                              ? copyRange.blockSize
                              : (copyRange.blockSize + std::max(0, copyRange.skip)));

                for (int row = 0; row < copyRange.count; ++row) {
                    const uint64_t srcRowOffset = static_cast<uint64_t>(copyRange.offset)
                        + static_cast<uint64_t>(row) * static_cast<uint64_t>(srcStride);
                    size_t colsToRead = static_cast<size_t>(copyRange.blockSize);
                    if (copyRange.bufferSize > 0) {
                        if (srcRowOffset >= static_cast<uint64_t>(copyRange.bufferSize)) {
                            break;
                        }
                        const size_t rem = static_cast<size_t>(
                            static_cast<uint64_t>(copyRange.bufferSize) - srcRowOffset);
                        colsToRead = std::min(colsToRead, rem);
                    }
                    std::copy_n(
                        buffer + static_cast<size_t>(srcRowOffset),
                        colsToRead,
                        _copyBuffer.data() + static_cast<size_t>(row) * static_cast<size_t>(copyRange.blockSize));
                }

                _copyRange.offset = 0;
                _copyRange.skip = 0;
                _copyRange.sourceRowWidth = copyRange.blockSize;
                _copyRange.bufferSize = static_cast<int>(tightSize);
            }
        }
    }

    setText(QObject::tr("Paste %1").arg(copyBufferTypeLabel(_copyRange.type)));
}

void PasteCommand::undo()
{
    if (!_captured) {
        return;
    }

    if (_copyRange.type == State::CopyRange::MAP) {
        _state->restoreMapPatch(_beforeMap);
    } else {
        _state->restoreCharsetAndColors(_beforeChars.charset, _beforeChars.tileColors);
    }
}

void PasteCommand::redo()
{
    if (_copyRange.type == State::CopyRange::MAP) {
        if (!_captured) {
            const QRect affected = computeClippedMapRect(
                _state->mapSize(),
                _offset,
                _copyRange.blockSize,
                _copyRange.count);
            if (!affected.isEmpty()) {
                _beforeMap = _state->captureMapPatch(affected);
            }
            _captured = true;
        }
        _state->_paste(_offset, _copyRange, _copyBuffer.data());
    } else {
        if (!_captured) {
            _beforeChars.charset = _state->charset();
            _beforeChars.tileColors = _state->tileColors();
            _captured = true;
        }
        _state->_paste(_offset, _copyRange, _copyBuffer.data());
    }
}

// CutCommand

CutCommand::CutCommand(
    State* state,
    const State::CopyRange& copyRange,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _copyRange(copyRange)
{
    if (_copyRange.type == State::CopyRange::TILES && _copyRange.tileProperties.interleaved == 1) {
        const int tileArea = std::max(
            1,
            _copyRange.tileProperties.size.width() * _copyRange.tileProperties.size.height());
        _offset = _copyRange.offset * tileArea;
    } else {
        _offset = _copyRange.offset;
    }

    setText(QObject::tr("Cut %1").arg(copyBufferTypeLabel(_copyRange.type)));
}

void CutCommand::undo()
{
    if (!_captured) {
        return;
    }

    if (_copyRange.type == State::CopyRange::MAP) {
        _state->restoreMapPatch(_beforeMap);
    } else {
        _state->restoreCharsetAndColors(_beforeChars.charset, _beforeChars.tileColors);
    }
}

void CutCommand::redo()
{
    if (_copyRange.type == State::CopyRange::MAP) {
        const QRect affected = computeClippedMapRect(
            _state->mapSize(),
            _offset,
            _copyRange.blockSize,
            _copyRange.count);
        if (!_captured) {
            if (!affected.isEmpty()) {
                _beforeMap = _state->captureMapPatch(affected);
            }
            _captured = true;
        }
        if (!affected.isEmpty()) {
            _state->_fillMapRect(affected, 0U);
        }
    } else {
        if (!_captured) {
            _beforeChars.charset = _state->charset();
            _beforeChars.tileColors = _state->tileColors();
            _captured = true;
        }
        static constexpr std::array<
            uint8_t,
            State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE>
            kZeroCharsAndColors { };
        State::CopyRange rangeWithSize = _copyRange;
        rangeWithSize.bufferSize = static_cast<int>(kZeroCharsAndColors.size());
        _state->_paste(_offset, rangeWithSize, kZeroCharsAndColors.data());
    }
}

// FlipTileHCommand

FlipTileHCommand::FlipTileHCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Flip Tile Horizontally #%1").arg(_tileIndex));
}

void FlipTileHCommand::undo()
{
    _state->_tileFlipHorizontally(_tileIndex);
}

void FlipTileHCommand::redo()
{
    _state->_tileFlipHorizontally(_tileIndex);
}

// FlipTileVCommand

FlipTileVCommand::FlipTileVCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Flip Tile Vertically #%1").arg(_tileIndex));
}

void FlipTileVCommand::undo()
{
    _state->_tileFlipVertically(_tileIndex);
}

void FlipTileVCommand::redo()
{
    _state->_tileFlipVertically(_tileIndex);
}

// RotateTileCommand

RotateTileCommand::RotateTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Rotate Tile #%1").arg(_tileIndex));
}

void RotateTileCommand::undo()
{
    if (_captured) {
        _state->copyTileToIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
    }
}

void RotateTileCommand::redo()
{
    if (!_state->canRotateTile(_tileIndex)) {
        return;
    }
    if (!_captured) {
        _state->copyTileFromIndex(_tileIndex, _buffer.data(), static_cast<int>(_buffer.size()));
        _captured = true;
    }
    _state->_tileRotate(_tileIndex);
}

// InvertTileCommand

InvertTileCommand::InvertTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Invert Tile #%1").arg(_tileIndex));
}

void InvertTileCommand::undo()
{
    _state->_tileInvert(_tileIndex);
}

void InvertTileCommand::redo()
{
    _state->_tileInvert(_tileIndex);
}

// ShiftLeftTileCommand

ShiftLeftTileCommand::ShiftLeftTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Shift Tile Left #%1").arg(_tileIndex));
}

void ShiftLeftTileCommand::undo()
{
    _state->_tileShiftRight(_tileIndex);
}

void ShiftLeftTileCommand::redo()
{
    _state->_tileShiftLeft(_tileIndex);
}

// ShiftRightTileCommand

ShiftRightTileCommand::ShiftRightTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Shift Tile Right #%1").arg(_tileIndex));
}

void ShiftRightTileCommand::undo()
{
    _state->_tileShiftLeft(_tileIndex);
}

void ShiftRightTileCommand::redo()
{
    _state->_tileShiftRight(_tileIndex);
}

// ShiftUpTileCommand

ShiftUpTileCommand::ShiftUpTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Shift Tile Up #%1").arg(_tileIndex));
}

void ShiftUpTileCommand::undo()
{
    _state->_tileShiftDown(_tileIndex);
}

void ShiftUpTileCommand::redo()
{
    _state->_tileShiftUp(_tileIndex);
}

// ShiftDownTileCommand

ShiftDownTileCommand::ShiftDownTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
{
    setText(QObject::tr("Shift Tile Down #%1").arg(_tileIndex));
}

void ShiftDownTileCommand::undo()
{
    _state->_tileShiftUp(_tileIndex);
}

void ShiftDownTileCommand::redo()
{
    _state->_tileShiftDown(_tileIndex);
}

// SetTilePropertiesCommand

SetTilePropertiesCommand::SetTilePropertiesCommand(
    State* state,
    const State::TileProperties& properties,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(properties)
{
    setText(QObject::tr("Tile Properties %1x%2 - %3")
            .arg(properties.size.width())
            .arg(properties.size.height())
            .arg(properties.interleaved));
}

void SetTilePropertiesCommand::undo()
{
    if (_captured) {
        _state->_setTileProperties(_old);
    }
}

void SetTilePropertiesCommand::redo()
{
    if (!_captured) {
        _old = _state->getTileProperties();
        _captured = true;
    }
    _state->_setTileProperties(_new);
}

// SetExportPropertiesCommand

SetExportPropertiesCommand::SetExportPropertiesCommand(
    State* state,
    const State::ExportProperties& properties,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(properties)
{
    setText(QObject::tr("Export Properties"));
}

void SetExportPropertiesCommand::undo()
{
    if (_captured) {
        _state->_setExportProperties(_old);
    }
}

void SetExportPropertiesCommand::redo()
{
    if (!_captured) {
        _old = _state->getExportProperties();
        _captured = true;
    }
    _state->_setExportProperties(_new);
}

// SetMulticolorModeCommand

SetMulticolorModeCommand::SetMulticolorModeCommand(
    State* state,
    bool multicolorEnabled,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(multicolorEnabled)
{
    if (_new) {
        setText(QObject::tr("Multicolor enabled"));
    } else {
        setText(QObject::tr("Multicolor disabled"));
    }
}

void SetMulticolorModeCommand::undo()
{
    if (_captured) {
        _state->_setMulticolorMode(_old);
    }
}

void SetMulticolorModeCommand::redo()
{
    if (!_captured) {
        _old = _state->isMulticolorMode();
        _captured = true;
    }
    _state->_setMulticolorMode(_new);
}

// SetColorCommand

SetColorCommand::SetColorCommand(
    State* state,
    int color,
    int pen,
    int tileIdx,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _pen(pen)
    , _new(static_cast<int>(static_cast<unsigned int>(color) & 0x0FU))
    , _tileIdx(tileIdx)
{
    setText(QObject::tr("Color[%1] = %2")
            .arg(pen)
            .arg(_new));
}

void SetColorCommand::undo()
{
    if (_captured) {
        _state->_setColorForPen(_pen, _old, _tileIdx);
    }
}

void SetColorCommand::redo()
{
    if (!_captured) {
        _old = _state->getColorForPen(_pen, _tileIdx);
        _captured = true;
    }
    _state->_setColorForPen(_pen, _new, _tileIdx);
}

// SetForegroundColorMode

SetForegroundColorMode::SetForegroundColorMode(
    State* state,
    State::ForegroundColorMode mode,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _mode(mode)
{
    setText(QObject::tr("Foreground Mode = %1").arg(static_cast<int>(mode)));
}

void SetForegroundColorMode::undo()
{
    if (_captured) {
        _state->_setForegroundColorMode(_oldMode);
    }
}

void SetForegroundColorMode::redo()
{
    if (!_captured) {
        _oldMode = _state->getForegroundColorMode();
        _captured = true;
    }
    _state->_setForegroundColorMode(_mode);
}

// SetMapSizeCommand

SetMapSizeCommand::SetMapSizeCommand(State* state, const QSize& mapSize, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(mapSize)
    , _old(state->getMapSize())
    , _oldMap(state->getMapBuffer())
{
    setText(QObject::tr("Map Size %1x%2")
            .arg(mapSize.width())
            .arg(mapSize.height()));
}

void SetMapSizeCommand::undo()
{
    _state->_setMapSize(_old);
    _state->_setMap(_oldMap, _old);
}

void SetMapSizeCommand::redo()
{
    _state->_setMapSize(_new);
}

// FillMapCommand

FillMapCommand::FillMapCommand(
    State* state,
    const QPoint& coord,
    int tileIdx,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _coord(coord)
    , _tileIdx(static_cast<uint8_t>(static_cast<unsigned int>(tileIdx) & 0xFFU))
{
    setText(QObject::tr("Map Fill"));
}

FillMapCommand::FillMapCommand(
    State* state,
    State::FloodFillPlan plan,
    uint8_t replacementTile,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(replacementTile)
    , _plan(std::move(plan))
    , _planReady(true)
{
    setText(QObject::tr("Map Fill"));
}

void FillMapCommand::undo()
{
    if (_captured && !_before.bounds.isEmpty()) {
        _state->restoreMapPatch(_before);
    }
}

void FillMapCommand::redo()
{
    if (!_planReady) {
        if (_state->isValidMapCoord(_coord)) {
            const size_t startIdx = static_cast<size_t>(_coord.y())
                    * static_cast<size_t>(_state->mapSize().width())
                + static_cast<size_t>(_coord.x());
            const uint8_t targetTile = _state->map()[startIdx];
            _plan = _state->collectFloodFillRegion(_coord, targetTile, _tileIdx);
        }
        _planReady = true;
    }

    if (_plan.coords.empty()) {
        return;
    }

    // CRITICAL: Capture _before from the untouched _map BEFORE writing _plan.coords
    // so pre-existing _tileIdx obstacle walls and enclosed pockets inside _plan.bounds
    // remain bit-exact across undo()/redo().
    if (!_captured) {
        _before = _state->captureMapPatch(_plan.bounds);
        _captured = true;
    }

    _state->_applyFloodFillCoords(_plan.coords, _tileIdx);
}

// ClearMapCommand

ClearMapCommand::ClearMapCommand(State* state, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tileIdx)
{
    setText(QObject::tr("Map Clear"));
}

void ClearMapCommand::undo()
{
    if (_captured) {
        _state->_setMap(_oldMap, _mapSize);
    }
}

void ClearMapCommand::redo()
{
    if (!_captured) {
        _mapSize = _state->getMapSize();
        _oldMap = _state->getMapBuffer();
        _captured = true;
    }
    _state->_mapClear(_tileIdx);
}

// PaintMapCommand

PaintMapCommand::PaintMapCommand(
    State* state,
    const QPoint& position,
    int tileIdx,
    bool mergeable,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(static_cast<uint8_t>(static_cast<unsigned int>(tileIdx) & 0xFFU))
    , _mergeable(mergeable)
    , _mapSize(state->getMapSize())
{
    _points.append(position);
    setText(QObject::tr("Paint Map #%1").arg(static_cast<int>(_tileIdx)));
}

void PaintMapCommand::undo()
{
    if (_captured && !_before.bounds.isEmpty()) {
        _state->restoreMapPatch(_before);
    }
}

void PaintMapCommand::redo()
{
    if (!_captured && !_points.isEmpty() && _state->isValidMapCoord(_points.first())) {
        _before = _state->captureMapPatch(QRect(_points.first(), QSize(1, 1)));
        _captured = true;
    }

    _state->_applyMapPoints(_points, _tileIdx);
}

bool PaintMapCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* p = dynamic_cast<const PaintMapCommand*>(other);
    if (p == nullptr
        || _state != p->_state
        || _tileIdx != p->_tileIdx
        || !p->_mergeable
        || _mapSize != p->_mapSize
        || _state->mapSize() != _mapSize) {
        return false;
    }

    if (!p->_before.bounds.isEmpty()) {
        if (_before.bounds.isEmpty()) {
            _before = p->_before;
            _captured = true;
        } else {
            const QRect b1 = _before.bounds;
            const QRect b2 = p->_before.bounds;
            const QRect united = b1.united(b2);
            if (united != b1) {
                std::vector<uint8_t> mergedCells(
                    static_cast<size_t>(united.width()) * static_cast<size_t>(united.height()),
                    0U);
                const auto& currentMap = _state->map();
                const int mapW = _mapSize.width();
                const int mapH = _mapSize.height();

                for (int y = united.top(); y <= united.bottom(); ++y) {
                    for (int x = united.left(); x <= united.right(); ++x) {
                        uint8_t origVal = 0U;
                        // Prefer b1 over b2 and currentMap: cells inside b1 were already
                        // mutated during earlier drag steps, so only b1 holds their true
                        // pre-stroke tile indices.
                        if (b1.contains(x, y)) {
                            const size_t idx1 = static_cast<size_t>(y - b1.top())
                                    * static_cast<size_t>(b1.width())
                                + static_cast<size_t>(x - b1.left());
                            origVal = _before.cells[idx1];
                        } else if (b2.contains(x, y)) {
                            const size_t idx2 = static_cast<size_t>(y - b2.top())
                                    * static_cast<size_t>(b2.width())
                                + static_cast<size_t>(x - b2.left());
                            origVal = p->_before.cells[idx2];
                        } else if (x >= 0 && x < mapW && y >= 0 && y < mapH) {
                            origVal = currentMap[static_cast<size_t>(y) * static_cast<size_t>(mapW)
                                + static_cast<size_t>(x)];
                        }
                        const size_t dstIdx = static_cast<size_t>(y - united.top())
                                * static_cast<size_t>(united.width())
                            + static_cast<size_t>(x - united.left());
                        mergedCells[dstIdx] = origVal;
                    }
                }
                _before.bounds = united;
                _before.cells = std::move(mergedCells);
            }
        }
    }

    _points.append(p->_points);
    return true;
}
