/*
 * shared/net/mm_net_telnet_rx.h — RFC 854 inbound telnet stream parser
 * shared across Pico, ESP32, and host_native (audit Finding —
 * "telnet IAC parsers").
 *
 * Three nearly-equivalent state machines previously lived in
 *   MMtelnet.c              (Pico)              — buggy: drops the whole
 *                                                 TCP segment if it starts
 *                                                 with IAC
 *   ports/esp32_s3_metro/main/esp32_telnet.c     — correct 5-state machine
 *   ports/host_native/host_web.c                 — correct 5-state machine
 *
 * The shared parser implements the canonical 5-state machine
 * (DATA / IAC / OPT / SB / SB_IAC), strips RFC 854 protocol bytes,
 * dedups CR NUL pairs, and pushes the resulting data stream into
 * ConsoleRxBuf with the same BreakKey / keyselect / overflow
 * semantics every port already used. State is module-private and
 * persists across feed() calls; call mm_net_telnet_rx_reset() when a
 * telnet connection closes so the next client starts in DATA state.
 */
#ifndef MM_NET_TELNET_RX_H
#define MM_NET_TELNET_RX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mm_net_telnet_rx_reset(void);
void mm_net_telnet_rx_feed(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_TELNET_RX_H */
