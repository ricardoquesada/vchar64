#include "stateexport.h"

qint64 StateExport::saveVChar64(State* state, QFile& file) { return 0; }
qint64 StateExport::saveRaw(const QString& filename, const void* buffer, qsizetype bufferSize) { return 0; }
qint64 StateExport::savePRG(const QString& filename, const void* buffer, qsizetype bufferSize, quint16 address) { return 0; }
qint64 StateExport::saveAsm(const QString& filename, const void* buffer, qsizetype bufferSize, const QString& label) { return 0; }
qint64 StateExport::saveC(const QString& filename, const void* buffer, qsizetype bufferSize, const QString& label) { return 0; }
qint64 StateExport::savePNG(const QString& filename, std::unique_ptr<QImage> image, State* state) { return 0; }
