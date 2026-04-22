/*
 * ports/pico_sdk_common/psram_cache.c — PSRAM XIP-cache save/restore.
 *
 * RP2350 PicoMite variants with PSRAM save the QMI controller settings
 * before disabling interrupts (so outstanding PSRAM writes can be
 * committed) and restore them after. On RP2040 (no PSRAM) both functions
 * are no-ops. Called from FileIO.c's disable_interrupts_pico /
 * enable_interrupts_pico.
 *
 * Port-scoped file: permissible to use #ifdef rp2350 internally under
 * the fixup-plan rule "conditional bodies live in HAL impl files or
 * drivers, not in headers that core includes."
 */

#include <stdint.h>

#ifdef rp2350
#include "hardware/structs/qmi.h"
#include "hardware/regs/addressmap.h"

static uint32_t m1_rfmt, m1_timing, m0_rfmt, m0_timing;
#endif

void mmbasic_save_psram_settings(void)
{
#ifdef rp2350
    /* Clean the XIP maintenance window so any dirty writes commit to
     * PSRAM before we touch the cache. */
    uint8_t *maintenance_ptr = (uint8_t *)XIP_MAINTENANCE_BASE;
    for (int i = 1; i < 16 * 1024; i += 8) {
        maintenance_ptr[i] = 0;
    }
    m1_timing = qmi_hw->m[1].timing;
    m1_rfmt   = qmi_hw->m[1].rfmt;
    m0_timing = qmi_hw->m[0].timing;
    m0_rfmt   = qmi_hw->m[0].rfmt;
#endif
}

void mmbasic_restore_psram_settings(void)
{
#ifdef rp2350
    qmi_hw->m[1].timing = m1_timing;
    qmi_hw->m[1].rfmt   = m1_rfmt;
    qmi_hw->m[0].timing = m0_timing;
    qmi_hw->m[0].rfmt   = m0_rfmt;
#endif
}
