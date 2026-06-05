/*
 * drivers/spi_lcd/spi_lcd_fastgfx_stub.c — FASTGFX command stubs for
 * device targets without an SPI-LCD (VGA / HDMI / WEB).
 *
 * The bytecode VM (`bc_fastgfx_*` in bc_vm.c / bc_runtime.c) and the
 * BASIC dialect (`cmd_fastgfx`) need these symbols to link on every
 * device target. On builds without an SPI LCD there's nothing to swap
 * — the stubs throw MMBasic errors except close/reset which silently
 * no-op (so cleanup paths don't barf).
 *
 * Host has its own richer FASTGFX simulator in ports/host_native/host_fastgfx.c; it
 * does not link this file. `merge_optimized` is PICOMITE-only (runs
 * under the FASTGFX DMA path); the non-PICOMITE callers never reach it
 * because `ShadowBuf` stays NULL on those builds.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void cmd_fastgfx(void) {
    error("FASTGFX not supported on this platform");
}
void bc_fastgfx_create(void) {
    error("FASTGFX not supported on this platform");
}
void bc_fastgfx_swap(void) {
    error("FASTGFX not supported on this platform");
}
void bc_fastgfx_sync(void) {
    error("FASTGFX not supported on this platform");
}
void bc_fastgfx_close(void) {}
void bc_fastgfx_reset(void) {}
void bc_fastgfx_set_fps(int fps) {
    (void)fps;
    error("FASTGFX not supported on this platform");
}

void merge_optimized(uint8_t colour) {
    (void)colour;
}
