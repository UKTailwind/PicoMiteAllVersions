#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "drivers/web_console/web_console_assets.h"
#include "drivers/web_console/web_console_display.h"
#include "drivers/web_console/web_console_protocol.h"
#include "drivers/web_console/web_console_transport.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_http_page.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_tcp_server_cmd.h"
#include "shared/net/mm_net_transmit_cmd.h"
#include "shared/net/mm_net_websocket.h"
#include "esp32_tcp_server.h"

#define ESP32_MAX_PCB      8
#define ESP32_TCP_RECV_BUF 2048
#define ESP32_TCP_PATH_MAX 128
#define ESP32_WEB_CONSOLE_RX_BUF 1024
#define ESP32_WEB_CONSOLE_PAYLOAD_LIMIT 512
#define ESP32_WEB_CONSOLE_TX_BUF 2048
#define ESP32_WEB_CONSOLE_SEND_CHUNK 1024

typedef enum {
    ESP32_WEB_TX_IDLE = 0,
    ESP32_WEB_TX_FRMB_WS_HEADER,
    ESP32_WEB_TX_FRMB_PAYLOAD_HEADER,
    ESP32_WEB_TX_FRMB_PIXELS,
} esp32_web_console_tx_kind_t;

extern volatile bool TCPreceived;
extern char *TCPreceiveInterrupt;

static mm_net_tcp_service_t s_tcp;
static mm_net_tcp_service_slot_t s_tcp_slots[ESP32_MAX_PCB];
static uint8_t s_tcp_recv_buf[ESP32_MAX_PCB][ESP32_TCP_RECV_BUF];
static char s_tcp_path[ESP32_MAX_PCB][ESP32_TCP_PATH_MAX];
static uint8_t s_tcp_accept_buf[ESP32_TCP_RECV_BUF];

typedef struct {
    hal_net_tcp_conn_t conn;
    uint8_t rx_buf[ESP32_WEB_CONSOLE_RX_BUF];
    size_t rx_len;
    esp32_web_console_tx_kind_t tx_kind;
    uint8_t ws_header[10];
    size_t ws_header_len;
    size_t ws_header_off;
    uint8_t payload_header[WEB_CONSOLE_FRAME_HEADER_LEN];
    size_t payload_header_off;
    size_t pixel_index;
    uint8_t pixel_chunk[ESP32_WEB_CONSOLE_TX_BUF];
    size_t pixel_chunk_len;
    size_t pixel_chunk_off;
    uint8_t pending_pong[125];
    size_t pending_pong_len;
    int pending_pong_valid;
} esp32_web_console_ws_t;

static esp32_web_console_ws_t s_web_console_ws;
static uint8_t s_web_console_tx_buf[ESP32_WEB_CONSOLE_TX_BUF];
static uint8_t s_web_console_payload[ESP32_WEB_CONSOLE_TX_BUF - 16u];

extern web_console_display_t *esp32_web_console_display(void);

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

static int esp32_tcp_find_free_slot(void)
{
    for (int i = 0; i < ESP32_MAX_PCB; i++) {
        if (!s_tcp_slots[i].conn && !s_tcp_slots[i].inttrig) return i;
    }
    return -1;
}

static void esp32_tcp_refresh_received(void)
{
    TCPreceived = mm_net_tcp_service_interrupt_pending(&s_tcp) ? true : false;
}

static int esp32_conn_send_cb(void *ctx, const void *buf, size_t len)
{
    hal_net_tcp_conn_t conn = *(hal_net_tcp_conn_t *)ctx;
    return hal_net_tcp_conn_send(conn, buf, len, 5000) == HAL_NET_OK;
}

