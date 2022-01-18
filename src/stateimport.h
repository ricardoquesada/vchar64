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

/**
 * @brief The StateImport class
 * Provices helper functions to load different formats into VChar64
 */
class StateImport {
public:
    /**
     * @brief loadRaw loads a raw file
     * @param state State
     * @param file file to load
     * @return whether or not the loading was successful
     */
    static qint64 loadRaw(State* state, QFile& file);

    /**
     * @brief loadPRG loads a PRG / 64C file. Same a raw, but the first 2 characters are ignored
     * @param state State
     * @param file file to load
     * @param outAddress address where it was loaded
     * @return whether or not the loading was successful
     */
    static qint64 loadPRG(State* state, QFile& file, quint16* outAddress);

    /**
     * @brief loadCTM loads a CharPad project file
     * @param state State
     * @param file file to load
     * @return whether or not the loading was successful
     */
    static qint64 loadCTM(State* state, QFile& file);

    /**
     * @brief loadVChar64 loads a VChar64 project file
     * @param state State
     * @param file file to load
     * @return whether or not the loading was successful
     */
    static qint64 loadVChar64(State* state, QFile& file);

    /**
     * @brief parseVICESnapshot parses a VICE snapshot file
     * @param file file to parse
     * @param buffer64k "out" buffer that will have the 64k RAM memory
     * @param outCharsetAddress "out" address that will contain where the default charset is
     * @param outScreenRAMAddress "out" address that will contain where the default Screen RAM is
     * @param outColorRAMBuf "out" buffer that will contain the color RAM
     * @param outVICRegistersBuf "out" buffer that will contain the VIC registers (d000-d03f)
     * @return
     */
    static qint64 parseVICESnapshot(QFile& file, quint8* buffer64k, quint16* outCharsetAddress, quint16* outScreenRAMAddress, quint8* outColorRAMBuf, quint8* outVICRegistersBuf);

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
    struct CTMHeader4 {
        char id[3]; // must be CTM
        char version; // must be 4
        char colors[4]; // BGR, MC1, MC2, RAM.
        char color_mode; // 0 = Global, 1 = Per Tile, 2 = Per Tile Cell.
        char vic_res; // 0 = Hi Resolution, 1 = Multicolour.

        unsigned short num_chars; // 16-bits, Number of chars - 1 (low, high).

        unsigned char num_tiles; // Number of tiles - 1.

        unsigned char tile_width; // Tile Width
        unsigned char tile_height; // Tile Height

        unsigned short map_width; // 16-bit Map width (low, high).
        unsigned short map_height; // 16-bit Map height (low, high).

        char expanded; // Boolean flag, 1 = CHAR_DATA is in "Expanded" form (CELL_DATA is unnecessary and absent).

        char reserved[4]; // Must be 24 bytes in total
    };
#pragma pack(pop)
    static_assert(sizeof(CTMHeader4) == 24, "Size is not correct");

