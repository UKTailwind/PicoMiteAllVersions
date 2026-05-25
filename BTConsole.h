/*
 * BTConsole.h — Bluetooth SPP (RFCOMM) console for PicoMiteBT.
 *
 * SPP is advertised under "Just Works" pairing (no PIN, no MITM). On
 * connect, the remote terminal sees a virtual COM port; characters in
 * either direction route through the same MMBasic console state the
 * USB CDC build uses.
 *
 * No persistence — link keys live in RAM only, so peers re-pair on
 * every cold boot. That's fine for the first cut; revisit once basics
 * work.
 */

#ifndef BTCONSOLE_H
#define BTCONSOLE_H

#ifdef PICOMITEBT

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up cyw43 + btstack, registers SPP service, starts advertising.
   Must be called once after cyw43_arch_init() has succeeded. */
void bt_console_init(void);

/* Cooperative poll — call from the main loop alongside the existing
   timer/keyboard polls. Pumps cyw43 + btstack run loop. */
void bt_console_poll(void);

/* True once an RFCOMM channel is open. */
bool bt_console_connected(void);

/* RX: drain accumulated bytes from the BT-side ring buffer. Returns
   the byte (0..255) or -1 if empty. Non-blocking. */
int bt_console_getc(void);

/* RX: bytes available to read. */
int bt_console_rx_available(void);

/* TX: enqueue a byte into the BT-side ring. Mirrors the UART writer —
   busy-waits on a full ring while connected; drops oldest when not
   connected so the BASIC interpreter never deadlocks pre-pairing. */
void bt_console_putc(uint8_t c);

/* TX: kick the run loop to request a CAN_SEND_NOW event. Called from
   the existing flush sites in place of tud_cdc_write_flush(). */
void bt_console_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* PICOMITEBT */
#endif /* BTCONSOLE_H */
