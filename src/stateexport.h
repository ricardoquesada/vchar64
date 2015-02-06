#pragma once

#include <QFile>

class State;

namespace StateExport
{
    qint64 saveVChar64(QFile& file, State* state);
}
