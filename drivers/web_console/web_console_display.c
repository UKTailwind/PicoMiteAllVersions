/*
 * drivers/web_console/web_console_display.c
 *
 * Target-clean RGB24 framebuffer for the browser web console. Drawing
 * primitives mutate pixels immediately and only coalesce dirty bounds;
 * transport code decides later whether to send a small BLIT delta or a
 * full FRMB snapshot.
 */

#include "web_console_display.h"
#include "web_console_protocol.h"

#include <string.h>

static uint32_t colour24(int c) {
    return (uint32_t)c & 0x00ffffffu;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void put_u16_le(uint8_t * dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_i16_le(uint8_t * dst, int16_t v) {
    put_u16_le(dst, (uint16_t)v);
}

static uint8_t rgb332(uint32_t c) {
    uint8_t r = (uint8_t)((c >> 16) & 0xffu);
    uint8_t g = (uint8_t)((c >> 8) & 0xffu);
    uint8_t b = (uint8_t)(c & 0xffu);
    return (uint8_t)((r & 0xe0u) | ((g & 0xe0u) >> 3) | (b >> 6));
}

static size_t rgb332_rle_len(const web_console_display_t * d,
                             int x1, int y1, int x2, int y2) {
    size_t len = 0;
    uint8_t last = 0;
    uint16_t run = 0;

    for (int yy = y1; yy <= y2; ++yy) {
        const uint32_t * row = d->pixels + (size_t)yy * (size_t)d->width +
                               (size_t)x1;
        for (int xx = x1; xx <= x2; ++xx) {
            uint8_t c = rgb332(row[xx - x1]);
            if (run && c == last && run < 65535u) {
                run++;
            } else {
                if (run) len += 3u;
                last = c;
                run = 1;
            }
        }
    }
    if (run) len += 3u;
    return len;
}

static uint8_t * pack_rgb332_rle(uint8_t * p, const web_console_display_t * d,
                                 int x1, int y1, int x2, int y2) {
    uint8_t last = 0;
    uint16_t run = 0;

    for (int yy = y1; yy <= y2; ++yy) {
        const uint32_t * row = d->pixels + (size_t)yy * (size_t)d->width +
                               (size_t)x1;
        for (int xx = x1; xx <= x2; ++xx) {
            uint8_t c = rgb332(row[xx - x1]);
            if (run && c == last && run < 65535u) {
                run++;
            } else {
                if (run) {
                    put_u16_le(p, run);
                    p[2] = last;
                    p += 3;
                }
                last = c;
                run = 1;
            }
        }
    }
    if (run) {
        put_u16_le(p, run);
        p[2] = last;
        p += 3;
    }
    return p;
}

static int in_bounds(const web_console_display_t * d, int x, int y) {
    return d && x >= 0 && y >= 0 && x < d->width && y < d->height;
}

static void mark_dirty(web_console_display_t * d,
                       int x1, int y1, int x2, int y2) {
    if (!d || !d->pixels) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    x1 = clamp_int(x1, 0, d->width - 1);
    x2 = clamp_int(x2, 0, d->width - 1);
    y1 = clamp_int(y1, 0, d->height - 1);
    y2 = clamp_int(y2, 0, d->height - 1);
    if (x1 > x2 || y1 > y2) return;
    if (!d->dirty) {
        d->dirty = 1;
        d->dirty_x1 = x1;
        d->dirty_y1 = y1;
        d->dirty_x2 = x2;
        d->dirty_y2 = y2;
        return;
    }
    if (x1 < d->dirty_x1) d->dirty_x1 = x1;
    if (y1 < d->dirty_y1) d->dirty_y1 = y1;
    if (x2 > d->dirty_x2) d->dirty_x2 = x2;
    if (y2 > d->dirty_y2) d->dirty_y2 = y2;
}

int web_console_display_init(web_console_display_t * d, int width, int height,
                             uint32_t * pixels, size_t pixel_count, int bg) {
    if (!d || width <= 0 || height <= 0 || !pixels) return 0;
    size_t need = (size_t)width * (size_t)height;
    if (pixel_count < need) return 0;
    memset(d, 0, sizeof(*d));
    d->width = width;
    d->height = height;
    d->pixels = pixels;
    d->pixel_count = need;
    for (size_t i = 0; i < need; ++i) pixels[i] = colour24(bg);
    d->generation++;
    d->needs_resync = 1;
    mark_dirty(d, 0, 0, width - 1, height - 1);
    return 1;
}

const uint32_t * web_console_display_pixels(const web_console_display_t * d,
                                            size_t * pixel_count) {
    if (pixel_count) *pixel_count = d ? d->pixel_count : 0;
    return d ? d->pixels : NULL;
}

void web_console_display_clear(web_console_display_t * d, int colour) {
    if (!d || !d->pixels) return;
    uint32_t c = colour24(colour);
    for (size_t i = 0; i < d->pixel_count; ++i) d->pixels[i] = c;
    d->generation++;
    mark_dirty(d, 0, 0, d->width - 1, d->height - 1);
}

void web_console_display_pixel(web_console_display_t * d,
                               int x, int y, int colour) {
    if (!in_bounds(d, x, y)) return;
    d->pixels[(size_t)y * (size_t)d->width + (size_t)x] = colour24(colour);
    d->generation++;
    mark_dirty(d, x, y, x, y);
}

void web_console_display_rect(web_console_display_t * d,
                              int x1, int y1, int x2, int y2, int colour) {
    if (!d || !d->pixels) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    x1 = clamp_int(x1, 0, d->width - 1);
    x2 = clamp_int(x2, 0, d->width - 1);
    y1 = clamp_int(y1, 0, d->height - 1);
    y2 = clamp_int(y2, 0, d->height - 1);
    if (x1 > x2 || y1 > y2) return;

    uint32_t c = colour24(colour);
    for (int y = y1; y <= y2; ++y) {
        uint32_t * row = d->pixels + (size_t)y * (size_t)d->width;
        for (int x = x1; x <= x2; ++x) row[x] = c;
    }
    d->generation++;
    mark_dirty(d, x1, y1, x2, y2);
}

void web_console_display_bitmap(web_console_display_t * d,
                                int x, int y, int width, int height,
                                int scale, int fc, int bc,
                                const unsigned char * bitmap) {
    if (!d || !bitmap || width <= 0 || height <= 0) return;
    if (scale < 1) scale = 1;
    int total_bits = width * height;
    uint32_t fg = colour24(fc);
    uint32_t bg = colour24(bc);
    int want_bg = bc >= 0;

    if (scale == 1) {
        int bx1 = x < 0 ? 0 : x;
        int by1 = y < 0 ? 0 : y;
        int bx2 = x + width - 1;
        int by2 = y + height - 1;
        if (bx2 >= d->width) bx2 = d->width - 1;
        if (by2 >= d->height) by2 = d->height - 1;
        if (bx1 > bx2 || by1 > by2) return;

        int touched = 0;
        for (int py = by1; py <= by2; ++py) {
            uint32_t * dst = d->pixels + (size_t)py * (size_t)d->width;
            int row = py - y;
            for (int px = bx1; px <= bx2; ++px) {
                int col = px - x;
                int bit = row * width + col;
                int on = (bitmap[bit / 8] >>
                          ((total_bits - bit - 1) % 8)) &
                         1;
                if (on) {
                    dst[px] = fg;
                    touched = 1;
                } else if (want_bg) {
                    dst[px] = bg;
                    touched = 1;
                }
            }
        }
        if (touched) {
            d->generation++;
            mark_dirty(d, bx1, by1, bx2, by2);
        }
        return;
    }

    int touched = 0;
    int bx1 = d->width;
    int by1 = d->height;
    int bx2 = -1;
    int by2 = -1;

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int bit = row * width + col;
            int on = (bitmap[bit / 8] >> ((total_bits - bit - 1) % 8)) & 1;
            if (!on && !want_bg) continue;
            uint32_t c = on ? fg : bg;
            for (int sy = 0; sy < scale; ++sy) {
                int py = y + row * scale + sy;
                if (py < 0 || py >= d->height) continue;
                uint32_t * dst = d->pixels + (size_t)py * (size_t)d->width;
                for (int sx = 0; sx < scale; ++sx) {
                    int px = x + col * scale + sx;
                    if (px < 0 || px >= d->width) continue;
                    dst[px] = c;
                    touched = 1;
                    if (px < bx1) bx1 = px;
                    if (py < by1) by1 = py;
                    if (px > bx2) bx2 = px;
                    if (py > by2) by2 = py;
                }
            }
        }
    }

    if (touched) {
        d->generation++;
        mark_dirty(d, bx1, by1, bx2, by2);
    }
}

