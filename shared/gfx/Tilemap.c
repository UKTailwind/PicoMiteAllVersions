/*
 * Tilemap.c — TILEMAP + TILEMAP SPRITE subsystem.
 *
 * Ported (paraphrased) from upstream UKTailwind/PicoMiteAllVersions
 * Draw.c (@04f81d0, lines ~14960-15850). Provides:
 *
 *   cmd_tilemap   — TILEMAP CREATE / ATTR / DESTROY / SET / DRAW /
 *                   SCROLL / VIEW / CLOSE plus the embedded
 *                   TILEMAP SPRITE CREATE / MOVE / SET / DRAW /
 *                   DESTROY / CLOSE sub-dispatch.
 *
 *   fun_tilemap   — TILEMAP(TILE / COLLISION / ATTR / VIEWX / VIEWY /
 *                   COLS / ROWS / SPRITE {X,Y,TILE,HIT,W,H}, …)
 *
 *   tilemap_closeall — release every tilemap + sprite slot. Called on
 *                      cmd_new / program-end via the existing cleanup
 *                      hooks.
 *
 * Storage model:
 *   - Tile atlas lives in a flash slot (FLASH LOAD IMAGE) packed as
 *     RGB121 nibbles preceded by an 8-byte [width:uint32 LE]
 *     [height:uint32 LE] header.
 *   - Map grid is heap-allocated uint16_t[map_cols*map_rows], populated
 *     from a BASIC DATA label by tilemap_read_data().
 *   - Tile attributes are a parallel uint16_t[num_attrs], same source.
 *   - Sprites are 64-slot fixed-size array of (x, y, tile_index,
 *     tilemap_ref, active).
 *
 * Adaptations vs upstream:
 *   1. Dropped the DISPLAY_TYPE == SCREENMODE2/3 gate. Upstream limits
 *      to PicoMiteVGA's RGB121 framebuffer; we draw through DrawPixel
 *      which handles every framebuffer format our targets ship with.
 *   2. tilemap_get_dest accepts L/F/N/T for parsing compatibility but
 *      always returns a non-NULL placeholder — blit121 ignores the
 *      destination pointer (see RGB121.c for why).
 *   3. Dropped the rp2350-specific SecondLayer "T" fallback path; on
 *      our targets there's no second hardware plane.
 *   4. Replaced upstream's getcsargs(&p, n) with the equivalent
 *      getargs(&p, n, ","). MAX_ARG_COUNT * 2 - 1 stays the same.
 *
 * No native VM opcode; reachable from FRUN via the bridge per
 * docs/bridge-restoration-plan.md.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"

/* RAM-mirrored flash slot region; defined in PicoMite.c on device,
 * host_fs_shims.c on host/wasm. */
extern const uint8_t *flash_target_contents;

/* commandtbl_decode lives static-inline in MMBasic.c; replicate the
 * tiny two-byte decode here so we don't have to widen its visibility
 * across the tree just for the DATA walker. Kept byte-identical to
 * MMBasic.c so any future change there must mirror here. */
static inline CommandToken tm_commandtbl_decode(const unsigned char *p) {
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f) << 7);
}

/* ============================================================================
 * TILEMAP subsystem
 * ============================================================================ */

#define MAX_TILEMAPS 4
#define MAX_SPRITES  64

typedef struct {
    uint8_t  *flash_data;   /* Pointer to pixel data in flash (past 8-byte hdr) */
    uint16_t *map;          /* Heap-allocated map (cols * rows uint16_t values) */
    uint16_t *attrs;        /* Heap-allocated attribute table (1-based by tile) */
    int num_attrs;
    int flash_w;            /* Tileset image width in pixels */
    int flash_h;            /* Tileset image height in pixels */
    int tile_w;             /* Single tile width in pixels */
    int tile_h;             /* Single tile height in pixels */
    int tiles_per_row;      /* Number of tiles across the tileset image */
    int map_cols;
    int map_rows;
    int view_x;             /* Viewport X offset in world pixels */
    int view_y;
    bool active;
} tilemap_t;

static tilemap_t tilemaps[MAX_TILEMAPS] = {0};

typedef struct {
    int16_t  x;
    int16_t  y;
    uint16_t tile;          /* Tile index in the parent tileset (1-based, 0 = inactive) */
    uint8_t  tilemap_ref;   /* 0-based index into tilemaps[] */
    bool     active;
} sprite_t;

