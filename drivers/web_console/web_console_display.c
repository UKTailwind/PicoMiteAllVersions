/*
 * drivers/web_console/web_console_display.c
 *
 * Small RGB24 framebuffer plus bounded CMDS queue. Producers never call
 * transport code; slow clients either receive later commands or a resync.
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

static void put_u16_le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_i16_le(uint8_t *dst, int16_t v) {
    put_u16_le(dst, (uint16_t)v);
}

static void put_u32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
    dst[2] = (uint8_t)((v >> 16) & 0xffu);
    dst[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void append_or_resync(web_console_display_t *d,
                             const uint8_t *cmd, size_t len) {
    if (!d || !cmd || !len || len > d->cmd_cap) return;
    if (d->cmd_len + len > d->cmd_cap) {
        d->cmd_len = 0;
        d->needs_resync = 1;
        d->dropped++;
    }
    memcpy(d->cmds + d->cmd_len, cmd, len);
    d->cmd_len += len;
}

static int in_bounds(const web_console_display_t *d, int x, int y) {
    return d && x >= 0 && y >= 0 && x < d->width && y < d->height;
}

int web_console_display_init(web_console_display_t *d, int width, int height,
                             uint32_t *pixels, size_t pixel_count,
                             uint8_t *cmd_buf, size_t cmd_cap, int bg) {
    if (!d || width <= 0 || height <= 0 || !pixels || !cmd_buf) return 0;
    size_t need = (size_t)width * (size_t)height;
    if (pixel_count < need || cmd_cap < 32u) return 0;
    memset(d, 0, sizeof(*d));
    d->width = width;
    d->height = height;
    d->pixels = pixels;
    d->pixel_count = need;
    d->cmds = cmd_buf;
    d->cmd_cap = cmd_cap;
    for (size_t i = 0; i < need; ++i) pixels[i] = colour24(bg);
    d->generation++;
    d->needs_resync = 1;
    return 1;
}

const uint32_t *web_console_display_pixels(const web_console_display_t *d,
                                           size_t *pixel_count) {
    if (pixel_count) *pixel_count = d ? d->pixel_count : 0;
    return d ? d->pixels : NULL;
}

void web_console_display_clear(web_console_display_t *d, int colour) {
    if (!d || !d->pixels) return;
    uint32_t c = colour24(colour);
    for (size_t i = 0; i < d->pixel_count; ++i) d->pixels[i] = c;
    d->cmd_len = 0;
    uint8_t cmd[5];
    size_t n = web_console_pack_cmd_cls(cmd, sizeof cmd, colour);
    append_or_resync(d, cmd, n);
    d->generation++;
}

void web_console_display_pixel(web_console_display_t *d,
                               int x, int y, int colour) {
    if (!in_bounds(d, x, y)) return;
    d->pixels[(size_t)y * (size_t)d->width + (size_t)x] = colour24(colour);
    uint8_t cmd[9];
    size_t n = web_console_pack_cmd_pixel(cmd, sizeof cmd, x, y, colour);
    append_or_resync(d, cmd, n);
    d->generation++;
}

void web_console_display_rect(web_console_display_t *d,
                              int x1, int y1, int x2, int y2, int colour) {
    if (!d || !d->pixels) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    x1 = clamp_int(x1, 0, d->width - 1);
    x2 = clamp_int(x2, 0, d->width - 1);
    y1 = clamp_int(y1, 0, d->height - 1);
    y2 = clamp_int(y2, 0, d->height - 1);
    if (x1 > x2 || y1 > y2) return;

    uint32_t c = colour24(colour);
    for (int y = y1; y <= y2; ++y) {
        uint32_t *row = d->pixels + (size_t)y * (size_t)d->width;
        for (int x = x1; x <= x2; ++x) row[x] = c;
    }

    uint8_t cmd[13];
    size_t n;
    if (x1 == 0 && y1 == 0 && x2 == d->width - 1 && y2 == d->height - 1) {
        d->cmd_len = 0;
        n = web_console_pack_cmd_cls(cmd, sizeof cmd, colour);
    } else {
        n = web_console_pack_cmd_rect(cmd, sizeof cmd, x1, y1, x2, y2,
                                      colour);
    }
    append_or_resync(d, cmd, n);
    d->generation++;
}

static void append_blit_rect(web_console_display_t *d,
                             int x, int y, int w, int h) {
    if (!d || w <= 0 || h <= 0) return;
    size_t pixels = (size_t)w * (size_t)h;
    size_t len = 9u + pixels * 4u;
    if (len > d->cmd_cap || d->cmd_len + len > d->cmd_cap) {
        d->cmd_len = 0;
        d->needs_resync = 1;
        d->dropped++;
        return;
    }

    uint8_t *p = d->cmds + d->cmd_len;
    p[0] = WEB_CONSOLE_OP_BLIT;
    put_i16_le(p + 1, (int16_t)x);
    put_i16_le(p + 3, (int16_t)y);
    put_u16_le(p + 5, (uint16_t)w);
    put_u16_le(p + 7, (uint16_t)h);
    p += 9;
    for (int yy = 0; yy < h; ++yy) {
        const uint32_t *row = d->pixels + (size_t)(y + yy) * (size_t)d->width +
                              (size_t)x;
        for (int xx = 0; xx < w; ++xx) {
            uint32_t c = row[xx];
            *p++ = (uint8_t)((c >> 16) & 0xffu);
            *p++ = (uint8_t)((c >> 8) & 0xffu);
            *p++ = (uint8_t)(c & 0xffu);
            *p++ = 0xffu;
        }
    }
    d->cmd_len += len;
}

void web_console_display_bitmap(web_console_display_t *d,
                                int x, int y, int width, int height,
                                int scale, int fc, int bc,
                                const unsigned char *bitmap) {
    if (!d || !bitmap || width <= 0 || height <= 0) return;
    if (scale < 1) scale = 1;
    int out_w = width * scale;
    int out_h = height * scale;
    int total_bits = width * height;
    uint32_t fg = colour24(fc);
    uint32_t bg = colour24(bc);
    int want_bg = bc >= 0;

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int bit = row * width + col;
            int on = (bitmap[bit / 8] >> ((total_bits - bit - 1) % 8)) & 1;
            if (!on && !want_bg) continue;
            uint32_t c = on ? fg : bg;
            for (int sy = 0; sy < scale; ++sy) {
                int py = y + row * scale + sy;
                if (py < 0 || py >= d->height) continue;
                uint32_t *dst = d->pixels + (size_t)py * (size_t)d->width;
                for (int sx = 0; sx < scale; ++sx) {
                    int px = x + col * scale + sx;
                    if (px >= 0 && px < d->width) dst[px] = c;
                }
            }
        }
    }

    int bx1 = clamp_int(x, 0, d->width - 1);
    int by1 = clamp_int(y, 0, d->height - 1);
    int bx2 = clamp_int(x + out_w - 1, 0, d->width - 1);
    int by2 = clamp_int(y + out_h - 1, 0, d->height - 1);
    if (bx1 <= bx2 && by1 <= by2) append_blit_rect(d, bx1, by1,
                                                   bx2 - bx1 + 1,
                                                   by2 - by1 + 1);
    d->generation++;
}

void web_console_display_draw_buffer(web_console_display_t *d,
                                     int x1, int y1, int x2, int y2,
                                     const unsigned char *bgr) {
    if (!d || !bgr) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    int src_w = x2 - x1 + 1;
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            size_t si = ((size_t)(y - y1) * (size_t)src_w +
                         (size_t)(x - x1)) * 3u;
            if (in_bounds(d, x, y)) {
                d->pixels[(size_t)y * (size_t)d->width + (size_t)x] =
                    ((uint32_t)bgr[si + 2] << 16) |
                    ((uint32_t)bgr[si + 1] << 8) |
                    (uint32_t)bgr[si];
            }
        }
    }
    int bx1 = clamp_int(x1, 0, d->width - 1);
    int by1 = clamp_int(y1, 0, d->height - 1);
    int bx2 = clamp_int(x2, 0, d->width - 1);
    int by2 = clamp_int(y2, 0, d->height - 1);
    if (bx1 <= bx2 && by1 <= by2) append_blit_rect(d, bx1, by1,
                                                   bx2 - bx1 + 1,
                                                   by2 - by1 + 1);
    d->generation++;
}

void web_console_display_read_buffer(const web_console_display_t *d,
                                     int x1, int y1, int x2, int y2,
                                     unsigned char *bgr) {
    if (!d || !bgr) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            uint32_t c = in_bounds(d, x, y) ?
                d->pixels[(size_t)y * (size_t)d->width + (size_t)x] : 0;
            *bgr++ = (unsigned char)(c & 0xffu);
            *bgr++ = (unsigned char)((c >> 8) & 0xffu);
            *bgr++ = (unsigned char)((c >> 16) & 0xffu);
        }
    }
}

void web_console_display_scroll(web_console_display_t *d, int lines, int bg) {
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
        uint32_t *tail = d->pixels + (size_t)(d->height - lines) * row;
        for (size_t i = 0; i < (size_t)lines * row; ++i) tail[i] = fill;
    } else {
        memmove(d->pixels + (size_t)abs_lines * row, d->pixels,
                (size_t)(d->height - abs_lines) * row * sizeof(*d->pixels));
        for (size_t i = 0; i < (size_t)abs_lines * row; ++i) d->pixels[i] = fill;
    }
    uint8_t cmd[7];
    size_t n = web_console_pack_cmd_scroll(cmd, sizeof cmd, lines, bg);
    append_or_resync(d, cmd, n);
    d->generation++;
}

int web_console_display_take_resync(web_console_display_t *d) {
    if (!d || !d->needs_resync) return 0;
    d->needs_resync = 0;
    d->cmd_len = 0;
    return 1;
}

static size_t command_len(const uint8_t *p, size_t avail) {
    if (!p || !avail) return 0;
    switch (p[0]) {
        case WEB_CONSOLE_OP_CLS: return avail >= 5u ? 5u : 0u;
        case WEB_CONSOLE_OP_RECT: return avail >= 13u ? 13u : 0u;
        case WEB_CONSOLE_OP_PIXEL: return avail >= 9u ? 9u : 0u;
        case WEB_CONSOLE_OP_SCROLL: return avail >= 7u ? 7u : 0u;
        case WEB_CONSOLE_OP_BLIT:
            if (avail < 9u) return 0u;
            {
                size_t w = (size_t)p[5] | ((size_t)p[6] << 8);
                size_t h = (size_t)p[7] | ((size_t)p[8] << 8);
                if (w == 0 || h == 0) return 0u;
                if (w > (SIZE_MAX - 9u) / h / 4u) return 0u;
                size_t n = 9u + w * h * 4u;
                return avail >= n ? n : 0u;
            }
        default:
            return 0u;
    }
}

size_t web_console_display_drain_cmds(web_console_display_t *d,
                                      uint8_t *payload, size_t payload_cap) {
    if (!d || !payload || payload_cap <= WEB_CONSOLE_FRAME_HEADER_LEN ||
        d->cmd_len == 0) {
        return 0;
    }

    size_t body_cap = payload_cap - WEB_CONSOLE_FRAME_HEADER_LEN;
    size_t body_len = 0;
    while (body_len < d->cmd_len) {
        size_t n = command_len(d->cmds + body_len, d->cmd_len - body_len);
        if (n == 0 || body_len + n > body_cap) break;
        body_len += n;
    }
    if (body_len == 0) {
        d->cmd_len = 0;
        d->needs_resync = 1;
        d->dropped++;
        return 0;
    }

    size_t packed = web_console_pack_cmds(payload, payload_cap,
                                          d->width, d->height,
                                          d->cmds, body_len);
    if (!packed) return 0;
    if (body_len < d->cmd_len) {
        memmove(d->cmds, d->cmds + body_len, d->cmd_len - body_len);
    }
    d->cmd_len -= body_len;
    return packed;
}
