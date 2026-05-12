/*
 * shared/net/mm_net_tcp_server_cmd.h - shared TCP server BASIC parsing.
 */

#ifndef MM_NET_TCP_SERVER_CMD_H
#define MM_NET_TCP_SERVER_CMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pcb;
} mm_net_tcp_server_slot_args_t;

typedef struct {
    int pcb;
    int64_t *dest;
    uint8_t *buffer;
    int array_bytes;
    int payload_capacity;
} mm_net_tcp_server_read_args_t;

typedef struct {
    int pcb;
    const uint8_t *payload;
    size_t payload_len;
} mm_net_tcp_server_send_args_t;

char *mm_net_tcp_server_parse_interrupt(unsigned char *arg);
void mm_net_tcp_server_parse_slot(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_slot_args_t *out);
void mm_net_tcp_server_parse_read(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_read_args_t *out);
void mm_net_tcp_server_parse_send(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_send_args_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_TCP_SERVER_CMD_H */