    /*
    CTM files begins with a 20 byte header...


    file_id       [00-02]   3 bytes : file identification string ("CTM" in ASCII).
    version       [03]      1 byte  : File version number (5).

    colours       [04-07]   4 bytes : Project colours (only the low nybbles are of interest).
                                      [04] = Background/transparent colour.
                                      [05] = Character multi-colour 1.
                                      [06] = Character multi-colour 2.
                                      [07] = Character colour. (primarily for 'Global' colouring mode).

    colour_method [08]      1 byte  : Character colouring method (values 0-2 are valid).
                                      (0 = Global, 1 = Per Tile, 2 = Per Character).

    flags         [09]      1 byte  : Various project flags:-

                                      bit 0 : Tile System (1 = Enabled).
                                      bit 1 : Expanded Data (1 = Yes).
                                      bit 2 : Multi-colour Mode (1 = Enabled).

                                      Remaining bits (3-7) are unused.

    num_chars-1   [10,11]   2 bytes : Number of characters -1 (low byte, high byte).
    num_tiles-1   [12,13]   2 bytes : Number of tiles -1 (low byte, high byte).
    tile_wid      [14]      1 byte  : Tile width (currently values of 1-8 are valid).
    tile_hei      [15]      1 byte  : Tile height (currently values of 1-8 are valid).
    map_wid       [16,17]   2 bytes : Map width (low byte, high byte).
    map_hei       [18,19]   2 bytes : Map height (low byte, high byte).




    Then come the data blocks...


    char_data       The character set. (size = num_chars * 8 bytes).

                    Each byte in this block represents the pixels of one row (of 8) for each character
                    definition, the sequence should be interpreted as groups of eight bytes where the
                    first byte in a group represents the topmost row of pixels in a character with the
                    following seven bytes representing each further row.



    char_attribs    The character attributes. (size = num_chars bytes).

                    Each byte should be interpreted as an "MMMMCCCC" bit pattern where 'M' is one of four
                    material bits and 'C' is one of four colour bits.

                    The colour attribute nybbles are only useful when colour_method = 2 (Per character)
                    and they should all read zero if any other character colouring method is used.



    tile_data       The tile data. (size = num_tiles * tile_wid * tile_hei bytes * 2 bytes).

                    This block only exists if the 'Expanded Data' flag is clear. (see header).

                    Tile_data is a stream of (16-bit) character codes to be arranged left-to-right,
                    top-to-bottom for each tile.

                    The character code values in this block should not exceed num_chars-1.

                    In the case that the 'Expanded Data' flag is set, the tile_data block will be absent
                    as it's contents would just be an ascending series starting at 0.

                    The format of each (16 bit) character code is Low Byte, High Byte.



    tile_colours    The tile colours. (size = num_tiles bytes).

                    one byte per tile, the lower nybbles indicate "char colour" (0-15) for the whole tile
                    and the upper nybbles are unused.

                    This block only exists if colour_method = 1 (Per tile).


    map_data        The map data. (size =  map_wid * map_hei * 2 bytes).

                    Each byte in this block is a (16 bit) tile code, the sequence should be interpreted as
                    running left-to-right for each map row starting with the topmost and moving down.
    */
#pragma pack(push)
#pragma pack(1)
    struct CTMHeader5 {
        char id[3]; // must be CTM
        char version; // must be 5
        char colors[4]; // BGR, MC1, MC2, RAM.
        char color_mode; // 0 = Global, 1 = Per Tile, 2 = Per Tile Cell.
        char flags; // bit 0 : Tile System (1 = Enabled), bit 1 : Expanded Data (1 = Yes), bit 2 : Multi-colour Mode (1 = Enabled).

        quint16 num_chars; // 16-bits, Number of chars - 1 (low, high).

        quint16 num_tiles; // Number of tiles - 1.

        unsigned char tile_width; // Tile Width: 1-8
        unsigned char tile_height; // Tile Height: 1-8

        quint16 map_width; // 16-bit Map width (low, high).
        quint16 map_height; // 16-bit Map height (low, high).
    };
#pragma pack(pop)
    static_assert(sizeof(CTMHeader5) == 20, "Size is not correct");

#pragma pack(push)
#pragma pack(1)
    struct VChar64Header {
        char id[5]; // must be VChar
        char version; // must be 3
        char colors[4]; // BGR, MC1, MC2, RAM.
        char vic_res; // 0 = Hi Resolution, 1 = Multicolour.

        quint16 num_chars; // 16-bits, Number of chars - 1 (low, high).

        quint8 tile_width; // between 1-8
        quint8 tile_height; // between 1-8
        quint8 char_interleaved; // between 1-128

        // until here, it shares same structure as version 1

        char color_mode; // 0 = Global, 1 = Per Tile

        quint16 map_width; // 16-bit Map width (low, high).
        quint16 map_height; // 16-bit Map height (low, high).

        quint16 address_charset; // 16-bit for the Charset export address
        quint16 address_map; // 16-bit for the Map export address
        quint16 address_attribs; // 16-bit for the Attribs/Colors export address
        quint8 export_features; // 8 bits. what features should be exported: charset(1<<0), map(1<<1), color(1<<2)
        quint8 export_format; // 8 bits: export type: 0: RAW, 1:PRG, 2:ASM, 3:C

        char reserved[3]; // Must be 32 bytes in total

        // after the header comes:
        //  - charset[256 * 8]
        //  - tile_colors[256]
        //  - map_data[map_width * map_height bytes]
    };
#pragma pack(pop)
    static_assert(sizeof(VChar64Header) == 32, "Size is not correct");

#pragma pack(push)
#pragma pack(1)
    struct VICESnapshotHeader {
        char id[19]; // "VICE Snapshot File\032"
        char major;
        char minor;
        char machine[16]; // "C64" or "C128" or...
    };
    static_assert(sizeof(VICESnapshotHeader) == 37, "Size is not correct");

