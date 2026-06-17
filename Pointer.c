/***********************************************************************************************************************
PicoMite MMBasic

Pointer.c

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
/**
 * @file Pointer.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the pointing-device MMBasic commands and functions:
 *        the GUI CURSOR mouse/virtual cursor overlay, GUI CLICK, the
 *        touch gesture state machine and the TOUCH()/CLICK() functions.
 *        Split out of Draw.c.
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <limits.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "DrawInternal.h"

#if defined(PICOMITEVGA) || defined(GUICONTROLS)
/* ====================================================================
 *  Mouse / virtual cursor overlay (all GUI-capable builds)
 * --------------------------------------------------------------------
 * Save-the-pixels-underneath sprite cursor that mimics the Colour
 * Maximite 2's GUI CURSOR command set:
 *
 *   GUI CURSOR ON [cursorno [, x, y [, cursorcolour]]]
 *   GUI CURSOR OFF
 *   GUI CURSOR HIDE | SHOW
 *   GUI CURSOR COLOUR cursorcolour
 *   GUI CURSOR x, y
 *   GUI CURSOR LINK MOUSE | UNLINK MOUSE
 *
 * Two built-in sprites are provided: 0 = arrow pointer (13x19, hot
 * point at top-left (0,0)) and 1 = cross-hair (15x15, hot point at
 * centre (7,7)). The hot point is the click-meaningful pixel — when
 * the cursor "is at" (X, Y), that pixel lands at (X, Y) on screen and
 * the bitmap is drawn at (X-hotX, Y-hotY).
 *
 * The implementation uses the existing ReadBuffer / DrawBuffer /
 * DrawPixel function pointers so it's mode-agnostic — the mode-specific
 * pixel packing is hidden behind those primitives. The cursor follows
 * whatever WriteBuf currently is; cmd_framebuffer / closeframebuffer /
 * cmd_cls call CursorErase() before flipping WriteBuf so the save
 * buffer is never restored to the wrong target.
 *
 * State model:
 *   cursor_enabled  -- ON (true) vs OFF (false)
 *   cursor_hidden   -- HIDE/SHOW (only meaningful while enabled)
 *   cursor_linked   -- LINK MOUSE / UNLINK MOUSE
 *   cursor_no       -- 0 = arrow, 1 = cross
 *   cursor_x/y      -- stored position (= hot-point position on screen)
 *   cursor_color    -- COLOUR (used by cursor 0 and 1 only)
 * ==================================================================== */

/* --- Built-in cursor sprites ------------------------------------------ */

#define ARROW_CURSOR_W 13
#define ARROW_CURSOR_H 19
static const uint16_t arrow_cursor_bits[ARROW_CURSOR_H] = {
    0x0001, /* 7              */
    0x0003, /* 77             */
    0x0005, /* 7 7            */
    0x0009, /* 7  7           */
    0x0011, /* 7   7          */
    0x0021, /* 7    7         */
    0x0041, /* 7     7        */
    0x0081, /* 7      7       */
    0x0101, /* 7       7      */
    0x0201, /* 7        7     */
    0x0401, /* 7         7    */
    0x0801, /* 7          7   */
    0x1F81, /* 7      777777  */
    0x0091, /* 7   7  7       */
    0x0099, /* 7  77  7       */
    0x0125, /* 7 7  7  7      */
    0x0123, /* 77   7  7      */
    0x0240, /*       7  7     */
    0x03C0, /*       7777     */
};

#define CROSS_CURSOR_W 15
#define CROSS_CURSOR_H 15
static const uint16_t cross_cursor_bits[CROSS_CURSOR_H] = {
    0x0080, /*        7        */
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x7FFF, /*  777777777777777 (centre, hot point at col 7) */
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x0080,
    0x0080,
};

struct cursor_sprite
{
    const uint16_t *bits;
    int w, h;
    int hot_x, hot_y;
};

/* Maximum cursor sprite dimensions. Built-ins are arrow (13x19) and
   cross (15x15); loaded user sprites are capped here. 24x24 leaves
   headroom over the built-ins without paying for the full 32x32
   save/sprite buffers (which dominated BSS). */
#define MAX_CURSOR_W 24
#define MAX_CURSOR_H 24
#define NUM_BUILTIN_CURSORS 2
#define USER_CURSOR_SLOT 2 /* cursor_no == 2 means user-loaded sprite */

static const struct cursor_sprite builtin_cursors[NUM_BUILTIN_CURSORS] = {
    /* 0: arrow pointer — hot point at the tip (0,0) */
    {arrow_cursor_bits, ARROW_CURSOR_W, ARROW_CURSOR_H, 0, 0},
    /* 1: cross-hair — hot point at the centre (7,7) */
    {cross_cursor_bits, CROSS_CURSOR_W, CROSS_CURSOR_H, 7, 7},
};

/* --- User-loaded sprite (GUI CURSOR LOAD) ----------------------------
   Stored one byte per pixel: 0xFF = transparent, 0..15 = palette index
   (resolved at paint time via sprite_color_mode0[]). This is the same
   palette/encoding as the existing SPRITE LOAD command, just with a
   4-arg header (width, height, xoffset, yoffset) and exactly one
   sprite in the file. */
static struct
{
    bool loaded;
    int w, h;
    int hot_x, hot_y;
    uint8_t pixels[MAX_CURSOR_W * MAX_CURSOR_H];
} user_cursor = {false, 0, 0, 0, 0, {0}};

/* --- Save buffer & paint state --------------------------------------- */

/* Save buffer for pixels under the cursor. Allocated lazily from the
   MMBasic heap when the cursor is first painted, so a build that never
   enables the cursor pays zero RAM for it.
   Size: MAX_CURSOR_W*H pixels × 3 bytes/pixel (RGB121 modes emit
   24-bit RGB via ReadBuffer16/DrawBuffer16; RGB555/RGB332 emit less).
   Lifetime: nulled by CursorOnHeapWipe() during ClearRuntime because
   InitHeap(true) wipes the underlying allocation. */
#define CURSOR_SAVE_BUF_BYTES (MAX_CURSOR_W * MAX_CURSOR_H * 3)
static uint8_t *cursor_save_buf = NULL;

static bool cursor_painted = false;        /* true while pixels are on screen */
static int cursor_saved_x, cursor_saved_y; /* top-left of saved rect on screen */
static int cursor_saved_w, cursor_saved_h;
/* Last hot-point position we painted at; used to short-circuit
   no-movement refreshes. */
static int cursor_target_x = INT_MIN, cursor_target_y = INT_MIN;

/* --- User-visible state ---------------------------------------------- */

static bool cursor_enabled = false; /* ON/OFF */
static bool cursor_hidden = false;  /* HIDE/SHOW (only relevant while enabled) */
static bool cursor_linked = false;  /* LINK MOUSE / UNLINK MOUSE */
static int cursor_no = 0;           /* 0 = arrow, 1 = cross */
int cursor_x = 0;                   /* stored hot-point position */
int cursor_y = 0;
static int cursor_color = WHITE; /* applies to cursor 0/1 only */

/* When true, refresh is skipped entirely. Set by cmd_cls / mode change
   etc. so the cursor doesn't try to restore stale pixels after a bulk
   screen operation. */
volatile bool CursorSuspend = false;

/* Synthetic left-mouse-button state. Set by GUI CLICK DOWN / UP. The
   mS-timer edge detector ORs this with nunstruct[2].L when computing
   the button state, so software-driven clicks coexist with a real
   mouse — neither path's writes clobber the other. Zero overhead when
   GUI CLICK isn't used. */
volatile bool gui_click_synthetic_down = false;

/* GUI CLICK PIN: a physical input pin acts as a click source, the
   same way the touch IRQ or the mouse left button does. Lets the
   user wire a joystick fire-button (or similar) to drive the GUI
   even while BASIC is blocked inside MsgBox or a modal control.
   click_pin == 0 means the feature is disabled. click_pin_inv
   selects polarity: false = active-low (idle pulled up, pressed
   reads 0); true = active-high (idle pulled down, pressed reads 1). */
int click_pin = 0;
bool click_pin_inv = false;

