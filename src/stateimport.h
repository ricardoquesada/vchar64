/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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

/**
 * @file stateimport.h
 * @brief Headless binary file parsers and packed wire headers for `vchar64_core`.
 *
 * Defines `KoalaBitmapData`, `StateImport`, and `#pragma pack(push, 1)` on-disk
 * header structures for VChar64 projects (v1–v3), CharPad CTM (v4 and v5),
 * Koala Painter multicolor bitmaps, C64 PRG/raw charsets, and VICE emulator
 * `.vsf` machine snapshots (`C64` and `C128`).
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QIODevice>
#include <QString>

#include "core/result.h"

class State;

/**
 * @brief Decoded Koala Painter (`.koa` / `.kla`) multicolor bitmap payload.
 *
 * Supports both canonical `10,003`-byte Koala files (2-byte LE load address +
 * 8,000-byte bitmap + 1,000-byte Screen RAM + 1,000-byte Color RAM + 1-byte
 * `$D021` background color) and `10,002`-byte Koala files omitting the trailing
 * background byte (`backgroundColor` defaults to `0`).
 */
struct KoalaBitmapData {
    static constexpr size_t kBitmapSize = 8000;
    static constexpr size_t kScreenRamSize = 1000;
    static constexpr size_t kColorRamSize = 1000;
    static constexpr qint64 kMinFileSize = 2 + 8000 + 1000 + 1000; // 10,002 bytes
    static constexpr qint64 kMaxFileSize = kMinFileSize + 1; // 10,003 bytes
    static constexpr qint64 kSizeWithoutBg = kMinFileSize;
    static constexpr qint64 kSizeWithBg = kMaxFileSize;

    uint16_t loadAddress = 0x6000U;
    std::array<uint8_t, kBitmapSize> bitmap {};
    std::array<uint8_t, kScreenRamSize> screenRam {};
    std::array<uint8_t, kColorRamSize> colorRam {};
    uint8_t backgroundColor = 0U;
};

/**
 * @brief Headless, bounds-checked binary importers for VChar64, CharPad CTM,
 *        PRG/64C, Raw charsets, Koala Painter bitmaps, and VICE snapshots.
 *
 * Every importer validates headers, dimensions, endianness, and payload sizes
 * into local staging buffers before committing to `State` via non-undo mutators,
 * guaranteeing zero `Qt6::Widgets` coupling and transactional failure safety.
 */
class StateImport {
public:
    using KoalaBitmap = KoalaBitmapData;

    /**
     * @brief Loads up to `2,048` raw glyph bytes from `device` into `state`.
     *
     * Payloads smaller than `2,048` bytes zero-pad the remaining glyphs; payloads
     * larger than `2,048` bytes clamp to `256` characters and record a non-fatal
     * warning in `ImportReport::warnings`.
     */
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadRaw(
        State& state,
        QIODevice& device);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadRaw(
        State& state,
        const QString& filename);

    /**
     * @brief Loads a C64 `.prg` or `.64c` file (2-byte little-endian load address + raw glyph payload).
     *
     * @param state Destination document state (mutated only on success).
     * @param device Open, readable `QIODevice` containing at least 3 bytes (2-byte address + `>= 1` glyph byte).
     * @param outAddress Optional output receiving the decoded 16-bit little-endian load address.
     */
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadPRG(
        State& state,
        QIODevice& device,
        quint16* outAddress = nullptr);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadPRG(
        State& state,
        const QString& filename,
        quint16* outAddress = nullptr);

    /**
     * @brief Loads a CharPad `.ctm` v4 (`24`-byte header) or v5 (`20`-byte header) project.
     *
     * Peeks only the 4-byte `"CTM"` + version prefix before dispatching to `loadCTM4()`
     * or `loadCTM5()`, avoiding cross-struct reinterpret-cast UB across differently sized headers.
     */
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadCTM(
        State& state,
        QIODevice& device);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadCTM(
        State& state,
        const QString& filename);

    /**
     * @brief Loads a native `.vchar64proj` v1, v2, or v3 file into `state`.
     *
     * Enforces explicit `qFromLittleEndian` conversion on all 16-bit header fields,
     * validates tile geometry (`W * H <= 256`, `interleaved == 1` or `256 / (W * H)`),
     * and clamps character counts exceeding `256`.
     */
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadVChar64(
        State& state,
        QIODevice& device);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadVChar64(
        State& state,
        const QString& filename);

