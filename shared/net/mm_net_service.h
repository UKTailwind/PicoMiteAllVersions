/*
 * shared/net/mm_net_service.h - shared TCP/UDP WEB service state.
 */

#ifndef MM_NET_SERVICE_H
#define MM_NET_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_net_tcp_conn_t conn;
    int inttrig;
    size_t recv_len;
    uint8_t *recv_buf;
    size_t recv_cap;
    char *path;
    size_t path_cap;
} mm_net_tcp_service_slot_t;

typedef struct {
    hal_net_tcp_server_t server;
    int port;
    mm_net_tcp_service_slot_t *slots;
    int max_slots;
} mm_net_tcp_service_t;

typedef struct {
    hal_net_udp_socket_t socket;
    int port;
} mm_net_udp_service_t;

void mm_net_tcp_service_init(mm_net_tcp_service_t *svc,
                             mm_net_tcp_service_slot_t *slots, int max_slots);
void mm_net_tcp_service_slot_init(mm_net_tcp_service_slot_t *slot,
                                  uint8_t *recv_buf, size_t recv_cap,
                                  char *path, size_t path_cap);
int mm_net_tcp_service_open(mm_net_tcp_service_t *svc, uint16_t port,
                            int backlog);
void mm_net_tcp_service_stop(mm_net_tcp_service_t *svc);
int mm_net_tcp_service_is_open(const mm_net_tcp_service_t *svc);
void mm_net_tcp_service_close_slot(mm_net_tcp_service_t *svc, int pcb);
int mm_net_tcp_service_interrupt_pending(const mm_net_tcp_service_t *svc);
int mm_net_tcp_service_poll(mm_net_tcp_service_t *svc);
int mm_net_tcp_service_read(mm_net_tcp_service_t *svc, int pcb,
                            int64_t *dest, uint8_t *buffer,
                            size_t payload_capacity, size_t array_bytes);
int mm_net_tcp_service_send(mm_net_tcp_service_t *svc, int pcb,
                            const void *payload, size_t payload_len,
                            uint32_t timeout_ms);
hal_net_tcp_conn_t mm_net_tcp_service_conn(const mm_net_tcp_service_t *svc,
                                           int pcb);
int mm_net_tcp_service_request_pending(const mm_net_tcp_service_t *svc,
                                       int pcb);
const char *mm_net_tcp_service_path(const mm_net_tcp_service_t *svc, int pcb);
void mm_net_tcp_service_clear_requests(mm_net_tcp_service_t *svc);

int mm_net_udp_service_open(mm_net_udp_service_t *svc, uint16_t port);
void mm_net_udp_service_stop(mm_net_udp_service_t *svc);
int mm_net_udp_service_is_open(const mm_net_udp_service_t *svc);
int mm_net_udp_service_poll(mm_net_udp_service_t *svc);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_SERVICE_H */
