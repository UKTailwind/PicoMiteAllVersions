/*
 * ports/host_native/hal_net_posix.c - POSIX socket network HAL backend.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "hal/hal_net.h"
#include "shared/net/mm_net_mqtt_wire.h"

#define HOST_NET_MAX_TCP_SERVERS 8
#define HOST_NET_MAX_TCP_CONNS 16
#define HOST_NET_MAX_TCP_CLIENTS 16
#define HOST_NET_MAX_UDP_SOCKS 16
#define HOST_NET_MAX_MQTT_CLIENTS 8

static int tcp_servers[HOST_NET_MAX_TCP_SERVERS];
static int tcp_conns[HOST_NET_MAX_TCP_CONNS];
static int tcp_clients[HOST_NET_MAX_TCP_CLIENTS];
static int udp_socks[HOST_NET_MAX_UDP_SOCKS];
static int mqtt_clients[HOST_NET_MAX_MQTT_CLIENTS];
static uint16_t mqtt_next_packet_id[HOST_NET_MAX_MQTT_CLIENTS];
static int tables_ready;

static void host_net_init_tables(void) {
    if (tables_ready) return;
    for (size_t i = 0; i < HOST_NET_MAX_TCP_SERVERS; ++i) tcp_servers[i] = -1;
    for (size_t i = 0; i < HOST_NET_MAX_TCP_CONNS; ++i) tcp_conns[i] = -1;
    for (size_t i = 0; i < HOST_NET_MAX_TCP_CLIENTS; ++i) tcp_clients[i] = -1;
    for (size_t i = 0; i < HOST_NET_MAX_UDP_SOCKS; ++i) udp_socks[i] = -1;
    for (size_t i = 0; i < HOST_NET_MAX_MQTT_CLIENTS; ++i) mqtt_clients[i] = -1;
    tables_ready = 1;
}

static int host_net_set_nonblock(int fd, int enabled) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return HAL_NET_ERR;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0 ? HAL_NET_OK : HAL_NET_ERR;
}

static uint16_t host_net_alloc(int *slots, size_t count, int fd) {
    host_net_init_tables();
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] < 0) {
            slots[i] = fd;
            return (uint16_t)(i + 1);
        }
    }
    return 0;
}

static int *host_net_slot(int *slots, size_t count, uint16_t handle) {
    if (handle == 0 || handle > count) return NULL;
    if (slots[handle - 1] < 0) return NULL;
    return &slots[handle - 1];
}

static int host_net_close_slot(int *slots, size_t count, uint16_t handle) {
    int *slot = host_net_slot(slots, count, handle);
    if (!slot) return HAL_NET_ERR;
    close(*slot);
    *slot = -1;
    return HAL_NET_OK;
}

static int host_net_wait_fd(int fd, int for_write, uint32_t timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms) {
        tv.tv_sec = (time_t)(timeout_ms / 1000);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
        tvp = &tv;
    }
    int rc = select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL,
                    NULL, tvp);
    if (rc > 0) return HAL_NET_OK;
    if (rc == 0) return HAL_NET_TIMEOUT;
    if (errno == EINTR) return HAL_NET_TIMEOUT;
    return HAL_NET_ERR;
}

static int host_net_send_all(int fd, const void *buf, size_t len,
                             uint32_t timeout_ms) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t sent = 0;
    while (sent < len) {
        int wait = host_net_wait_fd(fd, 1, timeout_ms);
        if (wait != HAL_NET_OK) return wait;
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        return HAL_NET_ERR;
    }
    return HAL_NET_OK;
}

static int host_net_recv_exact(int fd, void *buf, size_t len,
                               uint32_t timeout_ms) {
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < len) {
        int wait = host_net_wait_fd(fd, 0, timeout_ms);
        if (wait != HAL_NET_OK) return wait;
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        if (n == 0) return HAL_NET_ERR;
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        return HAL_NET_ERR;
    }
    return HAL_NET_OK;
}

static int host_net_resolve_addr(const char *host, uint16_t port, int socktype,
                                 struct addrinfo **out) {
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    return getaddrinfo(host, portbuf, &hints, out) == 0 ? HAL_NET_OK : HAL_NET_ERR;
}

uint32_t hal_net_capabilities(void) {
    return HAL_NET_CAP_TCP_SERVER |
           HAL_NET_CAP_TCP_CLIENT |
           HAL_NET_CAP_TCP_STREAM |
           HAL_NET_CAP_UDP_SERVER |
           HAL_NET_CAP_UDP_SEND |
           HAL_NET_CAP_MQTT_PLAIN;
}

int hal_net_init(void) {
    host_net_init_tables();
    return HAL_NET_OK;
}

void hal_net_poll(void) {
}

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw) {
    (void)ssid; (void)pass; (void)host; (void)ip; (void)mask; (void)gw;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_wifi_connect(uint32_t timeout_ms) {
    (void)timeout_ms;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_wifi_status(void) {
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcpip_status(void) {
    return 1;
}

int hal_net_ip_address(char *out, size_t out_len) {
    if (!out || out_len == 0) return HAL_NET_ERR;
    snprintf(out, out_len, "127.0.0.1");
    return HAL_NET_OK;
}

int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console) {
    (void)print_to_console;
    if (out && out_len) out[0] = 0;
    if (written) *written = 0;
    return HAL_NET_UNSUPPORTED;
}

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out) {
    if (!out) return HAL_NET_ERR;
    *out = 0;
    host_net_init_tables();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return HAL_NET_ERR;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, backlog > 0 ? backlog : 1) != 0 ||
        host_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }
    uint16_t handle = host_net_alloc(tcp_servers, HOST_NET_MAX_TCP_SERVERS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_server_close(hal_net_tcp_server_t server) {
    return host_net_close_slot(tcp_servers, HOST_NET_MAX_TCP_SERVERS, server);
}

int hal_net_tcp_accept_conn(hal_net_tcp_server_t server,
                            hal_net_tcp_conn_t *conn) {
    if (conn) *conn = 0;
    int *slot = host_net_slot(tcp_servers, HOST_NET_MAX_TCP_SERVERS, server);
    if (!slot) return HAL_NET_ERR;
    int fd = accept(*slot, NULL, NULL);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return HAL_NET_WOULD_BLOCK;
        return HAL_NET_ERR;
    }
    if (host_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }
    uint16_t handle = host_net_alloc(tcp_conns, HOST_NET_MAX_TCP_CONNS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    if (conn) *conn = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len) {
    if (conn) *conn = 0;
    if (len) *len = 0;
    hal_net_tcp_conn_t handle = 0;
    int rc = hal_net_tcp_accept_conn(server, &handle);
    if (rc != HAL_NET_OK) return rc;
    if (conn) *conn = handle;
    if (buf && cap) {
        int *conn_slot = host_net_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS,
                                       handle);
        if (!conn_slot) return HAL_NET_ERR;
        int fd = *conn_slot;
        int wait = host_net_wait_fd(fd, 0, 250);
        if (wait == HAL_NET_OK) {
            ssize_t n = recv(fd, buf, cap, 0);
            if (n > 0 && len) *len = (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                host_net_close_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS,
                                    handle);
                return HAL_NET_ERR;
            }
        } else if (wait != HAL_NET_TIMEOUT) {
            host_net_close_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS, handle);
            return wait;
        }
    }
    return HAL_NET_OK;
}

int hal_net_tcp_conn_recv(hal_net_tcp_conn_t conn, void *buf, size_t cap,
                          size_t *len) {
    if (len) *len = 0;
    int *slot = host_net_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS, conn);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    ssize_t n = recv(*slot, buf, cap, 0);
    if (n > 0) {
        if (len) *len = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_ERR;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_conn_send_some(hal_net_tcp_conn_t conn, const void *buf,
                               size_t cap, size_t *sent) {
    if (sent) *sent = 0;
    int *slot = host_net_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS, conn);
    if (!slot || (!buf && cap)) return HAL_NET_ERR;
    if (cap == 0) return HAL_NET_OK;
    ssize_t n = send(*slot, buf, cap, 0);
    if (n > 0) {
        if (sent) *sent = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_WOULD_BLOCK;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms) {
    int *slot = host_net_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS, conn);
    if (!slot) return HAL_NET_ERR;
    return host_net_send_all(*slot, buf, len, timeout_ms);
}

int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn) {
    return host_net_close_slot(tcp_conns, HOST_NET_MAX_TCP_CONNS, conn);
}

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out) {
    if (!host || !out) return HAL_NET_ERR;
    *out = 0;
    host_net_init_tables();
    struct addrinfo *ai = NULL;
    if (host_net_resolve_addr(host, port, SOCK_STREAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;
    int fd = -1;
    int ok = 0;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        host_net_set_nonblock(fd, 1);
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            ok = 1;
        } else if (errno == EINPROGRESS) {
            int wait = host_net_wait_fd(fd, 1, timeout_ms);
            if (wait == HAL_NET_OK) {
                int so_error = 0;
                socklen_t so_len = sizeof(so_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) == 0 &&
                    so_error == 0) {
                    ok = 1;
                }
            }
        }
        if (ok) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(ai);
    if (!ok || fd < 0) return HAL_NET_TIMEOUT;
    host_net_set_nonblock(fd, 0);
    uint16_t handle = host_net_alloc(tcp_clients, HOST_NET_MAX_TCP_CLIENTS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms) {
    int *slot = host_net_slot(tcp_clients, HOST_NET_MAX_TCP_CLIENTS, client);
    if (!slot) return HAL_NET_ERR;
    return host_net_send_all(*slot, buf, len, timeout_ms);
}

int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms) {
    if (len) *len = 0;
    int *slot = host_net_slot(tcp_clients, HOST_NET_MAX_TCP_CLIENTS, client);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    int wait = host_net_wait_fd(*slot, 0, timeout_ms);
    if (wait != HAL_NET_OK) return wait;
    ssize_t n = recv(*slot, buf, cap, 0);
    if (n > 0) {
        if (len) *len = (size_t)n;
        return HAL_NET_OK;
    }
    if (n == 0) return HAL_NET_OK;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return HAL_NET_WOULD_BLOCK;
    return HAL_NET_ERR;
}

int hal_net_tcp_client_close(hal_net_tcp_client_t client) {
    return host_net_close_slot(tcp_clients, HOST_NET_MAX_TCP_CLIENTS, client);
}

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out) {
    if (!out) return HAL_NET_ERR;
    *out = 0;
    host_net_init_tables();
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return HAL_NET_ERR;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        host_net_set_nonblock(fd, 1) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }
    uint16_t handle = host_net_alloc(udp_socks, HOST_NET_MAX_UDP_SOCKS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_udp_close(hal_net_udp_socket_t sock) {
    return host_net_close_slot(udp_socks, HOST_NET_MAX_UDP_SOCKS, sock);
}

int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port, const void *buf, size_t len,
                            uint32_t timeout_ms) {
    if (!host || (!buf && len)) return HAL_NET_ERR;
    int *slot = host_net_slot(udp_socks, HOST_NET_MAX_UDP_SOCKS, sock);
    if (!slot) return HAL_NET_ERR;
    struct addrinfo *ai = NULL;
    if (host_net_resolve_addr(host, port, SOCK_DGRAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;
    int result = HAL_NET_ERR;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        int wait = host_net_wait_fd(*slot, 1, timeout_ms);
        if (wait == HAL_NET_OK || wait == HAL_NET_TIMEOUT) {
            ssize_t n = sendto(*slot, buf, len, 0, rp->ai_addr, rp->ai_addrlen);
            if (n == (ssize_t)len) result = HAL_NET_OK;
        }
        if (result == HAL_NET_OK) break;
    }
    freeaddrinfo(ai);
    return result;
}

int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms) {
    if (!host || (!buf && len)) return HAL_NET_ERR;
    struct addrinfo *ai = NULL;
    if (host_net_resolve_addr(host, port, SOCK_DGRAM, &ai) != HAL_NET_OK)
        return HAL_NET_ERR;
    int result = HAL_NET_ERR;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int wait = host_net_wait_fd(fd, 1, timeout_ms);
        if (wait == HAL_NET_OK || wait == HAL_NET_TIMEOUT) {
            ssize_t n = sendto(fd, buf, len, 0, rp->ai_addr, rp->ai_addrlen);
            if (n == (ssize_t)len) result = HAL_NET_OK;
        }
        close(fd);
        if (result == HAL_NET_OK) break;
    }
    freeaddrinfo(ai);
    return result;
}

int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len) {
    if (len) *len = 0;
    if (from) memset(from, 0, sizeof(*from));
    int *slot = host_net_slot(udp_socks, HOST_NET_MAX_UDP_SOCKS, sock);
    if (!slot || !buf || cap == 0) return HAL_NET_ERR;
    struct sockaddr_storage src;
    socklen_t src_len = sizeof(src);
    ssize_t n = recvfrom(*slot, buf, cap, 0, (struct sockaddr *)&src, &src_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return HAL_NET_WOULD_BLOCK;
        return HAL_NET_ERR;
    }
    if (len) *len = (size_t)n;
    if (from && src.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&src;
        from->family = 4;
        from->port = ntohs(sin->sin_port);
        memcpy(from->bytes, &sin->sin_addr, 4);
    } else if (from && src.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&src;
        from->family = 6;
        from->port = ntohs(sin6->sin6_port);
        memcpy(from->bytes, &sin6->sin6_addr, 16);
    }
    return HAL_NET_OK;
}

static int mqtt_send_packet(int fd, uint8_t header, const void *body,
                            size_t body_len, uint32_t timeout_ms) {
    uint8_t fixed[5];
    fixed[0] = header;
    size_t rem_len = mm_net_mqtt_encode_remaining_length(fixed + 1, body_len);
    if (host_net_send_all(fd, fixed, rem_len + 1, timeout_ms) != HAL_NET_OK)
        return HAL_NET_ERR;
    if (body_len &&
        host_net_send_all(fd, body, body_len, timeout_ms) != HAL_NET_OK)
        return HAL_NET_ERR;
    return HAL_NET_OK;
}

static int mqtt_read_packet(int fd, uint8_t *header, uint8_t *body,
                            size_t body_cap, size_t *body_len,
                            uint32_t timeout_ms) {
    if (body_len) *body_len = 0;
    int rc = host_net_recv_exact(fd, header, 1, timeout_ms);
    if (rc != HAL_NET_OK) return rc;

    size_t multiplier = 1;
    size_t remaining = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t encoded = 0;
        rc = host_net_recv_exact(fd, &encoded, 1, timeout_ms);
        if (rc != HAL_NET_OK) return rc;
        remaining += (encoded & 127u) * multiplier;
        if (!(encoded & 128u)) break;
        multiplier *= 128u;
        if (i == 3) return HAL_NET_ERR;
    }
    if (remaining > body_cap) return HAL_NET_ERR;
    if (remaining) {
        rc = host_net_recv_exact(fd, body, remaining, timeout_ms);
        if (rc != HAL_NET_OK) return rc;
    }
    if (body_len) *body_len = remaining;
    return HAL_NET_OK;
}

static int mqtt_wait_for_type(int fd, uint8_t want_type, uint32_t timeout_ms) {
    uint8_t header = 0;
    uint8_t body[512];
    size_t body_len = 0;
    uint64_t deadline_us = 0;
    if (timeout_ms) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        deadline_us = ((uint64_t)tv.tv_sec * 1000000ULL) +
                      (uint64_t)tv.tv_usec +
                      (uint64_t)timeout_ms * 1000ULL;
    }
    while (1) {
        uint32_t wait_ms = timeout_ms;
        if (deadline_us) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            uint64_t now = ((uint64_t)tv.tv_sec * 1000000ULL) +
                           (uint64_t)tv.tv_usec;
            if (now >= deadline_us) return HAL_NET_TIMEOUT;
            wait_ms = (uint32_t)((deadline_us - now + 999ULL) / 1000ULL);
        }
        int rc = mqtt_read_packet(fd, &header, body, sizeof(body), &body_len,
                                  wait_ms);
        if (rc != HAL_NET_OK) return rc;
        if ((header >> 4) == want_type) return HAL_NET_OK;
    }
}

static uint16_t mqtt_next_id(hal_net_mqtt_client_t client) {
    uint16_t *next = &mqtt_next_packet_id[client - 1];
    if (*next == 0) *next = 1;
    uint16_t id = (*next)++;
    if (*next == 0) *next = 1;
    return id;
}

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out) {
    if (!host || !client_id || !out) return HAL_NET_ERR;
    *out = 0;
    hal_net_tcp_client_t tcp = 0;
    int rc = hal_net_tcp_client_open(host, port, timeout_ms, &tcp);
    if (rc != HAL_NET_OK) return rc;
    int *tcp_slot = host_net_slot(tcp_clients, HOST_NET_MAX_TCP_CLIENTS, tcp);
    if (!tcp_slot) return HAL_NET_ERR;
    int fd = *tcp_slot;
    *tcp_slot = -1;

    uint8_t body[1024];
    uint8_t *p = body;
    p = mm_net_mqtt_write_utf8(p, "MQTT");
    *p++ = 4;
    uint8_t flags = 0x02;
    if (user && *user) flags |= 0x80;
    if (pass && *pass) flags |= 0x40;
    *p++ = flags;
    *p++ = 0;
    *p++ = 100;
    p = mm_net_mqtt_write_utf8(p, client_id);
    if (user && *user) p = mm_net_mqtt_write_utf8(p, user);
    if (pass && *pass) p = mm_net_mqtt_write_utf8(p, pass);

    if (mqtt_send_packet(fd, 0x10, body, (size_t)(p - body), timeout_ms) !=
        HAL_NET_OK ||
        mqtt_wait_for_type(fd, 2, timeout_ms) != HAL_NET_OK) {
        close(fd);
        return HAL_NET_ERR;
    }

    uint16_t handle = host_net_alloc(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, fd);
    if (!handle) {
        close(fd);
        return HAL_NET_ERR;
    }
    mqtt_next_packet_id[handle - 1] = 1;
    *out = handle;
    return HAL_NET_OK;
}

int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos, int retain) {
    int *slot = host_net_slot(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, client);
    if (!slot || !topic || (!payload && len)) return HAL_NET_ERR;
    uint8_t body[1024];
    uint8_t *p = body;
    p = mm_net_mqtt_write_utf8(p, topic);
    if (qos) {
        uint16_t id = mqtt_next_id(client);
        *p++ = (uint8_t)(id >> 8);
        *p++ = (uint8_t)id;
    }
    if ((size_t)(p - body) + len > sizeof(body)) return HAL_NET_ERR;
    if (len) {
        memcpy(p, payload, len);
        p += len;
    }
    uint8_t header = 0x30 | (retain ? 0x01 : 0) | (qos == 1 ? 0x02 : 0);
    if (mqtt_send_packet(*slot, header, body, (size_t)(p - body), 5000) !=
        HAL_NET_OK)
        return HAL_NET_ERR;
    if (qos == 1) return mqtt_wait_for_type(*slot, 4, 5000);
    return HAL_NET_OK;
}

int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms) {
    int *slot = host_net_slot(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, client);
    if (!slot || !topic) return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t *p = body;
    uint16_t id = mqtt_next_id(client);
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    *p++ = (uint8_t)qos;
    if (mqtt_send_packet(*slot, 0x82, body, (size_t)(p - body), timeout_ms) !=
        HAL_NET_OK)
        return HAL_NET_ERR;
    return mqtt_wait_for_type(*slot, 9, timeout_ms);
}

int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms) {
    int *slot = host_net_slot(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, client);
    if (!slot || !topic) return HAL_NET_ERR;
    uint8_t body[512];
    uint8_t *p = body;
    uint16_t id = mqtt_next_id(client);
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)id;
    p = mm_net_mqtt_write_utf8(p, topic);
    if (mqtt_send_packet(*slot, 0xA2, body, (size_t)(p - body), timeout_ms) !=
        HAL_NET_OK)
        return HAL_NET_ERR;
    return mqtt_wait_for_type(*slot, 11, timeout_ms);
}

int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len) {
    if (payload_len) *payload_len = 0;
    if (topic && topic_cap) topic[0] = 0;
    int *slot = host_net_slot(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, client);
    if (!slot || !topic || topic_cap == 0 || !payload) return HAL_NET_ERR;
    int wait = host_net_wait_fd(*slot, 0, 1);
    if (wait == HAL_NET_TIMEOUT) return HAL_NET_WOULD_BLOCK;
    if (wait != HAL_NET_OK) return wait;
    uint8_t header = 0;
    uint8_t body[1024];
    size_t body_len = 0;
    int rc = mqtt_read_packet(*slot, &header, body, sizeof(body), &body_len, 1);
    if (rc != HAL_NET_OK) return rc;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_payload_len = 0;
    if (!mm_net_mqtt_decode_publish(header, body, body_len, topic, topic_cap,
                                    &decoded_payload, &decoded_payload_len))
        return HAL_NET_WOULD_BLOCK;
    if (decoded_payload_len > payload_cap) decoded_payload_len = payload_cap;
    if (decoded_payload_len)
        memcpy(payload, decoded_payload, decoded_payload_len);
    if (payload_len) *payload_len = decoded_payload_len;
    return HAL_NET_OK;
}

int hal_net_mqtt_close(hal_net_mqtt_client_t client) {
    int *slot = host_net_slot(mqtt_clients, HOST_NET_MAX_MQTT_CLIENTS, client);
    if (!slot) return HAL_NET_ERR;
    mqtt_send_packet(*slot, 0xE0, NULL, 0, 1000);
    close(*slot);
    *slot = -1;
    mqtt_next_packet_id[client - 1] = 0;
    return HAL_NET_OK;
}
