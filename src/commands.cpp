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
    _state->copyTileToIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
}

void PaintTileCommand::redo()
{
    _state->copyTileFromIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));

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
    _state->copyTileToIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
}

void ClearTileCommand::redo()
{
    _state->copyTileFromIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
    _state->_tileClear(_tileIndex);
}

// PasteCommand

PasteCommand::PasteCommand(State* state, int charIndex, const State::CopyRange& copyRange, const quint8* buffer, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _charIndex(charIndex)
    , _copyBuffer(nullptr)
    , _origBuffer(nullptr)
    , _copyRange(copyRange)
{

    int sizeToCopy = -1;
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES) {
        sizeToCopy = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
        Q_ASSERT(copyRange.bufferSize == sizeToCopy && "Invalid bufferSize");

        _copyBuffer = (quint8*)malloc(sizeToCopy);
        _origBuffer = (quint8*)malloc(sizeToCopy);
    } else /* MAP */
    {
        sizeToCopy = state->getMapSize().width() * state->getMapSize().height();
        _origBuffer = (quint8*)malloc(sizeToCopy);
        _copyBuffer = (quint8*)malloc(copyRange.bufferSize);
    }

    memcpy(_copyBuffer, buffer, copyRange.bufferSize);

    static const QString types[] = {
        QObject::tr("Chars"),
        QObject::tr("Tiles"),
        QObject::tr("Map")
    };

    setText(QObject::tr("Paste %1").arg(types[_copyRange.type]));
}

PasteCommand::~PasteCommand()
{
    free(_copyBuffer);
    free(_origBuffer);
}

void PasteCommand::undo()
{
    State::CopyRange reversedCopyRange = _copyRange;

    // FIXME: doesn't work when tiles are copied/paste to/from different tile properties
    if (_copyRange.type == State::CopyRange::TILES && reversedCopyRange.tileProperties.interleaved == 1)
        reversedCopyRange.offset = _charIndex / (reversedCopyRange.tileProperties.size.width() * reversedCopyRange.tileProperties.size.height());
    else
        reversedCopyRange.offset = _charIndex;

    _state->_paste(_charIndex, reversedCopyRange, _origBuffer);
}

void PasteCommand::redo()
{
    if (_copyRange.type == State::CopyRange::CHARS || _copyRange.type == State::CopyRange::TILES) {
        memcpy(_origBuffer, _state->getCharsetBuffer(), State::CHAR_BUFFER_SIZE);
        memcpy(_origBuffer + State::CHAR_BUFFER_SIZE, _state->getTileColors(), State::TILE_COLORS_BUFFER_SIZE);
    } else /* MAP */
    {
        memcpy(_origBuffer, _state->getMapBuffer(), _state->getMapSize().width() * _state->getMapSize().height());
    }
    _state->_paste(_charIndex, _copyRange, _copyBuffer);
}

// CutCommand

