/*
 * shared/net/mm_net_service.c - shared TCP/UDP WEB service state.
 */

#include <string.h>

#if defined(MMBASIC_HOST) && !defined(MMBASIC_ESP32)
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include "lwip/inet.h"
#endif

#include "MMBasic_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_state.h"

void mm_net_tcp_service_slot_init(mm_net_tcp_service_slot_t *slot,
                                  uint8_t *recv_buf, size_t recv_cap,
                                  char *path, size_t path_cap) {
    memset(slot, 0, sizeof(*slot));
    slot->recv_buf = recv_buf;
    slot->recv_cap = recv_cap;
    slot->path = path;
    slot->path_cap = path_cap;
}

void mm_net_tcp_service_init(mm_net_tcp_service_t *svc,
                             mm_net_tcp_service_slot_t *slots, int max_slots) {
    memset(svc, 0, sizeof(*svc));
    svc->slots = slots;
    svc->max_slots = max_slots;
}

static int tcp_slot_valid(const mm_net_tcp_service_t *svc, int pcb) {
    return svc && pcb >= 0 && pcb < svc->max_slots;
}

static void tcp_reset_slot(mm_net_tcp_service_slot_t *slot) {
    slot->conn = 0;
    slot->inttrig = 0;
    slot->recv_len = 0;
    if (slot->path && slot->path_cap) slot->path[0] = 0;
}

void mm_net_tcp_service_close_slot(mm_net_tcp_service_t *svc, int pcb) {
    if (!tcp_slot_valid(svc, pcb)) return;
    mm_net_tcp_service_slot_t *slot = &svc->slots[pcb];
    if (slot->conn) hal_net_tcp_conn_close(slot->conn);
    tcp_reset_slot(slot);
}

void mm_net_tcp_service_stop(mm_net_tcp_service_t *svc) {
    if (!svc) return;
    if (svc->server) {
        hal_net_tcp_server_close(svc->server);
        svc->server = 0;
    }
    for (int i = 0; i < svc->max_slots; i++)
        mm_net_tcp_service_close_slot(svc, i);
    svc->port = 0;
}

int mm_net_tcp_service_open(mm_net_tcp_service_t *svc, uint16_t port,
                            int backlog) {
    if (!svc || !port) return 0;
    if (svc->server && svc->port == port) return 1;
    mm_net_tcp_service_stop(svc);
    if (hal_net_tcp_server_open(port, backlog, &svc->server) != HAL_NET_OK) {
        svc->server = 0;
        return 0;
    }
    svc->port = port;
    return 1;
}

int mm_net_tcp_service_is_open(const mm_net_tcp_service_t *svc) {
    return svc && svc->server;
}

static int tcp_find_free_slot(const mm_net_tcp_service_t *svc) {
    for (int i = 0; i < svc->max_slots; i++) {
        if (!svc->slots[i].conn && !svc->slots[i].inttrig) return i;
    }
    return -1;
}

int mm_net_tcp_service_interrupt_pending(const mm_net_tcp_service_t *svc) {
    if (!svc) return 0;
    for (int i = 0; i < svc->max_slots; i++) {
        if (svc->slots[i].inttrig) return 1;
    }
    return 0;
}

int mm_net_tcp_service_poll(mm_net_tcp_service_t *svc) {
    if (!svc || !svc->server) return 0;
    int accepted = 0;
    for (;;) {
        int pcb = tcp_find_free_slot(svc);
        if (pcb < 0) return accepted;
        mm_net_tcp_service_slot_t *slot = &svc->slots[pcb];
        hal_net_tcp_conn_t conn = 0;
        size_t len = 0;
        int rc = hal_net_tcp_accept_event(svc->server, &conn, slot->recv_buf,
                                          slot->recv_cap, &len);
        if (rc == HAL_NET_WOULD_BLOCK) return accepted;
        if (rc != HAL_NET_OK || !conn) return accepted;
        if (len == 0) {
            hal_net_tcp_conn_close(conn);
            continue;
        }
        slot->conn = conn;
        slot->recv_len = len;
        if (slot->path && slot->path_cap) {
            mm_net_http_extract_path(slot->recv_buf, len, slot->path,
                                     slot->path_cap);
        }
        slot->inttrig = 1;
        TCPreceived = true;
        accepted++;
    }
}