/* True when the most recently latched click came from an emulated
   source (synthetic GUI CLICK or GUI CLICK PIN) rather than a real
   pointing device (touch panel or mouse). MsgBox refuses to open
   when this is set — the user can't reach the popup's buttons
   without a real pointer, so failing loudly beats hanging silently.
   Set / cleared by the mS-timer ISR at every down-edge. */
volatile bool gui_click_emulated = false;

static inline bool cursor_primitives_ready(void)
{
    return (void *)DrawPixel != (void *)DisplayNotSet && (void *)DrawBuffer != (void *)DisplayNotSet && (void *)ReadBuffer != (void *)DisplayNotSet && HRes > 0 && VRes > 0;
}

/* Restore the pixels that were under the cursor and mark it not
   painted. This is the *internal* hide — separate from the user's
   HIDE/SHOW state. If the save buffer was freed (ClearRuntime wiped
   the MMBasic heap), skip the restore — those pixels are gone with
   the buffer; the next paint will re-save fresh ones. */
void CursorErase(void)
{
    if (!cursor_painted)
        return;
    if (cursor_save_buf != NULL && cursor_saved_w > 0 && cursor_saved_h > 0 && cursor_primitives_ready())
    {
        DrawBuffer(cursor_saved_x, cursor_saved_y,
                   cursor_saved_x + cursor_saved_w - 1,
                   cursor_saved_y + cursor_saved_h - 1,
                   cursor_save_buf);
    }
    cursor_painted = false;
}

/* Back-compat alias used by callers outside this file (cmd_cls,
   cmd_framebuffer, closeframebuffer). They just want "wipe the cursor
   off the current WriteBuf before I change it" — same as CursorErase. */
void CursorHide(void) { CursorErase(); }

/* Reset all cursor state. Called from ClearRuntime() in MMBasic.c
   before InitHeap(true) wipes the BASIC heap. Matches MMBasic's
   convention that RUN resets everything except saved options — the
   user's program is expected to re-issue GUI CURSOR ON if it wants
   one. Also drops the GetMemory()'d save buffer reference, which
   would dangle once InitHeap(true) recycles the pool. */
void CursorOnHeapWipe(void)
{
    /* Save-buffer was on the about-to-be-wiped BASIC heap. */
    cursor_save_buf = NULL;
    cursor_painted = false;
    cursor_target_x = cursor_target_y = INT_MIN;

    /* User-visible cursor state — reset to boot defaults. */
    cursor_enabled = false;
    cursor_hidden = false;
    cursor_linked = false;
    cursor_no = 0;
    cursor_x = 0;
    cursor_y = 0;
    cursor_color = WHITE;

    /* Forget any GUI CURSOR LOAD-ed sprite — its pixel data lives in
       BSS and would survive, but selecting cursor 2 without an explicit
       LOAD in the new program would be a surprise. Consistent with
       MMBasic's reset-on-RUN convention. */
    user_cursor.loaded = false;

    /* Reset any synthetic-click state so a stuck GUI CLICK DOWN from
       the previous program doesn't bleed into the next one. */
    gui_click_synthetic_down = false;
}

/* Return the geometry of the currently-selected sprite. Returns
   false if cursor_no points at an unloaded user slot. */
static bool cursor_get_geometry(int *w, int *h, int *hot_x, int *hot_y)
{
    if (cursor_no >= 0 && cursor_no < NUM_BUILTIN_CURSORS)
    {
        const struct cursor_sprite *spr = &builtin_cursors[cursor_no];
        *w = spr->w;
        *h = spr->h;
        *hot_x = spr->hot_x;
        *hot_y = spr->hot_y;
        return true;
    }
    if (cursor_no == USER_CURSOR_SLOT && user_cursor.loaded)
    {
        *w = user_cursor.w;
        *h = user_cursor.h;
        *hot_x = user_cursor.hot_x;
        *hot_y = user_cursor.hot_y;
        return true;
    }
    return false;
}

/* Sprite pixel colour at (sx, sy) — returns -1 for transparent, else
   the 24-bit RGB to pass to DrawPixel. Built-ins are monochrome and
   use cursor_color; user-loaded sprites carry their own palette. */
static int cursor_get_sprite_pixel(int sx, int sy)
{
    if (cursor_no >= 0 && cursor_no < NUM_BUILTIN_CURSORS)
    {
        const struct cursor_sprite *spr = &builtin_cursors[cursor_no];
        return (spr->bits[sy] & (1u << sx)) ? cursor_color : -1;
    }
    if (cursor_no == USER_CURSOR_SLOT && user_cursor.loaded)
    {
        uint8_t v = user_cursor.pixels[sy * MAX_CURSOR_W + sx];
        if (v == 0xFF)
            return -1;
        return (int)sprite_color_mode0[v & 0x0F];
    }
    return -1;
}

/* Paint the current cursor sprite with its hot point at screen (x, y). */
static void CursorPaintAt(int x, int y)
{
    if (!cursor_primitives_ready())
        return;

    int sw, sh, hot_x, hot_y;
    if (!cursor_get_geometry(&sw, &sh, &hot_x, &hot_y))
    {
        cursor_painted = false;
        return;
    }

    /* Lazy-allocate the save buffer from the MMBasic heap. Done here
       (rather than at GUI CURSOR ON time) so a build that compiles in
       the cursor code but never enables it pays no heap either, and so
       the allocation is reclaimed cleanly when ClearRuntime() wipes
       the heap on RUN (CursorOnHeapWipe nulls our reference). */
    if (cursor_save_buf == NULL)
    {
        cursor_save_buf = (uint8_t *)GetMemory(CURSOR_SAVE_BUF_BYTES);
        if (cursor_save_buf == NULL)
        {
            /* Out of heap — give up silently rather than failing the
               BASIC program. The cursor just won't draw. */
            cursor_painted = false;
            return;
        }
    }

    /* Sprite top-left after applying the hot-point offset. */
    int sprite_x = x - hot_x;
    int sprite_y = y - hot_y;

    /* Clip the save rectangle to screen. */
    int x2 = sprite_x + sw - 1;
    int y2 = sprite_y + sh - 1;
    int sx = sprite_x, sy = sprite_y;
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y2 >= VRes)
        y2 = VRes - 1;
    cursor_saved_x = sx;
    cursor_saved_y = sy;
    cursor_saved_w = x2 - sx + 1;
    cursor_saved_h = y2 - sy + 1;
    if (cursor_saved_w <= 0 || cursor_saved_h <= 0)
    {
        cursor_painted = false;
        return;
    }
    /* Save the pixels we are about to overwrite. */
    ReadBuffer(cursor_saved_x, cursor_saved_y,
               cursor_saved_x + cursor_saved_w - 1,
               cursor_saved_y + cursor_saved_h - 1,
               cursor_save_buf);
    /* Draw the opaque cursor pixels. */
    for (int cy = 0; cy < sh; cy++)
    {
        int py = sprite_y + cy;
        if (py < 0 || py >= VRes)
            continue;
        for (int cx = 0; cx < sw; cx++)
        {
            int colour = cursor_get_sprite_pixel(cx, cy);
            if (colour < 0)
                continue; /* transparent */
            int px = sprite_x + cx;
            if (px < 0 || px >= HRes)
                continue;
            DrawPixel(px, py, colour);
        }
    }
    cursor_painted = true;
}

/* Load a CMM2-style cursor sprite file.
   File format (one sprite per file):
     Line 1: "width, height, xoffset, yoffset"
     Lines 2..N: <height> rows of <width> chars each, using the same
                 palette as SPRITE LOAD ('0'..'9' / 'A'..'F' = colour
                 indices 0..15, ' ' = transparent). Apostrophe lines
                 anywhere are treated as comments.
   On success, populates user_cursor and selects cursor_no = 2. */
