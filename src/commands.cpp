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

#include <cstring>

#include <QDebug>
#include <QObject>

#include "commands.h"

// Paint Tile
PaintTileCommand::PaintTileCommand(State* state, int tileIndex, const QPoint& position, int pen, bool mergeable, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
    , _pen(pen)
    , _buffer {}
    , _mergeable(mergeable)
{
    Q_ASSERT(position.x() < State::MAX_TILE_WIDTH * 8 && position.y() < State::MAX_TILE_HEIGHT * 8 && "Invalid position");

    _points.append(position);
    setText(QObject::tr("Paint Tile #%1").arg(_tileIndex));
}

void PaintTileCommand::undo()
{
    _state->copyTileToIndex(_tileIndex, &_buffer[0], sizeof(_buffer));
}

void PaintTileCommand::redo()
{
    _state->copyTileFromIndex(_tileIndex, &_buffer[0], sizeof(_buffer));

    for (auto point : _points) {
        _state->_tileSetPen(_tileIndex, point, _pen);
    }
}

bool PaintTileCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;

    auto p = dynamic_cast<const PaintTileCommand*>(other);

    if (_pen != p->_pen || _tileIndex != p->_tileIndex || !p->_mergeable)
        return false;

    _points.append(p->_points);

    return true;
}

// ClearTileCommand

