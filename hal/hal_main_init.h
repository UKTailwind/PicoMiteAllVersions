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

/* REPL-startup WiFi init: real impl on WiFi ports calls
 * cyw43_arch_init + WebConnect; stub no-op elsewhere. Called from
 * MMBasic_REPL.c after the program-memory clear. */
void port_repl_wifi_arch_init_and_connect(void);

/* REPL-startup display refresh: real impl on PicoMite SPI-LCD sets
 * SPIatRisk and full-frame refreshes the panel; stub no-op
 * elsewhere. */
void port_repl_post_clear_display_refresh(void);

/* PWM-mode GPIO 23 shadow — driven by the global PWM-enable on
 * non-WiFi ports (MMweb_stubs.c real impl); WiFi ports leave GPIO
 * 23 to the CYW43 module (MMsetwifi.c stub). Called from
 * mmc_stm32.c after audio/PWM init. */
void hal_pwm_mode_shadow_apply(void);

/* Re-enable input drivers on every PIO-eligible pin between
 * cmd_pio configures. Custom.c calls this at the end of each PIO
 * INIT/CONFIGURE setup. The pin upper bound differs by port shape:
 * WiFi ports include the full NBRPINS range, non-WiFi RP2350-A
 * stops at pin 43 (the CYW43-shadow region), other builds use the
 * full range. */
void port_pio_pin_reset_inputs(void);

/* Load the i2s PIO program once during start_i2s. Pure-VGA ports
 * skip this — drivers/vga_pio/vga_qvga_modes.c already loaded the
 * program when QVGA scanout came up because audio I²S shares PIO 0
 * with the scanout state machines on those boards. See
 * drivers/audio_i2s_pio/{audio_i2s_pio_load,audio_i2s_pio_stub}.c.
 * Pass the PIO instance as a void* so this header doesn't drag in
 * hardware/pio.h on host builds (the impls cast back to PIO). */
void port_audio_i2s_pio_add_program(void * pio);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MAIN_INIT_H */