static void cursor_load_from_file(const char *fname)
{
    int fnbr = FindFreeFileNbr();
    if (!InitSDCard())
        return;

    char fullname[STRINGSIZE];
    strncpy(fullname, fname, sizeof(fullname) - 1);
    fullname[sizeof(fullname) - 1] = 0;
    AppendDefaultExtension(fullname, ".spr");
    if (!BasicFileOpen(fullname, fnbr, FA_READ))
        error("File not found");

    unsigned char buff[256];
    /* Header */
    MMgetline(fnbr, (char *)buff);
    while (buff[0] == 39)
        MMgetline(fnbr, (char *)buff);

    unsigned char *z = buff;
    getargs(&z, 7, (unsigned char *)", ");
    if (argc != 7)
    {
        FileClose(fnbr);
        error("Cursor file header must be width, height, xoffset, yoffset");
    }
    int w = getinteger(argv[0]);
    int h = getinteger(argv[2]);
    int hx = getinteger(argv[4]);
    int hy = getinteger(argv[6]);
    if (w < 1 || w > MAX_CURSOR_W || h < 1 || h > MAX_CURSOR_H)
    {
        FileClose(fnbr);
        error("Cursor size out of range (1..24 each axis)");
    }
    if (hx < 0 || hx >= w || hy < 0 || hy >= h)
    {
        FileClose(fnbr);
        error("Cursor hot point outside sprite bounds");
    }

    /* Erase current cursor before we touch user_cursor.* — if cursor_no
       was already 2, painted state's geometry refers to the old sprite. */
    if (cursor_painted)
        CursorErase();

    /* Body: read h rows, each w characters wide. Pad short rows with
       spaces (= transparent). Decode straight into user_cursor.pixels —
       a 576-byte tmp buffer here used to bloat the stack pointlessly,
       since the data was just memcpy'd across at the end. user_cursor
       lives in BSS; .loaded is set last, so a mid-parse error leaves
       the sprite marked not-loaded and the partial pixels harmless. */
    memset(user_cursor.pixels, 0xFF, sizeof(user_cursor.pixels));
    for (int y = 0; y < h; y++)
    {
        MMgetline(fnbr, (char *)buff);
        while (buff[0] == 39)
            MMgetline(fnbr, (char *)buff);
        int len = (int)strlen((char *)buff);
        if (len < w)
            memset(&buff[len], ' ', w - len);
        for (int x = 0; x < w; x++)
        {
            int idx = spriteCharToColorIndex(buff[x]);
            user_cursor.pixels[y * MAX_CURSOR_W + x] =
                (idx < 0) ? 0xFF : (uint8_t)idx;
        }
    }
    FileClose(fnbr);

    user_cursor.w = w;
    user_cursor.h = h;
    user_cursor.hot_x = hx;
    user_cursor.hot_y = hy;
    user_cursor.loaded = true;
}

/* Called from routinechecks() ~every 10ms. Repaints if anything has
   moved; erases if state has become hidden/off; otherwise no-ops. */
void CursorRefresh(void)
{
    if (CursorSuspend)
        return;
    /* When linked, the cursor tracks the mouse — update the stored
       position from the live mouse state. This is the place where
       mouse motion becomes cursor motion. */
    if (cursor_enabled && cursor_linked)
    {
        cursor_x = (int)nunstruct[2].ax;
        cursor_y = (int)nunstruct[2].ay;
    }
    /* Off or hidden: ensure no pixels are on screen. */
    if (!cursor_enabled || cursor_hidden)
    {
        if (cursor_painted)
            CursorErase();
        return;
    }
    /* Visible: redraw only if the hot-point moved. */
    if (cursor_painted && cursor_x == cursor_target_x && cursor_y == cursor_target_y)
        return;
    CursorErase();
    cursor_target_x = cursor_x;
    cursor_target_y = cursor_y;
    CursorPaintAt(cursor_x, cursor_y);
}

/* Force the next CursorRefresh to repaint, even if the position
   hasn't moved (e.g. after changing cursor_no or cursor_color). */
static inline void cursor_invalidate(void)
{
    cursor_target_x = cursor_target_y = INT_MIN;
}

/* Sync the mouse state to the cursor's current position. Used by
   LINK MOUSE (so cursor doesn't teleport on the first refresh) and
   by ON (so the cursor starts at a known location). */
static inline void cursor_sync_mouse_to_cursor(void)
{
    nunstruct[2].ax = cursor_x;
    nunstruct[2].ay = cursor_y;
}

/* Is some kind of mouse currently attached/active?
   - USB HID: HID[1].Device_type == 2 (set by USBKeyboard.c / KeyboardMap.c
     when a HID mouse descriptor is enumerated).
   - PS/2: mouse0 (set by initMouse0 in mouse.c on a successful reset).
   PICOMITEBTH (BLE HID host) isn't a PICOMITEVGA variant, so not handled. */
static bool cursor_have_mouse(void)
{
#ifdef USBKEYBOARD
    return (HID[1].Device_type == 2);
#else
    return mouse0;
#endif
}

/* Is the GUI CLICK PIN currently "pressed"? Called from the mS-timer
   edge detector, from CLICK() / TOUCH() and from any wait loop that
   needs to know whether a click is still being held. Hot path, kept
   tiny — a single GPIO read plus a polarity flip. */
bool click_pin_pressed(void)
{
    if (click_pin == 0)
        return false;
    int level = gpio_get(PinDef[click_pin].GPno);
    return click_pin_inv ? (level != 0) : (level == 0);
}

/* ====================================================================
 *  CLICK() function — mouse-driven equivalent of TOUCH() on
 *  touch-screen builds. Mirrors the CMM2's CLICK() function.
 *
 *  CLICK(DOWN)    — 1 if the left mouse button is currently held down
 *  CLICK(UP)      — 1 if the left mouse button is currently released
 *  CLICK(LASTX)   — X coordinate of the last release (pen-up) event
 *  CLICK(LASTY)   — Y coordinate of the last release (pen-up) event
 *  CLICK(REF)     — control ref# currently being clicked, 0 if none
 *  CLICK(LASTREF) — control ref# of the last completed click
 *
 *  CurrentRef / LastRef / LastX / LastY are populated by
 *  ProcessTouch() in GUI.c when the mouse-button edge detector in
 *  PicoMite.c fires. Reading them here is just exposing that state
 *  to BASIC.
 * ==================================================================== */
#ifdef GUICONTROLS
void fun_click(void)
{
    /* CLICK() is source-agnostic: it reports whether ANYTHING is
       currently clicking. ORs three signals:
         - touch panel: TOUCH_DOWN (only relevant if a touch panel
           is wired, gated by TOUCH_GETIRQTRIS),
         - real mouse: nunstruct[2].L,
         - software-driven: gui_click_synthetic_down (GUI CLICK DOWN/UP).
       This makes CLICK() and TOUCH() interchangeable for typical
       GUI programs that don't care which input was used. TOUCH()
       remains the touch-panel-specific accessor for code that needs
       raw touch state. */
    int btn = (nunstruct[2].L || gui_click_synthetic_down || click_pin_pressed()) ? 1 : 0;
    if (TOUCH_GETIRQTRIS && TOUCH_DOWN)
        btn = 1;
    if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = btn;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = !btn;
    else if (checkstring(ep, (unsigned char *)"X"))
        /* CLICK(X) / CLICK(Y) mirror TOUCH(X) / TOUCH(Y): when a click
           is active, return the position from whichever source latched
           it (touch panel via GetTouch, else cursor_x/y which tracks
           mouse + synthetic). When nothing is down, return -1 so the
           caller can guard with the same idiom used for touch. */
        iret = btn ? (TOUCH_GETIRQTRIS && TOUCH_DOWN && !gui_click_from_mouse
                          ? GetTouch(GET_X_AXIS)
                          : cursor_x)
                   : TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = btn ? (TOUCH_GETIRQTRIS && TOUCH_DOWN && !gui_click_from_mouse
                          ? GetTouch(GET_Y_AXIS)
                          : cursor_y)
                   : TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"REF"))
        iret = CurrentRef;
    else if (checkstring(ep, (unsigned char *)"LASTREF"))
        iret = LastRef;
    else if (checkstring(ep, (unsigned char *)"LASTX"))
        iret = LastX;
    else if (checkstring(ep, (unsigned char *)"LASTY"))
        iret = LastY;
    else
        SyntaxError();
    targ = T_INT;
}

#endif /* GUICONTROLS — paused for the shared gesture machine */
#endif /* PICOMITEVGA || GUICONTROLS — also paused: the gesture machine is \
          broader than the cursor block (it serves every touch build that    \
          enables TOUCH_GESTURES), so it must sit OUTSIDE this guard. */

/* The gesture state machine is shared by every build that enables
   TOUCH_GESTURES (excludes PICOMITEMIN and the RP2040 WebMite — see
   Hardware_Includes.h), so it sits in its own guard rather than the
   cursor / GUICONTROLS fun_click/fun_touch blocks (which use GUI-only
   symbols like CurrentRef / GetTouch). Those resume after
   touch_gesture_pinch_end. */
