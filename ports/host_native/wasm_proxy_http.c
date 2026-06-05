#include "wasm_proxy_http.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

static void set_error(wasm_proxy_http_response_t * out, const char * msg) {
    if (!out) return;
    snprintf(out->error, sizeof(out->error), "%s", msg ? msg : "error");
}

static int wait_fd(int fd, int write_ready, int64_t deadline) {
    int rem = remaining_ms(deadline);
    if (rem <= 0) return 0;

    fd_set rfds;
    fd_set wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (write_ready)
        FD_SET(fd, &wfds);
    else
        FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec = rem / 1000;
    tv.tv_usec = (rem % 1000) * 1000;
    int rc = select(fd + 1, write_ready ? NULL : &rfds,
                    write_ready ? &wfds : NULL, NULL, &tv);
    if (rc < 0 && errno == EINTR) return wait_fd(fd, write_ready, deadline);
    return rc;
}

static int connect_with_timeout(const struct addrinfo * ai,
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

static int send_all(int fd, const uint8_t * data, size_t len,
                    int64_t deadline, wasm_proxy_http_response_t * out) {
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
            set_error(out, rc == 0 ? "write timeout" : "write failed");
            return rc == 0 ? WASM_PROXY_HTTP_TIMEOUT
                           : WASM_PROXY_HTTP_IO_FAILED;
        }
        set_error(out, "write failed");
        return WASM_PROXY_HTTP_IO_FAILED;
    }
    return WASM_PROXY_HTTP_OK;
}

static int recv_response(int fd, wasm_proxy_http_response_t * out,
                         size_t cap, int64_t deadline) {
    while (out->len < cap) {
        ssize_t n = recv(fd, out->data + out->len, cap - out->len, 0);
        if (n > 0) {
            out->len += (size_t)n;
            continue;
        }
        if (n == 0) return WASM_PROXY_HTTP_OK;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            int rc = wait_fd(fd, 0, deadline);
            if (rc > 0) continue;
            if (rc == 0 && out->len > 0) return WASM_PROXY_HTTP_OK;
            set_error(out, rc == 0 ? "read timeout" : "read failed");
            return rc == 0 ? WASM_PROXY_HTTP_TIMEOUT
                           : WASM_PROXY_HTTP_IO_FAILED;
        }
        set_error(out, "read failed");
        return WASM_PROXY_HTTP_IO_FAILED;
    }
    out->truncated = 1;
    return WASM_PROXY_HTTP_OK;
}

int wasm_proxy_http_request(const char * host, int port,
                            const uint8_t * request, size_t request_len,
                            size_t max_response_bytes, int timeout_ms,
                            wasm_proxy_http_response_t * out) {
    if (!out) return WASM_PROXY_HTTP_BAD_REQUEST;
    memset(out, 0, sizeof(*out));
    if (!host || !*host || port < 1 || port > 65535 || !request ||
        request_len == 0 ||
        request_len > WASM_PROXY_HTTP_MAX_REQUEST_BYTES ||
        max_response_bytes == 0) {
        set_error(out, "bad request");
        return WASM_PROXY_HTTP_BAD_REQUEST;
    }
    if (max_response_bytes > WASM_PROXY_HTTP_MAX_RESPONSE_BYTES)
        max_response_bytes = WASM_PROXY_HTTP_MAX_RESPONSE_BYTES;
    if (timeout_ms < 1) timeout_ms = 5000;

    out->data = (uint8_t *)malloc(max_response_bytes);
    if (!out->data) {
        set_error(out, "out of memory");
        return WASM_PROXY_HTTP_NO_MEMORY;
    }

    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo * res = NULL;
    int gai = getaddrinfo(host, service, &hints, &res);
    if (gai != 0) {
        snprintf(out->error, sizeof(out->error), "resolve failed: %s",
                 gai_strerror(gai));
        return WASM_PROXY_HTTP_CONNECT_FAILED;
    }

    int64_t deadline = now_ms() + timeout_ms;
    int fd = -1;
    for (const struct addrinfo * ai = res; ai; ai = ai->ai_next) {
        fd = connect_with_timeout(ai, deadline);
        if (fd >= 0) break;
        if (remaining_ms(deadline) <= 0) break;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        set_error(out, remaining_ms(deadline) <= 0 ? "connect timeout"
                                                   : "connect failed");
        return remaining_ms(deadline) <= 0 ? WASM_PROXY_HTTP_TIMEOUT
                                           : WASM_PROXY_HTTP_CONNECT_FAILED;
    }

    int rc = send_all(fd, request, request_len, deadline, out);
    if (rc == WASM_PROXY_HTTP_OK)
        rc = recv_response(fd, out, max_response_bytes, deadline);
    close(fd);
    return rc;
}

void wasm_proxy_http_response_free(wasm_proxy_http_response_t * resp) {
    if (!resp) return;
    free(resp->data);
    resp->data = NULL;
    resp->len = 0;
}
