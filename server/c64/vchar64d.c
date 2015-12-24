/* VChar64 server
 * Based on the Telnet server from Contiki
 */

/*
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
#include "sys/cc.h"
#include "contiki-lib.h"

PROCESS(vchar64d_process, "VChar64 server");

#define BUF_MAX_SIZE 128

struct vchar64d_buf {
    char bufmem[BUF_MAX_SIZE];
    int ptr;
    int size;
};
static struct vchar64d_buf buf;

struct vchar64d_state {
    char buf[BUF_MAX_SIZE + 1];
    char bufptr;
    uint16_t numsent;
    uint8_t state;
};
static struct vchar64d_state s;
enum {
    STATE_CLOSED,
    STATE_CONNECTED,
    STATE_INITED
};

/* protocol */
struct vchar64d_proto_header
{
    uint8_t type;
    /* uint8_t data[0]; */ /* not supported by cc65 */
};
enum {
    TYPE_HELLO,
    TYPE_SET_CHAR,
    TYPE_SET_RANGE,
    TYPE_SET_CHARSET,
    TYPE_BYEBYE
};

struct vchar64d_proto_set_char
{
    uint8_t idx;
    uint8_t chardata[8];
};

struct vchar64d_proto_set_charset
{
    uint8_t charset[256 * 8];
};

#define PROTO_VERSION 0x00
struct vchar64d_proto_hello
{
    uint8_t version;
};

/*---------------------------------------------------------------------------*/
static void buf_init(struct vchar64d_buf *buf)
{
    buf->ptr = 0;
    buf->size = BUF_MAX_SIZE;
}
/*---------------------------------------------------------------------------*/
static int buf_append(struct vchar64d_buf *buf, const char *data, int len)
{
    int copylen;

    copylen = MIN(len, buf->size - buf->ptr);
    memcpy(&buf->bufmem[buf->ptr], data, copylen);
    buf->ptr += copylen;

    return copylen;
}
/*---------------------------------------------------------------------------*/
static void buf_copyto(struct vchar64d_buf *buf, char *to, int len)
{
    memcpy(to, &buf->bufmem[0], len);
}
/*---------------------------------------------------------------------------*/
static void buf_pop(struct vchar64d_buf *buf, int len)
{
    int poplen;

    poplen = MIN(len, buf->ptr);
    memcpy(&buf->bufmem[0], &buf->bufmem[poplen], buf->ptr - poplen);
    buf->ptr -= poplen;
}

/*---------------------------------------------------------------------------*/
static int buf_len(struct vchar64d_buf *buf)
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

void proto_hello(struct vchar64d_proto_hello* data, int len)
{
    printf("hello: %d\n", len);
}

void proto_set_char(struct vchar64d_proto_set_char* data, int len)
{
    printf("set_char: %d\n", len);
}

void proto_set_charset(struct vchar64d_proto_set_charset* data, int len)
{
    printf("set_charset: %d\n", len);
}

void proto_what(struct vchar64d_proto_set_charset* data, int len)
{
    printf("what: %d\n", len);
    buf_append(&buf, "what?", 5);
}

void proto_close(void)
{
    buf_append(&buf, "what?", 5);
    s.state = STATE_CLOSED;
    uip_close();
}

static void newdata(void)
{
    uint16_t len;
    struct vchar64d_proto_header* header;
    void* payload;

    len = uip_datalen();
    header = uip_appdata;
    payload = &((uint8_t*)uip_appdata)[1];

    switch (header->type) {
        case TYPE_HELLO:
            proto_hello(payload, len-1);
            break;
        case TYPE_SET_CHAR:
            proto_set_char(payload, len-1);
            break;
        case TYPE_SET_CHARSET:
            proto_set_charset(payload, len-1);
            break;
        case TYPE_SET_RANGE:
            break;
        case TYPE_BYEBYE:
            proto_close();
            break;
        default:
            proto_what(payload, len-1);
            break;
    }
}

void vchar64d_appcall(void *ts)
{
    if(uip_connected()) {
        if(s.state == STATE_CLOSED) {
            buf_init(&buf);
            s.bufptr = 0;
            s.state = STATE_CONNECTED;
            ts = (char *)0;
        } else {
            uip_send("bye bye", 7);
            ts = (char *)1;
        }
        tcp_markconn(uip_conn, ts);
    }

    if(!ts) {
        if(uip_closed() || uip_aborted() || uip_timedout()) {
            s.state = STATE_CLOSED;
        }
        if(uip_acked()) {
            acked();
        }
        if(uip_newdata()) {
            newdata();
        }
        if(uip_rexmit() || uip_newdata() || uip_acked() || uip_connected() || uip_poll()) {
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

    tcp_listen(UIP_HTONS(6464));

    while(1) {
        PROCESS_WAIT_EVENT();
        if(ev == tcpip_event) {
            vchar64d_appcall(data);
        } else if(ev == PROCESS_EVENT_EXIT) {
            vchar64d_quit();
        } else {
        }
    }
    PROCESS_END();
}

void vchar64d_init(void)
{
    process_start(&vchar64d_process, NULL);
}

/*---------------------------------------------------------------------------*/
AUTOSTART_PROCESSES(&vchar64d_process);
/*---------------------------------------------------------------------------*/


