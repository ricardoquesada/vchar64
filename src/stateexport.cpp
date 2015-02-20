/****************************************************************************
Copyright 2015 Ricardo Quesada

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
****************************************************************************/

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

    auto tileSize = state->getTileSize();
    header.tile_width = tileSize.width();
    header.tile_height = tileSize.height();
    header.char_interleaved = state->getCharInterleaved();

    header.vic_res = state->isMultiColor();

    QByteArray arrayHeader((const char*)&header, sizeof(header));
    auto total = file.write(arrayHeader);


    auto buffer = state->getCharsBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);

    file.flush();

    return total;
}

qint64 StateExport::saveRaw(QFile& file, State* state)
{
    int total = 0;
    auto buffer = state->getCharsBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    return total;
}

qint64 StateExport::savePRG(QFile& file, State* state, quint16 address)
{
    int total = 0;

    address = qToLittleEndian(address);

    // PRG header
    QByteArray arrayAddress((char*)&address,2);
    total += file.write(arrayAddress);

    // data
    auto buffer = state->getCharsBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    return total;
}
