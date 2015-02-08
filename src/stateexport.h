#pragma once

#include <QFile>

class State;

namespace StateExport
{
    qint64 saveVChar64(QFile& file, State* state);
    qint64 saveRaw(QFile &file, State* state);
    qint64 savePRG(QFile &file, State* state, u_int16_t address);
}
