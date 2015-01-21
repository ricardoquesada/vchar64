#pragma once

#include <QFile>

class State;

namespace Import
{
    qint64 loadRaw(QFile& file, State* state);
    qint64 load64C(QFile& file, State* state);
    qint64 loadCTM(QFile& file, State* state);
}