static sprite_t sprites[MAX_SPRITES] = {0};

/* ============================================================================
 * DATA-statement walker — reads `count` integer values starting at the
 * named label into a freshly-allocated uint16_t[] array. Saves and
 * restores the user's NextDataLine / NextData read position.
 * ============================================================================ */
static uint16_t *tilemap_read_data(unsigned char *label, int count)
{
    unsigned char *save_line = NextDataLine;
    int save_data = NextData;

    NextDataLine = findlabel(label);
    NextData = 0;

    uint16_t *buf = (uint16_t *)GetMemory(count * sizeof(uint16_t));

    int datatoken = GetCommandValue((unsigned char *)"Data");
    unsigned char *p = NextDataLine;
    int idx = 0;

    while (idx < count) {
        while (1) {
            if (*p == 0) p++;
            if (*p == 0) {
                FreeMemory((unsigned char *)buf);
                NextDataLine = save_line;
                NextData = save_data;
                error("Not enough DATA for tilemap (need %, found %)", count, idx);
            }
            if (*p == T_NEWLINE) p++;
            if (*p == T_LINENBR) p += 3;
            skipspace(p);
            if (*p == T_LABEL) {
                p += p[1] + 2;
                skipspace(p);
            }
            CommandToken tkn = tm_commandtbl_decode(p);
            if (tkn == datatoken) break;
            while (*p) p++;
        }
        p += sizeof(CommandToken);
        skipspace(p);

        {
            getargs(&p, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
            int di = 0;
            while (di < argc && idx < count) {
                buf[idx++] = (uint16_t)getinteger(argv[di]);
                di += 2;
            }
        }
    }

    NextDataLine = save_line;
    NextData = save_data;
    return buf;
}

/* tilemap_get_dest: parses L/F/N/T destination letters from upstream's
 * cmd_tilemap. Our blit121 ignores the destination pointer (see RGB121.c)
 * and writes through DrawPixel, so any non-NULL sentinel suffices —
 * returning &sprites[0] (always-valid in-tree address). */
static unsigned char *tilemap_get_dest(unsigned char *token)
{
    if (checkstring(token, (unsigned char *)"L") ||
        checkstring(token, (unsigned char *)"F") ||
        checkstring(token, (unsigned char *)"N") ||
        checkstring(token, (unsigned char *)"T"))
    {
        return (unsigned char *)sprites;
    }
    error("Syntax");
    return NULL;
}

/* ============================================================================
 * SPRITE subcommands (all dispatched as TILEMAP SPRITE …)
 * ============================================================================ */

/* SPRITE CREATE id, tilemapRef, tileIndex, x, y */
static void sprite_cmd_create(unsigned char *p)
{
    getargs(&p, 9, (unsigned char *)",");
    if (argc != 9) error("Syntax");

    int id    = getint(argv[0], 1, MAX_SPRITES) - 1;
    int tmref = getint(argv[2], 1, MAX_TILEMAPS) - 1;
    int tile  = getint(argv[4], 1, 65535);
    int x     = getinteger(argv[6]);
    int y     = getinteger(argv[8]);

    tilemap_t *tm = &tilemaps[tmref];
    if (!tm->active) error("Tilemap % not created", tmref + 1);

    sprite_t *sp = &sprites[id];
    sp->x = (int16_t)x;
    sp->y = (int16_t)y;
    sp->tile = (uint16_t)tile;
    sp->tilemap_ref = (uint8_t)tmref;
    sp->active = true;
}

/* SPRITE MOVE id, x, y */
static void sprite_cmd_move(unsigned char *p)
{
    getargs(&p, 5, (unsigned char *)",");
    if (argc != 5) error("Syntax");

    int id = getint(argv[0], 1, MAX_SPRITES) - 1;
    sprite_t *sp = &sprites[id];
    if (!sp->active) error("Sprite % not created", id + 1);

    sp->x = (int16_t)getinteger(argv[2]);
    sp->y = (int16_t)getinteger(argv[4]);
}

/* SPRITE SET id, tileIndex */
static void sprite_cmd_set(unsigned char *p)
{
    getargs(&p, 3, (unsigned char *)",");
    if (argc != 3) error("Syntax");

    int id = getint(argv[0], 1, MAX_SPRITES) - 1;
    sprite_t *sp = &sprites[id];
    if (!sp->active) error("Sprite % not created", id + 1);

    sp->tile = (uint16_t)getint(argv[2], 1, 65535);
}

/* SPRITE DRAW dest, transparent */
static void sprite_cmd_draw(unsigned char *p)
{
    getargs(&p, 3, (unsigned char *)",");
    if (argc != 3) error("Syntax");

    unsigned char *dest = tilemap_get_dest(argv[0]);
    if (!dest) return;
    int transparent = getint(argv[2], -1, 15);

    HResD = HRes;
    VResD = VRes;

    for (int i = 0; i < MAX_SPRITES; i++) {
        sprite_t *sp = &sprites[i];
        if (!sp->active || sp->tile == 0) continue;

        tilemap_t *tm = &tilemaps[sp->tilemap_ref];
        if (!tm->active) continue;

        int tw = tm->tile_w;
        int th = tm->tile_h;

        int src_col = (sp->tile - 1) % tm->tiles_per_row;
        int src_row = (sp->tile - 1) / tm->tiles_per_row;
        int src_x = src_col * tw;
        int src_y = src_row * th;

        HResS = tm->flash_w;
        VResS = tm->flash_h;

        blit121(tm->flash_data, dest, src_x, src_y, tw, th, sp->x, sp->y, transparent);
    }
}

/* SPRITE DESTROY id */
static void sprite_cmd_destroy(unsigned char *p)
{
    getargs(&p, 1, (unsigned char *)",");
    if (argc != 1) error("Syntax");
    int id = getint(argv[0], 1, MAX_SPRITES) - 1;
    memset(&sprites[id], 0, sizeof(sprite_t));
}

static void sprite_closeall(void)
{
    memset(sprites, 0, sizeof(sprites));
}

/* ============================================================================
 * TILEMAP subcommands
 * ============================================================================ */

/* TILEMAP CREATE label, slot, tile_w, tile_h, tiles_per_row, cols, rows */
static void tilemap_cmd_create(unsigned char *p)
{
    skipspace(p);
    if (!isnamestart(*p)) error("Expected label");
    unsigned char *label = p;
    while (isnamechar(*p)) p++;
    skipspace(p);
    if (*p != ',') error("Syntax");
    p++;

    getargs(&p, 13, (unsigned char *)",");
    if (argc != 13) error("Syntax");

    int id   = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    int slot = getint(argv[2], 1, MAXFLASHSLOTS);
    int tw   = getint(argv[4], 1, 256);
    int th   = getint(argv[6], 1, 256);
    int tpr  = getint(argv[8], 1, 1024);
    int cols = getint(argv[10], 1, 10000);
    int rows = getint(argv[12], 1, 10000);

    uint8_t  *base = (uint8_t *)(flash_target_contents + (slot - 1) * MAX_PROG_SIZE);
    uint32_t *hdr  = (uint32_t *)base;
    int fw = (int)hdr[0];
    int fh = (int)hdr[1];
    if (fw < 1 || fw > 3840 || fh < 1 || fh > 2160)
        error("Invalid flash image in slot %", slot);

    tilemap_t *tm = &tilemaps[id];
    if (tm->map)   FreeMemory((unsigned char *)tm->map);
    if (tm->attrs) FreeMemory((unsigned char *)tm->attrs);
    memset(tm, 0, sizeof(tilemap_t));

    tm->map = tilemap_read_data(label, cols * rows);

    tm->flash_data    = base + 8;
    tm->flash_w       = fw;
    tm->flash_h       = fh;
    tm->tile_w        = tw;
    tm->tile_h        = th;
    tm->tiles_per_row = tpr;
    tm->map_cols      = cols;
    tm->map_rows      = rows;
    tm->view_x        = 0;
    tm->view_y        = 0;
    tm->active        = true;
}

/* TILEMAP ATTR label, num_tiles */
static void tilemap_cmd_attr(unsigned char *p)
{
    skipspace(p);
    if (!isnamestart(*p)) error("Expected label");
    unsigned char *label = p;
    while (isnamechar(*p)) p++;
    skipspace(p);
    if (*p != ',') error("Syntax");
    p++;

    getargs(&p, 3, (unsigned char *)",");
    if (argc != 3) error("Syntax");

    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (!tm->active) error("Tilemap % not created", id + 1);

    int num = getint(argv[2], 1, 65535);

    if (tm->attrs) FreeMemory((unsigned char *)tm->attrs);
    tm->attrs = tilemap_read_data(label, num);
    tm->num_attrs = num;
}

/* TILEMAP DESTROY id */
static void tilemap_cmd_destroy(unsigned char *p)
{
    getargs(&p, 1, (unsigned char *)",");
    if (argc != 1) error("Syntax");
    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (tm->map)   FreeMemory((unsigned char *)tm->map);
    if (tm->attrs) FreeMemory((unsigned char *)tm->attrs);
    memset(tm, 0, sizeof(tilemap_t));
}

void tilemap_closeall(void)
{
    for (int i = 0; i < MAX_TILEMAPS; i++) {
        if (tilemaps[i].map)   FreeMemory((unsigned char *)tilemaps[i].map);
        if (tilemaps[i].attrs) FreeMemory((unsigned char *)tilemaps[i].attrs);
    }
    memset(tilemaps, 0, sizeof(tilemaps));
}

/* TILEMAP SET id, col, row, tile_index */
static void tilemap_cmd_set(unsigned char *p)
{
    getargs(&p, 7, (unsigned char *)",");
    if (argc != 7) error("Syntax");

    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (!tm->active) error("Tilemap % not created", id + 1);

    int col  = getint(argv[2], 0, tm->map_cols - 1);
    int row  = getint(argv[4], 0, tm->map_rows - 1);
    int tile = getint(argv[6], 0, 65535);

    tm->map[col + row * tm->map_cols] = (uint16_t)tile;
}

/* TILEMAP DRAW id, dest, viewX, viewY, screenX, screenY, viewW, viewH [, transparent] */
static void tilemap_cmd_draw(unsigned char *p)
{
    getargs(&p, 17, (unsigned char *)",");
    if (!(argc == 15 || argc == 17)) error("Syntax");

    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (!tm->active) error("Tilemap % not created", id + 1);

    unsigned char *dest = tilemap_get_dest(argv[2]);
    if (!dest) return;

    int vx = getinteger(argv[4]);
    int vy = getinteger(argv[6]);
    int sx = getinteger(argv[8]);
    int sy = getinteger(argv[10]);
    int vw = getint(argv[12], 1, 3840);
    int vh = getint(argv[14], 1, 2160);
    int transparent = -1;
    if (argc == 17) transparent = getint(argv[16], -1, 15);

    tm->view_x = vx;
    tm->view_y = vy;

    HResS = tm->flash_w;
    VResS = tm->flash_h;
    HResD = HRes;
    VResD = VRes;

    int tw = tm->tile_w;
    int th = tm->tile_h;

    int col_start = vx / tw;
    int row_start = vy / th;
    int col_end   = (vx + vw - 1) / tw;
    int row_end   = (vy + vh - 1) / th;

    int offset_x = vx % tw;
    int offset_y = vy % th;

    for (int r = row_start; r <= row_end; r++) {
        for (int c = col_start; c <= col_end; c++) {
            if (c < 0 || c >= tm->map_cols || r < 0 || r >= tm->map_rows) continue;

            int tile = (int)tm->map[c + r * tm->map_cols];
            if (tile == 0) continue;

            int src_col = (tile - 1) % tm->tiles_per_row;
            int src_row = (tile - 1) / tm->tiles_per_row;
            int src_x = src_col * tw;
            int src_y = src_row * th;

            int dst_x = sx + (c - col_start) * tw - offset_x;
            int dst_y = sy + (r - row_start) * th - offset_y;

            blit121(tm->flash_data, dest, src_x, src_y, tw, th, dst_x, dst_y, transparent);
        }
    }
}

/* TILEMAP SCROLL id, dx, dy */
static void tilemap_cmd_scroll(unsigned char *p)
{
    getargs(&p, 5, (unsigned char *)",");
    if (argc != 5) error("Syntax");

    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (!tm->active) error("Tilemap % not created", id + 1);

    int dx = getinteger(argv[2]);
    int dy = getinteger(argv[4]);

    tm->view_x += dx;
    tm->view_y += dy;

    int max_x = tm->map_cols * tm->tile_w - HRes;
    int max_y = tm->map_rows * tm->tile_h - VRes;
    if (tm->view_x < 0) tm->view_x = 0;
    if (tm->view_y < 0) tm->view_y = 0;
    if (max_x > 0 && tm->view_x > max_x) tm->view_x = max_x;
    if (max_y > 0 && tm->view_y > max_y) tm->view_y = max_y;
}

/* TILEMAP VIEW id, x, y */
static void tilemap_cmd_view(unsigned char *p)
{
    getargs(&p, 5, (unsigned char *)",");
    if (argc != 5) error("Syntax");

    int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
    tilemap_t *tm = &tilemaps[id];
    if (!tm->active) error("Tilemap % not created", id + 1);

    tm->view_x = getinteger(argv[2]);
    tm->view_y = getinteger(argv[4]);
}

/* ============================================================================
 * cmd_tilemap dispatcher
 * ============================================================================ */
void MIPS16 cmd_tilemap(void)
{
    unsigned char *p;

    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        tilemap_cmd_create(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"ATTR"))) {
        tilemap_cmd_attr(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"DESTROY"))) {
        tilemap_cmd_destroy(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"SET"))) {
        tilemap_cmd_set(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"DRAW"))) {
        tilemap_cmd_draw(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"SCROLL"))) {
        tilemap_cmd_scroll(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"VIEW"))) {
        tilemap_cmd_view(p);
    } else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        tilemap_closeall();
        sprite_closeall();
    } else if ((p = checkstring(cmdline, (unsigned char *)"SPRITE"))) {
        unsigned char *q;
        if      ((q = checkstring(p, (unsigned char *)"CREATE")))  sprite_cmd_create(q);
        else if ((q = checkstring(p, (unsigned char *)"MOVE")))    sprite_cmd_move(q);
        else if ((q = checkstring(p, (unsigned char *)"SET")))     sprite_cmd_set(q);
        else if ((q = checkstring(p, (unsigned char *)"DRAW")))    sprite_cmd_draw(q);
        else if ((q = checkstring(p, (unsigned char *)"DESTROY"))) sprite_cmd_destroy(q);
        else if (checkstring(p, (unsigned char *)"CLOSE"))         sprite_closeall();
        else error("Syntax");
    } else {
        error("Syntax");
    }
}

