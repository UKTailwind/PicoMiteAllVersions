/* ports/pico_sdk_compat/hardware/uart.h
 *
 * Minimal Pico SDK uart.h shim for non-Pico ports. Lets files like
 * core/mmbasic/XModem.c (which #includes <hardware/uart.h>) compile and
 * link on hosts that don't have the real Pico SDK. The functions and
 * uart0/uart1 globals are only called when Option.SerialConsole != 0,
 * which is never set on pc386 / host_native — so the stubs below are
 * compile-time noise that never executes.
 */

#ifndef PICO_SDK_COMPAT_HARDWARE_UART_H
#define PICO_SDK_COMPAT_HARDWARE_UART_H

#include <stdbool.h>
#include <stdint.h>

typedef struct uart_inst {
    int _unused;
} uart_inst_t;

extern uart_inst_t * uart0;
extern uart_inst_t * uart1;

void uart_set_irq_enables(uart_inst_t * uart, bool rx_has_data, bool tx_needs_data);
void uart_putc_raw(uart_inst_t * uart, char c);
bool uart_is_readable(uart_inst_t * uart);
char uart_getc(uart_inst_t * uart);

#endif