#ifdef TOUCH_GESTURES
/* ====================================================================
 *  Touch gesture state machine.
 *  Modelled on the mouse double-click state machine in KeyboardMap.c:
 *  ProcessTouch (GUI.c) calls touch_gesture_on_down / _on_up at the
 *  edges, the helpers track the swipe in module-static state, and
 *  fun_touch reads it back via TOUCH(SWL/SWR/SWU/SWD) or TOUCH(SWIPE).
 *
 *  Direction codes:
 *    0 = none, 1 = left, 2 = right, 3 = up, 4 = down
 *
 *  Latching: a detected swipe stays latched until the next touch-down
 *  edge clears it. That lets the BASIC program poll TOUCH(SWL) etc.
 *  any time between gestures without race-of-read risk, the way
 *  nunstruct[n].Z works for the mouse double-click.
 * ==================================================================== */
static int16_t touch_swipe_start_x = 0;
static int16_t touch_swipe_start_y = 0;
static uint64_t touch_swipe_start_us = 0;
int touch_swipe_dir = 0;                   /* extern-visible via Draw.h */
int touch_tap = 0;                         /* 0 / 1, cleared on read */
int touch_longpress = 0;                   /* 0 / 1, cleared on read */
int touch_doubletap = 0;                   /* 0 / 1, cleared on read */
static bool touch_longpress_fired = false; /* set during hold; suppresses tap/swipe at lift */
static uint64_t touch_last_tap_us = 0;
static int16_t touch_last_tap_x = 0;
static int16_t touch_last_tap_y = 0;

/* Tunables — kept together so they're easy to tweak.
   TAP_MAX_DT       quick-press cap before a tap turns into a "held but
                    too short to be a long press"
   HOLD_MIN_DT      threshold below which a held finger isn't a long press
   STILL_MAX        pixel-radius the finger may wander and still count
                    as a tap / long press / double-tap second-touch
   DBL_TAP_WINDOW   max gap between two taps that get fused into a
                    double-tap
   DBL_TAP_RADIUS   max distance between the two taps' positions
   SWIPE_MAX_DT     above this dt the gesture is a slow drag, not a swipe
*/
#define TAP_MAX_DT 250000ULL
#define HOLD_MIN_DT 500000ULL
#define STILL_MAX 15
#define DBL_TAP_WINDOW 500000ULL
#define DBL_TAP_RADIUS 30
#define SWIPE_MAX_DT 600000ULL

void touch_gesture_on_down(int16_t x, int16_t y)
{
    touch_swipe_start_x = x;
    touch_swipe_start_y = y;
    touch_swipe_start_us = time_us_64();
    touch_swipe_dir = 0;
    touch_longpress_fired = false;
}

/* Called frequently while a touch is in progress (from the
   process_touch_report path for USB; could be wired into ProcessTouch
   for resistive panels too). Detects a long-press WHILE the finger is
   still down so UI code can show the menu / highlight without waiting
   for the lift. */
void touch_gesture_tick(int16_t cur_x, int16_t cur_y, bool is_down)
{
    if (!is_down)
        return;
    if (touch_swipe_start_us == 0)
        return;
    if (touch_longpress_fired)
        return;
    uint64_t dt = time_us_64() - touch_swipe_start_us;
    if (dt < HOLD_MIN_DT)
        return;
    int dx = (int)cur_x - (int)touch_swipe_start_x;
    int dy = (int)cur_y - (int)touch_swipe_start_y;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    if (adx > STILL_MAX || ady > STILL_MAX)
        return; /* moved too much — not a hold */
    touch_longpress = 1;
    touch_longpress_fired = true;
}

void touch_gesture_on_up(int16_t end_x, int16_t end_y)
{
    if (touch_swipe_start_us == 0)
        return;
    /* If long-press already fired during the hold, the touch is
       consumed — don't also report a tap or swipe for the same gesture. */
    if (touch_longpress_fired)
        return;
    uint64_t dt = time_us_64() - touch_swipe_start_us;
    int dx = end_x - touch_swipe_start_x;
    int dy = end_y - touch_swipe_start_y;
    /* Close the gesture window. Past this point only touch_swipe_start_x/y
       (the down position) are used, so zeroing start_us here makes every
       classification path below leave the machine idle — and stops a
       sampler that runs touch_gesture_tick() before the next
       touch_gesture_on_down() from seeing a stale start and mis-firing a
       long-press between gestures. */
    touch_swipe_start_us = 0;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    /* Swipe threshold ~15% of the shorter screen dimension, with a
       30-pixel floor; falls back to 30 px when HRes/VRes aren't
       initialised. */
    int min_dim = (HRes > 0 && VRes > 0)
                      ? (HRes < VRes ? HRes : VRes)
                      : 240;
    int sw_thresh = min_dim / 6;
    if (sw_thresh < 30)
        sw_thresh = 30;

    /* Try swipe first: large enough motion in the right time window. */
    if ((adx >= sw_thresh || ady >= sw_thresh) && dt <= SWIPE_MAX_DT)
    {
        if (adx > ady)
            touch_swipe_dir = (dx > 0) ? 2 : 1;
        else
            touch_swipe_dir = (dy > 0) ? 4 : 3;
        return;
    }

    /* Not a swipe. For tap / hold / double-tap classification the
       finger must have stayed roughly still. */
    if (adx > STILL_MAX || ady > STILL_MAX)
        return;

    if (dt >= HOLD_MIN_DT)
    {
        /* At-lift long-press fallback (covers callers that don't run
           the tick — resistive panels currently). */
        touch_longpress = 1;
        return;
    }
    if (dt <= TAP_MAX_DT)
    {
        /* Tap. Check whether it fuses with a previous tap to make a
           double-tap. */
        uint64_t now = time_us_64();
        if (touch_last_tap_us != 0 && (now - touch_last_tap_us) <= DBL_TAP_WINDOW)
        {
            int ddx = (int)touch_swipe_start_x - (int)touch_last_tap_x;
            int ddy = (int)touch_swipe_start_y - (int)touch_last_tap_y;
            int addx = (ddx < 0) ? -ddx : ddx;
            int addy = (ddy < 0) ? -ddy : ddy;
            if (addx <= DBL_TAP_RADIUS && addy <= DBL_TAP_RADIUS)
            {
                touch_doubletap = 1;
                touch_last_tap_us = 0; /* don't chain into triple-tap */
                return;
            }
        }
        touch_tap = 1;
        touch_last_tap_us = now;
        touch_last_tap_x = touch_swipe_start_x;
        touch_last_tap_y = touch_swipe_start_y;
    }
    /* else: between TAP_MAX_DT and HOLD_MIN_DT — not classified */
}

/* Two-finger gestures. Driven from process_touch_report when both
   contacts are simultaneously active. We capture the contact vector
   (x2-x1, y2-y1) at the moment both come down and again at the moment
   either lifts; that one vector pair is enough for all three
   classifications:
     pinch  — change in vector LENGTH (squared, to avoid sqrt)
     rotate — change in vector ANGLE (atan2 cross/dot)
     2-tap  — both contacts barely moved and the gesture was short
*/
static int16_t pinch_start_x1 = 0, pinch_start_y1 = 0;
static int16_t pinch_start_x2 = 0, pinch_start_y2 = 0;
static uint64_t pinch_start_us = 0;
static uint32_t touch_pinch_initial_dsq = 0;
int touch_pinch_dir = 0;  /* 0=none, 1=expand, 2=contract */
int touch_rotate_dir = 0; /* 0=none, 1=CW, 2=CCW */
int touch_twotap = 0;     /* 0 or 1 */

/* ~15° angular threshold for rotate. atan2 returns -π..π; 15° = ~0.26 rad. */
#define ROTATE_RAD_THRESHOLD 0.26f
/* Per-contact stillness for two-finger tap. */
#define TWOTAP_STILL_MAX 15
#define TWOTAP_MAX_DT 300000ULL

