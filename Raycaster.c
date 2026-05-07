/***********************************************************************************************************************
PicoMite MMBasic

Raycaster.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

/*
 * DDA Raycaster — renders a Wolfenstein 3D-style view into a 4bpp framebuffer.
 *
 * Wall textures are taken from the Turtle fill_patterns[] (8x8, 1-bit).
 * The framebuffer uses RGB121 encoding (4 bits per pixel), packed two pixels
 * per byte: even pixel in the low nibble, odd pixel in the high nibble.
 * This matches the PicoMite Mode 2 / RGB121 pixel layout.
 *
 * All state is heap-allocated via GetMemory() and freed with ray_close().
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Raycaster.h"
#include "Turtle.h" /* fill_patterns[], NUM_PATTERNS */
#include "RGB121.h" /* RGB121() inline, colours[] */
#include "Draw.h"   /* spritebuff[], sprite_transparent, MAXBLITBUF */
#include <math.h>

#ifndef F_PI
#define F_PI 3.14159265358979323846f
#endif

/* ============================================================================
 * Module state — single pointer, heap-allocated
 * ============================================================================ */
static RayState *rstate = NULL;

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/* Allocate and zero-fill raycaster state */
static void ray_alloc(void)
{
    if (rstate != NULL)
        return;
    rstate = (RayState *)GetMemory(sizeof(RayState));
    memset(rstate, 0, sizeof(RayState));
    rstate->cam_fov = 60.0f;
    rstate->floor_fg = 0; /* BLACK */
    rstate->floor_bg = 0;
    rstate->ceil_fg = 7; /* CYAN */
    rstate->ceil_bg = 7;
    rstate->floor_pat = 0; /* Solid */
    rstate->ceil_pat = 0;

    /* Initialise wall definitions with backwards-compatible defaults:
     * Types 1-15:  green walls  (fg=GREEN, bg=MIDGREEN, pat=type-1)
     * Types 16-31: brown doors  (fg=YELLOW, bg=BROWN, pat=type-1, is_door=1) */
    for (int i = 1; i < RAY_MAX_WALLDEFS; i++)
    {
        rstate->walldefs[i].pattern = (i - 1) % NUM_PATTERNS;
        if (i >= 16)
        {
            rstate->walldefs[i].fg = 14; /* YELLOW */
            rstate->walldefs[i].bg = 12; /* BROWN  */
            rstate->walldefs[i].is_door = 1;
        }
        else
        {
            rstate->walldefs[i].fg = 6; /* GREEN    */
            rstate->walldefs[i].bg = 4; /* MIDGREEN */
            rstate->walldefs[i].is_door = 0;
        }
    }
}

/* Free per-column arrays */
static void ray_free_columns(void)
{
    if (rstate == NULL)
        return;
    if (rstate->col_dist)
    {
        FreeMemory((unsigned char *)rstate->col_dist);
        rstate->col_dist = NULL;
    }
    if (rstate->col_wall)
    {
        FreeMemory((unsigned char *)rstate->col_wall);
        rstate->col_wall = NULL;
    }
    rstate->num_cols = 0;
}

/* Ensure per-column arrays match current HRes */
static void ray_ensure_columns(int ncols)
{
    if (rstate->num_cols == ncols)
        return;
    ray_free_columns();
    rstate->col_dist = (float *)GetMemory(ncols * sizeof(float));
    rstate->col_wall = (uint8_t *)GetMemory(ncols);
    rstate->num_cols = ncols;
}

/* Write a 4-bit pixel into a packed buffer (same layout as RGB121 framebuffer).
 * Even pixels → low nibble, odd pixels → high nibble.
 * buf is byte array of size (hres * vres) / 2.
 */
static inline void ray_putpixel(uint8_t *buf, int x, int y, int hres, uint8_t col4)
{
    int offset = y * (hres >> 1) + (x >> 1);
    if (x & 1)
    {
        buf[offset] = (buf[offset] & 0x0F) | (col4 << 4);
    }
    else
    {
        buf[offset] = (buf[offset] & 0xF0) | (col4 & 0x0F);
    }
}

/* Auto-dim a 4-bit RGB121 colour by decrementing the green channel.
 * RGB121 bit layout: bit3=R, bit2-1=G(2bits), bit0=B
 * Used for Y-side walls to create a depth cue (darker than X-side).
 */
static inline uint8_t ray_dim_colour(uint8_t c)
{
    uint8_t g = (c >> 1) & 3;
    if (g > 0)
        g--;
    return (c & 0x09) | (g << 1); /* preserve R and B, replace G */
}

/* ============================================================================
 * Door helpers
 * ============================================================================ */

/* Get the door offset for a map cell, or 0.0 if not an active door slot */
static float ray_get_door_offset(int mx, int my)
{
    if (rstate == NULL)
        return 0.0f;
    for (int i = 0; i < RAY_MAX_DOORS; i++)
    {
        if (rstate->doors[i].active &&
            rstate->doors[i].map_x == mx &&
            rstate->doors[i].map_y == my)
            return rstate->doors[i].offset;
    }
    return 0.0f;
}

/* Check if a ray passes through the open portion of a sliding door.
 * Returns 1 if the ray passes through, 0 if it hits the door.
 * Must only be called for cells whose WallDef has is_door=1. */
static int ray_door_check_pass(int mx, int my, int side,
                               float pos_x, float pos_y,
                               float ray_dx, float ray_dy,
                               float side_x, float side_y,
                               float delta_x, float delta_y)
{
    float door_off = ray_get_door_offset(mx, my);
    if (door_off <= 0.0f)
        return 0; /* fully closed */
    if (door_off >= 1.0f)
        return 1; /* fully open */
    float perp = (side == 0) ? (side_x - delta_x) : (side_y - delta_y);
    if (perp < 0.001f)
        perp = 0.001f;
    float frac = (side == 0) ? (pos_y + perp * ray_dy) : (pos_x + perp * ray_dx);
    frac -= floorf(frac);
    return (frac < door_off) ? 1 : 0;
}

/* Check if a map cell blocks movement (collision detection).
 * Returns 1 if blocked, 0 if passable (empty or fully-open door). */
static inline int ray_cell_blocks(uint8_t *m, int mw, int x, int y)
{
    uint8_t wt = m[y * mw + x];
    if (wt == 0)
        return 0;
    if (rstate->walldefs[wt].is_door && ray_get_door_offset(x, y) >= 1.0f)
        return 0;
    return 1;
}

/* Textured vertical line draw.
 * wall_type selects a WallDef entry for pattern, fg and bg colours.
 * tex_x is the horizontal texture coordinate (0-7).
 * The pattern bit selects between fg and bg.
 * Y-side walls are auto-dimmed (green channel decremented) for depth cueing.
 */
