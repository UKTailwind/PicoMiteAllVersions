/*
 * shared/net/mm_net_wifi_cmd.h - shared WiFi BASIC command parsing.
 */

#ifndef MM_NET_WIFI_CMD_H
#define MM_NET_WIFI_CMD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t * dest;
    char * buffer;
    int capacity;
} mm_net_scan_args_t;

typedef struct {
    char ssid[64];
    char password[64];
    char hostname[32];
    char ipaddress[16];
    char mask[16];
    char gateway[16];
    int has_static_ip;
} mm_net_wifi_credentials_t;

void mm_net_wifi_parse_scan(unsigned char * arg, mm_net_scan_args_t * out);
void mm_net_wifi_scan_command(unsigned char * arg);
void mm_net_wifi_parse_credentials(unsigned char * arg,
                                   const char * default_hostname,
                                   mm_net_wifi_credentials_t * out);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_WIFI_CMD_H */
