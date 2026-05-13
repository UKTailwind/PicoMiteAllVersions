/*
 * shared/net/mm_net_options.c - shared WEB option/MM.INFO parsing.
 */

#include <string.h>
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_options.h"

void mm_net_parse_server_port_option(unsigned char *arg,
                                     mm_net_server_port_option_t *out) {
    memset(out, 0, sizeof(*out));
    if (CurrentLinePtr) error("Invalid in a program");
    getargs(&arg, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Syntax");
    out->port = getint(argv[0], 0, 65535);
    out->response_ms = 5000;
    if (argc == 3) out->response_ms = getint(argv[2], 1000, 20000);
}

int mm_net_parse_web_messages_option(unsigned char *arg, bool *suppress_status) {
    if (checkstring(arg, (unsigned char *)"OFF")) {
        *suppress_status = true;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"ON")) {
        *suppress_status = false;
        return 1;
    }
    return 0;
}

int mm_net_parse_telnet_console_option(unsigned char *arg, int *telnet_mode) {
    if (CurrentLinePtr) error("Invalid in a program");
    if (checkstring(arg, (unsigned char *)"OFF")) {
        *telnet_mode = 0;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"ON")) {
        *telnet_mode = 1;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"ONLY")) {
        *telnet_mode = -1;
        return 1;
    }
    return 0;
}

int mm_net_parse_tftp_option(unsigned char *arg, int *disable_tftp) {
    if (CurrentLinePtr) error("Invalid in a program");
    if (checkstring(arg, (unsigned char *)"OFF") ||
        checkstring(arg, (unsigned char *)"DISABLE")) {
        *disable_tftp = 1;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"ON") ||
        checkstring(arg, (unsigned char *)"ENABLE")) {
        *disable_tftp = 0;
        return 1;
    }
    return 0;
}

mm_net_info_query_t mm_net_parse_info_query(unsigned char *expr, int max_pcb) {
    mm_net_info_query_t query = { MM_NET_INFO_NONE, -1 };
    unsigned char *tp;
    if ((tp = checkstring(expr, (unsigned char *)"TCP PATH"))) {
        query.kind = MM_NET_INFO_TCP_PATH;
        query.slot = getint(tp, 1, max_pcb) - 1;
        return query;
    }
    if ((tp = checkstring(expr, (unsigned char *)"TCP REQUEST"))) {
        query.kind = MM_NET_INFO_TCP_REQUEST;
        query.slot = getint(tp, 1, max_pcb) - 1;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"TCP PORT")) {
        query.kind = MM_NET_INFO_TCP_PORT;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"MAX CONNECTIONS")) {
        query.kind = MM_NET_INFO_MAX_CONNECTIONS;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"UDP PORT")) {
        query.kind = MM_NET_INFO_UDP_PORT;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"IP ADDRESS")) {
        query.kind = MM_NET_INFO_IP_ADDRESS;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"WIFI STATUS")) {
        query.kind = MM_NET_INFO_WIFI_STATUS;
        return query;
    }
    if (checkstring(expr, (unsigned char *)"TCPIP STATUS")) {
        query.kind = MM_NET_INFO_TCPIP_STATUS;
        return query;
    }
    return query;
}

static void mm_net_info_set_string(unsigned char *out_sret, int *out_targ,
                                   const char *value) {
    strcpy((char *)out_sret, value ? value : "");
    CtoM(out_sret);
    *out_targ = T_STR;
}

static void mm_net_info_set_int(int64_t *out_iret, int *out_targ,
                                int64_t value) {
    *out_iret = value;
    *out_targ = T_INT;
}

