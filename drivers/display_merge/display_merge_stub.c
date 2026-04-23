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
void hal_display_nextgen_refresh_rect(int x_lo, int y_lo, int x_hi, int y_hi) {
    (void)x_lo; (void)y_lo; (void)x_hi; (void)y_hi;
}
void hal_display_nextgen_scroll_reset(void) { }
int  hal_display_merge_has_pipeline(void) { return 0; }
void hal_display_merge_sync_wait(void) { }
void hal_display_merge_post_fill(unsigned colour) { (void)colour; }
void hal_display_merge_post_bg(unsigned colour, unsigned timer_us) {
    (void)colour; (void)timer_us;
}
void hal_display_merge_post_copy(const void *src) { (void)src; }
void hal_display_merge_post_blit_fill(int x, int y, int w, int h, unsigned colour) {
    (void)x; (void)y; (void)w; (void)h; (void)colour;
}
void hal_display_merge_post_blit_bg(int x, int y, int w, int h,
                                    unsigned colour, unsigned timer_us) {
    (void)x; (void)y; (void)w; (void)h; (void)colour; (void)timer_us;
}
