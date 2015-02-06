#include "stateexport.h"

#include <QtEndian>
#include <QByteArray>

#include "stateimport.h"
#include "state.h"

qint64 StateExport::saveVChar64(QFile& file, State* state)
{
    StateImport::VChar64Header header;

    strncpy(header.id, "VChar", 5);

    header.version = 1;

    for (int i=0;i<4;i++)
        header.colors[i] = state->getColorAtIndex(i);

    short chars = state->CHAR_BUFFER_SIZE / 8;

    chars = qToLittleEndian(chars);
    header.num_chars = chars;

    header.vic_res = state->isMultiColor();

    QByteArray arrayHeader((const char*)&header, sizeof(header));
    auto total = file.write(arrayHeader);


    const char* buffer = state->getCharsBuffer();
    QByteArray arrayData(buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);

    file.flush();

    return total;
}
