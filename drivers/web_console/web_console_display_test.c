#include "web_console_display.h"
#include "web_console_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_large_text_repaint_coalesces_to_dirty_bounds(void) {
    enum { W = 320,
           H = 240,
           GLYPH_W = 8,
           GLYPH_H = 12 };
    static uint32_t pixels[W * H];
    static const unsigned char glyph[] = {
        0xff,
        0x81,
        0xbd,
        0xa5,
        0xa5,
        0xbd,
        0x81,
        0xff,
        0xff,
        0x81,
        0x81,
        0xff,
    };
    web_console_display_t display;

    assert(web_console_display_init(&display, W, H, pixels, W * H, 0));
    assert(web_console_display_take_resync(&display) == 1);
    web_console_display_clear_dirty(&display);

    for (int y = 0; y + GLYPH_H <= H; y += GLYPH_H) {
        for (int x = 0; x + GLYPH_W <= W; x += GLYPH_W) {
            web_console_display_bitmap(&display, x, y, GLYPH_W, GLYPH_H,
                                       1, 0xffffff, 0x000000, glyph);
        }
    }

    int x1, y1, x2, y2;
    assert(web_console_display_dirty_bounds(&display, &x1, &y1, &x2, &y2));
    assert(x1 == 0);
    assert(y1 == 0);
    assert(x2 == W - 1);
    assert(y2 == H - 1);
    assert(display.needs_resync == 0);
    assert(display.generation > 1);

    static uint8_t payload[90000];
    size_t n = web_console_display_pack_dirty_blit(&display, payload,
                                                   sizeof payload,
                                                   x1, y1, x2, y2);
    assert(n > 0);
    assert(n < 8u + 9u + W * H);
    assert(payload[8] == WEB_CONSOLE_OP_BLIT_RGB332_RLE);
}

static void test_small_dirty_rect_packs_one_framebuffer_blit(void) {
    enum { W = 64,
           H = 32 };
    static uint32_t pixels[W * H];
    static uint8_t payload[512];
    web_console_display_t display;

    assert(web_console_display_init(&display, W, H, pixels, W * H, 0));
    assert(web_console_display_take_resync(&display) == 1);
    web_console_display_clear_dirty(&display);
    web_console_display_rect(&display, 2, 3, 9, 8, 0x112233);

    int x1, y1, x2, y2;
    assert(web_console_display_dirty_bounds(&display, &x1, &y1, &x2, &y2));
    size_t n = web_console_display_pack_dirty_blit(&display, payload,
                                                   sizeof payload,
                                                   x1, y1, x2, y2);
    assert(n == 8u + 9u + 3u);
    assert(memcmp(payload, "CMDS", 4) == 0);
    assert(payload[8] == WEB_CONSOLE_OP_BLIT_RGB332_RLE);
}

int main(void) {
    test_large_text_repaint_coalesces_to_dirty_bounds();
    test_small_dirty_rect_packs_one_framebuffer_blit();
    return 0;
}
