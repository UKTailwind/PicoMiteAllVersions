/*
 * Native loopback smoke for shared/net/mm_net_websocket.c.
 *
 * This intentionally uses raw POSIX sockets instead of Mongoose. The client is
 * a small standards-compliant raw WebSocket client because the repo does not
 * provide a dependency-free browser/Node WebSocket runtime for native tests.
 */

#include "shared/net/mm_net_websocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

typedef struct {
    int listen_fd;
    int result;
} server_ctx_t;

static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 1;
}

static int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (n == 0) return 0;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 1;
}

static int recv_http_request(int fd, char *buf, size_t buf_len, size_t *len) {
    *len = 0;
    while (*len + 1u < buf_len) {
        ssize_t n = recv(fd, buf + *len, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (n == 0) return 0;
        *len += 1u;
        if (*len >= 4u && memcmp(buf + *len - 4u, "\r\n\r\n", 4) == 0) {
            buf[*len] = 0;
            return 1;
        }
    }
    return 0;
}

static size_t make_client_frame(uint8_t opcode, const uint8_t *payload,
                                size_t payload_len, uint8_t *out,
                                size_t out_len) {
    const uint8_t mask[] = { 0xa5, 0x5a, 0xc3, 0x3c };
    size_t pos = 2;
    if (payload_len > 125u || out_len < 6u + payload_len) return 0;
    out[0] = 0x80u | (opcode & 0x0fu);
    out[1] = 0x80u | (uint8_t)payload_len;
    memcpy(out + pos, mask, sizeof(mask));
    pos += sizeof(mask);
    for (size_t i = 0; i < payload_len; i++) {
        out[pos + i] = payload[i] ^ mask[i & 3u];
    }
    return pos + payload_len;
}

static int server_read_frame(int fd, mm_net_ws_frame_t *frame,
                             uint8_t *payload, size_t payload_len) {
    uint8_t header[4];
    uint8_t buf[256];
    if (!recv_all(fd, header, 2)) return 0;
    size_t wire_len = (size_t)(header[1] & 0x7fu);
    size_t header_len = 2;
    if (wire_len == 126u) {
        if (!recv_all(fd, header + 2, 2)) return 0;
        wire_len = ((size_t)header[2] << 8) | header[3];
        header_len = 4;
    } else if (wire_len == 127u) {
        return 0;
    }
    if (header_len + 4u + wire_len > sizeof(buf)) return 0;
    memcpy(buf, header, header_len);
    if (!recv_all(fd, buf + header_len, 4u + wire_len)) return 0;
    size_t consumed = 0;
    return mm_net_ws_decode_frame(buf, header_len + 4u + wire_len,
                                  payload_len, payload, payload_len, frame,
                                  &consumed) == MM_NET_WS_OK &&
           consumed == header_len + 4u + wire_len;
}

static int client_read_server_frame(int fd, uint8_t *opcode, uint8_t *payload,
                                    size_t payload_cap, size_t *payload_len) {
    uint8_t header[4];
    if (!recv_all(fd, header, 2)) return 0;
    if ((header[1] & 0x80u) != 0) return 0;
    *opcode = header[0] & 0x0fu;
    size_t len = header[1] & 0x7fu;
    if (len == 126u) {
        if (!recv_all(fd, header + 2, 2)) return 0;
        len = ((size_t)header[2] << 8) | header[3];
    } else if (len == 127u) {
        return 0;
    }
    if (len > payload_cap) return 0;
    if (len && !recv_all(fd, payload, len)) return 0;
    *payload_len = len;
    return 1;
}

static void *server_thread(void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)arg;
    int fd = accept(ctx->listen_fd, NULL, NULL);
    if (fd < 0) {
        ctx->result = 1;
        return NULL;
    }

    char request[1024];
    size_t request_len = 0;
    char key[MM_NET_WS_MAX_KEY_LEN];
    char accept_value[MM_NET_WS_ACCEPT_LEN];
    char response[256];
    uint8_t payload[128];
    uint8_t out[128];
    mm_net_ws_frame_t frame;
    size_t written = 0;

    if (!recv_http_request(fd, request, sizeof(request), &request_len) ||
        mm_net_ws_validate_upgrade_request(request, request_len,
                                           "/__web_console/ws", key) !=
            MM_NET_WS_OK ||
        mm_net_ws_compute_accept(key, accept_value) != MM_NET_WS_OK) {
        ctx->result = 2;
        close(fd);
        return NULL;
    }

    int response_len = mm_net_ws_format_upgrade_response(
        response, sizeof(response), accept_value);
    if (response_len < 0 ||
        !send_all(fd, response, (size_t)response_len)) {
        ctx->result = 3;
        close(fd);
        return NULL;
    }

    if (!server_read_frame(fd, &frame, payload, sizeof(payload)) ||
        frame.opcode != MM_NET_WS_OPCODE_PING ||
        mm_net_ws_encode_frame(MM_NET_WS_OPCODE_PONG, payload,
                               frame.payload_len, out, sizeof(out),
                               &written) != MM_NET_WS_OK ||
        !send_all(fd, out, written)) {
        ctx->result = 4;
        close(fd);
        return NULL;
    }

    if (!server_read_frame(fd, &frame, payload, sizeof(payload)) ||
        frame.opcode != MM_NET_WS_OPCODE_TEXT ||
        frame.payload_len != 5 ||
        memcmp(payload, "hello", 5) != 0) {
        ctx->result = 5;
        close(fd);
        return NULL;
    }

    const uint8_t binary_reply[] = { 0x42, 0x24, 0x7e };
    if (mm_net_ws_encode_frame(MM_NET_WS_OPCODE_BINARY, binary_reply,
                               sizeof(binary_reply), out, sizeof(out),
                               &written) != MM_NET_WS_OK ||
        !send_all(fd, out, written)) {
        ctx->result = 6;
        close(fd);
        return NULL;
    }

    if (!server_read_frame(fd, &frame, payload, sizeof(payload)) ||
        frame.opcode != MM_NET_WS_OPCODE_CLOSE ||
        mm_net_ws_encode_frame(MM_NET_WS_OPCODE_CLOSE, payload,
                               frame.payload_len, out, sizeof(out),
                               &written) != MM_NET_WS_OK ||
        !send_all(fd, out, written)) {
        ctx->result = 7;
        close(fd);
        return NULL;
    }

    ctx->result = 0;
    close(fd);
    return NULL;
}

