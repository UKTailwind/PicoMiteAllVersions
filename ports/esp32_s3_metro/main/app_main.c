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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_psram.h"
#include "runtime/runtime.h"

extern jmp_buf mark;
extern unsigned char flash_prog_buf[];
extern const uint8_t *flash_progmemory;
extern void flash_range_erase(uint32_t off, uint32_t count);
extern void esp32_console_init(void);
extern void MMBasic_PrintBanner(void);
extern int esp32_flash_storage_load_options(void);
extern int esp32_flash_storage_init(void);
extern int esp32_web_console_display_init(void);

static int esp32_options_valid(void) {
    return Option.Magic == MagicKey &&
           Option.Width > 0 &&
           Option.Height > 0 &&
           (Option.Tab == 2 || Option.Tab == 3 || Option.Tab == 4 || Option.Tab == 8) &&
           Option.PROG_FLASH_SIZE == MAX_PROG_SIZE;
}

static void esp32_apply_terminal_option_defaults(void) {
    Option.DISPLAY_CONSOLE = 0;     /* no LCD - REPL is serial-only */
    Option.Width  = 80;
    Option.Height = 24;
    Option.Tab    = 4;
    Option.DefaultFont = 0x01;
    Option.ColourCode = 0;
    Option.DefaultFC = 0x00ff00;
    Option.DefaultBC = 0x000000;
    Option.Baudrate  = 115200;
    Option.repeat    = 0;
    Option.PIN       = 0;
    Option.Autorun   = 0;
    Option.Invert    = 0;
    Option.Listcase  = 0;
    Option.continuation = 0;
}

void app_main(void) {
    /* Console first so any printf/MMPrintString is captured cleanly. */
    esp32_console_init();
    esp_log_level_set("gpio", ESP_LOG_WARN);

    /* Brief pause so the host monitor has a chance to attach before
     * the banner flies past. */
    for (int i = 0; i < 5; i++) {
        printf(".");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    printf("\n");

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

    esp32_flash_storage_load_options();
    LoadOptions();
    if (!esp32_options_valid()) {
        ResetOptions(true);
        esp32_apply_terminal_option_defaults();
        SaveOptions();
    }

    extern short gui_font_width, gui_font_height;
    gui_font_width  = 8;
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

    /* ClearRuntime initialises OptionConsole (= 3 BOTH) and several other
     * runtime globals MMBasic expects to be sane before the first
     * EditInputLine. Without it, putConsole's `if (OptionConsole & 1)`
     * gate is false and PRINT / cmd_print produce no output until the
     * first error() call retroactively sets OptionConsole=1. */
    ClearRuntime(true);
    if (esp32_web_console_display_init()) SaveOptions();

    /* Mirror the Pico pattern: always call WebConnect at boot. The
     * lifecycle no-ops cleanly when Option.SSID is empty, and on success
     * it opens whichever network services are enabled (telnet, web
     * console) via mm_net_lifecycle_on_network_ready(). */
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
