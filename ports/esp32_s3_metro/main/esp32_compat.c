/*
 * esp32_compat.c — small porting bits with no obvious category:
 *
 *   - flash_prog_buf: RAM-backed mirror of the program-memory region.
 *     Replaced by an esp_partition-backed impl in a later phase.
 *   - cmd_framebuffer: error stub for a BASIC command that needs a
 *     framebuffer this port doesn't have.
 *
 * (timegm + the mmbasic_timegm/mmbasic_gmtime shim wrappers used to
 * live here. Retired in favour of the hal_calendar contract; the
 * canonical impl now lives in drivers/calendar/calendar_bare.c.)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"

#include "MMBasic_Includes.h"
#include "hal/hal_time.h"

/* ---- runtime backing storage ---- */

/* MAX_PROG_SIZE for the program + a small "erased flash" tail. Both
 * regions are 0xff-filled to mirror XIP-mapped flash semantics:
 * PrepareProgramExt walks past the program terminator looking for
 * 0xff as "end of program / start of CFunction area", and the
 * CFunction-walk loop expects 0xffffffff-aligned terminator. Stale
 * bytes here corrupt the walk and crash with LoadProhibited. */
#define FLASH_PROG_TRAILER  4096
unsigned char flash_prog_buf[MAX_PROG_SIZE + FLASH_PROG_TRAILER];

__attribute__((constructor))
static void flash_prog_buf_init(void) {
    memset(flash_prog_buf, 0xff, sizeof flash_prog_buf);
}

/* ---- BASIC commands that require a framebuffer this port doesn't have ---- */

void cmd_framebuffer(void) { error("FRAMEBUFFER not supported on this port"); }

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
uint32_t __get_MSP(void) { return 0xFFFFFFFFu; }

/* Pico-SDK hardware-register window stubs. Core code includes
 * hardware/structs/{dma,watchdog}.h and dereferences these on rp2040
 * paths that gate later via DISPLAY_TYPE / HAL_PORT_HAS_*. The
 * pointers must resolve to valid memory so the load instruction
 * doesn't fault before the gate filters out the path; the contents
 * never matter on this port. */
#include "hardware/structs/dma.h"
#include "hardware/structs/watchdog.h"
static dma_hw_t      _dma_hw_store     = {0};
static watchdog_hw_t _watchdog_hw_store = {0};
dma_hw_t      *dma_hw      = &_dma_hw_store;
watchdog_hw_t *watchdog_hw = &_watchdog_hw_store;

/* PSRAMsize / PSRAMbase / hal_psram_* live in hal_psram_esp32.c — the
 * ESP32 HAL implementation reserves a slab via heap_caps_aligned_alloc()
 * and publishes the (base, size) pair to MMBasic at boot. */
