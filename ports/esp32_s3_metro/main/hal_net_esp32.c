/*
 * ports/esp32_s3_metro/main/hal_net_esp32.c - ESP-IDF socket network HAL.
 *
 * This is the transport-facing slice of the ESP32 network backend. The
 * BASIC-visible WEB command surface still lives in esp32_wifi.c during the
 * migration; shared network code can use this file through hal_net.h.
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "hal/hal_net.h"

#define ESP32_NET_MAX_TCP_SERVERS 4
#define ESP32_NET_MAX_TCP_CONNS   12
#define ESP32_NET_MAX_TCP_CLIENTS 8
#define ESP32_NET_MAX_UDP_SOCKS   8
#define ESP32_NET_MAX_MQTT_CLIENTS 4
#define ESP32_NET_MQTT_TOPIC_MAX 256
#define ESP32_NET_MQTT_PAYLOAD_MAX 256
#define ESP32_NET_WIFI_MAX_RETRIES 5

extern volatile int WIFIconnected;
extern int startupcomplete;

typedef struct {
    esp_mqtt_client_handle_t client;
    volatile int connected;
    volatile int subscribed;
    volatile int unsubscribed;
    volatile int pending;
    char host[256];
    char user[256];
    char pass[256];
    char client_id[64];
    char topic[ESP32_NET_MQTT_TOPIC_MAX];
    uint8_t payload[ESP32_NET_MQTT_PAYLOAD_MAX];
    size_t payload_len;
} esp32_net_mqtt_slot_t;

static int tcp_servers[ESP32_NET_MAX_TCP_SERVERS];
static int tcp_conns[ESP32_NET_MAX_TCP_CONNS];
static int tcp_clients[ESP32_NET_MAX_TCP_CLIENTS];
static int udp_socks[ESP32_NET_MAX_UDP_SOCKS];
static esp32_net_mqtt_slot_t mqtt_clients[ESP32_NET_MAX_MQTT_CLIENTS];
static int tables_ready;

static int wifi_ready;
static int wifi_started;
static int wifi_retry_count;
static int wifi_last_status;
static esp_netif_t *wifi_sta_netif;
static char wifi_ssid[64];
static char wifi_pass[64];
static char wifi_host[32];
static char wifi_ip[16];
static char wifi_mask[16];
static char wifi_gw[16];

static void esp32_net_init_tables(void)
{
    if (tables_ready) return;
    for (size_t i = 0; i < ESP32_NET_MAX_TCP_SERVERS; ++i) tcp_servers[i] = -1;
    for (size_t i = 0; i < ESP32_NET_MAX_TCP_CONNS; ++i) tcp_conns[i] = -1;
    for (size_t i = 0; i < ESP32_NET_MAX_TCP_CLIENTS; ++i) tcp_clients[i] = -1;
    for (size_t i = 0; i < ESP32_NET_MAX_UDP_SOCKS; ++i) udp_socks[i] = -1;
    memset(mqtt_clients, 0, sizeof mqtt_clients);
    tables_ready = 1;
}

static int esp32_net_set_nonblock(int fd, int enabled)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return HAL_NET_ERR;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0 ? HAL_NET_OK : HAL_NET_ERR;
}

static uint16_t esp32_net_alloc(int *slots, size_t count, int fd)
{
    esp32_net_init_tables();
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] < 0) {
            slots[i] = fd;
            return (uint16_t)(i + 1);
        }
    }
    return 0;
}

static int *esp32_net_slot(int *slots, size_t count, uint16_t handle)
{
    if (handle == 0 || handle > count) return NULL;
    if (slots[handle - 1] < 0) return NULL;
    return &slots[handle - 1];
}

static int esp32_net_close_slot(int *slots, size_t count, uint16_t handle)
{
    int *slot = esp32_net_slot(slots, count, handle);
    if (!slot) return HAL_NET_ERR;
    shutdown(*slot, SHUT_RDWR);
    close(*slot);
    *slot = -1;
    return HAL_NET_OK;
}

static int esp32_net_wait_fd(int fd, int for_write, uint32_t timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms) {
        tv.tv_sec = (time_t)(timeout_ms / 1000);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
        tvp = &tv;
    }

    int rc = select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL,
                    NULL, tvp);
    if (rc > 0) return HAL_NET_OK;
    if (rc == 0) return HAL_NET_TIMEOUT;
    if (errno == EINTR) return HAL_NET_TIMEOUT;
    return HAL_NET_ERR;
}

static int esp32_net_send_all(int fd, const void *buf, size_t len,
                              uint32_t timeout_ms)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        int wait = esp32_net_wait_fd(fd, 1, timeout_ms);
        if (wait != HAL_NET_OK) return wait;
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        return HAL_NET_ERR;
    }
    return HAL_NET_OK;
}

static int esp32_net_resolve_addr(const char *host, uint16_t port, int socktype,
                                  struct addrinfo **out)
{
    char portbuf[16];
    snprintf(portbuf, sizeof portbuf, "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    return getaddrinfo(host, portbuf, &hints, out) == 0 ? HAL_NET_OK : HAL_NET_ERR;
}

static void esp32_net_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        WIFIconnected = 0;
        if (wifi_retry_count < ESP32_NET_WIFI_MAX_RETRIES) {
            wifi_retry_count++;
            esp_wifi_connect();
        } else {
            wifi_last_status = -1;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        (void)event_data;
        wifi_retry_count = 0;
        wifi_last_status = 1;
        WIFIconnected = 1;
    }
}

static int esp32_net_wifi_ensure_ready(void)
{
    if (wifi_ready) return HAL_NET_OK;

    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("pp", ESP_LOG_WARN);
    esp_log_level_set("net80211", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return HAL_NET_ERR;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return HAL_NET_ERR;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return HAL_NET_ERR;

    wifi_sta_netif = esp_netif_create_default_wifi_sta();
    if (!wifi_sta_netif) return HAL_NET_ERR;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return HAL_NET_ERR;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            esp32_net_wifi_event_handler, NULL,
                                            NULL) != ESP_OK) return HAL_NET_ERR;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            esp32_net_wifi_event_handler, NULL,
                                            NULL) != ESP_OK) return HAL_NET_ERR;

    wifi_ready = 1;
    startupcomplete = 1;
    return HAL_NET_OK;
}

static void esp32_net_wifi_apply_ip_config(void)
{
    if (!wifi_sta_netif) return;

    if (*wifi_host) esp_netif_set_hostname(wifi_sta_netif, wifi_host);

    if (*wifi_ip) {
        esp_netif_ip_info_t info;
        memset(&info, 0, sizeof info);
        esp_netif_str_to_ip4(wifi_ip, &info.ip);
        esp_netif_str_to_ip4(wifi_mask, &info.netmask);
        esp_netif_str_to_ip4(wifi_gw, &info.gw);
        esp_netif_dhcpc_stop(wifi_sta_netif);
        esp_netif_set_ip_info(wifi_sta_netif, &info);
    } else {
        esp_netif_dhcpc_start(wifi_sta_netif);
    }
}

static void esp32_net_copy_string(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = 0;
}

static hal_net_mqtt_client_t esp32_net_alloc_mqtt_slot(void)
{
    esp32_net_init_tables();
    for (size_t i = 0; i < ESP32_NET_MAX_MQTT_CLIENTS; ++i) {
        if (!mqtt_clients[i].client) {
            memset(&mqtt_clients[i], 0, sizeof mqtt_clients[i]);
            return (hal_net_mqtt_client_t)(i + 1);
        }
    }
    return 0;
}

static esp32_net_mqtt_slot_t *esp32_net_mqtt_slot(hal_net_mqtt_client_t handle)
{
    if (handle == 0 || handle > ESP32_NET_MAX_MQTT_CLIENTS) return NULL;
    if (!mqtt_clients[handle - 1].client) return NULL;
    return &mqtt_clients[handle - 1];
}

static void esp32_net_mqtt_make_client_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(out, out_len, "WebMite%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "WebMiteESP32");
    }
}

static int esp32_net_wait_flag(volatile int *flag, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    do {
        if (*flag) return HAL_NET_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    } while (waited < timeout_ms);
    return *flag ? HAL_NET_OK : HAL_NET_TIMEOUT;
}

static void esp32_net_mqtt_event_handler(void *handler_args,
                                         esp_event_base_t base,
                                         int32_t event_id,
                                         void *event_data)
{
    (void)base;
    esp32_net_mqtt_slot_t *slot = (esp32_net_mqtt_slot_t *)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (!slot || !event) return;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            slot->connected = 1;
            break;
        case MQTT_EVENT_DISCONNECTED:
            slot->connected = 0;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            slot->subscribed = 1;
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            slot->unsubscribed = 1;
            break;
        case MQTT_EVENT_DATA: {
            int tlen = event->topic_len;
            int dlen = event->data_len;
            if (tlen < 0) tlen = 0;
            if (dlen < 0) dlen = 0;
            if (tlen >= ESP32_NET_MQTT_TOPIC_MAX)
                tlen = ESP32_NET_MQTT_TOPIC_MAX - 1;
            if (dlen > ESP32_NET_MQTT_PAYLOAD_MAX)
                dlen = ESP32_NET_MQTT_PAYLOAD_MAX;
            memcpy(slot->topic, event->topic, (size_t)tlen);
            slot->topic[tlen] = 0;
            memcpy(slot->payload, event->data, (size_t)dlen);
            slot->payload_len = (size_t)dlen;
            slot->pending = 1;
            break;
        }
        default:
            break;
    }
}

uint32_t hal_net_capabilities(void)
{
    return HAL_NET_CAP_TCP_SERVER |
           HAL_NET_CAP_TCP_CLIENT |
           HAL_NET_CAP_TCP_STREAM |
           HAL_NET_CAP_UDP_SERVER |
           HAL_NET_CAP_UDP_SEND |
           HAL_NET_CAP_WIFI_SCAN |
           HAL_NET_CAP_WIFI_CONNECT |
           HAL_NET_CAP_MQTT_PLAIN;
}

int hal_net_init(void)
{
    esp32_net_init_tables();
    return HAL_NET_OK;
}

void hal_net_poll(void)
{
}

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw)
{
    esp32_net_copy_string(wifi_ssid, sizeof wifi_ssid, ssid);
    esp32_net_copy_string(wifi_pass, sizeof wifi_pass, pass);
    esp32_net_copy_string(wifi_host, sizeof wifi_host, host);
    esp32_net_copy_string(wifi_ip, sizeof wifi_ip, ip);
    esp32_net_copy_string(wifi_mask, sizeof wifi_mask, mask);
    esp32_net_copy_string(wifi_gw, sizeof wifi_gw, gw);
    return HAL_NET_OK;
}

int hal_net_wifi_connect(uint32_t timeout_ms)
{
    if (esp32_net_wifi_ensure_ready() != HAL_NET_OK) {
        WIFIconnected = 0;
        wifi_last_status = -1;
        return HAL_NET_ERR;
    }

    if (!*wifi_ssid) {
        if (!wifi_started) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            if (esp_wifi_start() == ESP_OK) wifi_started = 1;
        }
        return HAL_NET_OK;
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof wifi_config);
    strncpy((char *)wifi_config.sta.ssid, wifi_ssid,
            sizeof wifi_config.sta.ssid - 1);
    strncpy((char *)wifi_config.sta.password, wifi_pass,
            sizeof wifi_config.sta.password - 1);
    wifi_config.sta.threshold.authmode = *wifi_pass ? WIFI_AUTH_WPA2_PSK
                                                    : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    wifi_retry_count = 0;
    wifi_last_status = 0;
    WIFIconnected = 0;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp32_net_wifi_apply_ip_config();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    if (!wifi_started) {
        if (esp_wifi_start() == ESP_OK) wifi_started = 1;
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
    }

    uint32_t waited = 0;
    uint32_t limit = timeout_ms ? timeout_ms : 30000;
    char ipbuf[32];
    while (waited < limit) {
        if (WIFIconnected && hal_net_ip_address(ipbuf, sizeof ipbuf) == HAL_NET_OK)
            return HAL_NET_OK;
        if (wifi_last_status < 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    WIFIconnected = 0;
    wifi_last_status = -1;
    return HAL_NET_TIMEOUT;
}

int hal_net_wifi_status(void)
{
    return WIFIconnected ? 1 : wifi_last_status;
}

int hal_net_tcpip_status(void)
{
    return WIFIconnected ? 1 : wifi_last_status;
}

int hal_net_ip_address(char *out, size_t out_len)
{
    if (!out || out_len == 0) return HAL_NET_ERR;
    out[0] = 0;

    esp_netif_t *netif = wifi_sta_netif;
    if (!netif) netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return HAL_NET_ERR;

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0)
        return HAL_NET_ERR;
    esp_ip4addr_ntoa(&info.ip, out, (int)out_len);
    return HAL_NET_OK;
}

int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console)
{
    (void)print_to_console;
    if (out && out_len) out[0] = 0;
    if (written) *written = 0;

    if (!out || out_len == 0) return HAL_NET_ERR;
    if (esp32_net_wifi_ensure_ready() != HAL_NET_OK) return HAL_NET_ERR;

    esp_wifi_set_mode(WIFI_MODE_STA);
    if (!wifi_started) {
        if (esp_wifi_start() == ESP_OK) wifi_started = 1;
    }

    wifi_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof scan_config);
    if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) return HAL_NET_ERR;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *records = NULL;
    if (ap_count) {
        records = malloc(sizeof(*records) * ap_count);
        if (!records) return HAL_NET_ERR;
        if (esp_wifi_scan_get_ap_records(&ap_count, records) != ESP_OK) {
            free(records);
            return HAL_NET_ERR;
        }
    }

    size_t used = 0;
    for (uint16_t i = 0; i < ap_count; i++) {
        char line[160];
        int n = snprintf(line, sizeof line,
                         "ssid: %-32s rssi: %4d chan: %3d mac: %02x:%02x:%02x:%02x:%02x:%02x sec: %u\r\n",
                         (char *)records[i].ssid, records[i].rssi,
                         records[i].primary,
                         records[i].bssid[0], records[i].bssid[1],
                         records[i].bssid[2], records[i].bssid[3],
                         records[i].bssid[4], records[i].bssid[5],
                         records[i].authmode);
        if (n < 0) {
            free(records);
            return HAL_NET_ERR;
        }
        if (used + (size_t)n + 1 > out_len) {
            free(records);
            return HAL_NET_ERR;
        }
        memcpy(out + used, line, (size_t)n);
        used += (size_t)n;
        out[used] = 0;
    }
    free(records);
    if (written) *written = used;
    return HAL_NET_OK;
}

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out)
{
    if (!out) return HAL_NET_ERR;
    *out = 0;
    esp32_net_init_tables();

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return HAL_NET_ERR;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(fd, backlog > 0 ? backlog : 1) != 0 ||
        esp32_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }

    uint16_t handle = esp32_net_alloc(tcp_servers, ESP32_NET_MAX_TCP_SERVERS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_server_close(hal_net_tcp_server_t server)
{
    return esp32_net_close_slot(tcp_servers, ESP32_NET_MAX_TCP_SERVERS, server);
}

int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t *conn)
{
    if (conn) *conn = 0;
    int *slot = esp32_net_slot(tcp_servers, ESP32_NET_MAX_TCP_SERVERS, server);
    if (!slot) return HAL_NET_ERR;

    int fd = accept(*slot, NULL, NULL);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return HAL_NET_WOULD_BLOCK;
        return HAL_NET_ERR;
    }

    if (esp32_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }

    uint16_t handle = esp32_net_alloc(tcp_conns, ESP32_NET_MAX_TCP_CONNS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    if (conn) *conn = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len)
{
    if (conn) *conn = 0;
    if (len) *len = 0;
    hal_net_tcp_conn_t handle = 0;
    int rc = hal_net_tcp_accept_conn(server, &handle);
    if (rc != HAL_NET_OK) return rc;
    if (conn) *conn = handle;

    if (buf && cap) {
        int *conn_slot = esp32_net_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS,
                                        handle);
        if (!conn_slot) return HAL_NET_ERR;
        int fd = *conn_slot;
        int wait = esp32_net_wait_fd(fd, 0, 250);
        if (wait == HAL_NET_OK) {
            ssize_t n = recv(fd, buf, cap, 0);
            if (n > 0 && len) *len = (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                esp32_net_close_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS,
                                     handle);
                return HAL_NET_ERR;
            }
        } else if (wait != HAL_NET_TIMEOUT) {
            esp32_net_close_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS, handle);
            return wait;
        }
    }
    return HAL_NET_OK;
}

int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void *buf, size_t cap,
                          size_t *len)
{
    if (len) *len = 0;
    int *slot = esp32_net_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS, conn);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    ssize_t n = recv(*slot, buf, cap, 0);
    if (n > 0) {
        if (len) *len = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_ERR;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_conn_send_some(hal_net_tcp_conn_t conn, const void *buf,
                               size_t cap, size_t *sent)
{
    if (sent) *sent = 0;
    int *slot = esp32_net_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS, conn);
    if (!slot || (!buf && cap)) return HAL_NET_ERR;
    if (cap == 0) return HAL_NET_OK;
    ssize_t n = send(*slot, buf, cap, 0);
    if (n > 0) {
        if (sent) *sent = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_WOULD_BLOCK;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms)
{
    int *slot = esp32_net_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS, conn);
    if (!slot) return HAL_NET_ERR;
    return esp32_net_send_all(*slot, buf, len, timeout_ms);
}

int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn)
{
    return esp32_net_close_slot(tcp_conns, ESP32_NET_MAX_TCP_CONNS, conn);
}

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out)
{
    if (!host || !out) return HAL_NET_ERR;
    *out = 0;
    esp32_net_init_tables();

    struct addrinfo *ai = NULL;
    if (esp32_net_resolve_addr(host, port, SOCK_STREAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;

    int fd = -1;
    int ok = 0;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        esp32_net_set_nonblock(fd, 1);
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            ok = 1;
        } else if (errno == EINPROGRESS) {
            int wait = esp32_net_wait_fd(fd, 1, timeout_ms);
            if (wait == HAL_NET_OK) {
                int so_error = 0;
                socklen_t so_len = sizeof so_error;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) == 0 &&
                    so_error == 0) {
                    ok = 1;
                }
            }
        }
        if (ok) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(ai);
    if (!ok || fd < 0) return HAL_NET_TIMEOUT;

    esp32_net_set_nonblock(fd, 0);
    uint16_t handle = esp32_net_alloc(tcp_clients, ESP32_NET_MAX_TCP_CLIENTS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms)
{
    int *slot = esp32_net_slot(tcp_clients, ESP32_NET_MAX_TCP_CLIENTS, client);
    if (!slot) return HAL_NET_ERR;
    return esp32_net_send_all(*slot, buf, len, timeout_ms);
}

int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms)
{
    if (len) *len = 0;
    int *slot = esp32_net_slot(tcp_clients, ESP32_NET_MAX_TCP_CLIENTS, client);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    int wait = esp32_net_wait_fd(*slot, 0, timeout_ms);
    if (wait != HAL_NET_OK) return wait;
    ssize_t n = recv(*slot, buf, cap, 0);
    if (n > 0) {
        if (len) *len = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_OK;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_client_close(hal_net_tcp_client_t client)
{
    return esp32_net_close_slot(tcp_clients, ESP32_NET_MAX_TCP_CLIENTS, client);
}

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out)
{
    if (!out) return HAL_NET_ERR;
    *out = 0;
    esp32_net_init_tables();

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) return HAL_NET_ERR;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        esp32_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }

    uint16_t handle = esp32_net_alloc(udp_socks, ESP32_NET_MAX_UDP_SOCKS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_udp_close(hal_net_udp_socket_t sock)
{
    return esp32_net_close_slot(udp_socks, ESP32_NET_MAX_UDP_SOCKS, sock);
}

int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port, const void *buf, size_t len,
                            uint32_t timeout_ms)
{
    if (!host || (!buf && len)) return HAL_NET_ERR;
    int *slot = esp32_net_slot(udp_socks, ESP32_NET_MAX_UDP_SOCKS, sock);
    if (!slot) return HAL_NET_ERR;

    struct addrinfo *ai = NULL;
    if (esp32_net_resolve_addr(host, port, SOCK_DGRAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;

    int result = HAL_NET_ERR;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        int wait = esp32_net_wait_fd(*slot, 1, timeout_ms);
        if (wait == HAL_NET_OK || wait == HAL_NET_TIMEOUT) {
            ssize_t n = sendto(*slot, buf, len, 0, rp->ai_addr, rp->ai_addrlen);
            if (n == (ssize_t)len) result = HAL_NET_OK;
        }
        if (result == HAL_NET_OK) break;
    }
    freeaddrinfo(ai);
    return result;
}

int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms)
{
    if (!host || (!buf && len)) return HAL_NET_ERR;
    struct addrinfo *ai = NULL;
    if (esp32_net_resolve_addr(host, port, SOCK_DGRAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;

    int result = HAL_NET_ERR;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
        int wait = esp32_net_wait_fd(fd, 1, timeout_ms);
        if (wait == HAL_NET_OK || wait == HAL_NET_TIMEOUT) {
            ssize_t n = sendto(fd, buf, len, 0, rp->ai_addr, rp->ai_addrlen);
            if (n == (ssize_t)len) result = HAL_NET_OK;
        }
        close(fd);
        if (result == HAL_NET_OK) break;
    }
    freeaddrinfo(ai);
    return result;
}

int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len)
{
    if (len) *len = 0;
    if (from) memset(from, 0, sizeof *from);
    int *slot = esp32_net_slot(udp_socks, ESP32_NET_MAX_UDP_SOCKS, sock);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;

    struct sockaddr_storage src;
    socklen_t src_len = sizeof src;
    ssize_t n = recvfrom(*slot, buf, cap, 0, (struct sockaddr *)&src, &src_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return HAL_NET_WOULD_BLOCK;
        return HAL_NET_ERR;
    }

    if (len) *len = (size_t)n;
    if (from && src.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&src;
        from->family = 4;
        from->port = ntohs(sin->sin_port);
        memcpy(from->bytes, &sin->sin_addr, 4);
    } else if (from && src.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&src;
        from->family = 6;
        from->port = ntohs(sin6->sin6_port);
        memcpy(from->bytes, &sin6->sin6_addr, 16);
    }
    return HAL_NET_OK;
}

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out)
{
    if (out) *out = 0;
    if (!host || !out) return HAL_NET_ERR;

    hal_net_mqtt_client_t handle = esp32_net_alloc_mqtt_slot();
    if (!handle) return HAL_NET_ERR;
    esp32_net_mqtt_slot_t *slot = &mqtt_clients[handle - 1];

    esp32_net_copy_string(slot->host, sizeof slot->host, host);
    esp32_net_copy_string(slot->user, sizeof slot->user, user);
    esp32_net_copy_string(slot->pass, sizeof slot->pass, pass);
    if (client_id && *client_id) {
        esp32_net_copy_string(slot->client_id, sizeof slot->client_id, client_id);
    } else {
        esp32_net_mqtt_make_client_id(slot->client_id, sizeof slot->client_id);
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.hostname = slot->host,
        .broker.address.port = (uint32_t)port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = *slot->user ? slot->user : NULL,
        .credentials.client_id = slot->client_id,
        .credentials.authentication.password = *slot->pass ? slot->pass : NULL,
        .network.disable_auto_reconnect = true,
        .network.timeout_ms = timeout_ms ? (int)timeout_ms : 5000,
        .session.keepalive = 100,
    };

    slot->client = esp_mqtt_client_init(&cfg);
    if (!slot->client) {
        memset(slot, 0, sizeof *slot);
        return HAL_NET_ERR;
    }
    esp_mqtt_client_register_event(slot->client, MQTT_EVENT_ANY,
                                   esp32_net_mqtt_event_handler, slot);
    if (esp_mqtt_client_start(slot->client) != ESP_OK) {
        esp_mqtt_client_destroy(slot->client);
        memset(slot, 0, sizeof *slot);
        return HAL_NET_ERR;
    }
    int rc = esp32_net_wait_flag(&slot->connected,
                                 timeout_ms ? timeout_ms : 5000);
    if (rc != HAL_NET_OK) {
        esp_mqtt_client_stop(slot->client);
        esp_mqtt_client_destroy(slot->client);
        memset(slot, 0, sizeof *slot);
        return rc;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos, int retain)
{
    esp32_net_mqtt_slot_t *slot = esp32_net_mqtt_slot(client);
    if (!slot || !slot->connected || !topic || (!payload && len)) return HAL_NET_ERR;
    int id = esp_mqtt_client_publish(slot->client, topic, (const char *)payload,
                                     (int)len, qos, retain);
    return id < 0 ? HAL_NET_ERR : HAL_NET_OK;
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms)
{
    esp32_net_mqtt_slot_t *slot = esp32_net_mqtt_slot(client);
    if (!slot || !slot->connected || !topic) return HAL_NET_ERR;
    slot->subscribed = 0;
    if (esp_mqtt_client_subscribe(slot->client, topic, qos) < 0)
        return HAL_NET_ERR;
    return esp32_net_wait_flag(&slot->subscribed,
                               timeout_ms ? timeout_ms : 4000);
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms)
{
    esp32_net_mqtt_slot_t *slot = esp32_net_mqtt_slot(client);
    if (!slot || !slot->connected || !topic) return HAL_NET_ERR;
    slot->unsubscribed = 0;
    if (esp_mqtt_client_unsubscribe(slot->client, topic) < 0)
        return HAL_NET_ERR;
    return esp32_net_wait_flag(&slot->unsubscribed,
                               timeout_ms ? timeout_ms : 4000);
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len)
{
    esp32_net_mqtt_slot_t *slot = esp32_net_mqtt_slot(client);
    if (topic && topic_cap) topic[0] = 0;
    if (payload_len) *payload_len = 0;
    if (!slot || !payload || payload_cap == 0) return HAL_NET_ERR;
    if (!slot->pending) return HAL_NET_WOULD_BLOCK;
    if (topic && topic_cap) {
        strncpy(topic, slot->topic, topic_cap - 1);
        topic[topic_cap - 1] = 0;
    }
    size_t copy = slot->payload_len < payload_cap ? slot->payload_len : payload_cap;
    memcpy(payload, slot->payload, copy);
    if (payload_len) *payload_len = copy;
    slot->pending = 0;
    return HAL_NET_OK;
}

int hal_net_mqtt_close(hal_net_mqtt_client_t client)
{
    esp32_net_mqtt_slot_t *slot = esp32_net_mqtt_slot(client);
    if (!slot) return HAL_NET_ERR;
    esp_mqtt_client_stop(slot->client);
    esp_mqtt_client_destroy(slot->client);
    memset(slot, 0, sizeof *slot);
    return HAL_NET_OK;
}
