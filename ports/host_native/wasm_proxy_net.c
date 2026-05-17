#include "wasm_proxy_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct wasm_proxy_tcp_stream {
    int fd;
    int id;
} wasm_proxy_tcp_stream_t;

typedef struct wasm_proxy_tcp_listener {
    int fd;
    int id;
    int port;
} wasm_proxy_tcp_listener_t;

typedef struct wasm_proxy_tcp_server_conn {
    int fd;
    int id;
} wasm_proxy_tcp_server_conn_t;

typedef struct wasm_proxy_udp_socket {
    int fd;
    int id;
    int port;
} wasm_proxy_udp_socket_t;

static wasm_proxy_tcp_stream_t streams[WASM_PROXY_TCP_MAX_STREAMS];
static wasm_proxy_tcp_listener_t listeners[WASM_PROXY_TCP_MAX_LISTENERS];
static wasm_proxy_tcp_server_conn_t server_conns[WASM_PROXY_TCP_MAX_SERVER_CONNS];
static wasm_proxy_udp_socket_t udp_sockets[WASM_PROXY_UDP_MAX_SOCKETS];
static int next_stream_id = 1;
static int next_listener_id = 1;
static int next_server_conn_id = 1;
static int next_udp_id = 1;

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

static int remaining_ms(int64_t deadline) {
    int64_t rem = deadline - now_ms();
    if (rem <= 0) return 0;
    return rem > 60000 ? 60000 : (int)rem;
}

static void set_error(char *error, size_t error_len, const char *msg) {
    if (!error || error_len == 0) return;
    snprintf(error, error_len, "%s", msg ? msg : "error");
}

static int wait_fd(int fd, int write_ready, int64_t deadline) {
    int rem = remaining_ms(deadline);
    if (rem <= 0) return 0;

    fd_set rfds;
    fd_set wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (write_ready) FD_SET(fd, &wfds);
    else FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec = rem / 1000;
    tv.tv_usec = (rem % 1000) * 1000;
    int rc = select(fd + 1, write_ready ? NULL : &rfds,
                    write_ready ? &wfds : NULL, NULL, &tv);
    if (rc < 0 && errno == EINTR) return wait_fd(fd, write_ready, deadline);
    return rc;
}

static int connect_with_timeout(const struct addrinfo *ai,
                                int64_t deadline) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    if (rc != 0) {
        rc = wait_fd(fd, 1, deadline);
        if (rc <= 0) {
            close(fd);
            errno = rc == 0 ? ETIMEDOUT : errno;
            return -1;
        }
        int err = 0;
        socklen_t err_len = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 ||
            err != 0) {
            close(fd);
            errno = err ? err : errno;
            return -1;
        }
    }
    return fd;
}

static wasm_proxy_tcp_stream_t *find_stream(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < WASM_PROXY_TCP_MAX_STREAMS; ++i) {
        if (streams[i].fd >= 0 && streams[i].id == id) return &streams[i];
    }
    return NULL;
}

static wasm_proxy_tcp_stream_t *free_stream_slot(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_STREAMS; ++i) {
        if (streams[i].fd < 0) return &streams[i];
    }
    return NULL;
}

static wasm_proxy_tcp_listener_t *find_listener(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < WASM_PROXY_TCP_MAX_LISTENERS; ++i) {
        if (listeners[i].fd >= 0 && listeners[i].id == id)
            return &listeners[i];
    }
    return NULL;
}

static wasm_proxy_tcp_listener_t *free_listener_slot(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_LISTENERS; ++i) {
        if (listeners[i].fd < 0) return &listeners[i];
    }
    return NULL;
}

static wasm_proxy_tcp_server_conn_t *find_server_conn(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < WASM_PROXY_TCP_MAX_SERVER_CONNS; ++i) {
        if (server_conns[i].fd >= 0 && server_conns[i].id == id)
            return &server_conns[i];
    }
    return NULL;
}