static void esp32_send_http_status_conn(hal_net_tcp_conn_t conn, int status)
{
    const char *reason = mm_net_http_status_reason(status);
    char body[96];
    int body_len = mm_net_http_format_status_body(body, sizeof body,
                                                  status, reason);
    if (body_len >= 0) {
        (void)mm_net_http_send_response(status, reason, "text/plain", body,
                                        (size_t)body_len, "MMBasic-ESP32",
                                        esp32_conn_send_cb, &conn);
    }
    hal_net_tcp_conn_close(conn);
}

static void esp32_send_asset_conn(hal_net_tcp_conn_t conn,
                                  const web_console_asset_t *asset)
{
    if (asset) {
        (void)mm_net_http_send_response(200, NULL, asset->content_type,
                                        asset->data, asset->len,
                                        "MMBasic-ESP32",
                                        esp32_conn_send_cb, &conn);
    }
    hal_net_tcp_conn_close(conn);
}

static int esp32_path_has_prefix(const char *path, const char *prefix)
{
    size_t n = strlen(prefix);
    return path && strncmp(path, prefix, n) == 0;
}

static int esp32_path_equals_ignore_query(const char *path, const char *want)
{
    if (!path || !want) return 0;
    size_t n = 0;
    while (path[n] && path[n] != '?') n++;
    return strlen(want) == n && memcmp(path, want, n) == 0;
}

static void esp32_web_console_close(void)
{
    if (s_web_console_ws.conn) hal_net_tcp_conn_close(s_web_console_ws.conn);
    memset(&s_web_console_ws, 0, sizeof s_web_console_ws);
}

static int esp32_web_console_send_frame(uint8_t opcode, const void *payload,
                                        size_t payload_len)
{
    if (!s_web_console_ws.conn) return WEB_CONSOLE_TRANSPORT_CLOSED;
    if (s_web_console_ws.tx_kind != ESP32_WEB_TX_IDLE) {
        return WEB_CONSOLE_TRANSPORT_BACKPRESSURE;
    }
    size_t written = 0;
    int rc = mm_net_ws_encode_frame(opcode, payload, payload_len,
                                    s_web_console_tx_buf,
                                    sizeof s_web_console_tx_buf, &written);
    if (rc != MM_NET_WS_OK) return WEB_CONSOLE_TRANSPORT_ERROR;
    if (hal_net_tcp_conn_send(s_web_console_ws.conn, s_web_console_tx_buf,
                              written, 100) != HAL_NET_OK) {
        esp32_web_console_close();
        return WEB_CONSOLE_TRANSPORT_CLOSED;
    }
    return WEB_CONSOLE_TRANSPORT_OK;
}

static void esp32_web_console_send_text(const char *text)
{
    if (text) {
        (void)esp32_web_console_send_frame(MM_NET_WS_OPCODE_TEXT, text,
                                           strlen(text));
    }
}

static void esp32_web_console_send_hello(void)
{
    esp32_web_console_send_text(
        "{\"op\":\"hello\",\"protocol\":1,\"target\":\"esp32_s3_metro\","
        "\"caps\":[\"status\",\"control\",\"display\"],\"display\":true,"
        "\"audio\":false,\"keyboard\":false}");
}

static void esp32_web_console_send_status(void)
{
    char json[192];
    int n = snprintf(json, sizeof json,
                     "{\"op\":\"status\",\"phase\":4,\"ws\":\"connected\","
                     "\"display\":\"ready\",\"audio\":\"pending\","
                     "\"keyboard\":\"pending\"}");
    if (n > 0 && (size_t)n < sizeof json) esp32_web_console_send_text(json);
}

static void put_u16_le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_u64_be(uint8_t *dst, uint64_t v)
{
    for (int i = 0; i < 8; ++i) dst[7 - i] = (uint8_t)(v >> (i * 8));
}

