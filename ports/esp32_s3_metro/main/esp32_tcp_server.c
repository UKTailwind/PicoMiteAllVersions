#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "drivers/web_console/web_console_assets.h"
#include "drivers/web_console/web_console_display.h"
#include "drivers/web_console/web_console_input.h"
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
#define ESP32_WEB_CONSOLE_TX_BUF 4096
#define ESP32_WEB_CONSOLE_SEND_CHUNK 4096
#define ESP32_WEB_CONSOLE_INPUT_QUEUE 128
#define ESP32_WEB_CONSOLE_FRAME_INTERVAL_US 33333LL
#define ESP32_WEB_CONSOLE_MAX_MISSED_FRAMES 6

typedef enum {
    ESP32_WEB_TX_IDLE = 0,
    ESP32_WEB_TX_BUFFER_WS_HEADER,
    ESP32_WEB_TX_BUFFER_PAYLOAD,
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
    uint8_t *frame_payload;
    size_t frame_payload_cap;
    size_t frame_payload_len;
    size_t frame_payload_off;
    uint8_t pending_pong[125];
    size_t pending_pong_len;
    int pending_pong_valid;
    int64_t next_frame_us;
    int64_t tx_started_us;
    unsigned missed_frames;
} esp32_web_console_ws_t;

static esp32_web_console_ws_t s_web_console_ws;
static uint8_t s_web_console_tx_buf[ESP32_WEB_CONSOLE_TX_BUF];
static uint8_t s_web_console_input_buf[ESP32_WEB_CONSOLE_INPUT_QUEUE];
static web_console_input_t s_web_console_input;

extern web_console_display_t *esp32_web_console_display(void);

static void esp32_web_console_input_init_once(void)
{
    static int inited;
    if (inited) return;
    inited = 1;
    web_console_input_init(&s_web_console_input, s_web_console_input_buf,
                           sizeof s_web_console_input_buf);
}