/* ============================================================================
 * fun_tilemap dispatcher
 * ============================================================================ */
void MIPS16 fun_tilemap(void)
{
    unsigned char *p;

    if ((p = checkstring(ep, (unsigned char *)"TILE"))) {
        /* TILEMAP(TILE id, pixelX, pixelY) */
        getargs(&p, 5, (unsigned char *)",");
        if (argc != 5) error("Syntax");

        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);

        int px = getinteger(argv[2]);
        int py = getinteger(argv[4]);
        int col = px / tm->tile_w;
        int row = py / tm->tile_h;

        if (col < 0 || col >= tm->map_cols || row < 0 || row >= tm->map_rows)
            iret = 0;
        else
            iret = tm->map[col + row * tm->map_cols];
        targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"COLLISION"))) {
        /* TILEMAP(COLLISION id, x, y, w, h [, mask]) */
        getargs(&p, 11, (unsigned char *)",");
        if (!(argc == 9 || argc == 11)) error("Syntax");

        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);

        int bx = getinteger(argv[2]);
        int by = getinteger(argv[4]);
        int bw = getinteger(argv[6]);
        int bh = getinteger(argv[8]);
        int mask = 0;
        if (argc == 11) mask = getinteger(argv[10]);

        int col_start = bx / tm->tile_w;
        int row_start = by / tm->tile_h;
        int col_end   = (bx + bw - 1) / tm->tile_w;
        int row_end   = (by + bh - 1) / tm->tile_h;

        iret = 0;
        for (int r = row_start; r <= row_end && iret == 0; r++) {
            for (int c = col_start; c <= col_end && iret == 0; c++) {
                if (c >= 0 && c < tm->map_cols && r >= 0 && r < tm->map_rows) {
                    int tile = (int)tm->map[c + r * tm->map_cols];
                    if (tile != 0) {
                        if (mask == 0) {
                            iret = tile;
                        } else if (tm->attrs && tile <= tm->num_attrs) {
                            if (tm->attrs[tile - 1] & mask) iret = tile;
                        }
                    }
                }
            }
        }
        targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"ATTR"))) {
        /* TILEMAP(ATTR id, tileIndex) */
        getargs(&p, 3, (unsigned char *)",");
        if (argc != 3) error("Syntax");

        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);

        int tile = getint(argv[2], 1, 65535);
        if (!tm->attrs || tile > tm->num_attrs) iret = 0;
        else iret = tm->attrs[tile - 1];
        targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"VIEWX"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);
        iret = tm->view_x; targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"VIEWY"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);
        iret = tm->view_y; targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"COLS"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);
        iret = tm->map_cols; targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"ROWS"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        int id = getint(argv[0], 1, MAX_TILEMAPS) - 1;
        tilemap_t *tm = &tilemaps[id];
        if (!tm->active) error("Tilemap % not created", id + 1);
        iret = tm->map_rows; targ = T_INT;
    }
    else if ((p = checkstring(ep, (unsigned char *)"SPRITE"))) {
        unsigned char *q;
        if ((q = checkstring(p, (unsigned char *)"X"))) {
            getargs(&q, 1, (unsigned char *)",");
            if (argc != 1) error("Syntax");
            int id = getint(argv[0], 1, MAX_SPRITES) - 1;
            if (!sprites[id].active) error("Sprite % not created", id + 1);
            iret = sprites[id].x; targ = T_INT;
        }
        else if ((q = checkstring(p, (unsigned char *)"Y"))) {
            getargs(&q, 1, (unsigned char *)",");
            if (argc != 1) error("Syntax");
            int id = getint(argv[0], 1, MAX_SPRITES) - 1;
            if (!sprites[id].active) error("Sprite % not created", id + 1);
            iret = sprites[id].y; targ = T_INT;
        }
        else if ((q = checkstring(p, (unsigned char *)"TILE"))) {
            getargs(&q, 1, (unsigned char *)",");
            if (argc != 1) error("Syntax");
            int id = getint(argv[0], 1, MAX_SPRITES) - 1;
            if (!sprites[id].active) error("Sprite % not created", id + 1);
            iret = sprites[id].tile; targ = T_INT;
        }
        else if ((q = checkstring(p, (unsigned char *)"HIT"))) {
            /* TILEMAP(SPRITE HIT id1, id2) — bounding-box overlap test */
            getargs(&q, 3, (unsigned char *)",");
            if (argc != 3) error("Syntax");
            int id1 = getint(argv[0], 1, MAX_SPRITES) - 1;
            int id2 = getint(argv[2], 1, MAX_SPRITES) - 1;
            if (!sprites[id1].active) error("Sprite % not created", id1 + 1);
            if (!sprites[id2].active) error("Sprite % not created", id2 + 1);

            tilemap_t *tm1 = &tilemaps[sprites[id1].tilemap_ref];
            tilemap_t *tm2 = &tilemaps[sprites[id2].tilemap_ref];
            int w1 = tm1->tile_w, h1 = tm1->tile_h;
            int w2 = tm2->tile_w, h2 = tm2->tile_h;
            int ax = sprites[id1].x, ay = sprites[id1].y;
            int bx = sprites[id2].x, by = sprites[id2].y;

            iret = (ax < bx + w2 && ax + w1 > bx &&
                    ay < by + h2 && ay + h1 > by) ? 1 : 0;
            targ = T_INT;
        }
        else if ((q = checkstring(p, (unsigned char *)"W"))) {
            getargs(&q, 1, (unsigned char *)",");
            if (argc != 1) error("Syntax");
            int id = getint(argv[0], 1, MAX_SPRITES) - 1;
            if (!sprites[id].active) error("Sprite % not created", id + 1);
            iret = tilemaps[sprites[id].tilemap_ref].tile_w; targ = T_INT;
        }
        else if ((q = checkstring(p, (unsigned char *)"H"))) {
            getargs(&q, 1, (unsigned char *)",");
            if (argc != 1) error("Syntax");
            int id = getint(argv[0], 1, MAX_SPRITES) - 1;
            if (!sprites[id].active) error("Sprite % not created", id + 1);
            iret = tilemaps[sprites[id].tilemap_ref].tile_h; targ = T_INT;
        }
        else error("Syntax");
    }
    else {
        error("Syntax");
    }
}