static void esp32_web_console_start_frmb(web_console_display_t *display)
{
    if (!s_web_console_ws.conn || !display) return;
    uint64_t payload_len = WEB_CONSOLE_FRAME_HEADER_LEN +
        (uint64_t)display->pixel_count * 4u;
    s_web_console_ws.ws_header[0] = 0x80u | MM_NET_WS_OPCODE_BINARY;
    s_web_console_ws.ws_header[1] = 127u;
    put_u64_be(s_web_console_ws.ws_header + 2, payload_len);
    s_web_console_ws.ws_header_len = 10;
    s_web_console_ws.ws_header_off = 0;
    memcpy(s_web_console_ws.payload_header, "FRMB", 4);
    put_u16_le(s_web_console_ws.payload_header + 4,
               (uint16_t)display->width);
    put_u16_le(s_web_console_ws.payload_header + 6,
               (uint16_t)display->height);
    s_web_console_ws.payload_header_off = 0;
    s_web_console_ws.pixel_index = 0;
    s_web_console_ws.pixel_chunk_len = 0;
    s_web_console_ws.pixel_chunk_off = 0;
    s_web_console_ws.tx_kind = ESP32_WEB_TX_FRMB_WS_HEADER;
}

static int esp32_web_console_send_raw_progress(const uint8_t *buf, size_t len,
                                               size_t *off)
{
    if (!s_web_console_ws.conn) return -1;
    if (*off >= len) return 1;
    size_t n = len - *off;
    if (n > ESP32_WEB_CONSOLE_SEND_CHUNK) n = ESP32_WEB_CONSOLE_SEND_CHUNK;
    int rc = hal_net_tcp_conn_send(s_web_console_ws.conn, buf + *off, n, 1);
    if (rc == HAL_NET_TIMEOUT || rc == HAL_NET_WOULD_BLOCK) return 0;
    if (rc != HAL_NET_OK) {
        esp32_web_console_close();
        return -1;
    }
    *off += n;
    return *off >= len ? 1 : 0;
}

static int esp32_web_console_progress_frmb(web_console_display_t *display)
{
    if (!display) {
        s_web_console_ws.tx_kind = ESP32_WEB_TX_IDLE;
        return -1;
    }
    if (s_web_console_ws.tx_kind == ESP32_WEB_TX_FRMB_WS_HEADER) {
        int rc = esp32_web_console_send_raw_progress(
            s_web_console_ws.ws_header, s_web_console_ws.ws_header_len,
            &s_web_console_ws.ws_header_off);
        if (rc <= 0) return rc;
        s_web_console_ws.tx_kind = ESP32_WEB_TX_FRMB_PAYLOAD_HEADER;
    }
    if (s_web_console_ws.tx_kind == ESP32_WEB_TX_FRMB_PAYLOAD_HEADER) {
        int rc = esp32_web_console_send_raw_progress(
            s_web_console_ws.payload_header,
            sizeof s_web_console_ws.payload_header,
            &s_web_console_ws.payload_header_off);
        if (rc <= 0) return rc;
        s_web_console_ws.tx_kind = ESP32_WEB_TX_FRMB_PIXELS;
    }
    while (s_web_console_ws.tx_kind == ESP32_WEB_TX_FRMB_PIXELS) {
        if (s_web_console_ws.pixel_chunk_off >=
            s_web_console_ws.pixel_chunk_len) {
            if (s_web_console_ws.pixel_index >= display->pixel_count) {
                s_web_console_ws.tx_kind = ESP32_WEB_TX_IDLE;
                return 1;
            }
            size_t remaining = display->pixel_count -
                               s_web_console_ws.pixel_index;
            size_t pixels = sizeof s_web_console_ws.pixel_chunk / 4u;
            if (pixels > remaining) pixels = remaining;
            uint8_t *p = s_web_console_ws.pixel_chunk;
            const uint32_t *src = display->pixels +
                                  s_web_console_ws.pixel_index;
            for (size_t i = 0; i < pixels; ++i) {
                uint32_t c = src[i];
                *p++ = (uint8_t)((c >> 16) & 0xffu);
                *p++ = (uint8_t)((c >> 8) & 0xffu);
                *p++ = (uint8_t)(c & 0xffu);
                *p++ = 0xffu;
            }
            s_web_console_ws.pixel_index += pixels;
            s_web_console_ws.pixel_chunk_len = pixels * 4u;
            s_web_console_ws.pixel_chunk_off = 0;
        }
        int rc = esp32_web_console_send_raw_progress(
            s_web_console_ws.pixel_chunk, s_web_console_ws.pixel_chunk_len,
            &s_web_console_ws.pixel_chunk_off);
        if (rc <= 0) return rc;
    }
    return 1;
}

