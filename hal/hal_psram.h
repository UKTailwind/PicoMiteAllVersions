/*
 * hal/hal_psram.h — external PSRAM cache + flash-batch coordination HAL.
 *
 * Surface that shared `RAM` command code and the flash write-batch hooks
 * use to coordinate with the port's external-PSRAM cache. Two distinct
 * concerns share this header because both are owned by the port's PSRAM
 * controller:
 *
 *   1. Cache sync — clean + invalidate the PSRAM-side cache (RP2350 XIP
 *      cache; ESP32 internal cache). Needed when shared command-level
 *      code writes through a cached alias and then immediately reads
 *      the bytes back, or wants to be certain the writes have hit the
 *      backing store.
 *
 *   2. Nocache alias — return a pointer that bypasses the cache (RP2350
 *      XIP_NOCACHE region at `base + 0x04000000`). Ports without a
 *      nocache-aliased view return NULL; the shared `RAM TEST NOCACHE`
 *      path translates that into the BASIC-visible "NOCACHE not
 *      supported on this port" error.
 *
 *   3. Save/restore settings — flash erase/program on RP2350 mutates the
 *      QMI controller's M[0] timing/format registers, which would
 *      corrupt subsequent PSRAM access. Ports save the QMI state before
 *      the batch and restore after. ESP-IDF manages this internally, so
 *      ESP32 stubs are no-ops.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md):
 *   caller owns all buffers; HAL impl never calls MMBasic's error().
 */

#ifndef HAL_PSRAM_H
#define HAL_PSRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Acquire the PSRAM region from the underlying platform and publish its
 * base + size to MMBasic via the runtime globals `PSRAMbase` and
 * `PSRAMsize` (declared in Hardware_Includes.h). After this returns
 * non-zero, the shared `RAM` command, the bitmap allocator in
 * drivers/psram_heap, and Memory.c's PSRAM routing all see the same
 * (base, size) pair.
 *
 *   - RP2350 (Pico): runs the QSPI detect sequence, then sets PSRAMbase
 *     to the XIP cache region (HAL_PORT_PSRAM_BASE = 0x11000000) and
 *     PSRAMsize to the detected slot-trimmed value.
 *   - ESP32-S3: calls esp_psram_init() then heap_caps_aligned_alloc()
 *     for a fixed slab; publishes the returned slab pointer + size.
 *   - Stub ports (host, host_wasm, mmbasic_stdio, RP2040, etc.): leave
 *     PSRAMbase and PSRAMsize at 0; `if (!PSRAMsize)` runtime guards
 *     short-circuit every PSRAM code path.
 *
 * Idempotent; safe to call from a fresh boot. Must run before any code
 * that reads PSRAMsize or dereferences PSRAMbase. */
void hal_psram_init(void);

/* Clean + invalidate the PSRAM-side cache. Ensures cached writes have
 * landed in backing store and cached reads will refetch on next access.
 * On ports with no external cache between CPU and PSRAM, this is a
 * no-op. */
void hal_psram_cache_sync(void);

/* Return a CPU-addressable pointer that accesses PSRAM bypassing the
 * cache. Used by `RAM TEST NOCACHE` to drive backing-store traffic
 * directly. Returns NULL if the port has no nocache-aliased view
 * (ESP32-S3, etc.); callers translate NULL into the BASIC-visible
 * "NOCACHE not supported on this port" error. */
uint8_t * hal_psram_nocache_alias(uint8_t * base);

/* Save / restore the PSRAM controller's state across an operation that
 * may clobber it (typically a flash erase/program batch on RP2350).
 * Paired calls bracket the unsafe region; nested calls must be balanced
 * by the caller. Both are no-ops on ports where the flash controller is
 * independent of the PSRAM controller (ESP32, host). */
void hal_psram_save_settings(void);
void hal_psram_restore_settings(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PSRAM_H */
