/*
 * shared/net/mm_net_lifecycle.c - shared WEB lifecycle policy.
 */

#include <stdio.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_options.h"
#include "shared/net/mm_net_state.h"

extern bool optionsuppressstatus;
extern volatile int WIFIconnected;

static int caps_have(uint32_t required) {
    return (hal_net_capabilities() & required) == required;
}

int mm_net_lifecycle_service_supported(mm_net_lifecycle_service_t service) {
    switch (service) {
        case MM_NET_LIFECYCLE_WIFI_CONNECT:
            return caps_have(HAL_NET_CAP_WIFI_CONNECT);
        case MM_NET_LIFECYCLE_WIFI_SCAN:
            return caps_have(HAL_NET_CAP_WIFI_SCAN);
        case MM_NET_LIFECYCLE_TCP_CLIENT:
            return caps_have(HAL_NET_CAP_TCP_CLIENT) ||
                   caps_have(HAL_NET_CAP_HTTP_FETCH);
        case MM_NET_LIFECYCLE_TCP_SERVER:
            return caps_have(HAL_NET_CAP_TCP_SERVER);
        case MM_NET_LIFECYCLE_TCP_STREAM:
            return caps_have(HAL_NET_CAP_TCP_STREAM);
        case MM_NET_LIFECYCLE_UDP:
            return caps_have(HAL_NET_CAP_UDP_SERVER | HAL_NET_CAP_UDP_SEND);
        case MM_NET_LIFECYCLE_NTP:
            return caps_have(HAL_NET_CAP_UDP_SEND);
        case MM_NET_LIFECYCLE_TFTP:
            return caps_have(HAL_NET_CAP_UDP_SERVER | HAL_NET_CAP_UDP_SEND);
        case MM_NET_LIFECYCLE_TELNET:
            return caps_have(HAL_NET_CAP_TCP_SERVER);
        case MM_NET_LIFECYCLE_WEB_CONSOLE:
            return caps_have(HAL_NET_CAP_TCP_SERVER);
        case MM_NET_LIFECYCLE_MQTT:
            return caps_have(HAL_NET_CAP_MQTT_PLAIN) ||
                   caps_have(HAL_NET_CAP_MQTT_WEBSOCKET);
        default:
            return 0;
    }
}

const char *mm_net_lifecycle_unsupported_message(
    mm_net_lifecycle_service_t service) {
    switch (service) {
        case MM_NET_LIFECYCLE_WIFI_CONNECT:
        case MM_NET_LIFECYCLE_WIFI_SCAN:
            return "WiFi not supported";
        case MM_NET_LIFECYCLE_TCP_SERVER:
            return "TCP server not supported";
        case MM_NET_LIFECYCLE_TCP_STREAM:
            return "TCP stream not supported";
        case MM_NET_LIFECYCLE_UDP:
            return "UDP not supported";
        case MM_NET_LIFECYCLE_NTP:
            return "NTP not supported";
        case MM_NET_LIFECYCLE_TFTP:
            return "TFTP not supported";
        case MM_NET_LIFECYCLE_TELNET:
            return "Telnet not supported";
        case MM_NET_LIFECYCLE_WEB_CONSOLE:
            return "Web console not supported";
        case MM_NET_LIFECYCLE_MQTT:
            return "MQTT not supported";
        case MM_NET_LIFECYCLE_TCP_CLIENT:
        default:
            return "Network feature not supported";
    }
}

static mm_net_lifecycle_result_t maybe_reboot(unsigned mask, unsigned bit) {
    return (mask & bit) ? MM_NET_LIFECYCLE_REBOOT_REQUIRED
                        : MM_NET_LIFECYCLE_OK;
}

mm_net_lifecycle_result_t mm_net_lifecycle_on_network_ready(
    const mm_net_lifecycle_hooks_t *hooks) {
    if (!hooks) return MM_NET_LIFECYCLE_OK;
    if (Option.TCP_PORT && hooks->open_tcp_server &&
        mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_TCP_SERVER) &&
        !hooks->open_tcp_server((uint16_t)Option.TCP_PORT))
        return MM_NET_LIFECYCLE_ERROR;
    if (Option.UDP_PORT && hooks->open_udp_server &&
        mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_UDP) &&
        !hooks->open_udp_server((uint16_t)Option.UDP_PORT))
        return MM_NET_LIFECYCLE_ERROR;
    if (!Option.disabletftp && hooks->open_tftp &&
        mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_TFTP) &&
        !hooks->open_tftp())
        return MM_NET_LIFECYCLE_ERROR;
    if (Option.Telnet && hooks->open_telnet &&
        mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_TELNET) &&
        !hooks->open_telnet())
        return MM_NET_LIFECYCLE_ERROR;
    if (Option.WebConsole && hooks->open_web_console &&
        mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_WEB_CONSOLE) &&
        !hooks->open_web_console())
        return MM_NET_LIFECYCLE_ERROR;
    return MM_NET_LIFECYCLE_OK;
}

