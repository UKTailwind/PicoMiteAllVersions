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

/* Sanitize Option.* on first boot, before set_sys_clock_khz.
 *   pure VGA: VGA_HSYNC / VGA_BLUE pin defaults if zero.
 *   HDMI:     clamp Option.CPU_Speed to a valid HSTX frequency.
 *   SPI-LCD / WEB: no-op. */
void port_video_validate_boot_options(void);

/* Per-port translation from Option.CPU_Speed to the kHz value to feed
 * set_sys_clock_khz. HDMI clamps Option.CPU_Speed==FreqX to 252 MHz so
 * the CPU clock differs from the HSTX serializer clock. Other ports
 * pass cpu_khz through unchanged. */
unsigned port_video_sys_clock_khz(unsigned cpu_khz);

/* Run after set_sys_clock_khz + watchdog disable. Per-port adjustments
 * to framebuffersize / heap_memory_size for non-default CPU_Speed
 * modes, plus HSTX-clock + QVGA_CLKDIV setup where applicable. SPI-LCD
 * + WEB are no-ops. */
void port_video_post_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MAIN_INIT_H */
