/*
 * drivers/editor_console/editor_console_vga.c — pure-VGA real impl
 * of the Editor.c console surface. Linked on the pure-VGA-family
 * ports (vga, vga_rp2350, vga_wifi_rp2350) where the tile fg/bg
 * arrays are 16-bit RGB121-packed (tilefcols/tilebcols). No HDMI _w
 * companion arrays exist on these ports.
 *
 * Also provides a no-op ScrollLCDSPISCR symbol for the editor's
 * MX470Scroll function-pointer comparison. spi_lcd.c (which defines
 * the real ScrollLCDSPISCR) is gated `#if !HAL_PORT_IS_VGA`, so the
 * symbol is otherwise missing on VGA family. SPIREAD is always false
 * on VGA so the comparison's branch never executes.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include <string.h>
#include "hal/hal_editor_console.h"
#include "hal/hal_keyboard.h"

void ScrollLCDSPISCR(int lines) { (void)lines; }

void hal_editor_tile_save(int xt, int yt, uint16_t *fc_out, uint16_t *bc_out) {
    if (fc_out) *fc_out = tilefcols[yt * X_TILE + xt];
    if (bc_out) *bc_out = tilebcols[yt * X_TILE + xt];
}

void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc) {
    for (int i = xt_start; i < xt_end; i++) {
        tilefcols[yt * X_TILE + i] = fc;
        tilebcols[yt * X_TILE + i] = bc;
    }
}

void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int i = xt_start; i < xt_end; i++) {
        tilefcols[yt * X_TILE + i] = fc;
        tilebcols[yt * X_TILE + i] = bc;
    }
}

void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int x = xt_start; x < X_TILE; x++) {
        tilefcols[yt * X_TILE + x] = fc;
        tilebcols[yt * X_TILE + x] = bc;
    }
}

void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int y = yt_start; y < Y_TILE; y++) {
        for (int x = 0; x < X_TILE; x++) {
            tilefcols[y * X_TILE + x] = fc;
            tilebcols[y * X_TILE + x] = bc;
        }
    }
}

void hal_editor_tile_drawline(int yt, int fc_rgb) {
    /* Pure-VGA editor draws the divider line as RGB121-packed
     * MAGENTA (0x9999) — original used the literal value when
     * rendering. RGB121pack(MAGENTA) reduces to 0x9999. */
    uint16_t fc = RGB121pack(fc_rgb);
    int span = HRes / 8;
    for (int i = 0; i < span; i++) {
        tilefcols[yt * X_TILE + i] = fc;
    }
}

void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on) {
    /* Pure-VGA always uses RGB121pack on tilebcols. r_on=1 paints
     * the cursor cell BLUE; r_on=0 paints the default bg. The
     * original literal 0x1111 == RGB121pack(BLUE). */
    uint16_t bc = r_on ? RGB121pack(BLUE) : RGB121pack(bc_rgb);
    int span = gui_font_width / 8;
    for (int i = 0; i < span; i++) {
        tilebcols[yt * X_TILE + x_pixel / 8 + i] = bc;
    }
}

extern int editactive;
extern void ResetDisplay(void);
extern void MX470Display(int fn);
extern void PrintStatus(void);
extern void PositionCursor(unsigned char *curp);

void hal_editor_display_enter(hal_editor_display_state_t *st) {
    st->modmode = 0;
    st->oldmode = DISPLAY_TYPE;
    st->oldfont = PromptFont;
    st->Y_TILE_save = Y_TILE;
    st->ytileheight_save = ytileheight;
    st->RefreshSave = 0;
    editactive = 1;
    if (HRes < 512) {
        DISPLAY_TYPE = SCREENMODE1;
        st->modmode = 1;
        ResetDisplay();
    }
    memset((void *)WriteBuf, 0, ScreenSize);
#ifdef rp2350
    if (DISPLAY_TYPE == SCREENMODE3) {
        for (int i = 0; i < 16; i++) map16[i] = remap[i] = i;
    }
#endif
    ytileheight = gui_font_height;
    Y_TILE = VRes / ytileheight;
    if (VRes % ytileheight) Y_TILE++;
}

void hal_editor_display_exit(const hal_editor_display_state_t *st) {
    Y_TILE = st->Y_TILE_save;
    ytileheight = st->ytileheight_save;
    if (st->modmode) {
        DISPLAY_TYPE = st->oldmode;
        ResetDisplay();
        SetFont(st->oldfont);
        PromptFont = st->oldfont;
        MX470Display(1);   /* DISPLAY_CLS == 1 */
    }
}

void hal_editor_modmode_font_select(void) {
    /* Pure-VGA always uses SetFont(1) — HDMI's FullColour/MediumRes
     * dispatch lives in editor_console_hdmi.c. */
    SetFont(1);
    PromptFont = 1;
}

/* -----------------------------------------------------------------
 * Mouse-cursor pump. Pure-VGA pump uses RGB121-packed tile colours
 * via the existing tile hooks. The static state is shared between
 * FullScreenEditor and MarkMode pumps.
 * ----------------------------------------------------------------- */
