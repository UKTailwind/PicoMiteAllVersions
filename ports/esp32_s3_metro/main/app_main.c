/*
 * ports/esp32_s3_metro/main/app_main.c
 *
 * Phase B: link-validation entry point. Calls a minimum set of
 * MMBasic core functions to force --gc-sections to keep the core
 * + bytecode VM in the final image. If this links, the HAL surface
 * is closed enough to start porting Phase C (real impls).
 *
 * Still blinks GPIO13 + heartbeats so we can see the chip is alive
 * even before we see MMBasic output.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#define LED_GPIO GPIO_NUM_13

extern jmp_buf mark;
extern unsigned char flash_prog_buf[];
extern const uint8_t *flash_progmemory;
extern void flash_range_erase(uint32_t off, uint32_t count);

void app_main(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    printf("\n=== MMBasic Anywhere - ESP32-S3 Phase B ===\n");
    printf("Running: PRINT 1+1\n");
    printf("Result : ");
    fflush(stdout);

    /* Pull MMBasic core / VM into the final link by calling
     * representative entry points. The actual init sequence below
     * mirrors mmbasic_stdio; if any of these symbols can't resolve
     * on Xtensa, the linker tells us exactly which file is missing
     * a stub. */
    memset(flash_prog_buf, 0, MAX_PROG_SIZE);
    memset(flash_prog_buf + MAX_PROG_SIZE, 0xff, MAX_PROG_SIZE);
    flash_progmemory = flash_prog_buf;

    LoadOptions();
    InitBasic();
    InitHeap(true);
    MMerrno = 0;
    MMErrMsg[0] = '\0';

    /* Smallest possible BASIC program: PRINT 1+1 */
    const char *prog = "PRINT 1+1\n";
    flash_range_erase(0, MAX_PROG_SIZE);
    unsigned char *pm = (unsigned char *)ProgMemory;
    memcpy(inpbuf, prog, strlen(prog));
    inpbuf[strlen(prog)] = 0;
    /* strip trailing \n; tokenise expects line without it */
    if (inpbuf[strlen((char*)inpbuf) - 1] == '\n') inpbuf[strlen((char*)inpbuf) - 1] = 0;
    tokenise(0);
    unsigned char *tp = tknbuf;
    while (!(tp[0] == 0 && tp[1] == 0)) *pm++ = *tp++;
    *pm++ = 0; *pm++ = 0; *pm++ = 0;

    extern void vm_host_fat_reset(void);
    extern void vm_sys_file_reset(void);
    extern void vm_sys_pin_reset(void);
    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
    ClearRuntime(true);
    PrepareProgram(1);

    if (setjmp(mark) == 0) {
        ExecuteProgram((unsigned char *)ProgMemory);
        printf("\n=== MMBasic exited cleanly ===\n");
    } else {
        printf("\n=== MMBasic error: %s ===\n", MMErrMsg);
    }
    fflush(stdout);

    /* heartbeat so we can confirm the chip is alive even if MMBasic
     * output got swallowed by USB Serial/JTAG enumeration timing */
    int n = 0;
    while (1) {
        gpio_set_level(LED_GPIO, n & 1);
        printf("tick %d\n", n);
        fflush(stdout);
        n++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