static void ray_vline_textured(uint8_t *buf, int x, int y_start, int y_end,
                               int hres, int vres,
                               int wall_type, int tex_x, int side,
                               float wall_height_f)
{
    if (y_start > y_end)
        return;

    int half_hres = hres >> 1;
    int offset_base = x >> 1;
    int odd = x & 1;

    /* Look up wall definition */
    WallDef *wd = &rstate->walldefs[wall_type];
    int pat_idx = wd->pattern;
    if (pat_idx < 0)
        pat_idx = 0;
    if (pat_idx >= NUM_PATTERNS)
        pat_idx = NUM_PATTERNS - 1;

    /* Select colour pair: X-side uses definition colours, Y-side auto-dims */
    uint8_t fg_col, bg_col;
    if (side == 0)
    {
        fg_col = wd->fg;
        bg_col = wd->bg;
    }
    else
    {
        fg_col = ray_dim_colour(wd->fg);
        bg_col = ray_dim_colour(wd->bg);
    }

    /* Map screen Y range to texture Y (0-7, tiled 2x per wall height) */
    int wall_screen_h = y_end - y_start + 1;

    for (int y = y_start; y <= y_end; y++)
    {
        /* Texture Y coordinate — tile pattern 2x vertically */
        int tex_y = (((y - y_start) * 16) / wall_screen_h) & 7;
        if (tex_y > 7)
            tex_y = 7;

        /* Look up pattern bit */
        uint8_t pattern_row = fill_patterns[pat_idx][tex_y];
        uint8_t col4 = (pattern_row & (0x80 >> tex_x)) ? fg_col : bg_col;

        int offset = y * half_hres + offset_base;
        if (odd)
        {
            buf[offset] = (buf[offset] & 0x0F) | (col4 << 4);
        }
        else
        {
            buf[offset] = (buf[offset] & 0xF0) | (col4 & 0x0F);
        }
    }
}

/* ============================================================================
 * Sprite rendering — depth-sorted billboards drawn after walls
 * ============================================================================ */

static void ray_render_sprites(uint8_t *buf, int hres, int vres)
{
    if (rstate == NULL || rstate->col_dist == NULL)
        return;

    /* Collect active sprites and compute squared distances */
    int n = 0;
    int order[RAY_MAX_SPRITES];
    float sdist[RAY_MAX_SPRITES];
    float px = rstate->cam_x, py = rstate->cam_y;

    for (int i = 0; i < RAY_MAX_SPRITES; i++)
    {
        if (!rstate->sprites[i].active)
            continue;
        float dx = rstate->sprites[i].x - px;
        float dy = rstate->sprites[i].y - py;
        sdist[n] = dx * dx + dy * dy;
        order[n] = i;
        n++;
    }
    if (n == 0)
        return;

    /* Sort furthest-first (insertion sort, max 32 elements) */
    for (int i = 1; i < n; i++)
    {
        float kd = sdist[i];
        int ki = order[i];
        int j = i - 1;
        while (j >= 0 && sdist[j] < kd)
        {
            sdist[j + 1] = sdist[j];
            order[j + 1] = order[j];
            j--;
        }
        sdist[j + 1] = kd;
        order[j + 1] = ki;
    }

    /* Camera vectors */
    float angle_rad = rstate->cam_angle * (F_PI / 180.0f);
    float dir_x = cosf(angle_rad);
    float dir_y = sinf(angle_rad);
    float plane_len = tanf(rstate->cam_fov * 0.5f * (F_PI / 180.0f));
    float plane_x = -dir_y * plane_len;
    float plane_y = dir_x * plane_len;

    /* Inverse determinant of the 2x2 camera matrix */
    float inv_det = 1.0f / (plane_x * dir_y - dir_x * plane_y);
    int half_hres = hres >> 1;
    int half_vres = vres >> 1;
    int row_bytes = hres >> 1;

    uint8_t trans = sprite_transparent;

    for (int s = 0; s < n; s++)
    {
        RaySprite *sp = &rstate->sprites[order[s]];
        int snum = sp->spritenum;
        if (snum < 1 || snum > MAXBLITBUF)
            continue;
        if (spritebuff[snum] == NULL || spritebuff[snum]->spritebuffptr == NULL)
            continue;

        int spr_img_w = spritebuff[snum]->w;
        int spr_img_h = spritebuff[snum]->h;
        char *spr_data = spritebuff[snum]->spritebuffptr;

        float sx = sp->x - px;
        float sy = sp->y - py;

        /* Transform sprite position to camera space */
        float tx = inv_det * (dir_y * sx - dir_x * sy);
        float ty = inv_det * (-plane_y * sx + plane_x * sy);

        if (ty <= 0.1f)
            continue; /* behind camera */

        int scr_x = (int)(half_hres * (1.0f + tx / ty));
        /* Screen height based on sprite image aspect ratio */
        int spr_scr_h = (int)(vres / ty);
        if (spr_scr_h < 1)
            continue;
        if (spr_scr_h > vres * 4)
            spr_scr_h = vres * 4;
        int spr_scr_w = (spr_scr_h * spr_img_w) / spr_img_h;
        if (spr_scr_w < 1)
            spr_scr_w = 1;

        int y0 = half_vres - spr_scr_h / 2;
        int x0 = scr_x - spr_scr_w / 2;
        int x1 = x0 + spr_scr_w - 1;
        int y1 = y0 + spr_scr_h - 1;

        /* Clamp to screen bounds */
        int cx0 = (x0 < 0) ? 0 : x0;
        int cx1 = (x1 >= hres) ? hres - 1 : x1;
        int cy0 = (y0 < 0) ? 0 : y0;
        int cy1 = (y1 >= vres) ? vres - 1 : y1;

        /* Precompute sprite row stride in bytes (4bpp packed) */
        int spr_row_bytes = (spr_img_w + 1) >> 1;

        for (int x = cx0; x <= cx1; x++)
        {
            /* Z-buffer: skip column if wall is closer */
            if (ty >= rstate->col_dist[x])
                continue;

            int tex_x = ((x - x0) * spr_img_w) / spr_scr_w;
            if (tex_x >= spr_img_w)
                tex_x = spr_img_w - 1;
            int bx = x >> 1;
            int odd = x & 1;

            for (int y = cy0; y <= cy1; y++)
            {
                int tex_y = ((y - y0) * spr_img_h) / spr_scr_h;
                if (tex_y >= spr_img_h)
                    tex_y = spr_img_h - 1;

                /* Read 4bpp pixel from sprite buffer:
                 * even pixels in low nibble, odd in high nibble */
                int spr_off = tex_y * spr_row_bytes + (tex_x >> 1);
                uint8_t sbyte = (uint8_t)spr_data[spr_off];
                uint8_t col4;
                if (tex_x & 1)
                    col4 = (sbyte >> 4) & 0x0F;
                else
                    col4 = sbyte & 0x0F;

                /* Skip transparent pixels */
                if (col4 == trans)
                    continue;

                int off = y * row_bytes + bx;
                if (odd)
                    buf[off] = (buf[off] & 0x0F) | (col4 << 4);
                else
                    buf[off] = (buf[off] & 0xF0) | (col4 & 0x0F);
            }
        }
    }
}

