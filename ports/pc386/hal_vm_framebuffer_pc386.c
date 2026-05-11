/*
 * ports/pc386/hal_vm_framebuffer_pc386.c — VM-side FRAMEBUFFER.
 *
 * No graphics surface in stage 3. VGA mode 13h lights up in stage 5.
 * Until then, every FRAMEBUFFER command returns to the prompt with
 * an error rather than halting the kernel.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_vm_framebuffer.h"

void hal_vm_framebuffer_shutdown_runtime(void) {}
void hal_vm_framebuffer_service(void) {}

void hal_vm_framebuffer_create(int fast)
{ (void)fast; error("FRAMEBUFFER not available until stage 5 (VGA mode 13h)"); }

void hal_vm_framebuffer_layer(int has_colour, int colour)
{ (void)has_colour; (void)colour; error("FRAMEBUFFER not available until stage 5"); }

void hal_vm_framebuffer_write(char which)
{ (void)which; error("FRAMEBUFFER not available until stage 5"); }

void hal_vm_framebuffer_close(char which)
{ (void)which; }

void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms)
{ (void)has_colour; (void)colour; (void)mode; (void)has_rate; (void)rate_ms;
  error("FRAMEBUFFER not available until stage 5"); }

void hal_vm_framebuffer_sync(void) {}
void hal_vm_framebuffer_wait(void) {}

void hal_vm_framebuffer_copy(char from, char to, int background)
{ (void)from; (void)to; (void)background;
  error("FRAMEBUFFER not available until stage 5"); }
