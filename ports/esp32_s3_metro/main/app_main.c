/*
 * ports/esp32_s3_metro/main/app_main.c
 *
 * Phase C: USB Serial/JTAG REPL.
 *
 * Initialises USB Serial/JTAG for non-blocking line-edited I/O,
 * brings up MMBasic core, then enters MMBasic_RunPromptLoop().
 * The chip's onboard LED blinks on a separate FreeRTOS task as a
 * liveness indicator independent of MMBasic.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_psram.h"
#include "runtime/runtime.h"

extern jmp_buf mark;
extern unsigned char flash_prog_buf[];
extern const uint8_t * flash_progmemory;
extern void flash_range_erase(uint32_t off, uint32_t count);
extern void esp32_console_init(void);
extern void MMBasic_PrintBanner(void);
extern int esp32_flash_storage_init(void);
extern int esp32_web_console_display_init(void);
extern void esp32_usb_role_resolve_boot(void);
extern int esp32_usb_role_is_serial(void);
extern int esp32_usb_role_is_keyboard(void);
extern void esp32_usb_role_prepare_keyboard_host(void);
extern void esp32_usb_keyboard_start_host(void);
extern void esp32_usb_keyboard_start_hid(void);
extern int esp32_usb_keyboard_has_keyboard(void);
extern void esp32_usb_keyboard_print_status(void);

static const char * TAG = "app_main";

static int esp32_saved_options_valid(const char ** reason) {
    if (Option.Magic != MagicKey) {
        if (reason) *reason = "bad magic";
        return 0;
    }
    if (Option.Width <= 0) {
        if (reason) *reason = "bad width";
        return 0;
    }
    if (Option.Height <= 0) {
        if (reason) *reason = "bad height";
        return 0;
    }
    if (!(Option.Tab == 2 || Option.Tab == 3 || Option.Tab == 4 || Option.Tab == 8)) {
        if (reason) *reason = "bad tab";
        return 0;
    }
    if (Option.PROG_FLASH_SIZE != MAX_PROG_SIZE) {
        if (reason) *reason = "program flash size mismatch";
        return 0;
    }
    if (!(Option.USBRole == USB_ROLE_SERIAL || Option.USBRole == USB_ROLE_KEYBOARD)) {
        if (reason) *reason = "bad usb role";
        return 0;
    }
    if (reason) *reason = "valid";
    return 1;
}

static void esp32_keyboard_mode_recovery(void) {
    if (!esp32_usb_role_is_keyboard()) return;

    esp32_usb_keyboard_print_status();
    for (int i = 0; i < 100; i++) {
        if (esp32_usb_keyboard_has_keyboard()) {
            MMPrintString("USB keyboard attached\r\n\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    MMPrintString("\r\nUSB keyboard not enumerated yet; staying in USB KEYBOARD mode\r\n");
    esp32_usb_keyboard_print_status();
}

void app_main(void) {
    esp_log_level_set("gpio", ESP_LOG_WARN);

    /* Acquire the PSRAM slab and publish PSRAMbase / PSRAMsize before
     * any code reads them. mmbasic_runtime_init_common's heap init does
     * not depend on PSRAM, but the shared boot banner (and any later
     * BASIC code referencing MM.INFO(PSRAM SIZE)) does. */
    hal_psram_init();

    /* MMBasic boot. flash_prog_buf is sized MAX_PROG_SIZE + 4096 in
     * esp32_compat.c; the constructor 0xff-fills both the program region
     * and the trailer to mirror erased-flash semantics. PrepareProgramExt
     * walks past the program terminator looking for 0xff as the "end of
     * program / start of CFunction area" sentinel — non-0xff bytes there
     * cause it to deref garbage. */
    flash_progmemory = flash_prog_buf;

    LoadOptions();
    const char * options_reason = NULL;
    if (!esp32_saved_options_valid(&options_reason)) {
        ESP_LOGE(TAG, "saved options rejected at boot: %s; resetting to board defaults",
                 options_reason ? options_reason : "invalid");
        ResetOptions(true);
    }

    esp32_usb_role_resolve_boot();
    if (esp32_usb_role_is_serial()) {
        esp32_console_init();

        /* Brief pause so the host monitor has a chance to attach before
         * the banner flies past. */
        for (int i = 0; i < 5; i++) {
            printf(".");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        printf("\n");
    } else if (esp32_usb_role_is_keyboard()) {
        /* USB host startup is done after VGA is alive. Starting it here
         * leaves keyboard-mode boots with no local diagnostics if host
         * setup interferes with display bring-up. */
    }

    extern short gui_font_width, gui_font_height;
    gui_font_width = 8;
    gui_font_height = 12;
    ApplyDefaultConsoleColours();
    esp32_flash_storage_init();

    mmbasic_runtime_init_common(NULL,
                                MMBASIC_RUNTIME_INIT_FLAG_INIT_BASIC |
                                    MMBASIC_RUNTIME_INIT_FLAG_INIT_HEAP |
                                    MMBASIC_RUNTIME_INIT_FLAG_CLEAR_ERROR);

    extern void esp32_sd_diskio_reset(void);
    extern void vm_sys_file_reset(void);
    extern void vm_sys_pin_reset(void);
    esp32_sd_diskio_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
    extern void esp32_audio_reserve_option_pins(void);
    esp32_audio_reserve_option_pins();

    /* ClearRuntime initialises OptionConsole (= 3 BOTH) and several other
     * runtime globals MMBasic expects to be sane before the first
     * EditInputLine. Without it, putConsole's `if (OptionConsole & 1)`
     * gate is false and PRINT / cmd_print produce no output until the
     * first error() call retroactively sets OptionConsole=1. */
    ClearRuntime(true);
    (void)esp32_web_console_display_init();

    /* Bring up VGA before USB host, Wi-Fi, and LittleFS. In keyboard mode
     * this is the only guaranteed local diagnostic path. */
    extern void esp32_vga_display_init(void);
    esp32_vga_display_init();

    if (esp32_usb_role_is_keyboard()) {
        MMPrintString("USB KEYBOARD MODE: starting host\r\n");
        esp32_usb_role_prepare_keyboard_host();
        esp32_usb_keyboard_start_host();
        MMPrintString("USB KEYBOARD MODE: starting raw HID\r\n");
        esp32_usb_keyboard_start_hid();
        esp32_keyboard_mode_recovery();
    }

    /* Mirror the Pico pattern: always call WebConnect at boot. In keyboard
     * mode USB host starts first because Wi-Fi consumes scarce internal RAM
     * needed by the USB host controller and transfer tasks. */
    extern void WebConnect(void);
    WebConnect();

    /* Mount LittleFS for A: drive eagerly so cmd_files / cmd_save /
     * cmd_load can call lfs_*_open directly without going through
     * hal_fs_* (which would lazy-mount). First boot formats + writes
     * the bundled demo .bas files. */
    extern int esp32_lfs_mount(void);
    esp32_lfs_mount();

    MMBasic_PrintBanner();

    /* MMBasic_RunPromptLoop is its own setjmp loop — it longjmps back
     * to its own `mark` on error / Ctrl-C / END / NEW. We don't return. */
    mmbasic_runtime_enter_repl(NULL, 0);
}
