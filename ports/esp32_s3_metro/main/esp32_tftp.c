#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "hal/hal_filesystem.h"
#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "shared/net/mm_net_tftp.h"
#include "esp32_tftp.h"

#define ESP32_TFTP_PORT 69

typedef struct {
    hal_net_udp_socket_t socket;
    int port;
    mm_net_tftp_session_t session;
} esp32_tftp_server_t;

static esp32_tftp_server_t s_tftp = {
    .socket = 0,
    .port = 0,
};

static int esp32_tftp_peer_text(const mm_net_tftp_peer_t * peer, char * out,
                                size_t out_len) {
    if (!peer || !out || out_len == 0) return 0;
    if (peer->family == 4)
        return inet_ntop(AF_INET, peer->bytes, out, out_len) != NULL;
    if (peer->family == 6)
        return inet_ntop(AF_INET6, peer->bytes, out, out_len) != NULL;
    return 0;
}

static int esp32_tftp_send(void * ctx, const mm_net_tftp_peer_t * peer,
                           const void * buf, size_t len) {
    (void)ctx;
    if (!s_tftp.socket) return 0;
    char host[INET6_ADDRSTRLEN];
    if (!esp32_tftp_peer_text(peer, host, sizeof host)) return 0;
    return hal_net_udp_socket_send(s_tftp.socket, host, peer->port, buf, len,
                                   1000) == HAL_NET_OK;
}

static int esp32_tftp_open_file(void * ctx, const char * filename, int write,
                                void ** handle) {
    (void)ctx;
    hal_fs_fd_t fd;
    int flags = write ? (HAL_FS_O_WRONLY | HAL_FS_O_CREAT | HAL_FS_O_TRUNC)
                      : HAL_FS_O_RDONLY;
    if (hal_fs_open(filename, flags, &fd) < 0) return -1;
    *handle = (void *)(intptr_t)fd;
    return 0;
}

static ssize_t esp32_tftp_read_file(void * ctx, void * handle, void * buf,
                                    size_t len) {
    (void)ctx;
    return hal_fs_read((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static ssize_t esp32_tftp_write_file(void * ctx, void * handle, const void * buf,
                                     size_t len) {
    (void)ctx;
    return hal_fs_write((hal_fs_fd_t)(intptr_t)handle, buf, len);
}

static void esp32_tftp_close_file(void * ctx, void * handle) {
    (void)ctx;
    hal_fs_close((hal_fs_fd_t)(intptr_t)handle);
}

static const mm_net_tftp_ops_t esp32_tftp_ops = {
    .open = esp32_tftp_open_file,
    .read = esp32_tftp_read_file,
    .write = esp32_tftp_write_file,
    .close = esp32_tftp_close_file,
    .send = esp32_tftp_send,
};

static void esp32_tftp_ensure_init(void) {
    if (!s_tftp.session.ops)
        mm_net_tftp_init(&s_tftp.session, &esp32_tftp_ops, NULL);
}

void esp32_tftp_poll(void) {
    if (!s_tftp.socket) return;
    esp32_tftp_ensure_init();
    for (;;) {
        uint8_t packet[600];
        hal_net_addr_t from;
        size_t len = 0;
        int rc = hal_net_udp_recv_event(s_tftp.socket, &from, packet,
                                        sizeof packet, &len);
        if (rc == HAL_NET_WOULD_BLOCK) return;
        if (rc != HAL_NET_OK || len < 2) return;
        mm_net_tftp_peer_t peer;
        memset(&peer, 0, sizeof peer);
        peer.family = from.family;
        peer.port = from.port;
        memcpy(peer.bytes, from.bytes, sizeof peer.bytes);
        mm_net_tftp_handle_packet(&s_tftp.session, &peer, packet, len);
    }
}

void esp32_tftp_server_stop(void) {
    esp32_tftp_ensure_init();
    mm_net_tftp_close(&s_tftp.session);
    if (s_tftp.socket) {
        hal_net_udp_close(s_tftp.socket);
        s_tftp.socket = 0;
    }
    s_tftp.port = 0;
}

int esp32_tftp_server_open(void) {
    if (Option.disabletftp || !WIFIconnected) {
        esp32_tftp_server_stop();
        return 0;
    }
    if (s_tftp.socket && s_tftp.port == ESP32_TFTP_PORT) return 1;

    esp32_tftp_server_stop();

    if (hal_net_udp_bind(ESP32_TFTP_PORT, &s_tftp.socket) != HAL_NET_OK) {
        s_tftp.socket = 0;
        return 0;
    }

    s_tftp.port = ESP32_TFTP_PORT;
    esp32_tftp_ensure_init();
    return 1;
}

void esp32_tftp_close_session(void) {
    esp32_tftp_ensure_init();
    mm_net_tftp_close(&s_tftp.session);
}
