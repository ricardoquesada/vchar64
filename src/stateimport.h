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

#pragma once

#include <QFile>

class State;

class StateImport
{
public:
    // loads a raw file
    static qint64 loadRaw(State* state, QFile& file);

    // loads a PRG / 64C file. Same a raw, but the first 2 characters are ignored
    static qint64 loadPRG(State* state, QFile& file);

    // loads a CharPad project file
    static qint64 loadCTM(State* state, QFile& file);

    // loads a VChar64 project file
    static qint64 loadVChar64(State* state, QFile& file);



    //
    // From CharPad documentation
    //

    /*
    File Header, 20 bytes...


    ID          [00-02]    3 bytes  : ASCII ID string... "CTM"
    VERSION     [03]       1 byte   : version number, currently $04.
    COLOURS     [04-07]    4 bytes  : BGR, MC1, MC2, RAM.
    COLOUR_MODE [08]       1 byte   : 0 = Global, 1 = Per Tile, 2 = Per Tile Cell.
    VIC_RES     [09]       1 byte   : 0 = Hi Resolution, 1 = Multicolour.

    NUM_CHARS   [10,11]    2 bytes  : 16-bits, Number of chars - 1 (low, high).

    NUM_TILES   [12]       1 byte   : Number of tiles - 1.

    TILE_WID    [13]       1 byte   : Tile Width
    TILE_HEI    [14]       1 byte   : Tile Height

    MAP_WID     [15,16]    2 bytes  : 16-bit Map width (low, high).
    MAP_HEI     [17,18]    2 bytes  : 16-bit Map height (low, high).

    EXPANDED    [19]       1 byte   : Boolean flag, 1 = CHAR_DATA is in "Expanded" form (CELL_DATA is unnecessary and absent).

    RESERVED    [20]       1 byte
    RESERVED    [21]       1 byte
    RESERVED    [22]       1 byte
    RESERVED    [23]       1 byte   : (total header size is 24 bytes)


    File Data...


    CHAR_DATA.      The character set. Size = NUM_CHARS * 8 bytes.

                    NB. NUM_CHARS should equal NUM_TILES * TILE_SIZE * TILE_SIZE for an "Expanded" char-set.


                    An expanded char-set is one where CHAR_DATA contains one unique (8 byte) character definition for each
                    and every cell for each tile in the tile-set.

                    In such a case the CELL_DATA would simply be an ascending series starting at 0, easily recreated, and as
                    such it will be omitted from the file.



    CHAR_ATTRIBS.   1 byte for each character in the set.

                    Each byte should be interpreted as follows...

                    MMMMCCCC, where M is one of 4 material bits, C is one of 4 colour bits.

                    The CHAR_ATTRIBS block is new to CTM v4, CharPad generates this information when a charset is
                    compressed from an expanded state.

                    (CharPads compressor can optionally copy colour and/or material attributes from the CELL_ATTRIBS to the
                    CHAR_ATTRIBS prior to compression to allow varying levels of compression to be achieved)

                    CharPad always uses the CELL_ATTRIBS data when rendering tiles and CHAR_ATTRIBS when rendering the
                    character set itself.

                    Including CHAR_ATTRIBS in the CTM file allows a character set to be rendered correctly immediately
                    after loading instead of having to wait for the user to expand and compress.



    CELL_DATA.      Size = NUM_TILES * TILE_SIZE * TILE_SIZE bytes * 2 bytes. (only exists if CHAR_DATA is not "Expanded")

                    NB. CELL_DATA is a stream of 16-bit character codes to be arranged left-to-right, top-to-bottom for each tile.

                    The largest contained character code should not be greater than NUM_CHARS - 1.

                    In the case that the CHAR_DATA is saved in "Expanded" form, CELL_DATA will be absent as its values would just
                    be an ascending series starting at 0.

                    NB. The format of each (16-bit) code is Low Byte, High Byte.



    CELL_ATTRIBS.   Size = NUM_TILES * TILE_SIZE * TILE_SIZE bytes (exists for ALL modes)

                    1 attribute byte for each cell of each tile.

                    Upper 4 bits = cell Material value (0-15)
                    Lower 4 bits = cell "Colour RAM" value (0-15)    nb. only when COLOUR_MODE = 2 (Per Tile Cell)

                    NB. CELL_ATTRIBS is a stream of 8-bit attribute codes arranged left-to-right, top-to-bottom for each tile.


    TILE_ATTRIBS.   Size = NUM_TILES bytes (1 byte per tile = "RAM colour". only exists if COLOR_MODE = 1 (Per Tile)


    MAP_DATA.        Size =  MAP_WID x MAP_HEI bytes.
    */

    #pragma pack(push)
    #pragma pack(1)
    struct CTMHeader
    {
        char id[3];                 // must be CTM
        char version;               // must be 4
        char colors[4];             // BGR, MC1, MC2, RAM.
        char color_mode;            // 0 = Global, 1 = Per Tile, 2 = Per Tile Cell.
        char vic_res;               // 0 = Hi Resolution, 1 = Multicolour.

        unsigned short num_chars;   // 16-bits, Number of chars - 1 (low, high).

        unsigned char num_tiles;    // Number of tiles - 1.

        unsigned char tile_width;   // Tile Width
        unsigned char tile_height;  // Tile Height

        unsigned short map_width;   // 16-bit Map width (low, high).
        unsigned short map_height;  // 16-bit Map height (low, high).

        char expanded;              // Boolean flag, 1 = CHAR_DATA is in "Expanded" form (CELL_DATA is unnecessary and absent).

        char reserved[4];           // Must be 24 bytes in total
    };
    #pragma pack(pop)
    static_assert (sizeof(CTMHeader) == 24, "Size is not correct");


    #pragma pack(push)
    #pragma pack(1)
    struct VChar64Header
    {
        char id[5];                 // must be VChar
        char version;               // must be 1
        char colors[4];             // BGR, MC1, MC2, RAM.
        char vic_res;               // 0 = Hi Resolution, 1 = Multicolour.

        unsigned short num_chars;   // 16-bits, Number of chars - 1 (low, high).

        char tile_width;            // between 1-5
        char tile_height;           // between 1-5
        char char_interleaved;      // between 1-128

        char reserved[16];           // Must be 32 bytes in total
    };
    #pragma pack(pop)

    static_assert (sizeof(VChar64Header) == 32, "Size is not correct");
};


