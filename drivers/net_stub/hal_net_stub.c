/*
 * drivers/net_stub/hal_net_stub.c - no-network HAL backend.
 *
 * Linked by ports that do not expose the BASIC WEB surface. Keeping this
 * backend tiny lets those ports compile the HAL contract without permanent
 * network buffers or socket dependencies.
 */

#include <string.h>

#include "hal/hal_net.h"

uint32_t hal_net_capabilities(void) {
    return 0;
}

int hal_net_init(void) {
    return HAL_NET_OK;
}

void hal_net_poll(void) {
}

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw) {
    (void)ssid; (void)pass; (void)host; (void)ip; (void)mask; (void)gw;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_wifi_connect(uint32_t timeout_ms) {
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_wifi_status(void) {
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcpip_status(void) {
    return HAL_NET_UNSUPPORTED;
}

int hal_net_ip_address(char *out, size_t out_len) {
    if (out && out_len) out[0] = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console) {
    (void)print_to_console;
    if (out && out_len) out[0] = 0;
    if (written) *written = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out) {
    (void)port; (void)backlog;
    if (out) *out = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_server_close(hal_net_tcp_server_t server) {
    (void)server;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t *conn) {
    (void)server;
    if (conn) *conn = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len) {
    (void)server; (void)buf; (void)cap;
    if (conn) *conn = 0;
    if (len) *len = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void *buf, size_t cap,
                          size_t *len) {
    (void)conn; (void)buf; (void)cap;
    if (len) *len = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms) {
    (void)conn; (void)buf; (void)len; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn) {
    (void)conn;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out) {
    (void)host; (void)port; (void)timeout_ms;
    if (out) *out = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms) {
    (void)client; (void)buf; (void)len; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms) {
    (void)client; (void)buf; (void)cap; (void)timeout_ms;
    if (len) *len = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_client_close(hal_net_tcp_client_t client) {
    (void)client;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out) {
    (void)port;
    if (out) *out = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_udp_close(hal_net_udp_socket_t sock) {
    (void)sock;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port, const void *buf, size_t len,
                            uint32_t timeout_ms) {
    (void)sock; (void)host; (void)port; (void)buf; (void)len; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms) {
    (void)host; (void)port; (void)buf; (void)len; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len) {
    (void)sock; (void)buf; (void)cap;
    if (from) memset(from, 0, sizeof(*from));
    if (len) *len = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out) {
    (void)host; (void)port; (void)user; (void)pass; (void)client_id; (void)timeout_ms;
    if (out) *out = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos, int retain) {
    (void)client; (void)topic; (void)payload; (void)len; (void)qos; (void)retain;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms) {
    (void)client; (void)topic; (void)qos; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms) {
    (void)client; (void)topic; (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len) {
    (void)client; (void)payload; (void)payload_cap;
    if (topic && topic_cap) topic[0] = 0;
    if (payload_len) *payload_len = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_mqtt_close(hal_net_mqtt_client_t client) {
    (void)client;
    return HAL_NET_UNSUPPORTED;
}
