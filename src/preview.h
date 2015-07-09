#pragma once

#include <QObject>
#include "xlink.h"
#include "state.h"

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
};