static int esp32_web_console_send_binary_payload_now(const uint8_t *payload,
                                                     size_t payload_len)
{
    size_t written = 0;
    int rc = mm_net_ws_encode_frame(MM_NET_WS_OPCODE_BINARY, payload,
                                    payload_len, s_web_console_tx_buf,
                                    sizeof s_web_console_tx_buf, &written);
    if (rc != MM_NET_WS_OK) return 0;
    rc = hal_net_tcp_conn_send(s_web_console_ws.conn, s_web_console_tx_buf,
                               written, 1);
    if (rc == HAL_NET_TIMEOUT || rc == HAL_NET_WOULD_BLOCK) return 0;
    if (rc != HAL_NET_OK) {
        esp32_web_console_close();
        return -1;
    }
    return 1;
}

static void esp32_web_console_drain_display(void)
{
    web_console_display_t *display = esp32_web_console_display();
    if (!s_web_console_ws.conn || !display) return;

    if (s_web_console_ws.tx_kind != ESP32_WEB_TX_IDLE) {
        (void)esp32_web_console_progress_frmb(display);
        return;
    }
    if (s_web_console_ws.pending_pong_valid) {
        size_t len = s_web_console_ws.pending_pong_len;
        s_web_console_ws.pending_pong_valid = 0;
        (void)esp32_web_console_send_frame(MM_NET_WS_OPCODE_PONG,
                                           s_web_console_ws.pending_pong,
                                           len);
        if (!s_web_console_ws.conn) return;
    }
    if (web_console_display_take_resync(display)) {
        esp32_web_console_start_frmb(display);
        (void)esp32_web_console_progress_frmb(display);
        return;
    }

    size_t payload_len = web_console_display_drain_cmds(
        display, s_web_console_payload, sizeof s_web_console_payload);
    if (payload_len) {
        (void)esp32_web_console_send_binary_payload_now(s_web_console_payload,
                                                        payload_len);
    }
}

static int esp32_json_op_is(const uint8_t *payload, size_t len,
                            const char *op)
{
    const char needle[] = "\"op\"";
    const uint8_t *end = payload + len;
    const uint8_t *p = payload;
    while (p + sizeof(needle) - 1u <= end) {
        if (memcmp(p, needle, sizeof(needle) - 1u) == 0) {
            p += sizeof(needle) - 1u;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                               *p == '\n')) p++;
            if (p >= end || *p++ != ':') return 0;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                               *p == '\n')) p++;
            if (p >= end || *p++ != '"') return 0;
            size_t op_len = strlen(op);
            return (size_t)(end - p) >= op_len + 1u &&
                   memcmp(p, op, op_len) == 0 && p[op_len] == '"';
        }
        p++;
    }
    return 0;
}

static void esp32_web_console_handle_text(const uint8_t *payload,
                                          size_t payload_len)
{
    int key_code = -1;
    if (web_console_parse_key_json((const char *)payload, payload_len,
                                   &key_code)) {
        (void)key_code;
        esp32_web_console_send_status();
        return;
    }
    if (esp32_json_op_is(payload, payload_len, "hello")) {
        esp32_web_console_send_hello();
        esp32_web_console_send_status();
        return;
    }
    if (esp32_json_op_is(payload, payload_len, "status")) {
        esp32_web_console_send_status();
    }
}