void web_console_display_draw_buffer(web_console_display_t * d,
                                     int x1, int y1, int x2, int y2,
                                     const unsigned char * bgr) {
    if (!d || !bgr) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    int src_w = x2 - x1 + 1;
    int bx1 = x1 < 0 ? 0 : x1;
    int by1 = y1 < 0 ? 0 : y1;
    int bx2 = x2 >= d->width ? d->width - 1 : x2;
    int by2 = y2 >= d->height ? d->height - 1 : y2;
    if (bx1 > bx2 || by1 > by2) return;

    for (int y = by1; y <= by2; ++y) {
        uint32_t * dst = d->pixels + (size_t)y * (size_t)d->width;
        const unsigned char * src = bgr +
                                    ((size_t)(y - y1) * (size_t)src_w + (size_t)(bx1 - x1)) * 3u;
        for (int x = bx1; x <= bx2; ++x) {
            dst[x] = ((uint32_t)src[2] << 16) |
                     ((uint32_t)src[1] << 8) |
                     (uint32_t)src[0];
            src += 3;
        }
    }
    d->generation++;
    mark_dirty(d, bx1, by1, bx2, by2);
}

void web_console_display_read_buffer(const web_console_display_t * d,
                                     int x1, int y1, int x2, int y2,
                                     unsigned char * bgr) {
    if (!d || !bgr) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            uint32_t c = in_bounds(d, x, y) ? d->pixels[(size_t)y * (size_t)d->width + (size_t)x] : 0;
            *bgr++ = (unsigned char)(c & 0xffu);
            *bgr++ = (unsigned char)((c >> 8) & 0xffu);
            *bgr++ = (unsigned char)((c >> 16) & 0xffu);
        }
    }
}