static wasm_proxy_tcp_server_conn_t *free_server_conn_slot(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_SERVER_CONNS; ++i) {
        if (server_conns[i].fd < 0) return &server_conns[i];
    }
    return NULL;
}

static wasm_proxy_udp_socket_t *find_udp_socket(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < WASM_PROXY_UDP_MAX_SOCKETS; ++i) {
        if (udp_sockets[i].fd >= 0 && udp_sockets[i].id == id)
            return &udp_sockets[i];
    }
    return NULL;
}

static wasm_proxy_udp_socket_t *free_udp_slot(void) {
    for (int i = 0; i < WASM_PROXY_UDP_MAX_SOCKETS; ++i) {
        if (udp_sockets[i].fd < 0) return &udp_sockets[i];
    }
    return NULL;
}

static int send_all(int fd, const uint8_t *data, size_t len,
                    int64_t deadline, char *error, size_t error_len) {
    size_t pos = 0;
    while (pos < len) {
        ssize_t n = send(fd, data + pos, len - pos, 0);
        if (n > 0) {
            pos += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR)) {
            int rc = wait_fd(fd, 1, deadline);
            if (rc > 0) continue;
            set_error(error, error_len,
                      rc == 0 ? "write timeout" : "write failed");
            return rc == 0 ? WASM_PROXY_TCP_TIMEOUT
                           : WASM_PROXY_TCP_IO_FAILED;
        }
        set_error(error, error_len, "write failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    return WASM_PROXY_TCP_OK;
}

static int read_available(int fd, uint8_t *data, size_t cap,
                          size_t *out_len, int *out_closed,
                          char *error, size_t error_len) {
    *out_len = 0;
    if (out_closed) *out_closed = 0;
    while (*out_len < cap) {
        ssize_t n = recv(fd, data + *out_len, cap - *out_len, 0);
        if (n > 0) {
            *out_len += (size_t)n;
            continue;
        }
        if (n == 0) {
            if (out_closed) *out_closed = 1;
            return WASM_PROXY_TCP_OK;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return WASM_PROXY_TCP_OK;
        set_error(error, error_len, "read failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    return WASM_PROXY_TCP_OK;
}

static int read_initial_event(int fd, uint8_t *data, size_t cap,
                              size_t *out_len, int wait_ms,
                              char *error, size_t error_len) {
    if (out_len) *out_len = 0;
    if (!data || cap == 0 || !out_len) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    int64_t deadline = now_ms() + (wait_ms > 0 ? wait_ms : 250);
    int waited = wait_fd(fd, 0, deadline);
    if (waited == 0) return WASM_PROXY_TCP_OK;
    if (waited < 0) {
        set_error(error, error_len, "read failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    ssize_t n = recv(fd, data, cap, 0);
    if (n > 0) {
        *out_len = (size_t)n;
        return WASM_PROXY_TCP_OK;
    }
    if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK ||
        errno == EINTR) {
        return WASM_PROXY_TCP_OK;
    }
    set_error(error, error_len, "read failed");
    return WASM_PROXY_TCP_IO_FAILED;
}

__attribute__((constructor))
static void init_streams(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_STREAMS; ++i) {
        streams[i].fd = -1;
        streams[i].id = 0;
    }
    for (int i = 0; i < WASM_PROXY_TCP_MAX_LISTENERS; ++i) {
        listeners[i].fd = -1;
        listeners[i].id = 0;
        listeners[i].port = 0;
    }
    for (int i = 0; i < WASM_PROXY_TCP_MAX_SERVER_CONNS; ++i) {
        server_conns[i].fd = -1;
        server_conns[i].id = 0;
    }
    for (int i = 0; i < WASM_PROXY_UDP_MAX_SOCKETS; ++i) {
        udp_sockets[i].fd = -1;
        udp_sockets[i].id = 0;
        udp_sockets[i].port = 0;
    }
}

int wasm_proxy_tcp_open(const char *host, int port, int timeout_ms,
                        int *out_id, char *error, size_t error_len) {
    if (out_id) *out_id = 0;
    if (!host || !*host || port < 1 || port > 65535 || !out_id) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    if (timeout_ms < 1) timeout_ms = 5000;

    wasm_proxy_tcp_stream_t *slot = free_stream_slot();
    if (!slot) {
        set_error(error, error_len, "too many streams");
        return WASM_PROXY_TCP_NO_MEMORY;
    }

    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, service, &hints, &res);
    if (gai != 0) {
        if (error && error_len)
            snprintf(error, error_len, "resolve failed: %s",
                     gai_strerror(gai));
        return WASM_PROXY_TCP_CONNECT_FAILED;
    }

    int64_t deadline = now_ms() + timeout_ms;
    int fd = -1;
    for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = connect_with_timeout(ai, deadline);
        if (fd >= 0) break;
        if (remaining_ms(deadline) <= 0) break;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        set_error(error, error_len,
                  remaining_ms(deadline) <= 0 ? "connect timeout"
                                              : "connect failed");
        return remaining_ms(deadline) <= 0 ? WASM_PROXY_TCP_TIMEOUT
                                           : WASM_PROXY_TCP_CONNECT_FAILED;
    }

    slot->fd = fd;
    slot->id = next_stream_id++;
    if (next_stream_id <= 0) next_stream_id = 1;
    *out_id = slot->id;
    return WASM_PROXY_TCP_OK;
}

int wasm_proxy_tcp_write_read(int id, const uint8_t *write_data,
                              size_t write_len, uint8_t *read_data,
                              size_t read_cap, size_t *out_read_len,
                              int timeout_ms, int *out_closed,
                              char *error, size_t error_len) {
    if (out_read_len) *out_read_len = 0;
    if (out_closed) *out_closed = 0;
    if (!out_read_len || (!read_data && read_cap) ||
        (!write_data && write_len) ||
        write_len > WASM_PROXY_TCP_MAX_WRITE_BYTES ||
        read_cap > WASM_PROXY_TCP_MAX_READ_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_stream_t *s = find_stream(id);
    if (!s) {
        set_error(error, error_len, "stream not found");
        return WASM_PROXY_TCP_NOT_FOUND;
    }
    if (timeout_ms < 1) timeout_ms = 5000;

    int rc = WASM_PROXY_TCP_OK;
    if (write_len) {
        rc = send_all(s->fd, write_data, write_len, now_ms() + timeout_ms,
                      error, error_len);
    }
    if (rc == WASM_PROXY_TCP_OK && read_cap) {
        rc = read_available(s->fd, read_data, read_cap, out_read_len,
                            out_closed, error, error_len);
    }
    if (out_closed && *out_closed) {
        close(s->fd);
        s->fd = -1;
        s->id = 0;
    }
    return rc;
}

int wasm_proxy_tcp_close(int id) {
    wasm_proxy_tcp_stream_t *s = find_stream(id);
    if (!s) return WASM_PROXY_TCP_NOT_FOUND;
    close(s->fd);
    s->fd = -1;
    s->id = 0;
    return WASM_PROXY_TCP_OK;
}

void wasm_proxy_tcp_close_all(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_STREAMS; ++i) {
        if (streams[i].fd >= 0) close(streams[i].fd);
        streams[i].fd = -1;
        streams[i].id = 0;
    }
}

int wasm_proxy_tcp_listen(int port, int backlog, int *out_id,
                          char *error, size_t error_len) {
    if (out_id) *out_id = 0;
    if (!out_id || port < 1 || port > 65535) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_listener_t *slot = free_listener_slot();
    if (!slot) {
        set_error(error, error_len, "too many listeners");
        return WASM_PROXY_TCP_NO_MEMORY;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        set_error(error, error_len, "listener socket failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, backlog > 0 ? backlog : 1) != 0) {
        close(fd);
        set_error(error, error_len, "tcp listen failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    slot->fd = fd;
    slot->port = port;
    slot->id = next_listener_id++;
    if (next_listener_id <= 0) next_listener_id = 1;
    *out_id = slot->id;
    return WASM_PROXY_TCP_OK;
}

int wasm_proxy_tcp_listener_close(int id) {
    wasm_proxy_tcp_listener_t *s = find_listener(id);
    if (!s) return WASM_PROXY_TCP_NOT_FOUND;
    close(s->fd);
    s->fd = -1;
    s->id = 0;
    s->port = 0;
    return WASM_PROXY_TCP_OK;
}

int wasm_proxy_tcp_accept_conn(int listener_id, int *out_conn_id,
                               char *error, size_t error_len) {
    if (out_conn_id) *out_conn_id = 0;
    if (!out_conn_id) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_listener_t *listener = find_listener(listener_id);
    if (!listener) {
        set_error(error, error_len, "listener not found");
        return WASM_PROXY_TCP_NOT_FOUND;
    }

    int fd = accept(listener->fd, NULL, NULL);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return WASM_PROXY_TCP_TIMEOUT;
        set_error(error, error_len, "accept failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    wasm_proxy_tcp_server_conn_t *slot = free_server_conn_slot();
    if (!slot) {
        close(fd);
        set_error(error, error_len, "too many server connections");
        return WASM_PROXY_TCP_NO_MEMORY;
    }

    slot->fd = fd;
    slot->id = next_server_conn_id++;
    if (next_server_conn_id <= 0) next_server_conn_id = 1;
    *out_conn_id = slot->id;
    return WASM_PROXY_TCP_OK;
}

int wasm_proxy_tcp_accept_event(int listener_id, uint8_t *read_data,
                                size_t read_cap, size_t *out_read_len,
                                int *out_conn_id, char *error,
                                size_t error_len) {
    if (out_read_len) *out_read_len = 0;
    if (out_conn_id) *out_conn_id = 0;
    if (!out_read_len || !out_conn_id || !read_data || read_cap == 0 ||
        read_cap > WASM_PROXY_TCP_MAX_READ_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_listener_t *listener = find_listener(listener_id);
    if (!listener) {
        set_error(error, error_len, "listener not found");
        return WASM_PROXY_TCP_NOT_FOUND;
    }

    int fd = accept(listener->fd, NULL, NULL);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return WASM_PROXY_TCP_TIMEOUT;
        set_error(error, error_len, "accept failed");
        return WASM_PROXY_TCP_IO_FAILED;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    wasm_proxy_tcp_server_conn_t *slot = free_server_conn_slot();
    if (!slot) {
        close(fd);
        set_error(error, error_len, "too many server connections");
        return WASM_PROXY_TCP_NO_MEMORY;
    }

    int rc = read_initial_event(fd, read_data, read_cap, out_read_len, 250,
                                error, error_len);
    if (rc != WASM_PROXY_TCP_OK || *out_read_len == 0) {
        close(fd);
        if (rc == WASM_PROXY_TCP_OK) return WASM_PROXY_TCP_TIMEOUT;
        return rc;
    }

    slot->fd = fd;
    slot->id = next_server_conn_id++;
    if (next_server_conn_id <= 0) next_server_conn_id = 1;
    *out_conn_id = slot->id;
    return WASM_PROXY_TCP_OK;
}

int wasm_proxy_tcp_conn_recv(int conn_id, uint8_t *read_data,
                             size_t read_cap, size_t *out_read_len,
                             int *out_closed, char *error,
                             size_t error_len) {
    if (out_read_len) *out_read_len = 0;
    if (out_closed) *out_closed = 0;
    if (!out_read_len || !read_data || read_cap == 0 ||
        read_cap > WASM_PROXY_TCP_MAX_READ_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_server_conn_t *s = find_server_conn(conn_id);
    if (!s) {
        set_error(error, error_len, "connection not found");
        return WASM_PROXY_TCP_NOT_FOUND;
    }
    int rc = read_available(s->fd, read_data, read_cap, out_read_len,
                            out_closed, error, error_len);
    if (rc == WASM_PROXY_TCP_OK && *out_read_len == 0 &&
        (!out_closed || !*out_closed))
        return WASM_PROXY_TCP_TIMEOUT;
    if (rc == WASM_PROXY_TCP_OK && out_closed && *out_closed) {
        close(s->fd);
        s->fd = -1;
        s->id = 0;
    }
    return rc;
}

int wasm_proxy_tcp_conn_send(int conn_id, const uint8_t *data, size_t len,
                             int timeout_ms, char *error, size_t error_len) {
    if ((!data && len) || len > WASM_PROXY_TCP_MAX_WRITE_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_TCP_BAD_REQUEST;
    }
    wasm_proxy_tcp_server_conn_t *s = find_server_conn(conn_id);
    if (!s) {
        set_error(error, error_len, "connection not found");
        return WASM_PROXY_TCP_NOT_FOUND;
    }
    if (timeout_ms < 1) timeout_ms = 5000;
    return send_all(s->fd, data, len, now_ms() + timeout_ms, error,
                    error_len);
}

int wasm_proxy_tcp_conn_close(int conn_id) {
    wasm_proxy_tcp_server_conn_t *s = find_server_conn(conn_id);
    if (!s) return WASM_PROXY_TCP_NOT_FOUND;
    close(s->fd);
    s->fd = -1;
    s->id = 0;
    return WASM_PROXY_TCP_OK;
}

void wasm_proxy_tcp_listeners_close_all(void) {
    for (int i = 0; i < WASM_PROXY_TCP_MAX_LISTENERS; ++i) {
        if (listeners[i].fd >= 0) close(listeners[i].fd);
        listeners[i].fd = -1;
        listeners[i].id = 0;
        listeners[i].port = 0;
    }
    for (int i = 0; i < WASM_PROXY_TCP_MAX_SERVER_CONNS; ++i) {
        if (server_conns[i].fd >= 0) close(server_conns[i].fd);
        server_conns[i].fd = -1;
        server_conns[i].id = 0;
    }
}

int wasm_proxy_udp_bind(int port, int *out_id, char *error,
                        size_t error_len) {
    if (out_id) *out_id = 0;
    if (!out_id || port < 0 || port > 65535) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_UDP_BAD_REQUEST;
    }
    wasm_proxy_udp_socket_t *slot = free_udp_slot();
    if (!slot) {
        set_error(error, error_len, "too many udp sockets");
        return WASM_PROXY_UDP_NO_MEMORY;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        set_error(error, error_len, "udp socket failed");
        return WASM_PROXY_UDP_BIND_FAILED;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        set_error(error, error_len, "udp bind failed");
        return WASM_PROXY_UDP_BIND_FAILED;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    slot->fd = fd;
    slot->port = port;
    slot->id = next_udp_id++;
    if (next_udp_id <= 0) next_udp_id = 1;
    *out_id = slot->id;
    return WASM_PROXY_UDP_OK;
}

int wasm_proxy_udp_close(int id) {
    wasm_proxy_udp_socket_t *s = find_udp_socket(id);
    if (!s) return WASM_PROXY_UDP_NOT_FOUND;
    close(s->fd);
    s->fd = -1;
    s->id = 0;
    s->port = 0;
    return WASM_PROXY_UDP_OK;
}

int wasm_proxy_udp_send(int id, const char *host, int port,
                        const uint8_t *data, size_t len, int timeout_ms,
                        char *error, size_t error_len) {
    if (!host || !*host || port < 1 || port > 65535 || (!data && len) ||
        len > WASM_PROXY_UDP_MAX_DATAGRAM_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_UDP_BAD_REQUEST;
    }
    if (timeout_ms < 1) timeout_ms = 5000;

    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, service, &hints, &res);
    if (gai != 0) {
        if (error && error_len)
            snprintf(error, error_len, "resolve failed: %s",
                     gai_strerror(gai));
        return WASM_PROXY_UDP_IO_FAILED;
    }

    int owned_fd = -1;
    int fd = -1;
    if (id > 0) {
        wasm_proxy_udp_socket_t *s = find_udp_socket(id);
        if (!s) {
            freeaddrinfo(res);
            set_error(error, error_len, "udp socket not found");
            return WASM_PROXY_UDP_NOT_FOUND;
        }
        fd = s->fd;
    }

    int64_t deadline = now_ms() + timeout_ms;
    int rc = WASM_PROXY_UDP_IO_FAILED;
    for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (id <= 0) {
            if (owned_fd >= 0) close(owned_fd);
            owned_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (owned_fd < 0) continue;
            fd = owned_fd;
        }

        ssize_t n = sendto(fd, data, len, 0, ai->ai_addr, ai->ai_addrlen);
        if (n == (ssize_t)len) {
            rc = WASM_PROXY_UDP_OK;
            break;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR)) {
            int wr = wait_fd(fd, 1, deadline);
            if (wr <= 0) {
                rc = wr == 0 ? WASM_PROXY_UDP_TIMEOUT
                             : WASM_PROXY_UDP_IO_FAILED;
                break;
            }
            n = sendto(fd, data, len, 0, ai->ai_addr, ai->ai_addrlen);
            if (n == (ssize_t)len) {
                rc = WASM_PROXY_UDP_OK;
                break;
            }
        }
    }

    if (owned_fd >= 0) close(owned_fd);
    freeaddrinfo(res);
    if (rc != WASM_PROXY_UDP_OK) {
        set_error(error, error_len,
                  rc == WASM_PROXY_UDP_TIMEOUT ? "udp send timeout"
                                               : "udp send failed");
    }
    return rc;
}

int wasm_proxy_udp_recv(int id, wasm_proxy_udp_packet_t *packet,
                        char *error, size_t error_len) {
    if (!packet || !packet->data || packet->cap == 0 ||
        packet->cap > WASM_PROXY_UDP_MAX_DATAGRAM_BYTES) {
        set_error(error, error_len, "bad request");
        return WASM_PROXY_UDP_BAD_REQUEST;
    }
    packet->len = 0;
    packet->address[0] = 0;
    packet->family = 0;
    packet->port = 0;

    wasm_proxy_udp_socket_t *s = find_udp_socket(id);
    if (!s) {
        set_error(error, error_len, "udp socket not found");
        return WASM_PROXY_UDP_NOT_FOUND;
    }

    struct sockaddr_storage from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(s->fd, packet->data, packet->cap, 0,
                         (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return WASM_PROXY_UDP_WOULD_BLOCK;
        set_error(error, error_len, "udp recv failed");
        return WASM_PROXY_UDP_IO_FAILED;
    }
    packet->len = (size_t)n;
    if (from.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&from;
        inet_ntop(AF_INET, &sin->sin_addr, packet->address,
                  sizeof(packet->address));
        packet->family = 4;
        packet->port = ntohs(sin->sin_port);
    } else if (from.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&from;
        inet_ntop(AF_INET6, &sin6->sin6_addr, packet->address,
                  sizeof(packet->address));
        packet->family = 6;
        packet->port = ntohs(sin6->sin6_port);
    }
    return WASM_PROXY_UDP_OK;
}

void wasm_proxy_udp_close_all(void) {
    for (int i = 0; i < WASM_PROXY_UDP_MAX_SOCKETS; ++i) {
        if (udp_sockets[i].fd >= 0) close(udp_sockets[i].fd);
        udp_sockets[i].fd = -1;
        udp_sockets[i].id = 0;
        udp_sockets[i].port = 0;
    }
}
