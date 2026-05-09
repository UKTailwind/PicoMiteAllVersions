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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern jmp_buf mark;
extern unsigned char flash_prog_buf[];
extern const uint8_t *flash_progmemory;
extern void flash_range_erase(uint32_t off, uint32_t count);
extern void esp32_console_init(void);
extern void MMBasic_RunPromptLoop(void);
extern void MMBasic_PrintBanner(void);

void app_main(void) {
    /* Console first so any printf/MMPrintString is captured cleanly. */
    esp32_console_init();

    /* Brief pause so the host monitor has a chance to attach before
     * the banner flies past. */
    for (int i = 0; i < 5; i++) {
        printf(".");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    printf("\n");

    /* MMBasic boot. flash_prog_buf is sized MAX_PROG_SIZE + 4096 in
     * esp32_compat.c; the constructor 0xff-fills both the program region
     * and the trailer to mirror erased-flash semantics. PrepareProgramExt
     * walks past the program terminator looking for 0xff as the "end of
     * program / start of CFunction area" sentinel — non-0xff bytes there
     * cause it to deref garbage. */
    flash_progmemory = flash_prog_buf;

    LoadOptions();
    /* The hal_flash_esp32_stub doesn't persist Options anywhere, so
     * LoadOptions populates Option with zeros — and zeros are wrong
     * for many fields the console code uses as multipliers, divisors,
     * or limit checks (e.g. Option.Width=0 makes the line editor's
     * wrap math collapse; gui_font=0 divides by zero in
     * ListProgram). Mirror the defaults the host_main / host_wasm /
     * mmbasic_ansi ports set, adjusted for serial-only operation
     * (no LCD framebuffer on this board yet).
     *
     * After setting the in-memory Option, snapshot it into the
     * flash_option_contents RAM mirror so error() → LoadOptions()
     * restores the same defaults rather than re-zeroing them.
     * Phase E replaces this with an NVS-backed real flash impl. */
    Option.DISPLAY_CONSOLE = 0;     /* no LCD — REPL is serial-only */
    Option.Width  = 80;             /* terminal columns */
    Option.Height = 24;             /* terminal rows */
    Option.Tab    = 4;
    Option.DefaultFont = 0x01;
    Option.ColourCode = 0;
    Option.DefaultFC = 0x00ff00;
    Option.DefaultBC = 0x000000;
    Option.Baudrate  = 115200;
    Option.repeat    = 0;
    /* Zero out everything that, when 0xff-filled from a fresh flash,
     * would put MMBasic into a "weird mode" — PIN-locked console,
     * autorun-on-boot, listcase confusion, invert flag, etc. */
    Option.PIN       = 0;
    Option.Autorun   = 0;
    Option.Invert    = 0;
    Option.Listcase  = 0;
    Option.continuation = 0;

    extern short gui_font, gui_font_width, gui_font_height;
    extern int gui_fcolour, gui_bcolour;
    extern int PromptFC, PromptBC;
    gui_font = Option.DefaultFont;
    gui_font_width  = 8;
    gui_font_height = 12;
    gui_fcolour = Option.DefaultFC;
    gui_bcolour = Option.DefaultBC;
    PromptFC = Option.DefaultFC;
    PromptBC = Option.DefaultBC;

    /* Sync our defaults into the flash mirror so subsequent
     * LoadOptions calls (triggered from inside error()) don't
     * re-zero everything we just set. */
    extern void host_options_snapshot(void);
    host_options_snapshot();

    InitBasic();
    InitHeap(true);
    MMerrno = 0;
    MMErrMsg[0] = '\0';

    extern void vm_host_fat_reset(void);
    extern void vm_sys_file_reset(void);
    extern void vm_sys_pin_reset(void);
    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();

    /* ClearRuntime initialises OptionConsole (= 3 BOTH) and several other
     * runtime globals MMBasic expects to be sane before the first
     * EditInputLine. Without it, putConsole's `if (OptionConsole & 1)`
     * gate is false and PRINT / cmd_print produce no output until the
     * first error() call retroactively sets OptionConsole=1. */
    ClearRuntime(true);

    /* Mount LittleFS for A: drive eagerly so cmd_files / cmd_save /
     * cmd_load can call lfs_*_open directly without going through
     * hal_fs_* (which would lazy-mount). First boot formats + writes
     * the bundled demo .bas files. */
    extern int esp32_lfs_mount(void);
    esp32_lfs_mount();

    MMBasic_PrintBanner();

    /* MMBasic_RunPromptLoop is its own setjmp loop — it longjmps back
     * to its own `mark` on error / Ctrl-C / END / NEW. We don't return. */
    MMBasic_RunPromptLoop();
}
