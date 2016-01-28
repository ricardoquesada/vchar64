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
#include <QTextStream>
#include <QDebug>
#include <QApplication>


#include "stateimport.h"
#include "state.h"

qint64 StateExport::saveVChar64(State* state, QFile& file)
{
    StateImport::VChar64Header header;

    strncpy(header.id, "VChar", 5);

    header.version = 2;

    for (int i=0;i<4;i++)
        header.colors[i] = state->_penColors[i];

    short chars = state->CHAR_BUFFER_SIZE / 8;

    chars = qToLittleEndian(chars);
    header.num_chars = chars;

    auto properties = state->getTileProperties();
    header.tile_width = properties.size.width();
    header.tile_height = properties.size.height();
    header.char_interleaved = properties.interleaved;

    header.vic_res = state->isMulticolorMode();

    header.color_mode = state->getForegroundColorMode();
    header.map_width = qToLittleEndian(state->getMapSize().width());
    header.map_height = qToLittleEndian(state->getMapSize().height());

    QByteArray arrayHeader((const char*)&header, sizeof(header));
    auto total = file.write(arrayHeader);

    // charset
    auto buffer = state->getCharsetBuffer();
    QByteArray arrayCharset((char*)buffer, State::CHAR_BUFFER_SIZE);
    total += file.write(arrayCharset);

    // colors
    buffer = state->getTileColors();
    QByteArray arrayColors((char*)buffer, State::TILE_COLORS_BUFFER_SIZE);
    total += file.write(arrayColors);

    // map
    buffer = state->getMapBuffer();
    QByteArray arrayMap((char*)buffer, state->getMapSize().width() * state->getMapSize().height());
    total += file.write(arrayMap);

    file.flush();

    return total;
}

qint64 StateExport::saveRaw(const QString& filename, const void *buffer, int bufferSize)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return -1;

    int total = 0;

    QByteArray arrayData((char*)buffer, bufferSize);
    total += file.write(arrayData);
    file.flush();

    qDebug() << "File exported as RAW successfully:" << file.fileName();

    return total;
}

qint64 StateExport::savePRG(const QString& filename, const void* buffer, int bufferSize, quint16 address)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return -1;

    int total = 0;

    address = qToLittleEndian(address);

    // PRG header
    QByteArray arrayAddress((char*)&address,2);
    total += file.write(arrayAddress);

    // data
    QByteArray arrayData((char*)buffer, bufferSize);
    total += file.write(arrayData);
    file.flush();

    qDebug() << "File exported as PRG successfully: " << file.fileName();

    return total;
}

qint64 StateExport::saveAsm(const QString& filename, const void* buffer, int bufferSize, const QString& label)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return -1;

    const unsigned char* charBuffer = (const unsigned char*) buffer;

    QTextStream out(&file);
    out << "; Exported using VChar64 v" << QApplication::applicationVersion() << "\n";
    out << "; Total bytes: " << bufferSize << "\n";
    out << label << ":\n";
    for (int i=0; i<bufferSize;)
    {
        out << ".byte ";
        int j=0;
        for (j=0; j<16 && i<bufferSize ; ++j, ++i)
        {
            if (j != 0)
                out << ",";
            out << "$";
            out.setFieldWidth(2);
            out.setPadChar('0');
            out.setFieldAlignment(QTextStream::AlignRight);
            out << hex << (unsigned int)charBuffer[i];
            out.setFieldWidth(0);
        }
        out << "\t; " << dec << i-j << "\n";
    }
    out << label.toUpper() << "_COUNT = " << bufferSize << "\n";

    qDebug() << "File exported as ASM successfully: " << file.fileName();
    out.flush();
    return out.pos();
}
