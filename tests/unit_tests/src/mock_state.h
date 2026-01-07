#pragma once

#include <QFile>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <vector>

// Mock MainWindow
class MainWindow {
public:
    static MainWindow* getInstance() { return nullptr; }
    void showMessageOnStatusBar(const QString&) { }
};

// Mock State
class State {
public:
    constexpr static int CHAR_BUFFER_SIZE = 8 * 256;
    constexpr static int TILE_COLORS_BUFFER_SIZE = 256;
    constexpr static quint8 TILE_COLORS_DEFAULT = 11;
    constexpr static int MAX_TILE_WIDTH = 8;

    enum Pen {
        PEN_BACKGROUND,
        PEN_MULTICOLOR1,
        PEN_MULTICOLOR2,
        PEN_FOREGROUND,
        PEN_MAX
    };

    enum ForegroundColorMode {
        FOREGROUND_COLOR_GLOBAL,
        FOREGROUND_COLOR_PER_TILE
    };

    enum ExportFormat {
        EXPORT_FORMAT_RAW,
        EXPORT_FORMAT_PRG,
        EXPORT_FORMAT_ASM,
        EXPORT_FORMAT_C,
    };

    enum KeyboardMapping {
        KEYBOARD_MAPPING_C64, // Commodore 8-bit screen code. Not PETSCII
        KEYBOARD_MAPPING_ATARI8, // Atari 8-bit
    };

    struct TileProperties {
        QSize size;
        int interleaved;
    };

    struct ExportProperties {
        quint16 addresses[3]; // 0: CHARSET, 1:MAP, 2:COLORS
        ExportFormat format; // EXPORT_FORMAT
        quint8 features; // EXPORT_FEATURE
    };

    // Public members for easy access in tests
    quint8 _charset[CHAR_BUFFER_SIZE];
    quint8 _tileColors[TILE_COLORS_BUFFER_SIZE];
    std::vector<quint8> _map;
    QSize _mapSize;
    bool _multicolorMode;
    ForegroundColorMode _foregroundColorMode;
    TileProperties _tileProperties;
    ExportProperties _exportProperties;
    KeyboardMapping _keyboardMapping;

    State()
    {
        resetCharsetBuffer();
    }

    void resetCharsetBuffer()
    {
        for (int i = 0; i < CHAR_BUFFER_SIZE; ++i)
            _charset[i] = 0;
    }

    void _setColorForPen(int index, int color, int)
    {
        // Mock implementation
    }

    void _setMulticolorMode(bool mode)
    {
        _multicolorMode = mode;
    }

    void setTileProperties(const TileProperties& tp)
    {
        _tileProperties = tp;
    }

    void _setTileProperties(const TileProperties& tp)
    {
        _tileProperties = tp;
    }

    void _setForegroundColorMode(ForegroundColorMode mode)
    {
        _foregroundColorMode = mode;
    }

    void _setMapSize(const QSize& size)
    {
        _mapSize = size;
        _map.resize(size.width() * size.height());
    }

    // Add explicit mock methods for accessors if needed by StateImport
    // StateImport accesses members directly via friend, but here they are public.
};
