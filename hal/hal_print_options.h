/*
 * hal/hal_print_options.h — per-port-shape OPTION LIST printers.
 * Sub-hooks called from ports/pico_sdk_common/print_display_options.c.
 * Each is provided by exactly one driver TU on each port.
 */

#ifndef HAL_PRINT_OPTIONS_H
#define HAL_PRINT_OPTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Print the KEYBOARD-family lines (panel layout, pins, mouse,
 * conditional REPEAT). USB-host driver prints USB layout; PS/2
 * driver prints PS/2 layout + KEYBOARD PINS + MOUSE + LOCAL-keyboard
 * REPEAT. */
void port_print_kb_layout(void);

/* Print the standalone OPTION KEYBOARD REPEAT line that USB-host
 * builds emit late in the OPTION LIST output (after GUI controls).
 * PS/2 builds emit their REPEAT inside port_print_kb_layout, so
 * this hook is a stub on PS/2 ports. */
void port_print_kb_repeat(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PRINT_OPTIONS_H */
