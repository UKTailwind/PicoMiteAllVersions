/*
 * runtime/runtime_console_escdecode.h — shared ANSI/VT100 escape-sequence
 * decoder API. See runtime/runtime_console_escdecode.c for the rationale.
 *
 * Usage from a port's MMInkey:
 *
 *   1. Before consulting your input source, call
 *      mmbasic_escdecode_pop_pushback() to drain any chars left over
 *      from an earlier unrecognised escape sequence.
 *
 *   2. When you read a 0x1B byte from your input, call
 *      mmbasic_escdecode_run(my_port_read_byte_ms) and return what
 *      it gives you.
 *
 *   3. my_port_read_byte_ms(timeout_ms) must read one byte from the
 *      same input source as your main MMInkey path, waiting up to
 *      timeout_ms before returning -1. Per-call relative timeout.
 */
#ifndef RUNTIME_CONSOLE_ESCDECODE_H
#define RUNTIME_CONSOLE_ESCDECODE_H

#ifdef __cplusplus
extern "C" {
#endif

int mmbasic_escdecode_pop_pushback(void);
int mmbasic_escdecode_run(int (*read_byte_ms)(int timeout_ms));

/* Normalise a raw byte read from a serial console into the MMBasic
 * keymap. Maps 0x7F (DEL — Backspace on macOS Terminal / iTerm2 / most
 * terminals) to BKSP, and '\n' (LF) to ENTER. All other bytes pass
 * through unchanged. Use on bytes that were NOT pre-decoded (e.g. raw
 * USB-CDC / UART input); pre-decoded keys from a keyboard layer
 * (ESP32 web-console, pc386 PS/2, Pico USB-HID) already use MMBasic
 * codes and should not be re-normalised. */
int mmbasic_console_normalise_byte(int c);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CONSOLE_ESCDECODE_H */