static void esp32_web_console_send_close(uint16_t code)
{
    uint8_t payload[2] = {
        (uint8_t)(code >> 8),
        (uint8_t)(code & 0xffu),
    };
    (void)esp32_web_console_send_frame(MM_NET_WS_OPCODE_CLOSE, payload,
                                       sizeof payload);
    esp32_web_console_close();
}

static void esp32_web_console_process_rx(void)
{
    uint8_t payload[ESP32_WEB_CONSOLE_PAYLOAD_LIMIT];
    for (;;) {
        mm_net_ws_frame_t frame;
        size_t consumed = 0;
        int rc = mm_net_ws_decode_frame(s_web_console_ws.rx_buf,
                                        s_web_console_ws.rx_len,
                                        ESP32_WEB_CONSOLE_PAYLOAD_LIMIT,
                                        payload, sizeof payload, &frame,
                                        &consumed);
        if (rc == MM_NET_WS_NEED_MORE) return;
        if (rc != MM_NET_WS_OK || consumed == 0) {
            esp32_web_console_send_close(
                rc == MM_NET_WS_ERR_TOO_LARGE ? 1009u : 1002u);
            return;
        }
        if (consumed < s_web_console_ws.rx_len) {
            memmove(s_web_console_ws.rx_buf,
                    s_web_console_ws.rx_buf + consumed,
                    s_web_console_ws.rx_len - consumed);
        }
        s_web_console_ws.rx_len -= consumed;

        if (frame.opcode == MM_NET_WS_OPCODE_TEXT) {
            esp32_web_console_handle_text(payload, frame.payload_len);
        } else if (frame.opcode == MM_NET_WS_OPCODE_PING) {
            if (s_web_console_ws.tx_kind == ESP32_WEB_TX_IDLE) {
                (void)esp32_web_console_send_frame(MM_NET_WS_OPCODE_PONG,
                                                   payload, frame.payload_len);
            } else if (frame.payload_len <= sizeof s_web_console_ws.pending_pong) {
                memcpy(s_web_console_ws.pending_pong, payload,
                       frame.payload_len);
                s_web_console_ws.pending_pong_len = frame.payload_len;
                s_web_console_ws.pending_pong_valid = 1;
            }
        } else if (frame.opcode == MM_NET_WS_OPCODE_CLOSE) {
            esp32_web_console_send_close(frame.close_code ?
                                         frame.close_code : 1000u);
            return;
        }
        if (!s_web_console_ws.conn || s_web_console_ws.rx_len == 0) return;
    }
}

static void esp32_web_console_poll(void)
{
    if (!s_web_console_ws.conn) return;
    for (;;) {
        if (s_web_console_ws.rx_len >= sizeof s_web_console_ws.rx_buf) {
            esp32_web_console_send_close(1009u);
            return;
        }
        size_t len = 0;
        int rc = hal_net_tcp_conn_recv(
            s_web_console_ws.conn,
            s_web_console_ws.rx_buf + s_web_console_ws.rx_len,
            sizeof s_web_console_ws.rx_buf - s_web_console_ws.rx_len, &len);
        if (rc == HAL_NET_WOULD_BLOCK) {
            esp32_web_console_drain_display();
            return;
        }
        if (rc != HAL_NET_OK) {
            esp32_web_console_close();
            return;
        }
        if (len == 0) {
            esp32_web_console_drain_display();
            return;
        }
        s_web_console_ws.rx_len += len;
        esp32_web_console_process_rx();
        if (!s_web_console_ws.conn) return;
    }
}

