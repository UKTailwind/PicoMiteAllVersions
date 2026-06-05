/*
 * shared/net/mm_net_options.h - shared WEB option/MM.INFO parsing.
 */

#ifndef MM_NET_OPTIONS_H
#define MM_NET_OPTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int port;
    int response_ms;
} mm_net_server_port_option_t;

typedef enum {
    MM_NET_INFO_NONE = 0,
    MM_NET_INFO_TCP_PATH,
    MM_NET_INFO_TCP_REQUEST,
    MM_NET_INFO_TCP_PORT,
    MM_NET_INFO_MAX_CONNECTIONS,
    MM_NET_INFO_UDP_PORT,
    MM_NET_INFO_IP_ADDRESS,
    MM_NET_INFO_WIFI_STATUS,
    MM_NET_INFO_TCPIP_STATUS,
} mm_net_info_kind_t;

typedef struct {
    mm_net_info_kind_t kind;
    int slot;
} mm_net_info_query_t;

typedef struct {
    void (*before_query)(void);
    const char * (*tcp_path)(int slot);
    int (*tcp_request_pending)(int slot);
    int max_connections;
    int tcp_port;
    int udp_port;
    int (*ip_address)(char * out, size_t out_len);
    int (*wifi_status)(void);
    int (*tcpip_status)(void);
} mm_net_info_hooks_t;

void mm_net_parse_server_port_option(unsigned char * arg,
                                     mm_net_server_port_option_t * out);
int mm_net_parse_web_messages_option(unsigned char * arg, bool * suppress_status);
int mm_net_parse_telnet_console_option(unsigned char * arg, int * telnet_mode);
int mm_net_parse_web_console_option(unsigned char * arg, int * enabled);
int mm_net_parse_tftp_option(unsigned char * arg, int * disable_tftp);
mm_net_info_query_t mm_net_parse_info_query(unsigned char * expr, int max_pcb);
int mm_net_mminfo(unsigned char * expr, int64_t * out_iret,
                  unsigned char * out_sret, int * out_targ,
                  const mm_net_info_hooks_t * hooks);
void mm_net_print_wifi_option(const char * ssid, const char * password,
                              const char * hostname, const char * ipaddress,
                              const char * mask, const char * gateway);
void mm_net_print_options(int tcp_port, int tcp_response_ms,
                          int udp_port, int udp_response_ms,
                          bool suppress_status);
void mm_net_print_service_options(int telnet_mode, int disable_tftp);
void mm_net_print_web_console_option(int web_console);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_OPTIONS_H */