    struct VICESnapshotVersion {
        char id[13]; // "VICE Version\032"
        char viceversion[4]; // VICE number
        quint32 vice_svn_rev; // SVN revision, or 0
    };
    static_assert(sizeof(VICESnapshotVersion) == 21, "Size is not correct");

    struct VICESnapshoptModule {
        char moduleName[16]; // looking for "C64MEM"
        char major;
        char minor;
        quint32 lenght; // little endian
    };

    struct VICESnapshoptC64Mem {
        quint8 cpudata; // CPU port data byte
        quint8 cpudir; // CPU port direction byte
        quint8 exrom; // state of the EXROM line (?)
        quint8 game; // state of the GAME line (?)
        quint8 ram[65536]; // 64k RAM dump
    };

    struct VICESnapshoptC128Mem {
        quint8 mmu[11]; // MMU info
        quint8 ram[4 * 65536]; // 256k RAM dump
    };

    struct VICESnapshoptCIA2 {
        quint8 ora; // Output register A
        quint8 orb; // Output register B
        quint8 ddra; // Data direction register A
        quint8 ddrb; // Data direction register B
        quint16 tac; // Timer A counter value
        quint16 tbc; // Timer B counter value
        quint8 tod_ten; // Time of Day - current tenth of second
        quint8 tod_sec; // Time of Day - current seconds
        quint8 tod_min; // Time of Day - current minutes
        quint8 tod_hr; // Time of Day - current hours
        quint8 sdr; // contents of shift register
        quint8 ier; // mask of enabled interrupt masks
        quint8 cra; // Control register A
        quint8 crb; // Control register B
        quint16 tal; // Timer A latch value
        quint16 tbl; // Timer B latch value
        quint8 ifr; // mask of currently active interrupts
        quint8 pbstate; // Bit 6/7 reflect the PB6/7 toggle bit state.  Bit 2/3 reflect the corresponding port bit state.
        quint8 srhbits; // number of half-bits to still shift in/out SDR
        quint8 alarm_ten; // Time of Day - alarm tenth of second
        quint8 alarm_sec; // Time of Day - alarm seconds
        quint8 alarm_min; // Time of Day - alarm minutes
        quint8 alarm_hr; // Time of Day - alarm hours
        quint8 readicr; // current clock minus the clock when ICR was read last plus 128.
        quint8 todlatched; // Bit 0: 1= latched for reading, Bit 1: 2=stopped for writing
        quint8 todl_ten; // Time of Day - latched tenth of second
        quint8 todl_sec; // Time of Day - latched seconds
        quint8 todl_min; // Time of Day - latched minutes
        quint8 todl_hr; // Time of Day - latched hours
        quint32 tod_ticks; // clk ticks till next tenth of second
        quint16 tastate; // (v1.1+ only) The state bits of the CIA timer A, according to ciatimer.h
        quint16 tbstate; // (v1.1+ only) The state bits of the CIA timer B, according to ciatimer.h
    };
    struct VICESnapshoptVICII {
        quint8 allow_bad_lines; // flag: if true, bad lines can happen
        quint8 bad_line; // flag: this is a bad line
        quint8 blank_enabled; // flag: draw lines in border color
        quint8 color_buf[40]; // character memory buffer (loaded at bad line)
        quint8 color_ram[1024]; // contents of color RAM
        quint8 idle_state; // flag: idle state enabled
        quint8 lp_trigger; // flag: light pen has been triggered
        quint8 lp_x; // light pen X
        quint8 lp_Y; // light pen Y
        quint8 matrix_buf[40]; // video matrix buffer (loaded at bad line)
        quint8 new_sprite_dma_mask; // value for SpriteDmaMask after drawing sprites
        quint32 ram_base; // pointer to the start of RAM seen by the VIC
        quint8 raster_cycle; // current vicii.raster cycle
        quint16 raster_line; // current vicii.raster line
        quint8 registers[64]; // VIC-II registers

        // sprites data, and more should be here... too lazy to add it.
        // wanna help? send a Pull Request with the missing data. Take it from here:
        // https://sourceforge.net/p/vice-emu/code/HEAD/tree/trunk/vice/src/vicii/viciidtv-snapshot.c
    };
#pragma pack(pop)

protected:
    static qint64 loadCTM4(State* state, QFile& file, struct CTMHeader4* v5header);
    static qint64 loadCTM5(State* state, QFile& file, struct CTMHeader5* v5header);
};
