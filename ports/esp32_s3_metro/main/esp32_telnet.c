#include <stdint.h>
#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "esp32_telnet.h"

static char esp32_telnet_buf[256];
static int esp32_telnet_pos;
static hal_net_tcp_server_t esp32_telnet_server;
static hal_net_tcp_conn_t esp32_telnet_conn;
static int esp32_telnet_lastchar = -1;
/* IAC parser state for esp32_telnet_receive_bytes. RFC 854. */
#define TELNET_RX_DATA      0
#define TELNET_RX_IAC       1   /* saw 0xFF */
#define TELNET_RX_OPT       2   /* saw WILL/WONT/DO/DONT, expecting option */
#define TELNET_RX_SB        3   /* inside subnegotiation block */
#define TELNET_RX_SB_IAC    4   /* saw 0xFF inside subnegotiation */
static int esp32_telnet_rx_state = TELNET_RX_DATA;
static const uint8_t esp32_telnet_init_options[] = {
    255, 251, 3, 255, 253, 3, 255, 251, 1, 255, 253, 34, 255, 254, 34, 0,
};

static void esp32_telnet_close_conn(void) {
    if (esp32_telnet_conn) {
        hal_net_tcp_conn_close(esp32_telnet_conn);
        esp32_telnet_conn = 0;
    }
    esp32_telnet_pos = 0;
    esp32_telnet_rx_state = TELNET_RX_DATA;
    esp32_telnet_lastchar = -1;
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

static void esp32_telnet_deliver_byte(uint8_t b) {
    /* Collapse the telnet NVT CR-NUL pair into bare CR (the client sends
     * CR\0 to mean "carriage return without line feed"; MMBasic only
     * wants the CR). Also collapse the doubled 0xFF that telnet uses to
     * escape 0xFF in the data stream. */
    if ((esp32_telnet_lastchar == 13 && b == 0) ||
        (esp32_telnet_lastchar == 255 && b == 255)) {
        esp32_telnet_lastchar = -1;
        return;
    }
    ConsoleRxBuf[ConsoleRxBufHead] = (char)b;
    if (BreakKey && (char)b == BreakKey) {
        MMAbort = 1;
        ConsoleRxBufHead = ConsoleRxBufTail;
        esp32_telnet_lastchar = -1;
        return;
    }
    if ((char)b == keyselect && KeyInterrupt != NULL) {
        Keycomplete = 1;
        esp32_telnet_lastchar = -1;
        return;
    }
    esp32_telnet_lastchar = (int)b;
    ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
    if (ConsoleRxBufHead == ConsoleRxBufTail)
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
}

static void esp32_telnet_receive_bytes(const uint8_t *data, size_t len) {
    if (!data || len == 0) return;
    /* RFC 854 inbound parser. The old code did `if (data[0] == 255) return;`
     * which silently dropped the entire batch whenever it began with an
     * IAC sequence -- meaning client negotiation replies AND any typed
     * characters arriving in the same TCP segment were both lost. */
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        switch (esp32_telnet_rx_state) {
        case TELNET_RX_DATA:
            if (b == 255) {
                esp32_telnet_rx_state = TELNET_RX_IAC;
            } else {
                esp32_telnet_deliver_byte(b);
            }
            break;
        case TELNET_RX_IAC:
            if (b == 255) {
                /* IAC IAC -> literal 0xFF data byte. */
                esp32_telnet_deliver_byte(0xFF);
                esp32_telnet_rx_state = TELNET_RX_DATA;
            } else if (b == 250) {
                /* SB: subnegotiation. Consume until IAC SE. */
                esp32_telnet_rx_state = TELNET_RX_SB;
            } else if (b == 251 || b == 252 || b == 253 || b == 254) {
                /* WILL / WONT / DO / DONT -> one more option byte. */
                esp32_telnet_rx_state = TELNET_RX_OPT;
            } else {
                /* Standalone command (NOP, GA, etc.): no option byte. */
                esp32_telnet_rx_state = TELNET_RX_DATA;
            }
            break;
        case TELNET_RX_OPT:
            /* Option byte for WILL/WONT/DO/DONT. We don't dynamically
             * renegotiate -- the server announced its desired set during
             * the initial poll, and any client reply just confirms /
             * rejects. Drop the option byte and resume data parsing. */
            esp32_telnet_rx_state = TELNET_RX_DATA;
            break;
        case TELNET_RX_SB:
            if (b == 255) esp32_telnet_rx_state = TELNET_RX_SB_IAC;
            break;
        case TELNET_RX_SB_IAC:
            if (b == 240) {
                /* SE -- end of subnegotiation. */
                esp32_telnet_rx_state = TELNET_RX_DATA;
            } else {
                /* Either IAC IAC (escaped 0xFF inside SB) or some other
                 * embedded command. Either way, swallow it and stay in
                 * SB. */
                esp32_telnet_rx_state = TELNET_RX_SB;
            }
            break;
        default:
            esp32_telnet_rx_state = TELNET_RX_DATA;
            break;
        }
    }
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
