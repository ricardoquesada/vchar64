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
#include <QDebug>

#include "stateimport.h"
#include "state.h"

qint64 StateExport::saveVChar64(State* state, QFile& file)
{
    StateImport::VChar64Header header;

    strncpy(header.id, "VChar", 5);

    header.version = 1;

    for (int i=0;i<4;i++)
        header.colors[i] = state->getColorForPen(i);

    short chars = state->CHAR_BUFFER_SIZE / 8;

    chars = qToLittleEndian(chars);
    header.num_chars = chars;

    auto properties = state->getTileProperties();
    header.tile_width = properties.size.width();
    header.tile_height = properties.size.height();
    header.char_interleaved = properties.interleaved;

    header.vic_res = state->isMulticolorMode();

    QByteArray arrayHeader((const char*)&header, sizeof(header));
    auto total = file.write(arrayHeader);


    auto buffer = state->getCharsetBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);

    file.flush();

    return total;
}

qint64 StateExport::saveRaw(State* state, QFile& file)
{
    int total = 0;
    auto buffer = state->getCharsetBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    qDebug() << "File exported as RAW successfully:" << file.fileName();


    return total;
}

qint64 StateExport::savePRG(State* state, QFile& file, quint16 address)
{
    int total = 0;

    address = qToLittleEndian(address);

    // PRG header
    QByteArray arrayAddress((char*)&address,2);
    total += file.write(arrayAddress);

    // data
    auto buffer = state->getCharsetBuffer();
    QByteArray arrayData((char*)buffer, state->CHAR_BUFFER_SIZE);
    total += file.write(arrayData);
    file.flush();

    qDebug() << "File exported as PRG successfully: " << file.fileName();

    return total;
}