ClearTileCommand::ClearTileCommand(State* state, int tileIndex, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIndex(tileIndex)
    , _buffer {}
{
    setText(QObject::tr("Clear Tile #%1").arg(_tileIndex));
}

void ClearTileCommand::undo()
{
    _state->copyTileToIndex(_tileIndex, &_buffer[0], sizeof(_buffer));
}

void ClearTileCommand::redo()
{
    _state->copyTileFromIndex(_tileIndex, &_buffer[0], sizeof(_buffer));
    _state->_tileClear(_tileIndex);
}

// PasteCommand

PasteCommand::PasteCommand(State* state, int charIndex, const State::CopyRange& copyRange, const quint8* buffer, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _charIndex(charIndex)
    , _mapIdx(state->getMapIndex())
    , _copyRange(copyRange)
    , _origTileProperties(copyRange.tileProperties)
{

    int sizeToCopy = -1;
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES) {
        sizeToCopy = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
        Q_ASSERT(copyRange.bufferSize == sizeToCopy && "Invalid bufferSize");
        _copyBuffer.resize(sizeToCopy);
        _origBuffer.resize(sizeToCopy);
    } else {
        /* MAP */
        // the undo snapshot holds the map and its color area
        sizeToCopy = 2 * state->getMapSize().width() * state->getMapSize().height();
        _copyBuffer.resize(copyRange.bufferSize);
        _origBuffer.resize(sizeToCopy);
    }

    std::copy_n(buffer, copyRange.bufferSize, _copyBuffer.data());

    static const QString types[] = {
        QObject::tr("Chars"),
        QObject::tr("Tiles"),
        QObject::tr("Map")
    };

    setText(QObject::tr("Paste %1").arg(types[_copyRange.type]));
}

void PasteCommand::undo()
{
    if (_copyRange.type == State::CopyRange::MAP)
        _state->_setMapIndex(_mapIdx);

    State::CopyRange reversedCopyRange = _copyRange;

    // Use the original tile properties from when the copy was made
    reversedCopyRange.tileProperties = _origTileProperties;
    if (_copyRange.type == State::CopyRange::TILES && _origTileProperties.interleaved == 1)
        reversedCopyRange.offset = _charIndex / (_origTileProperties.size.width() * _origTileProperties.size.height());
    else
        reversedCopyRange.offset = _charIndex;

    if (_copyRange.type == State::CopyRange::MAP) {
        // the snapshot always carries the color area, even when the pasted
        // buffer did not (restoring unchanged colors is harmless)
        reversedCopyRange.tileProperties.interleaved = State::CopyRange::MAP_BUFFER_WITH_COLORS;
        reversedCopyRange.bufferSize = static_cast<int>(_origBuffer.size());
    }

    _state->_paste(_charIndex, reversedCopyRange, _origBuffer.data());
}

void PasteCommand::redo()
{
    if (_copyRange.type == State::CopyRange::CHARS || _copyRange.type == State::CopyRange::TILES) {
        std::copy_n(std::begin(_state->getCharsetBuffer()), State::CHAR_BUFFER_SIZE, std::begin(_origBuffer));
        std::copy_n(std::begin(_state->getTileColors()), State::TILE_COLORS_BUFFER_SIZE, std::back_inserter(_origBuffer));
    } else {
        /* MAP */
        _state->_setMapIndex(_mapIdx);
        _origBuffer = _state->getMapBuffer();
        const auto& colorMap = _state->getColorMapBuffer();
        _origBuffer.insert(_origBuffer.end(), colorMap.begin(), colorMap.end());
    }
    _state->_paste(_charIndex, _copyRange, _copyBuffer.data());
}

// CutCommand

CutCommand::CutCommand(State* state, const State::CopyRange& copyRange, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _mapIdx(state->getMapIndex())
    , _copyRange(copyRange)
    , _origTileProperties(copyRange.tileProperties)
{
    int sizeToCopy = -1;
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES) {
        sizeToCopy = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
        _zeroBuffer.resize(sizeToCopy);
    } else {
        /* MAP */
        // map and color area: cut clears tiles to 0 and colors to the default
        const int mapInBytes = state->getMapSize().width() * state->getMapSize().height();
        sizeToCopy = 2 * mapInBytes;
        _zeroBuffer.resize(sizeToCopy);
        std::fill(_zeroBuffer.begin() + mapInBytes, _zeroBuffer.end(), State::TILE_COLORS_DEFAULT);
    }
    _origBuffer.resize(sizeToCopy);

    // _charIndex: offset to be used for cut
    if (_copyRange.type == State::CopyRange::TILES && _copyRange.tileProperties.interleaved == 1)
        _charIndex = _copyRange.offset * (_copyRange.tileProperties.size.width() * _copyRange.tileProperties.size.height());
    else
        _charIndex = _copyRange.offset;

    static const QString types[] = {
        QObject::tr("Chars"),
        QObject::tr("Tiles"),
        QObject::tr("Map")
    };

    setText(QObject::tr("Cut %1").arg(types[_copyRange.type]));
}

void CutCommand::undo()
{
    if (_copyRange.type == State::CopyRange::MAP)
        _state->_setMapIndex(_mapIdx);

    State::CopyRange reversedCopyRange = _copyRange;

    // Use the original tile properties from when the copy was made
    reversedCopyRange.tileProperties = _origTileProperties;
    if (_copyRange.type == State::CopyRange::TILES && _origTileProperties.interleaved == 1)
        reversedCopyRange.offset = _charIndex / (_origTileProperties.size.width() * _origTileProperties.size.height());
    else
        reversedCopyRange.offset = _charIndex;

    if (_copyRange.type == State::CopyRange::MAP) {
        reversedCopyRange.tileProperties.interleaved = State::CopyRange::MAP_BUFFER_WITH_COLORS;
        reversedCopyRange.bufferSize = static_cast<int>(_origBuffer.size());
    }

    _state->_paste(_charIndex, reversedCopyRange, _origBuffer.data());
}

void CutCommand::redo()
{
    if (_copyRange.type == State::CopyRange::CHARS || _copyRange.type == State::CopyRange::TILES) {
        std::copy_n(std::begin(_state->getCharsetBuffer()), State::CHAR_BUFFER_SIZE, std::begin(_origBuffer));
        std::copy_n(std::begin(_state->getTileColors()), State::TILE_COLORS_BUFFER_SIZE, std::back_inserter(_origBuffer));
    } else {
        /* MAP */
        _state->_setMapIndex(_mapIdx);
        _origBuffer = _state->getMapBuffer();
        const auto& colorMap = _state->getColorMapBuffer();
        _origBuffer.insert(_origBuffer.end(), colorMap.begin(), colorMap.end());
    }
    _state->_paste(_charIndex, _copyRange, _zeroBuffer.data());
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
    _state->_tileRotate(_tileIndex);
    _state->_tileRotate(_tileIndex);
    _state->_tileRotate(_tileIndex);
}

void RotateTileCommand::redo()
{
    _state->_tileRotate(_tileIndex);
}

// InvertTile

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

// Shift left

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

// Shift Right

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

// Shift Up

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

// Shift Down

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

SetTilePropertiesCommand::SetTilePropertiesCommand(State* state, const State::TileProperties& properties, QUndoCommand* parent)
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
    _state->_setTileProperties(_old);
}

