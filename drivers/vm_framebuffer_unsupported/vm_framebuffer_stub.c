/*
 * drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c —
 * FRAMEBUFFER command surface is unsupported on scanout ports
 * (PICOMITEVGA + PICOMITEWEB). The BASIC FRAMEBUFFER commands are
 * SPI-LCD specific — the off-screen frame+layer buffers sit
 * alongside the physical panel; on VGA/HDMI/WEB the scanout IS the
 * buffer and there's no meaningful "merge" or "swap". Each entry
 * errors when actually invoked; the runtime/service/shutdown ones
 * are silent no-ops so the VM's per-tick service call stays cheap.
 *
 * Pixel readback is a separate concern handled by drivers/
 * display_pixel_readbuffer/, not part of this surface.
 */

#include <stdint.h>
#include "MMBasic_Includes.h"
#include "hal/hal_vm_framebuffer.h"

static void vm_fb_err(const char *msg) {
    error((char *)msg);
}

void hal_vm_framebuffer_shutdown_runtime(void) { }
void hal_vm_framebuffer_service(void) { }

void hal_vm_framebuffer_create(int fast) { (void)fast; vm_fb_err("FRAMEBUFFER not supported"); }
void hal_vm_framebuffer_layer(int has_colour, int colour) {
    (void)has_colour; (void)colour; vm_fb_err("FRAMEBUFFER not supported");
}
void hal_vm_framebuffer_write(char which) { (void)which; vm_fb_err("FRAMEBUFFER not supported"); }
void hal_vm_framebuffer_close(char which) { (void)which; vm_fb_err("FRAMEBUFFER not supported"); }
void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms) {
    (void)has_colour; (void)colour; (void)mode; (void)has_rate; (void)rate_ms;
    vm_fb_err("FRAMEBUFFER not supported");
}
void hal_vm_framebuffer_sync(void) { vm_fb_err("FRAMEBUFFER not supported"); }
void hal_vm_framebuffer_wait(void) { vm_fb_err("FRAMEBUFFER not supported"); }
void hal_vm_framebuffer_copy(char from, char to, int background) {
    (void)from; (void)to; (void)background;
    vm_fb_err("FRAMEBUFFER not supported");
}
