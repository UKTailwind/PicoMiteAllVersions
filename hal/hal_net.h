/*
 * hal/hal_net.h - network transport HAL.
 *
 * The shared WEB core owns BASIC parsing and user-visible state. Network
 * backends own WiFi association, DNS, sockets, MQTT clients, and event queues.
 */

#ifndef HAL_NET_H
#define HAL_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_NET_OK = 0,
    HAL_NET_ERR = -1,
    HAL_NET_UNSUPPORTED = -2,
    HAL_NET_TIMEOUT = -3,
    HAL_NET_WOULD_BLOCK = -4,
} hal_net_result_t;

typedef uint16_t hal_net_tcp_server_t;
typedef uint16_t hal_net_tcp_conn_t;
typedef uint16_t hal_net_tcp_client_t;
typedef uint16_t hal_net_udp_socket_t;
typedef uint16_t hal_net_mqtt_client_t;

typedef struct {
    uint8_t bytes[16];
    uint8_t family;      /* 4 or 6 */
    uint16_t port;
} hal_net_addr_t;

typedef struct {
    uint32_t raw;
    const char *name;
} hal_net_capability_t;

enum {
    HAL_NET_CAP_WIFI_SCAN       = 1u << 0,
    HAL_NET_CAP_WIFI_CONNECT    = 1u << 1,
    HAL_NET_CAP_TCP_SERVER      = 1u << 2,
    HAL_NET_CAP_TCP_CLIENT      = 1u << 3,
    HAL_NET_CAP_TCP_STREAM      = 1u << 4,
    HAL_NET_CAP_UDP_SERVER      = 1u << 5,
    HAL_NET_CAP_UDP_SEND        = 1u << 6,
    HAL_NET_CAP_MQTT_PLAIN      = 1u << 7,
    HAL_NET_CAP_MQTT_TLS        = 1u << 8,
    HAL_NET_CAP_HTTP_FETCH      = 1u << 9,
    HAL_NET_CAP_MQTT_WEBSOCKET  = 1u << 10,
};

uint32_t hal_net_capabilities(void);
int hal_net_init(void);
void hal_net_poll(void);

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw);
int hal_net_wifi_connect(uint32_t timeout_ms);
int hal_net_wifi_status(void);
int hal_net_tcpip_status(void);
int hal_net_ip_address(char *out, size_t out_len);
int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console);

/*
 * hal_net_tcp_server_open(port, backlog, out)
 *
 * Open a listening TCP socket on `port`. On success, returns HAL_NET_OK
 * and stores a non-zero handle in *out. On failure returns HAL_NET_ERR
 * and leaves *out == 0.
 *
 * REBIND CONTRACT: implementations MUST allow a subsequent open on the
 * same `port` to succeed immediately after a prior `hal_net_tcp_server_close`
 * on that port, even if peer connections from the previous listener are
 * still draining (TIME_WAIT / lingering pcb state). The shared lifecycle
 * (`shared/net/mm_net_service.c::mm_net_tcp_service_open` and the
 * `OPTION TCP SERVER PORT` setter) drives close-then-open sequences in
 * back-to-back lifecycle transitions; a backend that refuses rebind will
 * intermittently fail the second open with no listener bound and produce
 * silent ConnectionRefused errors at the BASIC layer.
 *
 * BSD sockets: set `SO_REUSEADDR` on the socket before `bind()`
 *   (see ports/esp32_s3_metro/main/hal_net_esp32.c).
 * lwip raw API: set `SOF_REUSEADDR` on the pcb via `ip_set_option` before
 *   `tcp_bind` AND ensure `LWIP_SO_REUSE`/`SO_REUSE` is enabled in
 *   lwipopts (see drivers/net_lwip_raw/hal_net_lwip.c, lwipopts.h).
 *
 * Backends MUST report bind failure by returning HAL_NET_ERR; the shared
 * caller layer surfaces "Failed to create TCP server" so harnesses can
 * detect the rebind failure rather than diagnose downstream symptoms.
 */
int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out);
int hal_net_tcp_server_close(hal_net_tcp_server_t server);
int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t *conn);
int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len);
int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void *buf, size_t cap,
                          size_t *len);
int hal_net_tcp_conn_send_some(hal_net_tcp_conn_t conn, const void *buf,
                               size_t cap, size_t *sent);
int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms);
int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn);

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out);
int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms);
int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms);
int hal_net_tcp_client_close(hal_net_tcp_client_t client);

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out);
int hal_net_udp_close(hal_net_udp_socket_t sock);
int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port, const void *buf, size_t len,
                            uint32_t timeout_ms);
int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms);
int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len);

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out);
int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos, int retain);
int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms);
int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms);
int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len);
int hal_net_mqtt_close(hal_net_mqtt_client_t client);

#ifdef __cplusplus
}
#endif

#endif /* HAL_NET_H */
