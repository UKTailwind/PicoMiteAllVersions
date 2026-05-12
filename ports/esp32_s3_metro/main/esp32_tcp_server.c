#include <stdint.h>
#include <string.h>

#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_http_page.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_tcp_server_cmd.h"
#include "shared/net/mm_net_transmit_cmd.h"
#include "esp32_tcp_server.h"

#define ESP32_MAX_PCB      8
#define ESP32_TCP_RECV_BUF 2048
#define ESP32_TCP_PATH_MAX 128

extern volatile bool TCPreceived;
extern char *TCPreceiveInterrupt;

static mm_net_tcp_service_t s_tcp;
static mm_net_tcp_service_slot_t s_tcp_slots[ESP32_MAX_PCB];
static uint8_t s_tcp_recv_buf[ESP32_MAX_PCB][ESP32_TCP_RECV_BUF];
static char s_tcp_path[ESP32_MAX_PCB][ESP32_TCP_PATH_MAX];

static void esp32_tcp_init_slots(void)
{
    static int inited;
    if (inited) return;
    inited = 1;
    for (int i = 0; i < ESP32_MAX_PCB; i++) {
        mm_net_tcp_service_slot_init(&s_tcp_slots[i], s_tcp_recv_buf[i],
                                     sizeof s_tcp_recv_buf[i], s_tcp_path[i],
                                     sizeof s_tcp_path[i]);
    }
    mm_net_tcp_service_init(&s_tcp, s_tcp_slots, ESP32_MAX_PCB);
}

static void esp32_tcp_close_slot(int pcb)
{
    esp32_tcp_init_slots();
    mm_net_tcp_service_close_slot(&s_tcp, pcb);
}

int esp32_tcp_interrupt_pending(void)
{
    esp32_tcp_init_slots();
    return mm_net_tcp_service_interrupt_pending(&s_tcp);
}

void esp32_tcp_server_poll(void)
{
    esp32_tcp_init_slots();
    mm_net_tcp_service_poll(&s_tcp);
}

void esp32_tcp_server_stop(void)
{
    esp32_tcp_init_slots();
    mm_net_tcp_service_stop(&s_tcp);
}

int esp32_tcp_server_open(uint16_t port)
{
    esp32_tcp_init_slots();
    if (!port) return 0;
    return mm_net_tcp_service_open(&s_tcp, port, ESP32_MAX_PCB);
}

void esp32_tcp_server_clear_requests(void)
{
    esp32_tcp_init_slots();
    mm_net_tcp_service_clear_requests(&s_tcp);
}

int esp32_tcp_server_max_connections(void)
{
    return ESP32_MAX_PCB;
}

int esp32_tcp_server_request_pending(int pcb)
{
    esp32_tcp_init_slots();
    return mm_net_tcp_service_request_pending(&s_tcp, pcb);
}

const char *esp32_tcp_server_path(int pcb)
{
    esp32_tcp_init_slots();
    return mm_net_tcp_service_path(&s_tcp, pcb);
}

static int esp32_http_send_cb(void *ctx, const void *buf, size_t len)
{
    int pcb = *(int *)ctx;
    if (!mm_net_tcp_service_conn(&s_tcp, pcb)) return 0;
    return mm_net_tcp_service_send(&s_tcp, pcb, buf, len, 5000) ==
           HAL_NET_OK;
}

static void esp32_tcp_send_http_buffer(int pcb, const char *ctype,
                                       const uint8_t *buf, size_t len)
{
    if (!mm_net_tcp_service_conn(&s_tcp, pcb)) error("Not connected");
    if (mm_net_http_send_response(200, NULL, ctype, buf, len,
                                  "MMBasic-ESP32", esp32_http_send_cb,
                                  &pcb) != 0) {
        error("Transmit failed");
    }
    esp32_tcp_close_slot(pcb);
}

static void esp32_tcp_send_status(int pcb, int status)
{
    if (!mm_net_tcp_service_conn(&s_tcp, pcb)) error("Not connected");
    const char *reason = mm_net_http_status_reason(status);

    char body[96];
    int body_len = mm_net_http_format_status_body(body, sizeof body,
                                                  status, reason);
    if (body_len < 0) error("Output buffer too small");
    if (mm_net_http_send_response(status, reason, "text/plain", body,
                                  (size_t)body_len, "MMBasic-ESP32",
                                  esp32_http_send_cb, &pcb) != 0) {
        error("Transmit failed");
    }
    esp32_tcp_close_slot(pcb);
}

