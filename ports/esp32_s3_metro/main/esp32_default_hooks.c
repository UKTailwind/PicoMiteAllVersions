/*
 * esp32_default_hooks.c — generic port_* no-op defaults for the ESP32-S3
 * port. None of these need real bodies in the stdio-REPL litmus scope;
 * each is a one-liner that returns 0 / no-op so the link succeeds.
 *
 * Per the D-decouple plan: ESP32 owns its full port surface. These were
 * previously satisfied by host_runtime.c / host_peripheral_stubs.c via
 * --allow-multiple-definition. Each entry below replaces a host_native
 * symbol the ESP32 link should not borrow.
 *
 * Symbols requiring real bodies live elsewhere:
 *   - port_drive_check / port_mount_sd_drive / port_apply_load_overrides
 *     → esp32_cmd_files_hooks.c (A:-only filesystem)
 *   - port_terminal_handle_cls / port_terminal_emit_colour
 *     → esp32_terminal.c (VT100 escapes; landed in D5)
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"

/* bc_bridge — funtbl[] subfun-hash rebuild on rp2350; no-op elsewhere. */
void port_bc_bridge_clear_subfun_hash(void) {}
void port_bc_bridge_rehash_subfun(unsigned char **subfun_arr) { (void)subfun_arr; }

/* bc_debug crash trail — read current stack pointer for crash dump. ESP32
 * has FreeRTOS task stacks; without a forcing function, just return 0
 * (the crash trail still records the most-recent VM op trail without it). */
uint32_t port_bc_crash_get_sp(void) { return 0; }
void port_bc_crash_save_fault_regs(BCCrashInfo *info) { (void)info; }

/* clear_runtime / error display — frame-buffer / LCD-banner no-ops on a
 * stdio-only port. */
void port_clear_runtime_display_reset(void) {}
void port_error_restore_console_surface(void) {}
void port_error_show_lcd_banner(int line_num, const char *source_line, const char *err_msg) {
    (void)line_num; (void)source_line; (void)err_msg;
}

/* MQTT — peripheral stub; no MQTT in stdio scope. The two-arg shape
 * mirrors host_peripheral_stubs.c. */
void port_fun_mm_mqtt_copy(int which, unsigned char *out) {
    (void)which; (void)out;
}

/* prepare_program subfun finalisation — rp2350 / pico-only path. No-op
 * on ESP32 (the funtbl hash rebuild used elsewhere isn't built here). */
void port_prepare_program_finalize_subfun(int ErrAbort) { (void)ErrAbort; }

/* WiFi-arch init — pico_sdk_common drives this via cyw43_arch on
 * PicoMiteWEB; ESP32 has native WiFi but the stdio scope explicitly
 * excludes it. No-op for now; real init lands when WiFi opts in. */
void port_repl_wifi_arch_init_and_connect(void) {}

/* Error-banner font selection — only meaningful when there's an LCD. */
void port_select_error_prompt_font(void) {}

/* Default Options after factory-reset / no-saved-options. ESP32-specific
 * defaults (heap size, banner colours) come from port_config.h via the
 * normal LoadOptions() path; this hook is no-op. */
void port_set_default_options(void) {}

/* funtbl[] hash-table queries — pico drives a real hash; host + ESP32
 * fall back to linear scans (return 0 = "not handled, do linear scan"). */
int port_try_check_var_subfun_collision(const unsigned char *name, int namelen) {
    (void)name; (void)namelen; return 0;
}
int port_try_find_label_hash(unsigned char *labelptr, unsigned char **out_ptr) {
    (void)labelptr; (void)out_ptr; return 0;
}
int port_try_find_subfun_hash(unsigned char *p, int *out_index) {
    (void)p; (void)out_index; return 0;
}

/* vm_sys_time clock source for DATE$ / TIME$ helpers. Returns -1 to
 * tell vm_sys_time to fall back to its monotonic-seconds path
 * (esp_timer_get_time-derived). Wall-clock support would call
 * localtime_r over an NTP-set RTC; deferred until a forcing function. */
int port_vm_time_get_tm(struct tm *out) {
    (void)out;
    return -1;
}

/* Web-runtime state reset for PicoMiteWEB. No-op on ESP32 stdio. */
void port_web_clear_runtime_state(void) {}
