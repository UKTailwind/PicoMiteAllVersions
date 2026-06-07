/*
 * esp32_compat.c — small porting bits with no obvious category:
 *
 *   - flash_prog_buf: RAM-backed mirror of the program-memory region.
 *     Replaced by an esp_partition-backed impl in a later phase.
 *   - cmd_framebuffer: direct-command parser that forwards to the
 *     ESP32 VM framebuffer HAL.
 *
 * (timegm + the mmbasic_timegm/mmbasic_gmtime shim wrappers used to
 * live here. Retired in favour of the hal_calendar contract; the
 * canonical impl now lives in drivers/calendar/calendar_bare.c.)
 */

#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"

#include "MMBasic_Includes.h"
#include "hal/hal_time.h"
#include "hal/hal_vm_framebuffer.h"

/* ---- runtime backing storage ---- */

/* MAX_PROG_SIZE for the program + a small "erased flash" tail. Both
 * regions are 0xff-filled to mirror XIP-mapped flash semantics:
 * PrepareProgramExt walks past the program terminator looking for
 * 0xff as "end of program / start of CFunction area", and the
 * CFunction-walk loop expects 0xffffffff-aligned terminator. Stale
 * bytes here corrupt the walk and crash with LoadProhibited. */
#define FLASH_PROG_TRAILER 4096
unsigned char flash_prog_buf[MAX_PROG_SIZE + FLASH_PROG_TRAILER];

__attribute__((constructor)) static void flash_prog_buf_init(void) {
    memset(flash_prog_buf, 0xff, sizeof flash_prog_buf);
}

/* ---- BASIC FRAMEBUFFER direct-command surface ---- */

static char fb_target_from_arg(unsigned char * p) {
    if (checkstring(p, (unsigned char *)"N")) return 'N';
    if (checkstring(p, (unsigned char *)"F")) return 'F';
    if (checkstring(p, (unsigned char *)"L")) return 'L';

    char * q = (char *)getCstring(p);
    if (q[0] != 0 && q[1] == 0) {
        char target = (char)toupper((unsigned char)q[0]);
        if (target == 'N' || target == 'F' || target == 'L') return target;
    }
    error("Syntax");
    return 0;
}

static int fb_mode_from_arg(unsigned char * p) {
    if (checkstring(p, (unsigned char *)"B")) return 1;
    if (checkstring(p, (unsigned char *)"R")) return 2;
    if (checkstring(p, (unsigned char *)"A")) return 3;
    char * q = (char *)getCstring(p);
    if (q[0] != 0 && q[1] == 0) {
        switch (toupper((unsigned char)q[0])) {
        case 'B': return 1;
        case 'R': return 2;
        case 'A': return 3;
        }
    }
    error("Syntax");
    return 0;
}

void cmd_framebuffer(void) {
    unsigned char * p = NULL;
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        int fast = 0;
        unsigned char * q = checkstring(p, (unsigned char *)"FAST");
        if (q) {
            fast = 1;
            checkend(q);
        } else {
            checkend(p);
        }
        hal_vm_framebuffer_create(fast);
    } else if ((p = checkstring(cmdline, (unsigned char *)"LAYER"))) {
        int has_colour = 0;
        int colour = 0;
        if (checkstring(p, (unsigned char *)"TOP")) error("Unsupported FRAMEBUFFER LAYER TOP");
        if (*p) {
            has_colour = 1;
            colour = getinteger(p);
        }
        hal_vm_framebuffer_layer(has_colour, colour);
    } else if ((p = checkstring(cmdline, (unsigned char *)"WRITE"))) {
        hal_vm_framebuffer_write(fb_target_from_arg(p));
    } else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        while (*p == ' ') p++;
        hal_vm_framebuffer_close(*p ? fb_target_from_arg(p) : 'A');
    } else if ((p = checkstring(cmdline, (unsigned char *)"MERGE"))) {
        int has_colour = 0;
        int colour = 0;
        int mode = 0;
        int has_rate = 0;
        int rate_ms = 0;
        getargs(&p, 5, (unsigned char *)",");
        if (argc >= 1 && *argv[0]) {
            has_colour = 1;
            colour = getinteger(argv[0]);
        }
        if (argc >= 3 && *argv[2]) mode = fb_mode_from_arg(argv[2]);
        if (argc == 5 && *argv[4]) {
            has_rate = 1;
            rate_ms = getint(argv[4], 0, 60 * 10 * 1000);
        }
        hal_vm_framebuffer_merge(has_colour, colour, mode, has_rate, rate_ms);
    } else if ((p = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        checkend(p);
        hal_vm_framebuffer_sync();
    } else if ((p = checkstring(cmdline, (unsigned char *)"WAIT"))) {
        checkend(p);
        hal_vm_framebuffer_wait();
    } else if ((p = checkstring(cmdline, (unsigned char *)"COPY"))) {
        int background = 0;
        getargs(&p, 5, (unsigned char *)",");
        if (!(argc == 3 || argc == 5)) error("Syntax");
        if (argc == 5) {
            if (fb_mode_from_arg(argv[4]) != 1) error("Syntax");
            background = 1;
        }
        hal_vm_framebuffer_copy(fb_target_from_arg(argv[0]),
                                fb_target_from_arg(argv[2]),
                                background);
    } else {
        error("Syntax");
    }
}

/* ---- microsecond clock ----
 * External.c's canonical readusclock is gated to non-host builds; on
 * ESP32 we provide our own backed by esp_timer_get_time. Used by
 * bc_vm.c's PAUSE / TIMER paths and by SETTICK accounting. */
uint64_t readusclock(void) {
    return (uint64_t)esp_timer_get_time();
}

/* MMBasic's busy-wait microsecond delay. Short waits busy-spin via
 * esp_rom_delay_us (no FreeRTOS yield); longer waits hand off to the
 * scheduler so the watchdog and USB driver can run. */
void uSec(int us) {
    if (us <= 0) return;
    hal_time_sleep_us((uint32_t)us);
}

/* Pico-SDK Cortex-M0+ "read main stack pointer" intrinsic, used by
 * Memory.c's TestStackOverflow. ESP32 has FreeRTOS task stacks; the
 * MMBasic check doesn't apply, so return ALL-ONES so the comparison
 * always passes. */
uint32_t __get_MSP(void) {
    return 0xFFFFFFFFu;
}

/* Pico-SDK hardware-register window stubs. Core code includes
 * hardware/structs/{dma,watchdog}.h and dereferences these on rp2040
 * paths that gate later via DISPLAY_TYPE / HAL_PORT_HAS_*. The
 * pointers must resolve to valid memory so the load instruction
 * doesn't fault before the gate filters out the path; the contents
 * never matter on this port. */
#include "hardware/structs/dma.h"
#include "hardware/structs/watchdog.h"
static dma_hw_t _dma_hw_store = {0};
static watchdog_hw_t _watchdog_hw_store = {0};
dma_hw_t * dma_hw = &_dma_hw_store;
watchdog_hw_t * watchdog_hw = &_watchdog_hw_store;

/* PSRAMsize / PSRAMbase / hal_psram_* live in hal_psram_esp32.c — the
 * ESP32 HAL implementation reserves a slab via heap_caps_aligned_alloc()
 * and publishes the (base, size) pair to MMBasic at boot. */
