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

    enum ExportFeature {
        EXPORT_FEATURE_NONE = 0,
        EXPORT_FEATURE_CHARSET = 1 << 0,
        EXPORT_FEATURE_MAP = 1 << 1,
        EXPORT_FEATURE_COLORS = 1 << 2,
        EXPORT_FEATURE_ALL = (EXPORT_FEATURE_CHARSET | EXPORT_FEATURE_MAP | EXPORT_FEATURE_COLORS)
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
    quint8 _penColors[PEN_MAX];

    State()
    {
        resetCharsetBuffer();
        // Initialize default pen colors matching State.cpp
        _penColors[PEN_BACKGROUND] = 1;
        _penColors[PEN_MULTICOLOR1] = 5;
        _penColors[PEN_MULTICOLOR2] = 7;
        _penColors[PEN_FOREGROUND] = 11;

        // Initialize export properties
        _exportProperties.addresses[0] = 0x3800;
        _exportProperties.addresses[1] = 0x4000;
        _exportProperties.addresses[2] = 0x4400;
        _exportProperties.format = EXPORT_FORMAT_RAW;
        _exportProperties.features = EXPORT_FEATURE_CHARSET;
    }

    void resetCharsetBuffer()
    {
        for (int i = 0; i < CHAR_BUFFER_SIZE; ++i)
            _charset[i] = 0;
    }

    using charset_t = quint8[CHAR_BUFFER_SIZE];
    using tileColors_t = quint8[TILE_COLORS_BUFFER_SIZE];

    // Accessors needed by StateExport
    const charset_t& getCharsetBuffer() const { return _charset; }
    const tileColors_t& getTileColors() const { return _tileColors; }
    const std::vector<quint8>& getMapBuffer() const { return _map; }
    const QSize& getMapSize() const { return _mapSize; }
    TileProperties getTileProperties() const { return _tileProperties; }
    ExportProperties getExportProperties() const { return _exportProperties; }
    bool isMulticolorMode() const { return _multicolorMode; }
    ForegroundColorMode getForegroundColorMode() const { return _foregroundColorMode; }

    // Non-const accessors for setting up state in tests
    charset_t& getCharsetBuffer() { return _charset; }

    void _setColorForPen(int index, int color, int)
    {
        // Mock implementation
        if (index >= 0 && index < PEN_MAX)
            _penColors[index] = color;
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

    int getColorForPen(int pen, int tileIndex) const
    {
        if (pen >= 0 && pen < PEN_MAX)
            return _penColors[pen];
        return 0;
    }

    int getTileIndex() const
    {
        return 0;
    }

    bool shouldBeDisplayedInMulticolor2(int tileIndex) const
    {
        return false;
    }

    // Add explicit mock methods for accessors if needed by StateImport
    // StateImport accesses members directly via friend, but here they are public.
};
