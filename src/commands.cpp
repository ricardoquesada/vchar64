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

// QUndoCommand undo/redo snapshot capture, stroke coalescing, and restoration.
//
// Key undo invariants enforced here:
// - Fixed-size charset/color snapshots: PasteCommand and CutCommand assign
//   fixed 2,304-byte buffers (CharsetColorSnapshot) rather than
//   std::back_inserter, preventing unbounded vector growth across repeated redo
//   cycles.
// - Pre-mutation flood-fill patches: FillMapCommand::redo() captures _before
//   over _plan.bounds before writing _plan.coords, preserving untouched obstacle
//   walls and interior pockets on undo().
// - Earliest-first brush union: PaintMapCommand::mergeWith() checks the
//   earlier patch before the later patch and current map, preserving
//   pre-stroke cell values across self-overlapping drags.

#include "commands.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUndoCommand>

namespace {

constexpr size_t kCharsetAndColorBytes = static_cast<size_t>(
    State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE);

constexpr std::array<uint8_t, kCharsetAndColorBytes> kZeroCharsAndColors { };

// Intersects a destination [block_width x row_count] map paste/cut region at
// `linear_offset` with `map_size`, clamping horizontal and vertical extents to
// [0 .. map_width - dst_x] and [0 .. map_height - dst_y] so edge selections
// never wrap onto the next row or read/write past `_map.size()`.
QRect ComputeClippedMapRect(
    const QSize& map_size,
    int linear_offset,
    int block_width,
    int row_count) noexcept
{
    const int map_width = map_size.width();
    const int map_height = map_size.height();
    if (map_width <= 0 || map_height <= 0 || linear_offset < 0 || block_width <= 0 || row_count <= 0) {
        return { };
    }
    const uint64_t total_cells = static_cast<uint64_t>(map_width) * static_cast<uint64_t>(map_height);
    if (static_cast<uint64_t>(linear_offset) >= total_cells) {
        return { };
    }

    const int dst_x = linear_offset % map_width;
    const int dst_y = linear_offset / map_width;
    const int cols = std::clamp(block_width, 0, map_width - dst_x);
    const int rows = std::clamp(row_count, 0, map_height - dst_y);
    if (cols <= 0 || rows <= 0) {
        return { };
    }
    return QRect(dst_x, dst_y, cols, rows);
}

QString CopyBufferTypeLabel(State::CopyRange::BufferType type)
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

// Extracts a strided map slice into a tightly packed [blockSize x count]
// subgrid so PasteCommand retains only the copied region on the undo stack.
void NormalizeMapCopyBuffer(
    const uint8_t* buffer,
    State::CopyRange& copy_range,
    std::vector<uint8_t>& out_buffer)
{
    if (buffer == nullptr || copy_range.offset < 0 || copy_range.blockSize <= 0 || copy_range.count <= 0) {
        return;
    }

    const uint64_t tight_cells = static_cast<uint64_t>(copy_range.blockSize)
        * static_cast<uint64_t>(copy_range.count);
    if (tight_cells == 0ULL || tight_cells > State::kMaxMapCells) {
        return;
    }

    const size_t tight_size = static_cast<size_t>(tight_cells);
    out_buffer.assign(tight_size, 0U);

    const uint64_t row_width_cells = (copy_range.sourceRowWidth >= copy_range.blockSize)
        ? (static_cast<uint64_t>(copy_range.sourceRowWidth)
              * static_cast<uint64_t>(copy_range.count))
        : 0ULL;
    const bool has_explicit_row_width = (copy_range.offset == 0 && copy_range.skip <= 0)
        && (row_width_cells > 0ULL)
        && (copy_range.bufferSize == static_cast<int>(row_width_cells));
    const bool is_already_tight = (copy_range.offset == 0)
        && ((copy_range.bufferSize == static_cast<int>(tight_size))
            || (copy_range.bufferSize <= 0 && copy_range.skip <= 0));
    const int src_stride = has_explicit_row_width
        ? copy_range.sourceRowWidth
        : (is_already_tight
                  ? copy_range.blockSize
                  : (copy_range.blockSize + std::max(0, copy_range.skip)));

    for (int row = 0; row < copy_range.count; ++row) {
        const uint64_t src_row_offset = static_cast<uint64_t>(copy_range.offset)
            + static_cast<uint64_t>(row) * static_cast<uint64_t>(src_stride);
        size_t cols_to_read = static_cast<size_t>(copy_range.blockSize);
        if (copy_range.bufferSize > 0) {
            if (src_row_offset >= static_cast<uint64_t>(copy_range.bufferSize)) {
                break;
            }
            const size_t remaining_bytes = static_cast<size_t>(
                static_cast<uint64_t>(copy_range.bufferSize) - src_row_offset);
            cols_to_read = std::min(cols_to_read, remaining_bytes);
        }
        std::copy_n(
            buffer + static_cast<size_t>(src_row_offset),
            cols_to_read,
            out_buffer.data() + static_cast<size_t>(row) * static_cast<size_t>(copy_range.blockSize));
    }

    copy_range.offset = 0;
    copy_range.skip = 0;
    copy_range.sourceRowWidth = copy_range.blockSize;
    copy_range.bufferSize = static_cast<int>(tight_size);
}

// Expands `before` to cover the union with `incoming_before`, preferring
// already-captured cells in `before` over `incoming_before` and `current_map`
// so pre-stroke values survive self-overlapping drag strokes.
void MergeBeforePatch(
    const State::MapBuffer& current_map,
    const QSize& map_size,
    const MapRegionPatch& incoming_before,
    MapRegionPatch& before,
    bool& captured)
{
    if (incoming_before.bounds.isEmpty()) {
        return;
    }
    if (before.bounds.isEmpty()) {
        before = incoming_before;
        captured = true;
        return;
    }

    const QRect first_bounds = before.bounds;
    const QRect second_bounds = incoming_before.bounds;
    const QRect united = first_bounds.united(second_bounds);
    if (united == first_bounds) {
        return;
    }

    std::vector<uint8_t> merged_cells(
        static_cast<size_t>(united.width()) * static_cast<size_t>(united.height()),
        0U);
    const int map_width = map_size.width();
    const int map_height = map_size.height();

    for (int y = united.top(); y <= united.bottom(); ++y) {
        for (int x = united.left(); x <= united.right(); ++x) {
            uint8_t original_value = 0U;
            if (first_bounds.contains(x, y)) {
                const size_t first_index = static_cast<size_t>(y - first_bounds.top())
                        * static_cast<size_t>(first_bounds.width())
                    + static_cast<size_t>(x - first_bounds.left());
                original_value = before.cells[first_index];
            } else if (second_bounds.contains(x, y)) {
                const size_t second_index = static_cast<size_t>(y - second_bounds.top())
                        * static_cast<size_t>(second_bounds.width())
                    + static_cast<size_t>(x - second_bounds.left());
                original_value = incoming_before.cells[second_index];
            } else if (x >= 0 && x < map_width && y >= 0 && y < map_height) {
                original_value = current_map[static_cast<size_t>(y) * static_cast<size_t>(map_width)
                    + static_cast<size_t>(x)];
            }
            const size_t dst_index = static_cast<size_t>(y - united.top())
                    * static_cast<size_t>(united.width())
                + static_cast<size_t>(x - united.left());
            merged_cells[dst_index] = original_value;
        }
    }

    before.bounds = united;
    before.cells = std::move(merged_cells);
}

} // namespace

