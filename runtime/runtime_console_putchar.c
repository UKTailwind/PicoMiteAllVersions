/*
 * runtime/runtime_console_putchar.c — shared MMputchar across every port.
 *
 * Previously duplicated in four places: runtime/runtime_console.c,
 * ports/pico_sdk_common/pico_console.c, ports/pc386/pc386_runtime.c, and
 * ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c. All four copies
 * had byte-identical bodies (`putConsole(c, flush)` + MMCharPos tracking).
 * Consolidated per docs/port-duplication-audit.md Finding 1 — partial:
 * MMPrintString / SSPrintString remain per-port because each port's
 * trailing-flush semantics differ in ways that require console_adapter
 * plumbing to merge.
 *
 * Body is intentionally short — every dependency (`putConsole`,
 * `MMCharPos`) is already a project-wide symbol resolved by each port's
 * own console glue.
 */

#include <ctype.h>

extern void putConsole(int c, int flush);
extern int MMCharPos;

char MMputchar(char c, int flush) {
    putConsole(c, flush);
    if (isprint((unsigned char)c)) MMCharPos++;
    if (c == '\r') MMCharPos = 1;
    return c;
}