    /** @brief Parses and validates a `10,002`-byte or `10,003`-byte Koala Painter multicolor bitmap. */
    [[nodiscard]] static vchar64::Result<KoalaBitmapData> loadKoala(QIODevice& device);
    [[nodiscard]] static vchar64::Result<KoalaBitmapData> loadKoala(const QString& filename);

    /**
     * @brief Extracts 64 KiB C64 RAM, VIC-II bank/charset/screen pointers, Color RAM, and VIC registers from a VICE `.vsf` snapshot.
     *
     * Supports both `C64` (`C64MEM`) and `C128` (`C128MEM` Bank 0) snapshots, as well as
     * CIA2 `$DD00` module v1.0 (`35` payload bytes) and v1.1+ (`39` payload bytes) to
     * compute the active 16 KiB VIC-II bank (`(3 - (ora & 0x03)) * 0x4000`).
     */
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> parseVICESnapshot(
        QIODevice& device,
        uint8_t* buffer64k,
        uint16_t* outCharsetAddress,
        uint16_t* outScreenRAMAddress,
        uint8_t* outColorRAMBuf,
        uint8_t* outVICRegistersBuf);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> parseVICESnapshot(
        const QString& filename,
        uint8_t* buffer64k,
        uint16_t* outCharsetAddress,
        uint16_t* outScreenRAMAddress,
        uint8_t* outColorRAMBuf,
        uint8_t* outVICRegistersBuf);

#pragma pack(push, 1)
    /** @brief Packed 24-byte CharPad v4 file header (`"CTM\x04"`). */
    struct CTMHeader4 {
        char id[3]; // "CTM"
        uint8_t version; // 4
        uint8_t colors[4]; // BGR, MC1, MC2, RAM
        uint8_t color_mode; // 0 = Global, 1 = Per Tile, 2 = Per Tile Cell
        uint8_t vic_res; // 0 = Hi Resolution, 1 = Multicolour
        uint16_t num_chars; // 16-bit LE, Number of chars - 1
        uint8_t num_tiles; // Number of tiles - 1
        uint8_t tile_width; // Tile Width (1-8)
        uint8_t tile_height; // Tile Height (1-8)
        uint16_t map_width; // 16-bit LE Map width
        uint16_t map_height; // 16-bit LE Map height
        uint8_t expanded; // 1 = Expanded form
        uint8_t reserved[4]; // Total header size: 24 bytes
    };
    static_assert(sizeof(CTMHeader4) == 24, "Invalid CTMHeader4 size");

    /** @brief Packed 20-byte CharPad v5 file header (`"CTM\x05"`). */
    struct CTMHeader5 {
        char id[3]; // "CTM"
        uint8_t version; // 5
        uint8_t colors[4]; // BGR, MC1, MC2, RAM
        uint8_t color_mode; // 0 = Global, 1 = Per Tile, 2 = Per Character
        uint8_t flags; // bit 0: Tile System, bit 1: Expanded, bit 2: Multicolour
        uint16_t num_chars; // 16-bit LE, Number of chars - 1
        uint16_t num_tiles; // 16-bit LE, Number of tiles - 1
        uint8_t tile_width; // Tile Width (1-8, or 0 when Tile System disabled)
        uint8_t tile_height; // Tile Height (1-8, or 0 when Tile System disabled)
        uint16_t map_width; // 16-bit LE Map width
        uint16_t map_height; // 16-bit LE Map height
    };
    static_assert(sizeof(CTMHeader5) == 20, "Invalid CTMHeader5 size");

    /** @brief Packed 32-byte native `.vchar64proj` file header (versions 1, 2, and 3). */
    struct VChar64Header {
        char id[5]; // "VChar"
        uint8_t version; // 1, 2, or 3
        uint8_t colors[4]; // BGR, MC1, MC2, RAM
        uint8_t vic_res; // 0 = Hi Resolution, 1 = Multicolour
        uint16_t num_chars; // 16-bit LE number of chars (typically 256)
        uint8_t tile_width; // 1-8
        uint8_t tile_height; // 1-8
        uint8_t char_interleaved; // 1-128

        // v2+ fields
        uint8_t color_mode; // 0 = Global, 1 = Per Tile
        uint16_t map_width; // 16-bit LE Map width
        uint16_t map_height; // 16-bit LE Map height

        // v3+ fields
        uint16_t address_charset; // 16-bit LE Charset export address
        uint16_t address_map; // 16-bit LE Map export address
        uint16_t address_attribs; // 16-bit LE Attribs/Colors export address
        uint8_t export_features; // bitmask: charset(1<<0), map(1<<1), colors(1<<2)
        uint8_t export_format; // 0: RAW, 1: PRG, 2: ASM, 3: C
        uint8_t keyboard_mapping; // 0: C64 screen code, 1: Atari 8-bit

