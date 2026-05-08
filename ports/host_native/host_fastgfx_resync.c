/*
 * host_fastgfx_resync.c — native resync hook (no-op).
 *
 * Native host's host_sleep_us is wall-clock accurate, so the next-sync
 * deadline never drifts. Pure pass-through.
 */

#include <stdint.h>

uint64_t host_fastgfx_resync_after_sleep(uint64_t next_sync_us, uint64_t frame_us) {
    (void)frame_us;
    return next_sync_us;
}
