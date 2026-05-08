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