        uint8_t reserved[2]; // Zeroed; 32 bytes total
    };
    static_assert(sizeof(VChar64Header) == 32, "Invalid VChar64Header size");

    /** @brief Packed 37-byte top-level VICE `.vsf` file header. */
    struct VICESnapshotHeader {
        char id[19]; // "VICE Snapshot File\032"
        uint8_t major;
        uint8_t minor;
        char machine[16]; // "C64", "C128", ...
    };
    static_assert(sizeof(VICESnapshotHeader) == 37, "Invalid VICESnapshotHeader size");

    /** @brief Packed 21-byte optional VICE 2.4.x+ `"VICE Version\032"` header extension. */
    struct VICESnapshotVersion {
        char id[13]; // "VICE Version\032"
        uint8_t viceversion[4];
        uint32_t vice_svn_rev;
    };
    static_assert(sizeof(VICESnapshotVersion) == 21, "Invalid VICESnapshotVersion size");

    /** @brief Packed 22-byte header preceding each module chunk inside a VICE `.vsf` stream. */
    struct VICESnapshotModule {
        char moduleName[16];
        uint8_t major;
        uint8_t minor;
        uint32_t length; // 32-bit LE total module size (including this 22-byte header)
    };
    static_assert(sizeof(VICESnapshotModule) == 22, "Invalid VICESnapshotModule size");
    using VICESnapshoptModule = VICESnapshotModule;

    /** @brief Packed `C64MEM` module payload (4 CPU port bytes + 64 KiB system RAM; heap-allocated during parsing). */
    struct VICESnapshotC64Mem {
        uint8_t cpudata;
        uint8_t cpudir;
        uint8_t exrom;
        uint8_t game;
        uint8_t ram[65536];
    };
    using VICESnapshoptC64Mem = VICESnapshotC64Mem;

    /** @brief Packed `C128MEM` module payload (11 MMU bytes + 256 KiB RAM across 4 banks; heap-allocated during parsing). */
    struct VICESnapshotC128Mem {
        uint8_t mmu[11];
        uint8_t ram[4 * 65536];
    };
    using VICESnapshoptC128Mem = VICESnapshotC128Mem;

    /** @brief Packed `CIA2` module payload (`39` bytes in v1.1+; `ora & 0x03` selects the 16 KiB VIC-II bank). */
    struct VICESnapshotCIA2 {
        uint8_t ora;
        uint8_t orb;
        uint8_t ddra;
        uint8_t ddrb;
        uint16_t tac;
        uint16_t tbc;
        uint8_t tod_ten;
        uint8_t tod_sec;
        uint8_t tod_min;
        uint8_t tod_hr;
        uint8_t sdr;
        uint8_t ier;
        uint8_t cra;
        uint8_t crb;
        uint16_t tal;
        uint16_t tbl;
        uint8_t ifr;
        uint8_t pbstate;
        uint8_t srhbits;
        uint8_t alarm_ten;
        uint8_t alarm_sec;
        uint8_t alarm_min;
        uint8_t alarm_hr;
        uint8_t readicr;
        uint8_t todlatched;
        uint8_t todl_ten;
        uint8_t todl_sec;
        uint8_t todl_min;
        uint8_t todl_hr;
        uint32_t tod_ticks;
        uint16_t tastate;
        uint16_t tbstate;
    };
    using VICESnapshoptCIA2 = VICESnapshotCIA2;

    /** @brief Packed `VIC-II` module payload providing 1 KiB Color RAM (`color_ram`) and 64 `$D000` registers (`registers`). */
    struct VICESnapshotVICII {
        uint8_t allow_bad_lines;
        uint8_t bad_line;
        uint8_t blank_enabled;
        uint8_t color_buf[40];
        uint8_t color_ram[1024];
        uint8_t idle_state;
        uint8_t lp_trigger;
        uint8_t lp_x;
        uint8_t lp_Y;
        uint8_t matrix_buf[40];
        uint8_t new_sprite_dma_mask;
        uint32_t ram_base;
        uint8_t raster_cycle;
        uint16_t raster_line;
        uint8_t registers[64];
    };
    using VICESnapshoptVICII = VICESnapshotVICII;
#pragma pack(pop)

private:
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadCTM4(
        State& state,
        QIODevice& device);
    [[nodiscard]] static vchar64::Result<vchar64::ImportReport> loadCTM5(
        State& state,
        QIODevice& device);
};
