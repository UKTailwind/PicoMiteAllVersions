/*
 * RGB121.c — RGB121 (4 bpp / 16 colour, 1:2:1 R:G:B) blit primitives.
 *
 * Ported (paraphrased) from upstream UKTailwind/PicoMiteAllVersions
 * RGB121.c (@04f81d0). Adapted to write through our DrawPixel function
 * pointer instead of upstream's nibble-aligned memcpy fast path:
 * upstream's destination is always the PicoMiteVGA SCREENMODE2/3
 * framebuffer (also 4 bpp, so memcpy works); our targets (PicoCalc LCD
 * RGB565, host uint32_t framebuffer, wasm uint32_t framebuffer) are all
 * 16/32 bpp, so we expand each source nibble through RGB121map[] and
 * call DrawPixel per pixel.
 *
 * Performance: ~10-30× slower than upstream's memcpy path for full-
 * screen blits, fine for the tile-atlas use case (tilemaps + sprites
 * are small per-frame draws and the current PicoMite already paints
 * through DrawPixel for everything else). Optimise later if profiling
 * the TILEMAP demos shows it matters.
 *
 * The 'destination' argument is accepted for API compatibility with
 * upstream's signature (so cmd_tilemap / sprite_cmd_draw port over
 * unchanged) but is ignored — writes always go through DrawPixel into
 * the current WriteBuf. cmd_tilemap's L/F/N/T destination-letter parse
 * happens by swapping WriteBuf temporarily before calling blit121.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"

/* HResS/VResS = source image dimensions; HResD/VResD = destination
 * (framebuffer) dimensions. Set by the caller before invoking blit121.
 * Match upstream so cmd_tilemap's existing assignment lines port. */
int HResD = 0;
int VResD = 0;
int HResS = 0;
int VResS = 0;

/* Standard 16-colour PicoMite palette, byte-identical to the table
 * LoadOptions installs into the global RGB121map[]. Used directly here
 * so blit121 doesn't depend on LoadOptions having already been called
 * — on host the global RGB121map is zero until the first error fires
 * LoadOptions, which used to leave TILEMAP DRAW painting black. */
static const int rgb121_palette[16] = {
    BLACK, BLUE,    MYRTLE,  COBALT,
    MIDGREEN, CERULEAN, GREEN,  CYAN,
    RED, MAGENTA, RUST,    FUCHSIA,
    BROWN, LILAC,   YELLOW,  WHITE
};

void blit121(uint8_t *source, uint8_t *destination,
             int xsource, int ysource, int width, int height,
             int xdestination, int ydestination, int missingcolour)
{
    (void)destination;  /* see header comment — DrawPixel selects plane */

    int has_transparency = (missingcolour >= 0 && missingcolour <= 15);

    /* Clip to destination framebuffer bounds */
    int start_x = 0;
    int start_y = 0;
    int end_x = width;
    int end_y = height;
    if (xdestination < 0)            start_x = -xdestination;
    if (ydestination < 0)            start_y = -ydestination;
    if (xdestination + width  > HResD) end_x = HResD - xdestination;
    if (ydestination + height > VResD) end_y = VResD - ydestination;
    if (start_x >= end_x || start_y >= end_y) return;

    int src_stride = (HResS + 1) >> 1;  /* bytes per row in source */

    for (int y = start_y; y < end_y; y++) {
        const uint8_t *src_row = source + (size_t)(ysource + y) * src_stride;
        int dst_y = ydestination + y;
        for (int x = start_x; x < end_x; x++) {
            int src_x = xsource + x;
            uint8_t b = src_row[src_x >> 1];
            uint8_t pixel = (src_x & 1) ? ((b >> 4) & 0x0F) : (b & 0x0F);
            if (has_transparency && pixel == missingcolour) continue;
            DrawPixel(xdestination + x, dst_y, rgb121_palette[pixel]);
        }
    }
}

/*
 * blit121_self — copy a region of the destination buffer to itself,
 * handling overlap. Used by SCROLL-style operations on PicoMiteVGA
 * SCREENMODE2/3, where source AND destination are the nibble-packed
 * framebuffer. We don't use that on PicoCalc / host / web (the active
 * framebuffer is RGB565/ARGB, not RGB121-packed), so this is a stub
 * for symbol-link parity with upstream. If a TILEMAP user wants
 * copy-self semantics on our targets, route through the existing
 * BLIT machinery instead.
 */
void blit121_self(uint8_t *framebuffer, int xsource, int ysource,
                  int width, int height,
                  int xdestination, int ydestination)
{
    (void)framebuffer; (void)xsource; (void)ysource;
    (void)width; (void)height; (void)xdestination; (void)ydestination;
    /* Intentionally empty — see header comment. */
}
