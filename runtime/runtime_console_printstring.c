/*
 * runtime/runtime_console_printstring.c — shared MMPrintString /
 * SSPrintString across every port.
 *
 * Previously duplicated in four sites with subtly different trailing-
 * flush behaviour:
 *
 *   runtime/runtime_console.c               (host/wasm/stdio/ansi):
 *     loop + console_adapter.stdout_flush + console_adapter.telnet_putc(0,-1)
 *   ports/pico_sdk_common/pico_console.c:
 *     loop with last byte flush=1 + fflush(stdout)
 *   ports/esp32_s3/main/esp32_mmbasic_console_glue.c:
 *     loop flush=0 + fflush(stdout)
 *   ports/pc386/pc386_runtime.c:
 *     loop flush=0, no flush (pc386_libc.c::fflush is a no-op shim)
 *
 * Pico's per-byte flush=1 quirk was equivalent to the trailing-fflush
 * form: TelnetPutC ignores flush=1 (only the -1 sentinel drains), and
 * console_cdc_putc(c, 1) is exactly `putc(c, stdout); fflush(stdout)`.
 * So fusing the flush with the last byte vs. trailing fflush produces
 * byte-identical USB output and identical telnet behaviour (5 ms timer
 * in pico_telnet_poll drains the buffer either way).
 *
 * This TU uses the existing mm_runtime_console_adapter slots when an
 * adapter is set (host family already plugs them) and falls back to
 * fflush(stdout) when no adapter is set (Pico/ESP32 get their stdio
 * flush; pc386's libc shim makes the fallback a harmless no-op).
 * Telnet-drain at end-of-bulk is only emitted when the adapter plugs
 * telnet_putc — host wants it (so its host_telnet_putc gets the -1
 * sentinel), Pico/ESP32 deliberately leave it NULL so their 5 ms
 * coalescing timer batches TCP sends instead.
 *
 * See docs/port-duplication-audit.md Finding 1.
 */

#include <stdio.h>
#include "runtime.h"

extern char MMputchar(char c, int flush);
extern char SerialConsolePutC(char c, int flush);

static void bulk_flush(void) {
    const mm_runtime_console_adapter * a = mmbasic_runtime_console_get_adapter();
    if (a && a->stdout_flush) {
        a->stdout_flush();
    } else {
        fflush(stdout);
    }
    if (a && a->telnet_putc) {
        a->telnet_putc(0, -1);
    }
}

void MMPrintString(char * s) {
    while (*s) MMputchar(*s++, 0);
    bulk_flush();
}

void SSPrintString(char * s) {
    while (*s) SerialConsolePutC(*s++, 0);
    bulk_flush();
}
