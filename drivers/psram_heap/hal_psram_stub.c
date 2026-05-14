/*
 * drivers/psram_heap/hal_psram_stub.c — no-op hal_psram for ports that
 * link the shared cmd_psram body but have no external PSRAM controller
 * to manage.
 *
 * Used by host (no PSRAM), and by any future port that wants the shared
 * `RAM` command surface without a hardware PSRAM cache. The runtime
 * guard at the top of `cmd_psram` (`if (!PSRAMsize) error("PSRAM not
 * enabled");`) keeps the test/march code from actually running on these
 * builds, so the no-op semantics are safe.
 *
 * Ports with a real PSRAM controller (RP2350 via pico_sdk_common,
 * ESP32-S3 via its main component) provide their own hal_psram_*
 * impl and exclude this stub from their source list.
 */

#include <stddef.h>
#include <stdint.h>

#include "hal/hal_psram.h"

void hal_psram_init(void) {}
void hal_psram_cache_sync(void) {}

uint8_t *hal_psram_nocache_alias(uint8_t *base)
{
    (void)base;
    return NULL;
}

void hal_psram_save_settings(void) {}
void hal_psram_restore_settings(void) {}
