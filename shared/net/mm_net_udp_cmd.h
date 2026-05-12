/*
 * shared/net/mm_net_udp_cmd.h - shared UDP BASIC command parsing.
 */

#ifndef MM_NET_UDP_CMD_H
#define MM_NET_UDP_CMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *host;
    int port;
    const uint8_t *payload;
    size_t payload_len;
} mm_net_udp_send_args_t;

char *mm_net_udp_parse_interrupt(unsigned char *arg);
void mm_net_udp_parse_send(unsigned char *arg, mm_net_udp_send_args_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_UDP_CMD_H */
