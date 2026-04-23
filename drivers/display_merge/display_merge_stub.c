/*
 * drivers/display_merge/display_merge_stub.c — no-op merge-pipeline
 * control for targets without a core1 merge loop.
 *
 * Linked into VGA, HDMI, WEB, and host builds. The core-side globals
 * `mergerunning` and `mergetimer` (core/state/display_state.c) are
 * read on these targets but never driven true by a pipeline, so both
 * hooks are trivially safe. See hal/hal_display_merge.h.
 */

#include "hal/hal_display_merge.h"

void hal_display_merge_abort(void) { }
void hal_display_merge_check_busy(void) { }
void hal_display_merge_lock_fb(void) { }
void hal_display_merge_unlock_fb(void) { }
void hal_display_merge_mark_done(void) { }
void hal_display_fast_dma_alloc(unsigned bytes) { (void)bytes; }
void hal_display_fast_dma_free(void) { }
