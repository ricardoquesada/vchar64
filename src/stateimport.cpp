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

#include "stateimport.h"

#include <algorithm>
#include <cstdlib>
#include <QDebug>
#include <QtEndian>

#include "state.h"

qint64 StateImport::loadRaw(State* state, QFile& file)
{
    auto size = file.size() - file.pos();
    if (size % 8 !=0) {
        qDebug() << "File size not multiple of 8 (" << size << "). Characters might be incomplete";
    }

    int toRead = std::min((int)size, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsBuffer();

    auto total = file.read((char*)state->getCharsBuffer(), toRead);

    Q_ASSERT(total == toRead && "Failed to read file");

    return total;
}

qint64 StateImport::loadPRG(State *state, QFile& file, quint16* outAddress)
{
    auto size = file.size();
    if (size < 10) { // 2 + 8 (at least one char)
        qDebug() << "Error: File Size too small.";
        return -1;
    }

    // ignore first 2 bytes
    quint16 address;
    file.read((char*)&address, 2);

    if (outAddress) {
        *outAddress = qFromLittleEndian(address);
    }

    return StateImport::loadRaw(state, file);
}

qint64 StateImport::loadCTM4(State *state, QFile& file, struct CTMHeader4* v4header)
{
    // only 20 bytes were read, but v4 headers has 24 bytes.
    // but the 4 remaing bytes are not important.
    char ignore[4];
    file.read(ignore, sizeof(ignore));

    int num_chars = qFromLittleEndian(v4header->num_chars);
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsBuffer();

    auto total = file.read((char*)state->getCharsBuffer(), toRead);

    for (int i=0; i<4; i++)
        state->setColorAtIndex(i, v4header->colors[i]);

    state->setMultiColor(v4header->vic_res);

    State::TileProperties tp;
    tp.interleaved = 1;
    tp.size.setWidth(v4header->tile_width);
    tp.size.setHeight(v4header->tile_height);
    state->setTileProperties(tp);

    return total;
}

qint64 StateImport::loadCTM5(State *state, QFile& file, struct CTMHeader5* v5header)
{
    int num_chars = qFromLittleEndian(v5header->num_chars);
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsBuffer();

    auto total = file.read((char*)state->getCharsBuffer(), toRead);

    for (int i=0; i<4; i++)
        state->setColorAtIndex(i, v5header->colors[i]);

    state->setMultiColor(v5header->flags & 0b00000100);

    State::TileProperties tp;
    tp.interleaved = 1;
    // some files reports size == 0. Bug in CTMv5?
    tp.size.setWidth(qMax((int)v5header->tile_width,1));
    tp.size.setHeight(qMax((int)v5header->tile_height,1));
    state->setTileProperties(tp);

    return total;
}

qint64 StateImport::loadCTM(State *state, QFile& file)
{
    struct CTMHeader5 header;
    auto size = file.size();
    if ((std::size_t)size<sizeof(header)) {
        qDebug() << "Error. File size too small to be CTM (" << size << ").";
        return -1;
    }

    size = file.read((char*)&header, sizeof(header));
    if ((std::size_t)size<sizeof(header))
        return -1;

    // check header
    if (header.id[0] != 'C' || header.id[1] != 'T' || header.id[2] != 'M') {
        qDebug() << "Not a valid CTM file";
        return -1;
    }

    // check version
    if (header.version == 4) {
        return loadCTM4(state, file, (struct CTMHeader4*)&header);
    } else if (header.version == 5) {
        return loadCTM5(state, file, &header);
    }
    qDebug() << "Invalid CTM version: " << header.version;
    return -1;
}

qint64 StateImport::loadVChar64(State *state, QFile& file)
{
    struct VChar64Header header;
    auto size = file.size();
    if ((std::size_t)size<sizeof(header)) {
        qDebug() << "Error. File size too small to be VChar64 (" << size << ").";
        return -1;
    }

    size = file.read((char*)&header, sizeof(header));
    if ((std::size_t)size<sizeof(header))
        return -1;

    // check header
    if (header.id[0] != 'V' || header.id[1] != 'C' || header.id[2] != 'h' || header.id[3] != 'a' || header.id[4] != 'r') {
        qDebug() << "Not a valid VChar64 file";
        return -1;
    }


    int num_chars = qFromLittleEndian((int)header.num_chars);
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsBuffer();

    auto total = file.read((char*)state->getCharsBuffer(), toRead);

    for (int i=0; i<4; i++)
        state->setColorAtIndex(i, header.colors[i]);

    state->setMultiColor(header.vic_res);
    State::TileProperties properties;
    properties.size = {header.tile_width, header.tile_height};
    properties.interleaved = header.char_interleaved;
    state->setTileProperties(properties);

    return total;
}
