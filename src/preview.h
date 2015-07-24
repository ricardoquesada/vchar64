#pragma once

#include <QObject>
#include <QLibrary>

#include "state.h"

typedef bool (*xlink_ping_t)(void);
typedef bool (*xlink_load_t)(uchar, uchar, ushort, uchar*, int);
typedef bool (*xlink_peek_t)(uchar, uchar, ushort, uchar*);
typedef bool (*xlink_poke_t)(uchar, uchar, ushort, uchar);
typedef bool (*xlink_fill_t)(uchar, uchar, ushort, uchar, uint);

class Preview : public QObject
{
    Q_OBJECT

    void updateBackgroundColor();
    void updateForegroundColor();
    void updateMulticolor1();
    void updateMulticolor2();
    void updateColorMode();
    void updateCharset();
    bool updateScreen(const QString &filename);
public:
    static Preview* getInstance();

public slots:
    // file loaded, or new project
    void fileLoaded();

    // when one byte in a part of the tile changes
    void byteUpdated(int);

    // when the whole tile changes
    void tileUpdated(int);

    // multi-color / hires or new colors
    void colorPropertiesUpdated();

    // a color was selected
    void colorSelected();

protected:
    Preview();
    void updateColorProperties();

    QLibrary *_xlink;
    xlink_ping_t xlink_ping;
    xlink_load_t xlink_load;
    xlink_peek_t xlink_peek;
    xlink_poke_t xlink_poke;
    xlink_fill_t xlink_fill;
};
