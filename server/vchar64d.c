/* VChar64 server.
 * Based on the Telnet server from Contiki
 *
 * Copyright (c) 2015 Ricardo Quesada
 *
 * Copyright (c) 2006, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>

#include "contiki-net.h"
#include "serverprotocol.h"

PROCESS(vchar64d_process, "VChar64 server");

#if defined(__C64__) || defined(__C128__)
// peek and poke
#define outb(addr, val) (*(addr) = (val))
#define inb(addr) (*(addr))

// addresses
#if defined(__C64__)
#define MMU_ADDR ((unsigned char*)0x01)
#define OUTBASIC_INKERNAL_INCHARSET 0x32
#elif defined(__C128__)
#define MMU_ADDR ((unsigned char*)0xff00)
#define OUTBASIC_INKERNAL_INCHARSET 0x0f
#endif
#define OLD_CHARSET ((unsigned char*)0xd000)
#define NEW_CHARSET ((unsigned char*)0xa800)
#define SCREEN ((unsigned char*)0xa400)
#else
#error "Invalid target
#endif

#define ZERO_ADDR ((unsigned char*)0)

#define BUF_MAX_SIZE 128

struct vchar64d_buf {
    char bufmem[BUF_MAX_SIZE];
    int ptr;
    int size;
};
static struct vchar64d_buf buf;

struct vchar64d_state {
    uint16_t numsent;
    uint8_t state;
};
static struct vchar64d_state s;
enum {
    STATE_CLOSED,
    STATE_CONNECTED,
    STATE_INITED
};

/*---------------------------------------------------------------------------*/
static void init_vic()
{
    uint8_t old;
    uint8_t i, j, k;

    __asm__("sei");

#if defined(__C128__)
    // disable interrupt driven screen editor, in order
    // to avoid using the c128 shadow variables
    __asm__("lda #$ff");
    __asm__("sta $d8");

    // CHAREN: disable ROM Charset... not really needed
//    __asm__("lda $01");
//    __asm__("ora #$04");
//    __asm__("sta $01");
#endif

    // clear screen
    memset(SCREEN, 0x20, 40 * 25);

    // chars
    i = 0;
    for (j = 0; j < 8; ++j) {
        for (k = 0; k < 32; ++k) {
            // charset at the top
            outb(&SCREEN[j * 40 + k], i);

            // tileset at the middle
            outb(&SCREEN[40 * 12 + j * 40 + k], i);
            ++i;
        }
    }

    // VIC Bank 2: $8000 - $bfff
    old = inb(&CIA2.pra);
    outb(&CIA2.pra, (old & 0xfc) | 1);

    // enable CHARSET at 0xa800: XXXX101X
    //         SCREEN at 0xa400: 1001XXXX
    // bin: %10011010
    outb(&VIC.addr, 0x9a);

    // MMU: out BASIC, in CHARSET, in KERNAL
    old = inb(&MMU_ADDR[0]);
    outb(&MMU_ADDR[0], OUTBASIC_INKERNAL_INCHARSET);

    // copy new charset
    memcpy(NEW_CHARSET, OLD_CHARSET, 8 * 256);

    // restore old MMU: No BASIC, IO, KERNAL
    outb(&MMU_ADDR[0], old);

    __asm__("cli");
}

/*---------------------------------------------------------------------------*/
static void buf_init(struct vchar64d_buf* buf)
{
    buf->ptr = 0;
    buf->size = BUF_MAX_SIZE;
}
/*---------------------------------------------------------------------------*/
static int buf_append(struct vchar64d_buf* buf, const char* data, int len)
{
    int copylen;

    copylen = MIN(len, buf->size - buf->ptr);
    memcpy(&buf->bufmem[buf->ptr], data, copylen);
    buf->ptr += copylen;

    return copylen;
}
/*---------------------------------------------------------------------------*/
static void buf_copyto(struct vchar64d_buf* buf, char* to, int len)
{
    memcpy(to, &buf->bufmem[0], len);
}
/*---------------------------------------------------------------------------*/
static void buf_pop(struct vchar64d_buf* buf, int len)
{
    int poplen;

    poplen = MIN(len, buf->ptr);
    memcpy(&buf->bufmem[0], &buf->bufmem[poplen], buf->ptr - poplen);
    buf->ptr -= poplen;
}

/*---------------------------------------------------------------------------*/
static int buf_len(struct vchar64d_buf* buf)
{
    return buf->ptr;
}
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
static void acked(void)
{
    buf_pop(&buf, s.numsent);
}
/*---------------------------------------------------------------------------*/
static void senddata(void)
{
    int len;
    len = MIN(buf_len(&buf), uip_mss());
    buf_copyto(&buf, uip_appdata, len);
    uip_send(uip_appdata, len);
    s.numsent = len;
}
/*---------------------------------------------------------------------------*/

uint16_t proto_hello(struct vchar64d_proto_hello* data)
{
    //    printf("hello: %d\n", len);
    return sizeof(*data);
}

