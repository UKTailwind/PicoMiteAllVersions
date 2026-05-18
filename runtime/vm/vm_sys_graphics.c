/*
 * VM syscall conversion rule:
 * - copy/adapt legacy implementation code as closely as possible
 * - copy/adapt dependent legacy helpers too when needed
 * - do not invent new algorithms when legacy code already exists
 * - do not call, wrap, or dispatch back into legacy handlers
 * Any deviation from legacy implementation shape must be explicit and justified.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "bc_alloc.h"
#include "bytecode.h"
#include "vm_device_support.h"
#include "vm_sys_graphics.h"

#include "hal/hal_display_merge.h"
#include "hal/hal_vm_framebuffer.h"
#include "hal/hal_display_pixel.h"

typedef struct VMGfxScratchBuffer {
    void *ptr;
    size_t bytes;
} VMGfxScratchBuffer;

static VMGfxScratchBuffer vm_gfx_short_a;
static VMGfxScratchBuffer vm_gfx_short_b;
static VMGfxScratchBuffer vm_gfx_float_a;
static VMGfxScratchBuffer vm_gfx_float_b;
static VMGfxScratchBuffer vm_gfx_float_c;
static VMGfxScratchBuffer vm_gfx_int_a;
static VMGfxScratchBuffer vm_gfx_int_b;
static VMGfxScratchBuffer vm_gfx_int_c;
static VMGfxScratchBuffer vm_gfx_int_d;
static VMGfxScratchBuffer vm_gfx_mask_a;
static VMGfxScratchBuffer vm_gfx_fb_line;

static void *vm_sys_graphics_reserve_scratch(VMGfxScratchBuffer *buffer, size_t bytes) {
    void *new_ptr;

    if (bytes == 0) return NULL;
    if (buffer->ptr != NULL && buffer->bytes >= bytes) return buffer->ptr;

    new_ptr = BC_ALLOC(bytes);
    if (!new_ptr) error("NEM[gfx:scratch] want=%", (int)bytes);
    if (buffer->ptr) BC_FREE(buffer->ptr);
    buffer->ptr = new_ptr;
    buffer->bytes = bytes;
    return buffer->ptr;
}

static uint8_t vm_sys_graphics_rgb121(uint32_t c) {
    return (uint8_t)(((c & 0x800000u) >> 20) |
                     ((c & 0x00C000u) >> 13) |
                     ((c & 0x000080u) >> 7));
}

/* FRAMEBUFFER merge-pipeline + copy scratch state + PICOMITE-side
 * internal helpers have moved to drivers/vm_framebuffer_picomite/
 * vm_framebuffer_picomite.c. Host impl lives in
 * ports/host_native/hal_vm_framebuffer_host.c; scanout ports (VGA/HDMI/WEB) link
 * the error-stub at drivers/vm_framebuffer_unsupported/. */

void vm_sys_graphics_reset(void) {
    VMGfxScratchBuffer *buffers[] = {
        &vm_gfx_short_a, &vm_gfx_short_b,
        &vm_gfx_float_a, &vm_gfx_float_b, &vm_gfx_float_c,
        &vm_gfx_int_a, &vm_gfx_int_b, &vm_gfx_int_c, &vm_gfx_int_d,
        &vm_gfx_mask_a, &vm_gfx_fb_line
    };
    size_t i;

    hal_vm_framebuffer_shutdown_runtime();

    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); i++) {
        if (buffers[i]->ptr) BC_FREE(buffers[i]->ptr);
        buffers[i]->ptr = NULL;
        buffers[i]->bytes = 0;
    }
}