void touch_gesture_pinch_start(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    pinch_start_x1 = x1;
    pinch_start_y1 = y1;
    pinch_start_x2 = x2;
    pinch_start_y2 = y2;
    pinch_start_us = time_us_64();
    int dx = (int)x2 - (int)x1;
    int dy = (int)y2 - (int)y1;
    touch_pinch_initial_dsq = (uint32_t)(dx * dx + dy * dy);
    touch_pinch_dir = 0;
    touch_rotate_dir = 0;
    touch_twotap = 0;
}

void touch_gesture_pinch_end(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    if (touch_pinch_initial_dsq == 0)
        return;
    uint64_t dt = time_us_64() - pinch_start_us;
    int dx_init = (int)pinch_start_x2 - (int)pinch_start_x1;
    int dy_init = (int)pinch_start_y2 - (int)pinch_start_y1;
    int dx_end = (int)x2 - (int)x1;
    int dy_end = (int)y2 - (int)y1;
    uint32_t init_dsq = touch_pinch_initial_dsq;
    uint32_t final_dsq = (uint32_t)(dx_end * dx_end + dy_end * dy_end);
    touch_pinch_initial_dsq = 0;
    uint32_t change_dsq = (final_dsq > init_dsq)
                              ? (final_dsq - init_dsq)
                              : (init_dsq - final_dsq);

    /* 1. Pinch (distance ratio change ≥ 30%). */
    if (change_dsq >= 900)
    {
        if (final_dsq * 100u > init_dsq * 169u)
        {
            touch_pinch_dir = 1; /* expand */
            return;
        }
        if (final_dsq * 169u < init_dsq * 100u)
        {
            touch_pinch_dir = 2; /* contract */
            return;
        }
        /* Distance changed but ratio inconclusive — keep going. */
    }

    /* 2. Rotate (angle between contact vectors ≥ ~15°).
       cross = dx_init * dy_end - dy_init * dx_end
       dot   = dx_init * dx_end + dy_init * dy_end
       angle = atan2(cross, dot)  — radians, sign-preserving
       Screen Y is down, so positive cross = clockwise on screen. */
    float cross = (float)dx_init * (float)dy_end - (float)dy_init * (float)dx_end;
    float dot = (float)dx_init * (float)dx_end + (float)dy_init * (float)dy_end;
    if (cross != 0.0f || dot != 0.0f)
    {
        float angle = atan2f(cross, dot);
        if (angle > ROTATE_RAD_THRESHOLD)
        {
            touch_rotate_dir = 1; /* CW */
            return;
        }
        if (angle < -ROTATE_RAD_THRESHOLD)
        {
            touch_rotate_dir = 2; /* CCW */
            return;
        }
    }

    /* 3. Two-finger tap (both contacts still + short duration). */
    if (dt <= TWOTAP_MAX_DT)
    {
        int dx1 = (int)x1 - (int)pinch_start_x1;
        int dy1 = (int)y1 - (int)pinch_start_y1;
        int dx2 = (int)x2 - (int)pinch_start_x2;
        int dy2 = (int)y2 - (int)pinch_start_y2;
        int adx1 = (dx1 < 0) ? -dx1 : dx1;
        int ady1 = (dy1 < 0) ? -dy1 : dy1;
        int adx2 = (dx2 < 0) ? -dx2 : dx2;
        int ady2 = (dy2 < 0) ? -dy2 : dy2;
        if (adx1 <= TWOTAP_STILL_MAX && ady1 <= TWOTAP_STILL_MAX && adx2 <= TWOTAP_STILL_MAX && ady2 <= TWOTAP_STILL_MAX)
        {
            touch_twotap = 1;
        }
    }
}
#endif /* TOUCH_GESTURES — end of shared gesture state machine */

#if defined(PICOMITEVGA) || defined(GUICONTROLS) /* resume the cursor block */
#ifdef GUICONTROLS                               /* resume the GUICONTROLS-only fun_click/fun_touch block */
/* ====================================================================
 *  TOUCH() function — historically touch-panel-only, now extended to
 *  the same input-source coverage as CLICK() so a single BASIC program
 *  can run on VGA/HDMI (mouse), touch-LCD (touch + optional mouse),
 *  and touch-LCD-with-no-input (synthetic GUI CLICK).
 *
 *  Position semantics: when a click is currently active, return the
 *  position from whichever source owns it (via gui_click_from_mouse,
 *  set at the down-edge by the mS-timer detector). When nothing is
 *  down, return TOUCH_ERROR (-1) — matches the existing idiom
 *  programs use to gate on "is the user actually touching".
 *
 *  TOUCH() and CLICK() return the same values for the overlapping
 *  subcommands. TOUCH() additionally exposes X2/Y2 multi-touch on
 *  capacitive panels, which has no mouse equivalent, and a small
 *  gesture analyser (SWL/SWR/SWU/SWD/SWIPE).
 * ==================================================================== */
