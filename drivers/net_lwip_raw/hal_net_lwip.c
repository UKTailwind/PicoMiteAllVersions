/*
 * drivers/net_lwip_raw/hal_net_lwip.c - lwIP raw network HAL backend.
 *
 * This starts with UDP because the Pico WEB command parser is already shared
 * and UDP maps cleanly onto hal_net.h. TCP/WiFi/MQTT continue to live in the
 * legacy Pico WEB files until their transport slices move behind this backend.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hal_net.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#define LWIP_HAL_MAX_UDP_SOCKS 4
#define LWIP_HAL_UDP_RX_CAP 512
#define LWIP_HAL_MAX_TCP_SERVERS 2
#define LWIP_HAL_MAX_TCP_CLIENTS 2
#define LWIP_HAL_MAX_TCP_CONNS 8
#define LWIP_HAL_MAX_MQTT_CLIENTS 1
#define LWIP_HAL_MQTT_TOPIC_CAP 256
#define LWIP_HAL_MQTT_PAYLOAD_CAP 256

typedef struct {
    struct udp_pcb *pcb;
    uint8_t pending;
    ip_addr_t from;
    uint16_t from_port;
    size_t len;
    uint8_t data[LWIP_HAL_UDP_RX_CAP];
} lwip_hal_udp_slot_t;

typedef struct {
    volatile int complete;
    volatile int failed;
    ip_addr_t addr;
} lwip_hal_dns_state_t;

typedef struct {
    char *out;
    size_t out_len;
    size_t used;
    int overflow;
    uint8_t seen_count;
    char seen[100][32];
} lwip_hal_wifi_scan_state_t;

typedef struct lwip_hal_rx_node {
    struct lwip_hal_rx_node *next;
    size_t len;
    size_t off;
    uint8_t data[];
} lwip_hal_rx_node_t;

typedef struct {
    struct tcp_pcb *pcb;
    volatile int connected;
    volatile int failed;
    volatile int closed;
    lwip_hal_rx_node_t *rx_head;
    lwip_hal_rx_node_t *rx_tail;
} lwip_hal_tcp_client_slot_t;

typedef struct {
    struct tcp_pcb *pcb;
    uint16_t port;
} lwip_hal_tcp_server_slot_t;

typedef struct {
    struct tcp_pcb *pcb;
    uint16_t server;
    volatile int closed;
    volatile int failed;
    volatile int accepted;
    volatile int pending;
    volatile int delivered;
    uint8_t *rx;
    size_t rx_len;
    size_t rx_off;
} lwip_hal_tcp_conn_slot_t;

typedef struct {
    mqtt_client_t *client;
    struct mqtt_connect_client_info_t info;
    volatile int connected;
    volatile int failed;
    volatile int sub_done;
    volatile int sub_failed;
    volatile int unsub_done;
    volatile int unsub_failed;
    volatile int pending;
    char client_id[64];
    char user[128];
    char pass[128];
    char topic[LWIP_HAL_MQTT_TOPIC_CAP];
    char pending_topic[LWIP_HAL_MQTT_TOPIC_CAP];
    uint8_t payload[LWIP_HAL_MQTT_PAYLOAD_CAP];
    size_t payload_len;
} lwip_hal_mqtt_slot_t;

static lwip_hal_udp_slot_t udp_slots[LWIP_HAL_MAX_UDP_SOCKS];
static lwip_hal_tcp_server_slot_t tcp_server_slots[LWIP_HAL_MAX_TCP_SERVERS];
static lwip_hal_tcp_conn_slot_t tcp_conn_slots[LWIP_HAL_MAX_TCP_CONNS];
static lwip_hal_tcp_client_slot_t tcp_client_slots[LWIP_HAL_MAX_TCP_CLIENTS];
static lwip_hal_mqtt_slot_t mqtt_slots[LWIP_HAL_MAX_MQTT_CLIENTS];
static char wifi_ssid[64];
static char wifi_pass[64];
static char wifi_host[32];
static char wifi_ip[16];
static char wifi_mask[16];
static char wifi_gw[16];

static lwip_hal_tcp_server_slot_t *tcp_server_slot(hal_net_tcp_server_t server);
static lwip_hal_tcp_conn_slot_t *tcp_conn_slot(hal_net_tcp_conn_t conn);
static void tcp_conn_close_slot(lwip_hal_tcp_conn_slot_t *slot);
static err_t tcp_server_accept_cb(void *arg, struct tcp_pcb *client_pcb,
                                  err_t err);
static int tcp_write_all(struct tcp_pcb *pcb, const void *buf, size_t len,
                         uint32_t timeout_ms);

uint32_t hal_net_capabilities(void) {
    return HAL_NET_CAP_WIFI_SCAN | HAL_NET_CAP_WIFI_CONNECT |
           HAL_NET_CAP_TCP_SERVER | HAL_NET_CAP_TCP_CLIENT |
           HAL_NET_CAP_TCP_STREAM | HAL_NET_CAP_UDP_SERVER |
           HAL_NET_CAP_UDP_SEND | HAL_NET_CAP_MQTT_PLAIN;
}

int hal_net_init(void) {
    return HAL_NET_OK;
}

void hal_net_poll(void) {
    cyw43_arch_poll();
}

static lwip_hal_udp_slot_t *udp_slot(hal_net_udp_socket_t sock) {
    if (sock == 0 || sock > LWIP_HAL_MAX_UDP_SOCKS) return NULL;
    lwip_hal_udp_slot_t *slot = &udp_slots[sock - 1];
    return slot->pcb ? slot : NULL;
}

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port) {
    (void)pcb;
    lwip_hal_udp_slot_t *slot = (lwip_hal_udp_slot_t *)arg;
    if (!slot || !p) return;
    size_t len = p->tot_len;
    if (len > sizeof(slot->data)) len = sizeof(slot->data);
    slot->len = pbuf_copy_partial(p, slot->data, (u16_t)len, 0);
    slot->from = *addr;
    slot->from_port = port;
    slot->pending = 1;
    pbuf_free(p);
}

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out) {
    if (out) *out = 0;

    int free_slot = -1;
    for (int i = 0; i < LWIP_HAL_MAX_UDP_SOCKS; i++) {
        if (!udp_slots[i].pcb) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return HAL_NET_ERR;

    struct udp_pcb *pcb = udp_new();
    if (!pcb) return HAL_NET_ERR;
    ip_set_option(pcb, SOF_BROADCAST);
    err_t err = udp_bind(pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        udp_remove(pcb);
        return HAL_NET_ERR;
    }

    lwip_hal_udp_slot_t *slot = &udp_slots[free_slot];
    memset(slot, 0, sizeof(*slot));
    slot->pcb = pcb;
    udp_recv(pcb, udp_recv_cb, slot);
    if (out) *out = (hal_net_udp_socket_t)(free_slot + 1);
    return HAL_NET_OK;
}

int hal_net_udp_close(hal_net_udp_socket_t sock) {
    lwip_hal_udp_slot_t *slot = udp_slot(sock);
    if (!slot) return HAL_NET_ERR;
    udp_remove(slot->pcb);
    memset(slot, 0, sizeof(*slot));
    return HAL_NET_OK;
}

static void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr,
                         void *arg) {
    (void)hostname;
    lwip_hal_dns_state_t *state = (lwip_hal_dns_state_t *)arg;
    if (ipaddr) {
        state->addr = *ipaddr;
        state->complete = 1;
    } else {
        state->failed = 1;
    }
}

static int resolve_host(const char *host, uint32_t timeout_ms, ip_addr_t *out) {
    if (!host || !*host || !out) return HAL_NET_ERR;
    ip4_addr_t ip4;
    if (!isalpha((unsigned char)host[0]) && strchr(host, '.') &&
        strchr(host, '.') < host + 4) {
        if (!ip4addr_aton(host, &ip4)) return HAL_NET_ERR;
        ip_addr_copy_from_ip4(*out, ip4);
        return HAL_NET_OK;
    }

    lwip_hal_dns_state_t state = {0};
    err_t err = dns_gethostbyname(host, out, dns_found_cb, &state);
    if (err == ERR_OK) return HAL_NET_OK;
    if (err != ERR_INPROGRESS) return HAL_NET_ERR;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!state.complete && !state.failed) {
        cyw43_arch_poll();
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0)
            return HAL_NET_TIMEOUT;
    }
    if (state.failed) return HAL_NET_ERR;
    *out = state.addr;
    return HAL_NET_OK;
}

int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port, const void *buf, size_t len,
                            uint32_t timeout_ms) {
    lwip_hal_udp_slot_t *slot = udp_slot(sock);
    if (!slot || !host || !buf || len > 0xffffu) return HAL_NET_ERR;

    ip_addr_t remote;
    int rc = resolve_host(host, timeout_ms, &remote);
    if (rc != HAL_NET_OK) return rc;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p) return HAL_NET_ERR;
    if (len) memcpy(p->payload, buf, len);
    err_t err = udp_sendto(slot->pcb, p, &remote, port);
    pbuf_free(p);
    return err == ERR_OK ? HAL_NET_OK : HAL_NET_ERR;
}

int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms) {
    if (!host || !buf || len > 0xffffu) return HAL_NET_ERR;
    hal_net_udp_socket_t sock = 0;
    int rc = hal_net_udp_bind(0, &sock);
    if (rc != HAL_NET_OK) {
        struct udp_pcb *pcb = udp_new();
        if (!pcb) return HAL_NET_ERR;
        ip_set_option(pcb, SOF_BROADCAST);
        ip_addr_t remote;
        rc = resolve_host(host, timeout_ms, &remote);
        if (rc == HAL_NET_OK) {
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
            if (!p) rc = HAL_NET_ERR;
            else {
                if (len) memcpy(p->payload, buf, len);
                rc = udp_sendto(pcb, p, &remote, port) == ERR_OK
                         ? HAL_NET_OK
                         : HAL_NET_ERR;
                pbuf_free(p);
            }
        }
        udp_remove(pcb);
        return rc;
    }
    rc = hal_net_udp_socket_send(sock, host, port, buf, len, timeout_ms);
    hal_net_udp_close(sock);
    return rc;
}

int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len) {
    if (len) *len = 0;
    if (from) memset(from, 0, sizeof(*from));
    lwip_hal_udp_slot_t *slot = udp_slot(sock);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    if (!slot->pending) return HAL_NET_WOULD_BLOCK;

    size_t copy = slot->len;
    if (copy > cap) copy = cap;
    memcpy(buf, slot->data, copy);
    if (len) *len = copy;
    if (from && IP_IS_V4(&slot->from)) {
        uint32_t raw = ip4_addr_get_u32(ip_2_ip4(&slot->from));
        from->family = 4;
        from->port = slot->from_port;
        memcpy(from->bytes, &raw, 4);
    }
    slot->pending = 0;
    slot->len = 0;
    return HAL_NET_OK;
}

static lwip_hal_tcp_server_slot_t *tcp_server_slot(hal_net_tcp_server_t server) {
    if (server == 0 || server > LWIP_HAL_MAX_TCP_SERVERS) return NULL;
    lwip_hal_tcp_server_slot_t *slot = &tcp_server_slots[server - 1];
    return slot->pcb ? slot : NULL;
}

static lwip_hal_tcp_conn_slot_t *tcp_conn_slot(hal_net_tcp_conn_t conn) {
    if (conn == 0 || conn > LWIP_HAL_MAX_TCP_CONNS) return NULL;
    lwip_hal_tcp_conn_slot_t *slot = &tcp_conn_slots[conn - 1];
    return (slot->pcb || slot->closed || slot->delivered || slot->pending ||
            slot->accepted)
               ? slot
               : NULL;
}

static int tcp_alloc_conn_slot(struct tcp_pcb *pcb, uint16_t server) {
    for (int i = 0; i < LWIP_HAL_MAX_TCP_CONNS; i++) {
        if (!tcp_conn_slots[i].pcb) {
            memset(&tcp_conn_slots[i], 0, sizeof(tcp_conn_slots[i]));
            tcp_conn_slots[i].pcb = pcb;
            tcp_conn_slots[i].server = server;
            tcp_conn_slots[i].accepted = 1;
            return i;
        }
    }
    return -1;
}

static void tcp_conn_close_slot(lwip_hal_tcp_conn_slot_t *slot) {
    if (!slot) return;
    if (slot->pcb) {
        tcp_arg(slot->pcb, NULL);
        tcp_recv(slot->pcb, NULL);
        tcp_sent(slot->pcb, NULL);
        tcp_err(slot->pcb, NULL);
        err_t err = tcp_close(slot->pcb);
        if (err != ERR_OK) tcp_abort(slot->pcb);
    }
    free(slot->rx);
    memset(slot, 0, sizeof(*slot));
}

static err_t tcp_server_conn_recv_cb(void *arg, struct tcp_pcb *pcb,
                                     struct pbuf *p, err_t err) {
    (void)err;
    lwip_hal_tcp_conn_slot_t *slot = (lwip_hal_tcp_conn_slot_t *)arg;
    if (!slot) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (!p) {
        slot->closed = 1;
        slot->pcb = NULL;
        return ERR_OK;
    }
    if (p->tot_len > 0) {
        size_t old_len = slot->rx_len;
        uint8_t *rx = (uint8_t *)realloc(slot->rx, old_len + p->tot_len);
        if (!rx) {
            pbuf_free(p);
            return ERR_MEM;
        }
        slot->rx = rx;
        slot->rx_len = old_len +
            pbuf_copy_partial(p, slot->rx + old_len, p->tot_len, 0);
        slot->pending = 1;
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_conn_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)pcb; (void)len;
    return ERR_OK;
}

static void tcp_server_conn_err_cb(void *arg, err_t err) {
    (void)err;
    lwip_hal_tcp_conn_slot_t *slot = (lwip_hal_tcp_conn_slot_t *)arg;
    if (!slot) return;
    slot->failed = 1;
    slot->pcb = NULL;
}

static err_t tcp_server_accept_cb(void *arg, struct tcp_pcb *client_pcb,
                                  err_t err) {
    lwip_hal_tcp_server_slot_t *server_slot =
        (lwip_hal_tcp_server_slot_t *)arg;
    if (err != ERR_OK || !client_pcb) return ERR_VAL;
    if (!server_slot) return ERR_VAL;
    int server_idx = (int)(server_slot - tcp_server_slots);
    if (server_idx < 0 || server_idx >= LWIP_HAL_MAX_TCP_SERVERS)
        return ERR_VAL;
    int idx = tcp_alloc_conn_slot(client_pcb, (uint16_t)(server_idx + 1));
    if (idx < 0) {
        tcp_abort(client_pcb);
        return ERR_ABRT;
    }
    lwip_hal_tcp_conn_slot_t *slot = &tcp_conn_slots[idx];
    tcp_arg(client_pcb, slot);
    tcp_recv(client_pcb, tcp_server_conn_recv_cb);
    tcp_sent(client_pcb, tcp_server_conn_sent_cb);
    tcp_err(client_pcb, tcp_server_conn_err_cb);
    return ERR_OK;
}

static int tcp_write_all(struct tcp_pcb *pcb, const void *buf, size_t len,
                         uint32_t timeout_ms) {
    if (!pcb || !buf) return HAL_NET_ERR;
    const uint8_t *src = (const uint8_t *)buf;
    size_t sent = 0;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (sent < len) {
        u16_t snd = tcp_sndbuf(pcb);
        if (snd == 0 || tcp_sndqueuelen(pcb) > 6) {
            cyw43_arch_poll();
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0)
                return HAL_NET_TIMEOUT;
            continue;
        }
        size_t chunk = len - sent;
        if (chunk > snd) chunk = snd;
        if (chunk > 0xffffu) chunk = 0xffffu;
        err_t err = tcp_write(pcb, src + sent, (u16_t)chunk,
                              TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) {
            cyw43_arch_poll();
            continue;
        }
        if (err != ERR_OK) return HAL_NET_ERR;
        sent += chunk;
        tcp_output(pcb);
    }
    return HAL_NET_OK;
}

static void copy_string(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    size_t len = strlen(src);
    if (len >= dst_len) len = dst_len - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw) {
    copy_string(wifi_ssid, sizeof(wifi_ssid), ssid);
    copy_string(wifi_pass, sizeof(wifi_pass), pass);
    copy_string(wifi_host, sizeof(wifi_host), host);
    copy_string(wifi_ip, sizeof(wifi_ip), ip);
    copy_string(wifi_mask, sizeof(wifi_mask), mask);
    copy_string(wifi_gw, sizeof(wifi_gw), gw);
    return HAL_NET_OK;
}

int hal_net_wifi_connect(uint32_t timeout_ms) {
    extern volatile int WIFIconnected;

    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    if (*wifi_ip) {
        ip4_addr_t ipaddr, gateway, mask;
        if (!ip4addr_aton(wifi_ip, &ipaddr) ||
            !ip4addr_aton(wifi_gw, &gateway) ||
            !ip4addr_aton(wifi_mask, &mask)) {
            WIFIconnected = 0;
            return HAL_NET_ERR;
        }
        dhcp_stop(cyw43_state.netif);
        netif_set_addr(cyw43_state.netif, &ipaddr, &mask, &gateway);
    }

    if (*wifi_host) netif_set_hostname(cyw43_state.netif, wifi_host);

    if (!*wifi_ssid) {
        WIFIconnected = 0;
        cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf);
        return HAL_NET_OK;
    }

    uint32_t limit = timeout_ms ? timeout_ms : 30000;
    int auth = *wifi_pass ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    int rc = cyw43_arch_wifi_connect_timeout_ms(wifi_ssid,
                                                *wifi_pass ? wifi_pass : NULL,
                                                auth, limit);
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf);
    if (rc) {
        WIFIconnected = 0;
        return HAL_NET_TIMEOUT;
    }
    WIFIconnected = 1;
    return HAL_NET_OK;
}

int hal_net_wifi_status(void) {
    return cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
}

int hal_net_tcpip_status(void) {
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
}

int hal_net_ip_address(char *out, size_t out_len) {
    if (out && out_len) out[0] = 0;
    if (!out || out_len == 0) return HAL_NET_ERR;
    struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
    if (!netif) return HAL_NET_ERR;
    snprintf(out, out_len, "%s", ip4addr_ntoa(netif_ip4_addr(netif)));
    return HAL_NET_OK;
}

static int wifi_scan_result_cb(void *env, const cyw43_ev_scan_result_t *result) {
    lwip_hal_wifi_scan_state_t *state = (lwip_hal_wifi_scan_state_t *)env;
    if (!state || !result || state->overflow) return 0;

    for (uint8_t i = 0; i < state->seen_count; i++) {
        if (strncmp((const char *)result->ssid, state->seen[i],
                    sizeof(state->seen[i])) == 0) {
            return 0;
        }
    }
    if (state->seen_count < 100) {
        copy_string(state->seen[state->seen_count],
                    sizeof(state->seen[state->seen_count]),
                    (const char *)result->ssid);
        state->seen_count++;
    }

    char line[160];
    int n = snprintf(line, sizeof(line),
                     "ssid: %-32s rssi: %4d chan: %3d mac: %02x:%02x:%02x:%02x:%02x:%02x sec: %u\r\n",
                     result->ssid, result->rssi, result->channel,
                     result->bssid[0], result->bssid[1], result->bssid[2],
                     result->bssid[3], result->bssid[4], result->bssid[5],
                     result->auth_mode);
    if (n < 0 || state->used + (size_t)n + 1 > state->out_len) {
        state->overflow = 1;
        return 0;
    }
    memcpy(state->out + state->used, line, (size_t)n);
    state->used += (size_t)n;
    state->out[state->used] = 0;
    return 0;
}

int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console) {
    (void)print_to_console;
    if (out && out_len) out[0] = 0;
    if (written) *written = 0;
    if (!out || out_len == 0) return HAL_NET_ERR;

    lwip_hal_wifi_scan_state_t *state =
        (lwip_hal_wifi_scan_state_t *)malloc(sizeof(*state));
    if (!state) return HAL_NET_ERR;
    memset(state, 0, sizeof(*state));
    state->out = out;
    state->out_len = out_len;

    cyw43_wifi_scan_options_t scan_options = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, state,
                              wifi_scan_result_cb);
    if (err != 0) {
        free(state);
        return HAL_NET_ERR;
    }

    absolute_time_t deadline = make_timeout_time_ms(10000);
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        cyw43_arch_poll();
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            free(state);
            return HAL_NET_TIMEOUT;
        }
    }

    int overflow = state->overflow;
    if (written) *written = state->used;
    free(state);
    return overflow ? HAL_NET_ERR : HAL_NET_OK;
}

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out) {
    if (out) *out = 0;
    if (!port) return HAL_NET_ERR;
    int free_slot = -1;
    for (int i = 0; i < LWIP_HAL_MAX_TCP_SERVERS; i++) {
        if (!tcp_server_slots[i].pcb) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return HAL_NET_ERR;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return HAL_NET_ERR;
    err_t err = tcp_bind(pcb, NULL, port);
    if (err != ERR_OK) {
        tcp_close(pcb);
        return HAL_NET_ERR;
    }
    struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(pcb, backlog);
    if (!listen_pcb) {
        tcp_close(pcb);
        return HAL_NET_ERR;
    }
    tcp_server_slots[free_slot].pcb = listen_pcb;
    tcp_server_slots[free_slot].port = port;
    tcp_arg(listen_pcb, &tcp_server_slots[free_slot]);
    tcp_accept(listen_pcb, tcp_server_accept_cb);
    if (out) *out = (hal_net_tcp_server_t)(free_slot + 1);
    return HAL_NET_OK;
}

int hal_net_tcp_server_close(hal_net_tcp_server_t server) {
    lwip_hal_tcp_server_slot_t *slot = tcp_server_slot(server);
    if (!slot) return HAL_NET_ERR;
    tcp_arg(slot->pcb, NULL);
    tcp_accept(slot->pcb, NULL);
    tcp_close(slot->pcb);
    memset(slot, 0, sizeof(*slot));
    return HAL_NET_OK;
}

int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t *conn) {
    if (conn) *conn = 0;
    lwip_hal_tcp_server_slot_t *server_slot = tcp_server_slot(server);
    if (!server_slot) return HAL_NET_ERR;
    for (int i = 0; i < LWIP_HAL_MAX_TCP_CONNS; i++) {
        lwip_hal_tcp_conn_slot_t *slot = &tcp_conn_slots[i];
        if (!slot->pcb || slot->server != server || !slot->accepted)
            continue;
        slot->accepted = 0;
        if (conn) *conn = (hal_net_tcp_conn_t)(i + 1);
        return HAL_NET_OK;
    }
    return HAL_NET_WOULD_BLOCK;
}

int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len) {
    if (conn) *conn = 0;
    if (len) *len = 0;
    lwip_hal_tcp_server_slot_t *server_slot = tcp_server_slot(server);
    if (!server_slot || !buf || cap == 0) return HAL_NET_ERR;
    for (int i = 0; i < LWIP_HAL_MAX_TCP_CONNS; i++) {
        lwip_hal_tcp_conn_slot_t *slot = &tcp_conn_slots[i];
        if (!slot->pcb || slot->server != server || !slot->pending ||
            slot->delivered)
            continue;
        size_t available = slot->rx_len - slot->rx_off;
        if (available > cap) return HAL_NET_ERR;
        memcpy(buf, slot->rx + slot->rx_off, available);
        for (size_t j = 0; j < available; j++)
            if (buf[j] == 0) buf[j] = ' ';
        if (len) *len = available;
        if (conn) *conn = (hal_net_tcp_conn_t)(i + 1);
        slot->accepted = 0;
        slot->pending = 0;
        slot->delivered = 1;
        free(slot->rx);
        slot->rx = NULL;
        slot->rx_len = 0;
        slot->rx_off = 0;
        return HAL_NET_OK;
    }
    return HAL_NET_WOULD_BLOCK;
}

int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void *buf, size_t cap,
                          size_t *len) {
    if (len) *len = 0;
    lwip_hal_tcp_conn_slot_t *slot = tcp_conn_slot(conn);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    if (slot->rx_off < slot->rx_len) {
        size_t copy = slot->rx_len - slot->rx_off;
        if (copy > cap) copy = cap;
        memcpy(buf, slot->rx + slot->rx_off, copy);
        slot->rx_off += copy;
        if (len) *len = copy;
        if (slot->rx_off >= slot->rx_len) {
            free(slot->rx);
            slot->rx = NULL;
            slot->rx_len = 0;
            slot->rx_off = 0;
            slot->pending = 0;
        }
        return HAL_NET_OK;
    }
    if (slot->closed || slot->failed || !slot->pcb) return HAL_NET_ERR;
    return HAL_NET_WOULD_BLOCK;
}

int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms) {
    lwip_hal_tcp_conn_slot_t *slot = tcp_conn_slot(conn);
    if (!slot || !buf) return HAL_NET_ERR;
    return tcp_write_all(slot->pcb, buf, len, timeout_ms);
}

int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn) {
    lwip_hal_tcp_conn_slot_t *slot = tcp_conn_slot(conn);
    if (!slot) return HAL_NET_ERR;
    tcp_conn_close_slot(slot);
    return HAL_NET_OK;
}

static lwip_hal_tcp_client_slot_t *tcp_client_slot(hal_net_tcp_client_t client) {
    if (client == 0 || client > LWIP_HAL_MAX_TCP_CLIENTS) return NULL;
    lwip_hal_tcp_client_slot_t *slot = &tcp_client_slots[client - 1];
    return slot->pcb || slot->closed ? slot : NULL;
}

static void tcp_client_free_rx(lwip_hal_tcp_client_slot_t *slot) {
    lwip_hal_rx_node_t *node = slot->rx_head;
    while (node) {
        lwip_hal_rx_node_t *next = node->next;
        free(node);
        node = next;
    }
    slot->rx_head = NULL;
    slot->rx_tail = NULL;
}

static err_t tcp_client_recv_cb(void *arg, struct tcp_pcb *pcb,
                                struct pbuf *p, err_t err) {
    (void)err;
    lwip_hal_tcp_client_slot_t *slot = (lwip_hal_tcp_client_slot_t *)arg;
    if (!slot) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (!p) {
        slot->closed = 1;
        slot->pcb = NULL;
        return ERR_OK;
    }
    if (p->tot_len > 0) {
        lwip_hal_rx_node_t *node =
            (lwip_hal_rx_node_t *)malloc(sizeof(*node) + p->tot_len);
        if (!node) {
            pbuf_free(p);
            return ERR_MEM;
        }
        node->next = NULL;
        node->len = pbuf_copy_partial(p, node->data, p->tot_len, 0);
        node->off = 0;
        if (slot->rx_tail) slot->rx_tail->next = node;
        else slot->rx_head = node;
        slot->rx_tail = node;
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_client_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)pcb; (void)len;
    return ERR_OK;
}

static void tcp_client_err_cb(void *arg, err_t err) {
    (void)err;
    lwip_hal_tcp_client_slot_t *slot = (lwip_hal_tcp_client_slot_t *)arg;
    if (!slot) return;
    slot->failed = 1;
    slot->pcb = NULL;
}

static err_t tcp_client_connected_cb(void *arg, struct tcp_pcb *pcb,
                                     err_t err) {
    (void)pcb;
    lwip_hal_tcp_client_slot_t *slot = (lwip_hal_tcp_client_slot_t *)arg;
    if (!slot) return ERR_ARG;
    if (err == ERR_OK) slot->connected = 1;
    else slot->failed = 1;
    return ERR_OK;
}

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out) {
    if (out) *out = 0;
    if (!host || !port) return HAL_NET_ERR;

    int free_slot = -1;
    for (int i = 0; i < LWIP_HAL_MAX_TCP_CLIENTS; i++) {
        if (!tcp_client_slots[i].pcb) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return HAL_NET_ERR;

    ip_addr_t remote;
    int rc = resolve_host(host, timeout_ms, &remote);
    if (rc != HAL_NET_OK) return rc;

    lwip_hal_tcp_client_slot_t *slot = &tcp_client_slots[free_slot];
    memset(slot, 0, sizeof(*slot));
    slot->pcb = tcp_new_ip_type(IP_GET_TYPE(&remote));
    if (!slot->pcb) return HAL_NET_ERR;

    tcp_arg(slot->pcb, slot);
    tcp_recv(slot->pcb, tcp_client_recv_cb);
    tcp_sent(slot->pcb, tcp_client_sent_cb);
    tcp_err(slot->pcb, tcp_client_err_cb);

    err_t err = tcp_connect(slot->pcb, &remote, port, tcp_client_connected_cb);
    if (err != ERR_OK) {
        hal_net_tcp_client_close((hal_net_tcp_client_t)(free_slot + 1));
        return HAL_NET_ERR;
    }

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!slot->connected && !slot->failed && !slot->closed) {
        cyw43_arch_poll();
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            hal_net_tcp_client_close((hal_net_tcp_client_t)(free_slot + 1));
            return HAL_NET_TIMEOUT;
        }
    }
    if (!slot->connected) {
        hal_net_tcp_client_close((hal_net_tcp_client_t)(free_slot + 1));
        return HAL_NET_ERR;
    }
    if (out) *out = (hal_net_tcp_client_t)(free_slot + 1);
    return HAL_NET_OK;
}

int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms) {
    lwip_hal_tcp_client_slot_t *slot = tcp_client_slot(client);
    if (!slot || !buf) return HAL_NET_ERR;
    const uint8_t *src = (const uint8_t *)buf;
    size_t sent = 0;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (sent < len) {
        if (slot->failed || slot->closed || !slot->pcb) return HAL_NET_ERR;
        u16_t snd = tcp_sndbuf(slot->pcb);
        if (snd == 0 || tcp_sndqueuelen(slot->pcb) > 6) {
            cyw43_arch_poll();
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0)
                return HAL_NET_TIMEOUT;
            continue;
        }
        size_t chunk = len - sent;
        if (chunk > snd) chunk = snd;
        if (chunk > 0xffffu) chunk = 0xffffu;
        err_t err = tcp_write(slot->pcb, src + sent, (u16_t)chunk,
                              TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) {
            cyw43_arch_poll();
            continue;
        }
        if (err != ERR_OK) return HAL_NET_ERR;
        sent += chunk;
        tcp_output(slot->pcb);
    }
    return HAL_NET_OK;
}

int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms) {
    if (len) *len = 0;
    lwip_hal_tcp_client_slot_t *slot = tcp_client_slot(client);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!slot->rx_head) {
        if (slot->failed || slot->closed || !slot->pcb)
            return HAL_NET_WOULD_BLOCK;
        if (timeout_ms == 0 ||
            absolute_time_diff_us(get_absolute_time(), deadline) <= 0)
            return HAL_NET_TIMEOUT;
        cyw43_arch_poll();
    }

    size_t copied = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (slot->rx_head && copied < cap) {
        lwip_hal_rx_node_t *node = slot->rx_head;
        size_t avail = node->len - node->off;
        size_t take = cap - copied;
        if (take > avail) take = avail;
        memcpy(dst + copied, node->data + node->off, take);
        copied += take;
        node->off += take;
        if (node->off == node->len) {
            slot->rx_head = node->next;
            if (!slot->rx_head) slot->rx_tail = NULL;
            free(node);
        }
    }
    if (len) *len = copied;
    return copied ? HAL_NET_OK : HAL_NET_WOULD_BLOCK;
}

int hal_net_tcp_client_close(hal_net_tcp_client_t client) {
    lwip_hal_tcp_client_slot_t *slot = tcp_client_slot(client);
    if (!slot) return HAL_NET_ERR;
    if (slot->pcb) {
        tcp_arg(slot->pcb, NULL);
        tcp_recv(slot->pcb, NULL);
        tcp_sent(slot->pcb, NULL);
        tcp_err(slot->pcb, NULL);
        err_t err = tcp_close(slot->pcb);
        if (err != ERR_OK) tcp_abort(slot->pcb);
    }
    tcp_client_free_rx(slot);
    memset(slot, 0, sizeof(*slot));
    return HAL_NET_OK;
}

static lwip_hal_mqtt_slot_t *mqtt_slot(hal_net_mqtt_client_t client) {
    if (client == 0 || client > LWIP_HAL_MAX_MQTT_CLIENTS) return NULL;
    lwip_hal_mqtt_slot_t *slot = &mqtt_slots[client - 1];
    return slot->client ? slot : NULL;
}

static hal_net_mqtt_client_t mqtt_alloc_slot(void) {
    for (int i = 0; i < LWIP_HAL_MAX_MQTT_CLIENTS; i++) {
        if (!mqtt_slots[i].client) {
            memset(&mqtt_slots[i], 0, sizeof(mqtt_slots[i]));
            return (hal_net_mqtt_client_t)(i + 1);
        }
    }
    return 0;
}

static int mqtt_wait_done(volatile int *done, volatile int *failed,
                          uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!*done && !*failed) {
        cyw43_arch_poll();
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0)
            return HAL_NET_TIMEOUT;
    }
    return *failed ? HAL_NET_ERR : HAL_NET_OK;
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg,
                               mqtt_connection_status_t status) {
    (void)client;
    lwip_hal_mqtt_slot_t *slot = (lwip_hal_mqtt_slot_t *)arg;
    if (!slot) return;
    if (status == MQTT_CONNECT_ACCEPTED) slot->connected = 1;
    else slot->failed = 1;
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic,
                                     u32_t tot_len) {
    (void)tot_len;
    lwip_hal_mqtt_slot_t *slot = (lwip_hal_mqtt_slot_t *)arg;
    if (!slot) return;
    copy_string(slot->topic, sizeof(slot->topic), topic);
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len,
                                  u8_t flags) {
    (void)flags;
    lwip_hal_mqtt_slot_t *slot = (lwip_hal_mqtt_slot_t *)arg;
    if (!slot || !data) return;
    size_t copy = len;
    if (copy > sizeof(slot->payload)) copy = sizeof(slot->payload);
    memcpy(slot->payload, data, copy);
    slot->payload_len = copy;
    copy_string(slot->pending_topic, sizeof(slot->pending_topic), slot->topic);
    slot->pending = 1;
}

static void mqtt_sub_cb(void *arg, err_t err) {
    lwip_hal_mqtt_slot_t *slot = (lwip_hal_mqtt_slot_t *)arg;
    if (!slot) return;
    if (err == ERR_OK) slot->sub_done = 1;
    else slot->sub_failed = 1;
}

static void mqtt_unsub_cb(void *arg, err_t err) {
    lwip_hal_mqtt_slot_t *slot = (lwip_hal_mqtt_slot_t *)arg;
    if (!slot) return;
    if (err == ERR_OK) slot->unsub_done = 1;
    else slot->unsub_failed = 1;
}

static void mqtt_request_cb(void *arg, err_t err) {
    (void)arg;
    (void)err;
}

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out) {
    if (out) *out = 0;
    if (!host || !port) return HAL_NET_ERR;

    ip_addr_t remote;
    int rc = resolve_host(host, timeout_ms, &remote);
    if (rc != HAL_NET_OK) return rc;

    hal_net_mqtt_client_t handle = mqtt_alloc_slot();
    if (!handle) return HAL_NET_ERR;
    lwip_hal_mqtt_slot_t *slot = &mqtt_slots[handle - 1];
    slot->client = mqtt_client_new();
    if (!slot->client) {
        memset(slot, 0, sizeof(*slot));
        return HAL_NET_ERR;
    }

    if (client_id && *client_id) {
        copy_string(slot->client_id, sizeof(slot->client_id), client_id);
    } else {
        snprintf(slot->client_id, sizeof(slot->client_id), "WebMite%llX",
                 (unsigned long long)time_us_64());
    }
    copy_string(slot->user, sizeof(slot->user), user);
    copy_string(slot->pass, sizeof(slot->pass), pass);

    memset(&slot->info, 0, sizeof(slot->info));
    slot->info.client_id = slot->client_id;
    slot->info.client_user = *slot->user ? slot->user : NULL;
    slot->info.client_pass = *slot->pass ? slot->pass : NULL;
    slot->info.keep_alive = 100;
    slot->info.will_qos = 1;
    slot->info.will_retain = 1;

    err_t err = mqtt_client_connect(slot->client, &remote, port,
                                    mqtt_connection_cb, slot, &slot->info);
    if (err != ERR_OK) {
        hal_net_mqtt_close(handle);
        return HAL_NET_ERR;
    }

    rc = mqtt_wait_done(&slot->connected, &slot->failed, timeout_ms);
    if (rc != HAL_NET_OK) {
        hal_net_mqtt_close(handle);
        return rc;
    }

    mqtt_set_inpub_callback(slot->client, mqtt_incoming_publish_cb,
                            mqtt_incoming_data_cb, slot);
    if (out) *out = handle;
    return HAL_NET_OK;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos,
                         int retain) {
    lwip_hal_mqtt_slot_t *slot = mqtt_slot(client);
    if (!slot || !topic || (!payload && len)) return HAL_NET_ERR;
    if (len > 0xffffu) return HAL_NET_ERR;
    err_t err = mqtt_publish(slot->client, topic, payload, (u16_t)len,
                             (u8_t)qos, (u8_t)retain, mqtt_request_cb, slot);
    return err == ERR_OK ? HAL_NET_OK : HAL_NET_ERR;
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms) {
    lwip_hal_mqtt_slot_t *slot = mqtt_slot(client);
    if (!slot || !topic) return HAL_NET_ERR;
    slot->sub_done = 0;
    slot->sub_failed = 0;
    err_t err = mqtt_sub_unsub(slot->client, topic, (u8_t)qos, mqtt_sub_cb,
                               slot, 1);
    if (err != ERR_OK) return HAL_NET_ERR;
    return mqtt_wait_done(&slot->sub_done, &slot->sub_failed, timeout_ms);
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms) {
    lwip_hal_mqtt_slot_t *slot = mqtt_slot(client);
    if (!slot || !topic) return HAL_NET_ERR;
    slot->unsub_done = 0;
    slot->unsub_failed = 0;
    err_t err = mqtt_sub_unsub(slot->client, topic, 0, mqtt_unsub_cb, slot, 0);
    if (err != ERR_OK) return HAL_NET_ERR;
    return mqtt_wait_done(&slot->unsub_done, &slot->unsub_failed, timeout_ms);
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len) {
    if (topic && topic_cap) topic[0] = 0;
    if (payload_len) *payload_len = 0;
    lwip_hal_mqtt_slot_t *slot = mqtt_slot(client);
    if (!slot || !payload || payload_cap == 0) return HAL_NET_ERR;
    if (!slot->pending) return HAL_NET_WOULD_BLOCK;

    copy_string(topic, topic_cap, slot->pending_topic);
    size_t copy = slot->payload_len;
    if (copy > payload_cap) copy = payload_cap;
    memcpy(payload, slot->payload, copy);
    if (payload_len) *payload_len = copy;
    slot->pending = 0;
    slot->payload_len = 0;
    return HAL_NET_OK;
}

int hal_net_mqtt_close(hal_net_mqtt_client_t client) {
    lwip_hal_mqtt_slot_t *slot = mqtt_slot(client);
    if (!slot) return HAL_NET_ERR;
    mqtt_disconnect(slot->client);
    mqtt_client_free(slot->client);
    memset(slot, 0, sizeof(*slot));
    return HAL_NET_OK;
}
