/***********************************************************************************************************************
PicoMite MMBasic

custom.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
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
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_http_page.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_tcp_server_cmd.h"
#include "shared/net/mm_net_transmit_cmd.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
bool optionsuppressstatus = 0;

//#define //DEBUG_printf printf
#define DEBUG_printf
const char httpheadersfail[] = "HTTP/1.0 404\r\n\r\n";
jmp_buf recover;

extern int pico_telnet_open(void);
extern void pico_telnet_close(void);
extern void pico_tftp_close(void);
extern void close_tcpclient(void);
extern void closeMQTT(void);
extern void port_web_clear_runtime_state(void);

static mm_net_tcp_service_t pico_tcp_server;
static mm_net_tcp_service_slot_t pico_tcp_slots[MaxPcb];
static uint8_t * pico_tcp_recv_buf[MaxPcb];
static char pico_tcp_path[MaxPcb][128];
static int pico_tcp_server_inited;

static void pico_tcp_server_service_init(void) {
    if (pico_tcp_server_inited) return;
    for (int i = 0; i < MaxPcb; i++) {
        if (!pico_tcp_recv_buf[i])
            pico_tcp_recv_buf[i] = GetMemory(TCP_READ_BUFFER_SIZE);
        mm_net_tcp_service_slot_init(&pico_tcp_slots[i], pico_tcp_recv_buf[i],
                                     TCP_READ_BUFFER_SIZE,
                                     pico_tcp_path[i],
                                     sizeof pico_tcp_path[i]);
    }
    mm_net_tcp_service_init(&pico_tcp_server, pico_tcp_slots, MaxPcb);
    pico_tcp_server_inited = 1;
}

void pico_tcp_server_poll(void) {
    pico_tcp_server_service_init();
    mm_net_tcp_service_poll(&pico_tcp_server);
}

int pico_tcp_server_request_pending(int pcb) {
    pico_tcp_server_service_init();
    return mm_net_tcp_service_request_pending(&pico_tcp_server, pcb);
}

const char * pico_tcp_server_path(int pcb) {
    pico_tcp_server_service_init();
    return mm_net_tcp_service_path(&pico_tcp_server, pcb);
}

static int pico_http_send_cb(void * ctx, const void * buf, size_t len) {
    int pcb = *(int *)ctx;
    return mm_net_tcp_service_send(&pico_tcp_server, pcb, buf, len, 5000) ==
           HAL_NET_OK;
}

void cleanserver(void) {
    port_web_clear_runtime_state();
}
void cmd_transmit(unsigned char * cmd) {
    mm_net_transmit_args_t parsed;
    if (!mm_net_transmit_parse(cmd, MaxPcb, &parsed)) error("Invalid option");
    if (!mm_net_tcp_service_conn(&pico_tcp_server, parsed.pcb))
        error("Not connected");
    if (parsed.kind == MM_NET_TRANSMIT_CODE) {
        int pcb = parsed.pcb;
        int tlen = parsed.status;
        if (setjmp(recover) != 0) error("Transmit failed");
        const char * reason = mm_net_http_status_reason(tlen);
        char body[96];
        int body_len = mm_net_http_format_status_body(body, sizeof body,
                                                      tlen, reason);
        if (body_len < 0) error("Output buffer too small");
        if (mm_net_http_send_response(tlen, reason, "text/plain",
                                      body, (size_t)body_len, "CPi",
                                      pico_http_send_cb, &pcb) != 0) {
            error("Transmit failed");
        }
        mm_net_tcp_service_close_slot(&pico_tcp_server, pcb);
        return;
    }

    if (parsed.kind == MM_NET_TRANSMIT_FILE ||
        parsed.kind == MM_NET_TRANSMIT_CSS ||
        parsed.kind == MM_NET_TRANSMIT_JS ||
        parsed.kind == MM_NET_TRANSMIT_IMAGE) {
        char * fname;
        char * ctype;
        int pcb = parsed.pcb;
        fname = (char *)parsed.filename;
        ctype = (char *)parsed.content_type;
        if (*fname == 0) error("Cannot find file");
        if (setjmp(recover) != 0) error("Transmit failed");
        int rc = mm_net_http_send_file(fname, ctype, "CPi",
                                       pico_http_send_cb, &pcb);
        if (rc == -1) {
            mm_net_tcp_service_send(&pico_tcp_server, pcb, httpheadersfail,
                                    strlen(httpheadersfail), 5000);
        }
        mm_net_tcp_service_close_slot(&pico_tcp_server, pcb);
        return;
    }
    if (parsed.kind == MM_NET_TRANSMIT_PAGE) {
        char * fname;
        int pcb = parsed.pcb;
        fname = (char *)parsed.filename;
        if (*fname == 0) error("Cannot find file");
        char * rendered = NULL;
        size_t rendered_len = 0;
        int rc = mm_net_http_render_page(fname, parsed.extra, &rendered, &rendered_len);
        if (rc == 0) {
            if (setjmp(recover) != 0) error("Transmit failed");
            if (mm_net_http_send_response(200, NULL, "text/html",
                                          rendered, rendered_len, "CPi",
                                          pico_http_send_cb,
                                          &pcb) != 0) {
                FreeMemory((void *)rendered);
                error("Transmit failed");
            }
            FreeMemory((void *)rendered);
        } else {
            mm_net_tcp_service_send(&pico_tcp_server, pcb, httpheadersfail,
                                    strlen(httpheadersfail), 5000);
        }
        mm_net_tcp_service_close_slot(&pico_tcp_server, pcb);
        return;
    }
    error("Invalid option");
}
int open_tcp_server(uint16_t port) {
    pico_tcp_server_service_init();
    if (port && WIFIconnected && !optionsuppressstatus) {
        MMPrintString("Starting TCP server at ");
        MMPrintString(ip4addr_ntoa(netif_ip4_addr(netif_list)));
        MMPrintString(" on port ");
        PInt(port);
        PRet();
    }
    if (port && !mm_net_tcp_service_open(&pico_tcp_server, port, MaxPcb)) {
        MMPrintString("Failed to create TCP server\r\n");
        return 0;
    }
    return 1;
}

void close_tcp_server(void) {
    pico_tcp_server_service_init();
    mm_net_tcp_service_stop(&pico_tcp_server);
}

static void pico_lifecycle_clear_tcp_requests(void) {
    if (!pico_tcp_server_inited) return;
    mm_net_tcp_service_clear_requests(&pico_tcp_server);
}

int cmd_tcpserver(void) {
    unsigned char * tp;
    tp = checkstring(cmdline, (unsigned char *)"TCP INTERRUPT");
    if (tp) {
        TCPreceiveInterrupt = mm_net_tcp_server_parse_interrupt(tp);
        InterruptUsed = true;
        TCPreceived = 0;
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP CLOSE");
    if (tp) {
        mm_net_tcp_server_slot_args_t parsed;
        mm_net_tcp_server_parse_slot(tp, MaxPcb, &parsed);
        mm_net_tcp_service_close_slot(&pico_tcp_server, parsed.pcb);
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TRANSMIT");
    if (tp) {
        cmd_transmit(tp);

        return 1;
    }

    tp = checkstring(cmdline, (unsigned char *)"TCP READ");
    if (tp) {
        mm_net_tcp_server_read_args_t parsed;
        mm_net_tcp_server_parse_read(tp, MaxPcb, &parsed);
        int rc = mm_net_tcp_service_read(&pico_tcp_server, parsed.pcb,
                                         parsed.dest, parsed.buffer,
                                         (size_t)parsed.payload_capacity,
                                         (size_t)parsed.array_bytes);
        if (rc == HAL_NET_WOULD_BLOCK) return 1;
        if (rc != HAL_NET_OK) error("array too small");
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP SEND");
    if (tp) {
        mm_net_tcp_server_send_args_t parsed;
        mm_net_tcp_server_parse_send(tp, MaxPcb, &parsed);
        if (mm_net_tcp_service_send(&pico_tcp_server, parsed.pcb,
                                    parsed.payload,
                                    parsed.payload_len, 5000) !=
            HAL_NET_OK)
            error("write failed");
        return 1;
    }
    return 0;
}

/* 3D sprite-builder teardown: real implementation lives in
 * drivers/gfx_3d/gfx_3d.c. WEB-only WiFi ports (WEB, WEBRP2350) don't
 * link gfx_3d.c — stub so FileIO.c::CloseAllFiles can call it
 * unconditionally. WiFi+PICOMITEVGA ports (F2 = VGAWIFIRP2350) DO
 * link gfx_3d.c (because their dispatch table needs fun_3D), so this
 * stub must NOT also be present there. */
#if !HAL_PORT_IS_VGA
void closeall3d(void) {}
#endif

/* Called from MMBasic.c::ClearRuntime() to clear pending TCP requests and
 * reset the suppress-status flag when a BASIC program restarts. */
void port_web_clear_runtime_state(void) {
    static const mm_net_lifecycle_runtime_hooks_t hooks = {
        .clear_tcp_requests = pico_lifecycle_clear_tcp_requests,
        .close_tcp_client = close_tcpclient,
        .close_mqtt = closeMQTT,
    };
    mm_net_lifecycle_runtime_reset(&hooks);
}
