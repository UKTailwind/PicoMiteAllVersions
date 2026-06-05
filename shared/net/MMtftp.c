#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "pico/time.h"
#include "shared/net/mm_net_tftp.h"

#define PICO_TFTP_PORT 69

static hal_net_udp_socket_t pico_tftp_socket;
static mm_net_tftp_session_t pico_tftp_session;
static int pico_tftp_fnbr;

static int pico_tftp_peer_text(const mm_net_tftp_peer_t * peer, char * out,
                               size_t out_len) {
    if (!peer || !out || out_len == 0) return 0;
    if (peer->family != 4) return 0;
    snprintf(out, out_len, "%u.%u.%u.%u", peer->bytes[0], peer->bytes[1],
             peer->bytes[2], peer->bytes[3]);
    return 1;
}

static int pico_tftp_send(void * ctx, const mm_net_tftp_peer_t * peer,
                          const void * buf, size_t len) {
    (void)ctx;
    if (!pico_tftp_socket) return 0;
    char host[32];
    if (!pico_tftp_peer_text(peer, host, sizeof host)) return 0;
    return hal_net_udp_socket_send(pico_tftp_socket, host, peer->port, buf,
                                   len, 1000) == HAL_NET_OK;
}

static int pico_tftp_open_file(void * ctx, const char * filename, int write,
                               void ** handle) {
    (void)ctx;
    if (!InitSDCard()) return -1;

    pico_tftp_fnbr = FindFreeFileNbr();
    BYTE mode;
    if (write) {
        mode = FA_WRITE | FA_CREATE_ALWAYS;
        if (!optionsuppressstatus) MMPrintString("TFTP request to create ");
    } else {
        mode = FA_READ;
        if (!optionsuppressstatus) MMPrintString("TFTP request to read ");
    }
    if (!optionsuppressstatus) {
        MMPrintString("binary file : ");
        MMPrintString((char *)filename);
        PRet();
    }

    if (!BasicFileOpen((char *)filename, pico_tftp_fnbr, mode)) return -1;
    *handle = &pico_tftp_fnbr;
    return 0;
}

static ssize_t pico_tftp_read_file(void * ctx, void * handle, void * buf,
                                   size_t len) {
    (void)ctx;
    int fnbr = *(int *)handle;
    ssize_t n = hal_fs_read(hal_fds[fnbr], buf, len);
    return n < 0 ? -1 : n;
}

static ssize_t pico_tftp_write_file(void * ctx, void * handle, const void * buf,
                                    size_t len) {
    (void)ctx;
    int fnbr = *(int *)handle;
    ssize_t w = hal_fs_write(hal_fds[fnbr], buf, len);
    FSerror = (w < 0) ? (int)w : 0;
    ErrorCheck(fnbr);
    return w < 0 ? -1 : w;
}

static void pico_tftp_close_file(void * ctx, void * handle) {
    (void)ctx;
    int fnbr = *(int *)handle;
    FileClose(fnbr);
    if (!optionsuppressstatus) MMPrintString("TFTP transfer complete\r\n");
}

static const mm_net_tftp_ops_t pico_tftp_ops = {
    .open = pico_tftp_open_file,
    .read = pico_tftp_read_file,
    .write = pico_tftp_write_file,
    .close = pico_tftp_close_file,
    .send = pico_tftp_send,
};

static void pico_tftp_ensure_init(void) {
    if (!pico_tftp_session.ops)
        mm_net_tftp_init(&pico_tftp_session, &pico_tftp_ops, NULL);
}

void pico_tftp_close(void) {
    pico_tftp_ensure_init();
    mm_net_tftp_close(&pico_tftp_session);
    if (pico_tftp_socket) {
        hal_net_udp_close(pico_tftp_socket);
        pico_tftp_socket = 0;
    }
}

void pico_tftp_poll(void) {
    if (!pico_tftp_socket) return;
    pico_tftp_ensure_init();
    int idle_polls = 0;
    for (;;) {
        uint8_t packet[600];
        hal_net_addr_t from;
        size_t len = 0;
        hal_net_poll();
        int rc = hal_net_udp_recv_event(pico_tftp_socket, &from, packet,
                                        sizeof packet, &len);
        if (rc == HAL_NET_WOULD_BLOCK) {
            if (pico_tftp_session.active && idle_polls++ < 20) {
                sleep_us(1000);
                continue;
            }
            return;
        }
        idle_polls = 0;
        if (rc != HAL_NET_OK || len < 2) return;
        mm_net_tftp_peer_t peer;
        memset(&peer, 0, sizeof peer);
        peer.family = from.family;
        peer.port = from.port;
        memcpy(peer.bytes, from.bytes, sizeof peer.bytes);
        mm_net_tftp_handle_packet(&pico_tftp_session, &peer, packet, len);
    }
}

int cmd_tftp_server_init(void) {
    if (Option.disabletftp || !WIFIconnected) {
        pico_tftp_close();
        return 0;
    }
    if (pico_tftp_socket) return 1;
    if (hal_net_udp_bind(PICO_TFTP_PORT, &pico_tftp_socket) != HAL_NET_OK) {
        pico_tftp_socket = 0;
        return 0;
    }
    pico_tftp_ensure_init();
    return 1;
}
