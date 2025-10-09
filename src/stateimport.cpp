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
#include <cstring>
#include <vector>

#include <QDebug>
#include <QtEndian>

#include "mainwindow.h"
#include "state.h"

qint64 StateImport::loadRaw(State* state, QFile& file)
{
    auto size = file.size() - file.pos();
    if (size % 8 != 0) {
        MainWindow::getInstance()->showMessageOnStatusBar(
            QObject::tr("Warning: file is not multiple of 8. Characters might be incomplete"));
        qDebug()
            << "File size not multiple of 8 (" << size << "). Characters might be incomplete";
    }

    int toRead = std::min((int)size, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsetBuffer();

    auto total = file.read((char*)state->_charset, toRead);

    Q_ASSERT(total == toRead && "Failed to read file");

    return total;
}

qint64 StateImport::loadPRG(State* state, QFile& file, quint16* outAddress)
{
    auto size = file.size();
    if (size < 10) { // 2 + 8 (at least one char)
        MainWindow::getInstance()->showMessageOnStatusBar(
            QObject::tr("Error: File size too small"));
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

qint64 StateImport::loadCTM4(State* state, QFile& file, struct CTMHeader4* v4header)
{
    // only expanded files are supported
    if (!v4header->expanded) {
        MainWindow::getInstance()->showMessageOnStatusBar(
            QObject::tr("CTM is not expanded"));
        qDebug() << "CTM is not expanded. Cannot load it";
        return -1;
    }

    // only 20 bytes were read, but v4 headers has 24 bytes.
    // but the 4 remaing bytes are not important.
    char ignore[4];
    file.read(ignore, sizeof(ignore));

    int num_chars = qFromLittleEndian(v4header->num_chars) + 1;
    int num_tiles = v4header->num_tiles + 1;
    QSize map_size = QSize(qFromLittleEndian(v4header->map_width), qFromLittleEndian(v4header->map_height));
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsetBuffer();

    auto total = file.read((char*)state->_charset, toRead);

    for (int i = 0; i < 4; i++)
        state->_setColorForPen(i, v4header->colors[i], -1);

    state->_setMulticolorMode(v4header->vic_res);

    State::TileProperties tp;
    tp.interleaved = 1;
    tp.size.setWidth(v4header->tile_width);
    tp.size.setHeight(v4header->tile_height);
    state->setTileProperties(tp);

    // cell data is not present when not expanded...

    // if color_mode is per_char, convert it to per_tile
    state->_setForegroundColorMode((State::ForegroundColorMode) !!v4header->color_mode);
    state->_setMapSize(map_size);

    if (v4header->color_mode == 0) {
        /* global */

        /* skip char attribs */
        file.seek(file.pos() + num_chars);

        /* skip cell attribs */
        file.seek(file.pos() + num_tiles * v4header->tile_width * v4header->tile_height);

        /* no tile attribs */
    } else if (v4header->color_mode == 1) {
        /* tile attribs */

        /* skip char attribs */
        file.seek(file.pos() + num_chars);

        /* skip cell attribs */
        file.seek(file.pos() + num_tiles * v4header->tile_width * v4header->tile_height);

        /* read tile attribs */
        total += file.read((char*)state->_tileColors, num_tiles);
    } else if (v4header->color_mode == 2) {
        /* char attribs */

        /* skip char attribs */
        total += file.read((char*)state->_tileColors, num_chars);

        /* skip cell attribs */
        file.seek(file.pos() + num_tiles * v4header->tile_width * v4header->tile_height);

        /* no tile attribs */
    }

    int mapInBytes = map_size.width() * map_size.height();
    // read map
    total += file.read((char*)state->_map.data(), mapInBytes);

    return total;
}

qint64 StateImport::loadCTM5(State* state, QFile& file, struct CTMHeader5* v5header)
{
    // Must be expanded, or tile system disabled:
    // flags that we don't want: flags & 0b11 = 0b01
    if ((v5header->flags & 0b00000011) == 0b00000001) {
        MainWindow::getInstance()->showMessageOnStatusBar(
            QObject::tr("CTM is not expanded"));
        qDebug() << "CTM is not expanded. Cannot load it";
        return -1;
    }

    int num_chars = qFromLittleEndian(v5header->num_chars) + 1;
    int num_tiles = qFromLittleEndian(v5header->num_tiles) + 1;
    QSize map_size = QSize(qFromLittleEndian(v5header->map_width),
        qFromLittleEndian(v5header->map_height));
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsetBuffer();

    auto total = file.read((char*)state->_charset, toRead);

    for (int i = 0; i < 4; i++)
        state->_setColorForPen(i, v5header->colors[i], -1);

    state->_setMulticolorMode(v5header->flags & 0b00000100);

    State::TileProperties tp;
    tp.interleaved = 1;
    // some files reports size == 0. Bug in CTMv5?
    tp.size.setWidth(qMax((int)v5header->tile_width, 1));
    tp.size.setHeight(qMax((int)v5header->tile_height, 1));
    state->_setTileProperties(tp);

    // if color_mode is per_char, convert it to per_tile
    state->_setForegroundColorMode((State::ForegroundColorMode) !!v5header->color_mode);
    state->_setMapSize(map_size);

    // color_mode == PER CHAR ?
    if (v5header->color_mode == 2) {
        // place char attribs in tile colors
        file.read((char*)state->_tileColors, num_chars);
        // clean the upper nibble
        for (int i = 0; i < num_chars; ++i)
            state->_tileColors[i] &= 0x0f;
    } else {
        file.seek(file.pos() + num_chars);

        if (v5header->color_mode == 1)
            file.read((char*)state->_tileColors, num_tiles);
        /* else, don't read in global mode */
    }

    // since it is expanded, there are no tile_data

    int mapInBytes = map_size.width() * map_size.height();
    std::vector<quint16> tmpBuffer(mapInBytes * 2);
    // read map
    file.read((char*)tmpBuffer.data(), mapInBytes * 2);
    for (int i = 0; i < mapInBytes; i++) {
        // FIXME: what happens with tiles bigger than 255?
        state->_map[i] = tmpBuffer[i] & 0xff;
    }

    return total;
}

qint64 StateImport::loadCTM(State* state, QFile& file)
{
    struct CTMHeader5 header;
    auto size = file.size();
    if ((std::size_t)size < sizeof(header)) {
        MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("CTM file too small"));
        qDebug() << "Error. File size too small to be CTM (" << size << ").";
        return -1;
    }

    size = file.read((char*)&header, sizeof(header));
    if ((std::size_t)size < sizeof(header))
        return -1;

    // check header
    if (header.id[0] != 'C' || header.id[1] != 'T' || header.id[2] != 'M') {
        MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("Invalid CTM file"));
        qDebug() << "Not a valid CTM file";
        return -1;
    }

    // check version
    if (header.version == 4) {
        return loadCTM4(state, file, (struct CTMHeader4*)&header);
    }
    if (header.version == 5) {
        return loadCTM5(state, file, &header);
    }

    MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("CTM version not supported"));
    qDebug() << "Invalid CTM version: " << header.version;
    return -1;
}