void mm_net_lifecycle_on_network_down(
    const mm_net_lifecycle_hooks_t *hooks) {
    if (!hooks) return;
    if (hooks->close_web_console) hooks->close_web_console();
    if (hooks->close_telnet) hooks->close_telnet();
    if (hooks->close_tftp) hooks->close_tftp();
    if (hooks->close_udp_server) hooks->close_udp_server();
    if (hooks->close_tcp_server) hooks->close_tcp_server();
}

void mm_net_lifecycle_store_wifi_credentials(
    unsigned char *arg, const char *default_hostname,
    mm_net_lifecycle_wifi_validate_t validate) {
    mm_net_wifi_credentials_t parsed;
    mm_net_wifi_parse_credentials(arg, default_hostname, &parsed);

    if (parsed.has_static_ip && validate) validate(&parsed);

    strcpy((char *)Option.SSID, parsed.ssid);
    strcpy((char *)Option.PASSWORD, parsed.password);
    strcpy(Option.hostname, parsed.hostname);
    if (parsed.has_static_ip) {
        strcpy(Option.ipaddress, parsed.ipaddress);
        strcpy(Option.mask, parsed.mask);
        strcpy(Option.gateway, parsed.gateway);
    } else {
        memset(Option.ipaddress, 0, sizeof Option.ipaddress);
        memset(Option.mask, 0, sizeof Option.mask);
        memset(Option.gateway, 0, sizeof Option.gateway);
    }
    SaveOptions();
}

mm_net_lifecycle_result_t mm_net_lifecycle_wifi_connect(
    const mm_net_lifecycle_wifi_connect_t *connect) {
    const char *password = *Option.PASSWORD ? (char *)Option.PASSWORD : "";

    hal_net_wifi_set_credentials((char *)Option.SSID, password,
                                 Option.hostname, Option.ipaddress,
                                 Option.mask, Option.gateway);

    if (!*Option.SSID) {
        uint32_t timeout_ms = connect ? connect->no_ssid_timeout_ms : 0;
        if (hal_net_wifi_connect(timeout_ms) != HAL_NET_OK) {
            if (connect && connect->no_ssid_failure_message)
                MMPrintString((char *)connect->no_ssid_failure_message);
            WIFIconnected = 0;
            return MM_NET_LIFECYCLE_ERROR;
        }
        return MM_NET_LIFECYCLE_OK;
    }

    const char *name = *Option.hostname ? Option.hostname :
                       (connect ? connect->default_hostname : NULL);
    if (name && *name) MMPrintString((char *)name);
    MMPrintString(" connecting to WiFi...\r\n");

    if (hal_net_wifi_connect(30000) != HAL_NET_OK) {
        MMPrintString("failed to connect.\r\n");
        WIFIconnected = 0;
        return MM_NET_LIFECYCLE_ERROR;
    }

    char ipbuf[32];
    if (hal_net_ip_address(ipbuf, sizeof ipbuf) != HAL_NET_OK) {
        MMPrintString("failed to connect.\r\n");
        WIFIconnected = 0;
        return MM_NET_LIFECYCLE_ERROR;
    }

    char buff[STRINGSIZE];
    snprintf(buff, sizeof buff, "Connected %s\r\n", ipbuf);
    MMPrintString(buff);
    WIFIconnected = 1;

    const mm_net_lifecycle_hooks_t *hooks = connect ? connect->hooks : NULL;
    if (mm_net_lifecycle_on_network_ready(hooks) != MM_NET_LIFECYCLE_OK) {
        MMPrintString("Failed to create network service\r\n");
        return MM_NET_LIFECYCLE_ERROR;
    }
    return MM_NET_LIFECYCLE_OK;
}

