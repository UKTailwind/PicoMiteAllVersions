/*
 * hal_vm_framebuffer_esp32_stub.c — Phase B stub for
 * hal/hal_vm_framebuffer.h. All FRAMEBUFFER commands are no-ops on
 * a port without a display.
 */

#include "hal/hal_vm_framebuffer.h"

void hal_vm_framebuffer_shutdown_runtime(void) {}
void hal_vm_framebuffer_service(void) {}
void hal_vm_framebuffer_create(int fast) { (void)fast; }
void hal_vm_framebuffer_layer(int hc, int c) { (void)hc; (void)c; }
void hal_vm_framebuffer_write(char w) { (void)w; }
void hal_vm_framebuffer_close(char w) { (void)w; }
void hal_vm_framebuffer_merge(int hc, int c, int m, int hr, int rms) {
    (void)hc; (void)c; (void)m; (void)hr; (void)rms;
}
void hal_vm_framebuffer_sync(void) {}
void hal_vm_framebuffer_wait(void) {}
void hal_vm_framebuffer_copy(char from, char to, int bg) { (void)from; (void)to; (void)bg; }

/* SPI-LCD.h declares Display_Refresh() — vm_sys_graphics.c and FileIO.c
 * call it whenever Option.Refresh == 1 to push a CPU-side framebuffer
 * out to the LCD. Stdio scope has no display; no-op. */
void Display_Refresh(void) {}

/* Display-related globals + functions referenced by core code that
 * doesn't gate on a display being configured. Stdio scope keeps
 * DISPLAY_TYPE = 0 (no display) so the gated paths skip; the function
 * stubs cover the few calls that fire unconditionally. */
volatile int DISPLAY_TYPE = 0;
void ScrollLCDSPISCR(int lines) { (void)lines; }
void setterminal(int height, int width) { (void)height; (void)width; }

/* PicoCalc 320×320-screen flag, dispatcher state, and direct-buffer
 * primitives. All no-op / zero on a port without a framebuffer. */
bool screen320 = 0;
void DisplayNotSet(void) {}
void setframebuffer(void) {}
void closeframebuffer(char layer) { (void)layer; }
#include <stdint.h>
void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    (void)x0; (void)y0; (void)w; (void)h; (void)colour;
}
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd) {
    (void)s; (void)xstart; (void)xend; (void)ystart; (void)yend; (void)odd;
}
