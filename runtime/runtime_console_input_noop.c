/*
 * runtime/runtime_console_input_noop.c — shared no-op fallbacks for
 * getConsole() / kbhitConsole().
 *
 * Used by ports that have no real keyboard or console input source:
 * host_native, host_wasm, mmbasic_ansi, mmbasic_stdio (input comes
 * through the harness / scripted-key hook, not getConsole) and pc386
 * (PS/2 keys arrive via the IRQ-driven ring drained inside MMInkey,
 * never through getConsole). Previously duplicated as 1-line bodies
 * in ports/host_native/host_runtime.c and ports/pc386/pc386_runtime.c.
 *
 * Pico (ports/pico_sdk_common/pico_console.c) and ESP32
 * (ports/esp32_s3/main/esp32_mmbasic_console_glue.c) supply
 * real implementations and deliberately do not link this TU.
 *
 * docs/port-duplication-audit.md Finding 10.
 */

int getConsole(void) {
    return -1;
}
int kbhitConsole(void) {
    return 0;
}
