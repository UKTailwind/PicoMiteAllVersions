/*
 * ports/pico_sdk_common/hal_psram_pico.c — hal_psram over the Pico SDK.
 *
 * RP2350 PicoMite variants with QSPI PSRAM use the XIP cache to access
 * the PSRAM-mapped region (`0x11000000`). The four HAL entry points
 * defined here cover everything shared code needs:
 *
 *   - hal_psram_cache_sync()      — clean + invalidate the XIP cache.
 *   - hal_psram_nocache_alias()   — translate to the XIP_NOCACHE region.
 *   - hal_psram_save_settings()   — snapshot QMI controller state.
 *   - hal_psram_restore_settings()— restore QMI controller state.
 *
 * RP2040 has no QSPI PSRAM controller and no XIP_NOCACHE alias; on that
 * chip the save/restore pair are no-ops and the nocache alias returns
 * NULL (which surfaces as "NOCACHE not supported on this port" through
 * the shared `RAM TEST NOCACHE` path).
 */

#include <stddef.h>
#include <stdint.h>

#include "hal/hal_psram.h"

#ifdef rp2350
#include "hardware/address_mapped.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/regs/xip.h"
#include "hardware/xip_cache.h"

static uint32_t m1_timing, m1_rfmt, m1_rcmd, m1_wfmt, m1_wcmd;
static uint32_t m0_timing, m0_rfmt, m0_rcmd;
#endif

void hal_psram_cache_sync(void) {
#ifdef rp2350
    xip_cache_clean_all();
    xip_cache_invalidate_all();
#endif
}

uint8_t * hal_psram_nocache_alias(uint8_t * base) {
#ifdef rp2350
    if (base == NULL) return NULL;
    /* XIP_NOCACHE region is XIP_BASE + 0x04000000; bypasses the cache. */
    return base + 0x04000000u;
#else
    (void)base;
    return NULL;
#endif
}

void hal_psram_save_settings(void) {
#ifdef rp2350
    /* RP2350 PSRAM writes go through the XIP cache. Use the SDK helper:
     * it includes the RP2350-E11 maintenance-window workaround and the
     * barriers needed before flash/QMI state changes or PSRAM restore. */
    xip_cache_clean_all();
    m1_timing = qmi_hw->m[1].timing;
    m1_rfmt = qmi_hw->m[1].rfmt;
    m1_rcmd = qmi_hw->m[1].rcmd;
    m1_wfmt = qmi_hw->m[1].wfmt;
    m1_wcmd = qmi_hw->m[1].wcmd;
    m0_timing = qmi_hw->m[0].timing;
    m0_rfmt = qmi_hw->m[0].rfmt;
    m0_rcmd = qmi_hw->m[0].rcmd;
#endif
}

void hal_psram_restore_settings(void) {
#ifdef rp2350
    qmi_hw->m[1].timing = m1_timing;
    qmi_hw->m[1].rfmt = m1_rfmt;
    qmi_hw->m[1].rcmd = m1_rcmd;
    qmi_hw->m[1].wfmt = m1_wfmt;
    qmi_hw->m[1].wcmd = m1_wcmd;
    qmi_hw->m[0].timing = m0_timing;
    qmi_hw->m[0].rfmt = m0_rfmt;
    qmi_hw->m[0].rcmd = m0_rcmd;
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
#endif
}
