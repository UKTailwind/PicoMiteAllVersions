/*
 * esp32_system.c — chip-level commands (CPU RESTART, CPU SLEEP).
 *
 * Mirrors PicoMite's MM_Misc.c::cmd_cpu, with esp_restart() in place
 * of the rp2040 watchdog reset.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_time.h"

extern char SerialConsolePutC(char c, int flush);

/* port_drive_check moved to esp32_cmd_files_hooks.c (Step C) — host_runtime.c
 * is no longer in the link, so we no longer need --wrap to override it. */

void cmd_cpu(void) {
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"RESTART"))) {
        const char *msg = "\r\nRestarting...\r\n";
        while (*msg) SerialConsolePutC(*msg++, 0);
        vTaskDelay(pdMS_TO_TICKS(50));   /* drain output */
        esp_restart();
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SLEEP"))) {
        getargs(&p, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        double secs = getnumber(argv[0]);
        if (secs <= 0.0) error("Invalid period");
        hal_time_sleep_us((uint32_t)(secs * 1000000.0));
        return;
    }
    error("Syntax");
}
