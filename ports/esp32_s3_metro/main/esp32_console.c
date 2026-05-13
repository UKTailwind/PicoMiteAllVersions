/*
 * esp32_console.c — USB Serial/JTAG console driver for the BASIC REPL.
 *
 * The chip's USB JTAG/Serial debug unit is the same physical pipe used
 * for `idf.py monitor` and `picocom /dev/cu.usbmodem*`. We install the
 * interrupt-driven driver and switch the IDF stdio VFS to use it so
 * that read(STDIN_FILENO, ...) actually drains the hardware FIFO, then
 * provide the byte-level read/write hooks the rest of this port uses.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void esp32_console_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {
            .tx_buffer_size = 256,
            .rx_buffer_size = 256,
        };
        usb_serial_jtag_driver_install(&cfg);
    }
    usb_serial_jtag_vfs_use_driver();

    /* Pass raw bytes both directions — MMBasic owns line endings. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

/* ---- output hook ----
 * MMBasic's MMputchar / MMPrintString / SerialConsolePutC route every
 * byte through this helper. We forward straight to fwrite(stdout), which
 * goes to USB Serial/JTAG via the driver installed above. */

void esp32_console_write_bytes(const char *text, int len) {
    fwrite(text, 1, len, stdout);
}

/* USB Serial/JTAG is a byte-level raw pipe — no terminal line discipline,
 * no readline. Tell MMInkey to route through the byte-level reads + the
 * ANSI escape decoder rather than the line-buffered fgetc fallback. */
int esp32_console_raw_mode_is_active(void) { return 1; }

/* ---- byte-level reads ----
 * esp32_console_read_byte_nonblock returns -1 when nothing's available so the
 * editor's poll loop spins without blocking. esp32_console_read_byte_blocking_ms
 * waits up to `ms` ticks (negative = forever). The pushback supports
 * the ANSI-escape decoder's one-byte lookahead. */

static int s_pushback = -1;

int esp32_console_read_byte_nonblock(void) {
    if (s_pushback >= 0) { int c = s_pushback; s_pushback = -1; return c; }
    unsigned char c;
    int n = usb_serial_jtag_read_bytes(&c, 1, 0);
    return (n == 1) ? (int)c : -1;
}

int esp32_console_read_byte_blocking_ms(int ms) {
    if (s_pushback >= 0) { int c = s_pushback; s_pushback = -1; return c; }
    TickType_t ticks = (ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    if (ms > 0 && ticks == 0) ticks = 1;
    unsigned char c;
    int n = usb_serial_jtag_read_bytes(&c, 1, ticks);
    return (n == 1) ? (int)c : -1;
}

void esp32_console_push_back_byte(int c) { s_pushback = c; }

/* MMBasic-facing console glue (MMputchar, MMPrintString, MMInkey,
 * SerialConsolePutC, ConsoleRxBuf*, MMgetline, …) lives in
 * esp32_mmbasic_console_glue.c — kept in a separate TU because pulling
 * in MMBasic_Includes.h here clashes with IDF's vfs.h DIR typedef. */
