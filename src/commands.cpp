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

#include <QObject>
#include <QDebug>

#include "commands.h"

// Paint Tile
PaintTileCommand::PaintTileCommand(State *state, int tileIndex, const QPoint& position, int pen, bool mergeable, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    Q_ASSERT(position.x()<State::MAX_TILE_WIDTH*8 && position.y()<State::MAX_TILE_HEIGHT*8 && "Invalid position");

    _state = state;
    _tileIndex = tileIndex;
    _pen = pen;
    _mergeable = mergeable;
    _points.append(position);

    setText(QObject::tr("Paint #%1").arg(_tileIndex));
}

void PaintTileCommand::undo()
{
    _state->copyCharToIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
}

void PaintTileCommand::redo()
{
    _state->copyCharFromIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));

    for (int i = 0; i < _points.size(); ++i) {
        _state->tileSetPen(_tileIndex, _points.at(i), _pen);
    }
}

bool PaintTileCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;

    auto p = static_cast<const PaintTileCommand*>(other);

    if (_pen != p->_pen || _tileIndex != p->_tileIndex || !p->_mergeable)
        return false;

    _points.append(p->_points);

    return true;
}


// ClearTileCommand

ClearTileCommand::ClearTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Clear #%1").arg(_tileIndex));
}

void ClearTileCommand::undo()
{
    _state->copyCharToIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
}

void ClearTileCommand::redo()
{
    _state->copyCharFromIndex(_tileIndex, (quint8*)&_buffer, sizeof(_buffer));
    _state->tileClear(_tileIndex);
}

// PasteCommand

PasteCommand::PasteCommand(State *state, int charIndex, const State::CopyRange& copyRange, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _charIndex = charIndex;
    _state = state;
    _copyRange = copyRange;

    setText(QObject::tr("Paste #%1").arg(_charIndex));
}

void PasteCommand::undo()
{
    State::CopyRange reversedCopyRange;
    memcpy(&reversedCopyRange, &_copyRange, sizeof(State::CopyRange));
    reversedCopyRange.offset = _charIndex;

    _state->paste(_charIndex, reversedCopyRange, _origBuffer);
}

void PasteCommand::redo()
{
    memcpy(_copyBuffer, _state->getCopyCharsetBuffer(), sizeof(_copyBuffer));
    memcpy(_origBuffer, _state->getCharsetBuffer(), sizeof(_origBuffer));
    _state->paste(_charIndex, _copyRange, _copyBuffer);
}

// FlipTileHCommand

FlipTileHCommand::FlipTileHCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Flip Horizontally #%1").arg(_tileIndex));
}

void FlipTileHCommand::undo()
{
    _state->tileFlipHorizontally(_tileIndex);
}

void FlipTileHCommand::redo()
{
    _state->tileFlipHorizontally(_tileIndex);
}

// FlipTileVCommand

FlipTileVCommand::FlipTileVCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Flip Vertically #%1").arg(_tileIndex));
}

void FlipTileVCommand::undo()
{
    _state->tileFlipVertically(_tileIndex);
}

void FlipTileVCommand::redo()
{
    _state->tileFlipVertically(_tileIndex);
}

// RotateTileCommand

RotateTileCommand::RotateTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Rotate #%1").arg(_tileIndex));
}

void RotateTileCommand::undo()
{
    _state->tileRotate(_tileIndex);
    _state->tileRotate(_tileIndex);
    _state->tileRotate(_tileIndex);
}

void RotateTileCommand::redo()
{
    _state->tileRotate(_tileIndex);
}

// InvertTile

InvertTileCommand::InvertTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Invert #%1").arg(_tileIndex));
}

void InvertTileCommand::undo()
{
    _state->tileInvert(_tileIndex);
}

void InvertTileCommand::redo()
{
    _state->tileInvert(_tileIndex);
}

// Shift left

ShiftLeftTileCommand::ShiftLeftTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Shift Left #%1").arg(_tileIndex));
}

void ShiftLeftTileCommand::undo()
{
    _state->tileShiftRight(_tileIndex);
}

void ShiftLeftTileCommand::redo()
{
    _state->tileShiftLeft(_tileIndex);
}

// Shift Right

ShiftRightTileCommand::ShiftRightTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Shift Right #%1").arg(_tileIndex));
}

void ShiftRightTileCommand::undo()
{
    _state->tileShiftLeft(_tileIndex);
}

void ShiftRightTileCommand::redo()
{
    _state->tileShiftRight(_tileIndex);
}

// Shift Up

ShiftUpTileCommand::ShiftUpTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Shift Up #%1").arg(_tileIndex));
}

void ShiftUpTileCommand::undo()
{
    _state->tileShiftDown(_tileIndex);
}

void ShiftUpTileCommand::redo()
{
    _state->tileShiftUp(_tileIndex);
}

// Shift Down

ShiftDownTileCommand::ShiftDownTileCommand(State *state, int tileIndex, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _tileIndex = tileIndex;
    _state = state;

    setText(QObject::tr("Shift Down #%1").arg(_tileIndex));
}

void ShiftDownTileCommand::undo()
{
    _state->tileShiftUp(_tileIndex);
}

void ShiftDownTileCommand::redo()
{
    _state->tileShiftDown(_tileIndex);
}

// SetTilePropertiesCommand

SetTilePropertiesCommand::SetTilePropertiesCommand(State *state, const State::TileProperties& properties, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _state = state;
    _new = properties;

    setText(QObject::tr("Tile Properties %1x%2 - %3")
            .arg(properties.size.width())
            .arg(properties.size.height())
            .arg(properties.interleaved)
            );
}

void SetTilePropertiesCommand::undo()
{
    _state->setTileProperties(_old);
}

void SetTilePropertiesCommand::redo()
{
    _old = _state->getTileProperties();
    _state->setTileProperties(_new);
}


// SetMulticolorModeCommand

SetMulticolorModeCommand::SetMulticolorModeCommand(State *state, bool multicolorEnabled, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    _state = state;
    _new = multicolorEnabled;

    if (_new)
        setText(QObject::tr("Multicolor enabled"));
    else
        setText(QObject::tr("Multicolor disabled"));
}

void SetMulticolorModeCommand::undo()
{
    _state->setMulticolorMode(_old);
}

void SetMulticolorModeCommand::redo()
{
    _old = _state->isMulticolorMode();
    _state->setMulticolorMode(_new);
}

// SetColorCommand

SetColorCommand::SetColorCommand(State *state, int color, int pen, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    setText(QObject::tr("Color[%1] = %2")
            .arg(pen)
            .arg(color)
            );

    _state = state;
    _pen = pen;
    _new = color;
}

void SetColorCommand::undo()
{
    _state->setColorForPen(_pen, _old);
}

void SetColorCommand::redo()
{
    _old = _state->getColorForPen(_pen);
    _state->setColorForPen(_pen, _new);
}
