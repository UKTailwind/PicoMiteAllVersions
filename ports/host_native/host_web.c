/*
 * ports/host_native/host_web.c - host BASIC WEB command surface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_filesystem.h"
#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "Memory.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_http_page.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_mqtt_cmd.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_ntp_hal.h"
#include "shared/net/mm_net_options.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_state.h"
#include "shared/net/mm_net_tcp_client_cmd.h"
#include "shared/net/mm_net_tcp_server_cmd.h"
#include "shared/net/mm_net_tftp.h"
#include "shared/net/mm_net_transmit_cmd.h"
#include "shared/net/mm_net_udp_cmd.h"
#include "shared/net/mm_net_web_cmd.h"

#define HOST_WEB_MAX_PCB 4
#define HOST_WEB_RECV_CAP 2048

volatile int WIFIconnected = 1;
bool optionsuppressstatus = false;

void ProcessWeb(int mode);
void port_web_clear_runtime_state(void);

static hal_net_tcp_client_t host_tcp_client;
static int host_tcp_client_open;
static int host_tcp_client_stream_active;
static uint8_t *host_tcp_client_stream_buf;
static int host_tcp_client_stream_size;
static int64_t *host_tcp_client_stream_read;
static int64_t *host_tcp_client_stream_write;
static mm_net_tcp_service_t host_tcp_server;
static mm_net_udp_service_t host_udp_server;
static hal_net_udp_socket_t host_tftp_server;
static int host_tftp_server_opened;
static hal_net_tcp_server_t host_telnet_server;
static hal_net_tcp_conn_t host_telnet_conn;
static int host_telnet_open_failed;
static hal_net_mqtt_client_t host_mqtt_client;
static int host_mqtt_connected;

static mm_net_tcp_service_slot_t host_tcp_slots[HOST_WEB_MAX_PCB];
static uint8_t host_tcp_recv_buf[HOST_WEB_MAX_PCB][HOST_WEB_RECV_CAP];
static char host_tcp_path[HOST_WEB_MAX_PCB][256];

static mm_net_tftp_session_t host_tftp;
static char host_telnet_buf[256];
static int host_telnet_pos;
static int host_telnet_lastchar = -1;
static const uint8_t host_telnet_init_options[] = {
    255, 251, 3, 255, 253, 3, 255, 251, 1, 255, 253, 34, 255, 254, 34, 0,
};

int host_tcp_interrupt_pending(void) {
    return mm_net_tcp_service_interrupt_pending(&host_tcp_server);
}

static void host_tcp_service_init(void) {
    static int inited;
    if (inited) return;
    for (int i = 0; i < HOST_WEB_MAX_PCB; ++i) {
        mm_net_tcp_service_slot_init(&host_tcp_slots[i],
                                     host_tcp_recv_buf[i],
                                     sizeof(host_tcp_recv_buf[i]),
                                     host_tcp_path[i],
                                     sizeof(host_tcp_path[i]));
    }
    mm_net_tcp_service_init(&host_tcp_server, host_tcp_slots,
                            HOST_WEB_MAX_PCB);
    inited = 1;
}

static void host_web_ensure_net(void) {
    static int ready;
    host_tcp_service_init();
    if (!ready) {
        if (hal_net_init() != HAL_NET_OK) error("Network init failed");
        ready = 1;
    }
}

void close_tcpclient(void) {
    if (host_tcp_client_open) {
        hal_net_tcp_client_close(host_tcp_client);
        host_tcp_client = 0;
        host_tcp_client_open = 0;
    }
    host_tcp_client_stream_active = 0;
    host_tcp_client_stream_buf = NULL;
    host_tcp_client_stream_size = 0;
    host_tcp_client_stream_read = NULL;
    host_tcp_client_stream_write = NULL;
}

void closeMQTT(void) {
    if (host_mqtt_connected) {
        hal_net_mqtt_close(host_mqtt_client);
        host_mqtt_client = 0;
        host_mqtt_connected = 0;
    }
}

static void host_tcp_close_slot(int pcb) {
    host_tcp_service_init();
    mm_net_tcp_service_close_slot(&host_tcp_server, pcb);
}

void cleanserver(void) {
    port_web_clear_runtime_state();
}

static void host_tcp_server_open_port(uint16_t port) {
    host_web_ensure_net();
    if (port == 0) {
        mm_net_tcp_service_stop(&host_tcp_server);
        return;
    }
    if (!mm_net_tcp_service_open(&host_tcp_server, port, HOST_WEB_MAX_PCB))
        error("Failed to create TCP server");
}

static int host_lifecycle_open_tcp(uint16_t port) {
    host_tcp_server_open_port(port);
    return 1;
}

static void host_lifecycle_close_tcp(void) {
    host_tcp_service_init();
    mm_net_tcp_service_stop(&host_tcp_server);
}

static void host_lifecycle_clear_tcp_requests(void) {
    mm_net_tcp_service_clear_requests(&host_tcp_server);
}

static const char *host_info_tcp_path(int slot) {
    return mm_net_tcp_service_path(&host_tcp_server, slot);
}

static int host_info_tcp_request_pending(int slot) {
    return mm_net_tcp_service_request_pending(&host_tcp_server, slot);
}

static void host_info_before_query(void) {
    ProcessWeb(0);
}

static int host_info_ip_address(char *out, size_t out_len) {
    host_web_ensure_net();
    if (hal_net_ip_address(out, out_len) != HAL_NET_OK) {
        if (out_len) out[0] = 0;
        return HAL_NET_ERR;
    }
    return HAL_NET_OK;
}

static int host_info_wifi_status(void) {
    return HAL_NET_UNSUPPORTED;
}

static int host_info_tcpip_status(void) {
    return 1;
}

static void host_udp_close_server(void) {
    mm_net_udp_service_stop(&host_udp_server);
}

static void host_udp_server_open_port(uint16_t port) {
    host_web_ensure_net();
    host_udp_close_server();
    if (port == 0) return;
    if (!mm_net_udp_service_open(&host_udp_server, port))
        error("Failed to create UDP server");
}

static int host_lifecycle_open_udp(uint16_t port) {
    host_udp_server_open_port(port);
    return 1;
}

static uint16_t host_tftp_port(void) {
    const char *env = getenv("MMBASIC_HOST_TFTP_PORT");
    if (!env || !*env) env = getenv("HOST_TFTP_PORT");
    if (env && *env) {
        char *end = NULL;
        long port = strtol(env, &end, 10);
        if (end && *end == 0 && port > 0 && port <= 65535)
            return (uint16_t)port;
    }
    return 69;
}

static int host_tftp_has_port_override(void) {
    const char *env = getenv("MMBASIC_HOST_TFTP_PORT");
    if (!env || !*env) env = getenv("HOST_TFTP_PORT");
    return env && *env;
}

static uint16_t host_telnet_port(void) {
    const char *env = getenv("MMBASIC_HOST_TELNET_PORT");
    if (!env || !*env) env = getenv("HOST_TELNET_PORT");
    if (env && *env) {
        char *end = NULL;
        long port = strtol(env, &end, 10);
        if (end && *end == 0 && port > 0 && port <= 65535)
            return (uint16_t)port;
    }
    return 23;
}

static int host_telnet_has_port_override(void) {
    const char *env = getenv("MMBASIC_HOST_TELNET_PORT");
    if (!env || !*env) env = getenv("HOST_TELNET_PORT");
    return env && *env;
}

static void host_tftp_ensure_init(void);

static void host_tftp_close_session(void) {
    mm_net_tftp_close(&host_tftp);
}

static void host_tftp_close_server(void) {
    host_tftp_close_session();
    if (host_tftp_server_opened) {
        hal_net_udp_close(host_tftp_server);
        host_tftp_server = 0;
        host_tftp_server_opened = 0;
    }
}

static void host_tftp_open_server(void) {
    host_web_ensure_net();
    host_tftp_ensure_init();
    if (Option.disabletftp) {
        host_tftp_close_server();
        return;
    }
    if (host_tftp_server_opened) return;
    if (hal_net_udp_bind(host_tftp_port(), &host_tftp_server) != HAL_NET_OK)
        error("Failed to create TFTP server");
    host_tftp_server_opened = 1;
}

static int host_lifecycle_open_tftp(void) {
    if (!host_tftp_has_port_override()) return 1;
    host_tftp_open_server();
    return 1;
}

static int host_tftp_peer_text(const mm_net_tftp_peer_t *peer, char *out,
                               size_t out_len) {
    if (peer->family == 4) {
        return inet_ntop(AF_INET, peer->bytes, out, out_len) != NULL;
    }
    if (peer->family == 6) {
        return inet_ntop(AF_INET6, peer->bytes, out, out_len) != NULL;
    }
    return 0;
}

static int host_tftp_send(void *ctx, const mm_net_tftp_peer_t *peer,
                          const void *buf, size_t len) {
    (void)ctx;
    char host[INET6_ADDRSTRLEN];
    if (!host_tftp_peer_text(peer, host, sizeof(host))) return 0;
    return hal_net_udp_socket_send(host_tftp_server, host, peer->port, buf,
                                   len, 1000) == HAL_NET_OK;
}

static int host_tftp_open_file(void *ctx, const char *filename, int write,
                               void **handle) {
    (void)ctx;
    hal_fs_fd_t fd;
    int flags = write ? (HAL_FS_O_WRONLY | HAL_FS_O_CREAT | HAL_FS_O_TRUNC)
                      : HAL_FS_O_RDONLY;
    if (hal_fs_open(filename, flags, &fd) < 0) return -1;
    *handle = (void *)(intptr_t)fd;
    return 0;
}

static ssize_t host_tftp_read_file(void *ctx, void *handle, void *buf,
                                   size_t len) {
    (void)ctx;
    return hal_fs_read((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static ssize_t host_tftp_write_file(void *ctx, void *handle, const void *buf,
                                    size_t len) {
    (void)ctx;
    return hal_fs_write((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static void host_tftp_close_file(void *ctx, void *handle) {
    (void)ctx;
    hal_fs_close((hal_fs_fd_t)(intptr_t)handle);
}

static const mm_net_tftp_ops_t host_tftp_ops = {
    .open = host_tftp_open_file,
    .read = host_tftp_read_file,
    .write = host_tftp_write_file,
    .close = host_tftp_close_file,
    .send = host_tftp_send,
};

static void host_tftp_ensure_init(void) {
    if (!host_tftp.ops) mm_net_tftp_init(&host_tftp, &host_tftp_ops, NULL);
}

static void host_tftp_poll(void) {
    uint8_t pkt[600];
    hal_net_addr_t from;
    mm_net_tftp_peer_t peer;
    size_t len = 0;
    if (!host_tftp_server_opened) return;
    host_tftp_ensure_init();
    for (;;) {
        int rc = hal_net_udp_recv_event(host_tftp_server, &from, pkt,
                                        sizeof(pkt), &len);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || len < 2) return;
        memset(&peer, 0, sizeof(peer));
        peer.family = from.family;
        peer.port = from.port;
        memcpy(peer.bytes, from.bytes, sizeof(peer.bytes));
        mm_net_tftp_handle_packet(&host_tftp, &peer, pkt, len);
    }
}

static void host_telnet_close_conn(void) {
    if (host_telnet_conn) {
        hal_net_tcp_conn_close(host_telnet_conn);
        host_telnet_conn = 0;
    }
    host_telnet_pos = 0;
}

static void host_telnet_close_server(void) {
    host_telnet_close_conn();
    if (host_telnet_server) {
        hal_net_tcp_server_close(host_telnet_server);
        host_telnet_server = 0;
    }
    host_telnet_open_failed = 0;
}

static int host_telnet_open_server(void) {
    if (!Option.Telnet) {
        host_telnet_close_server();
        return 1;
    }
    if (host_telnet_server) return 1;
    if (host_telnet_open_failed) return 0;
    host_web_ensure_net();
    if (hal_net_tcp_server_open(host_telnet_port(), 1,
                                &host_telnet_server) == HAL_NET_OK)
        return 1;
    host_telnet_open_failed = 1;
    return 0;
}

static int host_lifecycle_open_telnet(void) {
    if (!host_telnet_has_port_override()) return 1;
    return host_telnet_open_server();
}

static const mm_net_lifecycle_hooks_t host_lifecycle_hooks = {
    .open_tcp_server = host_lifecycle_open_tcp,
    .close_tcp_server = host_lifecycle_close_tcp,
    .open_udp_server = host_lifecycle_open_udp,
    .close_udp_server = host_udp_close_server,
    .open_tftp = host_lifecycle_open_tftp,
    .close_tftp = host_tftp_close_server,
    .open_telnet = host_lifecycle_open_telnet,
    .close_telnet = host_telnet_close_server,
};

void host_telnet_putc(int c, int flush) {
    if (!host_telnet_conn) return;
    if (flush != -1) {
        host_telnet_buf[host_telnet_pos++] = (char)c;
        if (c == 255) host_telnet_buf[host_telnet_pos++] = (char)c;
        if (c == 13) host_telnet_buf[host_telnet_pos++] = 0;
    }
    if (host_telnet_pos >= (int)sizeof(host_telnet_buf) - 4 ||
        (flush == -1 && host_telnet_pos)) {
        if (hal_net_tcp_conn_send(host_telnet_conn, host_telnet_buf,
                                  (size_t)host_telnet_pos, 5000) !=
            HAL_NET_OK) {
            host_telnet_close_conn();
        }
        host_telnet_pos = 0;
    }
}

static void host_telnet_receive_bytes(const uint8_t *data, size_t len) {
    if (!data || len == 0) return;
    if (data[0] == 255) return;
    for (size_t i = 0; i < len; ++i) {
        ConsoleRxBuf[ConsoleRxBufHead] = (char)data[i];
        if ((host_telnet_lastchar == 13 &&
             ConsoleRxBuf[ConsoleRxBufHead] == 0) ||
            (host_telnet_lastchar == 255 &&
             ConsoleRxBuf[ConsoleRxBufHead] == (char)255)) {
            host_telnet_lastchar = -1;
            continue;
        }
        if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {
            MMAbort = true;
            ConsoleRxBufHead = ConsoleRxBufTail;
        } else if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect &&
                   KeyInterrupt != NULL) {
            Keycomplete = true;
        } else {
            host_telnet_lastchar = ConsoleRxBuf[ConsoleRxBufHead];
            ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
            if (ConsoleRxBufHead == ConsoleRxBufTail)
                ConsoleRxBufTail = (ConsoleRxBufTail + 1) %
                                   CONSOLE_RX_BUF_SIZE;
        }
    }
}

static void host_telnet_poll(int mode) {
    static uint64_t flushtimer;
    if (!Option.Telnet) {
        host_telnet_close_server();
        return;
    }
    if (!host_telnet_server && !host_telnet_open_server()) return;
    if (!host_telnet_conn) {
        hal_net_tcp_conn_t conn = 0;
        int rc = hal_net_tcp_accept_conn(host_telnet_server, &conn);
        if (rc == HAL_NET_OK) {
            host_telnet_conn = conn;
            if (hal_net_tcp_conn_send(host_telnet_conn,
                                      host_telnet_init_options,
                                      sizeof(host_telnet_init_options),
                                      5000) != HAL_NET_OK) {
                host_telnet_close_conn();
                return;
            }
        } else if (rc != HAL_NET_WOULD_BLOCK) {
            host_telnet_close_server();
            return;
        }
    }
    if (!host_telnet_conn) return;
    uint8_t buf[128];
    for (;;) {
        size_t len = 0;
        int rc = hal_net_tcp_conn_recv(host_telnet_conn, buf, sizeof(buf),
                                       &len);
        if (rc == HAL_NET_OK) {
            host_telnet_receive_bytes(buf, len);
            continue;
        }
        if (rc != HAL_NET_WOULD_BLOCK) host_telnet_close_conn();
        break;
    }
    if (mode && host_telnet_conn && hal_time_us_64() > flushtimer) {
        host_telnet_putc(0, -1);
        flushtimer = hal_time_us_64() + 5000;
    }
}

static void host_udp_poll(void) {
    mm_net_udp_service_poll(&host_udp_server);
}

static void host_tcp_client_stream_poll(void) {
    if (!host_tcp_client_open || !host_tcp_client_stream_active ||
        !host_tcp_client_stream_buf || !host_tcp_client_stream_read ||
        !host_tcp_client_stream_write || host_tcp_client_stream_size <= 1)
        return;

    uint8_t buf[256];
    for (;;) {
        size_t got = 0;
        int rc = hal_net_tcp_client_recv(host_tcp_client, buf, sizeof(buf),
                                         &got, 1);
        if (rc == HAL_NET_TIMEOUT || rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || got == 0) return;

        mm_net_tcp_client_stream_append(host_tcp_client_stream_buf,
                                        host_tcp_client_stream_size,
                                        host_tcp_client_stream_read,
                                        host_tcp_client_stream_write,
                                        buf, got);
    }
}

static void host_mqtt_poll(void) {
    if (!host_mqtt_connected) return;
    for (;;) {
        char topic[MAXSTRLEN + 1];
        uint8_t payload[MAXSTRLEN + 1];
        size_t payload_len = 0;
        int rc = hal_net_mqtt_recv_event(host_mqtt_client, topic,
                                         sizeof(topic), payload, MAXSTRLEN,
                                         &payload_len);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK) return;
        mm_net_state_set_mstring(MM_NET_STATE_TOPIC, topic, strlen(topic));
        mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, payload, payload_len);
        MQTTComplete = true;
    }
}

static void host_tcp_server_poll(void) {
    mm_net_tcp_service_poll(&host_tcp_server);
}

void ProcessWeb(int mode) {
    static const mm_net_lifecycle_poll_hooks_t hooks = {
        .poll_udp = host_udp_poll,
        .poll_tftp = host_tftp_poll,
        .poll_tcp_client_stream = host_tcp_client_stream_poll,
        .poll_mqtt = host_mqtt_poll,
        .poll_tcp_server = host_tcp_server_poll,
        .poll_telnet = host_telnet_poll,
    };
    mm_net_lifecycle_poll(&hooks, mode, 1);
}

static void host_tcp_client_open_cmd(unsigned char *arg) {
    mm_net_tcp_client_open_args_t parsed;
    mm_net_tcp_client_parse_open(arg, &parsed);

    host_web_ensure_net();
    close_tcpclient();
    if (hal_net_tcp_client_open(parsed.host, (uint16_t)parsed.port,
                                (uint32_t)parsed.timeout_ms,
                                &host_tcp_client) != HAL_NET_OK)
        error("No response from client");

    host_tcp_client_open = 1;
    if (!optionsuppressstatus) MMPrintString("Connected\r\n");
}

static void host_tcp_client_request_cmd(unsigned char *arg) {
    if (!host_tcp_client_open) error("No connection");
    if (host_tcp_client_stream_active) error("Connection busy");

    mm_net_tcp_client_request_args_t parsed;
    mm_net_tcp_client_parse_request(arg, &parsed);

    if (parsed.request_len &&
        hal_net_tcp_client_send(host_tcp_client, parsed.request,
                                parsed.request_len,
                                (uint32_t)parsed.timeout_ms) != HAL_NET_OK)
        error("write failed");

    int total = 0;
    uint32_t wait_ms = (uint32_t)parsed.timeout_ms;
    while (total < parsed.payload_capacity) {
        size_t got = 0;
        int rc = hal_net_tcp_client_recv(
            host_tcp_client, parsed.buffer + total,
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

static void host_tcp_client_stream_cmd(unsigned char *arg) {
    if (!host_tcp_client_open) error("No connection");

    mm_net_tcp_client_stream_args_t parsed;
    mm_net_tcp_client_parse_stream(arg, &parsed);

    host_tcp_client_stream_active = 0;
    host_tcp_client_stream_buf = parsed.buffer;
    host_tcp_client_stream_size = parsed.payload_capacity;
    host_tcp_client_stream_read = parsed.read_pos;
    host_tcp_client_stream_write = parsed.write_pos;
    if (*parsed.read_pos < 0 || *parsed.read_pos >= parsed.payload_capacity)
        *parsed.read_pos = 0;
    if (*parsed.write_pos < 0 || *parsed.write_pos >= parsed.payload_capacity)
        *parsed.write_pos = *parsed.read_pos;

    if (parsed.request_len &&
        hal_net_tcp_client_send(host_tcp_client, parsed.request,
                                parsed.request_len, 5000) != HAL_NET_OK)
        error("write failed");
    host_tcp_client_stream_active = 1;
}

static int host_tcp_client_cmd(unsigned char *line) {
    unsigned char *tp;
    if ((tp = checkstring(line, (unsigned char *)"OPEN TCP CLIENT"))) {
        host_tcp_client_open_cmd(tp);
        return 1;
    }
    if ((tp = checkstring(line, (unsigned char *)"OPEN TCP STREAM"))) {
        host_tcp_client_open_cmd(tp);
        return 1;
    }
    if ((tp = checkstring(line, (unsigned char *)"TCP CLIENT REQUEST"))) {
        host_tcp_client_request_cmd(tp);
        return 1;
    }
    if ((tp = checkstring(line, (unsigned char *)"TCP CLIENT STREAM"))) {
        host_tcp_client_stream_cmd(tp);
        return 1;
    }
    if ((tp = checkstring(line, (unsigned char *)"CLOSE TCP CLIENT"))) {
        if (*tp) error("Syntax");
        if (!host_tcp_client_open) error("No connection");
        close_tcpclient();
        return 1;
    }
    return 0;
}

static int host_tcp_send_cb(void *ctx, const void *buf, size_t len) {
    int pcb = *(int *)ctx;
    if (!mm_net_tcp_service_conn(&host_tcp_server, pcb))
        return 0;
    return mm_net_tcp_service_send(&host_tcp_server, pcb, buf, len, 5000) ==
           HAL_NET_OK;
}

static void host_tcp_transmit_status(int pcb, int status) {
    char body[64];
    const char *reason = mm_net_http_status_reason(status);
    int body_len = mm_net_http_format_status_body(body, sizeof(body), status,
                                                  reason);
    if (body_len < 0) error("Transmit failed");
    if (mm_net_http_send_response(status, reason, "text/plain", body,
                                  (size_t)body_len, "MMBasic-Host",
                                  host_tcp_send_cb, &pcb) != 0)
        error("Transmit failed");
    host_tcp_close_slot(pcb);
}

static void host_tcp_transmit_file(int pcb, const char *fname,
                                   const char *content_type) {
    if (mm_net_http_send_file(fname, content_type, "MMBasic-Host",
                              host_tcp_send_cb, &pcb) != 0)
        error("File not found");
    host_tcp_close_slot(pcb);
}

static void host_tcp_transmit_page(int pcb, const char *fname, int extra) {
    char *rendered = NULL;
    size_t rendered_len = 0;
    if (mm_net_http_render_page(fname, extra, &rendered, &rendered_len) != 0)
        error("File not found");
    int rc = mm_net_http_send_response(200, NULL, "text/html", rendered,
                                       rendered_len, "MMBasic-Host",
                                       host_tcp_send_cb, &pcb);
    FreeMemory((unsigned char *)rendered);
    if (rc != 0) error("Transmit failed");
    host_tcp_close_slot(pcb);
}

static int host_transmit_cmd(unsigned char *arg) {
    mm_net_transmit_args_t parsed;
    if (!mm_net_transmit_parse(arg, HOST_WEB_MAX_PCB, &parsed)) return 0;
    if (!mm_net_tcp_service_conn(&host_tcp_server, parsed.pcb))
        error("Not connected");
    switch (parsed.kind) {
        case MM_NET_TRANSMIT_CODE:
            host_tcp_transmit_status(parsed.pcb, parsed.status);
            return 1;
        case MM_NET_TRANSMIT_FILE:
        case MM_NET_TRANSMIT_CSS:
        case MM_NET_TRANSMIT_JS:
        case MM_NET_TRANSMIT_IMAGE:
            host_tcp_transmit_file(parsed.pcb, parsed.filename,
                                   parsed.content_type);
            return 1;
        case MM_NET_TRANSMIT_PAGE:
            host_tcp_transmit_page(parsed.pcb, parsed.filename, parsed.extra);
            return 1;
        default:
            return 0;
    }
}

static int host_tcp_server_cmd(unsigned char *arg) {
    unsigned char *tp;
    if ((tp = checkstring(arg, (unsigned char *)"INTERRUPT"))) {
        TCPreceiveInterrupt = mm_net_tcp_server_parse_interrupt(tp);
        InterruptUsed = true;
        TCPreceived = 0;
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"CLOSE"))) {
        mm_net_tcp_server_slot_args_t parsed;
        mm_net_tcp_server_parse_slot(tp, HOST_WEB_MAX_PCB, &parsed);
        host_tcp_close_slot(parsed.pcb);
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"READ"))) {
        mm_net_tcp_server_read_args_t parsed;
        mm_net_tcp_server_parse_read(tp, HOST_WEB_MAX_PCB, &parsed);
        ProcessWeb(0);
        int rc = mm_net_tcp_service_read(&host_tcp_server, parsed.pcb,
                                         parsed.dest, parsed.buffer,
                                         (size_t)parsed.payload_capacity,
                                         (size_t)parsed.array_bytes);
        if (rc == HAL_NET_WOULD_BLOCK) return 1;
        if (rc != HAL_NET_OK)
            error("array too small");
        return 1;
    }
    if ((tp = checkstring(arg, (unsigned char *)"SEND"))) {
        mm_net_tcp_server_send_args_t parsed;
        mm_net_tcp_server_parse_send(tp, HOST_WEB_MAX_PCB, &parsed);
        if (!mm_net_tcp_service_conn(&host_tcp_server, parsed.pcb))
            error("Not connected");
        if (mm_net_tcp_service_send(&host_tcp_server, parsed.pcb,
                                    parsed.payload, parsed.payload_len,
                                    5000) != HAL_NET_OK)
            error("write failed");
        return 1;
    }
    return 0;
}

static int host_udp_cmd(unsigned char *arg) {
    unsigned char *tp = checkstring(arg, (unsigned char *)"INTERRUPT");
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
    host_web_ensure_net();
    if (hal_net_udp_send(parsed.host, (uint16_t)parsed.port, parsed.payload,
                         parsed.payload_len, 5000) != HAL_NET_OK)
        error("Failed to send UDP packet");
    return 1;
}

static void host_ntp_apply(uint32_t unix_seconds, MMFLOAT offset_hours) {
    extern int host_time_use_mmbasic_offset;
    time_t adjusted = (time_t)unix_seconds + (time_t)(offset_hours * 3600.0);
    struct tm *utc = gmtime(&adjusted);
    if (!utc) error("invalid ntp response");

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

static void host_ntp_cmd(unsigned char *arg) {
    getargs(&arg, 5, (unsigned char *)",");
    if (!(argc == 0 || argc == 1 || argc == 3 || argc == 5)) error("Syntax");

    MMFLOAT offset = 0.0;
    const char *server = "pool.ntp.org";
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
    char *colon = strrchr(hostbuf, ':');
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

    host_web_ensure_net();
    uint32_t unix_seconds = 0;
    int rc = mm_net_ntp_query_unix_seconds(hostbuf, port,
                                           (uint32_t)timeout_ms,
                                           &unix_seconds);
    if (rc == HAL_NET_TIMEOUT) error("NTP timeout");
    if (rc != HAL_NET_OK) error("NTP request failed");
    host_ntp_apply(unix_seconds, offset);
}

static void host_mqtt_after_subscribe(void) {
    ProcessWeb(0);
}

static int host_mqtt_cmd(unsigned char *line) {
    const mm_net_mqtt_hal_context_t ctx = {
        .client = &host_mqtt_client,
        .connected = &host_mqtt_connected,
        .client_id = "WebMiteHost",
        .ensure_net = host_web_ensure_net,
        .after_subscribe = host_mqtt_after_subscribe,
    };
    return mm_net_mqtt_hal_cmd(line, &ctx);
}

static void host_web_connect_cmd(unsigned char *arg) {
    if (*arg) error("WiFi not supported on host");
    host_web_ensure_net();
    if (mm_net_lifecycle_on_network_ready(&host_lifecycle_hooks) !=
        MM_NET_LIFECYCLE_OK)
        error("Failed to create network service");
}

static void host_web_scan_cmd(unsigned char *arg) {
    (void)arg;
    error("WiFi scan not supported on host");
}

static int host_web_is_connected(void) {
    return 1;
}

void cmd_web(void) {
    static const mm_net_web_dispatch_t dispatch = {
        .connect = host_web_connect_cmd,
        .scan = host_web_scan_cmd,
        .is_connected = host_web_is_connected,
        .not_connected_error = "Network not connected",
        .mqtt = host_mqtt_cmd,
        .tcp_client = host_tcp_client_cmd,
        .transmit = host_transmit_cmd,
        .tcp_server = host_tcp_server_cmd,
        .ntp = host_ntp_cmd,
        .udp = host_udp_cmd,
    };
    mm_net_web_dispatch(cmdline, &dispatch);
}

void port_web_print_options(void) {
    mm_net_print_options(Option.TCP_PORT, Option.ServerResponceTime,
                         Option.UDP_PORT, Option.UDPServerResponceTime,
                         optionsuppressstatus);
    mm_net_print_service_options((int)Option.Telnet, Option.disabletftp);
}

int port_web_option_setter(unsigned char *cmdline) {
    return mm_net_lifecycle_handle_option_result(
        mm_net_lifecycle_option_setter(cmdline, &host_lifecycle_hooks),
        NULL);
}

int port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                    unsigned char *out_sret, int *out_targ) {
    const mm_net_info_hooks_t hooks = {
        .before_query = host_info_before_query,
        .tcp_path = host_info_tcp_path,
        .tcp_request_pending = host_info_tcp_request_pending,
        .max_connections = HOST_WEB_MAX_PCB,
        .tcp_port = Option.TCP_PORT,
        .udp_port = Option.UDP_PORT,
        .ip_address = host_info_ip_address,
        .wifi_status = host_info_wifi_status,
        .tcpip_status = host_info_tcpip_status,
    };
    return mm_net_mminfo(ep, out_iret, out_sret, out_targ, &hooks);
}

void port_web_clear_runtime_state(void) {
    static const mm_net_lifecycle_runtime_hooks_t hooks = {
        .clear_tcp_requests = host_lifecycle_clear_tcp_requests,
        .close_tcp_client = close_tcpclient,
        .close_mqtt = closeMQTT,
        .close_tftp_session = host_tftp_close_session,
        .close_telnet_session = host_telnet_close_conn,
    };
    mm_net_lifecycle_runtime_reset(&hooks);
}

int port_web_get_ssid(unsigned char *out_sret, int *out_targ) {
    out_sret[0] = 0;
    out_sret[1] = 0;
    *out_targ = T_STR;
    return 1;
}
