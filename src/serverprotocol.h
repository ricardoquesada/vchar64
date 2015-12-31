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
#ifndef SERVERPROTOCOL_H
#define SERVERPROTOCOL_H

/* shared both by cc64 (c64) and Qt projects */
/* only add pure C code */

#ifndef __CC65__
#pragma pack(push)
#pragma pack(1)
#endif  /* __CC65__ */

struct vchar64d_proto_header
{
    uint8_t type;
};
enum {
    TYPE_HELLO,
    TYPE_SET_BYTE,
    TYPE_SET_CHAR,
    TYPE_SET_CHARS,
    TYPE_SET_COLORS,
    TYPE_POKE,
    TYPE_BYEBYE
};

// one byte
struct vchar64d_proto_poke
{
    uint16_t addr;
    uint8_t value;
};

// one byte of the char
struct vchar64d_proto_set_byte
{
    uint16_t idx;
    uint8_t byte;
};

// 1 char: 8 bytes
struct vchar64d_proto_set_char
{
    uint8_t idx;
    uint8_t chardata[8];
};

// multiple chars: multiple 8 bytes
struct vchar64d_proto_set_chars
{
    uint8_t idx;
    uint8_t count;
    uint8_t *charsdata;
};

#define PROTO_VERSION 0x00
struct vchar64d_proto_hello
{
    uint8_t version;
};

#ifndef __CC65__
#pragma pack(pop)
#endif /* __CC65__ */

#endif // SERVERPROTOCOL_H

