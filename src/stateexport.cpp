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

qint64 StateExport::saveRaw(QFile& file, State* state)
{
    int total = 0;
    const char* buffer = state->getCharsBuffer();
    QByteArray arrayData(buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    return total;
}

qint64 StateExport::savePRG(QFile& file, State* state, u_int16_t address)
{
    int total = 0;

    address = qToLittleEndian(address);

    // PRG header
    QByteArray arrayAddress((char*)&address,2);
    total += file.write(arrayAddress);

    // data
    const char* buffer = state->getCharsBuffer();
    QByteArray arrayData(buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    return total;
}
