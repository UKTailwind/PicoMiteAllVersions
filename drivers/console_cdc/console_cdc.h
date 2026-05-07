/*
 * drivers/console_cdc/console_cdc.h — RP2350 native USB CDC stdio
 * helpers, shared between every port that runs the USB peripheral in
 * device mode (i.e. every port except USB-host-keyboard).
 *
 * The HAL keyboard contract has three CDC-flavored hooks
 * (hal_console_usb_cdc_boot_init / hal_console_usb_cdc_putc /
 * hal_keyboard_routinechecks_pump). Each backend implements them; the
 * actual CDC plumbing lives here so PS/2 + CDC-only builds share one
 * source of truth.
 *
 * Backends call:
 *   console_cdc_boot_setup()   — once, at boot, before display init
 *   console_cdc_putc()         — from MMputchar via the HAL shim
 *   console_cdc_drain_to_rxbuf() — from routinechecks_pump every tick
 */

#ifndef DRIVERS_CONSOLE_CDC_H
#define DRIVERS_CONSOLE_CDC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Disable CRLF translation on the CDC pipe and wait up to 5 s for the
 * host to grab the port (so banner output isn't dropped). Skipped if
 * the user has explicitly chosen UART-1/2 with no Telnet client. */
void console_cdc_boot_setup(void);

/* Write one byte to CDC if connected and the active console is CDC
 * (Option.SerialConsole == 0 || > 4). No-op otherwise. */
void console_cdc_putc(char c, int flush);

/* Drain available CDC bytes into ConsoleRxBuf, honouring BreakKey and
 * keyselect. No-op if not connected or the active console is a UART. */
void console_cdc_drain_to_rxbuf(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_CONSOLE_CDC_H */
