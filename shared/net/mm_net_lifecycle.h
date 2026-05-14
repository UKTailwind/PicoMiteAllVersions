/*
 * shared/net/mm_net_lifecycle.h - shared WEB lifecycle policy.
 */

#ifndef MM_NET_LIFECYCLE_H
#define MM_NET_LIFECYCLE_H

#include <stdint.h>

#include "shared/net/mm_net_wifi_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MM_NET_LIFECYCLE_OK = 0,
    MM_NET_LIFECYCLE_NOT_HANDLED,
    MM_NET_LIFECYCLE_UNSUPPORTED,
    MM_NET_LIFECYCLE_REBOOT_REQUIRED,
    MM_NET_LIFECYCLE_ERROR,
} mm_net_lifecycle_result_t;

typedef enum {
    MM_NET_LIFECYCLE_WIFI_CONNECT = 0,
    MM_NET_LIFECYCLE_WIFI_SCAN,
    MM_NET_LIFECYCLE_TCP_CLIENT,
    MM_NET_LIFECYCLE_TCP_SERVER,
    MM_NET_LIFECYCLE_TCP_STREAM,
    MM_NET_LIFECYCLE_UDP,
    MM_NET_LIFECYCLE_NTP,
    MM_NET_LIFECYCLE_TFTP,
    MM_NET_LIFECYCLE_TELNET,
    MM_NET_LIFECYCLE_MQTT,
    MM_NET_LIFECYCLE_WEB_CONSOLE,
} mm_net_lifecycle_service_t;

typedef struct {
    mm_net_lifecycle_result_t (*apply_wifi)(unsigned char *arg);
    int (*open_tcp_server)(uint16_t port);
    void (*close_tcp_server)(void);
    int (*open_udp_server)(uint16_t port);
    void (*close_udp_server)(void);
    int (*open_tftp)(void);
    void (*close_tftp)(void);
    int (*open_telnet)(void);
    void (*close_telnet)(void);
    int (*open_web_console)(void);
    void (*close_web_console)(void);
    unsigned reboot_after_option_mask;
} mm_net_lifecycle_hooks_t;

typedef struct {
    const mm_net_lifecycle_hooks_t *hooks;
    const char *default_hostname;
    uint32_t no_ssid_timeout_ms;
    const char *no_ssid_failure_message;
} mm_net_lifecycle_wifi_connect_t;

typedef void (*mm_net_lifecycle_wifi_validate_t)(
    const mm_net_wifi_credentials_t *credentials);

typedef struct {
    void (*reboot_required)(void);
    char *unsupported_error;
    char *apply_error;
} mm_net_lifecycle_result_handler_t;

typedef struct {
    void (*clear_tcp_requests)(void);
    void (*close_tcp_client)(void);
    void (*close_mqtt)(void);
    void (*close_tftp_session)(void);
    void (*close_telnet_session)(void);
} mm_net_lifecycle_runtime_hooks_t;

typedef struct {
    void (*poll_udp)(void);
    void (*poll_tftp)(void);
    void (*poll_tcp_client_stream)(void);
    void (*poll_mqtt)(void);
    void (*poll_tcp_server)(void);
    void (*poll_telnet)(int mode);
} mm_net_lifecycle_poll_hooks_t;

enum {
    MM_NET_LIFECYCLE_REBOOT_WIFI = 1u << 0,
    MM_NET_LIFECYCLE_REBOOT_TCP = 1u << 1,
    MM_NET_LIFECYCLE_REBOOT_UDP = 1u << 2,
    MM_NET_LIFECYCLE_REBOOT_TFTP = 1u << 3,
    MM_NET_LIFECYCLE_REBOOT_TELNET = 1u << 4,
    MM_NET_LIFECYCLE_REBOOT_WEB_CONSOLE = 1u << 5,
};

int mm_net_lifecycle_service_supported(mm_net_lifecycle_service_t service);
const char *mm_net_lifecycle_unsupported_message(
    mm_net_lifecycle_service_t service);

mm_net_lifecycle_result_t mm_net_lifecycle_on_network_ready(
    const mm_net_lifecycle_hooks_t *hooks);
void mm_net_lifecycle_on_network_down(
    const mm_net_lifecycle_hooks_t *hooks);
void mm_net_lifecycle_store_wifi_credentials(
    unsigned char *arg, const char *default_hostname,
    mm_net_lifecycle_wifi_validate_t validate);
mm_net_lifecycle_result_t mm_net_lifecycle_wifi_connect(
    const mm_net_lifecycle_wifi_connect_t *connect);

mm_net_lifecycle_result_t mm_net_lifecycle_option_setter(
    unsigned char *cmdline, const mm_net_lifecycle_hooks_t *hooks);
int mm_net_lifecycle_handle_option_result(
    mm_net_lifecycle_result_t result,
    const mm_net_lifecycle_result_handler_t *handler);
void mm_net_lifecycle_runtime_reset(
    const mm_net_lifecycle_runtime_hooks_t *hooks);
void mm_net_lifecycle_poll(
    const mm_net_lifecycle_poll_hooks_t *hooks, int mode,
    int require_network_ready);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_LIFECYCLE_H */
