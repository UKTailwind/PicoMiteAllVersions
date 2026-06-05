/***********************************************************************************************************************
PicoMite MMBasic

custom.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_tcp_client_cmd.h"

static hal_net_tcp_client_t pico_tcp_client;
static int pico_tcp_client_opened;
static int pico_tcp_stream_active;
static uint8_t * pico_tcp_stream_buf;
static int pico_tcp_stream_size;
static int64_t * pico_tcp_stream_read;
static int64_t * pico_tcp_stream_write;

void close_tcpclient(void) {
    if (pico_tcp_client_opened) {
        hal_net_tcp_client_close(pico_tcp_client);
    }
    pico_tcp_client = 0;
    pico_tcp_client_opened = 0;
    pico_tcp_stream_active = 0;
    pico_tcp_stream_buf = NULL;
    pico_tcp_stream_size = 0;
    pico_tcp_stream_read = NULL;
    pico_tcp_stream_write = NULL;
}

void pico_tcp_client_stream_poll(void) {
    if (!pico_tcp_client_opened || !pico_tcp_stream_active ||
        !pico_tcp_stream_buf || !pico_tcp_stream_read ||
        !pico_tcp_stream_write || pico_tcp_stream_size <= 1)
        return;

    uint8_t buf[256];
    for (;;) {
        size_t got = 0;
        int rc = hal_net_tcp_client_recv(pico_tcp_client, buf, sizeof(buf),
                                         &got, 1);
        if (rc == HAL_NET_TIMEOUT || rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || got == 0) return;
        mm_net_tcp_client_stream_append(pico_tcp_stream_buf,
                                        pico_tcp_stream_size,
                                        pico_tcp_stream_read,
                                        pico_tcp_stream_write,
                                        buf, got);
    }
}

static void pico_tcp_client_open_cmd(unsigned char * tp) {
    mm_net_tcp_client_open_args_t parsed;
    mm_net_tcp_client_parse_open(tp, &parsed);

    close_tcpclient();
    int rc = hal_net_tcp_client_open(parsed.host, (uint16_t)parsed.port,
                                     (uint32_t)parsed.timeout_ms,
                                     &pico_tcp_client);
    if (rc == HAL_NET_TIMEOUT) error("No response from client");
    if (rc != HAL_NET_OK) error("Failed to open client");
    pico_tcp_client_opened = 1;
    if (!optionsuppressstatus) MMPrintString("Connected\r\n");
}

static void pico_tcp_client_request_cmd(unsigned char * tp) {
    if (!pico_tcp_client_opened) error("No connection");
    if (pico_tcp_stream_active) error("Connection busy");

    mm_net_tcp_client_request_args_t parsed;
    mm_net_tcp_client_parse_request(tp, &parsed);

    if (parsed.request_len &&
        hal_net_tcp_client_send(pico_tcp_client, parsed.request,
                                parsed.request_len,
                                (uint32_t)parsed.timeout_ms) != HAL_NET_OK)
        error("write failed");

    int total = 0;
    uint32_t wait_ms = (uint32_t)parsed.timeout_ms;
    while (total < parsed.payload_capacity) {
        size_t got = 0;
        int rc = hal_net_tcp_client_recv(
            pico_tcp_client, parsed.buffer + total,
            (size_t)(parsed.payload_capacity - total), &got, wait_ms);
        if (rc == HAL_NET_OK && got > 0) {
            total += (int)got;
            wait_ms = 200;
            routinechecks();
            continue;
        }
        if (total == 0 && rc != HAL_NET_OK) error("No response from server");
        break;
    }
    parsed.dest[0] = total;
}

static void pico_tcp_client_stream_cmd(unsigned char * tp) {
    if (!pico_tcp_client_opened) error("No connection");

    mm_net_tcp_client_stream_args_t parsed;
    mm_net_tcp_client_parse_stream(tp, &parsed);

    pico_tcp_stream_active = 0;
    pico_tcp_stream_buf = parsed.buffer;
    pico_tcp_stream_size = parsed.payload_capacity;
    pico_tcp_stream_read = parsed.read_pos;
    pico_tcp_stream_write = parsed.write_pos;
    if (*parsed.read_pos < 0 || *parsed.read_pos >= parsed.payload_capacity)
        *parsed.read_pos = 0;
    if (*parsed.write_pos < 0 || *parsed.write_pos >= parsed.payload_capacity)
        *parsed.write_pos = *parsed.read_pos;

    if (parsed.request_len &&
        hal_net_tcp_client_send(pico_tcp_client, parsed.request,
                                parsed.request_len, 5000) != HAL_NET_OK)
        error("write failed");
    pico_tcp_stream_active = 1;
}

int cmd_tcpclient(void) {
    unsigned char * tp;
    tp = checkstring(cmdline, (unsigned char *)"OPEN TCP CLIENT");
    if (tp) {
        pico_tcp_client_open_cmd(tp);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"OPEN TCP STREAM");
    if (tp) {
        pico_tcp_client_open_cmd(tp);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT REQUEST");
    if (tp) {
        pico_tcp_client_request_cmd(tp);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT STREAM");
    if (tp) {
        pico_tcp_client_stream_cmd(tp);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"CLOSE TCP CLIENT");
    if (tp) {
        if (*tp) error("Syntax");
        if (!pico_tcp_client_opened) error("No connection");
        close_tcpclient();
        return 1;
    }
    return 0;
}