static int vm_sys_graphics_circle_arg_int(const GfxCircleArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static MMFLOAT vm_sys_graphics_circle_arg_float(const GfxCircleArg *arg, int index) {
    if (!arg->present || arg->count <= 0) return 0;
    if (arg->get_float != NULL) return arg->get_float(arg->ctx, (arg->count > 1) ? index : 0);
    if (arg->get_int != NULL) return (MMFLOAT)arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
    return 0;
}

static void vm_sys_graphics_circle_fail_msg(const GfxCircleErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void vm_sys_graphics_circle_fail_range(const GfxCircleErrorSink *errors, const char *label,
                                     int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

static void vm_sys_graphics_arc_fill_circle_mask(int x, int y, int radius, int r, int fill,
                                                 int ints_per_line, uint32_t *br,
                                                 MMFLOAT aspect, MMFLOAT aspect2);

static int vm_sys_graphics_box_arg_value(const GfxBoxIntArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void vm_sys_graphics_box_fail_msg(const GfxBoxErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void vm_sys_graphics_box_fail_range(const GfxBoxErrorSink *errors, const char *label,
                                           int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

static void vm_sys_graphics_draw_hline_pixels(int x0, int x1, int y, int c) {
    int x;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (x = x0; x <= x1; x++) {
        DrawPixel(x, y, c);
    }
}

static void vm_sys_graphics_fill_rect_pixels(int x1, int y1, int x2, int y2, int c) {
    int y;
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
    for (y = y1; y <= y2; y++) {
        vm_sys_graphics_draw_hline_pixels(x1, x2, y, c);
    }
}

static void vm_sys_graphics_draw_rbox(int x1, int y1, int x2, int y2, int radius, int c, int fill) {
    int f, ddF_x, ddF_y, xx, yy;

    f = 1 - radius;
    ddF_x = 1;
    ddF_y = -2 * radius;
    xx = 0;
    yy = radius;

    while (xx < yy) {
        if (f >= 0) {
            yy -= 1;
            ddF_y += 2;
            f += ddF_y;
        }
        xx += 1;
        ddF_x += 2;
        f += ddF_x;
        DrawPixel(x2 + xx - radius, y2 + yy - radius, c);
        DrawPixel(x2 + yy - radius, y2 + xx - radius, c);
        DrawPixel(x1 - xx + radius, y2 + yy - radius, c);
        DrawPixel(x1 - yy + radius, y2 + xx - radius, c);

        DrawPixel(x2 + xx - radius, y1 - yy + radius, c);
        DrawPixel(x2 + yy - radius, y1 - xx + radius, c);
        DrawPixel(x1 - xx + radius, y1 - yy + radius, c);
        DrawPixel(x1 - yy + radius, y1 - xx + radius, c);
        if (fill >= 0) {
            vm_sys_graphics_draw_hline_pixels(x2 + xx - radius - 1, x1 - xx + radius + 1, y2 + yy - radius, fill);
            vm_sys_graphics_draw_hline_pixels(x2 + yy - radius - 1, x1 - yy + radius + 1, y2 + xx - radius, fill);
            vm_sys_graphics_draw_hline_pixels(x2 + xx - radius - 1, x1 - xx + radius + 1, y1 - yy + radius, fill);
            vm_sys_graphics_draw_hline_pixels(x2 + yy - radius - 1, x1 - yy + radius + 1, y1 - xx + radius, fill);
        }
    }
    if (fill >= 0) vm_sys_graphics_fill_rect_pixels(x1 + 1, y1 + radius, x2 - 1, y2 - radius, fill);
    vm_sys_graphics_draw_hline_pixels(x1 + radius - 1, x2 - radius + 1, y1, c);
    vm_sys_graphics_draw_hline_pixels(x1 + radius - 1, x2 - radius + 1, y2, c);
    vm_sys_graphics_fill_rect_pixels(x1, y1 + radius, x1, y2 - radius, c);
    vm_sys_graphics_fill_rect_pixels(x2, y1 + radius, x2, y2 - radius, c);
}

static void vm_sys_graphics_calc_triangle_edge(int x0, int y0, int x1, int y1,
                                               short *xmin, short *xmax) {
    int absX, absY, offX, offY, err, x, y;
    x = x0;
    y = y0;
    if (y >= 0 && y < VRes) {
        if (x < xmin[y]) xmin[y] = (short)x;
        if (x > xmax[y]) xmax[y] = (short)x;
    }
    absX = abs(x1 - x0);
    absY = abs(y1 - y0);
    offX = x0 < x1 ? 1 : -1;
    offY = y0 < y1 ? 1 : -1;
    if (absX > absY) {
        err = absX / 2;
        while (x != x1) {
            err -= absY;
            if (err < 0) {
                y += offY;
                err += absX;
            }
            x += offX;
            if (y >= 0 && y < VRes) {
                if (x < xmin[y]) xmin[y] = (short)x;
                if (x > xmax[y]) xmax[y] = (short)x;
            }
        }
    } else {
        err = absY / 2;
        while (y != y1) {
            err -= absX;
            if (err < 0) {
                x += offX;
                err += absY;
            }
            y += offY;
            if (y >= 0 && y < VRes) {
                if (x < xmin[y]) xmin[y] = (short)x;
                if (x > xmax[y]) xmax[y] = (short)x;
            }
        }
    }
}

static void vm_sys_graphics_draw_triangle_pixels(int x0, int y0, int x1, int y1,
                                                 int x2, int y2, int c, int f) {
    short *xmin;
    short *xmax;

    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0) {
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        if (y1 > y2) { int t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        DrawLine(x0, y0, x2, y2, 1, c);
        return;
    }

    if (f == -1) {
        DrawLine(x0, y0, x1, y1, 1, c);
        DrawLine(x1, y1, x2, y2, 1, c);
        DrawLine(x2, y2, x0, y0, 1, c);
        return;
    }

    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    if (VRes <= 0) return;
    xmin = (short *)vm_sys_graphics_reserve_scratch(&vm_gfx_short_a, (size_t)VRes * sizeof(short));
    xmax = (short *)vm_sys_graphics_reserve_scratch(&vm_gfx_short_b, (size_t)VRes * sizeof(short));

    for (int y = y0; y <= y2; y++) {
        if (y >= 0 && y < VRes) {
            xmin[y] = 32767;
            xmax[y] = -1;
        }
    }
    vm_sys_graphics_calc_triangle_edge(x0, y0, x1, y1, xmin, xmax);
    vm_sys_graphics_calc_triangle_edge(x1, y1, x2, y2, xmin, xmax);
    vm_sys_graphics_calc_triangle_edge(x2, y2, x0, y0, xmin, xmax);
    for (int y = y0; y <= y2; y++) {
        if (y >= 0 && y < VRes && xmax[y] >= xmin[y]) {
            vm_sys_graphics_draw_hline_pixels(xmin[y], xmax[y], y, f);
        }
    }
    DrawLine(x0, y0, x1, y1, 1, c);
    DrawLine(x1, y1, x2, y2, 1, c);
    DrawLine(x2, y2, x0, y0, 1, c);
}

static void vm_sys_graphics_fill_polygon_edges(const float *poly_x, const float *poly_y,
                                               int vertex_count, int count,
                                               int ystart, int yend,
                                               int c, int f) {
    float *node_x;
    int y, i, j;

    node_x = (float *)vm_sys_graphics_reserve_scratch(&vm_gfx_float_c, (size_t)count * sizeof(float));

    for (y = ystart; y < yend; y++) {
        int nodes = 0;
        float temp;
        j = vertex_count - 1;
        for (i = 0; i < vertex_count; i++) {
            if ((poly_y[i] < (float)y && poly_y[j] >= (float)y) ||
                (poly_y[j] < (float)y && poly_y[i] >= (float)y)) {
                node_x[nodes++] = (poly_x[i] +
                                   ((float)y - poly_y[i]) /
                                       (poly_y[j] - poly_y[i]) *
                                       (poly_x[j] - poly_x[i]));
            }
            j = i;
        }

        for (i = 1; i < nodes; i++) {
            temp = node_x[i];
            for (j = i; j > 0 && temp < node_x[j - 1]; j--) {
                node_x[j] = node_x[j - 1];
            }
            node_x[j] = temp;
        }

        for (i = 0; i + 1 < nodes; i += 2) {
            int xstart = (int)floorf(node_x[i]) + 1;
            int xend = (int)ceilf(node_x[i + 1]) - 1;
            DrawLine(xstart, y, xend, y, 1, f);
        }
    }

    for (i = 0; i < vertex_count; i++) {
        int x0 = (int)roundf(poly_x[i]);
        int y0 = (int)roundf(poly_y[i]);
        int x1 = (int)roundf(poly_x[(i + 1) % vertex_count]);
        int y1 = (int)roundf(poly_y[(i + 1) % vertex_count]);
        DrawLine(x0, y0, x1, y1, 1, c);
    }
}

static void vm_sys_graphics_draw_polygon_points(const int *x_values, const int *y_values,
                                                int point_count, int c, int f, int close) {
    int ymax = 0;
    int ymin = 1000000;
    int vertex_count = 0;
    float *poly_x;
    float *poly_y;

    if (point_count <= 0) return;
    if (f < 0) {
        for (int i = 0; i < point_count - 1; i++) {
            DrawLine(x_values[i], y_values[i], x_values[i + 1], y_values[i + 1], 1, c);
        }
        if (close) DrawLine(x_values[point_count - 1], y_values[point_count - 1], x_values[0], y_values[0], 1, c);
        return;
    }

    poly_x = (float *)vm_sys_graphics_reserve_scratch(&vm_gfx_float_a, (size_t)(point_count + 1) * sizeof(float));
    poly_y = (float *)vm_sys_graphics_reserve_scratch(&vm_gfx_float_b, (size_t)(point_count + 1) * sizeof(float));

    for (int i = 0; i < point_count; i++) {
        poly_x[vertex_count] = (float)x_values[i];
        poly_y[vertex_count] = (float)y_values[i];
        if (y_values[i] > ymax) ymax = y_values[i];
        if (y_values[i] < ymin) ymin = y_values[i];
        vertex_count++;
    }

    if (poly_y[vertex_count - 1] != poly_y[0] || poly_x[vertex_count - 1] != poly_x[0]) {
        poly_x[vertex_count] = poly_x[0];
        poly_y[vertex_count] = poly_y[0];
        vertex_count++;
    }

    if (vertex_count > 5) {
        vm_sys_graphics_fill_polygon_edges(poly_x, poly_y, vertex_count, point_count, ymin, ymax, c, f);
    } else if (vertex_count == 5) {
        vm_sys_graphics_draw_triangle_pixels((int)poly_x[0], (int)poly_y[0], (int)poly_x[1], (int)poly_y[1],
                                             (int)poly_x[2], (int)poly_y[2], f, f);
        vm_sys_graphics_draw_triangle_pixels((int)poly_x[0], (int)poly_y[0], (int)poly_x[2], (int)poly_y[2],
                                             (int)poly_x[3], (int)poly_y[3], f, f);
        if (f != c) {
            DrawLine((int)poly_x[0], (int)poly_y[0], (int)poly_x[1], (int)poly_y[1], 1, c);
            DrawLine((int)poly_x[1], (int)poly_y[1], (int)poly_x[2], (int)poly_y[2], 1, c);
            DrawLine((int)poly_x[2], (int)poly_y[2], (int)poly_x[3], (int)poly_y[3], 1, c);
            DrawLine((int)poly_x[3], (int)poly_y[3], (int)poly_x[4], (int)poly_y[4], 1, c);
        }
    } else {
        vm_sys_graphics_draw_triangle_pixels((int)poly_x[0], (int)poly_y[0], (int)poly_x[1], (int)poly_y[1],
                                             (int)poly_x[2], (int)poly_y[2], c, f);
    }
}

static void vm_sys_graphics_mask_hline(int x0, int x1, int y, int fill, int ints_per_line, uint32_t *br) {
    uint32_t x, xn;
    int w0, xx0, w1, xx1, i;
    const uint32_t a[] = {
        0xFFFFFFFF, 0x7FFFFFFF, 0x3FFFFFFF, 0x1FFFFFFF, 0x0FFFFFFF, 0x07FFFFFF, 0x03FFFFFF, 0x01FFFFFF,
        0x00FFFFFF, 0x007FFFFF, 0x003FFFFF, 0x001FFFFF, 0x000FFFFF, 0x0007FFFF, 0x0003FFFF, 0x0001FFFF,
        0x0000FFFF, 0x00007FFF, 0x00003FFF, 0x00001FFF, 0x00000FFF, 0x000007FF, 0x000003FF, 0x000001FF,
        0x000000FF, 0x0000007F, 0x0000003F, 0x0000001F, 0x0000000F, 0x00000007, 0x00000003, 0x00000001
    };
    const uint32_t b[] = {
        0x80000000, 0xC0000000, 0xE0000000, 0xF0000000, 0xF8000000, 0xFC000000, 0xFE000000, 0xFF000000,
        0xFF800000, 0xFFC00000, 0xFFE00000, 0xFFF00000, 0xFFF80000, 0xFFFC0000, 0xFFFE0000, 0xFFFF0000,
        0xFFFF8000, 0xFFFFC000, 0xFFFFE000, 0xFFFFF000, 0xFFFFF800, 0xFFFFFC00, 0xFFFFFE00, 0xFFFFFF00,
        0xFFFFFF80, 0xFFFFFFC0, 0xFFFFFFE0, 0xFFFFFFF0, 0xFFFFFFF8, 0xFFFFFFFC, 0xFFFFFFFE, 0xFFFFFFFF
    };

    w0 = y * ints_per_line + x0 / 32;
    xx0 = x0 & 0x1F;
    w1 = y * ints_per_line + x1 / 32;
    xx1 = x1 & 0x1F;

    if (w1 == w0) {
        x = a[xx0] & b[xx1];
        xn = ~x;
        if (fill) br[w0] |= x;
        else br[w0] &= xn;
    } else {
        if (w1 - w0 > 1) {
            for (i = w0 + 1; i < w1; i++) br[i] = fill ? 0xFFFFFFFF : 0;
        }
        x = ~a[xx0];
        br[w0] &= x;
        x = ~x;
        if (fill) br[w0] |= x;
        x = ~b[xx1];
        br[w1] &= x;
        x = ~x;
        if (fill) br[w1] |= x;
    }
}

static void vm_sys_graphics_arc_fill_circle_mask(int x, int y, int radius, int r, int fill,
                                                 int ints_per_line, uint32_t *br,
                                                 MMFLOAT aspect, MMFLOAT aspect2) {
    int a, b, P;
    int A, B, asp;

    x = (int)((MMFLOAT)r * aspect) + radius;
    y = r + radius;
    a = 0;
    b = radius;
    P = 1 - radius;
    asp = aspect2 * (MMFLOAT)(1 << 10);
    do {
        A = (a * asp) >> 10;
        B = (b * asp) >> 10;
        vm_sys_graphics_mask_hline(x - A - radius, x + A - radius, y + b - radius, fill, ints_per_line, br);
        vm_sys_graphics_mask_hline(x - A - radius, x + A - radius, y - b - radius, fill, ints_per_line, br);
        vm_sys_graphics_mask_hline(x - B - radius, x + B - radius, y + a - radius, fill, ints_per_line, br);
        vm_sys_graphics_mask_hline(x - B - radius, x + B - radius, y - a - radius, fill, ints_per_line, br);
        if (P < 0) P += 3 + 2 * a++;
        else P += 5 + 2 * (a++ - b--);
    } while (a <= b);
}

static void vm_sys_graphics_clear_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                                           int ints_per_line, uint32_t *br) {
    long a, b, y, last;
    long dx01, dy01, dx02, dy02, dx12, dy12, sa, sb;
    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0) return;

    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    dx01 = x1 - x0; dy01 = y1 - y0; dx02 = x2 - x0;
    dy02 = y2 - y0; dx12 = x2 - x1; dy12 = y2 - y1;
    sa = 0; sb = 0;
    last = (y1 == y2) ? y1 : (y1 - 1);
    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (a > b) { long t = a; a = b; b = t; }
        vm_sys_graphics_mask_hline((int)a, (int)b, (int)y, 0, ints_per_line, br);
    }
    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    while (y <= y2) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (a > b) { long t = a; a = b; b = t; }
        vm_sys_graphics_mask_hline((int)a, (int)b, (int)y, 0, ints_per_line, br);
        y++;
    }
}

static void vm_sys_graphics_arc_pointcalc(int angle, int x, int y, int r2, int *x0, int *y0) {
    MMFLOAT c1, s1;
    int quad;
    angle %= 360;
    switch (angle) {
        case 0:   *x0 = x;       *y0 = y - r2; break;
        case 45:  *x0 = x + r2 + 1; *y0 = y - r2; break;
        case 90:  *x0 = x + r2 + 1; *y0 = y; break;
        case 135: *x0 = x + r2 + 1; *y0 = y + r2; break;
        case 180: *x0 = x;       *y0 = y + r2; break;
        case 225: *x0 = x - r2;  *y0 = y + r2; break;
        case 270: *x0 = x - r2;  *y0 = y; break;
        case 315: *x0 = x - r2;  *y0 = y - r2; break;
        default:
            c1 = cos(Rad(angle));
            s1 = sin(Rad(angle));
            quad = (angle / 45) % 8;
            switch (quad) {
                case 0: *y0 = y - r2;     *x0 = x + s1 * r2 / c1; break;
                case 1:
                case 2: *x0 = x + r2 + 1; *y0 = y - c1 * r2 / s1; break;
                case 3:
                case 4: *y0 = y + r2;     *x0 = x - s1 * r2 / c1; break;
                case 5:
                case 6: *x0 = x - r2;     *y0 = y + c1 * r2 / s1; break;
                case 7: *y0 = y - r2;     *x0 = x + s1 * r2 / c1; break;
            }
            break;
    }
}

void vm_sys_graphics_arc_execute(int x, int y, int r1, int has_r2, int r2,
                                 MMFLOAT a1, MMFLOAT a2, int has_c, int c) {
    int i, j, k, xs = -1, xi = 0, m = 0;
    int rad1, rad2, rad3, rstart, quadr;
    int x0, y0, x1, y1, x2, y2, xr, yr;
    int quad1;
    int ints_per_line;
    int save_refresh;
    uint32_t *br;
    size_t words;

    if (!has_r2) {
        r2 = r1;
        r1--;
    }
    if (r1 < 0 || r2 < 0) error("Number out of bounds");
    if (r2 < r1) error("Inner radius < outer");
    rad1 = (int)a1;
    rad2 = (int)a2;
    while (rad1 < 0) rad1 += 360;
    while (rad2 < 0) rad2 += 360;
    if (rad1 == rad2) error("Radials");
    if (!has_c) c = gui_fcolour;
    if (c < 0 || c > WHITE) error("Number out of bounds");
    while (rad2 < rad1) rad2 += 360;
    rad3 = rad1 + 360;
    rstart = rad2;
    quad1 = (rad1 / 45) % 8;
    x2 = x; y2 = y;
    ints_per_line = RoundUptoInt((r2 * 2) + 1) / 32;
    words = (size_t)(ints_per_line + 1) * (size_t)((r2 * 2) + 1);
    br = (uint32_t *)vm_sys_graphics_reserve_scratch(&vm_gfx_mask_a, words * sizeof(uint32_t));
    memset(br, 0, words * sizeof(uint32_t));

    vm_sys_graphics_arc_fill_circle_mask(x, y, r2, r2, 1, ints_per_line, br, 1.0, 1.0);
    vm_sys_graphics_arc_fill_circle_mask(x, y, r1, r2, 0, ints_per_line, br, 1.0, 1.0);
    while (rstart < rad3) {
        vm_sys_graphics_arc_pointcalc(rstart, x, y, r2, &x0, &y0);
        quadr = (rstart / 45) % 8;
        if (quadr == quad1 && rad3 - rstart < 45) {
            vm_sys_graphics_arc_pointcalc(rad3, x, y, r2, &x1, &y1);
            vm_sys_graphics_clear_triangle(x0 - x + r2, y0 - y + r2, x1 - x + r2, y1 - y + r2,
                                           x2 - x + r2, y2 - y + r2, ints_per_line, br);
            rstart = rad3;
        } else {
            rstart += 45;
            rstart -= (rstart % 45);
            vm_sys_graphics_arc_pointcalc(rstart, x, y, r2, &xr, &yr);
            vm_sys_graphics_clear_triangle(x0 - x + r2, y0 - y + r2, xr - x + r2, yr - y + r2,
                                           x2 - x + r2, y2 - y + r2, ints_per_line, br);
        }
    }

    save_refresh = Option.Refresh;
    Option.Refresh = 0;
    for (j = 0; j < r2 * 2 + 1; j++) {
        for (i = 0; i < ints_per_line; i++) {
            k = (int)br[i + j * ints_per_line];
            for (m = 0; m < 32; m++) {
                if (xs == -1 && (k & 0x80000000u)) {
                    xs = m;
                    xi = i;
                }
                if (xs != -1 && !(k & 0x80000000u)) {
                    vm_sys_graphics_draw_hline_pixels(x - r2 + xs + xi * 32, x - r2 + m + i * 32, y - r2 + j, c);
                    xs = -1;
                }
                k <<= 1;
            }
        }
        if (xs != -1) {
            vm_sys_graphics_draw_hline_pixels(x - r2 + xs + xi * 32, x - r2 + m + i * 32, y - r2 + j, c);
            xs = -1;
        }
    }
    Option.Refresh = save_refresh;
}

void vm_sys_graphics_triangle_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                      const GfxBoxErrorSink *errors) {
    int x1, y1, x2, y2, x3, y3, c = gui_fcolour, f = -1;
    int n, nc = 0, nf = 0;
    int max_colour = WHITE;

    if (field_count < 6 || field_count > 8) {
        vm_sys_graphics_box_fail_msg(errors, "Argument count");
        return;
    }
    if (mode == GFX_BOX_MODE_SCALAR) {
        for (int i = 0; i < 6; i++) {
            if (!args[i].present || args[i].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Argument count");
                return;
            }
        }
        x1 = vm_sys_graphics_box_arg_value(&args[0], 0);
        y1 = vm_sys_graphics_box_arg_value(&args[1], 0);
        x2 = vm_sys_graphics_box_arg_value(&args[2], 0);
        y2 = vm_sys_graphics_box_arg_value(&args[3], 0);
        x3 = vm_sys_graphics_box_arg_value(&args[4], 0);
        y3 = vm_sys_graphics_box_arg_value(&args[5], 0);
        if (field_count >= 7 && args[6].present) {
            if (args[6].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Argument count");
                return;
            }
            c = vm_sys_graphics_box_arg_value(&args[6], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }
        if (field_count >= 8 && args[7].present) {
            if (args[7].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Argument count");
                return;
            }
            f = vm_sys_graphics_box_arg_value(&args[7], 0);
            if (f < -1 || f > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }
        vm_sys_graphics_draw_triangle_pixels(x1, y1, x2, y2, x3, y3, c, f);
        return;
    }

    for (int i = 0; i < 6; i++) {
        if (!args[i].present || args[i].count <= 1) {
            vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
            return;
        }
    }
    n = args[0].count;
    for (int i = 1; i < 6; i++) {
        if (args[i].count < n) n = args[i].count;
    }
    if (field_count >= 7 && args[6].present) {
        nc = args[6].count;
        if (nc == 1) {
            c = vm_sys_graphics_box_arg_value(&args[6], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        } else {
            if (nc < n) n = nc;
            for (int i = 0; i < nc; i++) {
                int value = vm_sys_graphics_box_arg_value(&args[6], i);
                if (value < 0 || value > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, value, 0, max_colour);
                    return;
                }
            }
        }
    }
    if (field_count >= 8 && args[7].present) {
        nf = args[7].count;
        if (nf == 1) {
            f = vm_sys_graphics_box_arg_value(&args[7], 0);
            if (f < -1 || f > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        } else {
            if (nf < n) n = nf;
            for (int i = 0; i < nf; i++) {
                int value = vm_sys_graphics_box_arg_value(&args[7], i);
                if (value < -1 || value > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, value, -1, max_colour);
                    return;
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        x1 = vm_sys_graphics_box_arg_value(&args[0], i);
        y1 = vm_sys_graphics_box_arg_value(&args[1], i);
        x2 = vm_sys_graphics_box_arg_value(&args[2], i);
        y2 = vm_sys_graphics_box_arg_value(&args[3], i);
        x3 = vm_sys_graphics_box_arg_value(&args[4], i);
        y3 = vm_sys_graphics_box_arg_value(&args[5], i);
        if (x1 == -1 && y1 == -1 && x2 == -1 && y2 == -1 && x3 == -1 && y3 == -1) return;
        if (nc > 1) c = vm_sys_graphics_box_arg_value(&args[6], i);
        if (nf > 1) f = vm_sys_graphics_box_arg_value(&args[7], i);
        vm_sys_graphics_draw_triangle_pixels(x1, y1, x2, y2, x3, y3, c, f);
    }
}

void vm_sys_graphics_polygon_execute(const GfxBoxIntArg *args, int field_count,
                                     const GfxBoxErrorSink *errors) {
    int c = gui_fcolour;
    int f = -1;
    const int max_colour = (int)WHITE;

    if (field_count < 3 || field_count > 5) {
        vm_sys_graphics_box_fail_msg(errors, "Argument count");
        return;
    }
    if (!args[0].present || !args[1].present || !args[2].present) {
        vm_sys_graphics_box_fail_msg(errors, "Argument count");
        return;
    }
    if (args[1].count <= 1 || args[2].count <= 1) {
        vm_sys_graphics_box_fail_msg(errors, "POLYGON requires numeric array arguments");
        return;
    }

    if (args[0].count == 1) {
        int xcount = vm_sys_graphics_box_arg_value(&args[0], 0);
        int xtot = xcount;
        int nx = args[1].count;
        int ny = args[2].count;
        int *x_values;
        int *y_values;

        if ((xcount < 3 || xcount > 9999) && xcount != 0) {
            vm_sys_graphics_box_fail_msg(errors, "Invalid number of vertices");
            return;
        }
        if (xcount == 0) xcount = xtot = nx;
        if (nx < xtot) {
            vm_sys_graphics_box_fail_msg(errors, "X Dimensions");
            return;
        }
        if (ny < xtot) {
            vm_sys_graphics_box_fail_msg(errors, "Y Dimensions");
            return;
        }
        if (field_count > 3 && args[3].present) {
            if (args[3].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Argument count");
                return;
            }
            c = vm_sys_graphics_box_arg_value(&args[3], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }
        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Argument count");
                return;
            }
            f = vm_sys_graphics_box_arg_value(&args[4], 0);
            if (f < 0 || f > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, f, 0, max_colour);
                return;
            }
        }

        x_values = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_a, (size_t)xcount * sizeof(int));
        y_values = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_b, (size_t)xcount * sizeof(int));
        for (int i = 0; i < xcount; i++) {
            x_values[i] = vm_sys_graphics_box_arg_value(&args[1], i);
            y_values[i] = vm_sys_graphics_box_arg_value(&args[2], i);
        }
        vm_sys_graphics_draw_polygon_points(x_values, y_values, xcount, c, f, 1);
        return;
    }

    {
        int n = args[0].count;
        int xtot = 0;
        int actual_n = 0;
        int nx = args[1].count;
        int ny = args[2].count;
        int *cc = NULL;
        int *ff = NULL;

        for (int i = 0; i < n; i++) {
            int value = vm_sys_graphics_box_arg_value(&args[0], i);
            if (value == 0) break;
            xtot += value;
            actual_n++;
            if (value < 3 || value > 9999) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid number of vertices");
                return;
            }
        }
        n = actual_n;
        if (nx < xtot) {
            vm_sys_graphics_box_fail_msg(errors, "X Dimensions");
            return;
        }
        if (ny < xtot) {
            vm_sys_graphics_box_fail_msg(errors, "Y Dimensions");
            return;
        }

        cc = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_c, (size_t)((n > 0) ? n : 1) * sizeof(int));
        ff = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_d, (size_t)((n > 0) ? n : 1) * sizeof(int));

        for (int i = 0; i < n; i++) cc[i] = gui_fcolour;
        if (field_count > 3 && args[3].present) {
            if (args[3].count == 1) {
                c = vm_sys_graphics_box_arg_value(&args[3], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                for (int i = 0; i < n; i++) cc[i] = c;
            } else {
                if (args[3].count < n) {
                    vm_sys_graphics_box_fail_msg(errors, "Foreground colour Dimensions");
                    return;
                }
                for (int i = 0; i < n; i++) {
                    cc[i] = vm_sys_graphics_box_arg_value(&args[3], i);
                    if (cc[i] < 0 || cc[i] > max_colour) {
                        int value = cc[i];
                        vm_sys_graphics_box_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                f = vm_sys_graphics_box_arg_value(&args[4], 0);
                if (f < 0 || f > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, f, 0, max_colour);
                    return;
                }
                for (int i = 0; i < n; i++) ff[i] = f;
            } else {
                if (args[4].count < n) {
                    vm_sys_graphics_box_fail_msg(errors, "Background colour Dimensions");
                    return;
                }
                for (int i = 0; i < n; i++) {
                    ff[i] = vm_sys_graphics_box_arg_value(&args[4], i);
                    if (ff[i] < 0 || ff[i] > max_colour) {
                        int value = ff[i];
                        vm_sys_graphics_box_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        for (int i = 0, xstart = 0; i < n; i++) {
            int xcount = vm_sys_graphics_box_arg_value(&args[0], i);
            int *x_values = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_a, (size_t)xcount * sizeof(int));
            int *y_values = (int *)vm_sys_graphics_reserve_scratch(&vm_gfx_int_b, (size_t)xcount * sizeof(int));
            int fill_colour = (field_count > 4 && args[4].present) ? ff[i] : -1;

            for (int j = 0; j < xcount; j++) {
                x_values[j] = vm_sys_graphics_box_arg_value(&args[1], xstart + j);
                y_values[j] = vm_sys_graphics_box_arg_value(&args[2], xstart + j);
            }
            vm_sys_graphics_draw_polygon_points(x_values, y_values, xcount, cc[i], fill_colour, 1);
            xstart += xcount;
        }
    }
}

static int vm_sys_graphics_line_arg_value(const GfxLineArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void vm_sys_graphics_line_fail_msg(const GfxLineErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void vm_sys_graphics_line_fail_range(const GfxLineErrorSink *errors, const char *label,
                                            int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

static int vm_sys_graphics_pixel_arg_value(const GfxPixelArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void vm_sys_graphics_pixel_fail_msg(const GfxPixelErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void vm_sys_graphics_pixel_fail_range(const GfxPixelErrorSink *errors, const char *label,
                                             int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

static void vm_sys_graphics_text_fail_msg(const GfxTextOps *ops, const char *msg) {
    if (ops && ops->fail_msg) ops->fail_msg(ops->ctx, msg);
}

static void vm_sys_graphics_text_fail_range(const GfxTextOps *ops, int value, int min, int max) {
    if (ops && ops->fail_range) ops->fail_range(ops->ctx, value, min, max);
}

static void vm_sys_graphics_plot_scaled_text_pixel(int x, int y, int width, int height,
                                                   int scale, int orientation,
                                                   int col, int row, int sx, int sy, int colour) {
    int px = x;
    int py = y;

    switch (orientation) {
        case ORIENT_INVERTED:
            px = x - (width * scale - 1) + (width - col - 1) * scale + sx;
            py = y - (height * scale - 1) + (height - row - 1) * scale + sy;
            break;
        case ORIENT_CCW90DEG:
            px = x + row * scale + sy;
            py = y - width * scale + (width - col - 1) * scale + sx;
            break;
        case ORIENT_CW90DEG:
            px = x - (height * scale - 1) + (height - row - 1) * scale + sy;
            py = y + col * scale + sx;
            break;
        case ORIENT_VERT:
        case ORIENT_NORMAL:
        default:
            px = x + col * scale + sx;
            py = y + row * scale + sy;
            break;
    }

    DrawPixel(px, py, colour);
}

static void vm_sys_graphics_fill_text_cell(int x, int y, int width, int height,
                                           int scale, int orientation, int colour) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    vm_sys_graphics_plot_scaled_text_pixel(x, y, width, height, scale,
                                                           orientation, col, row, sx, sy, colour);
                }
            }
        }
    }
}

static int vm_sys_graphics_font_has_char(const unsigned char *font, int ch) {
    return font && ch >= font[2] && ch < font[2] + font[3];
}

static const unsigned char *vm_sys_graphics_font_glyph(const unsigned char *font, int ch) {
    int width = font[0];
    int height = font[1];
    return font + 4 + (int)(((ch - font[2]) * height * width) / 8);
}

static int vm_sys_graphics_glyph_bit(const unsigned char *glyph, int width, int height,
                                     int col, int row) {
    int bit_number = row * width + col;
    return (glyph[bit_number / 8] >> (((height * width) - bit_number - 1) % 8)) & 1;
}

static void vm_sys_graphics_draw_text_char(int x, int y, int font, int scale,
                                           int orientation, int fc, int bc, int ch) {
    unsigned char *fp = FontTable[font - 1];
    int width;
    int height;
    int draw_scale = scale;
    const unsigned char *glyph;

    if (font == 6 && (ch == '-' || ch == '+' || ch == '=')) {
        fp = FontTable[0];
        draw_scale = scale * 4;
    }
    if (!fp) return;

    width = fp[0];
    height = fp[1];

    if (bc >= 0) {
        vm_sys_graphics_fill_text_cell(x, y, width, height, draw_scale, orientation, bc);
    }
    if (!vm_sys_graphics_font_has_char(fp, ch)) return;

    glyph = vm_sys_graphics_font_glyph(fp, ch);
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            if (!vm_sys_graphics_glyph_bit(glyph, width, height, col, row)) continue;
            for (int sy = 0; sy < draw_scale; sy++) {
                for (int sx = 0; sx < draw_scale; sx++) {
                    vm_sys_graphics_plot_scaled_text_pixel(x, y, width, height, draw_scale,
                                                           orientation, col, row, sx, sy, fc);
                }
            }
        }
    }
}

static void vm_sys_graphics_render_text(int x, int y, int font, int scale,
                                        int jh, int jv, int jo, int fc, int bc, char *text) {
    unsigned char *fp = FontTable[font - 1];
    int width = fp[0] * scale;
    int height = fp[1] * scale;
    int len = (int)strlen(text);

    if (jo == ORIENT_NORMAL) {
        if (jh == JUSTIFY_CENTER) x -= (len * width) / 2;
        if (jh == JUSTIFY_RIGHT) x -= len * width;
        if (jv == JUSTIFY_MIDDLE) y -= height / 2;
        if (jv == JUSTIFY_BOTTOM) y -= height;
    } else if (jo == ORIENT_VERT) {
        if (jh == JUSTIFY_CENTER) x -= width / 2;
        if (jh == JUSTIFY_RIGHT) x -= width;
        if (jv == JUSTIFY_MIDDLE) y -= (len * height) / 2;
        if (jv == JUSTIFY_BOTTOM) y -= len * height;
    } else if (jo == ORIENT_INVERTED) {
        if (jh == JUSTIFY_CENTER) x += (len * width) / 2;
        if (jh == JUSTIFY_RIGHT) x += len * width;
        if (jv == JUSTIFY_MIDDLE) y += height / 2;
        if (jv == JUSTIFY_BOTTOM) y += height;
    } else if (jo == ORIENT_CCW90DEG) {
        if (jh == JUSTIFY_CENTER) x -= height / 2;
        if (jh == JUSTIFY_RIGHT) x -= height;
        if (jv == JUSTIFY_MIDDLE) y += (len * width) / 2;
        if (jv == JUSTIFY_BOTTOM) y += len * width;
    } else if (jo == ORIENT_CW90DEG) {
        if (jh == JUSTIFY_CENTER) x += height / 2;
        if (jh == JUSTIFY_RIGHT) x += height;
        if (jv == JUSTIFY_MIDDLE) y -= (len * width) / 2;
        if (jv == JUSTIFY_BOTTOM) y -= len * width;
    }

    while (*text) {
        vm_sys_graphics_draw_text_char(x, y, font, scale, jo, fc, bc, (unsigned char)*text++);
        if (jo == ORIENT_NORMAL) x += width;
        else if (jo == ORIENT_VERT) y += height;
        else if (jo == ORIENT_INVERTED) x -= width;
        else if (jo == ORIENT_CCW90DEG) y -= width;
        else if (jo == ORIENT_CW90DEG) y += width;
    }
}

void vm_sys_graphics_box_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                 const GfxBoxErrorSink *errors) {
    int x1 = 0, y1 = 0, width = 0, height = 0, w = 1, c = gui_fcolour, f = -1;
    int wmod = 0, hmod = 0;
    const int max_colour = (int)WHITE;

    if (field_count < 4 || field_count > GFX_BOX_ARG_COUNT) {
        vm_sys_graphics_box_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_BOX_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[0].count != 1 || args[1].count != 1 || args[2].count != 1 || args[3].count != 1) {
            vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
            return;
        }

        x1 = vm_sys_graphics_box_arg_value(&args[0], 0);
        y1 = vm_sys_graphics_box_arg_value(&args[1], 0);
        width = vm_sys_graphics_box_arg_value(&args[2], 0);
        height = vm_sys_graphics_box_arg_value(&args[3], 0);
        wmod = (width > 0) ? -1 : 1;
        hmod = (height > 0) ? -1 : 1;

        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            w = vm_sys_graphics_box_arg_value(&args[4], 0);
            if (w < 0 || w > 100) {
                vm_sys_graphics_box_fail_range(errors, NULL, w, 0, 100);
                return;
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            c = vm_sys_graphics_box_arg_value(&args[5], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            f = vm_sys_graphics_box_arg_value(&args[6], 0);
            if (f < -1 || f > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }

        if (width != 0 && height != 0) {
            DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
        }
        return;
    }

    {
        int i;
        int n;
        int nwidth = 0, nheight = 0, nw = 0, nc = 0, nf = 0;

        if (!args[0].present || !args[1].present || args[0].count <= 1 || args[1].count <= 1) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;

        if (!args[2].present) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[2].count == 1) {
            width = vm_sys_graphics_box_arg_value(&args[2], 0);
            if (width < 1 || width > HRes) {
                vm_sys_graphics_box_fail_range(errors, "Width", width, 1, HRes);
                return;
            }
            nwidth = 1;
        } else {
            nwidth = args[2].count;
            if (nwidth > 1 && nwidth < n) n = nwidth;
            for (i = 0; i < nwidth; i++) {
                width = vm_sys_graphics_box_arg_value(&args[2], i);
                if (width < 1 || width > HRes) {
                    vm_sys_graphics_box_fail_range(errors, "Width", width, 1, HRes);
                    return;
                }
            }
        }

        if (!args[3].present) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[3].count == 1) {
            height = vm_sys_graphics_box_arg_value(&args[3], 0);
            if (height < 1 || height > VRes) {
                vm_sys_graphics_box_fail_range(errors, "Height", height, 1, VRes);
                return;
            }
            nheight = 1;
        } else {
            nheight = args[3].count;
            if (nheight > 1 && nheight < n) n = nheight;
            for (i = 0; i < nheight; i++) {
                height = vm_sys_graphics_box_arg_value(&args[3], i);
                if (height < 1 || height > VRes) {
                    vm_sys_graphics_box_fail_range(errors, "Height", height, 1, VRes);
                    return;
                }
            }
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                w = vm_sys_graphics_box_arg_value(&args[4], 0);
                if (w < 0 || w > 100) {
                    vm_sys_graphics_box_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[4].count;
                if (nw > 1 && nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    w = vm_sys_graphics_box_arg_value(&args[4], i);
                    if (w < 0 || w > 100) {
                        vm_sys_graphics_box_fail_range(errors, NULL, w, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = vm_sys_graphics_box_arg_value(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc > 1 && nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    c = vm_sys_graphics_box_arg_value(&args[5], i);
                    if (c < 0 || c > max_colour) {
                        vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count == 1) {
                f = vm_sys_graphics_box_arg_value(&args[6], 0);
                if (f < 0 || f > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, f, 0, max_colour);
                    return;
                }
                nf = 1;
            } else {
                nf = args[6].count;
                if (nf > 1 && nf < n) n = nf;
                for (i = 0; i < nf; i++) {
                    f = vm_sys_graphics_box_arg_value(&args[6], i);
                    if (f < -1 || f > max_colour) {
                        vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x1 = vm_sys_graphics_box_arg_value(&args[0], i);
            y1 = vm_sys_graphics_box_arg_value(&args[1], i);
            if (nwidth > 1) width = vm_sys_graphics_box_arg_value(&args[2], i);
            if (nheight > 1) height = vm_sys_graphics_box_arg_value(&args[3], i);
            wmod = (width > 0) ? -1 : 1;
            hmod = (height > 0) ? -1 : 1;
            if (nw > 1) w = vm_sys_graphics_box_arg_value(&args[4], i);
            if (nc > 1) c = vm_sys_graphics_box_arg_value(&args[5], i);
            if (nf > 1) f = vm_sys_graphics_box_arg_value(&args[6], i);
            if (width != 0 && height != 0) {
                DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
            }
        }
    }
}

void vm_sys_graphics_rbox_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                                  const GfxBoxErrorSink *errors) {
    int x1 = 0, y1 = 0, width = 0, height = 0, r = 10, c = gui_fcolour, f = -1;
    int wmod = 0, hmod = 0;
    const int max_colour = (int)WHITE;

    if (field_count < 4 || field_count > GFX_BOX_ARG_COUNT) {
        vm_sys_graphics_box_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_BOX_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[0].count != 1 || args[1].count != 1 || args[2].count != 1 || args[3].count != 1) {
            vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
            return;
        }

        x1 = vm_sys_graphics_box_arg_value(&args[0], 0);
        y1 = vm_sys_graphics_box_arg_value(&args[1], 0);
        width = vm_sys_graphics_box_arg_value(&args[2], 0);
        height = vm_sys_graphics_box_arg_value(&args[3], 0);
        wmod = (width > 0) ? -1 : 1;
        hmod = (height > 0) ? -1 : 1;

        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            r = vm_sys_graphics_box_arg_value(&args[4], 0);
            if (r < 0 || r > 100) {
                vm_sys_graphics_box_fail_range(errors, NULL, r, 0, 100);
                return;
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            c = vm_sys_graphics_box_arg_value(&args[5], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count != 1) {
                vm_sys_graphics_box_fail_msg(errors, "Invalid variable");
                return;
            }
            f = vm_sys_graphics_box_arg_value(&args[6], 0);
            if (f < -1 || f > max_colour) {
                vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }

        if (width != 0 && height != 0) {
            vm_sys_graphics_draw_rbox(x1, y1, x1 + width + wmod, y1 + height + hmod, r, c, f);
        }
        return;
    }

    {
        int i;
        int n;
        int nr = 0, nc = 0, nf = 0;

        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present ||
            args[0].count <= 1 || args[1].count <= 1 || args[2].count <= 1 || args[3].count <= 1) {
            vm_sys_graphics_box_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;
        if (args[2].count < n) n = args[2].count;
        if (args[3].count < n) n = args[3].count;

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                r = vm_sys_graphics_box_arg_value(&args[4], 0);
                if (r < 0 || r > 100) {
                    vm_sys_graphics_box_fail_range(errors, NULL, r, 0, 100);
                    return;
                }
                nr = 1;
            } else {
                nr = args[4].count;
                if (nr < n) n = nr;
                for (i = 0; i < nr; i++) {
                    int value = vm_sys_graphics_box_arg_value(&args[4], i);
                    if (value < 0 || value > 100) {
                        vm_sys_graphics_box_fail_range(errors, NULL, value, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = vm_sys_graphics_box_arg_value(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = vm_sys_graphics_box_arg_value(&args[5], i);
                    if (value < 0 || value > max_colour) {
                        vm_sys_graphics_box_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count == 1) {
                f = vm_sys_graphics_box_arg_value(&args[6], 0);
                if (f < -1 || f > max_colour) {
                    vm_sys_graphics_box_fail_range(errors, NULL, f, -1, max_colour);
                    return;
                }
                nf = 1;
            } else {
                nf = args[6].count;
                if (nf < n) n = nf;
                for (i = 0; i < nf; i++) {
                    int value = vm_sys_graphics_box_arg_value(&args[6], i);
                    if (value < -1 || value > max_colour) {
                        vm_sys_graphics_box_fail_range(errors, NULL, value, -1, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x1 = vm_sys_graphics_box_arg_value(&args[0], i);
            y1 = vm_sys_graphics_box_arg_value(&args[1], i);
            width = vm_sys_graphics_box_arg_value(&args[2], i);
            height = vm_sys_graphics_box_arg_value(&args[3], i);
            wmod = (width > 0) ? -1 : 1;
            hmod = (height > 0) ? -1 : 1;
            if (nr > 1) r = vm_sys_graphics_box_arg_value(&args[4], i);
            if (nc > 1) c = vm_sys_graphics_box_arg_value(&args[5], i);
            if (nf > 1) f = vm_sys_graphics_box_arg_value(&args[6], i);
            if (width != 0 && height != 0) {
                vm_sys_graphics_draw_rbox(x1, y1, x1 + width + wmod, y1 + height + hmod, r, c, f);
            }
        }
    }
}

void vm_sys_graphics_cls_execute(int has_arg, const GfxClsArg *arg, const GfxClsOps *ops) {
    int use_default = 1;
    int colour = 0;
    const int max_colour = (int)WHITE;

    if (has_arg) {
        if (!arg || !arg->get_int) {
            if (ops && ops->fail_msg) ops->fail_msg(ops->ctx, "Argument count");
            return;
        }
        colour = arg->get_int(arg->ctx);
        if (colour < 0 || colour > max_colour) {
            if (ops && ops->fail_range) ops->fail_range(ops->ctx, colour, 0, max_colour);
            return;
        }
        use_default = 0;
    }

    if (ops && ops->do_clear) ops->do_clear(ops->ctx, use_default, colour);
    CurrentX = 0;
    CurrentY = 0;
}

void vm_sys_graphics_line_execute(GfxLineMode mode, const GfxLineArg *args, int field_count,
                                  const GfxLineErrorSink *errors) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 1, c = gui_fcolour;
    const int max_colour = (int)WHITE;

    if (field_count < 2 || field_count > GFX_LINE_ARG_COUNT) {
        vm_sys_graphics_line_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_LINE_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || args[0].count != 1 || args[1].count != 1) {
            vm_sys_graphics_line_fail_msg(errors, "Argument count");
            return;
        }

        x1 = vm_sys_graphics_line_arg_value(&args[0], 0);
        y1 = vm_sys_graphics_line_arg_value(&args[1], 0);

        if (field_count > 2 && args[2].present) {
            if (args[2].count != 1) {
                vm_sys_graphics_line_fail_msg(errors, "Argument count");
                return;
            }
            x2 = vm_sys_graphics_line_arg_value(&args[2], 0);
        } else {
            x2 = CurrentX;
            CurrentX = x1;
        }

        if (field_count > 3 && args[3].present) {
            if (args[3].count != 1) {
                vm_sys_graphics_line_fail_msg(errors, "Argument count");
                return;
            }
            y2 = vm_sys_graphics_line_arg_value(&args[3], 0);
        } else {
            y2 = CurrentY;
            CurrentY = y1;
        }

        if (x1 == CurrentX && y1 == CurrentY) {
            CurrentX = x2;
            CurrentY = y2;
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                vm_sys_graphics_line_fail_msg(errors, "Argument count");
                return;
            }
            w = vm_sys_graphics_line_arg_value(&args[4], 0);
            if (w < -100 || w > 100) {
                vm_sys_graphics_line_fail_range(errors, NULL, w, 0, 100);
                return;
            }
            if (!w) return;
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                vm_sys_graphics_line_fail_msg(errors, "Argument count");
                return;
            }
            c = vm_sys_graphics_line_arg_value(&args[5], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_line_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }

        DrawLine(x1, y1, x2, y2, w, c);
        return;
    }

    {
        int i;
        int n;
        int nw = 0, nc = 0;

        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present ||
            args[0].count <= 1 || args[1].count <= 1 || args[2].count <= 1 || args[3].count <= 1) {
            vm_sys_graphics_line_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;
        if (args[2].count < n) n = args[2].count;
        if (args[3].count < n) n = args[3].count;

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                w = vm_sys_graphics_line_arg_value(&args[4], 0);
                if (w < -100 || w > 100) {
                    vm_sys_graphics_line_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[4].count;
                if (nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    int value = vm_sys_graphics_line_arg_value(&args[4], i);
                    if (value < -100 || value > 100) {
                        vm_sys_graphics_line_fail_range(errors, NULL, value, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = vm_sys_graphics_line_arg_value(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_line_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = vm_sys_graphics_line_arg_value(&args[5], i);
                    if (value < 0 || value > max_colour) {
                        vm_sys_graphics_line_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x1 = vm_sys_graphics_line_arg_value(&args[0], i);
            y1 = vm_sys_graphics_line_arg_value(&args[1], i);
            x2 = vm_sys_graphics_line_arg_value(&args[2], i);
            y2 = vm_sys_graphics_line_arg_value(&args[3], i);
            if (nw > 1) w = vm_sys_graphics_line_arg_value(&args[4], i);
            if (nc > 1) c = vm_sys_graphics_line_arg_value(&args[5], i);
            if (w) DrawLine(x1, y1, x2, y2, w, c);
        }
    }
}

void vm_sys_graphics_pixel_execute(GfxPixelMode mode, const GfxPixelArg *args, int field_count,
                                   const GfxPixelErrorSink *errors) {
    int x = 0, y = 0, c = gui_fcolour;
    const int max_colour = (int)WHITE;

    if (field_count < 2 || field_count > GFX_PIXEL_ARG_COUNT) {
        vm_sys_graphics_pixel_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_PIXEL_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || args[0].count != 1 || args[1].count != 1) {
            vm_sys_graphics_pixel_fail_msg(errors, "Argument count");
            return;
        }

        x = vm_sys_graphics_pixel_arg_value(&args[0], 0);
        y = vm_sys_graphics_pixel_arg_value(&args[1], 0);
        if (field_count > 2 && args[2].present) {
            if (args[2].count != 1) {
                vm_sys_graphics_pixel_fail_msg(errors, "Argument count");
                return;
            }
            c = vm_sys_graphics_pixel_arg_value(&args[2], 0);
            if (c < -1 || c > max_colour) {
                vm_sys_graphics_pixel_fail_range(errors, NULL, c, -1, max_colour);
                return;
            }
        }

        if (c != -1) DrawPixel(x, y, c);
        else {
            CurrentX = x;
            CurrentY = y;
        }
        return;
    }

    {
        int i;
        int n;
        int nc = 0;

        if (!args[0].present || !args[1].present || args[0].count <= 1 || args[1].count <= 1) {
            vm_sys_graphics_pixel_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;

        if (field_count > 2 && args[2].present) {
            if (args[2].count == 1) {
                c = vm_sys_graphics_pixel_arg_value(&args[2], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_pixel_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[2].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = vm_sys_graphics_pixel_arg_value(&args[2], i);
                    if (value < 0 || value > max_colour) {
                        vm_sys_graphics_pixel_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x = vm_sys_graphics_pixel_arg_value(&args[0], i);
            y = vm_sys_graphics_pixel_arg_value(&args[1], i);
            if (nc > 1) c = vm_sys_graphics_pixel_arg_value(&args[2], i);
            DrawPixel(x, y, c);
        }
    }
}

int vm_sys_graphics_read_pixel(int x, int y) {
    if (HRes <= 0 || VRes <= 0) error("Display not configured");
    return (int)hal_display_pixel_read(x, y);
}

void vm_sys_graphics_service(void) {
    hal_vm_framebuffer_service();
}

void vm_sys_graphics_framebuffer_create(int fast) { hal_vm_framebuffer_create(fast); }
void vm_sys_graphics_framebuffer_layer(int has_colour, int colour) {
    hal_vm_framebuffer_layer(has_colour, colour);
}
void vm_sys_graphics_framebuffer_write(char which) { hal_vm_framebuffer_write(which); }
void vm_sys_graphics_framebuffer_close(char which) { hal_vm_framebuffer_close(which); }
void vm_sys_graphics_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms) {
    hal_vm_framebuffer_merge(has_colour, colour, mode, has_rate, rate_ms);
}
void vm_sys_graphics_framebuffer_sync(void) { hal_vm_framebuffer_sync(); }
void vm_sys_graphics_framebuffer_wait(void) { hal_vm_framebuffer_wait(); }
void vm_sys_graphics_framebuffer_copy(char from, char to, int background) {
    hal_vm_framebuffer_copy(from, to, background);
}

void vm_sys_graphics_text_execute(const GfxTextArg *args, int field_count, const GfxTextOps *ops) {
    int x, y, font, scale, fc, bc;
    int jh = 0, jv = 0, jo = 0;
    const int max_colour = (int)WHITE;
    char *text;
    char *just;

    if (field_count < 3 || field_count > GFX_TEXT_ARG_COUNT) {
        vm_sys_graphics_text_fail_msg(ops, "Argument count");
        return;
    }
    if (!args[0].present || !args[1].present || !args[2].present ||
        !args[0].get_int || !args[1].get_int || !args[2].get_str) {
        vm_sys_graphics_text_fail_msg(ops, "Argument count");
        return;
    }

    x = args[0].get_int(args[0].ctx);
    y = args[1].get_int(args[1].ctx);
    text = args[2].get_str(args[2].ctx);

    if (field_count > 3 && args[3].present) {
        if (!args[3].get_str) {
            vm_sys_graphics_text_fail_msg(ops, "TEXT requires string arguments");
            return;
        }
        just = args[3].get_str(args[3].ctx);
        if (!GetJustification((char *)just, &jh, &jv, &jo)) {
            vm_sys_graphics_text_fail_msg(ops, "Justification");
            return;
        }
    }

    if (ops && ops->get_defaults) ops->get_defaults(ops->ctx, &font, &scale, &fc, &bc);
    else {
        font = (gui_font >> 4) + 1;
        scale = gui_font & 0x0F;
        fc = gui_fcolour;
        bc = gui_bcolour;
    }
    if (scale == 0) scale = 1;

    if (field_count > 4 && args[4].present) {
        if (!args[4].get_int) {
            vm_sys_graphics_text_fail_msg(ops, "Argument count");
            return;
        }
        font = args[4].get_int(args[4].ctx);
        if (font < 1 || font > FONT_TABLE_SIZE) {
            vm_sys_graphics_text_fail_range(ops, font, 1, FONT_TABLE_SIZE);
            return;
        }
    }
    if (ops && ops->font_valid && !ops->font_valid(ops->ctx, font)) {
        vm_sys_graphics_text_fail_msg(ops, "Invalid font");
        return;
    }

    if (field_count > 5 && args[5].present) {
        if (!args[5].get_int) {
            vm_sys_graphics_text_fail_msg(ops, "Argument count");
            return;
        }
        scale = args[5].get_int(args[5].ctx);
        if (scale < 1 || scale > 15) {
            vm_sys_graphics_text_fail_range(ops, scale, 1, 15);
            return;
        }
    }
    if (field_count > 6 && args[6].present) {
        if (!args[6].get_int) {
            vm_sys_graphics_text_fail_msg(ops, "Argument count");
            return;
        }
        fc = args[6].get_int(args[6].ctx);
        if (fc < 0 || fc > max_colour) {
            vm_sys_graphics_text_fail_range(ops, fc, 0, max_colour);
            return;
        }
    }
    if (field_count > 7 && args[7].present) {
        if (!args[7].get_int) {
            vm_sys_graphics_text_fail_msg(ops, "Argument count");
            return;
        }
        bc = args[7].get_int(args[7].ctx);
        if (bc < -1 || bc > max_colour) {
            vm_sys_graphics_text_fail_range(ops, bc, -1, max_colour);
            return;
        }
    }

    vm_sys_graphics_render_text(x, y, font, scale, jh, jv, jo, fc, bc, text);
}

static void vm_sys_graphics_hline(int x0, int x1, int y, int f, int ints_per_line, uint32_t *br) {
    uint32_t w1, xx1, w0, xx0, x, xn, i;
    const uint32_t a[] = {
        0xFFFFFFFF, 0x7FFFFFFF, 0x3FFFFFFF, 0x1FFFFFFF, 0x0FFFFFFF, 0x07FFFFFF, 0x03FFFFFF, 0x01FFFFFF,
        0x00FFFFFF, 0x007FFFFF, 0x003FFFFF, 0x001FFFFF, 0x000FFFFF, 0x0007FFFF, 0x0003FFFF, 0x0001FFFF,
        0x0000FFFF, 0x00007FFF, 0x00003FFF, 0x00001FFF, 0x00000FFF, 0x000007FF, 0x000003FF, 0x000001FF,
        0x000000FF, 0x0000007F, 0x0000003F, 0x0000001F, 0x0000000F, 0x00000007, 0x00000003, 0x00000001
    };
    const uint32_t b[] = {
        0x80000000, 0xC0000000, 0xE0000000, 0xF0000000, 0xF8000000, 0xFC000000, 0xFE000000, 0xFF000000,
        0xFF800000, 0xFFC00000, 0xFFE00000, 0xFFF00000, 0xFFF80000, 0xFFFC0000, 0xFFFE0000, 0xFFFF0000,
        0xFFFF8000, 0xFFFFC000, 0xFFFFE000, 0xFFFFF000, 0xFFFFF800, 0xFFFFFC00, 0xFFFFFE00, 0xFFFFFF00,
        0xFFFFFF80, 0xFFFFFFC0, 0xFFFFFFE0, 0xFFFFFFF0, 0xFFFFFFF8, 0xFFFFFFFC, 0xFFFFFFFE, 0xFFFFFFFF
    };

    w0 = y * ints_per_line;
    xx0 = 0;
    w1 = y * ints_per_line + x1 / 32;
    xx1 = x1 & 0x1F;
    w0 = y * ints_per_line + x0 / 32;
    xx0 = x0 & 0x1F;
    w1 = y * ints_per_line + x1 / 32;
    xx1 = x1 & 0x1F;

    if (w1 == w0) {
        x = a[xx0] & b[xx1];
        xn = ~x;
        if (f) br[w0] |= x;
        else br[w0] &= xn;
    } else {
        if (w1 - w0 > 1) {
            for (i = w0 + 1; i < w1; i++) {
                br[i] = f ? 0xFFFFFFFF : 0;
            }
        }
        x = ~a[xx0];
        br[w0] &= x;
        x = ~x;
        if (f) br[w0] |= x;
        x = ~b[xx1];
        br[w1] &= x;
        x = ~x;
        if (f) br[w1] |= x;
    }
}

static void vm_sys_graphics_fill_circle_mask(int x, int y, int radius, int r, int fill,
                                    int ints_per_line, uint32_t *br,
                                    MMFLOAT aspect, MMFLOAT aspect2) {
    int a, b, P;
    int A, B, asp;

    x = (int)((MMFLOAT)r * aspect) + radius;
    y = r + radius;
    a = 0;
    b = radius;
    P = 1 - radius;
    asp = aspect2 * (MMFLOAT)(1 << 10);
    do {
        A = (a * asp) >> 10;
        B = (b * asp) >> 10;
        vm_sys_graphics_hline(x - A - radius, x + A - radius, y + b - radius, fill, ints_per_line, br);
        vm_sys_graphics_hline(x - A - radius, x + A - radius, y - b - radius, fill, ints_per_line, br);
        vm_sys_graphics_hline(x - B - radius, x + B - radius, y + a - radius, fill, ints_per_line, br);
        vm_sys_graphics_hline(x - B - radius, x + B - radius, y - a - radius, fill, ints_per_line, br);
        if (P < 0) P += 3 + 2 * a++;
        else P += 5 + 2 * (a++ - b--);
    } while (a <= b);
}

static void vm_sys_graphics_draw_circle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect) {
    int a, b, P;
    int A, B;
    int asp;
    MMFLOAT aspect2;

    if (w > 1) {
        if (fill >= 0) {
            vm_sys_graphics_draw_circle(x, y, radius, 0, c, c, aspect);
            aspect2 = ((aspect * (MMFLOAT)radius) - (MMFLOAT)w) / ((MMFLOAT)(radius - w));
            vm_sys_graphics_draw_circle(x, y, radius - w, 0, fill, fill, aspect2);
        } else {
            int r1 = radius - w, r2 = radius, xs = -1, xi = 0, i, j, k, m, ll = radius;
            size_t words;
            if (aspect > 1.0) ll = (int)((MMFLOAT)radius * aspect);
            int ints_per_line = RoundUptoInt((ll * 2) + 1) / 32;
            uint32_t *br;

            words = (size_t)(ints_per_line + 1) * (size_t)((r2 * 2) + 1);
            br = (uint32_t *)vm_sys_graphics_reserve_scratch(&vm_gfx_mask_a, words * sizeof(uint32_t));
            vm_sys_graphics_fill_circle_mask(x, y, r2, r2, 1, ints_per_line, br, aspect, aspect);
            aspect2 = ((aspect * (MMFLOAT)r2) - (MMFLOAT)w) / ((MMFLOAT)r1);
            vm_sys_graphics_fill_circle_mask(x, y, r1, r2, 0, ints_per_line, br, aspect, aspect2);
            x = (int)((MMFLOAT)x + (MMFLOAT)r2 * (1.0 - aspect));
            for (j = 0; j < r2 * 2 + 1; j++) {
                for (i = 0; i < ints_per_line; i++) {
                    k = br[i + j * ints_per_line];
                    for (m = 0; m < 32; m++) {
                        if (xs == -1 && (k & 0x80000000)) {
                            xs = m;
                            xi = i;
                        }
                        if (xs != -1 && !(k & 0x80000000)) {
                            DrawRectangle(x - r2 + xs + xi * 32, y - r2 + j,
                                          x - r2 + m + i * 32, y - r2 + j, c);
                            xs = -1;
                        }
                        k <<= 1;
                    }
                }
                if (xs != -1) {
                    DrawRectangle(x - r2 + xs + xi * 32, y - r2 + j,
                                  x - r2 + m + i * 32, y - r2 + j, c);
                    xs = -1;
                }
            }
        }
    } else {
        int w1 = w, r1 = radius;
        if (fill >= 0) {
            while (w >= 0 && radius > 0) {
                a = 0;
                b = radius;
                P = 1 - radius;
                asp = aspect * (MMFLOAT)(1 << 10);
                do {
                    A = (a * asp) >> 10;
                    B = (b * asp) >> 10;
                    DrawRectangle(x - A, y + b, x + A, y + b, fill);
                    DrawRectangle(x - A, y - b, x + A, y - b, fill);
                    DrawRectangle(x - B, y + a, x + B, y + a, fill);
                    DrawRectangle(x - B, y - a, x + B, y - a, fill);
                    if (P < 0) P += 3 + 2 * a++;
                    else P += 5 + 2 * (a++ - b--);
                } while (a <= b);
                w--;
                radius--;
            }
        }
        if (c != fill) {
            w = w1;
            radius = r1;
            while (w >= 0 && radius > 0) {
                a = 0;
                b = radius;
                P = 1 - radius;
                asp = aspect * (MMFLOAT)(1 << 10);
                do {
                    A = (a * asp) >> 10;
                    B = (b * asp) >> 10;
                    if (w) {
                        DrawPixel(A + x, b + y, c);
                        DrawPixel(B + x, a + y, c);
                        DrawPixel(x - A, b + y, c);
                        DrawPixel(x - B, a + y, c);
                        DrawPixel(B + x, y - a, c);
                        DrawPixel(A + x, y - b, c);
                        DrawPixel(x - A, y - b, c);
                        DrawPixel(x - B, y - a, c);
                    }
                    if (P < 0) P += 3 + 2 * a++;
                    else P += 5 + 2 * (a++ - b--);
                } while (a <= b);
                w--;
                radius--;
            }
        }
    }
    if (Option.Refresh) Display_Refresh();
}

void vm_sys_graphics_circle_execute(GfxCircleMode mode, const GfxCircleArg *args, int field_count,
                           const GfxCircleErrorSink *errors) {
    int x = 0, y = 0, r = 0, w = 1, c = gui_fcolour, f = -1;
    MMFLOAT a = 1;
    const int max_colour = (int)WHITE;

    if (field_count < 3 || field_count > GFX_CIRCLE_ARG_COUNT) {
        vm_sys_graphics_circle_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_CIRCLE_MODE_SCALAR) {
        int save_refresh;

        if (!args[0].present || !args[1].present || !args[2].present) {
            vm_sys_graphics_circle_fail_msg(errors, "Argument count");
            return;
        }
        if (args[0].count != 1 || args[1].count != 1 || args[2].count != 1) {
            vm_sys_graphics_circle_fail_msg(errors, "Invalid variable");
            return;
        }

        x = vm_sys_graphics_circle_arg_int(&args[0], 0);
        y = vm_sys_graphics_circle_arg_int(&args[1], 0);
        r = vm_sys_graphics_circle_arg_int(&args[2], 0);

        if (field_count > 3 && args[3].present) {
            if (args[3].count != 1) {
                vm_sys_graphics_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            w = vm_sys_graphics_circle_arg_int(&args[3], 0);
            if (w < 0 || w > 100) {
                vm_sys_graphics_circle_fail_range(errors, NULL, w, 0, 100);
                return;
            }
        }
        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                vm_sys_graphics_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            a = vm_sys_graphics_circle_arg_float(&args[4], 0);
        }
        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                vm_sys_graphics_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            c = vm_sys_graphics_circle_arg_int(&args[5], 0);
            if (c < 0 || c > max_colour) {
                vm_sys_graphics_circle_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }
        if (field_count > 6 && args[6].present) {
            if (args[6].count != 1) {
                vm_sys_graphics_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            f = vm_sys_graphics_circle_arg_int(&args[6], 0);
            if (f < -1 || f > max_colour) {
                vm_sys_graphics_circle_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }

        save_refresh = Option.Refresh;
        Option.Refresh = 0;
        vm_sys_graphics_draw_circle(x, y, r, w, c, f, a);
        Option.Refresh = save_refresh;
        return;
    }

    {
        int i;
        int n;
        int nw = 0, na = 0, nc = 0, nf = 0;
        int save_refresh;

        if (!args[0].present || !args[1].present || !args[2].present ||
            args[0].count <= 1 || args[1].count <= 1 || args[2].count <= 1) {
            vm_sys_graphics_circle_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;
        if (args[2].count < n) n = args[2].count;

        if (field_count > 3 && args[3].present) {
            if (args[3].count == 1) {
                w = vm_sys_graphics_circle_arg_int(&args[3], 0);
                if (w < 0 || w > 100) {
                    vm_sys_graphics_circle_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[3].count;
                if (nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    int value = vm_sys_graphics_circle_arg_int(&args[3], i);
                    if (value < 0 || value > 100) {
                        vm_sys_graphics_circle_fail_range(errors, NULL, value, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                a = vm_sys_graphics_circle_arg_float(&args[4], 0);
                na = 1;
            } else {
                na = args[4].count;
                if (na < n) n = na;
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = vm_sys_graphics_circle_arg_int(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    vm_sys_graphics_circle_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = vm_sys_graphics_circle_arg_int(&args[5], i);
                    if (value < 0 || value > max_colour) {
                        vm_sys_graphics_circle_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count > 6 && args[6].present) {
            if (args[6].count == 1) {
                f = vm_sys_graphics_circle_arg_int(&args[6], 0);
                if (f < -1 || f > max_colour) {
                    vm_sys_graphics_circle_fail_range(errors, NULL, f, -1, max_colour);
                    return;
                }
                nf = 1;
            } else {
                nf = args[6].count;
                if (nf < n) n = nf;
                for (i = 0; i < nf; i++) {
                    int value = vm_sys_graphics_circle_arg_int(&args[6], i);
                    if (value < 0 || value > max_colour) {
                        vm_sys_graphics_circle_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        save_refresh = Option.Refresh;
        Option.Refresh = 0;
        for (i = 0; i < n; i++) {
            x = vm_sys_graphics_circle_arg_int(&args[0], i);
            y = vm_sys_graphics_circle_arg_int(&args[1], i);
            r = vm_sys_graphics_circle_arg_int(&args[2], i) - 1;
            if (nw > 1) w = vm_sys_graphics_circle_arg_int(&args[3], i);
            if (na > 1) a = vm_sys_graphics_circle_arg_float(&args[4], i);
            if (nc > 1) c = vm_sys_graphics_circle_arg_int(&args[5], i);
            if (nf > 1) f = vm_sys_graphics_circle_arg_int(&args[6], i);
            vm_sys_graphics_draw_circle(x, y, r, w, c, f, a);
        }
        Option.Refresh = save_refresh;
    }
}
