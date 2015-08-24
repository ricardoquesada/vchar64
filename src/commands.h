
#pragma once

#include <QUndoCommand>
#include <QPoint>
#include <QList>

#include "state.h"

// id
enum UndoCommands {
    Cmd_PaintTile,
};

//
class PaintTileCommand : public QUndoCommand
{
public:
    PaintTileCommand(State *state, int tileIndex, const QPoint& position, int colorIndex, bool mergeable, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;
    int id() const Q_DECL_OVERRIDE { return Cmd_PaintTile; }
    bool mergeWith(const QUndoCommand *other) Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
    int _colorIndex;
    quint8 _buffer[State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8];
    bool _mergeable;
    QList<QPoint> _points;
};

class PasteTileCommand : public QUndoCommand
{
public:
    PasteTileCommand(State *state, int tileIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _tileIndex;
    quint8 _buffer[State::MAX_TILE_HEIGHT * State::MAX_TILE_WIDTH * 8];
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
    SetColorCommand(State *state, int color, int colorIndex, QUndoCommand *parent = nullptr);
    void undo() Q_DECL_OVERRIDE;
    void redo() Q_DECL_OVERRIDE;

private:
    State* _state;
    int _colorIndex;
    int _old;
    int _new;
};