/* ============================================================================
 * Core DDA raycasting engine
 * ============================================================================ */

static void ray_render_to_buffer(uint8_t *buf, int hres, int vres)
{
    if (rstate == NULL)
        error("Raycaster not initialised");
    if (rstate->map == NULL)
        error("No map defined");

    ray_ensure_columns(hres);

    int half_vres = vres >> 1;
    int half_hres = hres >> 1;

    /* Camera vectors */
    float angle_rad = rstate->cam_angle * (F_PI / 180.0f);
    float dir_x = cosf(angle_rad);
    float dir_y = sinf(angle_rad);

    /* Camera plane (perpendicular to direction, scaled by FOV) */
    float plane_len = tanf(rstate->cam_fov * 0.5f * (F_PI / 180.0f));
    float plane_x = -dir_y * plane_len;
    float plane_y = dir_x * plane_len;

    float pos_x = rstate->cam_x;
    float pos_y = rstate->cam_y;
    int map_w = rstate->map_w;
    int map_h = rstate->map_h;
    uint8_t *map = rstate->map;

    /* ---- Textured floor and ceiling (horizontal scanline approach) ----
     * For each row below the horizon, compute the world-space floor
     * coordinates using the ray directions at leftmost/rightmost columns.
     * Ceiling is mirrored above the horizon.
     * This avoids per-pixel division — only additions in the inner loop.
     */
    {
        /* Ray directions at leftmost and rightmost screen edges */
        float ray_x0 = dir_x - plane_x;
        float ray_y0 = dir_y - plane_y;
        float ray_x1 = dir_x + plane_x;
        float ray_y1 = dir_y + plane_y;

        uint8_t f_fg = rstate->floor_fg;
        uint8_t f_bg = rstate->floor_bg;
        uint8_t c_fg = rstate->ceil_fg;
        uint8_t c_bg = rstate->ceil_bg;
        int f_pat = rstate->floor_pat;
        int c_pat = rstate->ceil_pat;
        if (f_pat >= NUM_PATTERNS)
            f_pat = 0;
        if (c_pat >= NUM_PATTERNS)
            c_pat = 0;

        float inv_hres = 1.0f / (float)hres;

        for (int y = half_vres + 1; y < vres; y++)
        {
            /* Row distance: camera height (0.5) * screen height / row offset */
            float row_dist = (float)half_vres / (float)(y - half_vres);

            /* Floor position at leftmost column */
            float floor_x = pos_x + row_dist * ray_x0;
            float floor_y = pos_y + row_dist * ray_y0;

            /* Step per column */
            float step_x = row_dist * (ray_x1 - ray_x0) * inv_hres;
            float step_y = row_dist * (ray_y1 - ray_y0) * inv_hres;

            /* Floor row byte pointer */
            uint8_t *floor_row = buf + y * half_hres;
            /* Ceiling row (mirrored): row (vres - 1 - y) */
            int ceil_y = vres - 1 - y;
            uint8_t *ceil_row = buf + ceil_y * half_hres;

            for (int x = 0; x < hres; x += 2)
            {
                /* Even pixel (low nibble) */
                int tx, ty;
                uint8_t f_col, c_col;

                tx = ((int)(floor_x * 16.0f)) & 7;
                ty = ((int)(floor_y * 16.0f)) & 7;
                if (tx < 0)
                    tx += 8;
                if (ty < 0)
                    ty += 8;
                f_col = (fill_patterns[f_pat][ty] & (0x80 >> tx)) ? f_fg : f_bg;
                c_col = (fill_patterns[c_pat][ty] & (0x80 >> tx)) ? c_fg : c_bg;

                floor_x += step_x;
                floor_y += step_y;

                /* Odd pixel (high nibble) */
                int tx1, ty1;
                uint8_t f_col1, c_col1;

                tx1 = ((int)(floor_x * 16.0f)) & 7;
                ty1 = ((int)(floor_y * 16.0f)) & 7;
                if (tx1 < 0)
                    tx1 += 8;
                if (ty1 < 0)
                    ty1 += 8;
                f_col1 = (fill_patterns[f_pat][ty1] & (0x80 >> tx1)) ? f_fg : f_bg;
                c_col1 = (fill_patterns[c_pat][ty1] & (0x80 >> tx1)) ? c_fg : c_bg;

                floor_x += step_x;
                floor_y += step_y;

                /* Write pixel pair */
                int bx = x >> 1;
                floor_row[bx] = (f_col1 << 4) | f_col;
                ceil_row[bx] = (c_col1 << 4) | c_col;
            }
        }
        /* Fill the horizon line itself with ceiling colour */
        {
            uint8_t ceil_pair = (c_fg << 4) | c_fg;
            memset(buf + half_vres * half_hres, ceil_pair, half_hres);
        }
    }

    /* Cast one ray per screen column */
    for (int col = 0; col < hres; col++)
    {
        /* Camera-space X coordinate: -1.0 (left) to +1.0 (right) */
        float camera_x = 2.0f * col / (float)hres - 1.0f;

        /* Ray direction */
        float ray_dx = dir_x + plane_x * camera_x;
        float ray_dy = dir_y + plane_y * camera_x;

        /* Current map cell */
        int map_x = (int)pos_x;
        int map_y = (int)pos_y;

        /* Delta distances: |1/ray_component| */
        float delta_dist_x = (ray_dx == 0.0f) ? 1e30f : fabsf(1.0f / ray_dx);
        float delta_dist_y = (ray_dy == 0.0f) ? 1e30f : fabsf(1.0f / ray_dy);

        /* Step direction and initial side distances */
        int step_x, step_y;
        float side_dist_x, side_dist_y;

        if (ray_dx < 0)
        {
            step_x = -1;
            side_dist_x = (pos_x - map_x) * delta_dist_x;
        }
        else
        {
            step_x = 1;
            side_dist_x = (map_x + 1.0f - pos_x) * delta_dist_x;
        }
        if (ray_dy < 0)
        {
            step_y = -1;
            side_dist_y = (pos_y - map_y) * delta_dist_y;
        }
        else
        {
            step_y = 1;
            side_dist_y = (map_y + 1.0f - pos_y) * delta_dist_y;
        }

        /* DDA loop */
        int hit = 0;
        int side = 0; /* 0 = X-side, 1 = Y-side */
        int wall_type = 0;

        while (!hit)
        {
            if (side_dist_x < side_dist_y)
            {
                side_dist_x += delta_dist_x;
                map_x += step_x;
                side = 0;
            }
            else
            {
                side_dist_y += delta_dist_y;
                map_y += step_y;
                side = 1;
            }
            /* Bounds check */
            if (map_x < 0 || map_x >= map_w || map_y < 0 || map_y >= map_h)
            {
                hit = 1;
                wall_type = 1; /* boundary wall */
            }
            else
            {
                wall_type = map[map_y * map_w + map_x];
                if (wall_type > 0)
                {
                    if (rstate->walldefs[wall_type].is_door &&
                        ray_door_check_pass(map_x, map_y, side,
                                            pos_x, pos_y, ray_dx, ray_dy,
                                            side_dist_x, side_dist_y,
                                            delta_dist_x, delta_dist_y))
                    {
                        /* Ray passes through open portion of door */
                    }
                    else
                    {
                        hit = 1;
                    }
                }
            }
        }

        /* Perpendicular distance (avoids fisheye) */
        float perp_dist;
        if (side == 0)
            perp_dist = side_dist_x - delta_dist_x;
        else
            perp_dist = side_dist_y - delta_dist_y;

        if (perp_dist < 0.001f)
            perp_dist = 0.001f;

        /* Store for query functions */
        rstate->col_dist[col] = perp_dist;
        rstate->col_wall[col] = (uint8_t)wall_type;

        /* Wall strip height */
        int line_height = (int)(vres / perp_dist);
        int draw_start = half_vres - line_height / 2;
        int draw_end = half_vres + line_height / 2 - 1;
        if (draw_start < 0)
            draw_start = 0;
        if (draw_end >= vres)
            draw_end = vres - 1;

        /* Texture X coordinate (0-7) from wall hit position */
        float wall_x;
        if (side == 0)
            wall_x = pos_y + perp_dist * ray_dy;
        else
            wall_x = pos_x + perp_dist * ray_dx;
        wall_x -= floorf(wall_x);
        int tex_x = (int)(wall_x * 16.0f) & 7;
        if (tex_x < 0)
            tex_x = 0;
        if (tex_x > 7)
            tex_x = 7;

        /* Draw the wall column */
        if (wall_type > 0 && draw_start <= draw_end)
        {
            ray_vline_textured(buf, col, draw_start, draw_end,
                               hres, vres, wall_type, tex_x, side,
                               (float)line_height);
        }
    }

    /* Render billboard sprites (depth-sorted, z-buffered against walls) */
    ray_render_sprites(buf, hres, vres);
}

