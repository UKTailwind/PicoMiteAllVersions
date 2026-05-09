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

    /* MMBasic boot, mirroring mmbasic_stdio's run_interpreter. */
    memset(flash_prog_buf, 0, MAX_PROG_SIZE);
    memset(flash_prog_buf + MAX_PROG_SIZE, 0xff, MAX_PROG_SIZE);
    flash_progmemory = flash_prog_buf;

    LoadOptions();
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

    MMBasic_PrintBanner();

    /* MMBasic_RunPromptLoop is its own setjmp loop — it longjmps back
     * to its own `mark` on error / Ctrl-C / END / NEW. We don't return. */
    MMBasic_RunPromptLoop();
}