static void esp32_tcp_init_slots(void)
{
    static int inited;
    if (inited) return;
    inited = 1;
    esp32_web_console_input_init_once();
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

static void esp32_web_console_close_reason(const char *reason)
{
    (void)reason;
    hal_net_tcp_conn_t conn = s_web_console_ws.conn;
    uint8_t *frame_payload = s_web_console_ws.frame_payload;
    size_t frame_payload_cap = s_web_console_ws.frame_payload_cap;
    if (conn) {
        web_console_input_release(&s_web_console_input, (uintptr_t)conn);
        hal_net_tcp_conn_close(conn);
    }
    memset(&s_web_console_ws, 0, sizeof s_web_console_ws);
    s_web_console_ws.frame_payload = frame_payload;
    s_web_console_ws.frame_payload_cap = frame_payload_cap;
}

void esp32_web_console_close(void)
{
    esp32_web_console_close_reason("close");
}

int esp32_web_console_open(void)
{
    /* The HTTP/WebSocket endpoint is hosted by the shared TCP server; just
     * make sure the server is listening on the configured port. */
    esp32_tcp_init_slots();
    if (!Option.WebConsole) return 1;
    if (!WIFIconnected) return 1;
    if (!Option.TCP_PORT) Option.TCP_PORT = 80;
    if (s_tcp.server) return 1;
    return mm_net_tcp_service_open(&s_tcp, (uint16_t)Option.TCP_PORT,
                                   ESP32_MAX_PCB);
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
    size_t sent = 0;
    if (hal_net_tcp_conn_send_some(s_web_console_ws.conn,
                                   s_web_console_tx_buf, written,
                                   &sent) != HAL_NET_OK || sent != written) {
        esp32_web_console_close_reason("send_frame");
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
        "\"caps\":[\"status\",\"control\",\"display\",\"keyboard\"],"
        "\"display\":true,\"audio\":false,\"keyboard\":true}");
}

static void esp32_web_console_send_status(void)
{
    char json[192];
    int n = snprintf(json, sizeof json,
                     "{\"op\":\"status\",\"phase\":5,\"ws\":\"connected\","
                     "\"display\":\"ready\",\"audio\":\"pending\","
                     "\"keyboard\":\"ready\",\"input_owner\":%s,"
                     "\"input_dropped\":%u}",
                     web_console_input_owner(&s_web_console_input) ==
                         (uintptr_t)s_web_console_ws.conn ? "true" : "false",
                     s_web_console_input.dropped);
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

static void put_i16_le(uint8_t *dst, int16_t v)
{
    put_u16_le(dst, (uint16_t)v);
}

static uint8_t esp32_web_console_rgb332(uint32_t c)
{
    uint8_t r = (uint8_t)((c >> 16) & 0xffu);
    uint8_t g = (uint8_t)((c >> 8) & 0xffu);
    uint8_t b = (uint8_t)(c & 0xffu);
    return (uint8_t)((r & 0xe0u) | ((g & 0xe0u) >> 3) | (b >> 6));
}

static size_t esp32_web_console_rle332_max_payload_len(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > (SIZE_MAX - WEB_CONSOLE_FRAME_HEADER_LEN - 9u) / 3u) {
        return 0;
    }
    return WEB_CONSOLE_FRAME_HEADER_LEN + 9u + pixels * 3u;
}

static size_t esp32_web_console_rle332_payload_len(const web_console_display_t *display,
                                                   int x1, int y1,
                                                   int w, int h)
{
    size_t len = WEB_CONSOLE_FRAME_HEADER_LEN + 9u;
    uint8_t last = 0;
    uint16_t run = 0;

    for (int yy = 0; yy < h; ++yy) {
        const uint32_t *row = display->pixels +
            (size_t)(y1 + yy) * (size_t)display->width + (size_t)x1;
        for (int xx = 0; xx < w; ++xx) {
            uint8_t c = esp32_web_console_rgb332(row[xx]);
            if (run && c == last && run < 65535u) {
                run++;
            } else {
                if (run) len += 3u;
                last = c;
                run = 1;
            }
        }
    }
    if (run) len += 3u;
    return len;
}

static int esp32_web_console_ensure_payload(size_t need)
{
    if (s_web_console_ws.frame_payload_cap >= need) return 1;
    size_t cap = need;
    uint8_t *p = (uint8_t *)heap_caps_realloc(
        s_web_console_ws.frame_payload, cap,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p && !s_web_console_ws.frame_payload) p = (uint8_t *)malloc(cap);
    if (!p) return 0;
    s_web_console_ws.frame_payload = p;
    s_web_console_ws.frame_payload_cap = cap;
    return 1;
}

static uint8_t *esp32_web_console_pack_rle332(uint8_t *p,
                                              const web_console_display_t *display,
                                              int x1, int y1, int w, int h)
{
    uint8_t last = 0;
    uint16_t run = 0;

    for (int yy = 0; yy < h; ++yy) {
        const uint32_t *row = display->pixels +
            (size_t)(y1 + yy) * (size_t)display->width + (size_t)x1;
        for (int xx = 0; xx < w; ++xx) {
            uint8_t c = esp32_web_console_rgb332(row[xx]);
            if (run && c == last && run < 65535u) {
                run++;
            } else {
                if (run) {
                    put_u16_le(p, run);
                    p[2] = last;
                    p += 3;
                }
                last = c;
                run = 1;
            }
        }
    }
    if (run) {
        put_u16_le(p, run);
        p[2] = last;
        p += 3;
    }
    return p;
}

static void esp32_web_console_make_ws_binary_header(uint64_t payload_len)
{
    s_web_console_ws.ws_header[0] = 0x80u | MM_NET_WS_OPCODE_BINARY;
    if (payload_len <= 125u) {
        s_web_console_ws.ws_header[1] = (uint8_t)payload_len;
        s_web_console_ws.ws_header_len = 2;
    } else if (payload_len <= 65535u) {
        s_web_console_ws.ws_header[1] = 126u;
        s_web_console_ws.ws_header[2] = (uint8_t)(payload_len >> 8);
        s_web_console_ws.ws_header[3] = (uint8_t)payload_len;
        s_web_console_ws.ws_header_len = 4;
    } else {
        s_web_console_ws.ws_header[1] = 127u;
        put_u64_be(s_web_console_ws.ws_header + 2, payload_len);
        s_web_console_ws.ws_header_len = 10;
    }
    s_web_console_ws.ws_header_off = 0;
}

static int esp32_web_console_start_blit(web_console_display_t *display,
                                        int x1, int y1, int x2, int y2)
{
    if (!s_web_console_ws.conn || !display) return 0;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= display->width) x2 = display->width - 1;
    if (y2 >= display->height) y2 = display->height - 1;
    if (x1 > x2 || y1 > y2) return 0;
    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;
    size_t max_payload_len = esp32_web_console_rle332_max_payload_len(w, h);
    if (!max_payload_len ||
        !esp32_web_console_ensure_payload(max_payload_len)) {
        size_t exact_payload_len = esp32_web_console_rle332_payload_len(
            display, x1, y1, w, h);
        if (!exact_payload_len ||
            !esp32_web_console_ensure_payload(exact_payload_len)) {
            return 0;
        }
    }

    uint8_t *p = s_web_console_ws.frame_payload;
    memcpy(p, "CMDS", 4);
    put_u16_le(p + 4, (uint16_t)display->width);
    put_u16_le(p + 6, (uint16_t)display->height);
    p += WEB_CONSOLE_FRAME_HEADER_LEN;
    *p++ = WEB_CONSOLE_OP_BLIT_RGB332_RLE;
    put_i16_le(p, (int16_t)x1);
    put_i16_le(p + 2, (int16_t)y1);
    put_u16_le(p + 4, (uint16_t)w);
    put_u16_le(p + 6, (uint16_t)h);
    p += 8;
    p = esp32_web_console_pack_rle332(p, display, x1, y1, w, h);
    s_web_console_ws.frame_payload_len =
        (size_t)(p - s_web_console_ws.frame_payload);
    s_web_console_ws.frame_payload_off = 0;
    esp32_web_console_make_ws_binary_header(s_web_console_ws.frame_payload_len);

    s_web_console_ws.tx_started_us = esp_timer_get_time();
    s_web_console_ws.missed_frames = 0;
    s_web_console_ws.tx_kind = ESP32_WEB_TX_BUFFER_WS_HEADER;
    return 1;
}

