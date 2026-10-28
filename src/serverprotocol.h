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
/**
 * @file serverprotocol.h
 * @brief Shared ANSI C89 TCP live-preview wire protocol (`vchar64d` C64 server & Qt desktop client).
 *
 * Compiled both by `cc65` (`server/vchar64d.c`, Contiki OS / uIP TCP stack on Commodore 64)
 * and by C++20 (`vchar64_net`). This header MUST strictly conform to ISO/IEC 9899:1990 (ANSI C89):
 * use traditional C block comments exclusively (zero C99/C++ single-line comments) and pack all
 * multi-byte wire fields as 1-byte-aligned little-endian integers matching the MOS 6510 CPU.
 */
#ifndef SERVERPROTOCOL_H
#define SERVERPROTOCOL_H

/* Shared by both cc65 (Commodore 64) and Qt C++ targets: pure ANSI C89 only. */

#include <stdint.h>

#ifndef __CC65__
#pragma pack(push)
#pragma pack(1)
#endif /* __CC65__ */

/** @brief Default TCP port on which `vchar64d` listens for desktop preview sessions (`6464`). */
#define VCHAR64_SERVER_LISTEN_PORT 6464

/**
 * @brief Maximum unacknowledged bytes (`1,400`) in flight before a `TYPE_PING` / `TYPE_PONG` sync.
 *
 * Contiki's uIP stack on the C64 uses a single MSS-sized receive buffer; exceeding 1,400 bytes
 * without waiting for `TYPE_PONG` drops or overruns incoming TCP segments on 1 MHz hardware.
 */
#define VCHAR64_SERVER_BUFFER_SIZE 1400

/** @brief 1-byte frame header preceding every wire command payload. */
struct vchar64d_proto_header {
    uint8_t type;
};

/** @brief Wire command opcodes carried in `vchar64d_proto_header::type`. */
enum {
    TYPE_HELLO,

    /* Character-RAM update opcodes (legacy aliases alongside TYPE_POKE / TYPE_SET_MEM). */
    TYPE_SET_BYTE_FOR_CHAR,
    TYPE_SET_CHAR,
    TYPE_SET_CHARS,

    TYPE_SET_MEM,
    TYPE_POKE,
    TYPE_FILL,
    TYPE_PING,
    TYPE_PONG,
    TYPE_BYEBYE
};

/** @brief `TYPE_POKE` payload: writes `value` to 16-bit LE C64 address `addr` (`3` bytes). */
struct vchar64d_proto_poke {
    uint16_t addr;
    uint8_t value;
};

/**
 * @brief `TYPE_SET_MEM` descriptor: 4-byte wire header (`addr`, `count` LE) followed by `count` raw bytes.
 *
 * Note: `data` is an in-memory pointer placeholder; on the wire, `count` payload bytes
 * immediately follow the 2-byte `addr` and 2-byte `count` fields (`1 + 4 + count` bytes total).
 */
struct vchar64d_proto_set_mem {
    uint16_t addr;
    uint16_t count;
    uint8_t* data;
};

/** @brief `TYPE_FILL` payload: fills `count` (16-bit LE) bytes starting at `addr` (16-bit LE) with `value` (`5` bytes). */
struct vchar64d_proto_fill {
    uint16_t addr;
    uint8_t value;
    uint16_t count;
};

/** @brief `TYPE_SET_BYTE_FOR_CHAR` payload: updates byte offset `idx` (`0..2047`, 16-bit LE) in charset RAM (`3` bytes). */
struct vchar64d_proto_set_byte {
    uint16_t idx;
    uint8_t byte;
};

/** @brief `TYPE_SET_CHAR` payload: replaces 8-byte glyph `idx` (`0..255`) in charset RAM (`9` bytes). */
struct vchar64d_proto_set_char {
    uint8_t idx;
    uint8_t chardata[8];
};

/**
 * @brief `TYPE_SET_CHARS` descriptor: 2-byte wire header (`idx`, `count`) followed by `count * 8` glyph bytes.
 *
 * Note: `charsdata` is an in-memory pointer placeholder; on the wire, `count * 8` bytes
 * immediately follow `idx` and `count` (`1 + 2 + count * 8` bytes total, chunked to `<= 128` chars).
 */
struct vchar64d_proto_set_chars {
    uint8_t idx;
    uint8_t count;
    uint8_t* charsdata;
};

/** @brief `TYPE_PING` / `TYPE_PONG` 1-byte flow-control Barrier payload (`2` bytes on wire with header). */
struct vchar64d_proto_ping {
    uint8_t something;
};

/** @brief Wire protocol version (`0x00`) exchanged in `TYPE_HELLO` upon TCP connection setup. */
#define PROTO_VERSION 0x00

/** @brief `TYPE_HELLO` handshake payload (`1` byte). */
struct vchar64d_proto_hello {
    uint8_t version;
};

#ifndef __CC65__
#pragma pack(pop)
#endif /* __CC65__ */

#endif /* SERVERPROTOCOL_H */