PaintTileCommand::PaintTileCommand(
    State* state,
    int tile_index,
    const QPoint& position,
    int pen,
    bool mergeable,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

    const PaintTileCommand* const other_cmd = dynamic_cast<const PaintTileCommand*>(other);
    if (other_cmd == nullptr
        || _state != other_cmd->_state
        || _pen != other_cmd->_pen
        || _tileIndex != other_cmd->_tileIndex
        || !other_cmd->_mergeable) {
        return false;
    }

    _points.append(other_cmd->_points);
    return true;
}

PasteCommand::PasteCommand(
    State* state,
    int offset,
    const State::CopyRange& copy_range,
    const uint8_t* buffer,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _offset(offset)
    , _copyRange(copy_range)
{
    if (copy_range.type == State::CopyRange::CHARS || copy_range.type == State::CopyRange::TILES) {
        _copyBuffer.assign(kCharsetAndColorBytes, 0U);
        if (buffer != nullptr) {
            const size_t available_bytes = (copy_range.bufferSize > 0)
                ? std::min(static_cast<size_t>(copy_range.bufferSize), kCharsetAndColorBytes)
                : kCharsetAndColorBytes;
            std::copy_n(buffer, available_bytes, _copyBuffer.data());
        }
        _copyRange.bufferSize = static_cast<int>(kCharsetAndColorBytes);
    } else {
        NormalizeMapCopyBuffer(buffer, _copyRange, _copyBuffer);
    }

    setText(QObject::tr("Paste %1").arg(CopyBufferTypeLabel(_copyRange.type)));
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
            const QRect affected = ComputeClippedMapRect(
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
        return;
    }

    if (!_captured) {
        _beforeChars.charset = _state->charset();
        _beforeChars.tileColors = _state->tileColors();
        _captured = true;
    }
    _state->_paste(_offset, _copyRange, _copyBuffer.data());
}

CutCommand::CutCommand(
    State* state,
    const State::CopyRange& copy_range,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _copyRange(copy_range)
{
    if (_copyRange.type == State::CopyRange::TILES && _copyRange.tileProperties.interleaved == 1) {
        const int tile_area = std::max(
            1,
            _copyRange.tileProperties.size.width() * _copyRange.tileProperties.size.height());
        _offset = _copyRange.offset * tile_area;
    } else {
        _offset = _copyRange.offset;
    }

    setText(QObject::tr("Cut %1").arg(CopyBufferTypeLabel(_copyRange.type)));
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
        const QRect affected = ComputeClippedMapRect(
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
        return;
    }

    if (!_captured) {
        _beforeChars.charset = _state->charset();
        _beforeChars.tileColors = _state->tileColors();
        _captured = true;
    }
    State::CopyRange range_with_size = _copyRange;
    range_with_size.bufferSize = static_cast<int>(kZeroCharsAndColors.size());
    _state->_paste(_offset, range_with_size, kZeroCharsAndColors.data());
}

FlipTileHCommand::FlipTileHCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

FlipTileVCommand::FlipTileVCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

ShiftLeftTileCommand::ShiftLeftTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

ShiftRightTileCommand::ShiftRightTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

ShiftUpTileCommand::ShiftUpTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

ShiftDownTileCommand::ShiftDownTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

RotateTileCommand::RotateTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

InvertTileCommand::InvertTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

ClearTileCommand::ClearTileCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tile_index)
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

SetCharacterModeCommand::SetCharacterModeCommand(
    State* state,
    State::CharacterMode mode,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(mode)
{
    switch (_new) {
    case State::CHARACTER_MODE_HIRES:
        setText(QObject::tr("Character Mode: Hi-Res"));
        break;
    case State::CHARACTER_MODE_MULTICOLOR:
        setText(QObject::tr("Character Mode: Multicolor"));
        break;
    case State::CHARACTER_MODE_EXTENDED_COLOR:
        setText(QObject::tr("Character Mode: Extended Color"));
        break;
    }
}

void SetCharacterModeCommand::undo()
{
    if (_captured) {
        _state->_setCharacterMode(_old);
    }
}

void SetCharacterModeCommand::redo()
{
    if (!_captured) {
        _old = _state->characterMode();
        _captured = true;
    }
    _state->_setCharacterMode(_new);
}

SetMulticolorModeCommand::SetMulticolorModeCommand(
    State* state,
    bool multicolor_enabled,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _newMode(multicolor_enabled ? State::CHARACTER_MODE_MULTICOLOR : State::CHARACTER_MODE_HIRES)
{
    if (multicolor_enabled) {
        setText(QObject::tr("Multicolor enabled"));
    } else {
        setText(QObject::tr("Multicolor disabled"));
    }
}

void SetMulticolorModeCommand::undo()
{
    if (_captured) {
        _state->_setCharacterMode(_oldMode);
    }
}

void SetMulticolorModeCommand::redo()
{
    if (!_captured) {
        _oldMode = _state->characterMode();
        _captured = true;
    }
    _state->_setCharacterMode(_newMode);
}

SetColorCommand::SetColorCommand(
    State* state,
    int color,
    int pen,
    int tile_index,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _pen(pen)
    , _new(static_cast<int>(static_cast<unsigned int>(color) & 0x0FU))
    , _tileIdx(tile_index)
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

SetMapSizeCommand::SetMapSizeCommand(State* state, const QSize& map_size, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(map_size)
    , _old(state->getMapSize())
    , _oldMap(state->getMapBuffer())
{
    setText(QObject::tr("Map Size %1x%2")
            .arg(map_size.width())
            .arg(map_size.height()));
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

FillMapCommand::FillMapCommand(
    State* state,
    const QPoint& coord,
    int tile_index,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _coord(coord)
    , _tileIdx(static_cast<uint8_t>(static_cast<unsigned int>(tile_index) & 0xFFU))
{
    setText(QObject::tr("Map Fill"));
}

FillMapCommand::FillMapCommand(
    State* state,
    State::FloodFillPlan plan,
    uint8_t replacement_tile,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(replacement_tile)
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
            const size_t start_index = static_cast<size_t>(_coord.y())
                    * static_cast<size_t>(_state->mapSize().width())
                + static_cast<size_t>(_coord.x());
            const uint8_t target_tile = _state->map()[start_index];
            _plan = _state->collectFloodFillRegion(_coord, target_tile, _tileIdx);
        }
        _planReady = true;
    }

    if (_plan.coords.empty()) {
        return;
    }

    // Capture _before from the untouched _map BEFORE writing _plan.coords so
    // pre-existing _tileIdx obstacle walls and enclosed pockets inside
    // _plan.bounds remain bit-exact across undo()/redo().
    if (!_captured) {
        _before = _state->captureMapPatch(_plan.bounds);
        _captured = true;
    }

    _state->_applyFloodFillCoords(_plan.coords, _tileIdx);
}

PaintMapCommand::PaintMapCommand(
    State* state,
    const QPoint& position,
    int tile_index,
    bool mergeable,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(static_cast<uint8_t>(static_cast<unsigned int>(tile_index) & 0xFFU))
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

    const PaintMapCommand* const other_cmd = dynamic_cast<const PaintMapCommand*>(other);
    if (other_cmd == nullptr
        || _state != other_cmd->_state
        || _tileIdx != other_cmd->_tileIdx
        || !other_cmd->_mergeable
        || _mapSize != other_cmd->_mapSize
        || _state->mapSize() != _mapSize) {
        return false;
    }

    MergeBeforePatch(_state->map(), _mapSize, other_cmd->_before, _before, _captured);
    _points.append(other_cmd->_points);
    return true;
}

ClearMapCommand::ClearMapCommand(State* state, int tile_index, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tile_index)
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