static int esp32_web_console_send_raw_progress(const uint8_t *buf, size_t len,
                                               size_t *off)
{
    if (!s_web_console_ws.conn) return -1;
    if (*off >= len) return 1;
    size_t n = len - *off;
    if (n > ESP32_WEB_CONSOLE_SEND_CHUNK) n = ESP32_WEB_CONSOLE_SEND_CHUNK;
    size_t sent = 0;
    int rc = hal_net_tcp_conn_send_some(s_web_console_ws.conn, buf + *off,
                                        n, &sent);
    if (rc == HAL_NET_TIMEOUT || rc == HAL_NET_WOULD_BLOCK) return 0;
    if (rc != HAL_NET_OK) {
        esp32_web_console_close_reason("send_raw");
        return -1;
    }
    if (sent == 0) return 0;
    *off += sent;
    return *off >= len ? 1 : 0;
}

static int esp32_web_console_progress_buffer(void)
{
    if (s_web_console_ws.tx_kind == ESP32_WEB_TX_BUFFER_WS_HEADER) {
        int rc = esp32_web_console_send_raw_progress(
            s_web_console_ws.ws_header, s_web_console_ws.ws_header_len,
            &s_web_console_ws.ws_header_off);
        if (rc <= 0) return rc;
        s_web_console_ws.tx_kind = ESP32_WEB_TX_BUFFER_PAYLOAD;
    }
    if (s_web_console_ws.tx_kind == ESP32_WEB_TX_BUFFER_PAYLOAD) {
        int rc = esp32_web_console_send_raw_progress(
            s_web_console_ws.frame_payload,
            s_web_console_ws.frame_payload_len,
            &s_web_console_ws.frame_payload_off);
        if (rc <= 0) return rc;
        s_web_console_ws.tx_kind = ESP32_WEB_TX_IDLE;
        s_web_console_ws.tx_started_us = 0;
        s_web_console_ws.missed_frames = 0;
    }
    return 1;
}

static int esp32_web_console_progress_tx(web_console_display_t *display)
{
    (void)display;
    if (s_web_console_ws.tx_kind == ESP32_WEB_TX_BUFFER_WS_HEADER ||
        s_web_console_ws.tx_kind == ESP32_WEB_TX_BUFFER_PAYLOAD)
        return esp32_web_console_progress_buffer();
    return 1;
}

static void esp32_web_console_start_frmb_and_clear_dirty(
    web_console_display_t *display)
{
    if (!display) return;
    if (!esp32_web_console_start_blit(display, 0, 0,
                                      display->width - 1,
                                      display->height - 1)) return;
    web_console_display_clear_dirty(display);
    s_web_console_ws.next_frame_us =
        esp_timer_get_time() + ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
}