int mm_net_mminfo(unsigned char *expr, int64_t *out_iret,
                  unsigned char *out_sret, int *out_targ,
                  const mm_net_info_hooks_t *hooks) {
    int max_connections = hooks ? hooks->max_connections : 0;
    mm_net_info_query_t query = mm_net_parse_info_query(expr, max_connections);
    if (query.kind == MM_NET_INFO_NONE) return 0;
    if (hooks && hooks->before_query) hooks->before_query();

    switch (query.kind) {
        case MM_NET_INFO_TCP_PATH:
            if (!hooks || !hooks->tcp_path) return 0;
            mm_net_info_set_string(out_sret, out_targ,
                                   hooks->tcp_path(query.slot));
            return 1;
        case MM_NET_INFO_TCP_REQUEST:
            if (!hooks || !hooks->tcp_request_pending) return 0;
            mm_net_info_set_int(out_iret, out_targ,
                                hooks->tcp_request_pending(query.slot));
            return 1;
        case MM_NET_INFO_TCP_PORT:
            mm_net_info_set_int(out_iret, out_targ,
                                hooks ? hooks->tcp_port : 0);
            return 1;
        case MM_NET_INFO_MAX_CONNECTIONS:
            mm_net_info_set_int(out_iret, out_targ, max_connections);
            return 1;
        case MM_NET_INFO_UDP_PORT:
            mm_net_info_set_int(out_iret, out_targ,
                                hooks ? hooks->udp_port : 0);
            return 1;
        case MM_NET_INFO_IP_ADDRESS: {
            char ipbuf[32];
            int rc = hooks && hooks->ip_address ?
                     hooks->ip_address(ipbuf, sizeof ipbuf) : HAL_NET_ERR;
            if (rc != HAL_NET_OK) strcpy(ipbuf, "0.0.0.0");
            mm_net_info_set_string(out_sret, out_targ, ipbuf);
            return 1;
        }
        case MM_NET_INFO_WIFI_STATUS:
            mm_net_info_set_int(out_iret, out_targ,
                                hooks && hooks->wifi_status ?
                                hooks->wifi_status() : HAL_NET_UNSUPPORTED);
            return 1;
        case MM_NET_INFO_TCPIP_STATUS:
            mm_net_info_set_int(out_iret, out_targ,
                                hooks && hooks->tcpip_status ?
                                hooks->tcpip_status() : HAL_NET_UNSUPPORTED);
            return 1;
        case MM_NET_INFO_NONE:
        default:
            return 0;
    }
}

void mm_net_print_wifi_option(const char *ssid, const char *password,
                              const char *hostname, const char *ipaddress,
                              const char *mask, const char *gateway) {
    if (!ssid || !*ssid) return;

    char masked_password[65];
    size_t password_len = password ? strlen(password) : 0;
    if (password_len >= sizeof(masked_password))
        password_len = sizeof(masked_password) - 1;
    memset(masked_password, '*', password_len);
    masked_password[password_len] = 0;

    MMPrintString("OPTION WIFI ");
    MMPrintString((char *)ssid);
    MMPrintString(", ");
    MMPrintString(masked_password);
    MMPrintString(", ");
    MMPrintString((char *)(hostname ? hostname : ""));
    if (ipaddress && *ipaddress) {
        MMPrintString(", ");
        MMPrintString((char *)ipaddress);
        MMPrintString(", ");
        MMPrintString((char *)(mask ? mask : ""));
        MMPrintString(", ");
        MMPrintString((char *)(gateway ? gateway : ""));
    }
    MMPrintString("\r\n");
}

void mm_net_print_options(int tcp_port, int tcp_response_ms,
                          int udp_port, int udp_response_ms,
                          bool suppress_status) {
    char buff[64];
    if (tcp_port && tcp_response_ms != 5000) {
        snprintf(buff, sizeof(buff), "OPTION TCP SERVER PORT %u, %u\r\n",
                 (unsigned)tcp_port, (unsigned)tcp_response_ms);
        MMPrintString(buff);
    } else if (tcp_port) {
        snprintf(buff, sizeof(buff), "OPTION TCP SERVER PORT %u\r\n",
                 (unsigned)tcp_port);
        MMPrintString(buff);
    }
    if (udp_port && udp_response_ms != 5000) {
        snprintf(buff, sizeof(buff), "OPTION UDP SERVER PORT %u, %u\r\n",
                 (unsigned)udp_port, (unsigned)udp_response_ms);
        MMPrintString(buff);
    } else if (udp_port) {
        snprintf(buff, sizeof(buff), "OPTION UDP SERVER PORT %u\r\n",
                 (unsigned)udp_port);
        MMPrintString(buff);
    }
    if (suppress_status) MMPrintString("OPTION WEB MESSAGES OFF\r\n");
}

void mm_net_print_service_options(int telnet_mode, int disable_tftp) {
    if (telnet_mode == 1) {
        MMPrintString("OPTION TELNET CONSOLE ON\r\n");
    } else if (telnet_mode == -1) {
        MMPrintString("OPTION TELNET CONSOLE ONLY\r\n");
    }
    if (disable_tftp) MMPrintString("OPTION TFTP OFF\r\n");
}
