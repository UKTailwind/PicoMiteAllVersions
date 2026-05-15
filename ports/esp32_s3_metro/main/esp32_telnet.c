#include <stdint.h>
#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "esp32_telnet.h"
#include "shared/net/mm_net_telnet_rx.h"

static char esp32_telnet_buf[256];
static int esp32_telnet_pos;
static hal_net_tcp_server_t esp32_telnet_server;
static hal_net_tcp_conn_t esp32_telnet_conn;
/* RFC 854 IAC parser + CR-NUL dedup live in shared/net/mm_net_telnet_rx.c. */
static const uint8_t esp32_telnet_init_options[] = {
    255, 251, 3, 255, 253, 3, 255, 251, 1, 255, 253, 34, 255, 254, 34, 0,
};

static void esp32_telnet_close_conn(void) {
    if (esp32_telnet_conn) {
        hal_net_tcp_conn_close(esp32_telnet_conn);
        esp32_telnet_conn = 0;
    }
    esp32_telnet_pos = 0;
    mm_net_telnet_rx_reset();
}

void esp32_telnet_close(void) {
    esp32_telnet_close_conn();
    if (esp32_telnet_server) {
        hal_net_tcp_server_close(esp32_telnet_server);
        esp32_telnet_server = 0;
    }
}

void esp32_telnet_close_session(void) {
    esp32_telnet_close_conn();
}

int esp32_telnet_open(void) {
    if (!Option.Telnet || !WIFIconnected) return 1;
    if (esp32_telnet_server) return 1;
    return hal_net_tcp_server_open(23, 1, &esp32_telnet_server) == HAL_NET_OK;
}

void esp32_telnet_putc(int c, int flush) {
    if (!esp32_telnet_conn || !WIFIconnected) return;
    if (flush != -1) {
        esp32_telnet_buf[esp32_telnet_pos++] = (char)c;
        if (c == 255) esp32_telnet_buf[esp32_telnet_pos++] = (char)c;
        if (c == 13) esp32_telnet_buf[esp32_telnet_pos++] = 0;
    }
    if (esp32_telnet_pos >= (int)sizeof(esp32_telnet_buf) - 4 ||
        (flush == -1 && esp32_telnet_pos)) {
        if (hal_net_tcp_conn_send(esp32_telnet_conn, esp32_telnet_buf,
                                  (size_t)esp32_telnet_pos, 5000) !=
            HAL_NET_OK) {
            esp32_telnet_close_conn();
        }
        esp32_telnet_pos = 0;
    }
}

/* RFC 854 IAC parser + ConsoleRxBuf delivery live in
 * shared/net/mm_net_telnet_rx.c — shared across every port. */
static void esp32_telnet_receive_bytes(const uint8_t *data, size_t len) {
    mm_net_telnet_rx_feed(data, len);
}

void esp32_telnet_poll(int mode) {
    static uint64_t flushtimer;
    if (!Option.Telnet || !WIFIconnected) {
        esp32_telnet_close();
        return;
    }
    if (!esp32_telnet_server && !esp32_telnet_open()) return;
    if (!esp32_telnet_conn) {
        hal_net_tcp_conn_t conn = 0;
        int rc = hal_net_tcp_accept_conn(esp32_telnet_server, &conn);
        if (rc == HAL_NET_OK) {
            esp32_telnet_conn = conn;
            if (hal_net_tcp_conn_send(esp32_telnet_conn,
                                      esp32_telnet_init_options,
                                      sizeof(esp32_telnet_init_options),
                                      5000) != HAL_NET_OK) {
                esp32_telnet_close_conn();
                return;
            }
        } else if (rc != HAL_NET_WOULD_BLOCK) {
            esp32_telnet_close();
            return;
        }
    }
    if (!esp32_telnet_conn) return;
    uint8_t buf[128];
    for (;;) {
        size_t len = 0;
        int rc = hal_net_tcp_conn_recv(esp32_telnet_conn, buf, sizeof(buf),
                                       &len);
        if (rc == HAL_NET_OK) {
            esp32_telnet_receive_bytes(buf, len);
            continue;
        }
        if (rc != HAL_NET_WOULD_BLOCK) esp32_telnet_close_conn();
        break;
    }
    /* Time-gated drain: fires from any ProcessWeb call (including the
     * mode=0 polls in MMInkey/MMgetchar). This is the catch-all that
     * delivers tail bytes within 5 ms when SerialConsolePutC isn't
     * called and the buffer hasn't hit its auto-flush threshold. */
    (void)mode;
    if (esp32_telnet_conn && hal_time_us_64() > flushtimer) {
        esp32_telnet_putc(0, -1);
        flushtimer = hal_time_us_64() + 5000;
    }
}