qint64 StateImport::loadVChar64(State* state, QFile& file)
{
    struct VChar64Header header;
    auto size = file.size();
    if ((std::size_t)size < sizeof(header)) {
        MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("Invalid VChar file"));
        qDebug() << "Error. File size too small to be VChar64 (" << size << ").";
        return -1;
    }

    size = file.read((char*)&header, sizeof(header));
    if ((std::size_t)size < sizeof(header))
        return -1;

    // check header
    if (memcmp(header.id, "VChar", 5) != 0) {
        MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("Invalid VChar file"));
        qDebug() << "Not a valid VChar64 file";
        return -1;
    }

    if (header.version > 3) {
        MainWindow::getInstance()->showMessageOnStatusBar(QObject::tr("VChar version not supported"));
        qDebug() << "VChar version not supported";
        return -1;
    }

    // common for version 1, 2 and 3

    int num_chars = qFromLittleEndian((int)header.num_chars);
    int toRead = std::min(num_chars * 8, State::CHAR_BUFFER_SIZE);

    // clean previous memory in case not all the chars are loaded
    state->resetCharsetBuffer();

    auto total = file.read((char*)state->_charset, toRead);

    for (int i = 0; i < 4; i++)
        state->_setColorForPen(i, header.colors[i], -1);

    state->_setMulticolorMode(header.vic_res);
    State::TileProperties properties;
    properties.size = { header.tile_width, header.tile_height };
    properties.interleaved = header.char_interleaved;
    state->_setTileProperties(properties);

    // version 2 and 3 only
    if (header.version == 2 || header.version == 3) {
        int color_mode = header.color_mode;
        state->_setForegroundColorMode(static_cast<State::ForegroundColorMode>(color_mode));

        int map_width = qFromLittleEndian((int)header.map_width);
        int map_height = qFromLittleEndian((int)header.map_height);
        state->_setMapSize(QSize(map_width, map_height));

        file.read((char*)state->_tileColors, std::size(state->_tileColors));
        file.read((char*)state->_map.data(), map_width * map_height);
    }

    // version 3 only
    if (header.version == 3) {
        quint16 charset_addr = qFromLittleEndian(header.address_charset);
        quint16 map_addr = qFromLittleEndian(header.address_map);
        quint16 color_addr = qFromLittleEndian(header.address_attribs);

        state->_exportProperties.addresses[0] = charset_addr;
        state->_exportProperties.addresses[1] = map_addr;
        state->_exportProperties.addresses[2] = color_addr;
        state->_exportProperties.format = static_cast<State::ExportFormat>(header.export_format);
        state->_exportProperties.features = header.export_features;
        state->_keyboardMapping = static_cast<State::KeyboardMapping>(header.keyboard_mapping);
    }

    return total;
}

