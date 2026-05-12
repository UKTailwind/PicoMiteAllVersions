/*
 * shared/net/mm_net_tcp_client_cmd.h - shared TCP client BASIC parsing.
 */

#ifndef MM_NET_TCP_CLIENT_CMD_H
#define MM_NET_TCP_CLIENT_CMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *host;
    int port;
    int timeout_ms;
} mm_net_tcp_client_open_args_t;

typedef struct {
    const uint8_t *request;
    size_t request_len;
    int64_t *dest;
    uint8_t *buffer;
    int array_bytes;
    int payload_capacity;
    int timeout_ms;
} mm_net_tcp_client_request_args_t;

typedef struct {
    const uint8_t *request;
    size_t request_len;
    int64_t *dest;
    uint8_t *buffer;
    int array_bytes;
    int payload_capacity;
    int64_t *read_pos;
    int64_t *write_pos;
} mm_net_tcp_client_stream_args_t;

void mm_net_tcp_client_parse_open(unsigned char *arg,
                                  mm_net_tcp_client_open_args_t *out);
void mm_net_tcp_client_parse_request(unsigned char *arg,
                                     mm_net_tcp_client_request_args_t *out);
void mm_net_tcp_client_parse_stream(unsigned char *arg,
                                    mm_net_tcp_client_stream_args_t *out);
void mm_net_tcp_client_stream_append(uint8_t *buffer, int capacity,
                                     int64_t *read_pos, int64_t *write_pos,
                                     const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_TCP_CLIENT_CMD_H */
