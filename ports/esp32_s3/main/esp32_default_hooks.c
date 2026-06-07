/*
 * esp32_default_hooks.c — generic port_* no-op defaults for the ESP32-S3
 * port. None of these need real bodies in the stdio-REPL litmus scope;
 * each is a one-liner that returns 0 / no-op so the link succeeds.
 *
 * Per the D-decouple plan: ESP32 owns its full port surface. Each entry
 * below replaces a symbol that was borrowed from host_native during
 * early bring-up.
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
#include "esp32_board_profile.h"
#include "esp32_option_ext.h"
#include "port_config.h"
#include "hal/hal_calendar.h"
#include "hal/hal_time.h"
#include "shared/net/mm_net_lifecycle.h"
#include "bytecode.h"
#include "esp32_mqtt.h"
#include "esp32_tcp_client.h"
#include "esp32_tcp_server.h"
#include "esp32_telnet.h"
#include "esp32_tftp.h"

/* bc_bridge — funtbl[] subfun-hash rebuild on rp2350; no-op elsewhere. */
void port_bc_bridge_clear_subfun_hash(void) {}
void port_bc_bridge_rehash_subfun(unsigned char ** subfun_arr) {
    (void)subfun_arr;
}

/* bc_debug crash trail — read current stack pointer for crash dump. ESP32
 * has FreeRTOS task stacks; without a forcing function, just return 0
 * (the crash trail still records the most-recent VM op trail without it). */
uint32_t port_bc_crash_get_sp(void) {
    return 0;
}
void port_bc_crash_save_fault_regs(BCCrashInfo * info) {
    (void)info;
}

/* clear_runtime / error display — frame-buffer / LCD-banner no-ops on a
 * stdio-only port. */
void port_clear_runtime_display_reset(void) {}
void port_error_restore_console_surface(void) {}
void port_error_show_lcd_banner(int line_num, const char * source_line, const char * err_msg) {
    (void)line_num;
    (void)source_line;
    (void)err_msg;
}

/* prepare_program subfun finalisation — rp2350 / pico-only path. No-op
 * on ESP32 (the funtbl hash rebuild used elsewhere isn't built here). */
void port_prepare_program_finalize_subfun(int ErrAbort) {
    (void)ErrAbort;
}

/* Runtime END/Clear cleanup. ESP32 stdio scope has no DMA/watchdog
 * resources owned by the shared Pico paths. */
void port_runtime_abort_dma(void) {}
void port_runtime_disable_watchdog(void) {}

/* Error-banner font selection — only meaningful when there's an LCD. */
void port_select_error_prompt_font(void) {}

/* Default Options after factory-reset / no-saved-options. The shared
 * option layer owns when these are persisted; the port only supplies the
 * board defaults. */
void port_set_default_options(void) {
    Option.DISPLAY_CONSOLE = 0;
    ESP32_OPTION_USB_ROLE = USB_ROLE_SERIAL;
    esp32_board_profile_apply_defaults(
        esp32_board_profile_by_id(ESP32_BOARD_PROFILE_ID_GENERIC));
}

void port_print_supported_boards(void) {
    MMPrintString(ESP32_BOARD_GENERIC_NAME "\r\n");
    MMPrintString(ESP32_BOARD_METRO_NAME "\r\n");
    MMPrintString(ESP32_BOARD_FREENOVE_ILI9341_NAME "\r\n");
}

static void factory_reset_to_profile(const esp32_board_profile_t * profile) {
    ResetOptions(false);
    esp32_board_profile_apply_defaults(profile);
    SaveOptions();
    printoptions();
    uSec(100000);
    _excep_code = RESET_COMMAND;
    SoftReset();
}

int port_factory_reset_board(unsigned char * p) {
    const esp32_board_profile_t * profile = esp32_board_profile_by_name(p);
    if (!profile) return 0;
    factory_reset_to_profile(profile);
    return 1;
}

/* funtbl[] hash-table queries — pico drives a real hash; host + ESP32
 * fall back to linear scans (return 0 = "not handled, do linear scan"). */
int port_try_check_var_subfun_collision(const unsigned char * name, int namelen) {
    (void)name;
    (void)namelen;
    return 0;
}
int port_try_find_label_hash(unsigned char * labelptr, unsigned char ** out_ptr) {
    (void)labelptr;
    (void)out_ptr;
    return 0;
}
int port_try_find_subfun_hash(unsigned char * p, int * out_index) {
    (void)p;
    (void)out_index;
    return 0;
}

int port_vm_time_get_tm(struct tm * out) {
    extern int64_t TimeOffsetToUptime;
    time_t epochnow = (time_t)(hal_time_us_64() / 1000000 + TimeOffsetToUptime);
    hal_calendar_epoch_to_tm(epochnow, out);
    return 1;
}

void port_web_clear_runtime_state(void) {
    static const mm_net_lifecycle_runtime_hooks_t hooks = {
        .clear_tcp_requests = esp32_tcp_server_clear_requests,
        .close_tcp_client = esp32_tcp_client_close,
        .close_mqtt = closeMQTT,
        .close_tftp_session = esp32_tftp_close_session,
    };
    mm_net_lifecycle_runtime_reset(&hooks);
}