qint64 StateImport::parseVICESnapshot(QFile& file, quint8* buffer64k, quint16* outCharsetAddress,
    quint16* outScreenRAMAddress, quint8* outColorRAMBuf, quint8* outVICRegistersBuf)
{
    struct VICESnapshotHeader header;
    struct VICESnapshotVersion version;
    struct VICESnapshoptModule module;
    struct VICESnapshoptC64Mem c64mem;
    struct VICESnapshoptC128Mem c128mem;

    static constexpr char VICE_HEADER_MAGIC[] = "VICE Snapshot File\032";
    static constexpr char VICE_VERSION_MAGIC[] = "VICE Version\032";
    static constexpr char VICE_C64MEM[] = "C64MEM";
    static constexpr char VICE_C128MEM[] = "C128MEM";
    static constexpr char VICE_VICII[] = "VIC-II";
    static constexpr char VICE_CIA2[] = "CIA2";

    auto mainwindow = MainWindow::getInstance();

    if (!file.isOpen()) {
        if (!file.open(QIODevice::ReadOnly)) {
            mainwindow->showMessageOnStatusBar(QObject::tr("Error: Failed to open file"));
            return -1;
        }
    }

    auto size = file.size();
    if (size < (qint64)sizeof(VICESnapshotHeader)) {
        mainwindow->showMessageOnStatusBar(QObject::tr("Error: VICE file too small"));
        return -1;
    }

    file.seek(0);

    //
    // parse snapshot header
    //
    size = file.read((char*)&header, sizeof(header));
    if (size != sizeof(header)) {
        mainwindow->showMessageOnStatusBar(QObject::tr("Error: VICE header too small"));
        return -1;
    }

    if (memcmp(header.id, VICE_HEADER_MAGIC, sizeof(header.id)) != 0) {
        mainwindow->showMessageOnStatusBar(QObject::tr("Error: Invalid VICE header Id"));
        return -1;
    }

    qDebug() << "Snapshot version:" << int(header.major) << "." << int(header.minor) << ". Machine: " << QString(header.machine);

    //
    // parse snapshot vice version (optional).
    //

    // since it is option, save current file pos. if it fails to read the optional
    // header, restore the file pos
    auto currentPos = file.pos();
    size = file.read((char*)&version, sizeof(version));
    if (size != sizeof(version)) {
        mainwindow->showMessageOnStatusBar(QObject::tr("Error: VICE header too small"));
        return -1;
    }

    if (memcmp(version.id, VICE_VERSION_MAGIC, sizeof(version.id)) == 0) {
        qDebug() << "VICE version:" << int(version.viceversion[0])
                 << int(version.viceversion[1])
                 << int(version.viceversion[2])
                 << int(version.viceversion[3])
                 << "SVN Rev:"
                 << version.vice_svn_rev;
    } else {
        // rewind
        file.seek(currentPos);
    }

    //
    // start parsing the modules
    //
    int offset = file.pos();
    int c64memoffset = -1;
    int c128memoffset = -1;
    int cia2offset = -1;
    int vic2offset = -1;
    *outCharsetAddress = 0; // in case we can't find the correct one
    *outScreenRAMAddress = 0x400; // in case we can't find the correct one

    while (true) {
        size = file.read((char*)&module, sizeof(module));
        if (size != sizeof(module))
            break;

        qDebug() << "VICE segment: " << module.moduleName;

        /* C64MEM */
        if (c64memoffset == -1 && memcmp(module.moduleName, VICE_C64MEM, sizeof(VICE_C64MEM)) == 0) {
            c64memoffset = file.pos();
        }
        /* C128MEM. Treat C128MEM as a C64MEM */
        if (c128memoffset == -1 && memcmp(module.moduleName, VICE_C128MEM, sizeof(VICE_C128MEM)) == 0) {
            c128memoffset = file.pos();
        }
        /* CIA2 */
        else if (cia2offset == -1 && memcmp(module.moduleName, VICE_CIA2, sizeof(VICE_CIA2)) == 0) {
            cia2offset = file.pos();
        }
        /* VICII */
        else if (vic2offset == -1 && memcmp(module.moduleName, VICE_VICII, sizeof(VICE_VICII)) == 0) {
            vic2offset = file.pos();
        }

        offset += qFromLittleEndian(module.lenght);
        if (!file.seek(offset))
            break;
    }

    if ((c64memoffset != -1 || c128memoffset != -1) && cia2offset != -1 && vic2offset != -1) {
        // copy 64k memory...
        // ...from c64
        if (c64memoffset != -1) {
            file.seek(c64memoffset);
            size = file.read((char*)&c64mem, sizeof(c64mem));
            if (size != sizeof(c64mem)) {
                mainwindow->showMessageOnStatusBar(QObject::tr("Error: Invalid VICE C64MEM segment"));
                return -1;
            }
            std::memcpy(buffer64k, c64mem.ram, sizeof(c64mem.ram));
        }
        // ...or from c128
        else if (c128memoffset != -1) {
            file.seek(c128memoffset);
            size = file.read((char*)&c128mem, sizeof(c128mem));
            if (size != sizeof(c128mem)) {
                mainwindow->showMessageOnStatusBar(QObject::tr("Error: Invalid VICE C128MEM segment"));
                return -1;
            }
            // FIXME: copy only first 64k
            std::memcpy(buffer64k, c128mem.ram, 65536);
        }

        // find default charset
        file.seek(cia2offset);
        struct VICESnapshoptCIA2 cia2;
        size = file.read((char*)&cia2, sizeof(cia2));
        if (size != sizeof(cia2)) {
            mainwindow->showMessageOnStatusBar(QObject::tr("Error: Invalid VICE CIA2 segment"));
            return -1;
        }
        int bank_addr = (3 - (cia2.ora & 0x03)) * 16384; // $dd00

        file.seek(vic2offset);
        struct VICESnapshoptVICII vic2;
        size = file.read((char*)&vic2, sizeof(vic2));
        if (size != sizeof(vic2)) {
            mainwindow->showMessageOnStatusBar(QObject::tr("Error: Invalid VICE VIC-II segment"));
            return -1;
        }

        static constexpr char seuck_signature[] = "PRESS FIRE TO COMMENCE SUPADEATH";

        if (memcmp(seuck_signature, &buffer64k[0x3fdc], sizeof(seuck_signature) - 1) == 0) {
            *outCharsetAddress = 0xf800;
            *outScreenRAMAddress = 0xec00;
        } else {
            int charset_offset = (vic2.registers[0x18] & 0x0e) >> 1; // $d018 & 0x7
            charset_offset *= 2048;
            *outCharsetAddress = bank_addr + charset_offset;

            int screenRAM_offset = vic2.registers[0x18] >> 4; // 4-MSB bit of $d018
            screenRAM_offset *= 1024;
            *outScreenRAMAddress = bank_addr + screenRAM_offset;
        }

        // update color RAM
        std::memcpy(outColorRAMBuf, vic2.color_ram, sizeof(vic2.color_ram));

        // update VIC registers
        std::memcpy(outVICRegistersBuf, vic2.registers, sizeof(vic2.registers));
    } else {
        mainwindow->showMessageOnStatusBar(QObject::tr("Error: VICE C64MEM/C128MEM segment not found"));
        return -1;
    }

    return 0;
}
