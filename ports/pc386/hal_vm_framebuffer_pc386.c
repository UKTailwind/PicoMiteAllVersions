/*
 * ports/pc386/hal_vm_framebuffer_pc386.c — VM-side FRAMEBUFFER.
 *
 * No graphics surface in stage 3. VGA mode 13h lights up in stage
 * 5. Service + shutdown are the boot-safe no-ops; every actual
 * FRAMEBUFFER command panics until then.
 */

#include "hal/hal_vm_framebuffer.h"
#include "pc386_panic.h"

void hal_vm_framebuffer_shutdown_runtime(void) {}
void hal_vm_framebuffer_service(void) {}

void hal_vm_framebuffer_create(int fast)
{ (void)fast; pc386_panic("FRAMEBUFFER CREATE not available until stage 5 (VGA mode 13h)"); }

void hal_vm_framebuffer_layer(int has_colour, int colour)
{ (void)has_colour; (void)colour; pc386_panic("FRAMEBUFFER LAYER not available until stage 5"); }

void hal_vm_framebuffer_write(char which)
{ (void)which; pc386_panic("FRAMEBUFFER WRITE not available until stage 5"); }

void hal_vm_framebuffer_close(char which)
{ (void)which; }

void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms)
{ (void)has_colour; (void)colour; (void)mode; (void)has_rate; (void)rate_ms;
  pc386_panic("FRAMEBUFFER MERGE not available until stage 5"); }

void hal_vm_framebuffer_sync(void) {}
void hal_vm_framebuffer_wait(void) {}

void hal_vm_framebuffer_copy(char from, char to, int background)
{ (void)from; (void)to; (void)background;
  pc386_panic("FRAMEBUFFER COPY not available until stage 5"); }