void web_console_display_scroll(web_console_display_t * d, int lines, int bg) {
    if (!d || !d->pixels || lines == 0) return;
    int abs_lines = lines < 0 ? -lines : lines;
    uint32_t fill = colour24(bg);
    if (abs_lines >= d->height) {
        web_console_display_clear(d, bg);
        return;
    }
    size_t row = (size_t)d->width;
    if (lines > 0) {
        memmove(d->pixels, d->pixels + (size_t)lines * row,
                (size_t)(d->height - lines) * row * sizeof(*d->pixels));
        uint32_t * tail = d->pixels + (size_t)(d->height - lines) * row;
        for (size_t i = 0; i < (size_t)lines * row; ++i) tail[i] = fill;
    } else {
        memmove(d->pixels + (size_t)abs_lines * row, d->pixels,
                (size_t)(d->height - abs_lines) * row * sizeof(*d->pixels));
        for (size_t i = 0; i < (size_t)abs_lines * row; ++i) d->pixels[i] = fill;
    }
    d->generation++;
    mark_dirty(d, 0, 0, d->width - 1, d->height - 1);
}

int web_console_display_take_resync(web_console_display_t * d) {
    if (!d || !d->needs_resync) return 0;
    d->needs_resync = 0;
    return 1;
}

void web_console_display_request_resync(web_console_display_t * d) {
    if (d) d->needs_resync = 1;
}

int web_console_display_dirty_bounds(const web_console_display_t * d,
                                     int * x1, int * y1, int * x2, int * y2) {
    if (!d || !d->dirty) return 0;
    if (x1) *x1 = d->dirty_x1;
    if (y1) *y1 = d->dirty_y1;
    if (x2) *x2 = d->dirty_x2;
    if (y2) *y2 = d->dirty_y2;
    return 1;
}

void web_console_display_clear_dirty(web_console_display_t * d) {
    if (d) d->dirty = 0;
}

size_t web_console_display_pack_dirty_blit(const web_console_display_t * d,
                                           uint8_t * payload,
                                           size_t payload_cap,
                                           int x1, int y1, int x2, int y2) {
    if (!d || !d->pixels || !payload ||
        payload_cap < WEB_CONSOLE_FRAME_HEADER_LEN + 9u) {
        return 0;
    }
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    x1 = clamp_int(x1, 0, d->width - 1);
    x2 = clamp_int(x2, 0, d->width - 1);
    y1 = clamp_int(y1, 0, d->height - 1);
    y2 = clamp_int(y2, 0, d->height - 1);
    if (x1 > x2 || y1 > y2) return 0;

    size_t w = (size_t)(x2 - x1 + 1);
    size_t h = (size_t)(y2 - y1 + 1);
    if (w > 65535u || h > 65535u) return 0;
    if (w > SIZE_MAX / h) {
        return 0;
    }
    size_t rle_len = rgb332_rle_len(d, x1, y1, x2, y2);
    if (rle_len > SIZE_MAX - WEB_CONSOLE_FRAME_HEADER_LEN - 9u) return 0;
    size_t body_len = 9u + rle_len;
    size_t total_len = WEB_CONSOLE_FRAME_HEADER_LEN + body_len;
    if (total_len > payload_cap) return 0;

    memcpy(payload, "CMDS", 4);
    put_u16_le(payload + 4, (uint16_t)d->width);
    put_u16_le(payload + 6, (uint16_t)d->height);
    uint8_t * p = payload + WEB_CONSOLE_FRAME_HEADER_LEN;
    *p++ = WEB_CONSOLE_OP_BLIT_RGB332_RLE;
    put_i16_le(p, (int16_t)x1);
    put_i16_le(p + 2, (int16_t)y1);
    put_u16_le(p + 4, (uint16_t)w);
    put_u16_le(p + 6, (uint16_t)h);
    p += 8;
    p = pack_rgb332_rle(p, d, x1, y1, x2, y2);
    (void)p;
    return total_len;
}
