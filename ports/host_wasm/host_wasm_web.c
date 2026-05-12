/*
 * ports/host_wasm/host_wasm_web.c - browser WEB command surface.
 *
 * Browser WASM cannot expose raw TCP, UDP, TFTP, or Telnet sockets. Keep the
 * BASIC-visible hooks present so the host runtime links, implement browser
 * HTTP fetch and MQTT-over-WebSocket client paths, and fail raw socket
 * commands explicitly instead of inheriting host-native POSIX sockets.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_mqtt_wire.h"
#include "shared/net/mm_net_options.h"
#include "shared/net/mm_net_state.h"
#include "shared/net/mm_net_tcp_client_cmd.h"

volatile int WIFIconnected = 0;
bool optionsuppressstatus = false;
int startupcomplete = 0;

void port_web_clear_runtime_state(void);

static const mm_net_lifecycle_result_handler_t wasm_lifecycle_result_handler = {
    .unsupported_error = "WEB networking not supported in browser build",
    .apply_error = "Syntax",
};

#define WASM_TCP_HOST_MAX 256
#define WASM_FETCH_MAX_HEADERS 16
#define WASM_MQTT_FRAME_MAX 2048

static char wasm_tcp_host[WASM_TCP_HOST_MAX];
static int wasm_tcp_port;
static int wasm_tcp_client_opened;
static hal_net_mqtt_client_t wasm_mqtt_client;
static int wasm_mqtt_connected;
static uint16_t wasm_mqtt_next_packet_id = 1;

#ifdef __EMSCRIPTEN__
static int wasm_mqtt_js_open(const char *url_ptr) {
    return MAIN_THREAD_EM_ASM_INT({
        const url = UTF8ToString($0);
        const state = {};
        state.ws = null;
        state.state = 1;
        state.rx = [];
        state.tx = [];
        Module.__picomiteMqtt = state;
        try {
            const ws = new WebSocket(url, "mqtt");
            ws.binaryType = "arraybuffer";
            state.ws = ws;
            ws.onopen = () => {
                state.state = 2;
                for (const msg of state.tx.splice(0)) ws.send(msg);
            };
            ws.onmessage = (ev) => {
                const data = ev.data instanceof ArrayBuffer
                    ? new Uint8Array(ev.data)
                    : new Uint8Array(ev.data.buffer || ev.data);
                if (state.rx.length < 32) state.rx.push(new Uint8Array(data));
            };
            ws.onerror = () => { if (state.state !== 3) state.state = -1; };
            ws.onclose = () => { if (state.state !== 3) state.state = 0; };
            return 1;
        } catch (e) {
            state.state = -1;
            return 0;
        }
    }, url_ptr);
}

static int wasm_mqtt_js_send(const uint8_t *data, int len) {
    return MAIN_THREAD_EM_ASM_INT({
        const state = Module.__picomiteMqtt;
        if (!state || $1 < 0) return -1;
        const msg = HEAPU8.slice($0, $0 + $1);
        if (state.state === 2 && state.ws) {
            state.ws.send(msg);
            return 0;
        }
        if (state.state === 1) {
            if (state.tx.length >= 16) return -1;
            state.tx.push(msg);
            return 0;
        }
        return -1;
    }, data, len);
}

static int wasm_mqtt_js_recv(uint8_t *dst, int cap) {
    return MAIN_THREAD_EM_ASM_INT({
        const state = Module.__picomiteMqtt;
        if (!state || !state.rx.length) return 0;
        const msg = state.rx.shift();
        if (msg.length > $1) return -1;
        HEAPU8.set(msg, $0);
        return msg.length;
    }, dst, cap);
}

static void wasm_mqtt_js_close(void) {
    MAIN_THREAD_EM_ASM({
        const state = Module.__picomiteMqtt;
        if (!state) return;
        state.state = 3;
        state.rx = [];
        state.tx = [];
        try {
            if (state.ws) state.ws.close();
        } catch (e) {}
        Module.__picomiteMqtt = null;
    });
}
#endif

static void wasm_web_unsupported(void) {
    error("WEB networking not supported in browser build");
}

static void wasm_tcp_client_reset(void) {
    wasm_tcp_host[0] = '\0';
    wasm_tcp_port = 0;
    wasm_tcp_client_opened = 0;
}

static uint16_t wasm_mqtt_next_id(void) {
    if (wasm_mqtt_next_packet_id == 0) wasm_mqtt_next_packet_id = 1;
    uint16_t id = wasm_mqtt_next_packet_id++;
    if (wasm_mqtt_next_packet_id == 0) wasm_mqtt_next_packet_id = 1;
    return id;
}

static int wasm_mqtt_send_packet(uint8_t header, const uint8_t *body,
                                 size_t body_len) {
    uint8_t packet[WASM_MQTT_FRAME_MAX];
    uint8_t rem[4];
    size_t rem_len = mm_net_mqtt_encode_remaining_length(rem, body_len);
    size_t total = 1 + rem_len + body_len;
    if (total > sizeof(packet)) return HAL_NET_ERR;
    packet[0] = header;
    memcpy(packet + 1, rem, rem_len);
    if (body_len) memcpy(packet + 1 + rem_len, body, body_len);
#ifdef __EMSCRIPTEN__
    return wasm_mqtt_js_send(packet, (int)total) == 0 ? HAL_NET_OK
                                                     : HAL_NET_ERR;
#else
    (void)packet;
    return HAL_NET_UNSUPPORTED;
#endif
}

static int wasm_mqtt_decode_frame(const uint8_t *frame, size_t frame_len,
                                  uint8_t *header, const uint8_t **body,
                                  size_t *body_len) {
    if (!frame || frame_len < 2 || !header || !body || !body_len) return 0;
    *header = frame[0];
    size_t multiplier = 1;
    size_t value = 0;
    size_t pos = 1;
    for (int i = 0; i < 4; ++i) {
        if (pos >= frame_len) return 0;
        uint8_t encoded = frame[pos++];
        value += (size_t)(encoded & 127) * multiplier;
        if ((encoded & 128) == 0) {
            if (pos + value > frame_len) return 0;
            *body = frame + pos;
            *body_len = value;
            return 1;
        }
        multiplier *= 128;
    }
    return 0;
}

static int wasm_mqtt_build_url(const char *host, uint16_t port, char *out,
                               size_t out_len) {
    if (!host || !*host || !out || out_len == 0) return 0;
    if (strncmp(host, "ws://", 5) == 0 || strncmp(host, "wss://", 6) == 0) {
        int n = snprintf(out, out_len, "%s", host);
        return n > 0 && (size_t)n < out_len;
    }

    const char *path = strchr(host, '/');
    size_t host_len = path ? (size_t)(path - host) : strlen(host);
    if (host_len == 0 || host_len >= 300) return 0;
    const char *scheme = (port == 443) ? "wss" : "ws";
    int default_port = (port == 443) ? 443 : 80;
    int n = snprintf(out, out_len, "%s://%.*s", scheme, (int)host_len, host);
    if (n < 0 || (size_t)n >= out_len) return 0;
    if (port != default_port) {
        size_t used = strlen(out);
        n = snprintf(out + used, out_len - used, ":%u",
                     (unsigned)port);
        if (n < 0 || (size_t)n >= out_len - used) return 0;
    }
    size_t used = strlen(out);
    n = snprintf(out + used, out_len - used, "%s", path ? path : "/mqtt");
    return n >= 0 && (size_t)n < out_len - used;
}

static void wasm_mqtt_poll(void) {
    if (!wasm_mqtt_connected) return;
#ifdef __EMSCRIPTEN__
    for (;;) {
        uint8_t frame[WASM_MQTT_FRAME_MAX];
        int got = wasm_mqtt_js_recv(frame, sizeof(frame));
        if (got == 0) return;
        if (got < 0) return;
        uint8_t header = 0;
        const uint8_t *body = NULL;
        size_t body_len = 0;
        if (!wasm_mqtt_decode_frame(frame, (size_t)got, &header, &body,
                                    &body_len))
            continue;
        if ((header >> 4) == 3) {
            char topic[MAXSTRLEN + 1];
            const uint8_t *payload = NULL;
            size_t payload_len = 0;
            if (mm_net_mqtt_decode_publish(header, body, body_len, topic,
                                           sizeof(topic), &payload,
                                           &payload_len)) {
                mm_net_state_set_mstring(MM_NET_STATE_TOPIC, topic,
                                         strlen(topic));
                mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, payload,
                                         payload_len);
                MQTTComplete = true;
            }
        }
    }
#endif
}

void closeMQTT(void) {
#ifdef __EMSCRIPTEN__
    if (wasm_mqtt_connected) wasm_mqtt_js_close();
#endif
    wasm_mqtt_connected = 0;
    wasm_mqtt_client = 0;
    wasm_mqtt_next_packet_id = 1;
}

void ProcessWeb(int mode) {
    static const mm_net_lifecycle_poll_hooks_t hooks = {
        .poll_mqtt = wasm_mqtt_poll,
    };
    mm_net_lifecycle_poll(&hooks, mode, 0);
}

int host_tcp_interrupt_pending(void) {
    return 0;
}

void cleanserver(void) {
    port_web_clear_runtime_state();
}

void close_tcpclient(void) {
    wasm_tcp_client_reset();
}

void host_telnet_putc(int c, int flush) {
    (void)c;
    (void)flush;
}

void WebConnect(void) {
    wasm_web_unsupported();
}

uint32_t hal_net_capabilities(void) {
    return HAL_NET_CAP_HTTP_FETCH | HAL_NET_CAP_MQTT_WEBSOCKET;
}

int hal_net_init(void) {
    return HAL_NET_OK;
}

void hal_net_poll(void) {
    wasm_mqtt_poll();
}

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out) {
    (void)timeout_ms;
    if (!host || !client_id || !out) return HAL_NET_ERR;
    *out = 0;
    closeMQTT();

    char url[512];
    if (!wasm_mqtt_build_url(host, port, url, sizeof(url)))
        return HAL_NET_ERR;

#ifdef __EMSCRIPTEN__
    if (!wasm_mqtt_js_open(url)) return HAL_NET_ERR;
#else
    return HAL_NET_UNSUPPORTED;
#endif

    uint8_t body[1024];
    uint8_t *p = body;
    p = mm_net_mqtt_write_utf8(p, "MQTT");
    *p++ = 4;
    uint8_t flags = 0x02;
    if (user && *user) flags |= 0x80;
    if (pass && *pass) flags |= 0x40;
    *p++ = flags;
    *p++ = 0;
    *p++ = 100;
    p = mm_net_mqtt_write_utf8(p, client_id);
    if (user && *user) p = mm_net_mqtt_write_utf8(p, user);
    if (pass && *pass) p = mm_net_mqtt_write_utf8(p, pass);

    wasm_mqtt_connected = 1;
    wasm_mqtt_client = 1;
    wasm_mqtt_next_packet_id = 1;
    if (wasm_mqtt_send_packet(0x10, body, (size_t)(p - body)) != HAL_NET_OK) {
        closeMQTT();
        return HAL_NET_ERR;
    }
    *out = wasm_mqtt_client;
    return HAL_NET_OK;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos,
                         int retain) {
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic ||
        (!payload && len))
        return HAL_NET_ERR;
    uint8_t body[1024];
    uint8_t *p = body;
    p = mm_net_mqtt_write_utf8(p, topic);
    if (qos) {
        uint16_t id = wasm_mqtt_next_id();
        *p++ = (uint8_t)(id >> 8);
        *p++ = (uint8_t)id;
    }
    if ((size_t)(p - body) + len > sizeof(body)) return HAL_NET_ERR;
    if (len) {
        memcpy(p, payload, len);
        p += len;
    }
    uint8_t header = 0x30 | (retain ? 0x01 : 0) | (qos == 1 ? 0x02 : 0);
    return wasm_mqtt_send_packet(header, body, (size_t)(p - body));
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic)
        return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t *p = body;
    uint16_t id = wasm_mqtt_next_id();
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    *p++ = (uint8_t)qos;
    return wasm_mqtt_send_packet(0x82, body, (size_t)(p - body));
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic)
        return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t *p = body;
    uint16_t id = wasm_mqtt_next_id();
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    return wasm_mqtt_send_packet(0xA2, body, (size_t)(p - body));
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len) {
    if (payload_len) *payload_len = 0;
    if (topic && topic_cap) topic[0] = 0;
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic ||
        topic_cap == 0 || !payload)
        return HAL_NET_ERR;
#ifdef __EMSCRIPTEN__
    uint8_t frame[WASM_MQTT_FRAME_MAX];
    int got = wasm_mqtt_js_recv(frame, sizeof(frame));
    if (got == 0) return HAL_NET_WOULD_BLOCK;
    if (got < 0) return HAL_NET_ERR;
    uint8_t header = 0;
    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (!wasm_mqtt_decode_frame(frame, (size_t)got, &header, &body, &body_len))
        return HAL_NET_WOULD_BLOCK;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_payload_len = 0;
    if (!mm_net_mqtt_decode_publish(header, body, body_len, topic, topic_cap,
                                    &decoded_payload, &decoded_payload_len))
        return HAL_NET_WOULD_BLOCK;
    if (decoded_payload_len > payload_cap) decoded_payload_len = payload_cap;
    if (decoded_payload_len)
        memcpy(payload, decoded_payload, decoded_payload_len);
    if (payload_len) *payload_len = decoded_payload_len;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_mqtt_close(hal_net_mqtt_client_t client) {
    if (!wasm_mqtt_connected || client != wasm_mqtt_client) return HAL_NET_ERR;
    wasm_mqtt_send_packet(0xE0, NULL, 0);
    closeMQTT();
    return HAL_NET_OK;
}

static int request_line_to_fetch(const uint8_t *request, size_t request_len,
                                 char *method, size_t method_len,
                                 char *url, size_t url_len,
                                 const char **headers,
                                 int *header_count,
                                 char **owned_request,
                                 char **body, size_t *body_len) {
    *owned_request = NULL;
    *body = NULL;
    *body_len = 0;
    *header_count = 0;
    if (!request || request_len == 0) return 0;

    char *copy = (char *)malloc(request_len + 1);
    if (!copy) error("Out of memory");
    memcpy(copy, request, request_len);
    copy[request_len] = '\0';
    *owned_request = copy;

    char *line_end = strpbrk(copy, "\r\n");
    if (!line_end) return 0;
    *line_end = '\0';
    char *sp1 = strchr(copy, ' ');
    if (!sp1) return 0;
    *sp1++ = '\0';
    while (*sp1 == ' ') sp1++;
    char *sp2 = strchr(sp1, ' ');
    if (!sp2) return 0;
    *sp2++ = '\0';
    while (*sp2 == ' ') sp2++;
    if (strncasecmp(sp2, "HTTP/", 5) != 0) return 0;
    if (strlen(copy) + 1 > method_len) return 0;
    snprintf(method, method_len, "%s", copy);

    if (strncmp(sp1, "http://", 7) == 0 || strncmp(sp1, "https://", 8) == 0) {
        if (strlen(sp1) + 1 > url_len) return 0;
        snprintf(url, url_len, "%s", sp1);
    } else if (sp1[0] == '/') {
        const char *scheme = (wasm_tcp_port == 443) ? "https" : "http";
        int default_port = (strcmp(scheme, "https") == 0) ? 443 : 80;
        int n = snprintf(url, url_len, "%s://%s", scheme, wasm_tcp_host);
        if (n < 0 || (size_t)n >= url_len) return 0;
        if (wasm_tcp_port != default_port) {
            n = snprintf(url + strlen(url), url_len - strlen(url), ":%d",
                         wasm_tcp_port);
            if (n < 0 || (size_t)n >= url_len - strlen(url)) return 0;
        }
        n = snprintf(url + strlen(url), url_len - strlen(url), "%s", sp1);
        if (n < 0 || (size_t)n >= url_len - strlen(url)) return 0;
    } else {
        return 0;
    }

    char *headers_start = line_end + 1;
    while (*headers_start == '\r' || *headers_start == '\n') headers_start++;

    char *headers_end = strstr(headers_start, "\r\n\r\n");
    size_t skip = 4;
    if (!headers_end) {
        headers_end = strstr(headers_start, "\n\n");
        skip = 2;
    }
    if (headers_end) {
        char *payload = headers_end + skip;
        *headers_end = '\0';
        if (payload < copy + request_len) {
            *body = payload;
            *body_len = (size_t)((copy + request_len) - payload);
        }
    }

    char *line = headers_start;
    while (*line && *header_count < WASM_FETCH_MAX_HEADERS) {
        while (*line == '\r' || *line == '\n') line++;
        if (!*line) break;
        char *next = strpbrk(line, "\r\n");
        if (next) *next++ = '\0';

        char *colon = strchr(line, ':');
        if (colon) {
            *colon++ = '\0';
            while (*colon == ' ' || *colon == '\t') colon++;
            char *end = colon + strlen(colon);
            while (end > colon && (end[-1] == ' ' || end[-1] == '\t'))
                *--end = '\0';
            if (strcasecmp(line, "Host") != 0 &&
                strcasecmp(line, "Content-Length") != 0 &&
                strcasecmp(line, "Connection") != 0 &&
                strcasecmp(line, "Transfer-Encoding") != 0 &&
                strncasecmp(line, "Sec-", 4) != 0 &&
                strncasecmp(line, "Proxy-", 6) != 0) {
                headers[(*header_count) * 2] = line;
                headers[(*header_count) * 2 + 1] = colon;
                (*header_count)++;
            }
        }
        if (!next) break;
        line = next;
    }
    headers[(*header_count) * 2] = NULL;
    return 1;
}

static void append_response(uint8_t *dst, int cap, int *total,
                            const void *src, size_t len) {
    if (!dst || cap <= 0 || !total || !src || len == 0) return;
    if (*total >= cap) return;
    size_t room = (size_t)(cap - *total);
    if (len > room) len = room;
    memcpy(dst + *total, src, len);
    *total += (int)len;
}

static void append_response_text(uint8_t *dst, int cap, int *total,
                                 const char *text) {
    append_response(dst, cap, total, text, strlen(text));
}

static void append_response_fmt(uint8_t *dst, int cap, int *total,
                                const char *fmt, int status,
                                const char *status_text) {
    char line[160];
    snprintf(line, sizeof(line), fmt, status, status_text ? status_text : "");
    append_response_text(dst, cap, total, line);
}

static void wasm_tcp_client_open_cmd(unsigned char *tp) {
    mm_net_tcp_client_open_args_t parsed;
    mm_net_tcp_client_parse_open(tp, &parsed);

    wasm_tcp_client_reset();
    snprintf(wasm_tcp_host, sizeof(wasm_tcp_host), "%s", parsed.host);
    wasm_tcp_port = parsed.port;
    wasm_tcp_client_opened = 1;
    if (!optionsuppressstatus) MMPrintString("Connected\r\n");
}

static void wasm_tcp_client_request_cmd(unsigned char *tp) {
    if (!wasm_tcp_client_opened) error("No connection");

    mm_net_tcp_client_request_args_t parsed;
    mm_net_tcp_client_parse_request(tp, &parsed);

    char method[32];
    char url[512];
    char *owned_request = NULL;
    char *body = NULL;
    size_t body_len = 0;
    const char *headers[WASM_FETCH_MAX_HEADERS * 2 + 1];
    int header_count = 0;
    int ok = request_line_to_fetch(parsed.request, parsed.request_len,
                                   method, sizeof(method), url, sizeof(url),
                                   headers, &header_count,
                                   &owned_request, &body, &body_len);
    if (!ok) {
        free(owned_request);
        error("Only HTTP requests are supported in browser build");
    }

#ifdef __EMSCRIPTEN__
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    snprintf(attr.requestMethod, sizeof(attr.requestMethod), "%s", method);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY |
                      EMSCRIPTEN_FETCH_SYNCHRONOUS |
                      EMSCRIPTEN_FETCH_REPLACE;
    attr.timeoutMSecs = (uint32_t)parsed.timeout_ms;
    if (body && body_len) {
        attr.requestData = body;
        attr.requestDataSize = body_len;
    }
    if (header_count > 0) attr.requestHeaders = headers;

    emscripten_fetch_t *fetch = emscripten_fetch(&attr, url);
    if (!fetch || fetch->status == 0) {
        if (fetch) emscripten_fetch_close(fetch);
        free(owned_request);
        error("No response from server");
    }

    int total = 0;
    append_response_fmt(parsed.buffer, parsed.payload_capacity, &total,
                        "HTTP/1.1 %d %s\r\n", fetch->status,
                        fetch->statusText);
    size_t headers_len = emscripten_fetch_get_response_headers_length(fetch);
    if (headers_len > 0) {
        char *headers = (char *)malloc(headers_len + 1);
        if (!headers) {
            emscripten_fetch_close(fetch);
            free(owned_request);
            error("Out of memory");
        }
        emscripten_fetch_get_response_headers(fetch, headers, headers_len + 1);
        append_response_text(parsed.buffer, parsed.payload_capacity, &total,
                             headers);
        size_t n = strlen(headers);
        if (n == 0 || headers[n - 1] != '\n') {
            append_response_text(parsed.buffer, parsed.payload_capacity, &total,
                                 "\r\n");
        }
        free(headers);
    }
    append_response_text(parsed.buffer, parsed.payload_capacity, &total,
                         "\r\n");
    append_response(parsed.buffer, parsed.payload_capacity, &total,
                    fetch->data, (size_t)fetch->numBytes);
    parsed.dest[0] = total;

    emscripten_fetch_close(fetch);
#else
    (void)method;
    (void)url;
    (void)body;
    (void)body_len;
    free(owned_request);
    error("WEB networking not supported in browser build");
#endif
    free(owned_request);
}

static int wasm_mqtt_cmd(unsigned char *line) {
    static mm_net_mqtt_hal_context_t ctx;
    ctx.client = &wasm_mqtt_client;
    ctx.connected = &wasm_mqtt_connected;
    ctx.client_id = "PicoMiteWASM";
    ctx.ensure_net = NULL;
    ctx.after_subscribe = NULL;
    return mm_net_mqtt_hal_cmd(line, &ctx);
}

void cmd_web(void) {
    unsigned char *tp;
    if (wasm_mqtt_cmd(cmdline)) return;
    if ((tp = checkstring(cmdline, (unsigned char *)"OPEN TCP CLIENT"))) {
        wasm_tcp_client_open_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT REQUEST"))) {
        wasm_tcp_client_request_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"CLOSE TCP CLIENT"))) {
        if (*tp) error("Syntax");
        if (!wasm_tcp_client_opened) error("No connection");
        wasm_tcp_client_reset();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"OPEN TCP STREAM") ||
        checkstring(cmdline, (unsigned char *)"TCP CLIENT STREAM")) {
        wasm_web_unsupported();
    }
    wasm_web_unsupported();
}

void port_repl_wifi_arch_init_and_connect(void) {
}

void port_web_clear_runtime_state(void) {
    static const mm_net_lifecycle_runtime_hooks_t hooks = {
        .close_tcp_client = close_tcpclient,
        .close_mqtt = closeMQTT,
    };
    mm_net_lifecycle_runtime_reset(&hooks);
}

void port_web_print_options(void) {
}

int port_web_option_setter(unsigned char *cmdline) {
    return mm_net_lifecycle_handle_option_result(
        mm_net_lifecycle_option_setter(cmdline, NULL),
        &wasm_lifecycle_result_handler);
}

static int wasm_mminfo_string(unsigned char *out_sret, int *out_targ,
                              const char *value) {
    strcpy((char *)out_sret, value);
    CtoM(out_sret);
    *out_targ = T_STR;
    return 1;
}

static int wasm_info_empty_ip(char *out, size_t out_len) {
    if (out_len) out[0] = 0;
    return HAL_NET_OK;
}

static int wasm_info_unsupported_status(void) {
    return HAL_NET_UNSUPPORTED;
}

int port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                    unsigned char *out_sret, int *out_targ) {
    if (checkstring(ep, (unsigned char *)"SSID"))
        return wasm_mminfo_string(out_sret, out_targ, "");
    static const mm_net_info_hooks_t hooks = {
        .max_connections = 0,
        .tcp_port = 0,
        .udp_port = 0,
        .ip_address = wasm_info_empty_ip,
        .wifi_status = wasm_info_unsupported_status,
        .tcpip_status = wasm_info_unsupported_status,
    };
    return mm_net_mminfo(ep, out_iret, out_sret, out_targ, &hooks);
}

int port_web_get_ssid(unsigned char *out_sret, int *out_targ) {
    return wasm_mminfo_string(out_sret, out_targ, "");
}

int wifi_serial_telnet_configured(void) {
    return 1;
}