static void esp32_tcp_transmit_file(int pcb, const char *fname,
                                    const char *ctype)
{
    if (!mm_net_tcp_service_conn(&s_tcp, pcb)) error("Not connected");
    if (!fname || !*fname) error("Cannot find file");

    int rc = mm_net_http_send_file(fname, ctype, "MMBasic-ESP32",
                                   esp32_http_send_cb, &pcb);
    if (rc == -1) {
        esp32_tcp_send_status(pcb, 404);
        return;
    }
    esp32_tcp_close_slot(pcb);
}

static void esp32_tcp_transmit_page(int pcb, const char *fname, int extra)
{
    if (!mm_net_tcp_service_conn(&s_tcp, pcb)) error("Not connected");
    if (!fname || !*fname) error("Cannot find file");
    if (extra < 0) error("Syntax");

    char *out = NULL;
    size_t len = 0;
    int rc = mm_net_http_render_page(fname, extra, &out, &len);
    if (rc == -1) {
        esp32_tcp_send_status(pcb, 404);
        return;
    }
    if (rc != 0) {
        esp32_tcp_send_status(pcb, 500);
        return;
    }

    esp32_tcp_send_http_buffer(pcb, "text/html", (const uint8_t *)out, len);
    FreeMemory((unsigned char *)out);
}

int esp32_transmit_cmd(unsigned char *tp)
{
    mm_net_transmit_args_t parsed;
    if (!mm_net_transmit_parse(tp, ESP32_MAX_PCB, &parsed)) return 0;

    switch (parsed.kind) {
        case MM_NET_TRANSMIT_CODE:
            esp32_tcp_send_status(parsed.pcb, parsed.status);
            return 1;
        case MM_NET_TRANSMIT_FILE:
        case MM_NET_TRANSMIT_CSS:
        case MM_NET_TRANSMIT_JS:
        case MM_NET_TRANSMIT_IMAGE:
            esp32_tcp_transmit_file(parsed.pcb, parsed.filename,
                                    parsed.content_type);
            return 1;
        case MM_NET_TRANSMIT_PAGE:
            esp32_tcp_transmit_page(parsed.pcb, parsed.filename, parsed.extra);
            return 1;
        default:
            return 0;
    }
}

int esp32_tcp_cmd(unsigned char *tp)
{
    unsigned char *arg;

    arg = checkstring(tp, (unsigned char *)"INTERRUPT");
    if (arg) {
        TCPreceiveInterrupt = mm_net_tcp_server_parse_interrupt(arg);
        InterruptUsed = true;
        TCPreceived = false;
        return 1;
    }

    arg = checkstring(tp, (unsigned char *)"CLOSE");
    if (arg) {
        mm_net_tcp_server_slot_args_t parsed;
        mm_net_tcp_server_parse_slot(arg, ESP32_MAX_PCB, &parsed);
        esp32_tcp_close_slot(parsed.pcb);
        return 1;
    }

    arg = checkstring(tp, (unsigned char *)"READ");
    if (arg) {
        mm_net_tcp_server_read_args_t parsed;
        mm_net_tcp_server_parse_read(arg, ESP32_MAX_PCB, &parsed);
        ProcessWeb(0);
        int rc = mm_net_tcp_service_read(&s_tcp, parsed.pcb, parsed.dest,
                                         parsed.buffer,
                                         (size_t)parsed.payload_capacity,
                                         (size_t)parsed.array_bytes);
        if (rc == HAL_NET_WOULD_BLOCK) return 1;
        if (rc != HAL_NET_OK) error("array too small");
        return 1;
    }

    arg = checkstring(tp, (unsigned char *)"SEND");
    if (arg) {
        mm_net_tcp_server_send_args_t parsed;
        mm_net_tcp_server_parse_send(arg, ESP32_MAX_PCB, &parsed);
        if (!mm_net_tcp_service_conn(&s_tcp, parsed.pcb))
            error("Not connected");
        if (mm_net_tcp_service_send(&s_tcp, parsed.pcb, parsed.payload,
                                    parsed.payload_len, 5000) != HAL_NET_OK)
            error("write failed");
        return 1;
    }

    return 0;
}