static short s_lastx1 = 9999, s_lasty1 = 9999;
static uint16_t s_lastfc, s_lastbc;
static bool s_leftpushed = false, s_rightpushed = false, s_middlepushed = false;

extern unsigned char *txtp;
extern unsigned char *EdBuff;
extern int curx, cury;
extern int VWidth, VHeight;

static void mouse_track_and_highlight(int fontinc) {
    if (!nunstruct[2].L) s_leftpushed = false;
    if (!nunstruct[2].R) s_rightpushed = false;
    if (!nunstruct[2].C) s_middlepushed = false;
    if (nunstruct[2].y1 != s_lasty1 || nunstruct[2].x1 != s_lastx1) {
        if (s_lastx1 != 9999) {
            hal_editor_tile_paint_saved(s_lastx1 * fontinc, (s_lastx1 + 1) * fontinc,
                                        s_lasty1, s_lastfc, s_lastbc);
        }
        s_lastx1 = nunstruct[2].x1;
        s_lasty1 = nunstruct[2].y1;
        if (s_lasty1 >= VHeight) s_lasty1 = VHeight - 1;
        hal_editor_tile_save(s_lastx1 * fontinc, s_lasty1, &s_lastfc, &s_lastbc);
        hal_editor_tile_paint_rgb(s_lastx1 * fontinc, (s_lastx1 + 1) * fontinc,
                                  s_lasty1, RED, WHITE);
    }
}

int hal_editor_mouse_main_pump(int fontinc, int *c_inout) {
    if (!hal_keyboard_external_mouse_active() || DISPLAY_TYPE != SCREENMODE1)
        return 1;
    mouse_track_and_highlight(fontinc);
    if ((nunstruct[2].L && !s_leftpushed && !s_rightpushed && !s_middlepushed) ||
        (nunstruct[2].R && !s_leftpushed && !s_rightpushed && !s_middlepushed) ||
        (nunstruct[2].C && !s_leftpushed && !s_rightpushed && !s_middlepushed)) {
        if (nunstruct[2].L) s_leftpushed = true;
        else if (nunstruct[2].R) s_rightpushed = true;
        else s_middlepushed = true;
        if (s_lastx1 >= 0 && s_lastx1 < VWidth && s_lasty1 >= 0 && s_lasty1 < VHeight) {
            ShowCursor(false);
            while (*txtp != 0 && s_lasty1 > cury)
                if (*txtp++ == '\n') cury++;
            while (txtp != EdBuff && s_lasty1 < cury)
                if (*--txtp == '\n') cury--;
            while (txtp != EdBuff && *(txtp - 1) != '\n') txtp--;
            for (curx = 0; curx < s_lastx1 && *txtp && *txtp != '\n'; curx++) txtp++;
            PositionCursor(txtp);
            PrintStatus();
            ShowCursor(true);
            hal_editor_tile_paint_saved(s_lastx1 * fontinc, (s_lastx1 + 1) * fontinc,
                                        s_lasty1, s_lastfc, s_lastbc);
        }
        if (s_rightpushed)       { *c_inout = F4; return 0; }
        else if (s_middlepushed) { *c_inout = F5; return 0; }
    }
    return 1;
}

int hal_editor_mouse_mark_pump(int fontinc, int *c_inout, unsigned char **mark_io) {
    if (!hal_keyboard_external_mouse_active() || DISPLAY_TYPE != SCREENMODE1)
        return 1;
    mouse_track_and_highlight(fontinc);
    if ((nunstruct[2].L && !s_leftpushed && !s_rightpushed && !s_middlepushed) ||
        (nunstruct[2].R && !s_leftpushed && !s_rightpushed && !s_middlepushed) ||
        (nunstruct[2].C && !s_leftpushed && !s_rightpushed && !s_middlepushed)) {
        if (nunstruct[2].L) s_leftpushed = true;
        else if (nunstruct[2].R) s_rightpushed = true;
        else s_middlepushed = true;
        if (s_lastx1 >= 0 && s_lastx1 < VWidth && s_lasty1 >= 0 && s_lasty1 < VHeight) {
            unsigned char *p = txtp;
            while (*p != 0 && s_lasty1 > cury)
                if (*p++ == '\n') cury++;
            while (p != EdBuff && s_lasty1 < cury)
                if (*--p == '\n') cury--;
            while (p != EdBuff && *(p - 1) != '\n') p--;
            for (curx = 0; curx < s_lastx1 && *p && *p != '\n'; curx++) p++;
            PositionCursor(p);
            *mark_io = p;
        }
        if (s_rightpushed)       { *c_inout = F4;   return 0; }
        if (s_middlepushed)      { *c_inout = F5;   return 0; }
        if (s_leftpushed)        { *c_inout = 9999; return 0; }
    }
    return 1;
}

void hal_editor_mouse_anchor_reset(void) {
    if (!hal_keyboard_external_mouse_active() || DISPLAY_TYPE != SCREENMODE1)
        return;
    nunstruct[2].ax = curx * FontTable[gui_font >> 4][0] * (gui_font & 0xf);
    nunstruct[2].ay = cury * FontTable[gui_font >> 4][1] * (gui_font & 0xf);
    s_lastx1 = 9999;
    s_lasty1 = 9999;
}
