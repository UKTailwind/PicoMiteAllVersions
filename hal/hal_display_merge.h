#ifndef HAL_DISPLAY_MERGE_H
#define HAL_DISPLAY_MERGE_H

/* Async layer/framebuffer merge pipeline control.
 *
 * The original PicoMite (SPI-LCD) runs a merge pipeline on core1 that
 * DMAs the layer buffer onto the physical LCD while core0 executes
 * BASIC. The pipeline is controlled via the rp2040 inter-core FIFO. On
 * VGA/HDMI/WEB/host targets the pipeline does not exist — those ports
 * composite via different means (QVGA scanout, HDMI DMA chain, host
 * framebuffer blit).
 *
 * This HAL surface exposes the two operations core code needs to
 * interact with the pipeline without knowing it exists:
 *   - abort:       tear down any in-flight merge and wait for it to
 *                  stop, hard-resetting the MCU if it won't stop.
 *   - check_busy:  throw MMBasic error("Display in use for merged
 *                  operation") if a merge is currently running.
 *
 * The `mergerunning`, `mergedone`, and `mergetimer` globals referenced
 * by both paths live in core/state/display_state.c as unconditional
 * storage so every TU can read them; only the PICOMITE runtime ever
 * actually writes them. */

#ifdef __cplusplus
extern "C" {
#endif

void hal_display_merge_abort(void);
void hal_display_merge_check_busy(void);

/* Take / release the framebuffer mutex guarding the layer buffer
 * against concurrent writes from core1's merge loop. No-op on targets
 * without a merge pipeline (they have no concurrent writer). */
void hal_display_merge_lock_fb(void);
void hal_display_merge_unlock_fb(void);

/* Initialise the merge-pipeline framebuffer mutex at boot. Real on
 * SPI-LCD ports calls mutex_init(&frameBufferMutex); stub no-op
 * elsewhere. */
void hal_display_merge_init_fb_mutex(void);

/* Signal that a full-frame merge has completed and advertise a clean
 * memory-barrier ordering for any waiter spinning on `mergedone`. The
 * spin is in FRAMEBUFFER SYNC (Draw.c::cmd_framebuffer), reading
 * `mergedone` from core0 while core1 sets it via this hook. No-op on
 * targets without a merge pipeline. */
void hal_display_merge_mark_done(void);

/* FRAMEBUFFER CREATE FAST DMA scaffolding. Allocates a shadow buffer +
 * claims a DMA channel used by the FASTGFX merge to diff the layer
 * against the last-drawn frame and DMA only the changed scanlines to
 * the LCD. No-op on targets without the pipeline (FAST silently
 * degrades to the standard merge path on non-PICOMITE). */
void hal_display_fast_dma_alloc(unsigned bytes);
void hal_display_fast_dma_free(void);

/* NEXTGEN (MEM332) shadow-buffer → LCD rectangle push. Posts a rect-
 * refresh message to core1 which DMAs the rect under the frame-buffer
 * mutex. Only PICOMITE rp2350 ever runs the NEXTGEN pipeline; the
 * stub is a no-op everywhere else. Caller must have already checked
 * `Option.DISPLAY_TYPE >= NEXTGEN`. */
void hal_display_nextgen_refresh_rect(int x_lo, int y_lo, int x_hi, int y_hi);

/* NEXTGEN scroll-register reset. Posts (7, 0) to core1 which rewrites
 * the scroll-start register on the ST7796SP / ILI9341BUFF family so
 * CLS resets the visible origin. No-op stub elsewhere. */
void hal_display_nextgen_scroll_reset(void);

/* Return 1 if the target has a running core1 merge pipeline (only
 * PICOMITE variants link the real impl), 0 otherwise. Used by
 * cmd_framebuffer MERGE / COPY to reject the async B/R/A flags on
 * targets without a pipeline with a consistent error message. */
int hal_display_merge_has_pipeline(void);

/* Spin until the most-recent background merge has signalled done, or
 * CheckAbort() trips. Paired with hal_display_merge_mark_done() set by
 * core1 on completion. No-op on stub. */
void hal_display_merge_sync_wait(void);

/* Post a FRAMEBUFFER MERGE B (opaque fill) message: (cmd=2, colour).
 * Real impl fires core1 once. Stub is a no-op (caller guards with
 * hal_display_merge_has_pipeline() first). */
void hal_display_merge_post_fill(unsigned colour);

/* Post a FRAMEBUFFER MERGE R (repeating timed merge) message:
 * (cmd=3, colour, timer_us). */
void hal_display_merge_post_bg(unsigned colour, unsigned timer_us);

/* Post a FRAMEBUFFER COPY B (async buffer copy) message: (cmd=1, src). */
void hal_display_merge_post_copy(const void * src);

/* Post a BLIT WRITE …, B rect-fill-merge message:
 * (cmd=4, x, y, w, h, colour). */
void hal_display_merge_post_blit_fill(int x, int y, int w, int h, unsigned colour);

/* Post a BLIT WRITE …, R timed rect-fill-merge message:
 * (cmd=5, x, y, w, h, colour, timer_us). */
void hal_display_merge_post_blit_bg(int x, int y, int w, int h,
                                    unsigned colour, unsigned timer_us);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_MERGE_H */