int mm_net_tcp_service_read(mm_net_tcp_service_t *svc, int pcb,
                            int64_t *dest, uint8_t *buffer,
                            size_t payload_capacity, size_t array_bytes) {
    if (!tcp_slot_valid(svc, pcb)) return HAL_NET_ERR;
    mm_net_tcp_service_slot_t *slot = &svc->slots[pcb];
    if (!slot->inttrig) {
        memset(dest, 0, array_bytes);
        return HAL_NET_WOULD_BLOCK;
    }
    if (payload_capacity < slot->recv_len) return HAL_NET_ERR;
    memcpy(buffer, slot->recv_buf, slot->recv_len);
    dest[0] = (int64_t)slot->recv_len;
    slot->inttrig = 0;
    return HAL_NET_OK;
}

int mm_net_tcp_service_send(mm_net_tcp_service_t *svc, int pcb,
                            const void *payload, size_t payload_len,
                            uint32_t timeout_ms) {
    if (!tcp_slot_valid(svc, pcb) || !svc->slots[pcb].conn)
        return HAL_NET_ERR;
    if (!payload_len) return HAL_NET_OK;
    return hal_net_tcp_conn_send(svc->slots[pcb].conn, payload, payload_len,
                                 timeout_ms);
}

hal_net_tcp_conn_t mm_net_tcp_service_conn(const mm_net_tcp_service_t *svc,
                                           int pcb) {
    if (!tcp_slot_valid(svc, pcb)) return 0;
    return svc->slots[pcb].conn;
}

int mm_net_tcp_service_request_pending(const mm_net_tcp_service_t *svc,
                                       int pcb) {
    if (!tcp_slot_valid(svc, pcb)) return 0;
    return svc->slots[pcb].inttrig ? 1 : 0;
}

const char *mm_net_tcp_service_path(const mm_net_tcp_service_t *svc, int pcb) {
    if (!tcp_slot_valid(svc, pcb) || !svc->slots[pcb].path) return "";
    return svc->slots[pcb].path;
}

void mm_net_tcp_service_clear_requests(mm_net_tcp_service_t *svc) {
    if (!svc) return;
    for (int i = 0; i < svc->max_slots; i++)
        mm_net_tcp_service_close_slot(svc, i);
}

int mm_net_udp_service_open(mm_net_udp_service_t *svc, uint16_t port) {
    if (!svc || !port) return 0;
    if (svc->socket && svc->port == port) return 1;
    mm_net_udp_service_stop(svc);
    if (hal_net_udp_bind(port, &svc->socket) != HAL_NET_OK) {
        svc->socket = 0;
        return 0;
    }
    svc->port = port;
    return 1;
}

void mm_net_udp_service_stop(mm_net_udp_service_t *svc) {
    if (!svc) return;
    if (svc->socket) {
        hal_net_udp_close(svc->socket);
        svc->socket = 0;
    }
    svc->port = 0;
}

int mm_net_udp_service_is_open(const mm_net_udp_service_t *svc) {
    return svc && svc->socket;
}

int mm_net_udp_service_poll(mm_net_udp_service_t *svc) {
    if (!svc || !svc->socket) return 0;
    int received = 0;
    for (;;) {
        uint8_t buf[STRINGSIZE - 1];
        hal_net_addr_t from;
        size_t len = 0;
        int rc = hal_net_udp_recv_event(svc->socket, &from, buf, sizeof buf,
                                        &len);
        if (rc == HAL_NET_WOULD_BLOCK) return received;
        if (rc != HAL_NET_OK) return received;
        mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, buf, len);
        if (from.family == 4) {
            mm_net_state_set_ipv4_address(from.bytes);
        } else if (from.family == 6) {
#ifdef AF_INET6
            char text[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, from.bytes, text, sizeof text))
                mm_net_state_set_mstring(MM_NET_STATE_ADDRESS, text,
                                         strlen(text));
#endif
        }
        UDPreceive = true;
        received++;
    }
}
