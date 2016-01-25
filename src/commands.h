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

#include <QUndoCommand>
#include <QPoint>
#include <QList>

#include "state.h"

// id
enum UndoCommands {
    Cmd_PaintTile,
    Cmd_PaintMap,
};

//
class PaintTileCommand : public QUndoCommand
{
public:
    PaintTileCommand(State *state, int tileIndex, const QPoint& position, int pen, bool mergeable, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;
    int id() const Q_DECL_OVERRIDE { return Cmd_PaintTile; }
    bool mergeWith(const QUndoCommand *other) Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
    int _pen;
    quint8 _buffer[State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8];
    bool _mergeable;
    QList<QPoint> _points;
};

//class PasteTileCommand : public QUndoCommand
//{
//public:
//    PasteTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
//    void undo() Q_DECL_OVERRIDE;
//    void redo() Q_DECL_OVERRIDE;

//private:
//    State* _state;
//    int _tileIndex;
//    quint8 _buffer[State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8];
//};

class PasteCommand : public QUndoCommand
{
public:
    PasteCommand(State *state, int charIndex, const State::CopyRange* copyRange, const quint8* charsetBuffer, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _charIndex;

    quint8 _copyBuffer[State::CHAR_BUFFER_SIZE + State::TILE_ATTRIBS_BUFFER_SIZE];
    quint8 _origBuffer[State::CHAR_BUFFER_SIZE + State::TILE_ATTRIBS_BUFFER_SIZE];
    State::CopyRange _copyRange;
};

class CutCommand : public QUndoCommand
{
public:
    CutCommand(State *state, int charIndex, const State::CopyRange &copyRange, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _charIndex;

    quint8 _zeroBuffer[State::CHAR_BUFFER_SIZE];
    quint8 _origBuffer[State::CHAR_BUFFER_SIZE];
    State::CopyRange _copyRange;
};

class FlipTileHCommand : public QUndoCommand
{
public:
    FlipTileHCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class FlipTileVCommand : public QUndoCommand
{
public:
    FlipTileVCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class ShiftLeftTileCommand : public QUndoCommand
{
public:
    ShiftLeftTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};


class ShiftRightTileCommand : public QUndoCommand
{
public:
    ShiftRightTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class ShiftUpTileCommand : public QUndoCommand
{
public:
    ShiftUpTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class ShiftDownTileCommand : public QUndoCommand
{
public:
    ShiftDownTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class RotateTileCommand : public QUndoCommand
{
public:
    RotateTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class InvertTileCommand : public QUndoCommand
{
public:
    InvertTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
};

class ClearTileCommand : public QUndoCommand
{
public:
    ClearTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
    quint8 _buffer[State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8];
};


class SetTilePropertiesCommand : public QUndoCommand
{
public:
    SetTilePropertiesCommand(State *state, const State::TileProperties& properties, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    State::TileProperties _new;
    State::TileProperties _old;
};

//
class SetMulticolorModeCommand : public QUndoCommand
{
public:
    SetMulticolorModeCommand(State *state, bool multicolorEnabled, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    bool _old;
    bool _new;
};

// SetColorCommand
class SetColorCommand : public QUndoCommand
{
public:
    SetColorCommand(State *state, int color, int pen, int tileIdx, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _pen;
    int _old;
    int _new;
    int _tileIdx;
};

// SetForegroundColorMode
class SetForegroundColorMode : public QUndoCommand
{
public:
    SetForegroundColorMode(State *state, int mode, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _mode;
    int _oldMode;
};

class SetMapSizeCommand : public QUndoCommand
{
public:
    SetMapSizeCommand(State *state, const QSize& mapSize, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    QSize _new;
    QSize _old;
};

// FillMapCommand
class FillMapCommand : public QUndoCommand
{
public:
    FillMapCommand(State *state, const QPoint& coord, int tileIdx, QUndoCommand *parent = nullptr);
    virtual ~FillMapCommand();
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    QPoint _coord;
    int _tileIdx;
    quint8* _oldMap;
    QSize _mapSize;
};

// PaintMapCommand
class PaintMapCommand : public QUndoCommand
{
public:
    PaintMapCommand(State *state, const QPoint& position, int tileIdx, bool mergeable, QUndoCommand *parent = nullptr);
    virtual ~PaintMapCommand();
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;
    int id() const Q_DECL_OVERRIDE { return Cmd_PaintMap; }
    bool mergeWith(const QUndoCommand* other) Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIdx;
    bool _mergeable;
    QList<QPoint> _points;
    quint8* _oldMap;
    QSize _mapSize;
};

// ClearMapCommand
class ClearMapCommand : public QUndoCommand
{
public:
    ClearMapCommand(State *state, int tileIdx, QUndoCommand *parent = nullptr);
    virtual ~ClearMapCommand();
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIdx;
    quint8* _oldMap;
    QSize _mapSize;

};