uint16_t proto_poke(struct vchar64d_proto_poke* data)
{
    // data->idx: is already in little endian
    outb(&ZERO_ADDR[data->addr], data->value);
    return sizeof(*data);
}

uint16_t proto_fill(struct vchar64d_proto_fill* data)
{
    memset((void*)data->addr, data->value, data->count);
    return sizeof(*data);
}

uint16_t proto_set_mem(struct vchar64d_proto_set_mem* data)
{
    memcpy((void*)data->addr, &data->data, data->count);
    // don't include the pointer
    return sizeof(*data) + data->count - sizeof(data->data);
}

uint16_t proto_set_byte(struct vchar64d_proto_set_byte* data)
{
    // data->idx: is already in little endian
    outb(&NEW_CHARSET[data->idx], data->byte);
    return sizeof(*data);
}

uint16_t proto_set_char(struct vchar64d_proto_set_char* data)
{
    //    printf("set_char: %d\n", len);
    memcpy(&NEW_CHARSET[data->idx * 8], &data->chardata, sizeof(data->chardata));
    return sizeof(*data);
}

uint16_t proto_set_chars(struct vchar64d_proto_set_chars* data)
{
    //    printf("proto_set_chars: %d\n", len);
    memcpy(&NEW_CHARSET[data->idx * 8], &data->charsdata, data->count * 8);
    // don't include the pointer
    return sizeof(*data) + data->count * 8 - sizeof(data->charsdata);
}

uint16_t proto_ping(struct vchar64d_proto_ping* data)
{
    struct {
        struct vchar64d_proto_header hdr;
        struct vchar64d_proto_ping payload;
    } response;

    response.hdr.type = TYPE_PONG;
    response.payload.something = data->something;
    buf_append(&buf, (char*)&response, sizeof(response));
    return sizeof(*data);
}

uint16_t proto_what(void)
{
    buf_append(&buf, "what?", 5);
    return 10000;
}

uint16_t proto_close(void)
{
    buf_append(&buf, "what?", 5);
    s.state = STATE_CLOSED;
    uip_close();

    // after close,
    return 10000;
}

static void newdata(void)
{
    uint16_t len, count;
    struct vchar64d_proto_header* header;
    void* payload;

    count = 0;
    len = uip_datalen();

    while (count < len) {
        header = &((struct vchar64d_proto_header*)uip_appdata)[count];
        payload = &((uint8_t*)uip_appdata)[++count];

        switch (header->type) {
            // charset related
        case TYPE_SET_BYTE_FOR_CHAR:
            count += proto_set_byte(payload);
            break;
        case TYPE_SET_CHAR:
            count += proto_set_char(payload);
            break;
        case TYPE_SET_CHARS:
            count += proto_set_chars(payload);
            break;
            // generic
        case TYPE_HELLO:
            count += proto_hello(payload);
            break;
        case TYPE_POKE:
            count += proto_poke(payload);
            break;
        case TYPE_FILL:
            count += proto_fill(payload);
            break;
        case TYPE_SET_MEM:
            count += proto_set_mem(payload);
            break;
        case TYPE_PING:
            count += proto_ping(payload);
            break;
        case TYPE_BYEBYE:
            count += proto_close();
            break;
        default:
            __asm__("inc $d020");
            count = len;
            break;
        }
    }
}

void vchar64d_appcall(void* ts)
{
    if (uip_connected()) {
        if (s.state == STATE_CLOSED) {
            buf_init(&buf);
            s.state = STATE_CONNECTED;
            ts = (char*)0;
        } else {
            uip_send("bye bye", 7);
            ts = (char*)1;
        }
        tcp_markconn(uip_conn, ts);
    }

    if (!ts) {
        if (uip_closed() || uip_aborted() || uip_timedout()) {
            s.state = STATE_CLOSED;
        }
        if (uip_acked()) {
            acked();
        }
        if (uip_newdata()) {
            newdata();
        }
        if (uip_rexmit() || uip_newdata() || uip_acked() || uip_connected() || uip_poll()) {
            senddata();
        }
    }
}

/*---------------------------------------------------------------------------*/
void vchar64d_quit(void)
{
    process_exit(&vchar64d_process);
    LOADER_UNLOAD();
}

PROCESS_THREAD(vchar64d_process, ev, data)
{
    PROCESS_BEGIN();

    printf("\nListening in port: %d\n", VCHAR64_SERVER_LISTEN_PORT);
    printf("Press any key to start servrer");
    while (!kbhit()) {
        /*        __asm__("inc $d020"); */
    }

    init_vic();

    // server port
    tcp_listen(UIP_HTONS(VCHAR64_SERVER_LISTEN_PORT));

    while (1) {
        PROCESS_WAIT_EVENT();
        if (ev == tcpip_event) {
            vchar64d_appcall(data);
        } else if (ev == PROCESS_EVENT_EXIT) {
            vchar64d_quit();
        } else {
        }
    }
    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
AUTOSTART_PROCESSES(&vchar64d_process);
/*---------------------------------------------------------------------------*/