CutCommand::CutCommand(State* state, const State::CopyRange& copyRange, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _zeroBuffer(nullptr)
    , _origBuffer(nullptr)
    , _copyRange(copyRange)
{
    int sizeToCopy = -1;
    if (copyRange.type == State::CopyRange::CHARS || copyRange.type == State::CopyRange::TILES) {
        sizeToCopy = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
        _zeroBuffer = (quint8*)malloc(sizeToCopy);
        _origBuffer = (quint8*)malloc(sizeToCopy);

        memset(_zeroBuffer, 0, sizeToCopy);
    } else /* MAP */
    {
        sizeToCopy = state->getMapSize().width() * state->getMapSize().height();
        _zeroBuffer = (quint8*)malloc(sizeToCopy);
        _origBuffer = (quint8*)malloc(sizeToCopy);

        memset(_zeroBuffer, 0 /*state->getTileIndex()*/, sizeToCopy);
    }

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

CutCommand::~CutCommand()
{
    free(_zeroBuffer);
    free(_origBuffer);
}

void CutCommand::undo()
{
    State::CopyRange reversedCopyRange = _copyRange;

    // FIXME: doesn't work when tiles are copied/paste to/from different tile properties
    if (_copyRange.type == State::CopyRange::TILES && reversedCopyRange.tileProperties.interleaved == 1)
        reversedCopyRange.offset = _charIndex / (reversedCopyRange.tileProperties.size.width() * reversedCopyRange.tileProperties.size.height());
    else
        reversedCopyRange.offset = _charIndex;

    _state->_paste(_charIndex, reversedCopyRange, _origBuffer);
}

void CutCommand::redo()
{
    if (_copyRange.type == State::CopyRange::CHARS || _copyRange.type == State::CopyRange::TILES) {
        memcpy(_origBuffer, _state->getCharsetBuffer(), State::CHAR_BUFFER_SIZE);
        memcpy(_origBuffer + State::CHAR_BUFFER_SIZE, _state->getTileColors(), State::TILE_COLORS_BUFFER_SIZE);
    } else /* MAP */
    {
        memcpy(_origBuffer, _state->getMapBuffer(), _state->getMapSize().width() * _state->getMapSize().height());
    }
    _state->_paste(_charIndex, _copyRange, _zeroBuffer);
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
    _oldMap = (quint8*)malloc(_old.width() * _old.height());
    Q_ASSERT(_oldMap && "No more memory");

    memcpy(_oldMap, _state->_map, _old.width() * _old.height());

    setText(QObject::tr("Map Size %1x%2")
                .arg(mapSize.width())
                .arg(mapSize.height()));
}
SetMapSizeCommand::~SetMapSizeCommand()
{
    free(_oldMap);
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
FillMapCommand::FillMapCommand(State* state, const QPoint& coord, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _coord(coord)
    , _tileIdx(tileIdx)
    , _oldMap(nullptr)
{
    setText(QObject::tr("Map Fill"));
}

FillMapCommand::~FillMapCommand()
{
    free(_oldMap);
}

void FillMapCommand::undo()
{
    _state->_setMap(_oldMap, _mapSize);
}

void FillMapCommand::redo()
{
    if (!_oldMap) {
        _mapSize = _state->getMapSize();
        _oldMap = (quint8*)malloc(_mapSize.width() * _mapSize.height());
        memcpy(_oldMap, _state->getMapBuffer(), _mapSize.width() * _mapSize.height());
    }
    _state->_mapFill(_coord, _tileIdx);
}

// ClearMapCommand
ClearMapCommand::ClearMapCommand(State* state, int tileIdx, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tileIdx)
    , _oldMap(nullptr)
{
    setText(QObject::tr("Map Clear"));
}

ClearMapCommand::~ClearMapCommand()
{
    free(_oldMap);
}

void ClearMapCommand::undo()
{
    _state->_setMap(_oldMap, _mapSize);
}

void ClearMapCommand::redo()
{
    if (!_oldMap) {
        _mapSize = _state->getMapSize();
        _oldMap = (quint8*)malloc(_mapSize.width() * _mapSize.height());
        memcpy(_oldMap, _state->getMapBuffer(), _mapSize.width() * _mapSize.height());
    }
    _state->_mapClear(_tileIdx);
}

// PaintMapCommand
PaintMapCommand::PaintMapCommand(State* state, const QPoint& position, int tileIdx, bool mergeable, QUndoCommand* parent)
    : QUndoCommand(parent)
    , _state(state)
    , _tileIdx(tileIdx)
    , _mergeable(mergeable)
    , _oldMap(nullptr)
{
    _points.append(position);

    setText(QObject::tr("Paint Map #%1").arg(_tileIdx));
}

PaintMapCommand::~PaintMapCommand()
{
    free(_oldMap);
}

void PaintMapCommand::undo()
{
    _state->_setMap(_oldMap, _mapSize);
}

void PaintMapCommand::redo()
{
    if (!_oldMap) {
        _mapSize = _state->getMapSize();
        _oldMap = (quint8*)malloc(_mapSize.width() * _mapSize.height());
    }
    memcpy(_oldMap, _state->getMapBuffer(), _mapSize.width() * _mapSize.height());

    for (auto _point : _points) {
        _state->_mapPaint(_point, _tileIdx);
    }
}

bool PaintMapCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;

    auto p = dynamic_cast<const PaintMapCommand*>(other);

    if (_tileIdx != p->_tileIdx || !p->_mergeable)
        return false;

    _points.append(p->_points);

    return true;
}
