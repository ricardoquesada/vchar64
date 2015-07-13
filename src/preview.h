#pragma once

#include <QObject>
#include <QLibrary>

#include "state.h"

typedef bool (*xlink_ping_t)(void);
typedef bool (*xlink_load_t)(uchar, uchar, ushort, uchar*, int);
typedef bool (*xlink_poke_t)(uchar, uchar, ushort, uchar);

class Preview : public QObject
{
    Q_OBJECT

public:
    static Preview* getInstance();

public slots:
    // file loaded, or new project
    void fileLoaded();

    // at least one pixel changes in the tile
    void tileUpdated(int tileIndex);

protected:
    Preview();
    QLibrary *xlink;
    xlink_ping_t xlink_ping;
    xlink_load_t xlink_load;
    xlink_poke_t xlink_poke;
};
