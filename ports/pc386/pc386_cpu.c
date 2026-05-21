/*
 * ports/pc386/pc386_cpu.c — x86 CPU drivers exposed to MMBasic.
 *
 *   CPU RESTART       — 8042-KBC pulse-reset, triple-fault fallback.
 *   CPU SLEEP n       — busy-wait sleep for n seconds.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern void     SoftReset(void);
extern void     hal_time_sleep_us(uint32_t us);

void cmd_cpu(void) {
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"RESTART"))) {
        checkend(p);
        hal_time_sleep_us(10000);
        SoftReset();
    } else if ((p = checkstring(cmdline, (unsigned char *)"SLEEP"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        MMFLOAT seconds = getnumber(argv[0]);
        if (seconds <= 0.0) error("Invalid period");
        hal_time_sleep_us((uint32_t)(seconds * 1000000.0));
    } else {
        error("Syntax");
    }
}

/* (fun_cpuid removed — `Cpuid(` isn't registered in core/mmbasic/
 * AllCommands.h's token table, so the function isn't BASIC-reachable.
 * Wiring it up requires a core change.) */
