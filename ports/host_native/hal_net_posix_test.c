/*
 * ports/host_native/hal_net_posix_test.c - host HAL network conformance.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "hal/hal_filesystem.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"
#include "shared/net/mm_net_mqtt_wire.h"
#include "shared/net/mm_net_ntp.h"
#include "shared/net/mm_net_ntp_hal.h"
#include "shared/net/mm_net_tftp.h"

#define TCP_CLIENT_PORT 19180
#define TCP_SERVER_PORT 19181
#define UDP_BIND_PORT 19182
#define UDP_SEND_PORT 19183
#define NTP_HAL_PORT 19184
#define MQTT_HAL_PORT 19185
#define TCP_SERVER_CONN_PORT 19186

static void fail(const char *msg) {
    fprintf(stderr, "host net HAL conformance failed: %s\n", msg);
    exit(1);
}

uint64_t hal_time_us_64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void hal_time_sleep_us(uint32_t us) {
    struct timespec req;
    req.tv_sec = (time_t)(us / 1000000u);
    req.tv_nsec = (long)((us % 1000000u) * 1000u);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

uint32_t hal_time_ms_tick(void) {
    return (uint32_t)(hal_time_us_64() / 1000ULL);
}

void hal_time_slowdown_tick(void) {
}

static int raw_tcp_connect(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) fail("raw tcp socket");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        fail("raw tcp connect");
    return fd;
}

static int raw_tcp_listen(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) fail("raw listen socket");
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        fail("raw listen bind");
    if (listen(fd, 1) != 0) fail("raw listen");
    return fd;
}

static void *tcp_client_peer(void *arg) {
    int listen_fd = *(int *)arg;
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) fail("tcp client peer accept");
    char buf[32];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n != 4 || memcmp(buf, "PING", 4) != 0) fail("tcp client peer recv");
    if (send(fd, "ACK", 3, 0) != 3) fail("tcp client peer send");
    close(fd);
    return NULL;
}

static void test_tcp_client(void) {
    int listen_fd = raw_tcp_listen(TCP_CLIENT_PORT);
    pthread_t thread;
    if (pthread_create(&thread, NULL, tcp_client_peer, &listen_fd) != 0)
        fail("tcp client peer thread");
    hal_net_tcp_client_t client = 0;
    if (hal_net_tcp_client_open("127.0.0.1", TCP_CLIENT_PORT, 1000, &client) != HAL_NET_OK)
        fail("hal tcp client open");
    if (hal_net_tcp_client_send(client, "PING", 4, 1000) != HAL_NET_OK)
        fail("hal tcp client send");
    char buf[16];
    size_t len = 0;
    if (hal_net_tcp_client_recv(client, buf, sizeof(buf), &len, 1000) != HAL_NET_OK)
        fail("hal tcp client recv");
    if (len != 3 || memcmp(buf, "ACK", 3) != 0)
        fail("hal tcp client payload");
    if (hal_net_tcp_client_close(client) != HAL_NET_OK)
        fail("hal tcp client close");
    pthread_join(thread, NULL);
    close(listen_fd);
}

static void test_tcp_server(void) {
    hal_net_tcp_server_t server = 0;
    if (hal_net_tcp_server_open(TCP_SERVER_PORT, 1, &server) != HAL_NET_OK)
        fail("hal tcp server open");
    int client = raw_tcp_connect(TCP_SERVER_PORT);
    const char request[] = "GET /host-hal HTTP/1.0\r\n\r\n";
    if (send(client, request, sizeof(request) - 1, 0) != (ssize_t)(sizeof(request) - 1))
        fail("raw tcp server request send");
    hal_net_tcp_conn_t conn = 0;
    unsigned char buf[128];
    size_t len = 0;
    int rc = HAL_NET_WOULD_BLOCK;
    for (int i = 0; i < 20 && rc == HAL_NET_WOULD_BLOCK; ++i) {
        rc = hal_net_tcp_accept_event(server, &conn, buf, sizeof(buf), &len);
        if (rc == HAL_NET_WOULD_BLOCK) usleep(50000);
    }
    if (rc != HAL_NET_OK || conn == 0) fail("hal tcp server accept");
    if (len < 14 || memcmp(buf, "GET /host-hal", 13) != 0)
        fail("hal tcp server request payload");
    if (hal_net_tcp_conn_send(conn, "OK", 2, 1000) != HAL_NET_OK)
        fail("hal tcp server send");
    char reply[8];
    ssize_t n = recv(client, reply, sizeof(reply), 0);
    if (n != 2 || memcmp(reply, "OK", 2) != 0)
        fail("raw tcp server reply");
    if (hal_net_tcp_conn_close(conn) != HAL_NET_OK)
        fail("hal tcp conn close");
    if (hal_net_tcp_server_close(server) != HAL_NET_OK)
        fail("hal tcp server close");
    close(client);
}

static void test_tcp_server_conn_recv(void) {
    hal_net_tcp_server_t server = 0;
    if (hal_net_tcp_server_open(TCP_SERVER_CONN_PORT, 1, &server) != HAL_NET_OK)
        fail("hal tcp server conn open");
    int client = raw_tcp_connect(TCP_SERVER_CONN_PORT);
    hal_net_tcp_conn_t conn = 0;
    int rc = HAL_NET_WOULD_BLOCK;
    for (int i = 0; i < 20 && rc == HAL_NET_WOULD_BLOCK; ++i) {
        rc = hal_net_tcp_accept_conn(server, &conn);
        if (rc == HAL_NET_WOULD_BLOCK) usleep(50000);
    }
    if (rc != HAL_NET_OK || conn == 0) fail("hal tcp accept conn");
    unsigned char buf[16];
    size_t len = 99;
    if (hal_net_tcp_conn_recv(conn, buf, sizeof(buf), &len) !=
            HAL_NET_WOULD_BLOCK ||
        len != 0)
        fail("hal tcp conn recv would block");
    const char payload[] = "TELNET";
    if (send(client, payload, sizeof(payload) - 1, 0) !=
        (ssize_t)(sizeof(payload) - 1))
        fail("raw tcp conn payload send");
    rc = HAL_NET_WOULD_BLOCK;
    for (int i = 0; i < 20 && rc == HAL_NET_WOULD_BLOCK; ++i) {
        rc = hal_net_tcp_conn_recv(conn, buf, sizeof(buf), &len);
        if (rc == HAL_NET_WOULD_BLOCK) usleep(50000);
    }
    if (rc != HAL_NET_OK || len != sizeof(payload) - 1 ||
        memcmp(buf, payload, sizeof(payload) - 1) != 0)
        fail("hal tcp conn recv payload");
    if (hal_net_tcp_conn_close(conn) != HAL_NET_OK)
        fail("hal tcp conn recv close");
    if (hal_net_tcp_server_close(server) != HAL_NET_OK)
        fail("hal tcp conn server close");
    close(client);
}

static int raw_udp_bound(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) fail("raw udp socket");
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        fail("raw udp bind");
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static void test_udp(void) {
    hal_net_udp_socket_t sock = 0;
    if (hal_net_udp_bind(UDP_BIND_PORT, &sock) != HAL_NET_OK)
        fail("hal udp bind");
    int raw_sender = socket(AF_INET, SOCK_DGRAM, 0);
    if (raw_sender < 0) fail("raw udp sender");
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(UDP_BIND_PORT);
    inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);
    if (sendto(raw_sender, "UIN", 3, 0, (struct sockaddr *)&target, sizeof(target)) != 3)
        fail("raw udp send to hal");
    char buf[32];
    size_t len = 0;
    hal_net_addr_t from;
    int rc = HAL_NET_WOULD_BLOCK;
    for (int i = 0; i < 20 && rc == HAL_NET_WOULD_BLOCK; ++i) {
        rc = hal_net_udp_recv_event(sock, &from, buf, sizeof(buf), &len);
        if (rc == HAL_NET_WOULD_BLOCK) usleep(50000);
    }
    if (rc != HAL_NET_OK || len != 3 || memcmp(buf, "UIN", 3) != 0 || from.family != 4)
        fail("hal udp recv");
    int raw_receiver = raw_udp_bound(UDP_SEND_PORT);
    if (hal_net_udp_send("127.0.0.1", UDP_SEND_PORT, "UOUT", 4, 1000) != HAL_NET_OK)
        fail("hal udp send");
    ssize_t n = recv(raw_receiver, buf, sizeof(buf), 0);
    if (n != 4 || memcmp(buf, "UOUT", 4) != 0)
        fail("raw udp receive from hal");
    if (hal_net_udp_socket_send(sock, "127.0.0.1", UDP_SEND_PORT, "USOCK", 5, 1000) != HAL_NET_OK)
        fail("hal udp socket send");
    n = recv(raw_receiver, buf, sizeof(buf), 0);
    if (n != 5 || memcmp(buf, "USOCK", 5) != 0)
        fail("raw udp receive from hal socket");
    if (hal_net_udp_close(sock) != HAL_NET_OK)
        fail("hal udp close");
    close(raw_sender);
    close(raw_receiver);
}

static void test_shared_http(void) {
    char path[32];
    const char request[] = "GET /host-hal?x=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    size_t len = mm_net_http_extract_path(request, sizeof(request) - 1,
                                          path, sizeof(path));
    if (len != 13 || strcmp(path, "/host-hal?x=1") != 0)
        fail("shared http path");

    if (strcmp(mm_net_http_mime_from_name("index.HTM", "fallback"),
               "text/html") != 0)
        fail("shared http html mime");
    if (strcmp(mm_net_http_mime_from_name("script.mjs", "fallback"),
               "application/javascript") != 0)
        fail("shared http js mime");
    if (strcmp(mm_net_http_mime_from_name("asset.unknown", "fallback"),
               "fallback") != 0)
        fail("shared http fallback mime");
    if (strcmp(mm_net_http_status_reason(404), "Not Found") != 0)
        fail("shared http status reason");

    char body[64];
    int body_len = mm_net_http_format_status_body(body, sizeof(body),
                                                  404, NULL);
    if (body_len <= 0 || strcmp(body, "404 Not Found\r\n") != 0)
        fail("shared http status body");

    char header[256];
    int n = mm_net_http_format_response_header(
        header, sizeof(header), 200, NULL, "HostNet", "text/plain", 3);
    if (n <= 0 || strstr(header, "HTTP/1.1 200 OK\r\n") != header ||
        strstr(header, "Server: HostNet\r\n") == NULL ||
        strstr(header, "Content-Type: text/plain\r\n") == NULL ||
        strstr(header, "Content-Length: 3\r\n") == NULL)
        fail("shared http response header");
}

struct http_capture {
    char buf[2048];
    size_t len;
    int calls;
};

static int http_capture_send(void *ctx, const void *buf, size_t len) {
    struct http_capture *cap = (struct http_capture *)ctx;
    if (cap->len + len >= sizeof(cap->buf)) return 0;
    memcpy(cap->buf + cap->len, buf, len);
    cap->len += len;
    cap->buf[cap->len] = 0;
    cap->calls++;
    return 1;
}

static void test_shared_http_response_sender(void) {
    struct http_capture cap;
    memset(&cap, 0, sizeof(cap));
    const char body[] = "HOST";
    if (mm_net_http_send_response(201, "Created", "text/plain",
                                  body, sizeof(body) - 1, "HostNet",
                                  http_capture_send, &cap) != 0)
        fail("shared http send response rc");
    if (cap.calls != 2) fail("shared http send response call count");
    if (strstr(cap.buf, "HTTP/1.1 201 Created\r\n") != cap.buf ||
        strstr(cap.buf, "Server: HostNet\r\n") == NULL ||
        strstr(cap.buf, "Content-Type: text/plain\r\n") == NULL ||
        strstr(cap.buf, "Content-Length: 4\r\n") == NULL ||
        strstr(cap.buf, "\r\n\r\nHOST") == NULL)
        fail("shared http send response wire image");

    memset(&cap, 0, sizeof(cap));
    if (mm_net_http_send_response(204, "No Content", "text/plain",
                                  NULL, 7, "HostNet",
                                  http_capture_send, &cap) != 0)
        fail("shared http send header-only response rc");
    if (cap.calls != 1 ||
        strstr(cap.buf, "HTTP/1.1 204 No Content\r\n") != cap.buf ||
        strstr(cap.buf, "Content-Length: 7\r\n") == NULL)
        fail("shared http send header-only response");
    if (mm_net_http_send_response(200, NULL, NULL, NULL, 0,
                                  NULL, NULL, NULL) != -2)
        fail("shared http send response rejects missing callback");
}

static const char fake_file_name[] = "host_file.txt";
static const char fake_file_body[] = "Host file body\r\n";
static size_t fake_file_pos;

int hal_fs_stat(const char *path, struct hal_stat *out) {
    if (!path || strcmp(path, fake_file_name) != 0 || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->size = (off_t)(sizeof(fake_file_body) - 1);
    out->mode = HAL_FS_S_IFREG;
    return 0;
}

int hal_fs_open(const char *path, int flags, hal_fs_fd_t *out) {
    if (!path || strcmp(path, fake_file_name) != 0 ||
        flags != HAL_FS_O_RDONLY || !out)
        return -1;
    fake_file_pos = 0;
    *out = 1;
    return 0;
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void *buf, size_t n) {
    if (fd != 1 || !buf) return -1;
    size_t remaining = (sizeof(fake_file_body) - 1) - fake_file_pos;
    if (remaining == 0) return 0;
    if (n > remaining) n = remaining;
    memcpy(buf, fake_file_body + fake_file_pos, n);
    fake_file_pos += n;
    return (ssize_t)n;
}

int hal_fs_close(hal_fs_fd_t fd) {
    return fd == 1 ? 0 : -1;
}

static void test_shared_http_file_sender(void) {
    struct http_capture cap;
    memset(&cap, 0, sizeof(cap));
    if (mm_net_http_send_file(fake_file_name, NULL, "HostNet",
                              http_capture_send, &cap) != 0)
        fail("shared http send file rc");
    if (cap.calls < 2) fail("shared http send file call count");
    if (strstr(cap.buf, "HTTP/1.1 200 OK\r\n") != cap.buf ||
        strstr(cap.buf, "Server: HostNet\r\n") == NULL ||
        strstr(cap.buf, "Content-Type: text/plain\r\n") == NULL ||
        strstr(cap.buf, "Content-Length: 16\r\n") == NULL ||
        strstr(cap.buf, "\r\n\r\nHost file body\r\n") == NULL)
        fail("shared http send file wire image");
    if (mm_net_http_send_file("missing.txt", NULL, "HostNet",
                              http_capture_send, &cap) != -1)
        fail("shared http send file missing");
}

struct tftp_capture {
    char file[1024];
    size_t len;
    size_t pos;
    uint8_t sent[4][600];
    size_t sent_len[4];
    int sent_count;
};

static int tftp_capture_open(void *ctx, const char *filename, int write,
                             void **handle) {
    struct tftp_capture *cap = (struct tftp_capture *)ctx;
    if (strcmp(filename, "core.txt") != 0) return -1;
    cap->pos = 0;
    if (write) cap->len = 0;
    *handle = cap;
    return 0;
}

static ssize_t tftp_capture_read(void *ctx, void *handle, void *buf,
                                 size_t len) {
    (void)ctx;
    struct tftp_capture *cap = (struct tftp_capture *)handle;
    size_t remaining = cap->len - cap->pos;
    if (len > remaining) len = remaining;
    memcpy(buf, cap->file + cap->pos, len);
    cap->pos += len;
    return (ssize_t)len;
}

static ssize_t tftp_capture_write(void *ctx, void *handle, const void *buf,
                                  size_t len) {
    (void)ctx;
    struct tftp_capture *cap = (struct tftp_capture *)handle;
    if (cap->len + len > sizeof(cap->file)) return -1;
    memcpy(cap->file + cap->len, buf, len);
    cap->len += len;
    return (ssize_t)len;
}

static void tftp_capture_close(void *ctx, void *handle) {
    (void)ctx;
    (void)handle;
}

static int tftp_capture_send(void *ctx, const mm_net_tftp_peer_t *peer,
                             const void *buf, size_t len) {
    struct tftp_capture *cap = (struct tftp_capture *)ctx;
    if (peer->family != 4 || peer->port != 12345 ||
        cap->sent_count >= (int)(sizeof(cap->sent_len) / sizeof(cap->sent_len[0])) ||
        len > sizeof(cap->sent[0]))
        return 0;
    memcpy(cap->sent[cap->sent_count], buf, len);
    cap->sent_len[cap->sent_count] = len;
    cap->sent_count++;
    return 1;
}

static const mm_net_tftp_ops_t tftp_capture_ops = {
    .open = tftp_capture_open,
    .read = tftp_capture_read,
    .write = tftp_capture_write,
    .close = tftp_capture_close,
    .send = tftp_capture_send,
};

static void test_shared_tftp(void) {
    struct tftp_capture cap;
    memset(&cap, 0, sizeof(cap));
    mm_net_tftp_session_t session;
    mm_net_tftp_init(&session, &tftp_capture_ops, &cap);
    mm_net_tftp_peer_t peer;
    memset(&peer, 0, sizeof(peer));
    peer.family = 4;
    peer.port = 12345;
    peer.bytes[0] = 127;
    peer.bytes[3] = 1;

    const uint8_t wrq[] = {
        0, 2, 'c', 'o', 'r', 'e', '.', 't', 'x', 't', 0,
        'o', 'c', 't', 'e', 't', 0
    };
    mm_net_tftp_handle_packet(&session, &peer, wrq, sizeof(wrq));
    if (cap.sent_count != 1 || cap.sent_len[0] != 4 ||
        memcmp(cap.sent[0], "\x00\x04\x00\x00", 4) != 0)
        fail("shared tftp wrq ack");

    const uint8_t data[] = {
        0, 3, 0, 1, 'S', 'H', 'A', 'R', 'E', 'D', '_', 'T', 'F', 'T', 'P'
    };
    mm_net_tftp_handle_packet(&session, &peer, data, sizeof(data));
    if (cap.sent_count != 2 || cap.sent_len[1] != 4 ||
        memcmp(cap.sent[1], "\x00\x04\x00\x01", 4) != 0 ||
        cap.len != sizeof(data) - 4 ||
        memcmp(cap.file, "SHARED_TFTP", sizeof(data) - 4) != 0)
        fail("shared tftp data write");

    const uint8_t rrq[] = {
        0, 1, 'c', 'o', 'r', 'e', '.', 't', 'x', 't', 0,
        'o', 'c', 't', 'e', 't', 0
    };
    mm_net_tftp_handle_packet(&session, &peer, rrq, sizeof(rrq));
    if (cap.sent_count != 3 || cap.sent_len[2] != sizeof(data) ||
        memcmp(cap.sent[2], data, sizeof(data)) != 0)
        fail("shared tftp rrq data");

    const uint8_t ack[] = { 0, 4, 0, 1 };
    mm_net_tftp_handle_packet(&session, &peer, ack, sizeof(ack));
    if (session.active) fail("shared tftp final ack closes");

    memset(&cap, 0, sizeof(cap));
    mm_net_tftp_init(&session, &tftp_capture_ops, &cap);
    const uint8_t wrq_blksize[] = {
        0, 2, 'c', 'o', 'r', 'e', '.', 't', 'x', 't', 0,
        'o', 'c', 't', 'e', 't', 0,
        'b', 'l', 'k', 's', 'i', 'z', 'e', 0,
        '5', '0', '8', 0
    };
    mm_net_tftp_handle_packet(&session, &peer, wrq_blksize,
                              sizeof(wrq_blksize));
    const uint8_t expected_oack[] = {
        0, 6, 'b', 'l', 'k', 's', 'i', 'z', 'e', 0, '5', '0', '8', 0
    };
    if (cap.sent_count != 1 || cap.sent_len[0] != sizeof(expected_oack) ||
        memcmp(cap.sent[0], expected_oack, sizeof(expected_oack)) != 0)
        fail("shared tftp wrq blksize oack");

    uint8_t data1[4 + 508];
    data1[0] = 0; data1[1] = 3; data1[2] = 0; data1[3] = 1;
    memset(data1 + 4, 'A', 508);
    mm_net_tftp_handle_packet(&session, &peer, data1, sizeof(data1));
    if (cap.sent_count != 2 || memcmp(cap.sent[1], "\x00\x04\x00\x01", 4) != 0 ||
        !session.active || cap.len != 508)
        fail("shared tftp full negotiated block stays active");

    const uint8_t data2[] = { 0, 3, 0, 2, 'Z' };
    mm_net_tftp_handle_packet(&session, &peer, data2, sizeof(data2));
    if (cap.sent_count != 3 || memcmp(cap.sent[2], "\x00\x04\x00\x02", 4) != 0 ||
        cap.len != 509 || cap.file[508] != 'Z' || session.active)
        fail("shared tftp negotiated final block closes");
}

static void test_shared_ntp(void) {
    uint8_t packet[MM_NET_NTP_PACKET_LEN];
    mm_net_ntp_build_request(packet);
    if (packet[0] != 0x1b) fail("shared ntp request mode");
    for (size_t i = 1; i < MM_NET_NTP_PACKET_LEN; ++i) {
        if (packet[i] != 0) fail("shared ntp request zero fill");
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 0x24;
    packet[1] = 1;
    uint32_t unix_seconds = 1700000000UL;
    uint32_t ntp_seconds = unix_seconds + MM_NET_NTP_UNIX_DELTA;
    packet[40] = (uint8_t)(ntp_seconds >> 24);
    packet[41] = (uint8_t)(ntp_seconds >> 16);
    packet[42] = (uint8_t)(ntp_seconds >> 8);
    packet[43] = (uint8_t)ntp_seconds;
    uint32_t parsed = 0;
    if (!mm_net_ntp_parse_unix_seconds(packet, sizeof(packet), &parsed) ||
        parsed != unix_seconds)
        fail("shared ntp parse");

    packet[1] = 0;
    if (mm_net_ntp_parse_unix_seconds(packet, sizeof(packet), &parsed))
        fail("shared ntp rejects unsynchronised server");
}

static void *ntp_hal_peer(void *arg) {
    (void)arg;
    int fd = raw_udp_bound(NTP_HAL_PORT);
    uint8_t request[MM_NET_NTP_PACKET_LEN];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(fd, request, sizeof(request), 0,
                         (struct sockaddr *)&from, &from_len);
    if (n != MM_NET_NTP_PACKET_LEN || request[0] != 0x1b)
        fail("hal ntp peer request");

    uint8_t response[MM_NET_NTP_PACKET_LEN];
    memset(response, 0, sizeof(response));
    response[0] = 0x24;
    response[1] = 1;
    uint32_t unix_seconds = 1700000123UL;
    uint32_t ntp_seconds = unix_seconds + MM_NET_NTP_UNIX_DELTA;
    response[40] = (uint8_t)(ntp_seconds >> 24);
    response[41] = (uint8_t)(ntp_seconds >> 16);
    response[42] = (uint8_t)(ntp_seconds >> 8);
    response[43] = (uint8_t)ntp_seconds;
    if (sendto(fd, response, sizeof(response), 0,
               (struct sockaddr *)&from, from_len) != (ssize_t)sizeof(response))
        fail("hal ntp peer response");
    close(fd);
    return NULL;
}

static void test_shared_ntp_hal(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, ntp_hal_peer, NULL) != 0)
        fail("hal ntp peer thread");
    usleep(50000);
    uint32_t unix_seconds = 0;
    if (mm_net_ntp_query_unix_seconds("127.0.0.1", NTP_HAL_PORT, 1000,
                                      &unix_seconds) != HAL_NET_OK)
        fail("hal ntp query");
    if (unix_seconds != 1700000123UL)
        fail("hal ntp query timestamp");
    pthread_join(thread, NULL);
}

static void test_shared_mqtt_wire(void) {
    uint8_t encoded[4];
    if (mm_net_mqtt_encode_remaining_length(encoded, 321) != 2 ||
        encoded[0] != 0xC1 || encoded[1] != 0x02)
        fail("shared mqtt remaining length");

    uint8_t body[64];
    uint8_t *p = mm_net_mqtt_write_utf8(body, "topic");
    memcpy(p, "payload", 7);
    p += 7;
    char topic[16];
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (!mm_net_mqtt_decode_publish(0x30, body, (size_t)(p - body), topic,
                                    sizeof(topic), &payload, &payload_len) ||
        strcmp(topic, "topic") != 0 || payload_len != 7 ||
        memcmp(payload, "payload", 7) != 0)
        fail("shared mqtt publish decode");
}

static size_t mqtt_test_encode_remaining_length(uint8_t *out, size_t value) {
    size_t n = 0;
    do {
        uint8_t encoded = (uint8_t)(value % 128);
        value /= 128;
        if (value) encoded |= 128;
        out[n++] = encoded;
    } while (value && n < 4);
    return n;
}

static void mqtt_test_send_packet(int fd, uint8_t header, const void *body,
                                  size_t body_len) {
    uint8_t fixed[5];
    fixed[0] = header;
    size_t n = mqtt_test_encode_remaining_length(fixed + 1, body_len);
    if (send(fd, fixed, n + 1, 0) != (ssize_t)(n + 1))
        fail("mqtt test send fixed");
    if (body_len && send(fd, body, body_len, 0) != (ssize_t)body_len)
        fail("mqtt test send body");
}

static int mqtt_test_recv_exact(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return 0;
        got += (size_t)n;
    }
    return 1;
}

static int mqtt_test_read_packet(int fd, uint8_t *header, uint8_t *body,
                                 size_t cap, size_t *body_len) {
    if (!mqtt_test_recv_exact(fd, header, 1)) return 0;
    size_t multiplier = 1;
    size_t remaining = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t encoded = 0;
        if (!mqtt_test_recv_exact(fd, &encoded, 1)) return 0;
        remaining += (encoded & 127u) * multiplier;
        if (!(encoded & 128u)) break;
        multiplier *= 128u;
    }
    if (remaining > cap) fail("mqtt test packet too large");
    if (remaining && !mqtt_test_recv_exact(fd, body, remaining)) return 0;
    *body_len = remaining;
    return 1;
}

static const uint8_t *mqtt_test_utf8(const uint8_t *body, size_t body_len,
                                     size_t pos, char *out, size_t out_len) {
    if (pos + 2 > body_len) fail("mqtt test truncated string");
    size_t len = ((size_t)body[pos] << 8) | body[pos + 1];
    pos += 2;
    if (pos + len > body_len) fail("mqtt test string length");
    size_t copy = len < out_len - 1 ? len : out_len - 1;
    memcpy(out, body + pos, copy);
    out[copy] = 0;
    return body + pos + len;
}

static void *mqtt_hal_peer(void *arg) {
    (void)arg;
    int listen_fd = raw_tcp_listen(MQTT_HAL_PORT);
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) fail("mqtt peer accept");
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t header = 0;
    uint8_t body[512];
    size_t body_len = 0;
    char topic[128];

    if (!mqtt_test_read_packet(fd, &header, body, sizeof(body), &body_len) ||
        (header >> 4) != 1)
        fail("mqtt peer connect");
    mqtt_test_send_packet(fd, 0x20, "\0\0", 2);

    if (!mqtt_test_read_packet(fd, &header, body, sizeof(body), &body_len) ||
        (header >> 4) != 8)
        fail("mqtt peer subscribe");
    if (body_len < 5) fail("mqtt peer subscribe short");
    mqtt_test_utf8(body, body_len, 2, topic, sizeof(topic));
    if (strcmp(topic, "host/hal/in") != 0) fail("mqtt peer subscribe topic");
    uint8_t suback[3] = { body[0], body[1], 0 };
    mqtt_test_send_packet(fd, 0x90, suback, sizeof(suback));
    const char payload[] = "HAL_MQTT_IN";
    uint8_t publish[128];
    size_t topic_len = strlen(topic);
    publish[0] = (uint8_t)(topic_len >> 8);
    publish[1] = (uint8_t)topic_len;
    memcpy(publish + 2, topic, topic_len);
    memcpy(publish + 2 + topic_len, payload, sizeof(payload) - 1);
    mqtt_test_send_packet(fd, 0x30, publish, 2 + topic_len + sizeof(payload) - 1);

    if (!mqtt_test_read_packet(fd, &header, body, sizeof(body), &body_len) ||
        (header >> 4) != 3)
        fail("mqtt peer publish");
    const uint8_t *payload_ptr = mqtt_test_utf8(body, body_len, 0, topic,
                                                sizeof(topic));
    if (strcmp(topic, "host/hal/out") != 0 ||
        body + body_len < payload_ptr ||
        (size_t)(body + body_len - payload_ptr) != 12 ||
        memcmp(payload_ptr, "HAL_MQTT_OUT", 12) != 0)
        fail("mqtt peer publish payload");

    if (!mqtt_test_read_packet(fd, &header, body, sizeof(body), &body_len) ||
        (header >> 4) != 10)
        fail("mqtt peer unsubscribe");
    mqtt_test_utf8(body, body_len, 2, topic, sizeof(topic));
    if (strcmp(topic, "host/hal/in") != 0) fail("mqtt peer unsubscribe topic");
    uint8_t unsuback[2] = { body[0], body[1] };
    mqtt_test_send_packet(fd, 0xB0, unsuback, sizeof(unsuback));

    if (!mqtt_test_read_packet(fd, &header, body, sizeof(body), &body_len) ||
        (header >> 4) != 14)
        fail("mqtt peer disconnect");
    close(fd);
    close(listen_fd);
    return NULL;
}

static void test_mqtt(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, mqtt_hal_peer, NULL) != 0)
        fail("mqtt peer thread");
    usleep(50000);

    hal_net_mqtt_client_t mqtt = 0;
    if (hal_net_mqtt_connect("127.0.0.1", MQTT_HAL_PORT, "", "",
                             "HostHal", 1000, &mqtt) != HAL_NET_OK ||
        mqtt == 0)
        fail("hal mqtt connect");
    if (hal_net_mqtt_subscribe(mqtt, "host/hal/in", 0, 1000) != HAL_NET_OK)
        fail("hal mqtt subscribe");
    char topic[64];
    char payload[64];
    size_t payload_len = 0;
    int rc = HAL_NET_WOULD_BLOCK;
    for (int i = 0; i < 20 && rc == HAL_NET_WOULD_BLOCK; ++i) {
        rc = hal_net_mqtt_recv_event(mqtt, topic, sizeof(topic), payload,
                                     sizeof(payload), &payload_len);
        if (rc == HAL_NET_WOULD_BLOCK) usleep(50000);
    }
    if (rc != HAL_NET_OK || strcmp(topic, "host/hal/in") != 0 ||
        payload_len != 11 || memcmp(payload, "HAL_MQTT_IN", 11) != 0)
        fail("hal mqtt recv");
    if (hal_net_mqtt_publish(mqtt, "host/hal/out", "HAL_MQTT_OUT", 12, 0, 0) !=
        HAL_NET_OK)
        fail("hal mqtt publish");
    if (hal_net_mqtt_unsubscribe(mqtt, "host/hal/in", 1000) != HAL_NET_OK)
        fail("hal mqtt unsubscribe");
    if (hal_net_mqtt_close(mqtt) != HAL_NET_OK)
        fail("hal mqtt close");
    pthread_join(thread, NULL);
}

int main(void) {
    if (hal_net_init() != HAL_NET_OK) fail("hal init");
    uint32_t caps = hal_net_capabilities();
    uint32_t required = HAL_NET_CAP_TCP_SERVER | HAL_NET_CAP_TCP_CLIENT |
                        HAL_NET_CAP_TCP_STREAM | HAL_NET_CAP_UDP_SERVER |
                        HAL_NET_CAP_UDP_SEND | HAL_NET_CAP_MQTT_PLAIN;
    if ((caps & required) != required) fail("capabilities");
    if (caps & (HAL_NET_CAP_WIFI_SCAN | HAL_NET_CAP_WIFI_CONNECT))
        fail("host wifi capabilities");
    if (hal_net_wifi_set_credentials("ssid", "pass", "host",
                                     NULL, NULL, NULL) != HAL_NET_UNSUPPORTED)
        fail("host wifi credentials unsupported");
    if (hal_net_wifi_connect(10) != HAL_NET_UNSUPPORTED)
        fail("host wifi connect unsupported");
    if (hal_net_wifi_status() != HAL_NET_UNSUPPORTED)
        fail("host wifi status unsupported");
    char scan_buf[8] = { 'x' };
    size_t scan_written = 1;
    if (hal_net_wifi_scan(scan_buf, sizeof(scan_buf), &scan_written, 0) !=
            HAL_NET_UNSUPPORTED ||
        scan_buf[0] != 0 || scan_written != 0)
        fail("host wifi scan unsupported");
    if (caps & (HAL_NET_CAP_MQTT_TLS | HAL_NET_CAP_MQTT_WEBSOCKET))
        fail("host unsupported mqtt capabilities");
    char ip[32];
    if (hal_net_ip_address(ip, sizeof(ip)) != HAL_NET_OK || strcmp(ip, "127.0.0.1") != 0)
        fail("ip address");
    test_tcp_client();
    test_tcp_server();
    test_tcp_server_conn_recv();
    test_udp();
    test_shared_http();
    test_shared_http_response_sender();
    test_shared_http_file_sender();
    test_shared_tftp();
    test_shared_ntp();
    test_shared_ntp_hal();
    test_shared_mqtt_wire();
    test_mqtt();
    printf("host net HAL conformance OK\n");
    return 0;
}