void fun_touch(void)
{
    unsigned char *tp;
    int btn = (nunstruct[2].L || gui_click_synthetic_down || click_pin_pressed()) ? 1 : 0;
    if (TOUCH_GETIRQTRIS && TOUCH_DOWN)
        btn = 1;
#ifdef USBKEYBOARD
    /* USB multi-touch acts as a third source for the touch button:
       contact 0 with tip=1 is treated the same as resistive pen-down.
       When active, TOUCH(X)/TOUCH(Y) return the scaled USB coords via
       GetTouch's USB-aware path (GUI.c). */
    if (usb_touch_active)
        btn = 1;
#endif
    /* Source of X/Y when the button is held:
         - hardware panel down and not in mouse-driven state  -> GetTouch()
         - USB touch active                                   -> GetTouch() (returns usb_touch_x/y)
         - everything else                                    -> cursor_x/y (mouse, GUI CLICK)
       The "real pointing device" check needs to fire for either
       physical panel or USB touch so we don't fall through to cursor
       coordinates while the user's finger is on the screen. */
    bool from_panel = (TOUCH_GETIRQTRIS && TOUCH_DOWN && !gui_click_from_mouse);
#ifdef USBKEYBOARD
    bool from_usb = usb_touch_active && !gui_click_from_mouse;
#else
    bool from_usb = false;
#endif
    if (checkstring(ep, (unsigned char *)"X"))
        iret = btn ? ((from_panel || from_usb)
                          ? GetTouch(GET_X_AXIS)
                          : cursor_x)
                   : TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = btn ? ((from_panel || from_usb)
                          ? GetTouch(GET_Y_AXIS)
                          : cursor_y)
                   : TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = btn;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = !btn;
    else if (checkstring(ep, (unsigned char *)"REF"))
        iret = CurrentRef;
    else if (checkstring(ep, (unsigned char *)"LASTREF"))
        iret = LastRef;
    else if (checkstring(ep, (unsigned char *)"LASTX"))
        iret = LastX;
    else if (checkstring(ep, (unsigned char *)"LASTY"))
        iret = LastY;
    /* Second contact point — TOUCH(X2) / TOUCH(Y2). Two possible
       sources:
         - capacitive resistive panel with Option.TOUCH_CAP set
           (non-PICOMITEVGA builds only — VGA/HDMI Option layout
           doesn't carry TOUCH_CAP)
         - USB multi-touch contact 1 (any USBKEYBOARD build)
       The first source that has live data wins. Returns TOUCH_ERROR
       when neither has a second contact, matching the X/Y behaviour
       when no touch is happening. */
    else if (checkstring(ep, (unsigned char *)"X2"))
    {
        int x2 = TOUCH_ERROR;
#ifndef PICOMITEVGA
        if (Option.TOUCH_CAP)
            x2 = GetTouch(GET_X_AXIS2);
#endif
#ifdef USBKEYBOARD
        if (x2 == TOUCH_ERROR && usb_touch_active2)
            x2 = usb_touch_x2;
#endif
        iret = x2;
    }
    else if (checkstring(ep, (unsigned char *)"Y2"))
    {
        int y2 = TOUCH_ERROR;
#ifndef PICOMITEVGA
        if (Option.TOUCH_CAP)
            y2 = GetTouch(GET_Y_AXIS2);
#endif
#ifdef USBKEYBOARD
        if (y2 == TOUCH_ERROR && usb_touch_active2)
            y2 = usb_touch_y2;
#endif
        iret = y2;
    }
    /* nth contact — TOUCH(XN n) / TOUCH(YN n), n = 1..MAX_TOUCH_CONTACTS.
       Same two-source merge as X2/Y2 (hardware panel first, then USB
       multi-touch), but generalised to every contact: n=1 matches X/Y,
       n=2 matches X2/Y2, and a GT911 or USB panel can carry more. Returns
       TOUCH_ERROR for any contact that isn't currently touching, so a BASIC
       loop can count fingers by walking n until it sees -1. */
    else if ((tp = checkstring(ep, (unsigned char *)"XN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        if (n == 0)
        {
            /* Contact count — hardware panel first, USB as fallback, the
               same source precedence used per-contact below. */
            int cnt = 0;
#ifndef PICOMITEVGA
            cnt = GetTouchCount();
#endif
#ifdef USBKEYBOARD
            if (cnt == 0)
                cnt = usb_touch_count;
#endif
            iret = cnt;
        }
        else
        {
            int xn = TOUCH_ERROR;
#ifndef PICOMITEVGA
            xn = GetTouchN(n - 1, GET_X_AXIS);
#endif
#ifdef USBKEYBOARD
            if (xn == TOUCH_ERROR && (n - 1) < usb_touch_count)
                xn = usb_touch_xn[n - 1];
#endif
            iret = xn;
        }
    }
    else if ((tp = checkstring(ep, (unsigned char *)"YN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        if (n == 0)
        {
            int cnt = 0;
#ifndef PICOMITEVGA
            cnt = GetTouchCount();
#endif
#ifdef USBKEYBOARD
            if (cnt == 0)
                cnt = usb_touch_count;
#endif
            iret = cnt;
        }
        else
        {
            int yn = TOUCH_ERROR;
#ifndef PICOMITEVGA
            yn = GetTouchN(n - 1, GET_Y_AXIS);
#endif
#ifdef USBKEYBOARD
            if (yn == TOUCH_ERROR && (n - 1) < usb_touch_count)
                yn = usb_touch_yn[n - 1];
#endif
            iret = yn;
        }
    }
    /* Single-finger swipe gestures. Latched at the touch-up edge by
       touch_gesture_on_up() (see gesture section above). Each one-shot
       accessor consumes its matching direction on read (mirroring the
       mouse double-click flag): reading TOUCH(SWL) on a left swipe
       returns 1 and clears; reading TOUCH(SWR) on the same left swipe
       returns 0 without clearing, so a polling loop that tests SWL
       first then SWR doesn't accidentally lose the event.
       TOUCH(SWIPE) returns the code (0/1/2/3/4) AND clears, so it's
       the natural form for a SELECT CASE dispatch. */
    else if (checkstring(ep, (unsigned char *)"SWL"))
    {
        if (touch_swipe_dir == 1)
        {
            iret = 1;
            touch_swipe_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWR"))
    {
        if (touch_swipe_dir == 2)
        {
            iret = 1;
            touch_swipe_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWU"))
    {
        if (touch_swipe_dir == 3)
        {
            iret = 1;
            touch_swipe_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWD"))
    {
        if (touch_swipe_dir == 4)
        {
            iret = 1;
            touch_swipe_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWIPE"))
    {
        iret = touch_swipe_dir;
        touch_swipe_dir = 0;
    }
    /* Two-finger pinch gestures. Latched when the second contact
       lifts (or the first does, whichever ends the dual-touch).
       Same clear-on-matching-read semantics as the swipes. */
    else if (checkstring(ep, (unsigned char *)"EXPAND"))
    {
        if (touch_pinch_dir == 1)
        {
            iret = 1;
            touch_pinch_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"CONTRACT"))
    {
        if (touch_pinch_dir == 2)
        {
            iret = 1;
            touch_pinch_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"PINCH"))
    {
        iret = touch_pinch_dir; /* 0=none, 1=expand, 2=contract */
        touch_pinch_dir = 0;
    }
    /* Single-finger tap / long-press / double-tap. All clear-on-read. */
    else if (checkstring(ep, (unsigned char *)"TAP"))
    {
        iret = touch_tap;
        touch_tap = 0;
    }
    else if (checkstring(ep, (unsigned char *)"HOLD"))
    {
        iret = touch_longpress;
        touch_longpress = 0;
    }
    else if (checkstring(ep, (unsigned char *)"DTAP"))
    {
        iret = touch_doubletap;
        touch_doubletap = 0;
    }
    /* Two-finger rotate / two-finger tap. Same clear-on-read pattern. */
    else if (checkstring(ep, (unsigned char *)"CW"))
    {
        if (touch_rotate_dir == 1)
        {
            iret = 1;
            touch_rotate_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"CCW"))
    {
        if (touch_rotate_dir == 2)
        {
            iret = 1;
            touch_rotate_dir = 0;
        }
        else
            iret = 0;
    }
    else if (checkstring(ep, (unsigned char *)"ROTATE"))
    {
        iret = touch_rotate_dir; /* 0=none, 1=CW, 2=CCW */
        touch_rotate_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"TTAP"))
    {
        iret = touch_twotap;
        touch_twotap = 0;
    }
    else
        SyntaxError();
    targ = T_INT;
}
#endif

/* Handle the GUI CURSOR subcommand. Returns true if cmdline matched
   "CURSOR", so callers know to stop further dispatch. CMM2-compatible
   syntax: ON [n[,x,y[,c]]] / OFF / HIDE / SHOW / COLOUR c /
   LINK MOUSE / UNLINK MOUSE / LOAD "file" / x,y. */
bool cursor_handle_gui_subcommand(unsigned char *cmdline_in)
{
    unsigned char *p, *q;
    if ((p = checkstring(cmdline_in, (unsigned char *)"CURSOR")) == NULL)
        return false;

    /* --- OFF: fully disable, erase from screen ------------------ */
    if (checkstring(p, (unsigned char *)"OFF"))
    {
        CursorErase();
        cursor_enabled = false;
        cursor_hidden = false;
        cursor_linked = false;
        cursor_invalidate();
        return true;
    }

    /* --- HIDE: keep state, just stop drawing ------------------- */
    if (checkstring(p, (unsigned char *)"HIDE"))
    {
        cursor_hidden = true;
        /* CursorRefresh will erase on its next tick. */
        return true;
    }

    /* --- SHOW: redraw a hidden cursor at its stored position ---- */
    if (checkstring(p, (unsigned char *)"SHOW"))
    {
        cursor_hidden = false;
        cursor_invalidate();
        return true;
    }

    /* --- COLOUR <n>: recolour built-in cursors ---------------- */
    if ((q = checkstring(p, (unsigned char *)"COLOUR")) || (q = checkstring(p, (unsigned char *)"COLOR")))
    {
        getcsargs(&q, 1);
        if (argc != 1)
            SyntaxError();
        cursor_color = getint(argv[0], 0, WHITE);
        cursor_invalidate(); /* repaint with new colour */
        return true;
    }

    /* --- LINK MOUSE / UNLINK MOUSE ---------------------------- */
    if (checkstring(p, (unsigned char *)"LINK MOUSE"))
    {
        /* Refuse to link unless a mouse is actually present —
           otherwise the cursor would freeze at its current spot
           because nunstruct[2] never updates, and the user might
           assume the cursor is broken. */
        if (!cursor_have_mouse())
            error("No mouse connected");
        /* Snap the mouse to where the cursor is, so the first
           refresh after linking doesn't teleport the cursor. */
        cursor_sync_mouse_to_cursor();
        cursor_linked = true;
        cursor_invalidate();
        return true;
    }
    if (checkstring(p, (unsigned char *)"UNLINK MOUSE"))
    {
        cursor_linked = false;
        /* Stored cursor_x/y stays where it was; cursor freezes. */
        return true;
    }

    /* --- LOAD "fname": load a CMM2-format sprite ---------------- */
    if ((q = checkstring(p, (unsigned char *)"LOAD")))
    {
        getcsargs(&q, 1);
        if (argc != 1)
            SyntaxError();
        char *fname = (char *)getCstring(argv[0]);
        cursor_load_from_file(fname);
        /* Selecting the user sprite is the natural follow-up — match
           CMM2 behaviour where the loaded sprite becomes cursor 2. */
        if (cursor_painted)
            CursorErase();
        cursor_no = USER_CURSOR_SLOT;
        cursor_invalidate();
        return true;
    }

    /* --- ON [cursorno [, x, y [, cursorcolour]]] -------------- */
    if ((q = checkstring(p, (unsigned char *)"ON")))
    {
        /* The cursor sprite needs ReadBuffer to save the pixels it
           overwrites. Touch LCDs that don't support framebuffer
           readback (no ReadBuffer implementation) can still use
           GUI CURSOR x,y + GUI CLICK to drive controls without a
           visible cursor — they just can't enable the sprite. */
        if ((void *)ReadBuffer == (void *)DisplayNotSet)
            error("Display does not support framebuffer readback");
        int new_no = cursor_no;
        int new_x = (HRes > 0) ? HRes / 2 : 0;
        int new_y = (VRes > 0) ? VRes / 2 : 0;
        int new_color = cursor_color;
        bool centre = true;
        /* cursor_no 0..1 = built-ins, 2 = user-loaded (only valid if
           user_cursor.loaded). */
        int max_no = user_cursor.loaded ? USER_CURSOR_SLOT
                                        : (NUM_BUILTIN_CURSORS - 1);

        if (*q != 0 && *q != '\'')
        {
            getcsargs(&q, 7);
            /* Allowed shapes: <no> | <no>,<x>,<y> | <no>,<x>,<y>,<colour> */
            if (!(argc == 1 || argc == 5 || argc == 7))
                SyntaxError();
            new_no = getint(argv[0], 0, max_no);
            if (argc >= 5)
            {
                new_x = getint(argv[2], 0, (HRes > 0 ? HRes - 1 : 0x7FFFFFFF));
                new_y = getint(argv[4], 0, (VRes > 0 ? VRes - 1 : 0x7FFFFFFF));
                centre = false;
            }
            if (argc == 7)
                new_color = getint(argv[6], 0, WHITE);
        }
        /* If we'd changed sprite while painted, the save-buffer's
           geometry is wrong for the new sprite — erase cleanly first. */
        if (cursor_painted && new_no != cursor_no)
            CursorErase();
        cursor_no = new_no;
        cursor_x = new_x;
        cursor_y = new_y;
        cursor_color = new_color;
        cursor_hidden = false;
        cursor_enabled = true;
        /* If the ON call placed the cursor explicitly, don't disturb
           the mouse position. If we defaulted to centre, sync the
           mouse there so the user has a known starting point. */
        if (centre)
            cursor_sync_mouse_to_cursor();
        cursor_invalidate();
        return true;
    }

    /* --- GUI CURSOR x, y --------------------------------------
       Updates stored position. Does NOT toggle visibility — per
       CMM2 spec, "Does not display the cursor if hidden but just
       updates the location". Does NOT change LINK state — but
       since CursorRefresh overwrites cursor_x/y from the mouse on
       every tick while linked, a manual move while linked will be
       overridden on the next refresh; warp the mouse too so it
       sticks. */
    getcsargs(&p, 3);
    if (argc != 3)
        SyntaxError();
    cursor_x = getint(argv[0], 0, (HRes > 0 ? HRes - 1 : 0x7FFFFFFF));
    cursor_y = getint(argv[2], 0, (VRes > 0 ? VRes - 1 : 0x7FFFFFFF));
    if (cursor_linked)
        cursor_sync_mouse_to_cursor();
    cursor_invalidate();
    return true;
}

#ifdef GUICONTROLS
/* ====================================================================
 *  GUI CLICK — synthesise a click event at the cursor position
 * --------------------------------------------------------------------
 * Allows the GUI to be driven from anything that can run BASIC code —
 * GPIO buttons, analog joysticks, automation scripts, etc. — without
 * a real mouse. The mS-timer edge detector ORs nunstruct[2].L with
 * gui_click_synthetic_down, so software clicks and a physical mouse
 * coexist cleanly.
 *
 *   GUI CLICK DOWN        — button down at the cursor (held)
 *   GUI CLICK UP          — button up
 *   GUI CLICK             — momentary click at the current cursor
 *   GUI CLICK x, y        — move cursor to (x,y) + momentary click
 *
 * The two momentary forms are split into a 50 ms down wait + 80 ms
 * up wait. Because MMBasic only dispatches interrupts between
 * statements, the wait loops use cmd_pause's re-entry trick: when
 * check_interrupt() queues a GUI interrupt, the wait sets
 * InterruptReturn to its own command token and returns, so the
 * executor runs the interrupt and IRET re-enters us at the next
 * phase. See the long comment in the bare-CLICK branch below.
 * ==================================================================== */
bool click_handle_gui_subcommand(unsigned char *cmdline_in)
{
    unsigned char *p;
    if ((p = checkstring(cmdline_in, (unsigned char *)"CLICK")) == NULL)
        return false;

    /* GUI CLICK PIN <pin> [, INV]
     * GUI CLICK PIN OFF
     *
     * Designates a physical input pin as a click source. The
     * mS-timer ISR polls the pin alongside the touch IRQ and the
     * mouse left button, latching coordinates from the soft cursor
     * on the down-edge. This lets the GUI be driven from a
     * joystick fire-button (or similar) even while BASIC is blocked
     * inside MsgBox or another modal control — the BASIC main loop
     * never has to run for the click to register.
     *
     * Polarity:
     *   default   active-low  (idle pulled up,   pressed reads 0)
     *   , INV     active-high (idle pulled down, pressed reads 1)
     *
     * The pin must be OFF (unconfigured) when this is issued; we
     * then claim it as a digital input with the appropriate pull.
     * GUI CLICK PIN OFF releases the pin back to EXT_NOT_CONFIG.
     *
     * This subcommand only configures GPIO; it doesn't depend on
     * the executor's between-command interrupt dispatch, so it's
     * safe to call from inside an interrupt routine. Handled here,
     * before the interrupt-context guard further down. */
    {
        unsigned char *q;
        if ((q = checkstring(p, (unsigned char *)"PIN")) != NULL)
        {
            if (checkstring(q, (unsigned char *)"OFF"))
            {
                if (click_pin)
                {
                    ExtCfg(click_pin, EXT_NOT_CONFIG, 0);
                    click_pin = 0;
                    click_pin_inv = false;
                }
                return true;
            }

            /* Force the user to release any existing click pin before
               assigning a new one; otherwise re-issuing the command
               with a different pin would leak the previous claim. */
            if (click_pin != 0)
                error("GUI CLICK PIN already set; use GUI CLICK PIN OFF first");

            getcsargs(&q, 3);
            if (argc != 1 && argc != 3)
                SyntaxError();

            int new_pin = getpinarg(argv[0]);
            bool new_inv = false;
            if (argc == 3)
            {
                if (checkstring(argv[2], (unsigned char *)"INV"))
                    new_inv = true;
                else
                    SyntaxError();
            }

            /* Refuse to steal a pin already configured for something
               else — the user must release it first. */
            if (ExtCurrentConfig[new_pin] != EXT_NOT_CONFIG)
                StandardErrorParam2(27, new_pin, new_pin);

            /* Hand the pin to the digital-input subsystem with the
               appropriate internal pull. Active-low → pull-up (idle
               high, button to GND pulls it down). Active-high → the
               PULLDOWN sequence mirrors how SETPIN .. DIN, PULLDOWN
               sets the pin (TRISCLR+LATCLR+TRISSET on RP2350) so
               the pin stays at zero when not driven. */
            if (new_inv)
            {
#ifdef rp2350
                PinSetBit(new_pin, TRISCLR);
                PinSetBit(new_pin, LATCLR);
                PinSetBit(new_pin, TRISSET);
#endif
                ExtCfg(new_pin, EXT_DIG_IN, CNPDSET);
            }
            else
            {
                ExtCfg(new_pin, EXT_DIG_IN, CNPUSET);
            }

            click_pin = new_pin;
            click_pin_inv = new_inv;
            return true;
        }
    }

    /* The remaining forms — GUI CLICK DOWN / UP and bare GUI CLICK —
       all rely on the BASIC executor running between statements to
       deliver the simulated click to any armed GUI INTERRUPT routine.
       From inside an interrupt that dispatch is blocked
       (checkdetailinterrupts short-circuits while InterruptReturn !=
       NULL), so DOWN/UP cause the user's interrupt to recurse
       silently once the current one IRETs, and the bare CLICK form
       additionally overwrites InterruptReturn (via cmd_pause-style
       re-entry) and corrupts the OUTER interrupt's return path.
       Refuse loudly so the programmer notices early. */
    if (InterruptReturn != NULL)
        error("GUI CLICK / DOWN / UP cannot be used in an interrupt routine");

    if (checkstring(p, (unsigned char *)"DOWN"))
    {
        /* The hit-test reads nunstruct[2].ax/.ay at the down-edge —
           snap them to where the cursor is so the click goes to the
           control under the visible pointer. */
        nunstruct[2].ax = cursor_x;
        nunstruct[2].ay = cursor_y;
        gui_click_synthetic_down = true;
        return true;
    }
    if (checkstring(p, (unsigned char *)"UP"))
    {
        gui_click_synthetic_down = false;
        return true;
    }

    /* Bare GUI CLICK / GUI CLICK x, y — momentary forms.
     *
     * MMBasic only dispatches interrupts between statements. The
     * obvious "set flag, busy-wait, clear flag" loop swallows any
     * GUI interrupt queued during the wait — checkdetailinterrupts
     * sets nextstmt to the interrupt vector while we're still inside
     * the command, and by the time control returns to the executor
     * the click has already been torn down so the queued dispatch
     * goes nowhere visible.
     *
     * Borrow cmd_pause's trick: when check_interrupt() queues an
     * interrupt during the wait, point InterruptReturn at our OWN
     * command token and return. The executor then runs the queued
     * interrupt; its IRET hands control back to us, and the static
     * `phase` distinguishes a fresh entry (start the down phase)
     * from a resume in the down wait (after the TouchDown interrupt
     * ran) from a resume in the up wait (after TouchUp ran).
     *
     *   phase 0: fresh entry — parse args, start down phase
     *   phase 1: down wait (synthetic_down = true)
     *   phase 2: up   wait (synthetic_down = false)
     * Reset to 0 once the up wait expires.
     */
    static int phase = 0;
    static uint64_t until = 0;
    extern int check_interrupt(void);

    if (phase == 0)
    {
        if (*p != 0 && *p != '\'')
        {
            getcsargs(&p, 3);
            if (argc != 3)
                SyntaxError();
            cursor_x = getint(argv[0], 0, (HRes > 0 ? HRes - 1 : 0x7FFFFFFF));
            cursor_y = getint(argv[2], 0, (VRes > 0 ? VRes - 1 : 0x7FFFFFFF));
            if (cursor_linked)
                cursor_sync_mouse_to_cursor();
            cursor_invalidate();
        }
        nunstruct[2].ax = cursor_x;
        nunstruct[2].ay = cursor_y;
        gui_click_synthetic_down = true;
        until = time_us_64() + 50000;
        phase = 1;
    }

    if (phase == 1)
    {
        while (time_us_64() < until)
        {
            CheckAbort();
            if (check_interrupt())
            {
                /* Interrupt queued. Surrender so the executor runs
                   it; IRET returns us here with phase still == 1.
                   We resume the wait (or fall through to phase 2 if
                   `until` has already passed). */
                while (*cmdline && *cmdline != cmdtoken)
                    cmdline--;
                InterruptReturn = cmdline;
                return true;
            }
        }
        /* Down wait expired (interrupt fired and `until` passed, or
           it never fired). Move to the up phase. */
        gui_click_synthetic_down = false;
        until = time_us_64() + 80000;
        phase = 2;
    }

    if (phase == 2)
    {
        while (time_us_64() < until)
        {
            CheckAbort();
            if (check_interrupt())
            {
                while (*cmdline && *cmdline != cmdtoken)
                    cmdline--;
                InterruptReturn = cmdline;
                return true;
            }
        }
        /* Up wait expired. Click finished — reset for the next one. */
        phase = 0;
    }

    return true;
}
#endif /* GUICONTROLS */

#if defined(PICOMITEVGA) && defined(USBKEYBOARD) && !defined(GUICONTROLS)
/* ====================================================================
 *  Minimal TOUCH() / CLICK() for VGAUSB-style builds — PICOMITEVGA +
 *  USBKEYBOARD without GUICONTROLS (e.g. VGAUSB on RP2040, where the
 *  Ctrl[] heap reservation doesn't fit so GUICONTROLS is deliberately
 *  omitted, see CMakeLists.txt comment "RP2040 VGA is too tight on
 *  memory for the Ctrl[] heap reservation"). The USB host driver
 *  already decodes mouse and multi-touch reports and populates
 *  nunstruct[2] (mouse) and usb_touch_x/y/active (touch) — this gives
 *  BASIC programs a way to read that state without dragging in the
 *  full GUI-controls stack (Ctrl[], gesture state machine, soft
 *  cursor, swipe detector, etc.).
 *
 *  Supported subcommands:
 *    TOUCH(X) / TOUCH(Y)       primary contact position, -1 if no touch
 *    TOUCH(DOWN) / TOUCH(UP)   button state
 *    TOUCH(X2) / TOUCH(Y2)     second contact, -1 if only one finger
 *    CLICK(X) / CLICK(Y)       active pointing-device position, -1 if up
 *    CLICK(DOWN) / CLICK(UP)   same button OR'd from USB touch + mouse
 *
 *  Unsupported here (full GUICONTROLS builds expose these):
 *    SWL/SWR/SWU/SWD/SWIPE     no gesture state machine on this build
 *    EXPAND/CONTRACT/CW/CCW    no two-finger gesture analyser
 *    TAP/DTAP/LONGPRESS/2TAP   same
 *    REF/LASTREF/LASTX/LASTY   no Ctrl[] array to reference
 *  Asking for any of these raises a syntax error so the program fails
 *  loudly rather than silently returning 0.
 * ==================================================================== */
#define VGAUSB_TOUCH_ERROR (-1)

void fun_touch(void)
{
    unsigned char *tp;
    /* Primary "is something touching" — USB touch contact 0 OR the
       (USB) mouse left button. */
    int btn = (usb_touch_active || nunstruct[2].L) ? 1 : 0;
    if (checkstring(ep, (unsigned char *)"X"))
        iret = btn ? (usb_touch_active ? usb_touch_x : nunstruct[2].ax)
                   : VGAUSB_TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = btn ? (usb_touch_active ? usb_touch_y : nunstruct[2].ay)
                   : VGAUSB_TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = btn;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = !btn;
    else if (checkstring(ep, (unsigned char *)"X2"))
        iret = usb_touch_active2 ? usb_touch_x2 : VGAUSB_TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"Y2"))
        iret = usb_touch_active2 ? usb_touch_y2 : VGAUSB_TOUCH_ERROR;
    /* TOUCH(XN n) / TOUCH(YN n): nth USB multi-touch contact (n = 1..
       MAX_TOUCH_CONTACTS). n=1/2 match X/Y and X2/Y2; -1 when that contact
       isn't touching. n=0 returns the contact count. No hardware panel
       exists on this build, so the source is the USB digitizer only. */
    else if ((tp = checkstring(ep, (unsigned char *)"XN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        if (n == 0)
            iret = usb_touch_count;
        else
            iret = ((n - 1) < usb_touch_count) ? usb_touch_xn[n - 1] : VGAUSB_TOUCH_ERROR;
    }
    else if ((tp = checkstring(ep, (unsigned char *)"YN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        if (n == 0)
            iret = usb_touch_count;
        else
            iret = ((n - 1) < usb_touch_count) ? usb_touch_yn[n - 1] : VGAUSB_TOUCH_ERROR;
    }
    else
        SyntaxError();
    targ = T_INT;
}

void fun_click(void)
{
    int btn = (usb_touch_active || nunstruct[2].L) ? 1 : 0;
    if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = btn;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = !btn;
    else if (checkstring(ep, (unsigned char *)"X"))
        iret = btn ? (usb_touch_active ? usb_touch_x : nunstruct[2].ax)
                   : VGAUSB_TOUCH_ERROR;
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = btn ? (usb_touch_active ? usb_touch_y : nunstruct[2].ay)
                   : VGAUSB_TOUCH_ERROR;
    else
        SyntaxError();
    targ = T_INT;
}
#endif /* PICOMITEVGA && USBKEYBOARD && !GUICONTROLS */

#endif /* PICOMITEVGA || GUICONTROLS — end of cursor block */

/*  @endcond */
