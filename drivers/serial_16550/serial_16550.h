/*
 * drivers/serial_16550/serial_16550.h — 16550-compatible UART driver.
 *
 * COM1 (port 0x3F8) only, for now. The pc386 port uses serial as the
 * test-output channel: QEMU's `-serial stdio` flag pipes our output to
 * the host terminal, which is what run_headless.sh / run_tests.sh
 * capture for golden-output comparison.
 *
 * Configuration: 38400 baud, 8N1, FIFO enabled. Standard PC default,
 * works under QEMU, DOSBox-X, 86Box, and on real hardware.
 *
 * Output is blocking: we spin-wait on the THR-empty bit. Acceptable in
 * a freestanding kernel; revisit if Stage 2+ wants interrupt-driven I/O.
 *
 * Input (serial_getc) is deferred to Stage 3 when we wire up keyboard
 * + interrupts more generally.
 */

#ifndef DRIVERS_SERIAL_16550_H
#define DRIVERS_SERIAL_16550_H

#include <stdbool.h>

bool serial_init(void);   /* false if loopback self-test fails */
void serial_putc(char c);
void serial_puts(const char *s);

#endif
