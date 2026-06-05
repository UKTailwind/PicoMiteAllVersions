/*
 * ports/host_wasm/host_wasm_web.c - browser WEB command surface.
 *
 * Browser WASM cannot expose raw sockets directly. Keep the BASIC-visible
 * hooks present so the host runtime links, implement browser HTTP fetch and
 * MQTT-over-WebSocket client paths, and use the trusted proxy for raw TCP,
 * UDP, and TFTP when it is available.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "hal/hal_calendar.h"
#include "hal/hal_filesystem.h"
#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_http_page.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_mqtt_wire.h"
#include "shared/net/mm_net_ntp_hal.h"
#include "shared/net/mm_net_options.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_state.h"
#include "shared/net/mm_net_tcp_client_cmd.h"
#include "shared/net/mm_net_tcp_server_cmd.h"
#include "shared/net/mm_net_tftp.h"
#include "shared/net/mm_net_transmit_cmd.h"
#include "shared/net/mm_net_udp_cmd.h"

volatile int WIFIconnected = 0;
bool optionsuppressstatus = false;
int startupcomplete = 0;

void port_web_clear_runtime_state(void);
void host_telnet_putc(int c, int flush);
void closeMQTT(void);

static const mm_net_lifecycle_result_handler_t wasm_lifecycle_result_handler = {
    .unsupported_error = "WEB networking not supported in browser build",
    .apply_error = "Syntax",
};

#define WASM_TCP_HOST_MAX 256
#define WASM_FETCH_MAX_HEADERS 16
#define WASM_MQTT_FRAME_MAX 2048
#define WASM_WEB_MAX_PCB 4
#define WASM_WEB_RECV_CAP 2048

static char wasm_tcp_host[WASM_TCP_HOST_MAX];
static int wasm_tcp_port;
static int wasm_tcp_client_opened;
static int wasm_proxy_detected;
static int wasm_proxy_http_capability;
static int wasm_proxy_tcp_stream_capability;
static int wasm_proxy_tcp_server_capability;
static int wasm_proxy_udp_capability;
static int wasm_proxy_ntp_capability;
static int wasm_proxy_tftp_capability;
static int wasm_proxy_telnet_capability;
static int wasm_proxy_mqtt_plain_capability;
static uint16_t wasm_proxy_tftp_port = 69;
static uint16_t wasm_proxy_telnet_port = 23;
static int wasm_proxy_tcp_stream_id;
static int wasm_tcp_client_stream_active;
static uint8_t * wasm_tcp_client_stream_buf;
static int wasm_tcp_client_stream_size;
static int64_t * wasm_tcp_client_stream_read;
static int64_t * wasm_tcp_client_stream_write;
static hal_net_mqtt_client_t wasm_mqtt_client;
static int wasm_mqtt_connected;
static int wasm_mqtt_proxy_transport;
static int wasm_mqtt_proxy_stream_id;
static uint16_t wasm_mqtt_next_packet_id = 1;
static uint8_t wasm_mqtt_rx[WASM_MQTT_FRAME_MAX * 2];
static size_t wasm_mqtt_rx_len;
static mm_net_tcp_service_t wasm_tcp_server;
static mm_net_udp_service_t wasm_udp_server;
static hal_net_udp_socket_t wasm_tftp_server;
static int wasm_tftp_server_opened;
static hal_net_tcp_server_t wasm_telnet_server;
static hal_net_tcp_conn_t wasm_telnet_conn;
static int wasm_telnet_open_failed;
static mm_net_tcp_service_slot_t wasm_tcp_slots[WASM_WEB_MAX_PCB];
static uint8_t wasm_tcp_recv_buf[WASM_WEB_MAX_PCB][WASM_WEB_RECV_CAP];
static char wasm_tcp_path[WASM_WEB_MAX_PCB][256];
static mm_net_tftp_session_t wasm_tftp;
static char wasm_telnet_buf[256];
static int wasm_telnet_pos;
static int wasm_telnet_lastchar = -1;
static int wasm_telnet_iac_state;
static const uint8_t wasm_telnet_init_options[] = {0};
static const size_t wasm_telnet_init_options_len = 0;

static int wasm_tcp_server_open_port(uint16_t port);
static void wasm_tcp_close_server(void);
static void wasm_tcp_service_init(void);
static void wasm_udp_poll(void);
static int wasm_udp_server_open_port(uint16_t port);
static void wasm_udp_close_server(void);
static int wasm_tftp_open_server(void);
static void wasm_tftp_close_server(void);
static void wasm_tftp_poll(void);
static void wasm_tftp_close_session(void);
static int wasm_telnet_open_server(void);
static void wasm_telnet_close_server(void);
static void wasm_telnet_close_conn(void);
static void wasm_telnet_poll(int mode);
extern void wasm_push_key(int code);

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_mode(int enabled) {
    wasm_proxy_detected = enabled ? 1 : 0;
    if (!wasm_proxy_detected) {
        wasm_proxy_http_capability = 0;
        wasm_proxy_tcp_stream_capability = 0;
        wasm_proxy_tcp_server_capability = 0;
        wasm_proxy_udp_capability = 0;
        wasm_proxy_ntp_capability = 0;
        wasm_proxy_tftp_capability = 0;
        wasm_proxy_telnet_capability = 0;
        wasm_proxy_mqtt_plain_capability = 0;
        wasm_tcp_close_server();
        wasm_udp_close_server();
        wasm_tftp_close_server();
        wasm_telnet_close_server();
        closeMQTT();
    }
    WIFIconnected = wasm_proxy_detected ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_http_capability(int enabled) {
    wasm_proxy_http_capability = enabled ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_tcp_stream_capability(int enabled) {
    wasm_proxy_tcp_stream_capability = enabled ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_tcp_server_capability(int enabled) {
    wasm_proxy_tcp_server_capability = enabled ? 1 : 0;
    if (!wasm_proxy_tcp_server_capability) wasm_tcp_close_server();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_udp_capability(int enabled) {
    wasm_proxy_udp_capability = enabled ? 1 : 0;
    if (!wasm_proxy_udp_capability) {
        wasm_udp_close_server();
        wasm_tftp_close_server();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_ntp_capability(int enabled) {
    wasm_proxy_ntp_capability = enabled ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_tftp_capability(int enabled) {
    wasm_proxy_tftp_capability = enabled ? 1 : 0;
    if (!wasm_proxy_tftp_capability) wasm_tftp_close_server();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_telnet_capability(int enabled) {
    wasm_proxy_telnet_capability = enabled ? 1 : 0;
    if (!wasm_proxy_telnet_capability) wasm_telnet_close_server();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_mqtt_plain_capability(int enabled) {
    wasm_proxy_mqtt_plain_capability = enabled ? 1 : 0;
    if (!wasm_proxy_mqtt_plain_capability && wasm_mqtt_proxy_transport)
        closeMQTT();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_tftp_port(int port) {
    if (port > 0 && port <= 65535) {
        uint16_t next = (uint16_t)port;
        if (next != wasm_proxy_tftp_port) {
            wasm_tftp_close_server();
            wasm_proxy_tftp_port = next;
        }
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_set_proxy_telnet_port(int port) {
    if (port > 0 && port <= 65535) {
        uint16_t next = (uint16_t)port;
        if (next != wasm_proxy_telnet_port) {
            wasm_telnet_close_server();
            wasm_proxy_telnet_port = next;
        }
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int wasm_proxy_mode(void) {
    return wasm_proxy_detected;
}

#ifdef __EMSCRIPTEN__
static int wasm_mqtt_js_open(const char * url_ptr) {
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
        } }, url_ptr);
}

static int wasm_mqtt_js_send(const uint8_t * data, int len) {
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
        return -1; }, data, len);
}

static int wasm_mqtt_js_recv(uint8_t * dst, int cap) {
    return MAIN_THREAD_EM_ASM_INT({
        const state = Module.__picomiteMqtt;
        if (!state || !state.rx.length) return 0;
        const msg = state.rx.shift();
        if (msg.length > $1) return -1;
        HEAPU8.set(msg, $0);
        return msg.length; }, dst, cap);
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
        } catch (e) {
        }
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
    wasm_proxy_tcp_stream_id = 0;
    wasm_tcp_client_stream_active = 0;
    wasm_tcp_client_stream_buf = NULL;
    wasm_tcp_client_stream_size = 0;
    wasm_tcp_client_stream_read = NULL;
    wasm_tcp_client_stream_write = NULL;
}

static void wasm_tcp_service_init(void) {
    static int inited;
    if (inited) return;
    for (int i = 0; i < WASM_WEB_MAX_PCB; ++i) {
        mm_net_tcp_service_slot_init(&wasm_tcp_slots[i],
                                     wasm_tcp_recv_buf[i],
                                     sizeof(wasm_tcp_recv_buf[i]),
                                     wasm_tcp_path[i],
                                     sizeof(wasm_tcp_path[i]));
    }
    mm_net_tcp_service_init(&wasm_tcp_server, wasm_tcp_slots,
                            WASM_WEB_MAX_PCB);
    inited = 1;
}

#ifdef __EMSCRIPTEN__
static int wasm_proxy_tcp_open_js(const char * host, int port, int timeout_ms);
static int wasm_proxy_tcp_stream_js(int stream_id, const uint8_t * write_data,
                                    int write_len, uint8_t * dst, int dst_cap,
                                    int timeout_ms);
static void wasm_proxy_tcp_close_js(int stream_id);
static int wasm_proxy_tcp_listen_js(int port, int backlog);
static void wasm_proxy_tcp_listener_close_js(int listener_id);
static int wasm_proxy_tcp_accept_conn_js(int listener_id);
static int wasm_proxy_tcp_accept_js(int listener_id, uint8_t * dst, int dst_cap,
                                    int * out_conn_id);
static int wasm_proxy_tcp_conn_recv_js(int conn_id, uint8_t * dst,
                                       int dst_cap);
static int wasm_proxy_tcp_conn_send_js(int conn_id, const uint8_t * data,
                                       int len, int timeout_ms);
static void wasm_proxy_tcp_conn_close_js(int conn_id);
static int wasm_proxy_udp_bind_js(int port);
static int wasm_proxy_udp_send_js(int socket_id, const char * host, int port,
                                  const uint8_t * data, int len,
                                  int timeout_ms);
static int wasm_proxy_udp_recv_js(int socket_id, uint8_t * dst, int dst_cap,
                                  hal_net_addr_t * from);
static void wasm_proxy_udp_close_js(int socket_id);
#endif

static uint16_t wasm_mqtt_next_id(void) {
    if (wasm_mqtt_next_packet_id == 0) wasm_mqtt_next_packet_id = 1;
    uint16_t id = wasm_mqtt_next_packet_id++;
    if (wasm_mqtt_next_packet_id == 0) wasm_mqtt_next_packet_id = 1;
    return id;
}

static int wasm_mqtt_decode_frame(const uint8_t * frame, size_t frame_len,
                                  uint8_t * header, const uint8_t ** body,
                                  size_t * body_len) {
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

static int wasm_mqtt_rx_append(const uint8_t * data, size_t len) {
    if (!data || len == 0) return HAL_NET_OK;
    if (len > sizeof(wasm_mqtt_rx) - wasm_mqtt_rx_len) return HAL_NET_ERR;
    memcpy(wasm_mqtt_rx + wasm_mqtt_rx_len, data, len);
    wasm_mqtt_rx_len += len;
    return HAL_NET_OK;
}

static int wasm_mqtt_rx_take_packet(uint8_t * header, uint8_t * body,
                                    size_t body_cap, size_t * body_len) {
    if (body_len) *body_len = 0;
    if (!header || !body || !body_len || wasm_mqtt_rx_len < 2)
        return HAL_NET_WOULD_BLOCK;

    size_t multiplier = 1;
    size_t remaining = 0;
    size_t pos = 1;
    for (int i = 0; i < 4; ++i) {
        if (pos >= wasm_mqtt_rx_len) return HAL_NET_WOULD_BLOCK;
        uint8_t encoded = wasm_mqtt_rx[pos++];
        remaining += (size_t)(encoded & 127u) * multiplier;
        if ((encoded & 128u) == 0) {
            if (remaining > body_cap) return HAL_NET_ERR;
            if (pos + remaining > wasm_mqtt_rx_len)
                return HAL_NET_WOULD_BLOCK;
            *header = wasm_mqtt_rx[0];
            if (remaining) memcpy(body, wasm_mqtt_rx + pos, remaining);
            *body_len = remaining;
            size_t consumed = pos + remaining;
            memmove(wasm_mqtt_rx, wasm_mqtt_rx + consumed,
                    wasm_mqtt_rx_len - consumed);
            wasm_mqtt_rx_len -= consumed;
            return HAL_NET_OK;
        }
        multiplier *= 128u;
    }
    return HAL_NET_ERR;
}

static void wasm_mqtt_sleep_poll(void) {
    hal_time_sleep_us(10000);
}

static int wasm_mqtt_proxy_read_some(uint32_t timeout_ms) {
#ifdef __EMSCRIPTEN__
    if (!wasm_mqtt_proxy_transport || wasm_mqtt_proxy_stream_id <= 0)
        return HAL_NET_UNSUPPORTED;
    uint8_t buf[512];
    int got = wasm_proxy_tcp_stream_js(wasm_mqtt_proxy_stream_id, NULL, 0,
                                       buf, sizeof(buf),
                                       timeout_ms ? (int)timeout_ms : 1);
    if (got < 0) return HAL_NET_ERR;
    return wasm_mqtt_rx_append(buf, (size_t)got);
#else
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
#endif
}

static int wasm_mqtt_transport_send_packet(uint8_t header,
                                           const uint8_t * body,
                                           size_t body_len,
                                           uint32_t timeout_ms) {
    uint8_t packet[WASM_MQTT_FRAME_MAX];
    uint8_t rem[4];
    size_t rem_len = mm_net_mqtt_encode_remaining_length(rem, body_len);
    size_t total = 1 + rem_len + body_len;
    if (total > sizeof(packet)) return HAL_NET_ERR;
    packet[0] = header;
    memcpy(packet + 1, rem, rem_len);
    if (body_len) memcpy(packet + 1 + rem_len, body, body_len);
#ifdef __EMSCRIPTEN__
    if (wasm_mqtt_proxy_transport) {
        uint8_t reply[512];
        int got = wasm_proxy_tcp_stream_js(wasm_mqtt_proxy_stream_id, packet,
                                           (int)total, reply, sizeof(reply),
                                           timeout_ms ? (int)timeout_ms
                                                      : 5000);
        if (got < 0) return HAL_NET_ERR;
        return wasm_mqtt_rx_append(reply, (size_t)got);
    }
    return wasm_mqtt_js_send(packet, (int)total) == 0 ? HAL_NET_OK
                                                      : HAL_NET_ERR;
#else
    (void)packet;
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
#endif
}

static int wasm_mqtt_read_packet(uint8_t * header, uint8_t * body,
                                 size_t body_cap, size_t * body_len,
                                 uint32_t timeout_ms) {
    if (body_len) *body_len = 0;
    if (!header || !body || !body_len) return HAL_NET_ERR;
#ifdef __EMSCRIPTEN__
    if (!wasm_mqtt_proxy_transport) {
        uint8_t frame[WASM_MQTT_FRAME_MAX];
        int got = wasm_mqtt_js_recv(frame, sizeof(frame));
        if (got == 0) return HAL_NET_WOULD_BLOCK;
        if (got < 0) return HAL_NET_ERR;
        const uint8_t * decoded_body = NULL;
        size_t decoded_body_len = 0;
        if (!wasm_mqtt_decode_frame(frame, (size_t)got, header,
                                    &decoded_body, &decoded_body_len))
            return HAL_NET_WOULD_BLOCK;
        if (decoded_body_len > body_cap) return HAL_NET_ERR;
        if (decoded_body_len) memcpy(body, decoded_body, decoded_body_len);
        *body_len = decoded_body_len;
        return HAL_NET_OK;
    }

    uint64_t deadline = timeout_ms
                            ? hal_time_us_64() + (uint64_t)timeout_ms * 1000ULL
                            : 0;
    for (;;) {
        int rc = wasm_mqtt_rx_take_packet(header, body, body_cap, body_len);
        if (rc != HAL_NET_WOULD_BLOCK) return rc;
        rc = wasm_mqtt_proxy_read_some(1);
        if (rc != HAL_NET_OK) return rc;
        rc = wasm_mqtt_rx_take_packet(header, body, body_cap, body_len);
        if (rc != HAL_NET_WOULD_BLOCK) return rc;
        if (!timeout_ms) return HAL_NET_WOULD_BLOCK;
        if (hal_time_us_64() >= deadline) return HAL_NET_TIMEOUT;
        wasm_mqtt_sleep_poll();
    }
#else
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
#endif
}

static void wasm_mqtt_handle_publish(uint8_t header, const uint8_t * body,
                                     size_t body_len) {
    char topic[MAXSTRLEN + 1];
    const uint8_t * payload = NULL;
    size_t payload_len = 0;
    if (mm_net_mqtt_decode_publish(header, body, body_len, topic,
                                   sizeof(topic), &payload, &payload_len)) {
        mm_net_state_set_mstring(MM_NET_STATE_TOPIC, topic, strlen(topic));
        mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, payload, payload_len);
        MQTTComplete = true;
    }
}

static int wasm_mqtt_wait_for_type(uint8_t want_type, uint32_t timeout_ms) {
    uint8_t header = 0;
    uint8_t body[1024];
    size_t body_len = 0;
    uint64_t deadline = timeout_ms
                            ? hal_time_us_64() + (uint64_t)timeout_ms * 1000ULL
                            : 0;
    for (;;) {
        uint32_t wait_ms = timeout_ms;
        if (deadline) {
            uint64_t now = hal_time_us_64();
            if (now >= deadline) return HAL_NET_TIMEOUT;
            wait_ms = (uint32_t)((deadline - now + 999ULL) / 1000ULL);
        }
        int rc = wasm_mqtt_read_packet(&header, body, sizeof(body),
                                       &body_len, wait_ms);
        if (rc != HAL_NET_OK) return rc;
        if ((header >> 4) == want_type) return HAL_NET_OK;
        if ((header >> 4) == 3) wasm_mqtt_handle_publish(header, body,
                                                         body_len);
    }
}

static int wasm_mqtt_send_packet(uint8_t header, const uint8_t * body,
                                 size_t body_len) {
    return wasm_mqtt_transport_send_packet(header, body, body_len, 5000);
}

static int wasm_mqtt_build_url(const char * host, uint16_t port, char * out,
                               size_t out_len) {
    if (!host || !*host || !out || out_len == 0) return 0;
    if (strncmp(host, "ws://", 5) == 0 || strncmp(host, "wss://", 6) == 0) {
        int n = snprintf(out, out_len, "%s", host);
        return n > 0 && (size_t)n < out_len;
    }

    const char * path = strchr(host, '/');
    size_t host_len = path ? (size_t)(path - host) : strlen(host);
    if (host_len == 0 || host_len >= 300) return 0;
    const char * scheme = (port == 443) ? "wss" : "ws";
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
        uint8_t header = 0;
        uint8_t body[1024];
        size_t body_len = 0;
        int rc = wasm_mqtt_read_packet(&header, body, sizeof(body),
                                       &body_len, 0);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK) return;
        if ((header >> 4) == 3) {
            wasm_mqtt_handle_publish(header, body, body_len);
        }
    }
#endif
}

static void wasm_tcp_client_stream_poll(void) {
#ifdef __EMSCRIPTEN__
    if (!wasm_proxy_detected || !wasm_proxy_tcp_stream_capability ||
        !wasm_tcp_client_opened || wasm_proxy_tcp_stream_id <= 0 ||
        !wasm_tcp_client_stream_active || !wasm_tcp_client_stream_buf ||
        !wasm_tcp_client_stream_read || !wasm_tcp_client_stream_write ||
        wasm_tcp_client_stream_size <= 1)
        return;

    uint8_t buf[512];
    for (;;) {
        int got = wasm_proxy_tcp_stream_js(wasm_proxy_tcp_stream_id, NULL, 0,
                                           buf, sizeof(buf), 5000);
        if (got <= 0) {
            if (got < 0) wasm_tcp_client_stream_active = 0;
            return;
        }
        mm_net_tcp_client_stream_append(wasm_tcp_client_stream_buf,
                                        wasm_tcp_client_stream_size,
                                        wasm_tcp_client_stream_read,
                                        wasm_tcp_client_stream_write,
                                        buf, (size_t)got);
        if (got < (int)sizeof(buf)) return;
    }
#endif
}

static void wasm_tcp_server_poll(void) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability) return;
    wasm_tcp_service_init();
    mm_net_tcp_service_poll(&wasm_tcp_server);
}

static int wasm_tcp_server_open_port(uint16_t port) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability || port == 0)
        return 0;
    wasm_tcp_service_init();
    return mm_net_tcp_service_open(&wasm_tcp_server, port, WASM_WEB_MAX_PCB);
}

static void wasm_tcp_close_server(void) {
    wasm_tcp_service_init();
    mm_net_tcp_service_stop(&wasm_tcp_server);
}

static void wasm_tcp_clear_requests(void) {
    wasm_tcp_service_init();
    mm_net_tcp_service_clear_requests(&wasm_tcp_server);
}

static const char * wasm_info_tcp_path(int slot) {
    wasm_tcp_service_init();
    return mm_net_tcp_service_path(&wasm_tcp_server, slot);
}

static int wasm_info_tcp_request_pending(int slot) {
    wasm_tcp_service_init();
    return mm_net_tcp_service_request_pending(&wasm_tcp_server, slot);
}

static void wasm_info_before_query(void) {
    wasm_tcp_server_poll();
    wasm_udp_poll();
}

static void wasm_udp_poll(void) {
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability) return;
    mm_net_udp_service_poll(&wasm_udp_server);
}

static int wasm_udp_server_open_port(uint16_t port) {
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability || port == 0)
        return 0;
    return mm_net_udp_service_open(&wasm_udp_server, port);
}

static void wasm_udp_close_server(void) {
    mm_net_udp_service_stop(&wasm_udp_server);
}

static int wasm_tftp_peer_text(const mm_net_tftp_peer_t * peer, char * out,
                               size_t out_len) {
    if (!peer || !out || out_len == 0) return 0;
    if (peer->family == 4) {
        int n = snprintf(out, out_len, "%u.%u.%u.%u",
                         (unsigned)peer->bytes[0],
                         (unsigned)peer->bytes[1],
                         (unsigned)peer->bytes[2],
                         (unsigned)peer->bytes[3]);
        return n > 0 && (size_t)n < out_len;
    }
    return 0;
}

static int wasm_tftp_send(void * ctx, const mm_net_tftp_peer_t * peer,
                          const void * buf, size_t len) {
    (void)ctx;
    char host[64];
    if (!wasm_tftp_server_opened ||
        !wasm_tftp_peer_text(peer, host, sizeof(host)))
        return 0;
    return hal_net_udp_socket_send(wasm_tftp_server, host, peer->port, buf,
                                   len, 1000) == HAL_NET_OK;
}

static int wasm_tftp_open_file(void * ctx, const char * filename, int write,
                               void ** handle) {
    (void)ctx;
    hal_fs_fd_t fd;
    int flags = write ? (HAL_FS_O_WRONLY | HAL_FS_O_CREAT | HAL_FS_O_TRUNC)
                      : HAL_FS_O_RDONLY;
    if (hal_fs_open(filename, flags, &fd) < 0) return -1;
    *handle = (void *)(intptr_t)fd;
    return 0;
}

static ssize_t wasm_tftp_read_file(void * ctx, void * handle, void * buf,
                                   size_t len) {
    (void)ctx;
    return hal_fs_read((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static ssize_t wasm_tftp_write_file(void * ctx, void * handle, const void * buf,
                                    size_t len) {
    (void)ctx;
    return hal_fs_write((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static void wasm_tftp_close_file(void * ctx, void * handle) {
    (void)ctx;
    hal_fs_close((hal_fs_fd_t)(intptr_t)handle);
}

static const mm_net_tftp_ops_t wasm_tftp_ops = {
    .open = wasm_tftp_open_file,
    .read = wasm_tftp_read_file,
    .write = wasm_tftp_write_file,
    .close = wasm_tftp_close_file,
    .send = wasm_tftp_send,
};

static void wasm_tftp_ensure_init(void) {
    if (!wasm_tftp.ops) mm_net_tftp_init(&wasm_tftp, &wasm_tftp_ops, NULL);
}

static void wasm_tftp_close_session(void) {
    mm_net_tftp_close(&wasm_tftp);
}

static void wasm_tftp_close_server(void) {
    wasm_tftp_close_session();
    if (wasm_tftp_server_opened) {
        hal_net_udp_close(wasm_tftp_server);
        wasm_tftp_server = 0;
        wasm_tftp_server_opened = 0;
    }
}

static int wasm_tftp_open_server(void) {
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability ||
        !wasm_proxy_tftp_capability)
        return 1;
    wasm_tftp_ensure_init();
    if (Option.disabletftp) {
        wasm_tftp_close_server();
        return 1;
    }
    if (wasm_tftp_server_opened) return 1;
    if (hal_net_udp_bind(wasm_proxy_tftp_port, &wasm_tftp_server) !=
        HAL_NET_OK)
        return 0;
    wasm_tftp_server_opened = 1;
    return 1;
}

static void wasm_tftp_poll(void) {
    uint8_t pkt[600];
    hal_net_addr_t from;
    mm_net_tftp_peer_t peer;
    size_t len = 0;
    if (!wasm_tftp_server_opened) return;
    wasm_tftp_ensure_init();
    for (;;) {
        int rc = hal_net_udp_recv_event(wasm_tftp_server, &from, pkt,
                                        sizeof(pkt), &len);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || len < 2) return;
        memset(&peer, 0, sizeof(peer));
        peer.family = from.family;
        peer.port = from.port;
        memcpy(peer.bytes, from.bytes, sizeof(peer.bytes));
        mm_net_tftp_handle_packet(&wasm_tftp, &peer, pkt, len);
    }
}

static void wasm_telnet_close_conn(void) {
    if (wasm_telnet_conn) {
        hal_net_tcp_conn_close(wasm_telnet_conn);
        wasm_telnet_conn = 0;
    }
    wasm_telnet_pos = 0;
    wasm_telnet_iac_state = 0;
}

static void wasm_telnet_close_server(void) {
    wasm_telnet_close_conn();
    if (wasm_telnet_server) {
        hal_net_tcp_server_close(wasm_telnet_server);
        wasm_telnet_server = 0;
    }
    wasm_telnet_open_failed = 0;
}

static int wasm_telnet_open_server(void) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability ||
        !wasm_proxy_telnet_capability)
        return 1;
    if (!Option.Telnet) {
        wasm_telnet_close_server();
        return 1;
    }
    if (wasm_telnet_server) return 1;
    if (wasm_telnet_open_failed) return 0;
    if (hal_net_tcp_server_open(wasm_proxy_telnet_port, 1,
                                &wasm_telnet_server) == HAL_NET_OK)
        return 1;
    wasm_telnet_open_failed = 1;
    return 0;
}

static void wasm_telnet_receive_bytes(const uint8_t * data, size_t len) {
    if (!data || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        int c = data[i];
        if (wasm_telnet_iac_state == 1) {
            if (c == 255) {
                wasm_telnet_iac_state = 0;
            } else if (c == 251 || c == 252 || c == 253 || c == 254) {
                wasm_telnet_iac_state = 2;
                continue;
            } else if (c == 250) {
                wasm_telnet_iac_state = 3;
                continue;
            } else {
                wasm_telnet_iac_state = 0;
                continue;
            }
        } else if (wasm_telnet_iac_state == 2) {
            wasm_telnet_iac_state = 0;
            continue;
        } else if (wasm_telnet_iac_state == 3) {
            if (c == 255) wasm_telnet_iac_state = 4;
            continue;
        } else if (wasm_telnet_iac_state == 4) {
            wasm_telnet_iac_state = (c == 240) ? 0 : 3;
            continue;
        } else if (c == 255) {
            wasm_telnet_iac_state = 1;
            continue;
        }
        if ((wasm_telnet_lastchar == 13 && c == 0) ||
            (wasm_telnet_lastchar == 13 && c == 10) ||
            (wasm_telnet_lastchar == 255 && c == 255)) {
            wasm_telnet_lastchar = -1;
            continue;
        }
        if (BreakKey && c == BreakKey) {
            MMAbort = true;
        } else {
            wasm_telnet_lastchar = c;
            wasm_push_key(c);
        }
    }
}

static void wasm_telnet_poll(int mode) {
    static uint64_t flushtimer;
    if (!Option.Telnet) {
        wasm_telnet_close_server();
        return;
    }
    if (!wasm_telnet_server && !wasm_telnet_open_server()) return;
    if (!wasm_telnet_conn) {
        hal_net_tcp_conn_t conn = 0;
        int rc = hal_net_tcp_accept_conn(wasm_telnet_server, &conn);
        if (rc == HAL_NET_OK) {
            wasm_telnet_conn = conn;
            if (wasm_telnet_init_options_len > 0 &&
                hal_net_tcp_conn_send(wasm_telnet_conn,
                                      wasm_telnet_init_options,
                                      wasm_telnet_init_options_len,
                                      5000) != HAL_NET_OK) {
                wasm_telnet_close_conn();
                return;
            }
        } else if (rc != HAL_NET_WOULD_BLOCK) {
            wasm_telnet_close_server();
            return;
        }
    }
    if (!wasm_telnet_conn) return;
    uint8_t buf[128];
    for (;;) {
        size_t len = 0;
        int rc = hal_net_tcp_conn_recv(wasm_telnet_conn, buf, sizeof(buf),
                                       &len);
        if (rc == HAL_NET_OK) {
            wasm_telnet_receive_bytes(buf, len);
            continue;
        }
        if (rc != HAL_NET_WOULD_BLOCK) wasm_telnet_close_conn();
        break;
    }
    if (mode && wasm_telnet_conn && hal_time_us_64() > flushtimer) {
        host_telnet_putc(0, -1);
        flushtimer = hal_time_us_64() + 5000;
    }
}

void closeMQTT(void) {
#ifdef __EMSCRIPTEN__
    if (wasm_mqtt_proxy_stream_id > 0) {
        wasm_proxy_tcp_close_js(wasm_mqtt_proxy_stream_id);
    } else if (wasm_mqtt_connected) {
        wasm_mqtt_js_close();
    }
#endif
    wasm_mqtt_connected = 0;
    wasm_mqtt_client = 0;
    wasm_mqtt_proxy_transport = 0;
    wasm_mqtt_proxy_stream_id = 0;
    wasm_mqtt_next_packet_id = 1;
    wasm_mqtt_rx_len = 0;
}

void ProcessWeb(int mode) {
    static const mm_net_lifecycle_poll_hooks_t hooks = {
        .poll_udp = wasm_udp_poll,
        .poll_tftp = wasm_tftp_poll,
        .poll_tcp_client_stream = wasm_tcp_client_stream_poll,
        .poll_mqtt = wasm_mqtt_poll,
        .poll_tcp_server = wasm_tcp_server_poll,
        .poll_telnet = wasm_telnet_poll,
    };
    mm_net_lifecycle_poll(&hooks, mode, 0);
}

int host_tcp_interrupt_pending(void) {
    wasm_tcp_service_init();
    return mm_net_tcp_service_interrupt_pending(&wasm_tcp_server);
}

void cleanserver(void) {
    port_web_clear_runtime_state();
}

void close_tcpclient(void) {
#ifdef __EMSCRIPTEN__
    if (wasm_proxy_tcp_stream_id > 0) {
        wasm_proxy_tcp_close_js(wasm_proxy_tcp_stream_id);
    }
#endif
    wasm_tcp_client_reset();
}

void host_telnet_putc(int c, int flush) {
    if (!wasm_telnet_conn) return;
    if (flush != -1) {
        wasm_telnet_buf[wasm_telnet_pos++] = (char)c;
        if (c == 255) wasm_telnet_buf[wasm_telnet_pos++] = (char)c;
        if (c == 13) wasm_telnet_buf[wasm_telnet_pos++] = 0;
    }
    if (wasm_telnet_pos >= (int)sizeof(wasm_telnet_buf) - 4 ||
        (flush == -1 && wasm_telnet_pos)) {
        if (hal_net_tcp_conn_send(wasm_telnet_conn, wasm_telnet_buf,
                                  (size_t)wasm_telnet_pos, 5000) !=
            HAL_NET_OK) {
            wasm_telnet_close_conn();
        }
        wasm_telnet_pos = 0;
    }
}

void WebConnect(void) {
    wasm_web_unsupported();
}

uint32_t hal_net_capabilities(void) {
    uint32_t caps = HAL_NET_CAP_HTTP_FETCH;
    if (!wasm_proxy_detected) caps |= HAL_NET_CAP_MQTT_WEBSOCKET;
    if (wasm_proxy_detected && wasm_proxy_tcp_stream_capability) {
        caps |= HAL_NET_CAP_TCP_STREAM;
        if (wasm_proxy_mqtt_plain_capability)
            caps |= HAL_NET_CAP_MQTT_PLAIN;
    }
    if (wasm_proxy_detected && wasm_proxy_tcp_server_capability)
        caps |= HAL_NET_CAP_TCP_SERVER;
    if (wasm_proxy_detected && wasm_proxy_udp_capability)
        caps |= HAL_NET_CAP_UDP_SERVER | HAL_NET_CAP_UDP_SEND;
    return caps;
}

int hal_net_init(void) {
    return HAL_NET_OK;
}

void hal_net_poll(void) {
    wasm_tcp_server_poll();
    wasm_udp_poll();
    wasm_mqtt_poll();
    wasm_telnet_poll(0);
}

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t * out) {
    if (out) *out = 0;
    if (!out || !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int id = wasm_proxy_tcp_listen_js((int)port, backlog);
    if (id <= 0) return HAL_NET_ERR;
    *out = (hal_net_tcp_server_t)id;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_server_close(hal_net_tcp_server_t server) {
    if (!server) return HAL_NET_OK;
#ifdef __EMSCRIPTEN__
    wasm_proxy_tcp_listener_close_js((int)server);
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t * conn) {
    if (conn) *conn = 0;
    if (!server || !conn ||
        !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int conn_id = wasm_proxy_tcp_accept_conn_js((int)server);
    if (conn_id == 0) return HAL_NET_WOULD_BLOCK;
    if (conn_id < 0) return HAL_NET_ERR;
    *conn = (hal_net_tcp_conn_t)conn_id;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t * conn,
                             uint8_t * buf, size_t cap, size_t * len) {
    if (conn) *conn = 0;
    if (len) *len = 0;
    if (!server || !conn || !buf || cap == 0 ||
        !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int conn_id = 0;
    int got = wasm_proxy_tcp_accept_js((int)server, buf, (int)cap, &conn_id);
    if (got == 0) return HAL_NET_WOULD_BLOCK;
    if (got < 0 || conn_id <= 0) return HAL_NET_ERR;
    *conn = (hal_net_tcp_conn_t)conn_id;
    if (len) *len = (size_t)got;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void * buf, size_t cap,
                          size_t * len) {
    if (len) *len = 0;
    if (!conn || !buf || cap == 0 || !len ||
        !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int got = wasm_proxy_tcp_conn_recv_js((int)conn, (uint8_t *)buf,
                                          (int)cap);
    if (got == 0) return HAL_NET_WOULD_BLOCK;
    if (got < 0) return HAL_NET_ERR;
    *len = (size_t)got;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void * buf, size_t len,
                          uint32_t timeout_ms) {
    if (!conn || (!buf && len) ||
        !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int rc = wasm_proxy_tcp_conn_send_js((int)conn, (const uint8_t *)buf,
                                         (int)len, (int)timeout_ms);
    return rc == 0 ? HAL_NET_OK : HAL_NET_ERR;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_conn_send_some(hal_net_tcp_conn_t conn, const void * buf,
                               size_t cap, size_t * sent) {
    if (sent) *sent = 0;
    if (!conn || (!buf && cap) ||
        !wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int rc = wasm_proxy_tcp_conn_send_js((int)conn, (const uint8_t *)buf,
                                         (int)cap, 0);
    if (rc == 0) {
        if (sent) *sent = cap;
        return HAL_NET_OK;
    }
    return HAL_NET_WOULD_BLOCK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn) {
    if (!conn) return HAL_NET_OK;
#ifdef __EMSCRIPTEN__
    wasm_proxy_tcp_conn_close_js((int)conn);
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t * out) {
    if (out) *out = 0;
    if (!out || !wasm_proxy_detected || !wasm_proxy_udp_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int id = wasm_proxy_udp_bind_js((int)port);
    if (id <= 0) return HAL_NET_ERR;
    *out = (hal_net_udp_socket_t)id;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_udp_close(hal_net_udp_socket_t sock) {
    if (!sock) return HAL_NET_OK;
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    wasm_proxy_udp_close_js((int)sock);
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char * host,
                            uint16_t port, const void * buf, size_t len,
                            uint32_t timeout_ms) {
    if (!host || (!buf && len) ||
        !wasm_proxy_detected || !wasm_proxy_udp_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    int rc = wasm_proxy_udp_send_js((int)sock, host, (int)port,
                                    (const uint8_t *)buf, (int)len,
                                    (int)timeout_ms);
    return rc == 0 ? HAL_NET_OK : HAL_NET_ERR;
#else
    (void)sock;
    (void)port;
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_udp_send(const char * host, uint16_t port,
                     const void * buf, size_t len, uint32_t timeout_ms) {
    return hal_net_udp_socket_send(0, host, port, buf, len, timeout_ms);
}

int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t * from,
                           void * buf, size_t cap, size_t * len) {
    if (len) *len = 0;
    if (!sock || !from || !buf || cap == 0 ||
        !wasm_proxy_detected || !wasm_proxy_udp_capability)
        return HAL_NET_UNSUPPORTED;
#ifdef __EMSCRIPTEN__
    memset(from, 0, sizeof(*from));
    int got = wasm_proxy_udp_recv_js((int)sock, (uint8_t *)buf, (int)cap,
                                     from);
    if (got == 0) return HAL_NET_WOULD_BLOCK;
    if (got < 0) return HAL_NET_ERR;
    if (len) *len = (size_t)got;
    return HAL_NET_OK;
#else
    return HAL_NET_UNSUPPORTED;
#endif
}

int hal_net_mqtt_connect(const char * host, uint16_t port, const char * user,
                         const char * pass, const char * client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t * out) {
    if (!host || !client_id || !out) return HAL_NET_ERR;
    *out = 0;
    closeMQTT();

#ifdef __EMSCRIPTEN__
    if (wasm_proxy_detected) {
        if (!wasm_proxy_tcp_stream_capability ||
            !wasm_proxy_mqtt_plain_capability)
            return HAL_NET_UNSUPPORTED;
        int stream_id = wasm_proxy_tcp_open_js(host, (int)port,
                                               (int)timeout_ms);
        if (stream_id <= 0) return HAL_NET_ERR;
        wasm_mqtt_proxy_transport = 1;
        wasm_mqtt_proxy_stream_id = stream_id;
    } else {
        char url[512];
        if (!wasm_mqtt_build_url(host, port, url, sizeof(url)))
            return HAL_NET_ERR;
        if (!wasm_mqtt_js_open(url)) return HAL_NET_ERR;
        wasm_mqtt_proxy_transport = 0;
        wasm_mqtt_proxy_stream_id = 0;
    }
#else
    return HAL_NET_UNSUPPORTED;
#endif

    uint8_t body[1024];
    uint8_t * p = body;
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
    if (wasm_mqtt_transport_send_packet(0x10, body, (size_t)(p - body),
                                        timeout_ms) != HAL_NET_OK) {
        closeMQTT();
        return HAL_NET_ERR;
    }
    if (wasm_mqtt_proxy_transport &&
        wasm_mqtt_wait_for_type(2, timeout_ms) != HAL_NET_OK) {
        closeMQTT();
        return HAL_NET_ERR;
    }
    *out = wasm_mqtt_client;
    return HAL_NET_OK;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char * topic,
                         const void * payload, size_t len, int qos,
                         int retain) {
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic ||
        (!payload && len))
        return HAL_NET_ERR;
    uint8_t body[1024];
    uint8_t * p = body;
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
    if (wasm_mqtt_send_packet(header, body, (size_t)(p - body)) !=
        HAL_NET_OK)
        return HAL_NET_ERR;
    if (wasm_mqtt_proxy_transport && qos == 1)
        return wasm_mqtt_wait_for_type(4, 5000);
    return HAL_NET_OK;
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char * topic,
                           int qos, uint32_t timeout_ms) {
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic)
        return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t * p = body;
    uint16_t id = wasm_mqtt_next_id();
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    *p++ = (uint8_t)qos;
    if (wasm_mqtt_transport_send_packet(0x82, body, (size_t)(p - body),
                                        timeout_ms) != HAL_NET_OK)
        return HAL_NET_ERR;
    if (wasm_mqtt_proxy_transport)
        return wasm_mqtt_wait_for_type(9, timeout_ms);
    return HAL_NET_OK;
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char * topic,
                             uint32_t timeout_ms) {
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic)
        return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t * p = body;
    uint16_t id = wasm_mqtt_next_id();
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    if (wasm_mqtt_transport_send_packet(0xA2, body, (size_t)(p - body),
                                        timeout_ms) != HAL_NET_OK)
        return HAL_NET_ERR;
    if (wasm_mqtt_proxy_transport)
        return wasm_mqtt_wait_for_type(11, timeout_ms);
    return HAL_NET_OK;
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char * topic,
                            size_t topic_cap, void * payload,
                            size_t payload_cap, size_t * payload_len) {
    if (payload_len) *payload_len = 0;
    if (topic && topic_cap) topic[0] = 0;
    if (!wasm_mqtt_connected || client != wasm_mqtt_client || !topic ||
        topic_cap == 0 || !payload)
        return HAL_NET_ERR;
#ifdef __EMSCRIPTEN__
    uint8_t header = 0;
    uint8_t body[1024];
    size_t body_len = 0;
    int rc = wasm_mqtt_read_packet(&header, body, sizeof(body), &body_len, 0);
    if (rc != HAL_NET_OK) return rc;
    const uint8_t * decoded_payload = NULL;
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

static int request_line_to_fetch(const uint8_t * request, size_t request_len,
                                 char * method, size_t method_len,
                                 char * url, size_t url_len,
                                 const char ** headers,
                                 int * header_count,
                                 char ** owned_request,
                                 char ** body, size_t * body_len) {
    *owned_request = NULL;
    *body = NULL;
    *body_len = 0;
    *header_count = 0;
    if (!request || request_len == 0) return 0;

    char * copy = (char *)malloc(request_len + 1);
    if (!copy) error("Out of memory");
    memcpy(copy, request, request_len);
    copy[request_len] = '\0';
    *owned_request = copy;

    char * line_end = strpbrk(copy, "\r\n");
    if (!line_end) return 0;
    *line_end = '\0';
    char * sp1 = strchr(copy, ' ');
    if (!sp1) return 0;
    *sp1++ = '\0';
    while (*sp1 == ' ') sp1++;
    char * sp2 = strchr(sp1, ' ');
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
        const char * scheme = (wasm_tcp_port == 443) ? "https" : "http";
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

    char * headers_start = line_end + 1;
    while (*headers_start == '\r' || *headers_start == '\n') headers_start++;

    char * headers_end = strstr(headers_start, "\r\n\r\n");
    size_t skip = 4;
    if (!headers_end) {
        headers_end = strstr(headers_start, "\n\n");
        skip = 2;
    }
    if (headers_end) {
        char * payload = headers_end + skip;
        *headers_end = '\0';
        if (payload < copy + request_len) {
            *body = payload;
            *body_len = (size_t)((copy + request_len) - payload);
        }
    }

    char * line = headers_start;
    while (*line && *header_count < WASM_FETCH_MAX_HEADERS) {
        while (*line == '\r' || *line == '\n') line++;
        if (!*line) break;
        char * next = strpbrk(line, "\r\n");
        if (next) *next++ = '\0';

        char * colon = strchr(line, ':');
        if (colon) {
            *colon++ = '\0';
            while (*colon == ' ' || *colon == '\t') colon++;
            char * end = colon + strlen(colon);
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

static void append_response(uint8_t * dst, int cap, int * total,
                            const void * src, size_t len) {
    if (!dst || cap <= 0 || !total || !src || len == 0) return;
    if (*total >= cap) return;
    size_t room = (size_t)(cap - *total);
    if (len > room) len = room;
    memcpy(dst + *total, src, len);
    *total += (int)len;
}

static void append_response_text(uint8_t * dst, int cap, int * total,
                                 const char * text) {
    append_response(dst, cap, total, text, strlen(text));
}

static void append_response_fmt(uint8_t * dst, int cap, int * total,
                                const char * fmt, int status,
                                const char * status_text) {
    char line[160];
    snprintf(line, sizeof(line), fmt, status, status_text ? status_text : "");
    append_response_text(dst, cap, total, line);
}

static int request_line_is_http_request(const uint8_t * request,
                                        size_t request_len,
                                        int port,
                                        int * is_https) {
    if (is_https) *is_https = 0;
    if (!request || request_len == 0 || request_len > 4096) return 0;

    char line[512];
    size_t n = 0;
    while (n < request_len && n + 1 < sizeof(line) &&
           request[n] != '\r' && request[n] != '\n') {
        line[n] = (char)request[n];
        n++;
    }
    line[n] = '\0';
    if (n == 0 || n + 1 == sizeof(line)) return 0;

    char * sp1 = strchr(line, ' ');
    if (!sp1) return 0;
    *sp1++ = '\0';
    while (*sp1 == ' ') sp1++;
    char * sp2 = strchr(sp1, ' ');
    if (!sp2) return 0;
    *sp2++ = '\0';
    while (*sp2 == ' ') sp2++;
    if (line[0] == '\0' || strncasecmp(sp2, "HTTP/", 5) != 0) return 0;
    if (is_https &&
        (strncasecmp(sp1, "https://", 8) == 0 || port == 443)) {
        *is_https = 1;
    }
    if (sp1[0] == '/' || strncasecmp(sp1, "http://", 7) == 0 ||
        strncasecmp(sp1, "https://", 8) == 0) {
        return 1;
    }
    return 0;
}

#ifdef __EMSCRIPTEN__
static int wasm_proxy_http_request_js(const char * host, int port,
                                      const uint8_t * request, int request_len,
                                      uint8_t * dst, int dst_cap,
                                      int timeout_ms) {
    return EM_ASM_INT({
        const host = UTF8ToString($0);
        const port = $1;
        const reqPtr = $2;
        const reqLen = $3;
        const dstPtr = $4;
        const dstCap = $5;
        const timeoutMs = $6;
        try {
            const url = new URL('/__picomite_proxy/http', self.location.href);
            url.searchParams.set('host', host);
            url.searchParams.set('port', String(port));
            url.searchParams.set('timeout_ms', String(timeoutMs));
            url.searchParams.set('max_bytes', String(dstCap));

            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            xhr.overrideMimeType('text/plain; charset=x-user-defined');
            const body = HEAPU8.slice(reqPtr, reqPtr + reqLen);
            xhr.send(body);
            if (xhr.status !== 200) {
                Module.__picomiteProxyHttpError =
                    xhr.responseText || `proxy_http_${xhr.status}`;
                return -2;
            }
            const text = xhr.responseText || "";
            const n = Math.min(text.length, dstCap);
            for (let i = 0; i < n; i++) {
                HEAPU8[dstPtr + i] = text.charCodeAt(i) & 0xff;
            }
            return n;
        } catch (e) {
            Module.__picomiteProxyHttpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, host, port, request, request_len, dst, dst_cap, timeout_ms);
}

static int wasm_proxy_tcp_open_js(const char * host, int port, int timeout_ms) {
    return EM_ASM_INT({
        const host = UTF8ToString($0);
        const port = $1;
        const timeoutMs = $2;
        try {
            const url = new URL('/__picomite_proxy/tcp/open', self.location.href);
            url.searchParams.set('host', host);
            url.searchParams.set('port', String(port));
            url.searchParams.set('timeout_ms', String(timeoutMs));

            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_open_${xhr.status}`;
                return -2;
            }
            const id = parseInt(xhr.responseText, 10);
            return Number.isFinite(id) && id > 0 ? id : -1;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, host, port, timeout_ms);
}

static int wasm_proxy_tcp_stream_js(int stream_id, const uint8_t * write_data,
                                    int write_len, uint8_t * dst, int dst_cap,
                                    int timeout_ms) {
    return EM_ASM_INT({
        const streamId = $0;
        const writePtr = $1;
        const writeLen = $2;
        const dstPtr = $3;
        const dstCap = $4;
        const timeoutMs = $5;
        try {
            const url = new URL('/__picomite_proxy/tcp/stream', self.location.href);
            url.searchParams.set('id', String(streamId));
            url.searchParams.set('timeout_ms', String(timeoutMs));
            url.searchParams.set('max_bytes', String(dstCap));

            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            xhr.overrideMimeType('text/plain; charset=x-user-defined');
            const body = writeLen > 0
                ? HEAPU8.slice(writePtr, writePtr + writeLen)
                : new Uint8Array(0);
            xhr.send(body);
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_stream_${xhr.status}`;
                return -2;
            }
            const text = xhr.responseText || "";
            const n = Math.min(text.length, dstCap);
            for (let i = 0; i < n; i++) {
                HEAPU8[dstPtr + i] = text.charCodeAt(i) & 0xff;
            }
            return n;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, stream_id, write_data, write_len, dst, dst_cap, timeout_ms);
}

static void wasm_proxy_tcp_close_js(int stream_id) {
    EM_ASM({
        const streamId = $0;
        if (streamId <= 0) return;
        try {
            const url = new URL('/__picomite_proxy/tcp/close', self.location.href);
            url.searchParams.set('id', String(streamId));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
        } catch (e) {} }, stream_id);
}

static int wasm_proxy_tcp_listen_js(int port, int backlog) {
    return EM_ASM_INT({
        const port = $0;
        const backlog = $1;
        try {
            const url = new URL('/__picomite_proxy/tcp/listen', self.location.href);
            url.searchParams.set('port', String(port));
            url.searchParams.set('backlog', String(backlog));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_listen_${xhr.status}`;
                return -2;
            }
            const id = parseInt(xhr.responseText, 10);
            return Number.isFinite(id) && id > 0 ? id : -1;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, port, backlog);
}

static void wasm_proxy_tcp_listener_close_js(int listener_id) {
    EM_ASM({
        const listenerId = $0;
        if (listenerId <= 0) return;
        try {
            const url = new URL('/__picomite_proxy/tcp/listener-close', self.location.href);
            url.searchParams.set('id', String(listenerId));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
        } catch (e) {} }, listener_id);
}

static int wasm_proxy_tcp_accept_conn_js(int listener_id) {
    return EM_ASM_INT({
        const listenerId = $0;
        try {
            const url = new URL('/__picomite_proxy/tcp/accept-conn', self.location.href);
            url.searchParams.set('id', String(listenerId));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
            if (xhr.status === 204) return 0;
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_accept_conn_${xhr.status}`;
                return -2;
            }
            const id = parseInt(xhr.responseText, 10);
            return Number.isFinite(id) && id > 0 ? id : -1;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, listener_id);
}

static int wasm_proxy_tcp_accept_js(int listener_id, uint8_t * dst, int dst_cap,
                                    int * out_conn_id) {
    return EM_ASM_INT({
        const listenerId = $0;
        const dstPtr = $1;
        const dstCap = $2;
        const connPtr = $3;
        HEAP32[connPtr >> 2] = 0;
        try {
            const url = new URL('/__picomite_proxy/tcp/accept', self.location.href);
            url.searchParams.set('id', String(listenerId));
            url.searchParams.set('max_bytes', String(dstCap));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.overrideMimeType('text/plain; charset=x-user-defined');
            xhr.send(new Uint8Array(0));
            if (xhr.status === 204) return 0;
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_accept_${xhr.status}`;
                return -2;
            }
            const connId = parseInt(xhr.getResponseHeader('X-PicoMite-Proxy-Conn') || '0', 10);
            if (!Number.isFinite(connId) || connId <= 0) return -1;
            HEAP32[connPtr >> 2] = connId;
            const text = xhr.responseText || "";
            const n = Math.min(text.length, dstCap);
            for (let i = 0; i < n; i++) {
                HEAPU8[dstPtr + i] = text.charCodeAt(i) & 0xff;
            }
            return n;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, listener_id, dst, dst_cap, out_conn_id);
}

static int wasm_proxy_tcp_conn_recv_js(int conn_id, uint8_t * dst,
                                       int dst_cap) {
    return EM_ASM_INT({
        const connId = $0;
        const dstPtr = $1;
        const dstCap = $2;
        try {
            const url = new URL('/__picomite_proxy/tcp/conn-recv', self.location.href);
            url.searchParams.set('id', String(connId));
            url.searchParams.set('max_bytes', String(dstCap));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.overrideMimeType('text/plain; charset=x-user-defined');
            xhr.send(new Uint8Array(0));
            if (xhr.status === 204) return 0;
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_conn_recv_${xhr.status}`;
                return -2;
            }
            const text = xhr.responseText || "";
            const n = Math.min(text.length, dstCap);
            for (let i = 0; i < n; i++) {
                HEAPU8[dstPtr + i] = text.charCodeAt(i) & 0xff;
            }
            return n;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, conn_id, dst, dst_cap);
}

static int wasm_proxy_tcp_conn_send_js(int conn_id, const uint8_t * data,
                                       int len, int timeout_ms) {
    return EM_ASM_INT({
        const connId = $0;
        const dataPtr = $1;
        const len = $2;
        const timeoutMs = $3;
        try {
            const url = new URL('/__picomite_proxy/tcp/conn-send', self.location.href);
            url.searchParams.set('id', String(connId));
            url.searchParams.set('timeout_ms', String(timeoutMs));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            const body = len > 0
                ? HEAPU8.slice(dataPtr, dataPtr + len)
                : new Uint8Array(0);
            xhr.send(body);
            if (xhr.status !== 200) {
                Module.__picomiteProxyTcpError =
                    xhr.responseText || `proxy_tcp_conn_send_${xhr.status}`;
                return -2;
            }
            return 0;
        } catch (e) {
            Module.__picomiteProxyTcpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, conn_id, data, len, timeout_ms);
}

static void wasm_proxy_tcp_conn_close_js(int conn_id) {
    EM_ASM({
        const connId = $0;
        if (connId <= 0) return;
        try {
            const url = new URL('/__picomite_proxy/tcp/conn-close', self.location.href);
            url.searchParams.set('id', String(connId));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
        } catch (e) {} }, conn_id);
}

static int wasm_proxy_udp_bind_js(int port) {
    return EM_ASM_INT({
        const port = $0;
        try {
            const url = new URL('/__picomite_proxy/udp/bind', self.location.href);
            url.searchParams.set('port', String(port));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
            if (xhr.status !== 200) {
                Module.__picomiteProxyUdpError =
                    xhr.responseText || `proxy_udp_bind_${xhr.status}`;
                return -2;
            }
            const id = parseInt(xhr.responseText, 10);
            return Number.isFinite(id) && id > 0 ? id : -1;
        } catch (e) {
            Module.__picomiteProxyUdpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, port);
}

static int wasm_proxy_udp_send_js(int socket_id, const char * host, int port,
                                  const uint8_t * data, int len,
                                  int timeout_ms) {
    return EM_ASM_INT({
        const socketId = $0;
        const host = UTF8ToString($1);
        const port = $2;
        const dataPtr = $3;
        const len = $4;
        const timeoutMs = $5;
        try {
            const url = new URL('/__picomite_proxy/udp/send', self.location.href);
            url.searchParams.set('id', String(socketId));
            url.searchParams.set('host', host);
            url.searchParams.set('port', String(port));
            url.searchParams.set('timeout_ms', String(timeoutMs));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            const body = len > 0
                ? HEAPU8.slice(dataPtr, dataPtr + len)
                : new Uint8Array(0);
            xhr.send(body);
            if (xhr.status !== 200) {
                Module.__picomiteProxyUdpError =
                    xhr.responseText || `proxy_udp_send_${xhr.status}`;
                return -2;
            }
            return 0;
        } catch (e) {
            Module.__picomiteProxyUdpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, socket_id, host, port, data, len, timeout_ms);
}

static int wasm_proxy_udp_recv_js(int socket_id, uint8_t * dst, int dst_cap,
                                  hal_net_addr_t * from) {
    return EM_ASM_INT({
        const socketId = $0;
        const dstPtr = $1;
        const dstCap = $2;
        const fromPtr = $3;
        function setU16(ptr, value) {
            HEAPU8[ptr] = value & 0xff;
            HEAPU8[ptr + 1] = (value >> 8) & 0xff;
        }
        function writeIPv4(text) {
            const parts = String(text || "").split(".").map((p) => parseInt(p, 10));
            if (parts.length !== 4 || parts.some((p) => !Number.isFinite(p) || p < 0 || p > 255)) {
                return false;
            }
            for (let i = 0; i < 4; i++) HEAPU8[fromPtr + i] = parts[i];
            HEAPU8[fromPtr + 16] = 4;
            return true;
        }
        try {
            const url = new URL('/__picomite_proxy/udp/recv', self.location.href);
            url.searchParams.set('id', String(socketId));
            url.searchParams.set('max_bytes', String(dstCap));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.overrideMimeType('text/plain; charset=x-user-defined');
            xhr.send(new Uint8Array(0));
            if (xhr.status === 204) return 0;
            if (xhr.status !== 200) {
                Module.__picomiteProxyUdpError =
                    xhr.responseText || `proxy_udp_recv_${xhr.status}`;
                return -2;
            }
            const text = xhr.responseText || "";
            const n = Math.min(text.length, dstCap);
            for (let i = 0; i < n; i++) {
                HEAPU8[dstPtr + i] = text.charCodeAt(i) & 0xff;
            }
            HEAPU8.fill(0, fromPtr, fromPtr + 20);
            const family = parseInt(xhr.getResponseHeader("X-PicoMite-Proxy-Family") || "0", 10);
            const port = parseInt(xhr.getResponseHeader("X-PicoMite-Proxy-Port") || "0", 10);
            const address = xhr.getResponseHeader("X-PicoMite-Proxy-Address") || "";
            if (family === 4) writeIPv4(address);
            setU16(fromPtr + 18, Number.isFinite(port) ? port : 0);
            return n;
        } catch (e) {
            Module.__picomiteProxyUdpError =
                e && e.message ? e.message : String(e);
            return -1;
        } }, socket_id, dst, dst_cap, from);
}

static void wasm_proxy_udp_close_js(int socket_id) {
    EM_ASM({
        const socketId = $0;
        if (socketId <= 0) return;
        try {
            const url = new URL('/__picomite_proxy/udp/close', self.location.href);
            url.searchParams.set('id', String(socketId));
            const xhr = new XMLHttpRequest();
            xhr.open('POST', url.toString(), false);
            xhr.send(new Uint8Array(0));
        } catch (e) {} }, socket_id);
}
#endif

static void wasm_tcp_client_open_cmd(unsigned char * tp) {
    mm_net_tcp_client_open_args_t parsed;
    mm_net_tcp_client_parse_open(tp, &parsed);

    close_tcpclient();
    snprintf(wasm_tcp_host, sizeof(wasm_tcp_host), "%s", parsed.host);
    wasm_tcp_port = parsed.port;
    wasm_tcp_client_opened = 1;
    if (!optionsuppressstatus) MMPrintString("Connected\r\n");
}

static void wasm_tcp_client_open_stream_cmd(unsigned char * tp) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_stream_capability)
        wasm_web_unsupported();

    mm_net_tcp_client_open_args_t parsed;
    mm_net_tcp_client_parse_open(tp, &parsed);

#ifdef __EMSCRIPTEN__
    close_tcpclient();
    int id = wasm_proxy_tcp_open_js(parsed.host, parsed.port,
                                    parsed.timeout_ms);
    if (id <= 0) error("No response from client");
    snprintf(wasm_tcp_host, sizeof(wasm_tcp_host), "%s", parsed.host);
    wasm_tcp_port = parsed.port;
    wasm_tcp_client_opened = 1;
    wasm_proxy_tcp_stream_id = id;
    if (!optionsuppressstatus) MMPrintString("Connected\r\n");
#else
    (void)parsed;
    wasm_web_unsupported();
#endif
}

static void wasm_tcp_client_request_cmd(unsigned char * tp) {
    if (!wasm_tcp_client_opened) error("No connection");
    if (wasm_tcp_client_stream_active || wasm_proxy_tcp_stream_id > 0)
        error("Connection busy");

    mm_net_tcp_client_request_args_t parsed;
    mm_net_tcp_client_parse_request(tp, &parsed);

#ifdef __EMSCRIPTEN__
    if (wasm_proxy_detected && wasm_proxy_http_capability) {
        int is_https = 0;
        if (!request_line_is_http_request(parsed.request, parsed.request_len,
                                          wasm_tcp_port, &is_https)) {
            error("Only HTTP requests are supported in browser proxy mode");
        }
        if (is_https) error("HTTPS/TLS not supported by browser proxy");
        int total = wasm_proxy_http_request_js(
            wasm_tcp_host, wasm_tcp_port, parsed.request,
            (int)parsed.request_len, parsed.buffer, parsed.payload_capacity,
            parsed.timeout_ms);
        if (total < 0) error("No response from proxy");
        parsed.dest[0] = total;
        return;
    }
#endif

    char method[32];
    char url[512];
    char * owned_request = NULL;
    char * body = NULL;
    size_t body_len = 0;
    const char * headers[WASM_FETCH_MAX_HEADERS * 2 + 1];
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

    emscripten_fetch_t * fetch = emscripten_fetch(&attr, url);
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
        char * headers = (char *)malloc(headers_len + 1);
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

static void wasm_tcp_client_stream_cmd(unsigned char * tp) {
    if (!wasm_tcp_client_opened) error("No connection");
    if (!wasm_proxy_detected || !wasm_proxy_tcp_stream_capability ||
        wasm_proxy_tcp_stream_id <= 0)
        wasm_web_unsupported();

    mm_net_tcp_client_stream_args_t parsed;
    mm_net_tcp_client_parse_stream(tp, &parsed);

    wasm_tcp_client_stream_active = 0;
    wasm_tcp_client_stream_buf = parsed.buffer;
    wasm_tcp_client_stream_size = parsed.payload_capacity;
    wasm_tcp_client_stream_read = parsed.read_pos;
    wasm_tcp_client_stream_write = parsed.write_pos;
    if (*parsed.read_pos < 0 || *parsed.read_pos >= parsed.payload_capacity)
        *parsed.read_pos = 0;
    if (*parsed.write_pos < 0 || *parsed.write_pos >= parsed.payload_capacity)
        *parsed.write_pos = *parsed.read_pos;

#ifdef __EMSCRIPTEN__
    int read_cap = parsed.payload_capacity;
    if (read_cap > 4096) read_cap = 4096;
    uint8_t * buf = (uint8_t *)malloc((size_t)read_cap);
    if (!buf) error("Out of memory");
    int got = wasm_proxy_tcp_stream_js(
        wasm_proxy_tcp_stream_id, parsed.request, (int)parsed.request_len,
        buf, read_cap, 5000);
    if (got < 0) {
        free(buf);
        error("write failed");
    }
    if (got > 0) {
        mm_net_tcp_client_stream_append(parsed.buffer, parsed.payload_capacity,
                                        parsed.read_pos, parsed.write_pos,
                                        buf, (size_t)got);
    }
    free(buf);
    wasm_tcp_client_stream_active = 1;
#else
    wasm_web_unsupported();
#endif
}

static int wasm_tcp_send_cb(void * ctx, const void * buf, size_t len) {
    int pcb = *(int *)ctx;
    wasm_tcp_service_init();
    if (!mm_net_tcp_service_conn(&wasm_tcp_server, pcb)) return 0;
    return mm_net_tcp_service_send(&wasm_tcp_server, pcb, buf, len, 5000) ==
           HAL_NET_OK;
}

static void wasm_tcp_close_slot(int pcb) {
    wasm_tcp_service_init();
    mm_net_tcp_service_close_slot(&wasm_tcp_server, pcb);
}

static void wasm_tcp_transmit_status(int pcb, int status) {
    char body[64];
    const char * reason = mm_net_http_status_reason(status);
    int body_len = mm_net_http_format_status_body(body, sizeof(body), status,
                                                  reason);
    if (body_len < 0) error("Transmit failed");
    if (mm_net_http_send_response(status, reason, "text/plain", body,
                                  (size_t)body_len, "MMBasic-WASM",
                                  wasm_tcp_send_cb, &pcb) != 0)
        error("Transmit failed");
    wasm_tcp_close_slot(pcb);
}

static void wasm_tcp_transmit_file(int pcb, const char * fname,
                                   const char * content_type) {
    if (mm_net_http_send_file(fname, content_type, "MMBasic-WASM",
                              wasm_tcp_send_cb, &pcb) != 0)
        error("File not found");
    wasm_tcp_close_slot(pcb);
}

static void wasm_tcp_transmit_page(int pcb, const char * fname, int extra) {
    char * rendered = NULL;
    size_t rendered_len = 0;
    if (mm_net_http_render_page(fname, extra, &rendered, &rendered_len) != 0)
        error("File not found");
    int rc = mm_net_http_send_response(200, NULL, "text/html", rendered,
                                       rendered_len, "MMBasic-WASM",
                                       wasm_tcp_send_cb, &pcb);
    FreeMemory((unsigned char *)rendered);
    if (rc != 0) error("Transmit failed");
    wasm_tcp_close_slot(pcb);
}

static int wasm_transmit_cmd(unsigned char * arg) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        wasm_web_unsupported();
    wasm_tcp_service_init();
    mm_net_transmit_args_t parsed;
    if (!mm_net_transmit_parse(arg, WASM_WEB_MAX_PCB, &parsed)) return 0;
    if (!mm_net_tcp_service_conn(&wasm_tcp_server, parsed.pcb))
        error("Not connected");
    switch (parsed.kind) {
    case MM_NET_TRANSMIT_CODE:
        wasm_tcp_transmit_status(parsed.pcb, parsed.status);
        return 1;
    case MM_NET_TRANSMIT_FILE:
    case MM_NET_TRANSMIT_CSS:
    case MM_NET_TRANSMIT_JS:
    case MM_NET_TRANSMIT_IMAGE:
        wasm_tcp_transmit_file(parsed.pcb, parsed.filename,
                               parsed.content_type);
        return 1;
    case MM_NET_TRANSMIT_PAGE:
        wasm_tcp_transmit_page(parsed.pcb, parsed.filename, parsed.extra);
        return 1;
    default:
        return 0;
    }
}

static int wasm_tcp_server_cmd(unsigned char * arg) {
    if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability)
        wasm_web_unsupported();
    wasm_tcp_service_init();
    unsigned char * tp;
    if ((tp = checkstring(arg, (unsigned char *)"INTERRUPT"))) {
        TCPreceiveInterrupt = mm_net_tcp_server_parse_interrupt(tp);
        InterruptUsed = true;
        TCPreceived = 0;
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"CLOSE"))) {
        mm_net_tcp_server_slot_args_t parsed;
        mm_net_tcp_server_parse_slot(tp, WASM_WEB_MAX_PCB, &parsed);
        wasm_tcp_close_slot(parsed.pcb);
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"READ"))) {
        mm_net_tcp_server_read_args_t parsed;
        mm_net_tcp_server_parse_read(tp, WASM_WEB_MAX_PCB, &parsed);
        ProcessWeb(0);
        int rc = mm_net_tcp_service_read(&wasm_tcp_server, parsed.pcb,
                                         parsed.dest, parsed.buffer,
                                         (size_t)parsed.payload_capacity,
                                         (size_t)parsed.array_bytes);
        if (rc == HAL_NET_WOULD_BLOCK) return 1;
        if (rc != HAL_NET_OK) error("array too small");
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"SEND"))) {
        mm_net_tcp_server_send_args_t parsed;
        mm_net_tcp_server_parse_send(tp, WASM_WEB_MAX_PCB, &parsed);
        if (!mm_net_tcp_service_conn(&wasm_tcp_server, parsed.pcb))
            error("Not connected");
        if (mm_net_tcp_service_send(&wasm_tcp_server, parsed.pcb,
                                    parsed.payload, parsed.payload_len,
                                    5000) != HAL_NET_OK)
            error("write failed");
        return 1;
    }
    return 0;
}

static int wasm_udp_cmd(unsigned char * arg) {
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability)
        wasm_web_unsupported();
    unsigned char * tp = checkstring(arg, (unsigned char *)"INTERRUPT");
    if (tp) {
        UDPinterrupt = mm_net_udp_parse_interrupt(tp);
        InterruptUsed = true;
        UDPreceive = false;
        return 1;
    }
    tp = checkstring(arg, (unsigned char *)"SEND");
    if (!tp) error("Syntax");
    mm_net_udp_send_args_t parsed;
    mm_net_udp_parse_send(tp, &parsed);
    if (hal_net_udp_send(parsed.host, (uint16_t)parsed.port, parsed.payload,
                         parsed.payload_len, 5000) != HAL_NET_OK)
        error("Failed to send UDP packet");
    return 1;
}

static void wasm_ntp_apply(uint32_t unix_seconds, MMFLOAT offset_hours) {
    extern int host_time_use_mmbasic_offset;
    time_t adjusted = (time_t)unix_seconds + (time_t)(offset_hours * 3600.0);
    struct tm utc_buf;
    hal_calendar_epoch_to_tm(adjusted, &utc_buf);
    struct tm * utc = &utc_buf;

    day_of_week = utc->tm_wday;
    if (day_of_week == 0) day_of_week = 7;
    TimeOffsetToUptime = (int64_t)adjusted -
                         (int64_t)(hal_time_us_64() / 1000000ULL);
    host_time_use_mmbasic_offset = 1;

    if (!optionsuppressstatus) {
        char buff[STRINGSIZE];
        snprintf(buff, sizeof buff,
                 "got ntp response: %02d/%02d/%04d %02d:%02d:%02d\r\n",
                 utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
                 utc->tm_hour, utc->tm_min, utc->tm_sec);
        MMPrintString(buff);
    }
}

static void wasm_ntp_cmd(unsigned char * arg) {
    if (!wasm_proxy_detected || !wasm_proxy_udp_capability ||
        !wasm_proxy_ntp_capability)
        wasm_web_unsupported();

    getargs(&arg, 5, (unsigned char *)",");
    if (!(argc == 0 || argc == 1 || argc == 3 || argc == 5)) error("Syntax");

    MMFLOAT offset = 0.0;
    const char * server = "pool.ntp.org";
    int timeout_ms = 5000;
    uint16_t port = 123;

    if (argc >= 1 && *argv[0]) {
        offset = getnumber(argv[0]);
        if (offset < -12.0 || offset > 14.0) error("Invalid Time Offset");
    }
    if (argc >= 3 && *argv[2]) server = (const char *)getCstring(argv[2]);
    if (argc == 5 && *argv[4]) timeout_ms = getint(argv[4], 0, 100000);

    char hostbuf[STRINGSIZE];
    strncpy(hostbuf, server, sizeof(hostbuf) - 1);
    hostbuf[sizeof(hostbuf) - 1] = 0;
    char * colon = strrchr(hostbuf, ':');
    if (colon && colon[1]) {
        int parsed_port = atoi(colon + 1);
        if (parsed_port > 0 && parsed_port <= 65535) {
            *colon = 0;
            port = (uint16_t)parsed_port;
        }
    }

    if (!optionsuppressstatus) {
        char buff[STRINGSIZE];
        snprintf(buff, sizeof buff, "ntp address %s\r\n", hostbuf);
        MMPrintString(buff);
    }

    uint32_t unix_seconds = 0;
    int rc = mm_net_ntp_query_unix_seconds(hostbuf, port,
                                           (uint32_t)timeout_ms,
                                           &unix_seconds);
    if (rc == HAL_NET_TIMEOUT) error("NTP timeout");
    if (rc != HAL_NET_OK) error("NTP request failed");
    wasm_ntp_apply(unix_seconds, offset);
}

static int wasm_mqtt_cmd(unsigned char * line) {
    static mm_net_mqtt_hal_context_t ctx;
    ctx.client = &wasm_mqtt_client;
    ctx.connected = &wasm_mqtt_connected;
    ctx.client_id = "PicoMiteWASM";
    ctx.ensure_net = NULL;
    ctx.after_subscribe = NULL;
    return mm_net_mqtt_hal_cmd(line, &ctx);
}

void cmd_web(void) {
    unsigned char * tp;
    if (wasm_mqtt_cmd(cmdline)) return;
    if ((tp = checkstring(cmdline, (unsigned char *)"OPEN TCP CLIENT"))) {
        wasm_tcp_client_open_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"OPEN TCP STREAM"))) {
        wasm_tcp_client_open_stream_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT REQUEST"))) {
        wasm_tcp_client_request_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TCP CLIENT STREAM"))) {
        if (!wasm_proxy_detected || !wasm_proxy_tcp_stream_capability)
            wasm_web_unsupported();
        wasm_tcp_client_stream_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"CLOSE TCP CLIENT"))) {
        if (*tp) error("Syntax");
        if (!wasm_tcp_client_opened) error("No connection");
        close_tcpclient();
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TRANSMIT"))) {
        if (wasm_transmit_cmd(tp)) return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TCP"))) {
        if (wasm_tcp_server_cmd(tp)) return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"NTP"))) {
        wasm_ntp_cmd(tp);
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"UDP"))) {
        wasm_udp_cmd(tp);
        return;
    }
    wasm_web_unsupported();
}

void port_repl_wifi_arch_init_and_connect(void) {
    static const mm_net_lifecycle_hooks_t hooks = {
        .open_tcp_server = wasm_tcp_server_open_port,
        .close_tcp_server = wasm_tcp_close_server,
        .open_udp_server = wasm_udp_server_open_port,
        .close_udp_server = wasm_udp_close_server,
        .open_tftp = wasm_tftp_open_server,
        .close_tftp = wasm_tftp_close_server,
        .open_telnet = wasm_telnet_open_server,
        .close_telnet = wasm_telnet_close_server,
    };
    if (wasm_proxy_detected) {
        WIFIconnected = 1;
        mm_net_lifecycle_on_network_ready(&hooks);
    }
}

void port_web_clear_runtime_state(void) {
    static const mm_net_lifecycle_runtime_hooks_t hooks = {
        .clear_tcp_requests = wasm_tcp_clear_requests,
        .close_tcp_client = close_tcpclient,
        .close_mqtt = closeMQTT,
        .close_tftp_session = wasm_tftp_close_session,
    };
    static const mm_net_lifecycle_hooks_t service_hooks = {
        .open_tcp_server = wasm_tcp_server_open_port,
        .close_tcp_server = wasm_tcp_close_server,
        .open_udp_server = wasm_udp_server_open_port,
        .close_udp_server = wasm_udp_close_server,
        .open_tftp = wasm_tftp_open_server,
        .close_tftp = wasm_tftp_close_server,
        .open_telnet = wasm_telnet_open_server,
        .close_telnet = wasm_telnet_close_server,
    };
    mm_net_lifecycle_runtime_reset(&hooks);
    if (wasm_proxy_detected)
        mm_net_lifecycle_on_network_ready(&service_hooks);
}

void port_web_print_options(void) {
}

int port_web_option_setter(unsigned char * cmdline) {
    if (checkstring(cmdline, (unsigned char *)"TELNET CONSOLE")) {
        if (!wasm_proxy_detected || !wasm_proxy_tcp_server_capability ||
            !wasm_proxy_telnet_capability)
            wasm_web_unsupported();
    }
    if (checkstring(cmdline, (unsigned char *)"TFTP") &&
        (!wasm_proxy_detected || !wasm_proxy_udp_capability ||
         !wasm_proxy_tftp_capability)) {
        wasm_web_unsupported();
    }
    static const mm_net_lifecycle_hooks_t hooks = {
        .open_tcp_server = wasm_tcp_server_open_port,
        .close_tcp_server = wasm_tcp_close_server,
        .open_udp_server = wasm_udp_server_open_port,
        .close_udp_server = wasm_udp_close_server,
        .open_tftp = wasm_tftp_open_server,
        .close_tftp = wasm_tftp_close_server,
        .open_telnet = wasm_telnet_open_server,
        .close_telnet = wasm_telnet_close_server,
    };
    return mm_net_lifecycle_handle_option_result(
        mm_net_lifecycle_option_setter(cmdline, &hooks),
        &wasm_lifecycle_result_handler);
}

static int wasm_mminfo_string(unsigned char * out_sret, int * out_targ,
                              const char * value) {
    strcpy((char *)out_sret, value);
    CtoM(out_sret);
    *out_targ = T_STR;
    return 1;
}

static int wasm_info_empty_ip(char * out, size_t out_len) {
    if (out_len) out[0] = 0;
    return HAL_NET_OK;
}

static int wasm_info_unsupported_status(void) {
    return HAL_NET_UNSUPPORTED;
}

static int wasm_info_tcpip_status(void) {
    return wasm_proxy_detected ? 1 : HAL_NET_UNSUPPORTED;
}

int port_web_mminfo(unsigned char * ep, int64_t * out_iret,
                    unsigned char * out_sret, int * out_targ) {
    if (checkstring(ep, (unsigned char *)"SSID"))
        return wasm_mminfo_string(out_sret, out_targ, "");
    const mm_net_info_hooks_t hooks = {
        .before_query = wasm_info_before_query,
        .tcp_path = wasm_info_tcp_path,
        .tcp_request_pending = wasm_info_tcp_request_pending,
        .max_connections = wasm_proxy_tcp_server_capability ? WASM_WEB_MAX_PCB : 0,
        .tcp_port = Option.TCP_PORT,
        .udp_port = Option.UDP_PORT,
        .ip_address = wasm_info_empty_ip,
        .wifi_status = wasm_info_unsupported_status,
        .tcpip_status = wasm_info_tcpip_status,
    };
    return mm_net_mminfo(ep, out_iret, out_sret, out_targ, &hooks);
}

int port_web_get_ssid(unsigned char * out_sret, int * out_targ) {
    return wasm_mminfo_string(out_sret, out_targ, "");
}

int wifi_serial_telnet_configured(void) {
    return 1;
}
