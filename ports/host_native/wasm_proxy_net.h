#ifndef WASM_PROXY_NET_H
#define WASM_PROXY_NET_H

#include <stddef.h>
#include <stdint.h>

#define WASM_PROXY_TCP_MAX_STREAMS 8
#define WASM_PROXY_TCP_MAX_LISTENERS 4
#define WASM_PROXY_TCP_MAX_SERVER_CONNS 8
#define WASM_PROXY_TCP_MAX_READ_BYTES 65536
#define WASM_PROXY_TCP_MAX_WRITE_BYTES 65536
#define WASM_PROXY_UDP_MAX_SOCKETS 8
#define WASM_PROXY_UDP_MAX_DATAGRAM_BYTES 65507

typedef enum wasm_proxy_tcp_status {
    WASM_PROXY_TCP_OK = 0,
    WASM_PROXY_TCP_BAD_REQUEST = -1,
    WASM_PROXY_TCP_CONNECT_FAILED = -2,
    WASM_PROXY_TCP_IO_FAILED = -3,
    WASM_PROXY_TCP_TIMEOUT = -4,
    WASM_PROXY_TCP_NO_MEMORY = -5,
    WASM_PROXY_TCP_NOT_FOUND = -6,
} wasm_proxy_tcp_status_t;

typedef enum wasm_proxy_udp_status {
    WASM_PROXY_UDP_OK = 0,
    WASM_PROXY_UDP_BAD_REQUEST = -1,
    WASM_PROXY_UDP_BIND_FAILED = -2,
    WASM_PROXY_UDP_IO_FAILED = -3,
    WASM_PROXY_UDP_TIMEOUT = -4,
    WASM_PROXY_UDP_NO_MEMORY = -5,
    WASM_PROXY_UDP_NOT_FOUND = -6,
    WASM_PROXY_UDP_WOULD_BLOCK = -7,
} wasm_proxy_udp_status_t;

typedef struct wasm_proxy_udp_packet {
    uint8_t *data;
    size_t cap;
    size_t len;
    char address[80];
    int family;
    int port;
} wasm_proxy_udp_packet_t;

int wasm_proxy_tcp_open(const char *host, int port, int timeout_ms,
                        int *out_id, char *error, size_t error_len);
int wasm_proxy_tcp_write_read(int id, const uint8_t *write_data,
                              size_t write_len, uint8_t *read_data,
                              size_t read_cap, size_t *out_read_len,
                              int timeout_ms, int *out_closed,
                              char *error, size_t error_len);
int wasm_proxy_tcp_close(int id);
void wasm_proxy_tcp_close_all(void);

int wasm_proxy_tcp_listen(int port, int backlog, int *out_id,
                          char *error, size_t error_len);
int wasm_proxy_tcp_listener_close(int id);
int wasm_proxy_tcp_accept_conn(int listener_id, int *out_conn_id,
                               char *error, size_t error_len);
int wasm_proxy_tcp_accept_event(int listener_id, uint8_t *read_data,
                                size_t read_cap, size_t *out_read_len,
                                int *out_conn_id, char *error,
                                size_t error_len);
int wasm_proxy_tcp_conn_recv(int conn_id, uint8_t *read_data,
                             size_t read_cap, size_t *out_read_len,
                             int *out_closed, char *error,
                             size_t error_len);
int wasm_proxy_tcp_conn_send(int conn_id, const uint8_t *data, size_t len,
                             int timeout_ms, char *error, size_t error_len);
int wasm_proxy_tcp_conn_close(int conn_id);
void wasm_proxy_tcp_listeners_close_all(void);

int wasm_proxy_udp_bind(int port, int *out_id, char *error,
                        size_t error_len);
int wasm_proxy_udp_close(int id);
int wasm_proxy_udp_send(int id, const char *host, int port,
                        const uint8_t *data, size_t len, int timeout_ms,
                        char *error, size_t error_len);
int wasm_proxy_udp_recv(int id, wasm_proxy_udp_packet_t *packet,
                        char *error, size_t error_len);
void wasm_proxy_udp_close_all(void);

#endif