void SetTilePropertiesCommand::redo()
{
    _old = _state->getTileProperties();
    _state->_setTileProperties(_new);
}

// SetExportPropertiesCommand

SetExportPropertiesCommand::SetExportPropertiesCommand(State* state, const State::ExportProperties& properties, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(properties)
{
    setText(QObject::tr("Export Properties"));
}

void SetExportPropertiesCommand::undo()
{
    _state->_setExportProperties(_old);
}

void SetExportPropertiesCommand::redo()
{
    _old = _state->getExportProperties();
    _state->_setExportProperties(_new);
}

// SetMulticolorModeCommand

SetMulticolorModeCommand::SetMulticolorModeCommand(State* state, bool multicolorEnabled, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(multicolorEnabled)
{
    if (_new)
        setText(QObject::tr("Multicolor enabled"));
    else
        setText(QObject::tr("Multicolor disabled"));
}

void SetMulticolorModeCommand::undo()
{
    _state->_setMulticolorMode(_old);
}

void SetMulticolorModeCommand::redo()
{
    _old = _state->isMulticolorMode();
    _state->_setMulticolorMode(_new);
}

// SetColorCommand

SetColorCommand::SetColorCommand(State* state, int color, int pen, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _pen(pen)
    , _new(color)
    , _tileIdx(tileIdx)
{
    setText(QObject::tr("Color[%1] = %2")
            .arg(pen)
            .arg(color));
}

void SetColorCommand::undo()
{
    _state->_setColorForPen(_pen, _old, _tileIdx);
}

void SetColorCommand::redo()
{
    _old = _state->getColorForPen(_pen, _tileIdx);
    _state->_setColorForPen(_pen, _new, _tileIdx);
}

// SetForegroundColorMode

SetForegroundColorMode::SetForegroundColorMode(State* state, State::ForegroundColorMode mode, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _mode(mode)
{
    setText(QObject::tr("Foreground Mode = %1")
            .arg(mode));
}

void SetForegroundColorMode::undo()
{
    _state->_setForegroundColorMode(_oldMode);
}

void SetForegroundColorMode::redo()
{
    _oldMode = _state->getForegroundColorMode();
    _state->_setForegroundColorMode(_mode);
}

// SetMapSizeCommand

SetMapSizeCommand::SetMapSizeCommand(State* state, const QSize& mapSize, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _new(mapSize)
{
    _old = _state->getMapSize();
    // resizing affects all maps, so all of them must be restored on undo
    _oldMaps = _state->_maps;
    _oldColorMaps = _state->_colorMaps;

    setText(QObject::tr("Map Size %1x%2")
            .arg(mapSize.width())
            .arg(mapSize.height()));
}

void SetMapSizeCommand::undo()
{
    _state->_setMapSize(_old);
    _state->_setMaps(_oldMaps, _oldColorMaps, _old);
}

void SetMapSizeCommand::redo()
{
    _state->_setMapSize(_new);
}

// FillMapCommand
FillMapCommand::FillMapCommand(State* state, const QPoint& coord, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _coord(coord)
    , _tileIdx(tileIdx)
    , _mapIdx(state->getMapIndex())
{
    setText(QObject::tr("Map Fill"));
}

void FillMapCommand::undo()
{
    _state->_setMapIndex(_mapIdx);
    _state->_setMap(_oldMap, _mapSize);
    _state->_setColorMap(_oldColorMap, _mapSize);
}

void FillMapCommand::redo()
{
    _state->_setMapIndex(_mapIdx);
    if (_oldMap.empty()) {
        _mapSize = _state->getMapSize();
        _oldMap = _state->getMapBuffer();
        _oldColorMap = _state->getColorMapBuffer();
    }
    _state->_mapFill(_coord, _tileIdx);
}

// ClearMapCommand
ClearMapCommand::ClearMapCommand(State* state, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tileIdx)
    , _mapIdx(state->getMapIndex())
{
    setText(QObject::tr("Map Clear"));
}

void ClearMapCommand::undo()
{
    _state->_setMapIndex(_mapIdx);
    _state->_setMap(_oldMap, _mapSize);
    _state->_setColorMap(_oldColorMap, _mapSize);
}

void ClearMapCommand::redo()
{
    _state->_setMapIndex(_mapIdx);
    if (_oldMap.empty()) {
        _mapSize = _state->getMapSize();
        _oldMap = _state->getMapBuffer();
        _oldColorMap = _state->getColorMapBuffer();
    }
    _state->_mapClear(_tileIdx);
}

// PaintMapCommand
PaintMapCommand::PaintMapCommand(State* state, const QPoint& position, int tileIdx, bool mergeable, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tileIdx)
    , _mapIdx(state->getMapIndex())
    , _mergeable(mergeable)
{
    _points.append(position);

    setText(QObject::tr("Paint Map #%1").arg(_tileIdx));
}

void PaintMapCommand::undo()
{
    _state->_setMapIndex(_mapIdx);
    _state->_setMap(_oldMap, _mapSize);
    _state->_setColorMap(_oldColorMap, _mapSize);
}

void PaintMapCommand::redo()
{
    _state->_setMapIndex(_mapIdx);
    if (_oldMap.empty())
        _mapSize = _state->getMapSize();
    _oldMap = _state->getMapBuffer();
    _oldColorMap = _state->getColorMapBuffer();

    for (auto _point : _points) {
        _state->_mapPaint(_point, _tileIdx);
    }
}

bool PaintMapCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;

    auto p = dynamic_cast<const PaintMapCommand*>(other);

    if (_tileIdx != p->_tileIdx || _mapIdx != p->_mapIdx || !p->_mergeable)
        return false;

    _points.append(p->_points);

    return true;
}

// PaintColorMapCommand
PaintColorMapCommand::PaintColorMapCommand(State* state, const QPoint& position, int color, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _coord(position)
    , _color(color)
    , _mapIdx(state->getMapIndex())
{
    _oldColor = state->getColorFromMap(position);

    setText(QObject::tr("Paint Color #%1").arg(_color));
}

void PaintColorMapCommand::undo()
{
    _state->_setMapIndex(_mapIdx);
    _state->_colorMapPaint(_coord, _oldColor);
}

void PaintColorMapCommand::redo()
{
    _state->_setMapIndex(_mapIdx);
    _state->_colorMapPaint(_coord, _color);
}

// AddMapCommand
AddMapCommand::AddMapCommand(State* state, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
{
    setText(QObject::tr("Add Map"));
}

void AddMapCommand::undo()
{
    // the added map is always the last one
    _state->_mapDelete(_state->getMapCount() - 1);
}

void AddMapCommand::redo()
{
    _state->_mapAdd();
}

// DeleteMapCommand
DeleteMapCommand::DeleteMapCommand(State* state, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _mapIdx(state->getMapIndex())
{
    _oldMap = state->getMapBuffer();
    _oldColorMap = state->getColorMapBuffer();

    setText(QObject::tr("Delete Map #%1").arg(_mapIdx + 1));
}

void DeleteMapCommand::undo()
{
    _state->_mapInsert(_mapIdx, _oldMap, _oldColorMap);
}

void DeleteMapCommand::redo()
{
    _state->_mapDelete(_mapIdx);
}
