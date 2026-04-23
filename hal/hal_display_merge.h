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

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_MERGE_H */
