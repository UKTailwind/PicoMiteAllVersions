/*
 * drivers/vga_pio/vga_memory.c — VGA-specific framebuffer + tile-mode
 * state storage.
 *
 * Linked only on PICOMITEVGA variants (VGA + VGAUSB + VGARP2350 +
 * VGAUSBRP2350 + HDMI + HDMIUSB). Relocated out of Memory.c's
 * `#ifdef PICOMITEVGA` block so the core allocator file stays HAL-clean.
 *
 * Internal target-macro ifdefs are permitted here — this is a driver
 * impl file and the HAL standard only applies to core files.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"

/* Unified scanout framebuffer lives at the end of Memory.c's
 * AllMemory[] slab — see `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES` in the
 * port's port_config.h for the trailer size. The real FRAMEBUFFER
 * pointer + framebuffersize live here (VGA real impl); non-VGA ports
 * get the NULL/0 stubs in vga_ops_stub.c. */
extern unsigned char AllMemory[];
unsigned char *FRAMEBUFFER    = AllMemory + HEAP_MEMORY_SIZE + 256;
uint32_t       framebuffersize = HAL_PORT_FRAMEBUFFER_TRAILER_BYTES;

/* -----------------------------------------------------------------
 * Tile-colour tables (VGA foreground/background per tile).
 *
 * rp2350 VGA allocates these inside the framebuffer trailer at
 * runtime; rp2040 VGA uses a fixed 80×40 tile grid stored in BSS.
 * HDMI (rp2350) uses the rp2350 pointer flavour plus 8-bit wide
 * companion arrays.
 * ----------------------------------------------------------------- */
#ifdef rp2350
uint16_t *tilefcols;
uint16_t *tilebcols;
#else
uint16_t __attribute__ ((aligned (256))) tilefcols[80*40];
uint16_t __attribute__ ((aligned (256))) tilebcols[80*40];
#endif

#if HAL_PORT_HAS_HDMI
uint8_t *tilefcols_w;
uint8_t *tilebcols_w;
uint16_t HDMIlines[2][848] = {0};
volatile int X_TILE = 80, Y_TILE = 40;
volatile int ytileheight = 480/12;
#else
/* Non-HDMI QVGA colour-remap tables (16-entry RGB121 palettes). */
uint16_t M_Foreground[16] = {
    0x0000,0x000F,0x00f0,0x00ff,0x0f00,0x0f0F,0x0ff0,0x0fff,
    0xf000,0xf00F,0xf0f0,0xf0ff,0xff00,0xff0F,0xfff0,0xffff
};
uint16_t M_Background[16] = {
    0xffff,0xfff0,0xff0f,0xff00,0xf0ff,0xf0f0,0xf00f,0xf000,
    0x0fff,0x0ff0,0x0f0f,0x0f00,0x00ff,0x00f0,0x000f,0x0000
};
volatile int ytileheight = 16;
#endif

/* -----------------------------------------------------------------
 * Framebuffer-plane pointers (WriteBuf / DisplayBuf / LayerBuf / ...).
 *
 * All six point at the start of the VGA framebuffer at boot; cmd_
 * framebuffer / cmd_blit retarget them as the user configures
 * additional layers. The framebuffer always lives in AllMemory's
 * trailer region now that rp2040 VGA also uses the unified layout
 * (port_config.h's HAL_PORT_FRAMEBUFFER_TRAILER_BYTES).
 * ----------------------------------------------------------------- */
unsigned char *WriteBuf    = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *DisplayBuf  = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *LayerBuf    = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *FrameBuf    = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *SecondLayer = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *SecondFrame = AllMemory + HEAP_MEMORY_SIZE + 256;

/* ShadowBuf / fb_dma_chan are FASTGFX + DMA state owned by the
 * SPI-LCD display_merge driver; VGA doesn't use them but vm_sys_
 * graphics.c / display_merge_stub.c reference them unconditionally,
 * so define them here as the VGA-side no-op storage. */
unsigned char *ShadowBuf   = NULL;
int            fb_dma_chan = -1;

/* Called from Memory.c::InitHeap(all=true) to rebind the plane
 * pointers back to the head of the VGA framebuffer after a runtime
 * reset. No-op stub on non-VGA targets. */
extern unsigned char *FRAMEBUFFER;
void vga_memory_init_planes(void) {
    WriteBuf   = (unsigned char *)FRAMEBUFFER;
    DisplayBuf = (unsigned char *)FRAMEBUFFER;
    LayerBuf   = (unsigned char *)FRAMEBUFFER;
    FrameBuf   = (unsigned char *)FRAMEBUFFER;
}
