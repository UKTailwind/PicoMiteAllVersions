/*
 * ports/pico_sdk_common/psram_cache.c — PSRAM XIP-cache save/restore.
 *
 * RP2350 PicoMite variants with PSRAM save the QMI controller settings
 * before disabling interrupts (so outstanding PSRAM writes can be
 * committed) and restore them after. On RP2040 (no PSRAM) both functions
 * are no-ops. Called from hal_flash_pico.c's write-batch hooks.
 *
 * Port-scoped file: permissible to use #ifdef rp2350 internally under
 * the fixup-plan rule "conditional bodies live in HAL impl files or
 * drivers, not in headers that core includes."
 */

#include <stdint.h>

#ifdef rp2350
#include "hardware/address_mapped.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/regs/xip.h"
#include "hardware/xip_cache.h"

static uint32_t m1_timing, m1_rfmt, m1_rcmd, m1_wfmt, m1_wcmd;
static uint32_t m0_timing, m0_rfmt, m0_rcmd;
#endif

void mmbasic_save_psram_settings(void)
{
#ifdef rp2350
    /* RP2350 PSRAM writes go through the XIP cache. Use the SDK helper:
     * it includes the RP2350-E11 maintenance-window workaround and the
     * barriers needed before flash/QMI state changes or PSRAM restore. */
    xip_cache_clean_all();
    m1_timing = qmi_hw->m[1].timing;
    m1_rfmt   = qmi_hw->m[1].rfmt;
    m1_rcmd   = qmi_hw->m[1].rcmd;
    m1_wfmt   = qmi_hw->m[1].wfmt;
    m1_wcmd   = qmi_hw->m[1].wcmd;
    m0_timing = qmi_hw->m[0].timing;
    m0_rfmt   = qmi_hw->m[0].rfmt;
    m0_rcmd   = qmi_hw->m[0].rcmd;
#endif
}

void mmbasic_restore_psram_settings(void)
{
#ifdef rp2350
    qmi_hw->m[1].timing = m1_timing;
    qmi_hw->m[1].rfmt   = m1_rfmt;
    qmi_hw->m[1].rcmd   = m1_rcmd;
    qmi_hw->m[1].wfmt   = m1_wfmt;
    qmi_hw->m[1].wcmd   = m1_wcmd;
    qmi_hw->m[0].timing = m0_timing;
    qmi_hw->m[0].rfmt   = m0_rfmt;
    qmi_hw->m[0].rcmd   = m0_rcmd;
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
#endif
}