mm_net_lifecycle_result_t mm_net_lifecycle_option_setter(
    unsigned char *cmdline, const mm_net_lifecycle_hooks_t *hooks) {
    unsigned char *tp;

    tp = checkstring(cmdline, (unsigned char *)"WEB MESSAGES");
    if (tp) {
        if (mm_net_parse_web_messages_option(tp, &optionsuppressstatus))
            return MM_NET_LIFECYCLE_OK;
        return MM_NET_LIFECYCLE_ERROR;
    }

    tp = checkstring(cmdline, (unsigned char *)"WIFI");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(
                MM_NET_LIFECYCLE_WIFI_CONNECT))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        if (!hooks || !hooks->apply_wifi)
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        mm_net_lifecycle_result_t rc = hooks->apply_wifi(tp);
        if (rc != MM_NET_LIFECYCLE_OK) return rc;
        return maybe_reboot(hooks->reboot_after_option_mask,
                            MM_NET_LIFECYCLE_REBOOT_WIFI);
    }

    tp = checkstring(cmdline, (unsigned char *)"TCP SERVER PORT");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(
                MM_NET_LIFECYCLE_TCP_SERVER))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        mm_net_server_port_option_t parsed;
        mm_net_parse_server_port_option(tp, &parsed);
        Option.TCP_PORT = parsed.port;
        Option.ServerResponceTime = parsed.response_ms;
        SaveOptions();
        if (hooks && (hooks->reboot_after_option_mask &
                      MM_NET_LIFECYCLE_REBOOT_TCP))
            return MM_NET_LIFECYCLE_REBOOT_REQUIRED;
        if (parsed.port) {
            if (WIFIconnected && hooks && hooks->open_tcp_server &&
                !hooks->open_tcp_server((uint16_t)parsed.port))
                return MM_NET_LIFECYCLE_ERROR;
        } else if (hooks && hooks->close_tcp_server) {
            hooks->close_tcp_server();
        }
        return maybe_reboot(hooks ? hooks->reboot_after_option_mask : 0,
                            MM_NET_LIFECYCLE_REBOOT_TCP);
    }

    tp = checkstring(cmdline, (unsigned char *)"UDP SERVER PORT");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_UDP))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        mm_net_server_port_option_t parsed;
        mm_net_parse_server_port_option(tp, &parsed);
        Option.UDP_PORT = parsed.port;
        Option.UDPServerResponceTime = parsed.response_ms;
        SaveOptions();
        if (hooks && (hooks->reboot_after_option_mask &
                      MM_NET_LIFECYCLE_REBOOT_UDP))
            return MM_NET_LIFECYCLE_REBOOT_REQUIRED;
        if (parsed.port) {
            if (WIFIconnected && hooks && hooks->open_udp_server &&
                !hooks->open_udp_server((uint16_t)parsed.port))
                return MM_NET_LIFECYCLE_ERROR;
        } else if (hooks && hooks->close_udp_server) {
            hooks->close_udp_server();
        }
        return maybe_reboot(hooks ? hooks->reboot_after_option_mask : 0,
                            MM_NET_LIFECYCLE_REBOOT_UDP);
    }

    tp = checkstring(cmdline, (unsigned char *)"TELNET CONSOLE");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_TELNET))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        int telnet = (int)Option.Telnet;
        if (!mm_net_parse_telnet_console_option(tp, &telnet))
            return MM_NET_LIFECYCLE_ERROR;
        Option.Telnet = telnet;
        SaveOptions();
        if (hooks && (hooks->reboot_after_option_mask &
                      MM_NET_LIFECYCLE_REBOOT_TELNET))
            return MM_NET_LIFECYCLE_REBOOT_REQUIRED;
        if (Option.Telnet) {
            if (WIFIconnected && hooks && hooks->open_telnet &&
                !hooks->open_telnet())
                return MM_NET_LIFECYCLE_ERROR;
        } else if (hooks && hooks->close_telnet) {
            hooks->close_telnet();
        }
        return maybe_reboot(hooks ? hooks->reboot_after_option_mask : 0,
                            MM_NET_LIFECYCLE_REBOOT_TELNET);
    }

    tp = checkstring(cmdline, (unsigned char *)"WEB CONSOLE");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_WEB_CONSOLE))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        int enabled = (int)Option.WebConsole;
        if (!mm_net_parse_web_console_option(tp, &enabled))
            return MM_NET_LIFECYCLE_ERROR;
        Option.WebConsole = (unsigned char)enabled;
        SaveOptions();
        if (hooks && (hooks->reboot_after_option_mask &
                      MM_NET_LIFECYCLE_REBOOT_WEB_CONSOLE))
            return MM_NET_LIFECYCLE_REBOOT_REQUIRED;
        if (Option.WebConsole) {
            if (hooks && hooks->open_web_console && !hooks->open_web_console())
                return MM_NET_LIFECYCLE_ERROR;
        } else if (hooks && hooks->close_web_console) {
            hooks->close_web_console();
        }
        return maybe_reboot(hooks ? hooks->reboot_after_option_mask : 0,
                            MM_NET_LIFECYCLE_REBOOT_WEB_CONSOLE);
    }

    tp = checkstring(cmdline, (unsigned char *)"TFTP");
    if (tp) {
        if (!mm_net_lifecycle_service_supported(MM_NET_LIFECYCLE_TFTP))
            return MM_NET_LIFECYCLE_UNSUPPORTED;
        int disable_tftp = Option.disabletftp;
        if (!mm_net_parse_tftp_option(tp, &disable_tftp))
            return MM_NET_LIFECYCLE_ERROR;
        Option.disabletftp = (char)disable_tftp;
        SaveOptions();
        if (hooks && (hooks->reboot_after_option_mask &
                      MM_NET_LIFECYCLE_REBOOT_TFTP))
            return MM_NET_LIFECYCLE_REBOOT_REQUIRED;
        if (Option.disabletftp) {
            if (hooks && hooks->close_tftp) hooks->close_tftp();
        } else if (WIFIconnected && hooks && hooks->open_tftp &&
                   !hooks->open_tftp()) {
            return MM_NET_LIFECYCLE_ERROR;
        }
        return maybe_reboot(hooks ? hooks->reboot_after_option_mask : 0,
                            MM_NET_LIFECYCLE_REBOOT_TFTP);
    }

    return MM_NET_LIFECYCLE_NOT_HANDLED;
}