static int esp32_web_console_accept_ws(hal_net_tcp_conn_t conn,
                                       const uint8_t *request,
                                       size_t request_len)
{
    if (s_web_console_ws.conn) {
        esp32_send_http_status_conn(conn, 403);
        return 1;
    }

    char key[MM_NET_WS_MAX_KEY_LEN];
    int rc = mm_net_ws_validate_upgrade_request(request, request_len,
                                                WEB_CONSOLE_WS_PATH, key);
    if (rc != MM_NET_WS_OK) {
        esp32_send_http_status_conn(conn, 400);
        return 1;
    }

    char accept[MM_NET_WS_ACCEPT_LEN];
    char response[256];
    rc = mm_net_ws_compute_accept(key, accept);
    if (rc == MM_NET_WS_OK) {
        rc = mm_net_ws_format_upgrade_response(response, sizeof response,
                                               accept);
    }
    if (rc < 0 || hal_net_tcp_conn_send(conn, response, (size_t)rc, 5000) !=
        HAL_NET_OK) {
        hal_net_tcp_conn_close(conn);
        return 1;
    }

    memset(&s_web_console_ws, 0, sizeof s_web_console_ws);
    s_web_console_ws.conn = conn;
    esp32_web_console_send_hello();
    esp32_web_console_send_status();
    web_console_display_t *display = esp32_web_console_display();
    if (display) display->needs_resync = 1;
    esp32_web_console_drain_display();
    return 1;
}

static int esp32_web_console_handle_request(hal_net_tcp_conn_t conn,
                                            const uint8_t *request,
                                            size_t request_len,
                                            const char *path)
{
    if (esp32_path_equals_ignore_query(path, WEB_CONSOLE_WS_PATH)) {
        return esp32_web_console_accept_ws(conn, request, request_len);
    }

    const web_console_asset_t *asset = web_console_asset_find(path);
    if (asset) {
        esp32_send_asset_conn(conn, asset);
        return 1;
    }

    if (esp32_path_has_prefix(path, "/__web_console")) {
        esp32_send_http_status_conn(conn, 404);
        return 1;
    }

    return 0;
}

int esp32_tcp_interrupt_pending(void)
{
    esp32_tcp_init_slots();
    return mm_net_tcp_service_interrupt_pending(&s_tcp);
}

void esp32_tcp_server_poll(void)
{
    esp32_tcp_init_slots();
    esp32_web_console_poll();
    if (!s_tcp.server) return;

    for (;;) {
        hal_net_tcp_conn_t conn = 0;
        size_t len = 0;
        int rc = hal_net_tcp_accept_event(s_tcp.server, &conn,
                                          s_tcp_accept_buf,
                                          sizeof s_tcp_accept_buf, &len);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || !conn) return;
        if (len == 0) {
            hal_net_tcp_conn_close(conn);
            continue;
        }

        char path[ESP32_TCP_PATH_MAX];
        mm_net_http_extract_path(s_tcp_accept_buf, len, path, sizeof path);
        if (esp32_web_console_handle_request(conn, s_tcp_accept_buf, len,
                                             path)) {
            esp32_tcp_refresh_received();
            continue;
        }

        int pcb = esp32_tcp_find_free_slot();
        if (pcb < 0) {
            hal_net_tcp_conn_close(conn);
            continue;
        }
        mm_net_tcp_service_slot_t *slot = &s_tcp_slots[pcb];
        slot->conn = conn;
        slot->recv_len = len;
        memcpy(slot->recv_buf, s_tcp_accept_buf, len);
        if (slot->path && slot->path_cap) {
            memcpy(slot->path, path, strlen(path) + 1u);
        }
        slot->inttrig = 1;
        TCPreceived = true;
    }
}

void esp32_tcp_server_stop(void)
{
    esp32_tcp_init_slots();
    esp32_web_console_close();
    mm_net_tcp_service_stop(&s_tcp);
}

int esp32_tcp_server_open(uint16_t port)
{
    esp32_tcp_init_slots();
    if (!port) return 0;
    if (s_tcp.server && s_tcp.port != (int)port) esp32_web_console_close();
    return mm_net_tcp_service_open(&s_tcp, port, ESP32_MAX_PCB);
}

void esp32_tcp_server_clear_requests(void)
{
    esp32_tcp_init_slots();
    esp32_web_console_close();
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