/* ============================================================================
 * Helper: decode a map character to a cell value (0-35).
 * '0'-'9' → 0-9, 'A'-'Z' / 'a'-'z' → 10-35.
 * Returns -1 for invalid characters.
 * ============================================================================ */
static int ray_decode_map_char(unsigned char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 10;
    return -1;
}

/* ============================================================================
 * RAY MAP w, h, map%()       — integer array form
 * RAY MAP w, h, map$()       — string array form
 *
 * String array form: each element is one row of the map.  Characters are
 * decoded as:  '0'-'9' → 0-9,  'A'-'Z' / 'a'-'z' → 10-35.
 * This uses 1 byte per cell in program memory (the string literal) and
 * 1 byte per cell in variable memory, instead of 8 bytes per cell for
 * the integer array form.
 * ============================================================================ */
static void ray_cmd_map(unsigned char *p)
{
    getcsargs(&p, 5);
    if (argc < 5)
        error("Expected: RAY MAP w, h, map%() or map$()");

    int w = getint(argv[0], 1, RAY_MAX_MAP_W);
    int h = getint(argv[2], 1, RAY_MAX_MAP_H);

    ray_alloc();

    /* (Re)allocate map storage */
    if (rstate->map)
    {
        FreeMemory((unsigned char *)rstate->map);
        rstate->map = NULL;
    }
    rstate->map = (uint8_t *)GetMemory(w * h);
    rstate->map_w = w;
    rstate->map_h = h;

    /* Probe the variable type to decide integer vs string array */
    findvar(argv[4], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
    int var_type = g_vartbl[g_VarIndex].type;

    if (var_type & T_STR)
    {
        /* ---- String array form ---- */
        unsigned char *a1str = NULL;
        unsigned char str_size = 0;
#ifdef rp2350
        int dims[MAXDIM] = {0};
#else
        short dims[MAXDIM] = {0};
#endif
        int card = parsestringarray(argv[4], &a1str, 3, 1, dims, false, &str_size);
        if (card < h)
            error("String array too small: need % rows", h);

        int stride = (int)str_size + 1;

        for (int row = 0; row < h; row++)
        {
            unsigned char *elem = a1str + row * stride;
            int slen = elem[0]; /* length-prefix byte */
            if (slen < w)
                error("Row % string too short: need % chars", row, w);

            for (int col = 0; col < w; col++)
            {
                int v = ray_decode_map_char(elem[1 + col]);
                if (v < 0)
                    error("Invalid map character '%c' at row %, col %",
                          (int)elem[1 + col], row, col);
                if (v >= RAY_MAX_WALLDEFS)
                    v = RAY_MAX_WALLDEFS - 1;
                rstate->map[row * w + col] = (uint8_t)v;
            }
        }
    }
    else
    {
        /* ---- Integer array form (original) ---- */
        long long int *arr = NULL;
        int arr_size = parseintegerarray(argv[4], &arr, 3, 1, NULL, false, NULL);
        if (arr_size < w * h)
            error("Map array too small: need % elements", w * h);

        for (int i = 0; i < w * h; i++)
        {
            int v = (int)arr[i];
            if (v < 0)
                v = 0;
            if (v > NUM_PATTERNS)
                v = NUM_PATTERNS;
            rstate->map[i] = (uint8_t)v;
        }
    }
}

/* ============================================================================
 * RAY CAMERA x!, y!, angle! [, fov!]
 * ============================================================================ */
static void ray_cmd_camera(unsigned char *p)
{
    getcsargs(&p, 7);
    if (argc < 5)
        error("Expected: RAY CAMERA x, y, angle [, fov]");

    ray_alloc();
    rstate->cam_x = (float)getnumber(argv[0]);
    rstate->cam_y = (float)getnumber(argv[2]);
    rstate->cam_angle = (float)getnumber(argv[4]);
    if (argc >= 7)
        rstate->cam_fov = (float)getnumber(argv[6]);
    if (rstate->cam_fov < 10.0f)
        rstate->cam_fov = 10.0f;
    if (rstate->cam_fov > 170.0f)
        rstate->cam_fov = 170.0f;
}

/* ============================================================================
 * RAY COLOUR floor_fg, ceil_fg [, floor_bg, ceil_bg, floor_pat, ceil_pat]
 * 2 args: solid floor and ceiling
 * 6 args: textured floor and ceiling
 * ============================================================================ */
static void ray_cmd_colour(unsigned char *p)
{
    getcsargs(&p, 11);
    if (argc < 3)
        error("Expected: RAY COLOUR floor, ceiling [, floor_bg, ceiling_bg, floor_pat, ceiling_pat]");

    ray_alloc();

    if (argc >= 11)
    {
        /* 6 arguments: full textured floor/ceiling */
        rstate->floor_fg = (uint8_t)(getint(argv[0], 0, 15));
        rstate->ceil_fg = (uint8_t)(getint(argv[2], 0, 15));
        rstate->floor_bg = (uint8_t)(getint(argv[4], 0, 15));
        rstate->ceil_bg = (uint8_t)(getint(argv[6], 0, 15));
        rstate->floor_pat = (uint8_t)(getint(argv[8], 0, NUM_PATTERNS - 1));
        rstate->ceil_pat = (uint8_t)(getint(argv[10], 0, NUM_PATTERNS - 1));
    }
    else
    {
        /* 2 arguments: solid floor and ceiling */
        rstate->floor_fg = (uint8_t)(getint(argv[0], 0, 15));
        rstate->floor_bg = rstate->floor_fg;
        rstate->ceil_fg = (uint8_t)(getint(argv[2], 0, 15));
        rstate->ceil_bg = rstate->ceil_fg;
        rstate->floor_pat = 0; /* Solid */
        rstate->ceil_pat = 0;
    }
}

/* ============================================================================
 * Helper: evaluate an argument as a wall type (0-35).
 * Accepts either an integer value or a single-character string
 * ('0'-'9' → 0-9, 'A'-'Z' / 'a'-'z' → 10-35).
 * Uses evaluate() to determine the argument type at runtime.
 * ============================================================================ */
static int ray_get_walltype(unsigned char *arg, int lo, int hi)
{
    MMFLOAT f;
    long long int i64;
    unsigned char *s = NULL;
    int t = T_NOTYPE;

    evaluate(arg, &f, &i64, &s, &t, false);

    if (t & T_STR)
    {
        /* String: must be exactly 1 character */
        int slen = *s;
        if (slen != 1)
            error("Expected single character for wall type");
        int v = ray_decode_map_char(s[1]);
        if (v < 0)
            error("Invalid wall type character");
        if (v < lo || v > hi)
            error("% is invalid (valid is % to %)", v, lo, hi);
        return v;
    }
    else
    {
        /* Numeric: convert float to int if needed */
        int v = (t & T_INT) ? (int)i64 : (int)f;
        if (v < lo || v > hi)
            error("% is invalid (valid is % to %)", v, lo, hi);
        return v;
    }
}

/* ============================================================================
 * RAY DEFINE type, fg, bg, pattern [, door]
 * Configure the appearance and behaviour of a wall type (1-31).
 * type: integer 1-31 or single character '1'-'9','A'-'Z' (10-35).
 * fg, bg: foreground/background colours (0-15).
 * pattern: fill pattern index (0-31).
 * door: 0 = solid wall (default), 1 = sliding door type.
 * ============================================================================ */
static void ray_cmd_define(unsigned char *p)
{
    getcsargs(&p, 9);
    if (argc < 7)
        error("Expected: RAY DEFINE type, fg, bg, pattern [, door]");

    ray_alloc();

    int wtype = ray_get_walltype(argv[0], 1, RAY_MAX_WALLDEFS - 1);
    int fg = getint(argv[2], 0, 15);
    int bg = getint(argv[4], 0, 15);
    int pat = getint(argv[6], 0, NUM_PATTERNS - 1);
    int door = 0;
    if (argc >= 9)
        door = getint(argv[8], 0, 1);

    rstate->walldefs[wtype].fg = (uint8_t)fg;
    rstate->walldefs[wtype].bg = (uint8_t)bg;
    rstate->walldefs[wtype].pattern = (uint8_t)pat;
    rstate->walldefs[wtype].is_door = (uint8_t)door;
}

/* ============================================================================
 * RAY CELL x, y, value
 * Write a map cell.  value: integer 0-31 or single character '0'-'9','A'-'Z'.
 * ============================================================================ */
static void ray_cmd_cell(unsigned char *p)
{
    getcsargs(&p, 5);
    if (argc < 5)
        error("Expected: RAY CELL x, y, value");

    if (rstate == NULL || rstate->map == NULL)
        error("No map defined");

    int x = getint(argv[0], 0, rstate->map_w - 1);
    int y = getint(argv[2], 0, rstate->map_h - 1);
    int v = ray_get_walltype(argv[4], 0, RAY_MAX_WALLDEFS - 1);

    rstate->map[y * rstate->map_w + x] = (uint8_t)v;
}

/* ============================================================================
 * Internal: cast a single ray from camera at a given absolute angle (degrees).
 * Stores result in rstate->cast_* fields.
 * ============================================================================ */
static void ray_do_cast(float angle_deg)
{
    if (rstate == NULL)
        error("Raycaster not initialised");
    if (rstate->map == NULL)
        error("No map defined");

    float angle_rad = angle_deg * (F_PI / 180.0f);
    float ray_dx = cosf(angle_rad);
    float ray_dy = sinf(angle_rad);

    float pos_x = rstate->cam_x;
    float pos_y = rstate->cam_y;
    int map_w = rstate->map_w;
    int map_h = rstate->map_h;
    uint8_t *map = rstate->map;

    int map_x = (int)pos_x;
    int map_y = (int)pos_y;

    float delta_dist_x = (ray_dx == 0.0f) ? 1e30f : fabsf(1.0f / ray_dx);
    float delta_dist_y = (ray_dy == 0.0f) ? 1e30f : fabsf(1.0f / ray_dy);

    int step_x, step_y;
    float side_dist_x, side_dist_y;

    if (ray_dx < 0)
    {
        step_x = -1;
        side_dist_x = (pos_x - map_x) * delta_dist_x;
    }
    else
    {
        step_x = 1;
        side_dist_x = (map_x + 1.0f - pos_x) * delta_dist_x;
    }
    if (ray_dy < 0)
    {
        step_y = -1;
        side_dist_y = (pos_y - map_y) * delta_dist_y;
    }
    else
    {
        step_y = 1;
        side_dist_y = (map_y + 1.0f - pos_y) * delta_dist_y;
    }

    int hit = 0;
    int side = 0;
    int wall_type = 0;

    while (!hit)
    {
        if (side_dist_x < side_dist_y)
        {
            side_dist_x += delta_dist_x;
            map_x += step_x;
            side = 0;
        }
        else
        {
            side_dist_y += delta_dist_y;
            map_y += step_y;
            side = 1;
        }
        if (map_x < 0 || map_x >= map_w || map_y < 0 || map_y >= map_h)
        {
            hit = 1;
            wall_type = 1;
        }
        else
        {
            wall_type = map[map_y * map_w + map_x];
            if (wall_type > 0)
            {
                if (rstate->walldefs[wall_type].is_door &&
                    ray_door_check_pass(map_x, map_y, side,
                                        pos_x, pos_y, ray_dx, ray_dy,
                                        side_dist_x, side_dist_y,
                                        delta_dist_x, delta_dist_y))
                {
                    /* Ray passes through open portion of door */
                }
                else
                {
                    hit = 1;
                }
            }
        }
    }

    float perp_dist;
    if (side == 0)
        perp_dist = side_dist_x - delta_dist_x;
    else
        perp_dist = side_dist_y - delta_dist_y;
    if (perp_dist < 0.001f)
        perp_dist = 0.001f;

    rstate->cast_dist = perp_dist;
    rstate->cast_wall = wall_type;
    rstate->cast_side = side;
    rstate->cast_mapx = map_x;
    rstate->cast_mapy = map_y;
}

/* ============================================================================
 * RAY CAST angle!
 * Cast a single ray from camera at absolute angle (degrees).
 * Results retrieved via RAY(CASTDIST), RAY(CASTWALL), etc.
 * ============================================================================ */
static void ray_cmd_cast(unsigned char *p)
{
    getcsargs(&p, 1);
    if (argc < 1)
        error("Expected: RAY CAST angle");

    float angle = (float)getnumber(argv[0]);
    ray_do_cast(angle);
}

/* ============================================================================
 * Collision detection helper
 * Checks if a bounding box of ±radius around (x,y) overlaps any wall cell.
 * Returns 1 if blocked, 0 if clear.
 * ============================================================================ */
#define RAY_COLLISION_RADIUS 0.25f

static int ray_is_blocked(float x, float y, float radius)
{
    if (rstate == NULL || rstate->map == NULL)
        return 1;

    int mw = rstate->map_w;
    int mh = rstate->map_h;
    uint8_t *m = rstate->map;

    /* Check all 4 corners of the bounding box */
    int x0 = (int)(x - radius);
    int x1 = (int)(x + radius);
    int y0 = (int)(y - radius);
    int y1 = (int)(y + radius);

    /* Out of bounds = blocked */
    if (x0 < 0 || x1 >= mw || y0 < 0 || y1 >= mh)
        return 1;

    if (m[y0 * mw + x0] > 0 && ray_cell_blocks(m, mw, x0, y0))
        return 1;
    if (m[y0 * mw + x1] > 0 && ray_cell_blocks(m, mw, x1, y0))
        return 1;
    if (m[y1 * mw + x0] > 0 && ray_cell_blocks(m, mw, x0, y1))
        return 1;
    if (m[y1 * mw + x1] > 0 && ray_cell_blocks(m, mw, x1, y1))
        return 1;

    return 0;
}

/* ============================================================================
 * RAY MOVE speed! [, strafe!]
 * Move camera with built-in collision detection and wall sliding.
 * speed > 0 = forward, < 0 = backward.
 * strafe > 0 = right, < 0 = left.
 * If the full move is blocked, tries X-only then Y-only (wall sliding).
 * ============================================================================ */
static void ray_cmd_move(unsigned char *p)
{
    getcsargs(&p, 3);
    if (argc < 1)
        error("Expected: RAY MOVE speed [, strafe]");

    if (rstate == NULL)
        error("Raycaster not initialised");
    if (rstate->map == NULL)
        error("No map defined");

    float speed = (float)getnumber(argv[0]);
    float strafe = 0.0f;
    if (argc >= 3)
        strafe = (float)getnumber(argv[2]);

    float angle_rad = rstate->cam_angle * (F_PI / 180.0f);
    float dx = cosf(angle_rad);
    float dy = sinf(angle_rad);

    /* Forward/backward + strafe (strafe is perpendicular to facing) */
    float move_x = dx * speed - dy * strafe;
    float move_y = dy * speed + dx * strafe;

    float cur_x = rstate->cam_x;
    float cur_y = rstate->cam_y;
    float new_x = cur_x + move_x;
    float new_y = cur_y + move_y;
    float r = RAY_COLLISION_RADIUS;

    /* Try full movement */
    if (!ray_is_blocked(new_x, new_y, r))
    {
        rstate->cam_x = new_x;
        rstate->cam_y = new_y;
        return;
    }

    /* Wall sliding: try X-only */
    if (!ray_is_blocked(new_x, cur_y, r))
    {
        rstate->cam_x = new_x;
        return;
    }

    /* Wall sliding: try Y-only */
    if (!ray_is_blocked(cur_x, new_y, r))
    {
        rstate->cam_y = new_y;
        return;
    }

    /* Completely blocked — don't move */
}

/* ============================================================================
 * RAY TURN degrees!
 * Adjust camera angle. Positive = clockwise/right, negative = left.
 * ============================================================================ */
static void ray_cmd_turn(unsigned char *p)
{
    getcsargs(&p, 1);
    if (argc < 1)
        error("Expected: RAY TURN degrees");

    if (rstate == NULL)
        error("Raycaster not initialised");

    float degrees = (float)getnumber(argv[0]);
    rstate->cam_angle += degrees;

    /* Normalise to 0-360 */
    while (rstate->cam_angle < 0.0f)
        rstate->cam_angle += 360.0f;
    while (rstate->cam_angle >= 360.0f)
        rstate->cam_angle -= 360.0f;
}

/* ============================================================================
 * RAY SPRITE id, spritenum, x!, y!
 * RAY SPRITE REMOVE id
 * RAY SPRITE CLEAR
 *
 * spritenum is a SPRITE buffer number (1-64) loaded via SPRITE LOAD.
 * The sprite's 4bpp pixel data is read from spritebuff[spritenum].
 * ============================================================================ */
static void ray_cmd_sprite(unsigned char *p)
{
    unsigned char *q;
    if ((q = checkstring(p, (unsigned char *)"CLEAR")))
    {
        if (rstate)
            memset(rstate->sprites, 0, sizeof(rstate->sprites));
        return;
    }
    if ((q = checkstring(p, (unsigned char *)"REMOVE")))
    {
        getcsargs(&q, 1);
        if (argc < 1)
            error("Expected: RAY SPRITE REMOVE id");
        int id = getint(argv[0], 0, RAY_MAX_SPRITES - 1);
        if (rstate)
            rstate->sprites[id].active = 0;
        return;
    }

    /* RAY SPRITE id, spritenum, x!, y! */
    getcsargs(&p, 7);
    if (argc < 7)
        error("Expected: RAY SPRITE id, spritenum, x, y");

    ray_alloc();

    int id = getint(argv[0], 0, RAY_MAX_SPRITES - 1);
    int snum = getint(argv[2], 1, MAXBLITBUF);
    if (spritebuff[snum] == NULL || spritebuff[snum]->spritebuffptr == NULL)
        error("Sprite buffer %d not loaded", snum);

    RaySprite *sp = &rstate->sprites[id];
    sp->spritenum = snum;
    sp->x = (float)getnumber(argv[4]);
    sp->y = (float)getnumber(argv[6]);
    sp->active = 1;
}

/* ============================================================================
 * RAY MINIMAP x, y, size
 * Draw top-down minimap overlay into WriteBuf at (x,y).
 * Shows the entire map scaled so the longest axis fits 'size' pixels.
 * ============================================================================ */
static void ray_render_minimap(uint8_t *buf, int hres, int vres,
                               int mx, int my, int size)
{
    if (rstate == NULL || rstate->map == NULL)
        return;

    int mw = rstate->map_w;
    int mh = rstate->map_h;
    uint8_t *map = rstate->map;

    /* Scale so the longest map axis fits 'size' pixels */
    int view_w, view_h;
    if (mw >= mh)
    {
        view_w = size;
        view_h = (size * mh + mw - 1) / mw; /* round up */
    }
    else
    {
        view_h = size;
        view_w = (size * mw + mh - 1) / mh;
    }
    if (view_w < 1)
        view_w = 1;
    if (view_h < 1)
        view_h = 1;

    /* Draw map cells: walls = GREEN(6), empty = BLACK(0) */
    for (int dy = 0; dy < view_h; dy++)
    {
        int cell_y = dy * mh / view_h;
        int py = my + dy;
        if (py < 0 || py >= vres)
            continue;
        for (int dx = 0; dx < view_w; dx++)
        {
            int cell_x = dx * mw / view_w;
            int px = mx + dx;
            if (px < 0 || px >= hres)
                continue;

            uint8_t col;
            if (cell_x < 0 || cell_x >= mw || cell_y < 0 || cell_y >= mh)
                col = 0;
            else
            {
                uint8_t wt = map[cell_y * mw + cell_x];
                if (wt == 0)
                    col = 0; /* empty = BLACK */
                else if (rstate->walldefs[wt].is_door)
                {
                    float doff = ray_get_door_offset(cell_x, cell_y);
                    if (doff >= 1.0f)
                        col = 0; /* fully open door = BLACK */
                    else if (doff > 0.0f)
                        col = 14; /* partially open = YELLOW */
                    else
                        col = rstate->walldefs[wt].fg; /* closed door */
                }
                else
                    col = rstate->walldefs[wt].fg; /* wall = definition fg */
            }

            ray_putpixel(buf, px, py, hres, col);
        }
    }

    /* Border — MYRTLE(2) */
    for (int i = 0; i < view_w; i++)
    {
        int bx = mx + i;
        if (bx >= 0 && bx < hres)
        {
            if (my >= 0 && my < vres)
                ray_putpixel(buf, bx, my, hres, 2);
            int by = my + view_h - 1;
            if (by >= 0 && by < vres)
                ray_putpixel(buf, bx, by, hres, 2);
        }
    }
    for (int i = 0; i < view_h; i++)
    {
        int by = my + i;
        if (by >= 0 && by < vres)
        {
            if (mx >= 0 && mx < hres)
                ray_putpixel(buf, mx, by, hres, 2);
            int bx = mx + view_w - 1;
            if (bx >= 0 && bx < hres)
                ray_putpixel(buf, bx, by, hres, 2);
        }
    }

    /* Active sprites shown as WHITE dots */
    for (int i = 0; i < RAY_MAX_SPRITES; i++)
    {
        if (!rstate->sprites[i].active)
            continue;
        int sx = mx + (int)(rstate->sprites[i].x * view_w / mw);
        int sy = my + (int)(rstate->sprites[i].y * view_h / mh);
        if (sx >= mx && sx < mx + view_w && sy >= my && sy < my + view_h &&
            sx >= 0 && sx < hres && sy >= 0 && sy < vres)
            ray_putpixel(buf, sx, sy, hres, 9); /* YELLOW dot for sprites */
    }

    /* Player dot (WHITE=15) + direction indicator */
    int ppx = mx + (int)(rstate->cam_x * view_w / mw);
    int ppy = my + (int)(rstate->cam_y * view_h / mh);
    if (ppx >= 0 && ppx < hres && ppy >= 0 && ppy < vres)
        ray_putpixel(buf, ppx, ppy, hres, 15);
    float arad = rstate->cam_angle * (F_PI / 180.0f);
    for (int i = 1; i <= 2; i++)
    {
        int fx = ppx + (int)(cosf(arad) * (float)i);
        int fy = ppy + (int)(sinf(arad) * (float)i);
        if (fx >= mx && fx < mx + view_w && fy >= my && fy < my + view_h &&
            fx >= 0 && fx < hres && fy >= 0 && fy < vres)
            ray_putpixel(buf, fx, fy, hres, 15);
    }
}

static void ray_cmd_minimap(unsigned char *p)
{
    getcsargs(&p, 5);
    if (argc < 5)
        error("Expected: RAY MINIMAP x, y, size");

    if (rstate == NULL)
        error("Raycaster not initialised");
    if (WriteBuf == NULL)
        error("No framebuffer");

    int mx = getint(argv[0], 0, HRes - 1);
    int my = getint(argv[2], 0, VRes - 1);
    int sz = getint(argv[4], 4, 256);

    ray_render_minimap((uint8_t *)WriteBuf, HRes, VRes, mx, my, sz);
}

/* ============================================================================
 * RAY DOOR x, y, offset!    — set/create sliding door
 * RAY DOOR CLOSE x, y       — remove door slot
 * RAY DOOR CLEAR             — remove all door slots
 * ============================================================================ */
static void ray_cmd_door(unsigned char *p)
{
    unsigned char *q;
    if ((q = checkstring(p, (unsigned char *)"CLEAR")))
    {
        if (rstate)
            memset(rstate->doors, 0, sizeof(rstate->doors));
        return;
    }
    if ((q = checkstring(p, (unsigned char *)"CLOSE")))
    {
        getcsargs(&q, 3);
        if (argc < 3)
            error("Expected: RAY DOOR CLOSE x, y");
        if (rstate == NULL || rstate->map == NULL)
            error("No map defined");
        int x = getint(argv[0], 0, rstate->map_w - 1);
        int y = getint(argv[2], 0, rstate->map_h - 1);
        for (int i = 0; i < RAY_MAX_DOORS; i++)
        {
            if (rstate->doors[i].active &&
                rstate->doors[i].map_x == x &&
                rstate->doors[i].map_y == y)
            {
                rstate->doors[i].active = 0;
                return;
            }
        }
        return; /* not found, silently ignore */
    }

    /* RAY DOOR x, y, offset! */
    getcsargs(&p, 5);
    if (argc < 5)
        error("Expected: RAY DOOR x, y, offset");
    if (rstate == NULL || rstate->map == NULL)
        error("No map defined");

    int x = getint(argv[0], 0, rstate->map_w - 1);
    int y = getint(argv[2], 0, rstate->map_h - 1);
    float offset = (float)getnumber(argv[4]);
    if (offset < 0.0f)
        offset = 0.0f;
    if (offset > 1.0f)
        offset = 1.0f;

    /* Look for existing slot */
    for (int i = 0; i < RAY_MAX_DOORS; i++)
    {
        if (rstate->doors[i].active &&
            rstate->doors[i].map_x == x &&
            rstate->doors[i].map_y == y)
        {
            rstate->doors[i].offset = offset;
            return;
        }
    }

    /* Allocate new slot */
    for (int i = 0; i < RAY_MAX_DOORS; i++)
    {
        if (!rstate->doors[i].active)
        {
            rstate->doors[i].map_x = x;
            rstate->doors[i].map_y = y;
            rstate->doors[i].offset = offset;
            rstate->doors[i].active = 1;
            return;
        }
    }
    error("Maximum %d active doors exceeded", RAY_MAX_DOORS);
}

/* ============================================================================
 * RAY RENDER
 * Renders the scene directly into WriteBuf (must be set via FRAMEBUFFER).
 * ============================================================================ */
static void ray_cmd_render(unsigned char *p)
{
    if (WriteBuf == NULL)
        error("No framebuffer: use FRAMEBUFFER CREATE first");

    int hres = HRes;
    int vres = VRes;

    /* Render directly into the current write framebuffer */
    ray_render_to_buffer((uint8_t *)WriteBuf, hres, vres);
}

/* ============================================================================
 * RAY CLOSE
 * ============================================================================ */
void ray_close(void)
{
    if (rstate == NULL)
        return;
    ray_free_columns();
    if (rstate->map)
    {
        FreeMemory((unsigned char *)rstate->map);
        rstate->map = NULL;
    }
    FreeMemory((unsigned char *)rstate);
    rstate = NULL;
}

/* ============================================================================
 * Command dispatcher: RAY <subcommand> ...
 * ============================================================================ */
void MIPS16 cmd_ray(void)
{
    unsigned char *p;

    if ((p = checkstring(cmdline, (unsigned char *)"MAP")))
    {
        ray_cmd_map(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CAMERA")))
    {
        ray_cmd_camera(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"COLOUR")))
    {
        ray_cmd_colour(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"COLOR")))
    {
        ray_cmd_colour(p); /* American spelling alias */
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"DEFINE")))
    {
        ray_cmd_define(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MOVE")))
    {
        ray_cmd_move(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"TURN")))
    {
        ray_cmd_turn(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CELL")))
    {
        ray_cmd_cell(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CAST")))
    {
        ray_cmd_cast(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"DOOR")))
    {
        ray_cmd_door(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SPRITE")))
    {
        ray_cmd_sprite(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MINIMAP")))
    {
        ray_cmd_minimap(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"RENDER")))
    {
        ray_cmd_render(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        ray_close();
    }
    else
    {
        SyntaxError();
    }
}

/* ============================================================================
 * Function dispatcher: RAY( <query> )
 * ============================================================================ */
void MIPS16 fun_ray(void)
{
    unsigned char *p;

    if ((p = checkstring(ep, (unsigned char *)"MAPW")))
    {
        iret = rstate ? rstate->map_w : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"MAPH")))
    {
        iret = rstate ? rstate->map_h : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CAMX")))
    {
        fret = rstate ? (MMFLOAT)rstate->cam_x : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CAMY")))
    {
        fret = rstate ? (MMFLOAT)rstate->cam_y : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CAMA")))
    {
        fret = rstate ? (MMFLOAT)rstate->cam_angle : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"DIST")))
    {
        getcsargs(&p, 1);
        int col = getint(argv[0], 0, (rstate && rstate->num_cols > 0) ? rstate->num_cols - 1 : 0);
        fret = (rstate && rstate->col_dist) ? (MMFLOAT)rstate->col_dist[col] : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"WALL")))
    {
        getcsargs(&p, 1);
        int col = getint(argv[0], 0, (rstate && rstate->num_cols > 0) ? rstate->num_cols - 1 : 0);
        iret = (rstate && rstate->col_wall) ? rstate->col_wall[col] : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CELL")))
    {
        getcsargs(&p, 3);
        if (argc < 3)
            error("Expected: RAY(CELL x, y)");
        if (rstate == NULL || rstate->map == NULL)
        {
            iret = 0;
        }
        else
        {
            int x = getint(argv[0], 0, rstate->map_w - 1);
            int y = getint(argv[2], 0, rstate->map_h - 1);
            iret = rstate->map[y * rstate->map_w + x];
        }
    }
    else if ((p = checkstring(ep, (unsigned char *)"CASTDIST")))
    {
        fret = rstate ? (MMFLOAT)rstate->cast_dist : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CASTWALL")))
    {
        iret = rstate ? rstate->cast_wall : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CASTSIDE")))
    {
        iret = rstate ? rstate->cast_side : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CASTX")))
    {
        iret = rstate ? rstate->cast_mapx : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"CASTY")))
    {
        iret = rstate ? rstate->cast_mapy : 0;
    }
    else if ((p = checkstring(ep, (unsigned char *)"DOOR")))
    {
        getcsargs(&p, 3);
        if (argc < 3)
            error("Expected: RAY(DOOR x, y)");
        if (rstate == NULL || rstate->map == NULL)
        {
            fret = -1.0;
        }
        else
        {
            int x = getint(argv[0], 0, rstate->map_w - 1);
            int y = getint(argv[2], 0, rstate->map_h - 1);
            int found = 0;
            for (int i = 0; i < RAY_MAX_DOORS; i++)
            {
                if (rstate->doors[i].active &&
                    rstate->doors[i].map_x == x &&
                    rstate->doors[i].map_y == y)
                {
                    fret = (MMFLOAT)rstate->doors[i].offset;
                    found = 1;
                    break;
                }
            }
            if (!found)
                fret = -1.0;
        }
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"DEFINE")))
    {
        getcsargs(&p, 3);
        if (argc < 3)
            error("Expected: RAY(DEFINE type, property)");
        int wtype = ray_get_walltype(argv[0], 1, RAY_MAX_WALLDEFS - 1);
        int prop = getint(argv[2], 0, 3);
        if (rstate == NULL)
        {
            iret = 0;
        }
        else
        {
            switch (prop)
            {
            case 0:
                iret = rstate->walldefs[wtype].fg;
                break;
            case 1:
                iret = rstate->walldefs[wtype].bg;
                break;
            case 2:
                iret = rstate->walldefs[wtype].pattern;
                break;
            case 3:
                iret = rstate->walldefs[wtype].is_door;
                break;
            default:
                iret = 0;
                break;
            }
        }
    }
    else if ((p = checkstring(ep, (unsigned char *)"SPRITES")))
    {
        int cnt = 0;
        if (rstate)
        {
            for (int i = 0; i < RAY_MAX_SPRITES; i++)
                if (rstate->sprites[i].active)
                    cnt++;
        }
        iret = cnt;
    }
    else if ((p = checkstring(ep, (unsigned char *)"SPRITEX")))
    {
        getcsargs(&p, 1);
        int id = getint(argv[0], 0, RAY_MAX_SPRITES - 1);
        fret = (rstate && rstate->sprites[id].active) ? (MMFLOAT)rstate->sprites[id].x : 0.0;
        targ = T_NBR;
        return;
    }
    else if ((p = checkstring(ep, (unsigned char *)"SPRITEY")))
    {
        getcsargs(&p, 1);
        int id = getint(argv[0], 0, RAY_MAX_SPRITES - 1);
        fret = (rstate && rstate->sprites[id].active) ? (MMFLOAT)rstate->sprites[id].y : 0.0;
        targ = T_NBR;
        return;
    }
    else
    {
        SyntaxError();
    }
    targ = T_INT;
}
