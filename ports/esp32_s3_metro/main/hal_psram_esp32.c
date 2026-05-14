/*
 * ports/esp32_s3_metro/main/hal_psram_esp32.c — ESP32-S3 PSRAM HAL.
 *
 * Owns MMBasic's PSRAMsize / PSRAMbase globals on this port. At boot,
 * hal_psram_init():
 *
 *   1. Calls esp_psram_init() to bring up the ESP-IDF SPIRAM driver.
 *   2. Reserves a fixed slab (HAL_PORT_PSRAM_SLAB_BYTES) via
 *      heap_caps_aligned_alloc(PAGESIZE, slab, MALLOC_CAP_SPIRAM |
 *      MALLOC_CAP_8BIT). PAGESIZE alignment matches the bitmap allocator
 *      page granularity in drivers/psram_heap/psram_heap_real.c.
 *   3. Publishes the slab pointer and size to PSRAMbase / PSRAMsize so
 *      Memory.c routing, the `RAM` command, and the bitmap allocator
 *      all see the same region.
 *
 * Failure modes are non-fatal: if esp_psram_init() or
 * heap_caps_aligned_alloc() fail, PSRAMbase / PSRAMsize stay 0 and the
 * `if (PSRAMsize)` guards in core code keep PSRAM paths dormant. A
 * one-line warning goes to the console so the operator can investigate.
 *
 * The other HAL hooks (cache_sync / nocache_alias / save_settings /
 * restore_settings) are wired against ESP-IDF expectations:
 *
 *   - cache_sync: clean + invalidate via esp_cache_msync over the slab.
 *   - nocache_alias: returns NULL — ESP32-S3 has no cache-bypass alias
 *     for PSRAM; the shared `RAM TEST NOCACHE` path translates NULL
 *     into the BASIC-visible "NOCACHE not supported on this port" error.
 *   - save/restore_settings: no-ops; ESP-IDF manages cache coherency
 *     around flash erase/program internally.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_psram.h"

#include "esp_err.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"

/* Strong definitions of the runtime PSRAM globals on this port.
 * PicoMite.c provides them on Pico builds; the ESP32 port has its own
 * non-PicoMite.c TU layout, so the responsibility lives here. */
uint32_t PSRAMsize = 0;
uintptr_t PSRAMbase = 0;

/* Slab handle retained for the lifetime of the program; freed only at
 * reset. cache_sync uses these to scope esp_cache_msync to exactly the
 * slab — no point invalidating ESP-IDF-owned SPIRAM regions. */
static void *s_psram_slab = NULL;
static size_t s_psram_slab_bytes = 0;

void hal_psram_init(void)
{
    if (s_psram_slab != NULL) return;  /* idempotent */

    /* CONFIG_SPIRAM_BOOT_INIT=y in sdkconfig brings up PSRAM during the
     * ESP-IDF bootloader, so we only call esp_psram_init() if it hasn't
     * happened yet. Calling it twice returns ESP_ERR_INVALID_STATE. */
    if (!esp_psram_is_initialized()) {
        esp_err_t err = esp_psram_init();
        if (err != ESP_OK) {
            printf("hal_psram_init: esp_psram_init failed (%d); "
                   "PSRAM disabled\n", (int)err);
            return;
        }
    }

    /*
     * The shared formula PSRAMblock = PSRAMbase + PSRAMsize + 0x60000
     * places the numbered-slot region 0x60000 bytes past the heap
     * region's end. Allocate a physical slab that covers the heap, the
     * 0x60000 gap, and PSRAMblocksize bytes for the slot region — but
     * publish only HAL_PORT_PSRAM_SLAB_BYTES as PSRAMsize so the bitmap
     * allocator in drivers/psram_heap/psram_heap_real.c and Memory.c's
     * routing stay confined to the heap portion. The slot region lives
     * in the slab tail and is addressed linearly by cmd_psram via
     * PSRAMblock.
     */
    const size_t heap_bytes  = (size_t)HAL_PORT_PSRAM_SLAB_BYTES;
    const size_t slot_bytes  = (size_t)HAL_PORT_PSRAM_BLOCK_SIZE;
    const size_t slab_bytes  = heap_bytes + 0x60000u + slot_bytes;
    void *slab = heap_caps_aligned_alloc(PAGESIZE, slab_bytes,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!slab) {
        printf("hal_psram_init: heap_caps_aligned_alloc(%u) failed; "
               "PSRAM disabled\n", (unsigned)slab_bytes);
        return;
    }

    s_psram_slab = slab;
    s_psram_slab_bytes = slab_bytes;
    PSRAMbase = (uintptr_t)slab;
    PSRAMsize = (uint32_t)heap_bytes;
    printf("hal_psram_init: slab=%p slab_bytes=%u heap=%u slot=%u "
           "PSRAMbase=0x%08lx PSRAMsize=%u\n",
           slab, (unsigned)slab_bytes,
           (unsigned)heap_bytes, (unsigned)slot_bytes,
           (unsigned long)PSRAMbase, (unsigned)PSRAMsize);
}

void hal_psram_cache_sync(void)
{
    if (!s_psram_slab) return;
    (void)esp_cache_msync(s_psram_slab, s_psram_slab_bytes,
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                          ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

uint8_t *hal_psram_nocache_alias(uint8_t *base)
{
    (void)base;
    return NULL;  /* ESP32-S3 has no cache-bypass alias for PSRAM. */
}

void hal_psram_save_settings(void)    {}
void hal_psram_restore_settings(void) {}
