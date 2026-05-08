/*
 * hal/hal_vm_framebuffer.h — VM-side FRAMEBUFFER command surface.
 *
 * The BASIC interpreter's FRAMEBUFFER CREATE / LAYER / WRITE / CLOSE /
 * MERGE / SYNC / WAIT / COPY commands have three equivalents in
 * vm_sys_graphics.c (one per cmd). Each had an MMBASIC_HOST vs
 * PICOMITE fork:
 *
 *   - host → host_framebuffer_* (software simulation in host/host_fb.c)
 *   - PICOMITE SPI-LCD → drives the core1 merge pipeline via
 *     hal_display_merge_* hooks + local scratch buffers
 *   - VGA/HDMI/WEB → hard-error (FRAMEBUFFER isn't meaningful on a
 *     scanout-framebuffer target because the display IS the buffer)
 *
 * This HAL surface hides the three paths behind a single set of
 * entries. Each port supplies its own impl:
 *
 *   host/hal_vm_framebuffer_host.c         — wraps host_framebuffer_*
 *   drivers/vm_framebuffer_picomite/...    — SPI-LCD real impl
 *   drivers/vm_framebuffer_unsupported/... — error stub for VGA/HDMI/WEB
 *
 * The VM syscall layer (vm_sys_graphics.c) just calls these entries;
 * no target-macro gates required.
 */

#ifndef HAL_VM_FRAMEBUFFER_H
#define HAL_VM_FRAMEBUFFER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ClearRuntime reset: tear down any in-progress merge, free layer
 * buffers, restore the physical display panel as the write target. */
void hal_vm_framebuffer_shutdown_runtime(void);

/* Periodic service — called from bc_vm_poll_interrupts. Drives
 * merge-pipeline bookkeeping on SPI-LCD; flushes pending canvas
 * updates on host; no-op on unsupported targets. */
void hal_vm_framebuffer_service(void);

/* FRAMEBUFFER CREATE [FAST] — allocate the off-screen frame buffer.
 * `fast` selects the FASTGFX DMA shadow-buffer path on SPI-LCD. */
void hal_vm_framebuffer_create(int fast);

/* FRAMEBUFFER LAYER [TRANSPARENCY colour] — allocate the layer
 * buffer. `has_colour` = 1 if the caller specified a transparency
 * colour (value in `colour` as 24-bit RGB). */
void hal_vm_framebuffer_layer(int has_colour, int colour);

/* FRAMEBUFFER WRITE which — point the active write target at buffer
 * `which` ('N' = panel, 'F' = frame, 'L' = layer). */
void hal_vm_framebuffer_write(char which);

/* FRAMEBUFFER CLOSE [which] — release buffer `which`
 * (BC_FB_TARGET_DEFAULT = all). */
void hal_vm_framebuffer_close(char which);

/* FRAMEBUFFER MERGE [TRANSPARENCY colour] [mode] [RATE rate_ms] —
 * merge layer into frame with optional transparency + mode +
 * repeating rate. */
void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms);

/* FRAMEBUFFER SYNC — wait for the current background merge to
 * complete and flush any pending async copy. */
void hal_vm_framebuffer_sync(void);

/* FRAMEBUFFER WAIT — wait for vertical blanking / next scanline-0 on
 * panels that support tearing-aware updates. No-op otherwise. */
void hal_vm_framebuffer_wait(void);

/* FRAMEBUFFER COPY from TO to [BACKGROUND col] — copy one buffer to
 * another. */
void hal_vm_framebuffer_copy(char from, char to, int background);

#ifdef __cplusplus
}
#endif

#endif /* HAL_VM_FRAMEBUFFER_H */
