/*
 * esp32_wifi.c — ESP-IDF native WiFi support for the ESP32-S3 port.
 *
 * This intentionally does not reuse MMsetwifi.c: that file is tied to
 * the Pico W CYW43 arch layer and lwIP raw callbacks. The user-visible
 * BASIC surface is kept compatible where the ESP32 port can support it.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "OptionCommands.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_options.h"
#include "shared/net/mm_net_web_cmd.h"
#include "shared/net/mm_net_wifi_cmd.h"
#include "esp32_mqtt.h"
#include "esp32_ntp.h"
#include "esp32_tcp_client.h"
#include "esp32_tcp_server.h"
#include "esp32_telnet.h"
#include "esp32_tftp.h"
#include "esp32_udp.h"

static const char *TAG = "mmbasic_wifi";

volatile int WIFIconnected = 0;
int startupcomplete = 0;

void WebConnect(void);

void ProcessWeb(int mode)
{
    static const mm_net_lifecycle_poll_hooks_t hooks = {
        .poll_udp = esp32_udp_poll,
        .poll_tftp = esp32_tftp_poll,
        .poll_mqtt = esp32_mqtt_poll,
        .poll_tcp_server = esp32_tcp_server_poll,
        .poll_telnet = esp32_telnet_poll,
    };
    mm_net_lifecycle_poll(&hooks, mode, 1);
}

static void esp32_wifi_default_hostname(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(out, out_len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "ESP32");
    }
}

static void esp32_validate_static_ip(
    const mm_net_wifi_credentials_t *credentials)
{
    esp_ip4_addr_t ipaddr;
    if (esp_netif_str_to_ip4(credentials->ipaddress, &ipaddr) != ESP_OK)
        error("Invalid IP address");
    if (esp_netif_str_to_ip4(credentials->mask, &ipaddr) != ESP_OK)
        error("Invalid mask address");
    if (esp_netif_str_to_ip4(credentials->gateway, &ipaddr) != ESP_OK)
        error("Invalid gateway address");
}

static void esp32_wifi_set_credentials(unsigned char *tp)
{
    char default_hostname[32];
    esp32_wifi_default_hostname(default_hostname, sizeof default_hostname);
    mm_net_lifecycle_store_wifi_credentials(tp, default_hostname,
                                            esp32_validate_static_ip);
}

static mm_net_lifecycle_result_t esp32_lifecycle_apply_wifi(unsigned char *arg)
{
    esp32_wifi_set_credentials(arg);
    WebConnect();
    return MM_NET_LIFECYCLE_OK;
}

static int esp32_lifecycle_open_web_console(void)
{
    /* Auto-start WiFi if needed; the actual listening socket is the
     * shared TCP server, which esp32_web_console_open() brings up. */
    if (!WIFIconnected) WebConnect();
    return esp32_web_console_open();
}

static const mm_net_lifecycle_hooks_t esp32_lifecycle_hooks = {
    .apply_wifi = esp32_lifecycle_apply_wifi,
    .open_tcp_server = esp32_tcp_server_open,
    .close_tcp_server = esp32_tcp_server_stop,
    .open_udp_server = esp32_udp_server_open,
    .close_udp_server = esp32_udp_server_stop,
    .open_tftp = esp32_tftp_server_open,
    .close_tftp = esp32_tftp_server_stop,
    .open_telnet = esp32_telnet_open,
    .close_telnet = esp32_telnet_close,
    .open_web_console = esp32_lifecycle_open_web_console,
    .close_web_console = esp32_web_console_close,
};

static void esp32_info_before_query(void)
{
    ProcessWeb(0);
}

void WebConnect(void)
{
    static const mm_net_lifecycle_wifi_connect_t connect = {
        .hooks = &esp32_lifecycle_hooks,
        .default_hostname = "ESP32",
        .no_ssid_timeout_ms = 30000,
        .no_ssid_failure_message = "WiFi init failed\r\n",
    };
    (void)mm_net_lifecycle_wifi_connect(&connect);
}

void port_repl_wifi_arch_init_and_connect(void)
{
    /*
     * Keep ESP-IDF WiFi out of the early REPL boot path. The ESP32 USB
     * Serial/JTAG console is the only control channel on this port, so
     * WiFi is initialized lazily by OPTION WIFI / WEB / MM.INFO instead
     * of risking a pre-prompt failure.
     */
}

int esp32_wifi_option_setter(unsigned char *cmdline)
{
    return mm_net_lifecycle_handle_option_result(
        mm_net_lifecycle_option_setter(cmdline, &esp32_lifecycle_hooks),
        NULL);
}

int esp32_wifi_get_ssid(unsigned char *out_sret, int *out_targ)
{
    strcpy((char *)out_sret, (char *)Option.SSID);
    CtoM(out_sret);
    *out_targ = T_STR;
    return 1;
}

int esp32_wifi_mminfo(unsigned char *ep, int64_t *out_iret,
                      unsigned char *out_sret, int *out_targ)
{
    const mm_net_info_hooks_t hooks = {
        .before_query = esp32_info_before_query,
        .tcp_path = esp32_tcp_server_path,
        .tcp_request_pending = esp32_tcp_server_request_pending,
        .max_connections = esp32_tcp_server_max_connections(),
        .tcp_port = Option.TCP_PORT,
        .udp_port = Option.UDP_PORT,
        .ip_address = hal_net_ip_address,
        .wifi_status = hal_net_wifi_status,
        .tcpip_status = hal_net_tcpip_status,
    };
    return mm_net_mminfo(ep, out_iret, out_sret, out_targ, &hooks);
}

static void esp32_wifi_scan(unsigned char *arg)
{
    mm_net_wifi_scan_command(arg);
}

static void esp32_web_connect_cmd(unsigned char *arg)
{
    if (*arg) esp32_wifi_set_credentials(arg);
    WebConnect();
}

static int esp32_web_is_connected(void)
{
    return WIFIconnected ? 1 : 0;
}

void cmd_web(void)
{
    static const mm_net_web_dispatch_t dispatch = {
        .connect = esp32_web_connect_cmd,
        .scan = esp32_wifi_scan,
        .is_connected = esp32_web_is_connected,
        .not_connected_error = "WIFI not connected",
        .mqtt = esp32_mqtt_cmd,
        .tcp_client = esp32_tcp_client_cmd,
        .transmit = esp32_transmit_cmd,
        .tcp_server = esp32_tcp_cmd,
        .ntp = esp32_ntp_cmd,
        .udp = esp32_udp_cmd,
    };
    mm_net_web_dispatch(cmdline, &dispatch);
}

void esp32_wifi_print_options(void)
{
    mm_net_print_wifi_option((char *)Option.SSID, (char *)Option.PASSWORD,
                             Option.hostname, Option.ipaddress, Option.mask,
                             Option.gateway);
    mm_net_print_options(Option.TCP_PORT, Option.ServerResponceTime,
                         Option.UDP_PORT, Option.UDPServerResponceTime,
                         optionsuppressstatus);
    mm_net_print_service_options((int)Option.Telnet, Option.disabletftp);
    mm_net_print_web_console_option(Option.WebConsole);
}
