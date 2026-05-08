/*
 * host/hal_vm_framebuffer_host.c — host impl of the VM framebuffer
 * command HAL. Thin wrapper around host/host_fb.c's
 * `host_framebuffer_*` software-simulation surface.
 */

#include <stdint.h>

#include "hal/hal_vm_framebuffer.h"

extern void host_framebuffer_reset_runtime(int colour);
extern void host_framebuffer_shutdown_runtime(void);
extern void host_framebuffer_create(void);
extern void host_framebuffer_layer(int has_colour, int colour);
extern void host_framebuffer_write(char which);
extern void host_framebuffer_close(char which);
extern void host_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms);
extern void host_framebuffer_sync(void);
extern void host_framebuffer_wait(void);
extern void host_framebuffer_copy(char from, char to, int background);
extern void host_framebuffer_service(void);
void hal_vm_framebuffer_shutdown_runtime(void) { host_framebuffer_shutdown_runtime(); }
void hal_vm_framebuffer_service(void)          { host_framebuffer_service(); }
void hal_vm_framebuffer_create(int fast)       { host_framebuffer_create(); (void)fast; }
void hal_vm_framebuffer_layer(int has_colour, int colour) {
    host_framebuffer_layer(has_colour, colour);
}
void hal_vm_framebuffer_write(char which) { host_framebuffer_write(which); }
void hal_vm_framebuffer_close(char which) { host_framebuffer_close(which); }
void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms) {
    host_framebuffer_merge(has_colour, colour, mode, has_rate, rate_ms);
}
void hal_vm_framebuffer_sync(void) { host_framebuffer_sync(); }
void hal_vm_framebuffer_wait(void) { host_framebuffer_wait(); }
void hal_vm_framebuffer_copy(char from, char to, int background) {
    host_framebuffer_copy(from, to, background);
}