int mm_net_lifecycle_handle_option_result(
    mm_net_lifecycle_result_t result,
    const mm_net_lifecycle_result_handler_t *handler) {
    switch (result) {
        case MM_NET_LIFECYCLE_NOT_HANDLED:
            return 0;
        case MM_NET_LIFECYCLE_OK:
            return 1;
        case MM_NET_LIFECYCLE_REBOOT_REQUIRED:
            if (handler && handler->reboot_required) {
                handler->reboot_required();
                return 1;
            }
            error(handler && handler->apply_error ? handler->apply_error :
                                                   "Failed to apply network option");
            return 1;
        case MM_NET_LIFECYCLE_UNSUPPORTED:
            error(handler && handler->unsupported_error ?
                  handler->unsupported_error : "Network feature not supported");
            return 1;
        case MM_NET_LIFECYCLE_ERROR:
        default:
            error(handler && handler->apply_error ? handler->apply_error :
                                                   "Failed to apply network option");
            return 1;
    }
    return 1;
}

void mm_net_lifecycle_runtime_reset(
    const mm_net_lifecycle_runtime_hooks_t *hooks) {
    if (hooks) {
        if (hooks->clear_tcp_requests) hooks->clear_tcp_requests();
        if (hooks->close_tcp_client) hooks->close_tcp_client();
        if (hooks->close_mqtt) hooks->close_mqtt();
        if (hooks->close_tftp_session) hooks->close_tftp_session();
        if (hooks->close_telnet_session) hooks->close_telnet_session();
    }

    TCPreceived = false;
    TCPreceiveInterrupt = NULL;
    UDPreceive = false;
    UDPinterrupt = NULL;
    MQTTComplete = false;
    MQTTInterrupt = NULL;
    mm_net_state_clear_messages();
    optionsuppressstatus = false;
}

void mm_net_lifecycle_poll(
    const mm_net_lifecycle_poll_hooks_t *hooks, int mode,
    int require_network_ready) {
    if (!hooks) return;
    if (require_network_ready && !WIFIconnected) return;

    if (hooks->poll_udp) hooks->poll_udp();
    if (hooks->poll_tftp) hooks->poll_tftp();
    if (hooks->poll_tcp_client_stream) hooks->poll_tcp_client_stream();
    if (hooks->poll_mqtt) hooks->poll_mqtt();
    if (hooks->poll_tcp_server) hooks->poll_tcp_server();
    if (hooks->poll_telnet) hooks->poll_telnet(mode);
}
