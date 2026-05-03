/*
 * hal/hal_main_init.h — boot-time core1 launch + final display
 * preparation. Called once from PicoMite.c::main() after the boot
 * banner has been printed and the keyboard subsystem is initialised.
 *
 * Real impls per port:
 *   drivers/vga_pio/vga_qvga_modes.c   — pure-VGA QVGA scanout core
 *   drivers/hdmi/hdmi_scanout.c        — HDMI HSTX scanout core
 *   drivers/display_merge/display_merge_pico.c — SPI-LCD merge core
 *   drivers/main_init/main_init_stub.c — WEB / no-core1 ports
 */

#ifndef HAL_MAIN_INIT_H
#define HAL_MAIN_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Launch the per-port core1 worker (HDMICore / QVgaCore / UpdateCore)
 * and perform any post-launch display reset (pure-VGA + HDMI clear
 * the framebuffer; SPI-LCD does the reset earlier via
 * InitDisplaySSD/SPI). Stub no-ops on WEB. */
void port_main_launch_core1(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MAIN_INIT_H */