static void esp32_web_console_start_blit_and_clear_dirty(
    web_console_display_t *display, int x1, int y1, int x2, int y2)
{
    if (!esp32_web_console_start_blit(display, x1, y1, x2, y2)) return;
    web_console_display_clear_dirty(display);
    s_web_console_ws.next_frame_us =
        esp_timer_get_time() + ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
}

static void esp32_web_console_drain_display(void)
{
    web_console_display_t *display = esp32_web_console_display();
    if (!s_web_console_ws.conn || !display) return;

    if (s_web_console_ws.tx_kind != ESP32_WEB_TX_IDLE) {
        int x1, y1, x2, y2;
        if (web_console_display_dirty_bounds(display, &x1, &y1, &x2, &y2)) {
            int64_t now = esp_timer_get_time();
            if (s_web_console_ws.next_frame_us == 0) {
                s_web_console_ws.next_frame_us =
                    now + ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
            }
            while (now >= s_web_console_ws.next_frame_us) {
                s_web_console_ws.missed_frames++;
                s_web_console_ws.next_frame_us +=
                    ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
            }
            if (s_web_console_ws.missed_frames >=
                ESP32_WEB_CONSOLE_MAX_MISSED_FRAMES) {
                web_console_display_request_resync(display);
            }
        }
        (void)esp32_web_console_progress_tx(display);
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
        esp32_web_console_start_frmb_and_clear_dirty(display);
        (void)esp32_web_console_progress_tx(display);
        return;
    }

    int x1, y1, x2, y2;
    if (!web_console_display_dirty_bounds(display, &x1, &y1, &x2, &y2)) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (s_web_console_ws.next_frame_us == 0) {
        s_web_console_ws.next_frame_us = now + ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
        return;
    }
    if (now < s_web_console_ws.next_frame_us) return;

    esp32_web_console_start_blit_and_clear_dirty(display, x1, y1, x2, y2);
    (void)esp32_web_console_progress_tx(display);
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
        if (BreakKey && key_code == BreakKey) {
            MMAbort = 1;
            ConsoleRxBufHead = ConsoleRxBufTail;
            web_console_input_clear(&s_web_console_input);
        } else if (key_code == keyselect && KeyInterrupt != NULL) {
            Keycomplete = 1;
        } else {
            (void)web_console_input_push(&s_web_console_input,
                                         (uintptr_t)s_web_console_ws.conn,
                                         key_code);
        }
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
    esp32_web_console_close_reason("ws_close");
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
            esp32_web_console_close_reason("recv_error");
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
    if (!Option.WebConsole) {
        esp32_send_http_status_conn(conn, 404);
        return 1;
    }
    char key[MM_NET_WS_MAX_KEY_LEN];
    int rc = mm_net_ws_validate_upgrade_request(request, request_len,
                                                WEB_CONSOLE_WS_PATH, key);
    if (rc != MM_NET_WS_OK) {
        esp32_send_http_status_conn(conn, 400);
        return 1;
    }

    if (s_web_console_ws.conn) {
        esp32_web_console_close_reason("ws_replace");
    }

    if (!web_console_input_acquire(&s_web_console_input, (uintptr_t)conn)) {
        esp32_send_http_status_conn(conn, 403);
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
        web_console_input_release(&s_web_console_input, (uintptr_t)conn);
        hal_net_tcp_conn_close(conn);
        return 1;
    }

    memset(&s_web_console_ws, 0, sizeof s_web_console_ws);
    s_web_console_ws.conn = conn;
    esp32_web_console_send_hello();
    esp32_web_console_send_status();
    web_console_display_t *display = esp32_web_console_display();
    if (display) web_console_display_request_resync(display);
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

int esp32_web_console_input_available(void)
{
    esp32_tcp_init_slots();
    return web_console_input_available(&s_web_console_input);
}

int esp32_web_console_pop_key(void)
{
    int code = -1;
    esp32_tcp_init_slots();
    if (!web_console_input_pop(&s_web_console_input, &code)) return -1;
    return code;
}

int esp32_web_console_connected(void)
{
    esp32_tcp_init_slots();
    return s_web_console_ws.conn != 0;
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
    esp32_web_console_close_reason("server_stop");
    mm_net_tcp_service_stop(&s_tcp);
}

int esp32_tcp_server_open(uint16_t port)
{
    esp32_tcp_init_slots();
    if (!port) return 0;
    if (s_tcp.server && s_tcp.port != (int)port)
        esp32_web_console_close_reason("port_change");
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