static int connect_loopback(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int run_client(uint16_t port) {
    int fd = connect_loopback(port);
    if (fd < 0) return 1;

    const char request[] =
        "GET /__web_console/ws HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    if (!send_all(fd, request, strlen(request))) {
        close(fd);
        return 2;
    }

    char response[512];
    size_t response_len = 0;
    if (!recv_http_request(fd, response, sizeof(response), &response_len) ||
        !strstr(response, "HTTP/1.1 101 Switching Protocols") ||
        !strstr(response, "Sec-WebSocket-Accept: "
                          "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")) {
        close(fd);
        return 3;
    }

    uint8_t frame[128];
    uint8_t payload[128];
    uint8_t opcode = 0;
    size_t payload_len = 0;
    size_t n = make_client_frame(MM_NET_WS_OPCODE_PING,
                                 (const uint8_t *)"?", 1, frame,
                                 sizeof(frame));
    if (!n || !send_all(fd, frame, n) ||
        !client_read_server_frame(fd, &opcode, payload, sizeof(payload),
                                  &payload_len) ||
        opcode != MM_NET_WS_OPCODE_PONG || payload_len != 1 ||
        payload[0] != '?') {
        close(fd);
        return 4;
    }

    n = make_client_frame(MM_NET_WS_OPCODE_TEXT, (const uint8_t *)"hello", 5,
                          frame, sizeof(frame));
    if (!n || !send_all(fd, frame, n) ||
        !client_read_server_frame(fd, &opcode, payload, sizeof(payload),
                                  &payload_len) ||
        opcode != MM_NET_WS_OPCODE_BINARY || payload_len != 3 ||
        payload[0] != 0x42 || payload[1] != 0x24 || payload[2] != 0x7e) {
        close(fd);
        return 5;
    }

    const uint8_t close_payload[] = { 0x03, 0xe8 };
    n = make_client_frame(MM_NET_WS_OPCODE_CLOSE, close_payload,
                          sizeof(close_payload), frame, sizeof(frame));
    if (!n || !send_all(fd, frame, n) ||
        !client_read_server_frame(fd, &opcode, payload, sizeof(payload),
                                  &payload_len) ||
        opcode != MM_NET_WS_OPCODE_CLOSE || payload_len != 2 ||
        payload[0] != 0x03 || payload[1] != 0xe8) {
        close(fd);
        return 6;
    }

    close(fd);
    return 0;
}

int main(void) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_TRUE(listen_fd >= 0);
    if (listen_fd < 0) return 1;

    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_TRUE(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_TRUE(listen(listen_fd, 1) == 0);

    socklen_t addr_len = sizeof(addr);
    EXPECT_TRUE(getsockname(listen_fd, (struct sockaddr *)&addr,
                            &addr_len) == 0);
    uint16_t port = ntohs(addr.sin_port);

    server_ctx_t ctx;
    ctx.listen_fd = listen_fd;
    ctx.result = -1;
    pthread_t thread;
    EXPECT_TRUE(pthread_create(&thread, NULL, server_thread, &ctx) == 0);

    int client_result = run_client(port);
    EXPECT_TRUE(client_result == 0);

    pthread_join(thread, NULL);
    close(listen_fd);
    EXPECT_TRUE(ctx.result == 0);

    if (failures) return 1;
    puts("mm_net_websocket_loopback_test: PASS");
    return 0;
}
