/*
 * shared/net/mm_net_telnet_rx.c — RFC 854 inbound telnet parser used by
 * every port's telnet daemon. See header for rationale.
 *
 * State machine (5 states):
 *   DATA        — normal payload; 0xFF transitions to IAC
 *   IAC         — saw 0xFF. Next byte chooses the path:
 *                   0xFF                   → emit literal 0xFF, back to DATA
 *                   251..254 (WILL..DONT)  → expect option byte, go to OPT
 *                   250 (SB)               → enter subnegotiation, go to SB
 *                   anything else          → standalone command, back to DATA
 *   OPT         — eat the option byte, back to DATA
 *   SB          — consume payload until 0xFF (escape), go to SB_IAC
 *   SB_IAC      — 240 (SE) ends subnegotiation; otherwise stay in SB
 *
 * Pair-dedup (after IAC stripping):
 *   CR  NUL     — RFC 854 NVT: bare CR transmitted as CR NUL; drop the NUL
 *
 * Sink: ConsoleRxBuf with the usual BreakKey / keyselect / overflow
 * handling. Identical destination and byte-for-byte semantics to the
 * three per-port versions that preceded this file.
 *
 * Type discipline: MMBasic_Includes.h + Hardware_Includes.h provide the
 * canonical types for every global we touch (MMAbort = volatile int,
 * Keycomplete = volatile bool, etc). Earlier attempts at this file
 * used hand-rolled extern decls to dodge MM_Misc.h:104's `time_t` —
 * that backfired silently because C linkage doesn't compare types
 * across TUs and the size mismatch on MMAbort (bool vs int) corrupted
 * adjacent state. With MM_Misc.h now self-contained (it includes
 * <time.h> itself), pulling in the canonical headers is safe.
 */

#include "shared/net/mm_net_telnet_rx.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* RFC 854 commands referenced here. */
#define IAC_BYTE 0xFFu
#define TELNET_SE 240
#define TELNET_SB 250
#define TELNET_WILL 251
#define TELNET_WONT 252
#define TELNET_DO 253
#define TELNET_DONT 254

enum {
    RX_DATA = 0,
    RX_IAC,
    RX_OPT,
    RX_SB,
    RX_SB_IAC,
};

static int g_rx_state = RX_DATA;
static int g_lastchar = -1;

void mm_net_telnet_rx_reset(void) {
    g_rx_state = RX_DATA;
    g_lastchar = -1;
}

static void rx_push(uint8_t c) {
    /* CR NUL → drop NUL. IAC IAC is collapsed in the state machine
     * before reaching here (one push per literal 0xFF data byte). */
    if (g_lastchar == 13 && c == 0) {
        g_lastchar = -1;
        return;
    }

    ConsoleRxBuf[ConsoleRxBufHead] = (char)c;
    if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {
        MMAbort = true;
        ConsoleRxBufHead = ConsoleRxBufTail;
        g_lastchar = -1;
        return;
    }
    if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL) {
        Keycomplete = 1;
        g_lastchar = -1;
        return;
    }
    g_lastchar = (int)c;
    ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
    if (ConsoleRxBufHead == ConsoleRxBufTail) {
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
    }
}

void mm_net_telnet_rx_feed(const uint8_t * data, size_t len) {
    if (!data || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        switch (g_rx_state) {
        case RX_DATA:
            if (b == IAC_BYTE) {
                g_rx_state = RX_IAC;
            } else {
                rx_push(b);
            }
            break;
        case RX_IAC:
            if (b == IAC_BYTE) {
                /* IAC IAC → literal 0xFF data byte. */
                rx_push(IAC_BYTE);
                g_rx_state = RX_DATA;
            } else if (b == TELNET_SB) {
                g_rx_state = RX_SB;
            } else if (b == TELNET_WILL || b == TELNET_WONT ||
                       b == TELNET_DO || b == TELNET_DONT) {
                g_rx_state = RX_OPT;
            } else {
                /* NOP / GA / standalone command — no option byte. */
                g_rx_state = RX_DATA;
            }
            break;
        case RX_OPT:
            /* Skip the option byte; we don't dynamically renegotiate. */
            g_rx_state = RX_DATA;
            break;
        case RX_SB:
            /* Consume subnegotiation payload until IAC SE. */
            if (b == IAC_BYTE) g_rx_state = RX_SB_IAC;
            break;
        case RX_SB_IAC:
            if (b == TELNET_SE) {
                g_rx_state = RX_DATA;
            } else {
                /* IAC IAC inside SB, or another embedded command —
                 * either way swallow and stay in SB. */
                g_rx_state = RX_SB;
            }
            break;
        default:
            g_rx_state = RX_DATA;
            break;
        }
    }
}
