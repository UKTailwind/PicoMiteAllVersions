#ifndef __VM_SYS_GRAPHICS_H
#define __VM_SYS_GRAPHICS_H

#include "shared/gfx/gfx_box_shared.h"
#include "shared/gfx/gfx_circle_shared.h"
#include "shared/gfx/gfx_cls_shared.h"
#include "shared/gfx/gfx_line_shared.h"
#include "shared/gfx/gfx_pixel_shared.h"
#include "shared/gfx/gfx_text_shared.h"

void vm_sys_graphics_reset(void);
void vm_sys_graphics_service(void);

void vm_sys_graphics_box_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                 const GfxBoxErrorSink *errors);
void vm_sys_graphics_rbox_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                  const GfxBoxErrorSink *errors);
void vm_sys_graphics_arc_execute(int x, int y, int r1, int has_r2, int r2,
                                 MMFLOAT a1, MMFLOAT a2, int has_c, int c);
void vm_sys_graphics_triangle_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                      const GfxBoxErrorSink *errors);
void vm_sys_graphics_polygon_execute(const GfxBoxIntArg *args, int field_count,
                                     const GfxBoxErrorSink *errors);

void vm_sys_graphics_cls_execute(int has_arg, const GfxClsArg *arg, const GfxClsOps *ops);

void vm_sys_graphics_circle_execute(GfxCircleMode mode, const GfxCircleArg *args, int field_count,
                                    const GfxCircleErrorSink *errors);

void vm_sys_graphics_line_execute(GfxLineMode mode, const GfxLineArg *args, int field_count,
                                  const GfxLineErrorSink *errors);

void vm_sys_graphics_pixel_execute(GfxPixelMode mode, const GfxPixelArg *args, int field_count,
                                   const GfxPixelErrorSink *errors);
int vm_sys_graphics_read_pixel(int x, int y);

void vm_sys_graphics_text_execute(const GfxTextArg *args, int field_count, const GfxTextOps *ops);
void vm_sys_graphics_framebuffer_create(int fast);
void vm_sys_graphics_framebuffer_layer(int has_colour, int colour);
void vm_sys_graphics_framebuffer_write(char which);
void vm_sys_graphics_framebuffer_close(char which);
void vm_sys_graphics_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms);
void vm_sys_graphics_framebuffer_sync(void);
void vm_sys_graphics_framebuffer_wait(void);
void vm_sys_graphics_framebuffer_copy(char from, char to, int background);

#endif
