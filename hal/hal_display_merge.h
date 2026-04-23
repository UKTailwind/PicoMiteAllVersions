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

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_MERGE_H */
