/*
 * drivers/display_merge/display_merge_pico.c — real merge-pipeline
 * control for the PicoMite (SPI-LCD) device family.
 *
 * Linked only into PICOMITE variants (rp2040 + rp2350). The pipeline
 * is driven through the rp2040 inter-core FIFO; core1 runs the merge
 * loop in PicoMite.c::UpdateCore. See hal/hal_display_merge.h for the
 * contract.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"
#include "pico/multicore.h"

extern bool mergerunning;
extern uint32_t mergetimer;
extern uint32_t _excep_code;

void hal_display_merge_abort(void) {
    if (!mergerunning) return;
    multicore_fifo_push_blocking(0xFF);
    busy_wait_ms(mergetimer + 200);
    if (mergerunning) {
        /* Core1 failed to ack the stop within mergetimer+200 ms. Hard
         * reset — the BASIC program is about to run with a half-dead
         * display pipeline otherwise. Matches the legacy inline
         * pattern that lived in Draw.c before the HAL extraction. */
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
}

void hal_display_merge_check_busy(void) {
    if (mergerunning) error("Display in use for merged operation");
}
