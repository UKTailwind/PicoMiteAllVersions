/***********************************************************************************************************************
PicoMite MMBasic

Draw.c

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
 * @file Draw.c
 * @author Geoff Graham, Peter Mather
 * Thanks to ksinger for the ideas behind the SPRITE(B function
 * @brief Source for Graphics MMBasic commands and functions
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <stdarg.h>
#include <math.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/spi.h"
#include "Memory.h"
#include "DrawInternal.h"
#ifndef PICOMITEWEB
#include "pico/multicore.h"
extern mutex_t frameBufferMutex;
#endif

#ifdef PICOMITEWEB
#include "pico/cyw43_arch.h"
#endif
#if PICOMITERP2350
#include "VGA222.h"
#endif
#define LONG long
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))


// Maximum number of vertices for polygon fill operations
#define MAX_POLYGON_VERTICES 256



void DrawFilledCircle(int x, int y, int radius, int r, int fill, int ints_per_line, uint32_t *br, MMFLOAT aspect, MMFLOAT aspect2);
void SaveTriangle(int bnbr, char *buff);
void RestoreTriangle(int bnbr, char *buff);
void ReadLine(int x1, int y1, int x2, int y2, char *buff);
void cmd_RestoreTriangle(unsigned char *p);
void DrawCircleRingLineByLine(int x, int y, int r1, int r2, int c, MMFLOAT aspect, MMFLOAT aspect2);

/***************************************************************************/
// define the fonts

#include "font1.h"
#include "Misc_12x20_LE.h"
#include "Hom_16x24_LE.h"
#include "Fnt_10x16.h"
#include "Inconsola.h"
#include "ArialNumFontPlus.h"
#include "Font_8x6.h"
#include "arial_bold.h"
#ifdef PICOMITEVGA
#ifndef HDMI
#include "Include.h"
#endif
#endif
#include "smallfont.h"
#include "font-8x10.h"

unsigned char *FontTable[FONT_TABLE_SIZE] = {(unsigned char *)font1,
                                             (unsigned char *)Misc_12x20_LE,
#ifdef PICOMITEVGA
#ifdef HDMI
                                             (unsigned char *)Hom_16x24_LE,
#else
                                             (unsigned char *)arial_bold,
#endif
#else
                                             (unsigned char *)Hom_16x24_LE,
#endif
                                             (unsigned char *)Fnt_10x16,
                                             (unsigned char *)Inconsola,
                                             (unsigned char *)ArialNumFontPlus,
                                             (unsigned char *)F_6x8_LE,
                                             (unsigned char *)TinyFont,
                                             (unsigned char *)font8x10,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL};

/***************************************************************************/
// the default function for DrawRectangle() and DrawBitmap()

short gui_font;
int gui_fcolour;
int gui_bcolour;
volatile short low_x = silly_low, high_y = silly_high, low_y = silly_low, high_x = silly_high;
int PrintPixelMode = 0;

short CurrentX = 0, CurrentY = 0; // the current default position for the next char to be written
short DisplayHRes, DisplayVRes;   // the physical characteristics of the display


char CMM1 = 0;
// the MMBasic programming characteristics of the display
// note that HRes == 0 is an indication that a display is not configured
short HRes = 0, VRes = 0;
short lastx, lasty;
/* Runtime working copy of Option.VRes_reserved (the on-screen-keyboard
   strip height) expressed as a percentage of VRes, 0..50. Defined here
   (not in GUI.c) because GUI.c isn't compiled on every target — e.g.
   RP2040 VGA builds skip it — yet Draw.c / RGB121.c / Editor.c / FileIO.c
   reference this symbol unconditionally for their VResEdit math. On
   builds without the OSK the value stays 0 and (VRes - VRes*0/100) == VRes,
   so the math degenerates harmlessly. */
uint8_t OptionVResreserved;
const int CMM1map[16] = {BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, YELLOW, WHITE, MYRTLE, COBALT, MIDGREEN, CERULEAN, RUST, FUCHSIA, BROWN, LILAC};
int RGB121map[16];
// pointers to the drawing primitives

/* Sprite/cursor palette tables. Used by the legacy SPRITE LOAD command
   here AND by the GUI CURSOR overlay in Pointer.c (declared in
   DrawInternal.h along with the spriteCharToColorIndex() helper).
   Defined unconditionally so both call sites compile. */
// Mode 1 (alternate palette): ' '=0, 0-9, A-F map to specific colors
const uint32_t sprite_color_mode1[16] = {
    BLACK, BLUE, MYRTLE, COBALT, MIDGREEN, CERULEAN, GREEN, CYAN,
    RED, MAGENTA, RUST, FUCHSIA, BROWN, LILAC, YELLOW, WHITE};

// Mode 0 (standard palette): ' '=0, 0-9, A-F map to specific colors
const uint32_t sprite_color_mode0[16] = {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, YELLOW, WHITE,
    MYRTLE, COBALT, MIDGREEN, CERULEAN, RUST, FUCHSIA, BROWN, LILAC};


#ifdef PICOMITEVGA
#ifndef HDMI
uint32_t remap[256];
#else
uint32_t remap555[256];
uint32_t remap332[256];
uint16_t remap256[256];
#endif

#ifndef GUICONTROLS
/* On VGA/HDMI builds without GUI controls these globals live in Draw.c.
   When GUICONTROLS is enabled (mouse-driven GUI on RP2350 VGA/HDMI),
   GUI.c owns them — skip the Draw.c definitions to avoid duplicate
   symbols at link time. */
short gui_font_width, gui_font_height;
int last_bcolour, last_fcolour;
volatile int CursorTimer = 0; // used to time the flashing cursor
#endif
extern volatile int QVgaScanLine;
bool mergedread = 0;
int ScreenSize = 0;
#ifdef GUICONTROLS
extern int InvokingCtrl;
#endif
#endif /* PICOMITEVGA */

/* The mouse/virtual cursor overlay, GUI CLICK, the touch gesture state
   machine and the TOUCH()/CLICK() functions that used to sit between
   these two blocks now live in Pointer.c. */
#ifndef PICOMITEVGA
/* Non-VGA branch (was previously the #else of the outer PICOMITEVGA
   wrapper at the top of this section). */
volatile int ScrollStart;
int SSD1963data = 0;
int map[16] = {0};
#if defined(PICOMITEWEB) || defined(PICOMITEMIN)
#ifndef rp2350
short gui_font_width, gui_font_height;
int last_bcolour, last_fcolour;
volatile int CursorTimer = 0; // used to time the flashing cursor
int display_backlight;        // the brightness of the backlight (1 to 100)
#endif
extern int InvokingCtrl;
#else
extern int InvokingCtrl;
bool mergerunning = false;
volatile bool mergedone = false;
uint32_t mergetimer = 0;
#endif
#ifdef PICOMITEMIN
bool mergerunning = false;
volatile bool mergedone = false;
uint32_t mergetimer = 0;
#endif
#endif /* !PICOMITEVGA */
void cmd_ReadTriangle(unsigned char *p);
void (*DrawRectangle)(int x1, int y1, int x2, int y2, int c) = (void (*)(int, int, int, int, int))DisplayNotSet;
void (*DrawBitmap)(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap) = (void (*)(int, int, int, int, int, int, int, unsigned char *))DisplayNotSet;
void (*ScrollLCD)(int lines) = (void (*)(int))DisplayNotSet;
void (*DrawBuffer)(int x1, int y1, int x2, int y2, unsigned char *c) = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
void (*ReadBuffer)(int x1, int y1, int x2, int y2, unsigned char *c) = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
void (*DrawBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c) = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
void (*ReadBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c) = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
void (*DrawBufferFast)(int x1, int y1, int x2, int y2, int blank, unsigned char *c) = (void (*)(int, int, int, int, int, unsigned char *))DisplayNotSet;
void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char *c) = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
void (*DrawPixel)(int x1, int y1, int c) = (void (*)(int, int, int))DisplayNotSet;
void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill);
// these are the GUI commands that are common to the MX170 and MX470 versions
// in the case of the MX170 this function is called directly by MMBasic when the GUI command is used
// in the case of the MX470 it is called by MX470GUI in GUI.c
const int colours[16] = {0x00, 0xFF, 0x4000, 0x40ff, 0x8000, 0x80ff, 0xff00, 0xffff, 0xff0000, 0xff00FF, 0xff4000, 0xff40ff, 0xff8000, 0xff80ff, 0xffff00, 0xffffff};
void MIPS16 initFonts(void)
{
    FontTable[0] = (unsigned char *)font1;
    FontTable[1] = (unsigned char *)Misc_12x20_LE;
#ifdef PICOMITEVGA
#ifdef HDMI
    FontTable[2] = (unsigned char *)Hom_16x24_LE;
#else
    FontTable[2] = (unsigned char *)arial_bold;
#endif
#else
    FontTable[2] = (unsigned char *)Hom_16x24_LE;
#endif
    FontTable[3] = (unsigned char *)Fnt_10x16;
    FontTable[4] = (unsigned char *)Inconsola;
    FontTable[5] = (unsigned char *)ArialNumFontPlus;
    FontTable[6] = (unsigned char *)F_6x8_LE;
    FontTable[7] = (unsigned char *)TinyFont;
    FontTable[8] = (unsigned char *)font8x10;
    FontTable[9] = NULL;
    FontTable[10] = NULL;
    FontTable[11] = NULL;
    FontTable[12] = NULL;
    FontTable[13] = NULL;
    FontTable[14] = NULL;
    FontTable[15] = NULL;
}
#if PICOMITERP2350
uint16_t __not_in_flash_func(RGB565)(uint32_t c)
{
    return ((c >> 16) & 0b11111000) | ((c >> 13) & 0b00000111) | ((c << 3) & 0b1110000000000000) | ((c << 5) & 0b0001111100000000);
}
#endif

uint16_t __not_in_flash_func(RGB555)(uint32_t c)
{
    return ((c & 0xf8) >> 3) | ((c & 0xf800) >> 6) | ((c & 0xf80000) >> 9);
}
uint8_t __not_in_flash_func(RGB332)(uint32_t c)
{
    return (c >> 16 & 0xE0) | (c >> 11 & 0x1C) | (c >> 6 & 0x03);
}
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
/* GUI TEST LCDPANEL / GUI TEST TOUCH run until a console keypress. When the
   on-screen keyboard is the only keyboard (OPTION SCREEN KEYBOARD set, no
   external console) there is no key to press — the test pattern owns the
   panel — so the tests would be inescapable. While the OSK is configured a
   double tap (two pen-down edges 100..500 ms apart, landing within 50
   pixels of each other) exits instead. The proximity test matters for
   GUI TEST TOUCH, where rapid taps in different places are normal use
   and must not end the test. Pen-down is sampled via GetTouch(), which
   on these builds mirrors USB touch contact 0; the test loops already
   pump the USB host stack via getConsole() -> CheckAbort() ->
   routinechecks(). The caller owns the edge-tracking state so
   consecutive tests start clean. */
#define GUI_TEST_DOUBLETAP_MIN_US 100000ull
#define GUI_TEST_DOUBLETAP_MAX_US 500000ull
#define GUI_TEST_DOUBLETAP_RADIUS 50
static bool GuiTestDoubleTap(bool *wasdown, uint64_t *lastdown)
{
    static int lastx, lasty; // position of the previous down edge
    bool down, tapped = false;
    if (Option.VRes_reserved == 0)
        return false; // no OSK configured: keypress remains the only exit
    int x = GetTouch(GET_X_AXIS);
    down = (x != TOUCH_ERROR);
    if (down && !*wasdown)
    {
        int y = GetTouch(GET_Y_AXIS);
        uint64_t gap = time_us_64() - *lastdown;
        tapped = (gap >= GUI_TEST_DOUBLETAP_MIN_US && gap <= GUI_TEST_DOUBLETAP_MAX_US &&
                  abs(x - lastx) <= GUI_TEST_DOUBLETAP_RADIUS && abs(y - lasty) <= GUI_TEST_DOUBLETAP_RADIUS);
        *lastdown = time_us_64();
        lastx = x;
        lasty = y;
    }
    *wasdown = down;
    if (!tapped)
        return false;
    /* Wait for the finger to lift before returning so the still-held second
       tap can't register as a key press on the OSK that the post-test
       ClearScreen() is about to redraw. */
    while (GetTouch(GET_X_AXIS) != TOUCH_ERROR)
        CheckAbort();
    return true;
}
#endif
/*  @endcond */
void MIPS16 cmd_guiMX170(void)
{
    unsigned char *p;

    CheckDisplay(); // display a bitmap stored in an integer or string
#if defined(PICOMITEVGA) || defined(GUICONTROLS)
    /* GUI CURSOR — handles ON [,colour] / OFF / MOUSE / x,y / etc.
       Available on every build that has the cursor module compiled:
       VGA/HDMI variants, and all touch-LCD GUICONTROLS builds. */
    if (cursor_handle_gui_subcommand(cmdline))
        return;
#endif
#ifdef GUICONTROLS
    /* GUI CLICK — synthesise click events. Requires GUICONTROLS since
       the only thing to click on is GUI controls. */
    if (click_handle_gui_subcommand(cmdline))
        return;
#endif
    if ((p = checkstring(cmdline, (unsigned char *)"BITMAP")))
    {
        int x, y, fc, bc, h, w, scale, t, bytes;
        unsigned char *s;
        MMFLOAT f;
        long long int i64;

        getcsargs(&p, 15);
        if (!(argc & 1) || argc < 5)
            StandardError(2);

        // set the defaults
        h = 8;
        w = 8;
        scale = 1;
        bytes = 8;
        fc = gui_fcolour;
        bc = gui_bcolour;

        x = getinteger(argv[0]);
        y = getinteger(argv[2]);

        // get the type of argument 3 (the bitmap) and its value (integer or string)
        t = T_NOTYPE;
        evaluate(argv[4], &f, &i64, &s, &t, true);
        if (t & T_NBR)
            SyntaxError();
        else if (t & T_INT)
            s = (unsigned char *)&i64;
        else if (t & T_STR)
            bytes = *s++;

        if (argc > 5 && *argv[6])
            w = getint(argv[6], 1, HRes);
        if (argc > 7 && *argv[8])
            h = getint(argv[8], 1, VRes);
        if (argc > 9 && *argv[10])
            scale = getint(argv[10], 1, 15);
        if (argc > 11 && *argv[12])
            fc = getint(argv[12], 0, WHITE);
        if (argc == 15)
            bc = getint(argv[14], -1, WHITE);
        if (h * w > bytes * 8)
            error("Not enough data");
        DrawBitmap(x, y, w, h, scale, fc, bc, (unsigned char *)s);
        if (Option.Refresh)
            Display_Refresh();
        return;
    }
#ifndef PICOMITEVGA
#ifdef GUICONTROLS
    if ((p = checkstring(cmdline, (unsigned char *)"BEEP")))
    {
        if (Option.TOUCH_Click == 0)
            error("Click option not set");
        ClickTimer = getint(p, 0, INT_MAX) + 1;
        return;
    }
#endif
    if ((p = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        if ((checkstring(p, (unsigned char *)"LCDPANEL")))
        {
#ifdef PICOMITE
            if (mergerunning)
            {
                multicore_fifo_push_blocking(0xFF);
                busy_wait_ms(mergetimer + 200);
                if (mergerunning)
                {
                    SoftReset(SOFT_RESET);
                }
            }
#endif
            InitDisplaySPI(true);
            InitDisplayI2C(true);
            if ((Option.TOUCH_CS || Option.TOUCH_IRQ) && !Option.TOUCH_CAP)
            {
                GetTouchValue(CMD_PENIRQ_ON); // send the controller the command to turn on PenIRQ
                GetTouchAxis(CMD_MEASURE_X);
            }
            return;
        }
    }

    if ((p = checkstring(cmdline, (unsigned char *)"CALIBRATE")))
    {
        int tlx, tly, trx, try, blx, bly, brx, bry, midy;
        char *s;
        if (Option.TOUCH_CS == 0 && Option.TOUCH_IRQ == 0)
            error("Touch not configured");

        if (*p && *p != '\'')
        { // if the calibration is provided on the command line
            getcsargs(&p, 9);
            if (argc != 9)
                StandardError(2);
            Option.TOUCH_SWAPXY = getinteger(argv[0]);
            Option.TOUCH_XZERO = getinteger(argv[2]);
            Option.TOUCH_YZERO = getinteger(argv[4]);
            Option.TOUCH_XSCALE = getinteger(argv[6]) / 10000.0;
            Option.TOUCH_YSCALE = getinteger(argv[8]) / 10000.0;
            if (!CurrentLinePtr)
                SaveOptions();
            return;
        }
        else
        {
            if (CurrentLinePtr)
                StandardError(10);
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 0;
            Option.TOUCH_YZERO = 0;
            Option.TOUCH_XSCALE = 1.0f;
            Option.TOUCH_YSCALE = 1.0f;
        }
        calibrate = 1;
        GetCalibration(TARGET_OFFSET, TARGET_OFFSET, &tlx, &tly);
        GetCalibration(HRes - TARGET_OFFSET, TARGET_OFFSET, &trx, &try);
        if (abs(trx - tlx) < CAL_ERROR_MARGIN && abs(tly - try) < CAL_ERROR_MARGIN)
        {
            calibrate = 0;
            error("Touch hardware failure %,%,%,%", tlx, trx, tly, try);
        }

        GetCalibration(TARGET_OFFSET, VRes - TARGET_OFFSET, &blx, &bly);
        GetCalibration(HRes - TARGET_OFFSET, VRes - TARGET_OFFSET, &brx, &bry);
        calibrate = 0;
        midy = max(max(tly, try), max(bly, bry)) / 2;
        Option.TOUCH_SWAPXY = ((tly < midy && try > midy) || (tly > midy && try < midy));

        if (Option.TOUCH_SWAPXY)
        {
            swap(tlx, tly);
            swap(trx, try);
            swap(blx, bly);
            swap(brx, bry);
        }

        Option.TOUCH_XSCALE = (MMFLOAT)(HRes - TARGET_OFFSET * 2) / (MMFLOAT)(trx - tlx);
        Option.TOUCH_YSCALE = (MMFLOAT)(VRes - TARGET_OFFSET * 2) / (MMFLOAT)(bly - tly);
        Option.TOUCH_XZERO = ((MMFLOAT)tlx - ((MMFLOAT)TARGET_OFFSET / Option.TOUCH_XSCALE));
        Option.TOUCH_YZERO = ((MMFLOAT)tly - ((MMFLOAT)TARGET_OFFSET / Option.TOUCH_YSCALE));
        SaveOptions();
        brx = (HRes - TARGET_OFFSET) - ((brx - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);
        bry = (VRes - TARGET_OFFSET) - ((bry - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE);
        if (abs(brx) > CAL_ERROR_MARGIN || abs(bry) > CAL_ERROR_MARGIN)
        {
            s = "Warning: Inaccurate calibration\r\n";
        }
        else
            s = "Done. No errors\r\n";
        CurrentX = CurrentY = 0;
        MMPrintString(s);
        strcpy((char *)inpbuf, "Deviation X = ");
        IntToStr((char *)inpbuf + strlen((char *)inpbuf), brx, 10);
        strcat((char *)inpbuf, ", Y = ");
        IntToStr((char *)inpbuf + strlen((char *)inpbuf), bry, 10);
        strcat((char *)inpbuf, " (pixels)\r\n");
        MMPrintString((char *)inpbuf);
        if (!Option.DISPLAY_CONSOLE)
        {
            GUIPrintString(0, 0, 0x11, JUSTIFY_LEFT, JUSTIFY_TOP, ORIENT_NORMAL, WHITE, BLACK, s);
            GUIPrintString(0, 36, 0x11, JUSTIFY_LEFT, JUSTIFY_TOP, ORIENT_NORMAL, WHITE, BLACK, (char *)inpbuf);
        }
        return;
    }
#endif
    if ((p = checkstring(cmdline, (unsigned char *)"TEST")))
    {
        if ((checkstring(p, (unsigned char *)"LCDPANEL")))
        {
            int t, count = 0;
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
            bool tapdown = false;
            uint64_t taptime = 0;
#endif
            uint64_t start = time_us_64();
            t = ((HRes > VRes) ? HRes : VRes) / 7;
            while (getConsole() < '\r')
            {
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
                if (GuiTestDoubleTap(&tapdown, &taptime))
                    break;
#endif
                routinechecks();
#ifdef PICOMITEWEB
                {
                    if (startupcomplete)
                        ProcessWeb(1);
                }
#endif
                DrawCircle(rand() % HRes, rand() % VRes, (rand() % t) + t / 5, 1, 1, rgb((rand() % 8) * 256 / 8, (rand() % 8) * 256 / 8, (rand() % 8) * 256 / 8), 1);
                count++;
#ifdef PICOMITEVGA
#ifdef HDMI
                while (v_scanline != 0)
                {
                }
#else
                while (QVgaScanLine != 0)
                {
                }
#endif
#endif
            }
            ClearScreen(gui_bcolour);
            PFlt(((MMFLOAT)count) / (((MMFLOAT)(time_us_64() - start)) / 1000000.0));
            MMPrintString(" Circles per Second");
            return;
        }
/* GUI TEST TOUCH needs GetTouch / GET_X_AXIS / TOUCH_ERROR from Touch.h.
   Touch.h is included by Hardware_Includes.h only for PICOMITE,
   PICOMITEWEB, or (PICOMITEVGA && GUICONTROLS). VGAUSB defines USBKEYBOARD
   but not GUICONTROLS — so the gate has to match the Touch.h include
   condition exactly, not just USBKEYBOARD. */
#if defined(PICOMITE) || defined(PICOMITEWEB) || (defined(PICOMITEVGA) && defined(GUICONTROLS))
        if ((checkstring(p, (unsigned char *)"TOUCH")))
        {
            int x, y, x2, y2;
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
            bool tapdown = false;
            uint64_t taptime = 0;
#endif
            ClearScreen(gui_bcolour);
            while (getConsole() < '\r')
            {
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
                if (GuiTestDoubleTap(&tapdown, &taptime))
                    break;
#endif
                x = GetTouch(GET_X_AXIS);
                y = GetTouch(GET_Y_AXIS);
                if (x != TOUCH_ERROR && y != TOUCH_ERROR)
                    DrawBox(x - 1, y - 1, x + 1, y + 1, 0, WHITE, WHITE);
                /* Second contact, so two-finger panels can be exercised.
                   Sources mirror TOUCH(X2)/TOUCH(Y2): a TOUCH_CAP resistive
                   panel (non-VGA) or USB multi-touch contact 1. Drawn in a
                   distinct colour so the two fingers are distinguishable. */
                x2 = TOUCH_ERROR;
                y2 = TOUCH_ERROR;
#ifndef PICOMITEVGA
                if (Option.TOUCH_CAP)
                {
                    x2 = GetTouch(GET_X_AXIS2);
                    y2 = GetTouch(GET_Y_AXIS2);
                }
#endif
#ifdef USBKEYBOARD
                if (x2 == TOUCH_ERROR && usb_touch_active2)
                {
                    x2 = usb_touch_x2;
                    y2 = usb_touch_y2;
                }
#endif
                if (x2 != TOUCH_ERROR && y2 != TOUCH_ERROR)
                    DrawBox(x2 - 1, y2 - 1, x2 + 1, y2 + 1, 0, CYAN, CYAN);
            }
            ClearScreen(gui_bcolour);
            return;
        }
#endif
    }
    StandardError(36);
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */


/****************************************************************************************************

 General purpose drawing routines

****************************************************************************************************/
int rgb(int r, int g, int b)
{
    return RGB(r, g, b);
}
void getcoord(char *p, int *x, int *y)
{
    unsigned char *tp, *ttp;
    char b[STRINGSIZE];
    char savechar;
    tp = getclosebracket((unsigned char *)p);
    savechar = *tp;
    *tp = 0;        // remove the closing brackets
    strcpy(b, p);   // copy the coordinates to the temp buffer
    *tp = savechar; // put back the closing bracket
    ttp = (unsigned char *)b + 1;
    // kludge (todo: fix this)
    {
        getcsargs(&ttp, 3); // this is a macro and must be the first executable stmt in a block
        if (argc != 3)
            SyntaxError();
        *x = getinteger(argv[0]);
        *y = getinteger(argv[2]);
    }
}

int getColour(char *c, int minus)
{
    int colour;
    if (CMM1)
    {
        colour = getint((unsigned char *)c, (minus ? -1 : 0), 15);
        if (colour >= 0)
            colour = CMM1map[colour];
    }
    else
        colour = getint((unsigned char *)c, (minus ? -1 : 0), 0xFFFFFFF);
    return colour;
}
#ifndef PICOMITEVGA
void DrawPixelNormal(int x, int y, int c)
{
    DrawRectangle(x, y, x, y, c);
}
#endif
void ClearScreen(int c)
{
    if (!Option.DISPLAY_TYPE)
        return;
#if PICOMITERP2350
    if (ScrollLCD == ScrollLCDMEM332)
    {
        multicore_fifo_push_blocking(7);
        multicore_fifo_push_blocking((uint32_t)0);
        ScrollStart = 0;
    }
#endif
#ifdef PICOMITEVGA
    if (DISPLAY_TYPE == SCREENMODE1 && WriteBuf == DisplayBuf)
    {
        DrawRectangle(0, 0, HRes - 1, VRes - 1, 0);
#ifdef HDMI
        memset((void *)WriteBuf, 0, ScreenSize);
        if (FullColour)
        {
            uint16_t bcolour = RGB555(c);
            for (int x = 0; x < X_TILE; x++)
            {
                for (int y = 0; y < Y_TILE; y++)
                {
                    tilefcols[y * X_TILE + x] = RGB555(gui_fcolour);
                    tilebcols[y * X_TILE + x] = bcolour;
                }
            }
        }
        else
        {
            uint8_t bcolour = RGB332(c);
            for (int x = 0; x < X_TILE; x++)
            {
                for (int y = 0; y < Y_TILE; y++)
                {
                    tilefcols_w[y * X_TILE + x] = RGB332(gui_fcolour);
                    tilebcols_w[y * X_TILE + x] = bcolour;
                }
            }
        }
        CurrentX = CurrentY = 0;
#else
        memset((void *)WriteBuf, 0, ScreenSize);
        for (int x = 0; x < X_TILE; x++)
        {
            for (int y = 0; y < Y_TILE; y++)
            {
                tilefcols[y * X_TILE + x] = RGB121pack(gui_fcolour);
                tilebcols[y * X_TILE + x] = RGB121pack(c);
            }
        }
#endif
    }
    else
        DrawRectangle(0, 0, HRes - 1, VRes - 1, c);
#else
    DrawRectangle(0, 0, HRes - 1, VRes - 1, c);
#endif
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
    /* The reserved strip just got wiped. In system mode we own the strip
       and must restore the OSK so the prompt and editor remain usable.
       Program mode is the BASIC program's responsibility — drop our state
       so taps don't keep firing on now-invisible buttons. */
    if (OSK_IsProgramActive())
        OSK_DropState();
    else if (OSK_IsActive())
        OSK_DrawAll();
#endif
}
void DrawBuffered(int xti, int yti, int c, int complete)
{
    static unsigned char pos = 0;
    static unsigned char movex, movey, movec;
    static short xtilast[8];
    static short ytilast[8];
    static int clast[8];
    xtilast[pos] = xti;
    ytilast[pos] = yti;
    clast[pos] = c;
    if (complete == 1)
    {
        if (pos == 1)
        {
            DrawPixel(xtilast[0], ytilast[0], clast[0]);
        }
        else
        {
            DrawLine(xtilast[0], ytilast[0], xtilast[pos - 1], ytilast[pos - 1], 1, clast[0]);
        }
        pos = 0;
    }
    else
    {
        if (pos == 0)
        {
            movex = movey = movec = 1;
            pos += 1;
        }
        else
        {
            if (xti == xtilast[0] && abs(yti - ytilast[pos - 1]) == 1)
                movex = 0;
            else
                movex = 1;
            if (yti == ytilast[0] && abs(xti - xtilast[pos - 1]) == 1)
                movey = 0;
            else
                movey = 1;
            if (c == clast[0])
                movec = 0;
            else
                movec = 1;
            if (movec == 0 && (movex == 0 || movey == 0) && pos < 6)
                pos += 1;
            else
            {
                if (pos == 1)
                {
                    DrawPixel(xtilast[0], ytilast[0], clast[0]);
                }
                else
                {
                    DrawLine(xtilast[0], ytilast[0], xtilast[pos - 1], ytilast[pos - 1], 1, clast[0]);
                }
                movex = movey = movec = 1;
                xtilast[0] = xti;
                ytilast[0] = yti;
                clast[0] = c;
                pos = 1;
            }
        }
    }
}

/**************************************************************************************************
Draw a line on a the video output
  x1, y1 - the start coordinate
  x2, y2 - the end coordinate
    w - the width of the line (ignored for diagional lines)
  c - the colour to use
***************************************************************************************************/
#define abs(a) (((a) > 0) ? (a) : -(a))
int SizeLine(int x1, int y1, int x2, int y2)
{
    int n = 0;
    if (y1 == y2)
    {
        return abs(x1 - x2) + 1;
    }
    if (x1 == x2)
    {
        return abs(y1 - y2) + 1;
    }
    int dx, dy, sx, sy, err, e2;
    dx = abs(x2 - x1);
    sx = x1 < x2 ? 1 : -1;
    dy = -abs(y2 - y1);
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    while (1)
    {
        n++;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            if (x1 == x2)
                break;
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            if (y1 == y2)
                break;
            err += dx;
            y1 += sy;
        }
    }
    return n;
}
void ReadLine(int x1, int y1, int x2, int y2, char *buff)
{
    if (y1 == y2 || x1 == x2)
    {
        ReadBuffer(x1, y1, x2, y2, (unsigned char *)buff); // horiz line
        return;
    }
    int dx, dy, sx, sy, err, e2;
    dx = abs(x2 - x1);
    sx = x1 < x2 ? 1 : -1;
    dy = -abs(y2 - y1);
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    while (1)
    {
        ReadBuffer(x1, y1, x1, y1, (unsigned char *)buff);
        buff += 3;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            if (x1 == x2)
                break;
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            if (y1 == y2)
                break;
            err += dx;
            y1 += sy;
        }
    }
}
void RestoreLine(int x1, int y1, int x2, int y2, char *buff)
{
    if (y1 == y2 || x1 == x2)
    {
        DrawBuffer(x1, y1, x2, y2, (unsigned char *)buff); // horiz line
        return;
    }
    int dx, dy, sx, sy, err, e2;
    dx = abs(x2 - x1);
    sx = x1 < x2 ? 1 : -1;
    dy = -abs(y2 - y1);
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    while (1)
    {
        DrawBuffer(x1, y1, x1, y1, (unsigned char *)buff);
        buff += 3;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            if (x1 == x2)
                break;
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            if (y1 == y2)
                break;
            err += dx;
            y1 += sy;
        }
    }
}

void __no_inline_not_in_flash_func(DrawLine)(int x1, int y1, int x2, int y2, int w, int c)
{

    if (y1 == y2 && w > 0)
    {
        DrawRectangle(x1, y1, x2, y2 + w - 1, c); // horiz line
        if (Option.Refresh)
            Display_Refresh();
        return;
    }
    if (x1 == x2 && w > 0)
    {
        DrawRectangle(x1, y1, x2 + w - 1, y2, c); // vert line
        if (Option.Refresh)
            Display_Refresh();
        return;
    }
    if (w == 1 || w == -1)
    {
        int dx, dy, sx, sy, err, e2;
        dx = abs(x2 - x1);
        sx = x1 < x2 ? 1 : -1;
        dy = -abs(y2 - y1);
        sy = y1 < y2 ? 1 : -1;
        err = dx + dy;
        while (1)
        {
            DrawBuffered(x1, y1, c, 0);
            e2 = 2 * err;
            if (e2 >= dy)
            {
                if (x1 == x2)
                    break;
                err += dy;
                x1 += sx;
            }
            if (e2 <= dx)
            {
                if (y1 == y2)
                    break;
                err += dx;
                y1 += sy;
            }
        }
        DrawBuffered(0, 0, 0, 1);
    }
    else
    {
        float start, end;
        if (w < 0)
        {
            w = abs(w);
            start = -(w / 2.0f);
            end = w / 2.0f;
        }
        else
        {
            start = 0.0f;
            end = w;
        }
        // Calculate the line direction and length
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = sqrtf(dx * dx + dy * dy);

        // Normalize direction vector
        float nx = dx / length;
        float ny = dy / length;

        // Calculate the perpendicular vector for width
        float px = -ny;
        float py = nx;

        // Half-width adjustment

        // Loop through every pixel inside the bounding rectangle of the line
        for (int i = 0; i <= length; i++)
        {
            float lineX = x1 + i * nx;
            float lineY = y1 + i * ny;

            for (float j = start; j <= end; j += 0.25f)
            { // Finer granularity
                float pixelX = lineX + j * px;
                float pixelY = lineY + j * py;

                DrawPixel(roundf(pixelX), roundf(pixelY), c);
            }
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}

/**********************************************************************************************
Draw a box
     x1, y1 - the start coordinate
     x2, y2 - the end coordinate
     w      - the width of the sides of the box (can be zero)
     c      - the colour to use for sides of the box
     fill   - the colour to fill the box (-1 for no fill)
***********************************************************************************************/
void DrawBox(int x1, int y1, int x2, int y2, int w, int c, int fill)
{
    int t;

    // make sure the coordinates are in the right sequence
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (w > x2 - x1)
        w = x2 - x1;
    if (w > y2 - y1)
        w = y2 - y1;

    if (w > 0)
    {
        w--;
        DrawRectangle(x1, y1, x2, y1 + w, c); // Draw the top horiz line
        DrawRectangle(x1, y2 - w, x2, y2, c); // Draw the bottom horiz line
        DrawRectangle(x1, y1, x1 + w, y2, c); // Draw the left vert line
        DrawRectangle(x2 - w, y1, x2, y2, c); // Draw the right vert line
        w++;
    }

    if (fill >= 0)
        DrawRectangle(x1 + w, y1 + w, x2 - w, y2 - w, fill);
}

/**********************************************************************************************
Draw a box with rounded corners
     x1, y1 - the start coordinate
     x2, y2 - the end coordinate
     radius - the radius (in pixels) of the arc forming the corners
     c      - the colour to use for sides
     fill   - the colour to fill the box (-1 for no fill)
***********************************************************************************************/
void MIPS16 DrawRBox(int x1, int y1, int x2, int y2, int radius, int c, int fill)
{
    int f, ddF_x, ddF_y, xx, yy, maxr, t;

    // normalise the corners so x1,y1 is top-left and x2,y2 is bottom-right
    if (x2 < x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    // the radius cannot exceed half of the shorter side, otherwise the arcs
    // do not meet the straight sides and the outline/fill is left with gaps
    if (radius < 0)
        radius = 0;
    maxr = (x2 - x1) / 2;
    if ((y2 - y1) / 2 < maxr)
        maxr = (y2 - y1) / 2;
    if (radius > maxr)
        radius = maxr;
    if (radius < 1)
    { // degenerate to an ordinary rectangle (no rounding possible)
        if (fill >= 0)
            DrawRectangle(x1 + 1, y1 + 1, x2 - 1, y2 - 1, fill);
        DrawRectangle(x1, y1, x2, y1, c); // top side
        DrawRectangle(x1, y2, x2, y2, c); // bottom side
        DrawRectangle(x1, y1, x1, y2, c); // left side
        DrawRectangle(x2, y1, x2, y2, c); // right side
        if (Option.Refresh)
            Display_Refresh();
        return;
    }

    f = 1 - radius;
    ddF_x = 1;
    ddF_y = -2 * radius;
    xx = 0;
    yy = radius;

    while (xx < yy)
    {
        if (f >= 0)
        {
            yy -= 1;
            ddF_y += 2;
            f += ddF_y;
        }
        xx += 1;
        ddF_x += 2;
        f += ddF_x;
        if (fill >= 0)
        {
            DrawRectangle(x2 + xx - radius - 1, y2 + yy - radius, x1 - xx + radius + 1, y2 + yy - radius, fill);
            DrawRectangle(x2 + yy - radius - 1, y2 + xx - radius, x1 - yy + radius + 1, y2 + xx - radius, fill);
            DrawRectangle(x2 + xx - radius - 1, y1 - yy + radius, x1 - xx + radius + 1, y1 - yy + radius, fill);
            DrawRectangle(x2 + yy - radius - 1, y1 - xx + radius, x1 - yy + radius + 1, y1 - xx + radius, fill);
        }
        else
        {
            DrawPixel(x2 + xx - radius, y2 + yy - radius, c); // Bottom Right Corner
            DrawPixel(x2 + yy - radius, y2 + xx - radius, c); // ^^^
            DrawPixel(x1 - xx + radius, y2 + yy - radius, c); // Bottom Left Corner
            DrawPixel(x1 - yy + radius, y2 + xx - radius, c); // ^^^

            DrawPixel(x2 + xx - radius, y1 - yy + radius, c); // Top Right Corner
            DrawPixel(x2 + yy - radius, y1 - xx + radius, c); // ^^^
            DrawPixel(x1 - xx + radius, y1 - yy + radius, c); // Top Left Corner
            DrawPixel(x1 - yy + radius, y1 - xx + radius, c); // ^^^
        }
    }
    if (fill >= 0)
    {
        DrawRectangle(x1 + 1, y1 + radius, x2 - 1, y2 - radius, fill);
        DrawRBox(x1, y1, x2, y2, radius, c, -1);
    }
    else
    {
        DrawRectangle(x1 + radius - 1, y1, x2 - radius + 1, y1, c); // top side
        DrawRectangle(x1 + radius - 1, y2, x2 - radius + 1, y2, c); // botom side
        DrawRectangle(x1, y1 + radius, x1, y2 - radius, c);         // left side
        DrawRectangle(x2, y1 + radius, x2, y2 - radius, c);         // right side
    }
    //    if (fill != c && fill != -1)
    //        DrawRBox(x1, y1, x2, y2, radius, c, -1);
    if (Option.Refresh)
        Display_Refresh();
}

/***********************************************************************************************
Draw a circle on the video output
  x, y - the center of the circle
  radius - the radius of the circle
    w - width of the line drawing the circle
  c - the colour to use for the circle
  fill - the colour to use for the fill or -1 if no fill
  aspect - the ration of the x and y axis (a MMFLOAT).  1.0 gives a prefect circle
***********************************************************************************************/
/***********************************************************************************************
Draw a circle on the video output
    x, y - the center of the circle
    radius - the radius of the circle
    w - width of the line drawing the circle
    c - the colour to use for the circle
    fill - the colour to use for the fill or -1 if no fill
    aspect - the ration of the x and y axis (a MMFLOAT).  1.0 gives a prefect circle
***********************************************************************************************/
#ifdef rp2350
void __not_in_flash_func(DrawCircle)(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect)
{
#else
void DrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect)
{
#endif
    int a, b, P;
    int A, B;
    int asp;
    MMFLOAT aspect2;

    if (w > 1)
    {
        if (fill >= 0)
        {
            // Thick border with filled centre
            DrawCircle(x, y, radius, 0, c, c, aspect);
            aspect2 = ((aspect * (MMFLOAT)radius) - (MMFLOAT)w) / ((MMFLOAT)(radius - w));
            DrawCircle(x, y, radius - w, 0, fill, fill, aspect2);
        }
        else
        {
            // OPTIMIZED: Thick border with empty centre - LINE BY LINE
            int r1 = radius - w;
            int r2 = radius;

            aspect2 = ((aspect * (MMFLOAT)r2) - (MMFLOAT)w) / ((MMFLOAT)r1);

            // Use new line-by-line algorithm - DRAMATICALLY reduced memory!
            DrawCircleRingLineByLine(x, y, r1, r2, c, aspect, aspect2);
        }
    }
    else
    {
        // OPTIMIZED: Single thickness outline
        int w1 = w;
        int r1 = radius;

        if (fill >= 0)
        {
            asp = (int)(aspect * 1024.0f);

            while (w >= 0 && radius > 0)
            {
                a = 0;
                b = radius;
                P = 1 - radius;

                do
                {
                    A = (a * asp) >> 10;
                    B = (b * asp) >> 10;

                    DrawRectangle(x - A, y + b, x + A, y + b, fill);
                    DrawRectangle(x - A, y - b, x + A, y - b, fill);
                    DrawRectangle(x - B, y + a, x + B, y + a, fill);
                    DrawRectangle(x - B, y - a, x + B, y - a, fill);

                    if (P < 0)
                    {
                        P += 3 + (a << 1);
                        a++;
                    }
                    else
                    {
                        P += 5 + ((a - b) << 1);
                        a++;
                        b--;
                    }
                } while (a <= b);

                w--;
                radius--;
            }
        }

        if (c != fill)
        {
            w = w1;
            radius = r1;
            asp = (int)(aspect * 1024.0f);

            while (w >= 0 && radius > 0)
            {
                a = 0;
                b = radius;
                P = 1 - radius;

                do
                {
                    A = (a * asp) >> 10;
                    B = (b * asp) >> 10;

                    if (w)
                    {
                        // OPTIMIZED: Unrolled pixel drawing
                        DrawPixel(A + x, b + y, c);
                        DrawPixel(B + x, a + y, c);
                        DrawPixel(x - A, b + y, c);
                        DrawPixel(x - B, a + y, c);
                        DrawPixel(B + x, y - a, c);
                        DrawPixel(A + x, y - b, c);
                        DrawPixel(x - A, y - b, c);
                        DrawPixel(x - B, y - a, c);
                    }

                    if (P < 0)
                    {
                        P += 3 + (a << 1);
                        a++;
                    }
                    else
                    {
                        P += 5 + ((a - b) << 1);
                        a++;
                        b--;
                    }
                } while (a <= b);

                w--;
                radius--;
            }
        }
    }

    if (Option.Refresh)
        Display_Refresh();
}

#define ABS(X) ((X) < 0 ? -(X) : (X))
#define CLAMP(v, min, max) ((v) < (min) ? (min) : ((v) >= (max) ? (max) - 1 : (v)))
#define SWAP(a, b)      \
    do                  \
    {                   \
        int _tmp = (a); \
        (a) = (b);      \
        (b) = _tmp;     \
    } while (0)

// Internal optimized version with offset support
static void CalcLineInternal(int x1, int y1, int x2, int y2, short *xmin, short *xmax, int yoffset)
{
    // Handle horizontal line
    if (y1 == y2)
    {
        if (y1 < 0 || y1 >= VRes)
            return;

        int idx = y1 - yoffset;
        int minx = (x1 < x2) ? x1 : x2;
        int maxx = (x1 > x2) ? x1 : x2;

        if (minx < xmin[idx])
            xmin[idx] = minx;
        if (maxx > xmax[idx])
            xmax[idx] = maxx;
        return;
    }

    // Handle vertical line
    if (x1 == x2)
    {
        if (y2 < y1)
            SWAP(y2, y1);

        int ystart = CLAMP(y1, 0, VRes);
        int yend = CLAMP(y2, 0, VRes);

        for (int y = ystart; y <= yend; y++)
        {
            int idx = y - yoffset;
            if (x1 < xmin[idx])
                xmin[idx] = x1;
            if (x1 > xmax[idx])
                xmax[idx] = x1;
        }
        return;
    }

    // Bresenham's line algorithm
    if (y1 > y2)
    {
        SWAP(y1, y2);
        SWAP(x1, x2);
    }

    int absX = ABS(x1 - x2);
    int absY = ABS(y1 - y2);
    int offX = x2 < x1 ? 1 : -1;
    int offY = y2 < y1 ? 1 : -1;

    int x = x2;
    int y = y2;
    int err;

    // Update initial point if in bounds
    if (y >= 0 && y < VRes)
    {
        int idx = y - yoffset;
        if (x < xmin[idx])
            xmin[idx] = x;
        if (x > xmax[idx])
            xmax[idx] = x;
    }

    if (absX > absY)
    {
        // Line is more horizontal
        err = absX >> 1;
        while (x != x1)
        {
            err = err - absY;
            if (err < 0)
            {
                y += offY;
                err += absX;
            }
            x += offX;

            if (y >= 0 && y < VRes)
            {
                int idx = y - yoffset;
                if (x < xmin[idx])
                    xmin[idx] = x;
                if (x > xmax[idx])
                    xmax[idx] = x;
            }
        }
    }
    else
    {
        // Line is more vertical
        err = absY >> 1;
        while (y != y1)
        {
            err = err - absX;
            if (err < 0)
            {
                x += offX;
                err += absY;
            }
            y += offY;

            if (y >= 0 && y < VRes)
            {
                int idx = y - yoffset;
                if (x < xmin[idx])
                    xmin[idx] = x;
                if (x > xmax[idx])
                    xmax[idx] = x;
            }
        }
    }
}

// Original CalcLine - maintains backward compatibility
void CalcLine(int x1, int y1, int x2, int y2, short *xmin, short *xmax)
{
    CalcLineInternal(x1, y1, x2, y2, xmin, xmax, 0);
}

void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int f)
{
    // Check for degenerate triangle (collinear points)
    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0)
    {
        // Sort points by y coordinate
        if (y0 > y2)
        {
            SWAP(y0, y2);
            SWAP(x0, x2);
        }
        if (y0 > y1)
        {
            SWAP(y0, y1);
            SWAP(x0, x1);
        }
        if (y1 > y2)
        {
            SWAP(y1, y2);
            SWAP(x1, x2);
        }

        DrawLine(x0, y0, x2, y2, 1, c);
        return;
    }

    // Draw outline only
    if (f == -1)
    {
        DrawLine(x0, y0, x1, y1, 1, c);
        DrawLine(x1, y1, x2, y2, 1, c);
        DrawLine(x2, y2, x0, y0, 1, c);
        return;
    }

    // Sort vertices by y coordinate (sorting network)
    if (y0 > y2)
    {
        SWAP(y0, y2);
        SWAP(x0, x2);
    }
    if (y0 > y1)
    {
        SWAP(y0, y1);
        SWAP(x0, x1);
    }
    if (y1 > y2)
    {
        SWAP(y1, y2);
        SWAP(x1, x2);
    }

    // Calculate actual y range needed
    int ymin = (y0 < 0) ? 0 : y0;
    int ymax = (y2 >= VRes) ? VRes - 1 : y2;

    // Early exit if completely off-screen
    if (ymin >= VRes || ymax < 0)
        return;

    int range = ymax - ymin + 1;

    // Allocate only needed range
    short *xmin = (short *)GetMemory(range * sizeof(short));
    short *xmax = (short *)GetMemory(range * sizeof(short));

    // Initialize arrays
    for (int i = 0; i < range; i++)
    {
        xmin[i] = 32767;
        xmax[i] = -32768;
    }

    // Calculate scanline extents
    CalcLineInternal(x0, y0, x1, y1, xmin, xmax, ymin);
    CalcLineInternal(x1, y1, x2, y2, xmin, xmax, ymin);
    CalcLineInternal(x2, y2, x0, y0, xmin, xmax, ymin);

    // Fill scanlines
    for (int y = ymin; y <= ymax; y++)
    {
        int idx = y - ymin;
        if (xmax[idx] >= xmin[idx]) // Valid span
            DrawRectangle(xmin[idx], y, xmax[idx], y, f);
    }

    // Draw outline
    DrawLine(x0, y0, x1, y1, 1, c);
    DrawLine(x1, y1, x2, y2, 1, c);
    DrawLine(x2, y2, x0, y0, 1, c);

    FreeMemory((unsigned char *)xmin);
    FreeMemory((unsigned char *)xmax);
}
void RestoreTriangle(int bnbr, char *buff)
{
    short *p = (short *)buff;
    int x0 = p[0];
    int y0 = p[1];
    int x1 = p[2];
    int y1 = p[3];
    int x2 = p[4];
    int y2 = p[5];
    char *buffp = (char *)&p[6];
    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0)
    { // points are co-linear i.e zero area
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        RestoreLine(x0, y0, x2, y2, buffp);
    }
    else
    {
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        short *xmin = (short *)GetMemory(VRes * sizeof(short));
        short *xmax = (short *)GetMemory(VRes * sizeof(short));

        int y;
        for (y = y0; y <= y2; y++)
        {
            if (y >= 0 && y < VRes)
            {
                xmin[y] = 32767;
                xmax[y] = -1;
            }
        }
        CalcLine(x0, y0, x1, y1, xmin, xmax);
        CalcLine(x1, y1, x2, y2, xmin, xmax);
        CalcLine(x2, y2, x0, y0, xmin, xmax);
        for (y = y0; y <= y2; y++)
        {
            DrawBuffer(xmin[y], y, xmax[y], y, (unsigned char *)buffp);
            buffp += (xmax[y] - xmin[y] + 1) * 3;
        }
        FreeMemory((unsigned char *)xmin);
        FreeMemory((unsigned char *)xmax);
    }
}
void SaveTriangle(int bnbr, char *buff)
{
    short *p = (short *)buff;
    int x0 = p[0];
    int y0 = p[1];
    int x1 = p[2];
    int y1 = p[3];
    int x2 = p[4];
    int y2 = p[5];
    char *buffp = (char *)&p[6];
    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0)
    { // points are co-linear i.e zero area
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        ReadLine(x0, y0, x2, y2, buffp);
    }
    else
    {
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        short *xmin = (short *)GetMemory(VRes * sizeof(short));
        short *xmax = (short *)GetMemory(VRes * sizeof(short));

        int y;
        for (y = y0; y <= y2; y++)
        {
            if (y >= 0 && y < VRes)
            {
                xmin[y] = 32767;
                xmax[y] = -1;
            }
        }
        CalcLine(x0, y0, x1, y1, xmin, xmax);
        CalcLine(x1, y1, x2, y2, xmin, xmax);
        CalcLine(x2, y2, x0, y0, xmin, xmax);
        for (y = y0; y <= y2; y++)
        {
            ReadBuffer(xmin[y], y, xmax[y], y, (unsigned char *)buffp);
            buffp += (xmax[y] - xmin[y] + 1) * 3;
        }
        FreeMemory((unsigned char *)xmin);
        FreeMemory((unsigned char *)xmax);
    }
}
int SizeTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int n = 0;
    if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0)
    { // points are co-linear i.e zero area
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        return SizeLine(x0, y0, x2, y2);
    }
    else
    {
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        if (y1 > y2)
        {
            swap(y2, y1);
            swap(x2, x1);
        }
        if (y0 > y1)
        {
            swap(y0, y1);
            swap(x0, x1);
        }
        short *xmin = (short *)GetMemory(VRes * sizeof(short));
        short *xmax = (short *)GetMemory(VRes * sizeof(short));

        int y;
        for (y = y0; y <= y2; y++)
        {
            if (y >= 0 && y < VRes)
            {
                xmin[y] = 32767;
                xmax[y] = -1;
            }
        }
        CalcLine(x0, y0, x1, y1, xmin, xmax);
        CalcLine(x1, y1, x2, y2, xmin, xmax);
        CalcLine(x2, y2, x0, y0, xmin, xmax);
        for (y = y0; y <= y2; y++)
        {
            n += (xmax[y] - xmin[y] + 1);
        }
        FreeMemory((unsigned char *)xmin);
        FreeMemory((unsigned char *)xmax);
    }
    return n;
}
/*  @endcond */

void cmd_RestoreTriangle(unsigned char *p)
{
    getcsargs(&p, 1);
    if (*argv[0] == '#')
        argv[0]++;
    int bnbr = getint(argv[0], 1, MAXBLITBUF) - 1; // get the buffer number
    if (blitbuff[bnbr].blitbuffptr == NULL)
        error((char *)"Buffer not in use");
    if (blitbuff[bnbr].h != TRIANGLE_BUFFER_MARKER)
        error("Invalid buffer for restore");
    RestoreTriangle(bnbr, blitbuff[bnbr].blitbuffptr);
    FreeMemory((unsigned char *)blitbuff[bnbr].blitbuffptr);
    blitbuff[bnbr].blitbuffptr = NULL;
}

void cmd_ReadTriangle(unsigned char *p)
{
    int bnbr, x1, x2, x3, y1, y2, y3, size;
    getcsargs(&p, 13);
    if (argc != 13)
        SyntaxError();
    if (*argv[0] == '#')
        argv[0]++;
    bnbr = getint(argv[0], 1, MAXBLITBUF) - 1; // get the buffer number
    if (blitbuff[bnbr].blitbuffptr != NULL)
        error((char *)"Buffer in use");
    x1 = getinteger(argv[2]);
    y1 = getinteger(argv[4]);
    x2 = getinteger(argv[6]);
    y2 = getinteger(argv[8]);
    x3 = getinteger(argv[10]);
    y3 = getinteger(argv[12]);
    size = SizeTriangle(x1, y1, x2, y2, x3, y3);
    blitbuff[bnbr].blitbuffptr = GetMemory(size * 3 + 256);
    blitbuff[bnbr].h = TRIANGLE_BUFFER_MARKER;
    short *buff = (short *)blitbuff[bnbr].blitbuffptr;
    *buff++ = x1;
    *buff++ = y1;
    *buff++ = x2;
    *buff++ = y2;
    *buff++ = x3;
    *buff++ = y3;
    SaveTriangle(bnbr, blitbuff[bnbr].blitbuffptr);
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

/******************************************************************************************
 Print a char on the LCD display
 Any characters not in the font will print as a space.
 The char is printed at the current location defined by CurrentX and CurrentY
*****************************************************************************************/
void GUIPrintChar(int fnt, int fc, int bc, char c, int orientation)
{
    unsigned char *p, *fp, *np = NULL, *AllocatedMemory = NULL;
    int BitNumber, BitPos, x, y, newx, newy, modx, mody, scale = fnt & 0b1111;
    int height, width;
    if (PrintPixelMode == 1)
        bc = -1;
    if (PrintPixelMode == 2)
    {
        int s = bc;
        bc = fc;
        fc = s;
    }
    if (PrintPixelMode == 5)
    {
        fc = bc;
        bc = -1;
    }
    // to get the +, - and = chars for font 6 we fudge them by scaling up font 1
    if ((fnt & 0xf0) == 0x50 && (c == '-' || c == '+' || c == '='))
    {
        fp = (unsigned char *)FontTable[0];
        scale = scale * 4;
    }
    else
        fp = (unsigned char *)FontTable[fnt >> 4];

    height = fp[1];
    width = fp[0];
    modx = mody = 0;
    if (orientation > ORIENT_VERT)
    {
        AllocatedMemory = np = GetMemory(width * height);
        if (orientation == ORIENT_INVERTED)
        {
            modx -= width * scale - 1;
            mody -= height * scale - 1;
        }
        else if (orientation == ORIENT_CCW90DEG)
        {
            mody -= width * scale;
        }
        else if (orientation == ORIENT_CW90DEG)
        {
            modx -= height * scale - 1;
        }
    }

    if (c >= fp[2] && c < fp[2] + fp[3])
    {
        p = fp + 4 + (int)(((c - fp[2]) * height * width) / 8);

        if (orientation > ORIENT_VERT)
        { // non-standard orientation
            if (orientation == ORIENT_INVERTED)
            {
                for (y = 0; y < height; y++)
                {
                    newy = height - y - 1;
                    for (x = 0; x < width; x++)
                    {
                        newx = width - x - 1;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1)
                        {
                            BitNumber = ((newy * width) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            }
            else if (orientation == ORIENT_CCW90DEG)
            {
                for (y = 0; y < height; y++)
                {
                    newx = y;
                    for (x = 0; x < width; x++)
                    {
                        newy = width - x - 1;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1)
                        {
                            BitNumber = ((newy * height) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            }
            else if (orientation == ORIENT_CW90DEG)
            {
                for (y = 0; y < height; y++)
                {
                    newx = height - y - 1;
                    for (x = 0; x < width; x++)
                    {
                        newy = x;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1)
                        {
                            BitNumber = ((newy * height) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            }
        }
        else
            np = p;

        if (orientation < ORIENT_CCW90DEG)
            DrawBitmap(CurrentX + modx, CurrentY + mody, width, height, scale, fc, bc, np);
        else
            DrawBitmap(CurrentX + modx, CurrentY + mody, height, width, scale, fc, bc, np);
    }
    else
    {
        if (orientation < ORIENT_CCW90DEG)
            DrawRectangle(CurrentX + modx, CurrentY + mody, CurrentX + modx + (width * scale), CurrentY + mody + (height * scale), bc);
        else
            DrawRectangle(CurrentX + modx, CurrentY + mody, CurrentX + modx + (height * scale), CurrentY + mody + (width * scale), bc);
    }

    // to get the . and degree symbols for font 6 we draw a small circle
    if ((fnt & 0xf0) == 0x50)
    {
        if (orientation > ORIENT_VERT)
        {
            if (orientation == ORIENT_INVERTED)
            {
                if (c == '.')
                    DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + 7 * scale, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60)
                    DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + (height * scale) - 9 * scale, 6 * scale, 2 * scale, fc, -1, 1.0);
            }
            else if (orientation == ORIENT_CCW90DEG)
            {
                if (c == '.')
                    DrawCircle(CurrentX + modx + (height * scale) - 7 * scale, CurrentY + mody + (width * scale) / 2, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60)
                    DrawCircle(CurrentX + modx + 9 * scale, CurrentY + mody + (width * scale) / 2, 6 * scale, 2 * scale, fc, -1, 1.0);
            }
            else if (orientation == ORIENT_CW90DEG)
            {
                if (c == '.')
                    DrawCircle(CurrentX + modx + 7 * scale, CurrentY + mody + (width * scale) / 2, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60)
                    DrawCircle(CurrentX + modx + (height * scale) - 9 * scale, CurrentY + mody + (width * scale) / 2, 6 * scale, 2 * scale, fc, -1, 1.0);
            }
        }
        else
        {
            if (c == '.')
                DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + (height * scale) - 7 * scale, 4 * scale, 0, fc, fc, 1.0);
            if (c == 0x60)
                DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + 9 * scale, 6 * scale, 2 * scale, fc, -1, 1.0);
        }
    }

    if (orientation == ORIENT_NORMAL)
        CurrentX += width * scale;
    else if (orientation == ORIENT_VERT)
        CurrentY += height * scale;
    else if (orientation == ORIENT_INVERTED)
        CurrentX -= width * scale;
    else if (orientation == ORIENT_CCW90DEG)
        CurrentY -= width * scale;
    else if (orientation == ORIENT_CW90DEG)
        CurrentY += width * scale;
    if (orientation > ORIENT_VERT)
        FreeMemory(AllocatedMemory);
}

/******************************************************************************************
 Print a string on the LCD display
 The string must be a C string (not an MMBasic string)
 Any characters not in the font will print as a space.
*****************************************************************************************/
void GUIPrintString(int x, int y, int fnt, int jh, int jv, int jo, int fc, int bc, char *str)
{
    CurrentX = x;
    CurrentY = y;
    if (jo == ORIENT_NORMAL)
    {
        if (jh == JUSTIFY_CENTER)
            CurrentX -= (strlen(str) * GetFontWidth(fnt)) / 2;
        if (jh == JUSTIFY_RIGHT)
            CurrentX -= (strlen(str) * GetFontWidth(fnt));
        if (jv == JUSTIFY_MIDDLE)
            CurrentY -= GetFontHeight(fnt) / 2;
        if (jv == JUSTIFY_BOTTOM)
            CurrentY -= GetFontHeight(fnt);
    }
    else if (jo == ORIENT_VERT)
    {
        if (jh == JUSTIFY_CENTER)
            CurrentX -= GetFontWidth(fnt) / 2;
        if (jh == JUSTIFY_RIGHT)
            CurrentX -= GetFontWidth(fnt);
        if (jv == JUSTIFY_MIDDLE)
            CurrentY -= (strlen(str) * GetFontHeight(fnt)) / 2;
        if (jv == JUSTIFY_BOTTOM)
            CurrentY -= (strlen(str) * GetFontHeight(fnt));
    }
    else if (jo == ORIENT_INVERTED)
    {
        if (jh == JUSTIFY_CENTER)
            CurrentX += (strlen(str) * GetFontWidth(fnt)) / 2;
        if (jh == JUSTIFY_RIGHT)
            CurrentX += (strlen(str) * GetFontWidth(fnt));
        if (jv == JUSTIFY_MIDDLE)
            CurrentY += GetFontHeight(fnt) / 2;
        if (jv == JUSTIFY_BOTTOM)
            CurrentY += GetFontHeight(fnt);
    }
    else if (jo == ORIENT_CCW90DEG)
    {
        if (jh == JUSTIFY_CENTER)
            CurrentX -= GetFontHeight(fnt) / 2;
        if (jh == JUSTIFY_RIGHT)
            CurrentX -= GetFontHeight(fnt);
        if (jv == JUSTIFY_MIDDLE)
            CurrentY += (strlen(str) * GetFontWidth(fnt)) / 2;
        if (jv == JUSTIFY_BOTTOM)
            CurrentY += (strlen(str) * GetFontWidth(fnt));
    }
    else if (jo == ORIENT_CW90DEG)
    {
        if (jh == JUSTIFY_CENTER)
            CurrentX += GetFontHeight(fnt) / 2;
        if (jh == JUSTIFY_RIGHT)
            CurrentX += GetFontHeight(fnt);
        if (jv == JUSTIFY_MIDDLE)
            CurrentY -= (strlen(str) * GetFontWidth(fnt)) / 2;
        if (jv == JUSTIFY_BOTTOM)
            CurrentY -= (strlen(str) * GetFontWidth(fnt));
    }
    while (*str)
    {
#ifdef GUICONTROLS
        if (*str == 0xff && Ctrl[InvokingCtrl].type == 10)
        {
            //            fc = rgb(0, 0, 255);                                // this is specially for GUI FORMATBOX
            str++;
            GUIPrintChar(fnt, bc, fc, *str++, jo);
        }
        else
#endif
            GUIPrintChar(fnt, fc, bc, *str++, jo);
    }
}

/****************************************************************************************************

 MMBasic commands and functions

****************************************************************************************************/

// get and decode the justify$ string used in TEXT and GUI CAPTION
// the values are returned via pointers
int GetJustification(char *p, int *jh, int *jv, int *jo)
{
    switch (mytoupper(*p++))
    {
    case 'L':
        *jh = JUSTIFY_LEFT;
        break;
    case 'C':
        *jh = JUSTIFY_CENTER;
        break;
    case 'R':
        *jh = JUSTIFY_RIGHT;
        break;
    case 0:
        return true;
    default:
        p--;
    }
    skipspace(p);
    switch (mytoupper(*p++))
    {
    case 'T':
        *jv = JUSTIFY_TOP;
        break;
    case 'M':
        *jv = JUSTIFY_MIDDLE;
        break;
    case 'B':
        *jv = JUSTIFY_BOTTOM;
        break;
    case 0:
        return true;
    default:
        p--;
    }
    skipspace(p);
    switch (mytoupper(*p++))
    {
    case 'N':
        *jo = ORIENT_NORMAL;
        break; // normal
    case 'V':
        *jo = ORIENT_VERT;
        break; // vertical text (top to bottom)
    case 'I':
        *jo = ORIENT_INVERTED;
        break; // inverted
    case 'U':
        *jo = ORIENT_CCW90DEG;
        break; // rotated CCW 90 degrees
    case 'D':
        *jo = ORIENT_CW90DEG;
        break; // rotated CW 90 degrees
    case 0:
        return true;
    default:
        return false;
    }
    return *p == 0;
}

/*  @endcond */
void cmd_text(void)
{
    int x, y, font, scale, fc, bc;
    char *s;
    int jh = 0, jv = 0, jo = 0;

    getcsargs(&cmdline, 17); // this is a macro and must be the first executable stmt
    CheckDisplay();
    if (!(argc & 1) || argc < 5)
        StandardError(2);
    x = getinteger(argv[0]);
    y = getinteger(argv[2]);
    s = (char *)getCstring(argv[4]);

    if (argc > 5 && *argv[6])
        if (!GetJustification((char *)argv[6], &jh, &jv, &jo))
            if (!GetJustification((char *)getCstring(argv[6]), &jh, &jv, &jo))
                error("Justification");
    ;

    font = (gui_font >> 4) + 1;
    scale = (gui_font & 0b1111);
    fc = gui_fcolour;
    bc = gui_bcolour; // the defaults
    if (argc > 7 && *argv[8])
    {
        if (*argv[8] == '#')
            argv[8]++;
        font = getint(argv[8], 1, FONT_TABLE_SIZE);
    }
    if (FontTable[font - 1] == NULL)
        error("Invalid font #%", font);
    if (argc > 9 && *argv[10])
        scale = getint(argv[10], 1, 15);
    if (argc > 11 && *argv[12])
        fc = getint(argv[12], 0, WHITE);
    if (argc == 15)
        bc = getint(argv[14], -1, WHITE);
    GUIPrintString(x, y, ((font - 1) << 4) | scale, jh, jv, jo, fc, bc, s);
    if (Option.Refresh)
        Display_Refresh();
}

void cmd_pixel(void)
{
    CheckDisplay();
    if (CMM1)
    {
        int x, y, value;
        getcoord((char *)cmdline, &x, &y);
        cmdline = getclosebracket(cmdline) + 1;
        while (*cmdline && tokenfunction(*cmdline) != op_equal)
            cmdline++;
        if (!*cmdline)
            SyntaxError();
        ++cmdline;
        if (!*cmdline)
            SyntaxError();
        value = getColour((char *)cmdline, 0);
        DrawPixel(x, y, value);
        lastx = x;
        lasty = y;
    }
    else
    {
        int x1, y1, c = 0, n = 0, i, nc = 0;
        int x1stride = sizeof(MMFLOAT), y1stride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT);
        long long int *x1ptr, *y1ptr, *cptr;
        MMFLOAT *x1fptr, *y1fptr, *cfptr;
        getcsargs(&cmdline, 5);
        if (!(argc == 3 || argc == 5))
            StandardError(2);
        getargaddress(argv[0], &x1ptr, &x1fptr, &n, &x1stride);
        if (n != 1)
            getargaddress(argv[2], &y1ptr, &y1fptr, &n, &y1stride);
        if (n == 1)
        {                    // just a single point
            c = gui_fcolour; // setup the defaults
            x1 = getinteger(argv[0]);
            y1 = getinteger(argv[2]);
            if (argc == 5)
                c = getint(argv[4], -1, WHITE);
            else
                c = gui_fcolour;
            if (c != -1)
                DrawPixel(x1, y1, c);
            else
            {
                CurrentX = x1;
                CurrentY = y1;
            }
        }
        else
        {
            c = gui_fcolour; // setup the defaults
            if (argc == 5)
            {
                getargaddress(argv[4], &cptr, &cfptr, &nc, &cstride);
                if (nc == 1)
                    c = getint(argv[4], 0, WHITE);
                else if (nc > 1)
                {
                    if (nc < n)
                        n = nc; // adjust the dimensionality
                    for (i = 0; i < nc; i++)
                    {
                        c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                        if (c < 0 || c > WHITE)
                            StandardErrorParam3(26, (int)c, 0, WHITE);
                    }
                }
            }
            for (i = 0; i < n; i++)
            {
                x1 = (x1fptr == NULL ? STRIDE_INT(x1ptr, i, x1stride) : (int)STRIDE_FLOAT(x1fptr, i, x1stride));
                y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, i, y1stride) : (int)STRIDE_FLOAT(y1fptr, i, y1stride));
                if (nc > 1)
                    c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                DrawPixel(x1, y1, c);
            }
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}

void cmd_circle(void)
{
    CheckDisplay();
    if (CMM1)
    {
        int x, y, radius, colour, fill;
        float aspect;
        getcsargs(&cmdline, 9);
        if (argc % 2 == 0 || argc < 3)
            SyntaxError();
        if (*argv[0] != '(')
            error("Expected opening bracket");
        if (mytoupper(*argv[argc - 1]) == 'F')
        {
            argc -= 2;
            fill = true;
        }
        else
            fill = false;
        getcoord((char *)argv[0], &x, &y);
        radius = getinteger(argv[2]);
        if (radius == 0)
            return; // nothing to draw
        if (radius < 1)
            SyntaxError();
        if (argc > 3 && *argv[4])
            colour = getColour((char *)argv[4], 0);
        else
            colour = gui_fcolour;

        if (argc > 5 && *argv[6])
            aspect = getnumber(argv[6]);
        else
            aspect = 1;

        DrawCircle(x, y, radius, (fill ? 0 : 1), colour, (fill ? colour : -1), aspect);
        lastx = x;
        lasty = y;
    }
    else
    {
        int x, y, r, w = 0, c = 0, f = 0, n = 0, i, nc = 0, nw = 0, nf = 0, na = 0;
        int xstride = sizeof(MMFLOAT), ystride = sizeof(MMFLOAT), rstride = sizeof(MMFLOAT);
        int wstride = sizeof(MMFLOAT), astride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT), fstride = sizeof(MMFLOAT);
        MMFLOAT a;
        long long int *xptr, *yptr, *rptr, *fptr, *wptr, *cptr, *aptr;
        MMFLOAT *xfptr, *yfptr, *rfptr, *ffptr, *wfptr, *cfptr, *afptr;
        getcsargs(&cmdline, 13);
        if (!(argc & 1) || argc < 5)
            StandardError(2);
        getargaddress(argv[0], &xptr, &xfptr, &n, &xstride);
        if (n != 1)
        {
            getargaddress(argv[2], &yptr, &yfptr, &n, &ystride);
            getargaddress(argv[4], &rptr, &rfptr, &n, &rstride);
        }
        if (n == 1)
        {
            w = 1;
            c = gui_fcolour;
            f = -1;
            a = 1; // setup the defaults
            x = getinteger(argv[0]);
            y = getinteger(argv[2]);
            r = getinteger(argv[4]);
            if (argc > 5 && *argv[6])
                w = getint(argv[6], 0, 100);
            if (argc > 7 && *argv[8])
                a = getnumber(argv[8]);
            if (argc > 9 && *argv[10])
                c = getint(argv[10], 0, WHITE);
            if (argc > 11)
                f = getint(argv[12], -1, WHITE);
            int save_refresh = Option.Refresh;
            Option.Refresh = 0;
            DrawCircle(x, y, r, w, c, f, a);
            Option.Refresh = save_refresh;
        }
        else
        {
            w = 1;
            c = gui_fcolour;
            f = -1;
            a = 1; // setup the defaults
            if (argc > 5 && *argv[6])
            {
                getargaddress(argv[6], &wptr, &wfptr, &nw, &wstride);
                if (nw == 1)
                    w = getint(argv[6], 0, 100);
                else if (nw > 1)
                {
                    if (nw > 1 && nw < n)
                        n = nw; // adjust the dimensionality
                    for (i = 0; i < nw; i++)
                    {
                        w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                        if (w < 0 || w > 100)
                            StandardErrorParam3(26, (int)w, 0, 100);
                    }
                }
            }
            if (argc > 7 && *argv[8])
            {
                getargaddress(argv[8], &aptr, &afptr, &na, &astride);
                if (na == 1)
                    a = getnumber(argv[8]);
                if (na > 1 && na < n)
                    n = na; // adjust the dimensionality
            }
            if (argc > 9 && *argv[10])
            {
                getargaddress(argv[10], &cptr, &cfptr, &nc, &cstride);
                if (nc == 1)
                    c = getint(argv[10], 0, WHITE);
                else if (nc > 1)
                {
                    if (nc > 1 && nc < n)
                        n = nc; // adjust the dimensionality
                    for (i = 0; i < nc; i++)
                    {
                        c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                        if (c < 0 || c > WHITE)
                            StandardErrorParam3(26, (int)c, 0, WHITE);
                    }
                }
            }
            if (argc > 11)
            {
                getargaddress(argv[12], &fptr, &ffptr, &nf, &fstride);
                if (nf == 1)
                    f = getint(argv[12], -1, WHITE);
                else if (nf > 1)
                {
                    if (nf > 1 && nf < n)
                        n = nf; // adjust the dimensionality
                    for (i = 0; i < nf; i++)
                    {
                        f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
                        if (f < 0 || f > WHITE)
                            StandardErrorParam3(26, (int)f, 0, WHITE);
                    }
                }
            }
            int save_refresh = Option.Refresh;
            Option.Refresh = 0;
            for (i = 0; i < n; i++)
            {
                x = (xfptr == NULL ? STRIDE_INT(xptr, i, xstride) : (int)STRIDE_FLOAT(xfptr, i, xstride));
                y = (yfptr == NULL ? STRIDE_INT(yptr, i, ystride) : (int)STRIDE_FLOAT(yfptr, i, ystride));
                r = (rfptr == NULL ? STRIDE_INT(rptr, i, rstride) : (int)STRIDE_FLOAT(rfptr, i, rstride)) - 1;
                if (nw > 1)
                    w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                if (nc > 1)
                    c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                if (nf > 1)
                    f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
                if (na > 1)
                    a = (afptr == NULL ? (MMFLOAT)STRIDE_INT(aptr, i, astride) : STRIDE_FLOAT(afptr, i, astride));
                DrawCircle(x, y, r, w, c, f, a);
            }
            Option.Refresh = save_refresh;
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
static short xb0, xb1, yb0, yb1;
void GetPixel(int x, int y, int *r, int *g, int *b)
{
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    ReadBuffer(x, y, x, y, (unsigned char *)&c.rgb);
    *r = c.rgbbytes[2];
    *g = c.rgbbytes[1];
    *b = c.rgbbytes[0];
}

void MIPS16 drawAAPixel(int x, int y, MMFLOAT alpha, uint32_t c)
{
    int bgR, bgG, bgB;

    // Get the current background color of the pixel
    GetPixel(x, y, &bgR, &bgG, &bgB);
    union colourmap
    {
        unsigned char rgbbytes[4];
        unsigned int rgb;
    } col;
    col.rgb = c;
    col.rgbbytes[0] = (unsigned char)((MMFLOAT)col.rgbbytes[0] * alpha);
    col.rgbbytes[0] += (unsigned char)((MMFLOAT)bgB * (1.0 - alpha));
    col.rgbbytes[1] = (unsigned char)((MMFLOAT)col.rgbbytes[1] * alpha);
    col.rgbbytes[1] += (unsigned char)((MMFLOAT)bgG * (1.0 - alpha));
    col.rgbbytes[2] = (unsigned char)((MMFLOAT)col.rgbbytes[2] * alpha);
    col.rgbbytes[2] += (unsigned char)((MMFLOAT)bgR * (1.0 - alpha));
    if (((x >= xb0 && x <= xb1) && (y >= yb0 && y <= yb1)))
        DrawPixel(x, y, col.rgb);
}
void MIPS16 drawAALine(MMFLOAT x0, MMFLOAT y0, MMFLOAT x1, MMFLOAT y1, uint32_t c, int w)
{
    // Ensure positive integer values for width
    if (w < 1)
        w = 1;

    // If drawing a dot, the call drawDot function
    // if Math.abs(y1 - y0) < 1.0 && Math.abs(x1 - x0) < 1.0
    //  #drawDot (x0 + x1) / 2, (y0 + y1) / 2
    //  return
    xb0 = x0;
    xb1 = x1;
    yb0 = y0;
    yb1 = y1;
    if (xb1 < xb0)
        swap(xb1, xb0);
    if (yb1 < yb0)
        swap(yb1, yb0);

    // steep means that m > 1
    int steep = abs(y1 - y0) >= abs(x1 - x0);
    // swap the co-ordinates if slope > 1 or we
    // draw backwards
    if (steep)
    {
        swap(x0, y0);
        swap(x1, y1);
    }
    if (x0 > x1)
    {
        swap(x0, x1);
        swap(y0, y1);
    }
    // compute the slope
    MMFLOAT dx = x1 - x0;
    MMFLOAT dy = y1 - y0;

    MMFLOAT gradient;
    if (dx <= 0.0)
        gradient = 1;
    else
        gradient = dy / dx;

    // rotate w
    w = w * sqrt(1 + (gradient * gradient));

    // Handle first endpoint
    MMFLOAT xend = round(x0);
    MMFLOAT yend = y0 - (w - 1) * 0.5 + gradient * (xend - x0);
    MMFLOAT xgap = 1 - (x0 + 0.5 - xend);
    MMFLOAT xpxl1 = xend; // this will be used in the main loop
    MMFLOAT ypxl1 = floor(yend);
    MMFLOAT fpart = yend - floor(yend);
    MMFLOAT rfpart = 1.0 - fpart;

    if (steep)
    {
        drawAAPixel(ypxl1, xpxl1, rfpart * xgap, c);
        for (int i = 1; i <= w; i++)
            drawAAPixel(ypxl1 + i, xpxl1, 1, c);
        drawAAPixel(ypxl1 + w, xpxl1, fpart * xgap, c);
    }
    else
    {
        drawAAPixel(xpxl1, ypxl1, rfpart * xgap, c);
        for (int i = 1; i <= w; i++)
            drawAAPixel(xpxl1, ypxl1 + i, 1, c);
        drawAAPixel(xpxl1, ypxl1 + w, fpart * xgap, c);
    }
    MMFLOAT intery = yend + gradient; // first y-intersection for the main loop

    // Handle second endpoint
    xend = round(x1);
    yend = y1 - (w - 1) * 0.5 + gradient * (xend - x1);
    xgap = 1 - (x1 + 0.5 - xend);
    MMFLOAT xpxl2 = xend; // this will be used in the main loop
    MMFLOAT ypxl2 = floor(yend);
    fpart = yend - floor(yend);
    rfpart = 1 - fpart;

    if (steep)
    {
        drawAAPixel(ypxl2, xpxl2, rfpart * xgap, c);
        for (int i = 1; i <= w; i++)
            drawAAPixel(ypxl2 + i, xpxl2, 1, c);
        drawAAPixel(ypxl2 + w, xpxl2, fpart * xgap, c);
    }
    else
    {
        drawAAPixel(xpxl2, ypxl2, rfpart * xgap, c);
        for (int i = 1; i <= w; i++)
            drawAAPixel(xpxl2, ypxl2 + i, 1, c);
        drawAAPixel(xpxl2, ypxl2 + w, fpart * xgap, c);
    }
    // main loop
    if (steep)
    {
        for (int x = xpxl1 + 1; x <= xpxl2; x++)
        {
            fpart = intery - floor(intery);
            rfpart = 1 - fpart;
            MMFLOAT y = floor(intery);
            drawAAPixel(y, x, rfpart, c);
            for (int i = 1; i < w; i++)
                drawAAPixel(y + i, x, 1, c);
            drawAAPixel(y + w, x, fpart, c);
            intery = intery + gradient;
        }
    }
    else
    {
        for (int x = xpxl1 + 1; x <= xpxl2; x++)
        {
            fpart = intery - floor(intery);
            rfpart = 1 - fpart;
            MMFLOAT y = floor(intery);
            drawAAPixel(x, y, rfpart, c);
            for (int i = 1; i < w; i++)
                drawAAPixel(x, y + i, 1, c);
            drawAAPixel(x, y + w, fpart, c);
            intery = intery + gradient;
        }
    }
}
/*  @endcond */

void cmd_line(void)
{
    CheckDisplay();
    unsigned char *p;
    if (CMM1)
    {
        int x1, y1, x2, y2, colour, box, fill;
        getcsargs(&cmdline, 5);

        // check if it is actually a LINE INPUT command
        if (argc < 1)
            SyntaxError();
        x1 = lastx;
        y1 = lasty;
        colour = gui_fcolour;
        box = false;
        fill = false; // set the defaults for optional components
        p = argv[0];
        if (tokenfunction(*p) != op_subtract)
        {
            // the start point is specified - get the coordinates and step over to where the minus token should be
            if (*p != '(')
                error("Expected opening bracket");
            getcoord((char *)p, &x1, &y1);
            p = getclosebracket(p) + 1;
            skipspace(p);
        }
        if (tokenfunction(*p) != op_subtract)
            SyntaxError();
        p++;
        skipspace(p);
        if (*p != '(')
            error("Expected opening bracket");
        getcoord((char *)p, &x2, &y2);
        if (argc > 1 && *argv[2])
        {
            colour = getColour((char *)argv[2], 0);
        }
        if (argc == 5)
        {
            box = (strchr((char *)argv[4], 'b') != NULL || strchr((char *)argv[4], 'B') != NULL);
            fill = (strchr((char *)argv[4], 'f') != NULL || strchr((char *)argv[4], 'F') != NULL);
        }
        if (box)
            DrawBox(x1, y1, x2, y2, 1, colour, (fill ? colour : -1)); // draw a box
        else
            DrawLine(x1, y1, x2, y2, 1, colour); // or just a line

        lastx = x2;
        lasty = y2; // save in case the user wants the last value
    }
    else
    {
        int x1, y1, x2, y2, w = 0, c = 0, n = 0, i, nc = 0, nw = 0;
        if ((p = checkstring(cmdline, (unsigned char *)"PLOT")))
        {
            long long int *y1ptr;
            MMFLOAT *y1fptr;
            int xs = 0, xinc = 1;
            int ys = 0, yinc = 1;
            int y1stride = sizeof(MMFLOAT);
            getcsargs(&p, 13);
            getargaddress(argv[0], &y1ptr, &y1fptr, &n, &y1stride);
            if (n == 1)
                error("Argument 1 is not an array");
            nc = n;
            if (argc >= 3 && *argv[2])
                nc = getint(argv[2], 1, HRes - 1);
            if (nc > n)
                nc = n;
            if (argc >= 5 && *argv[4])
                xs = getint(argv[4], 0, HRes - 1);
            if (argc >= 7 && *argv[6])
                xinc = getint(argv[6], 1, HRes - 1);
            if (argc >= 9 && *argv[8])
                ys = getint(argv[8], g_OptionBase, n - 2 + g_OptionBase);
            if (argc >= 11 && *argv[10])
                yinc = getint(argv[10], 1, n - 1);
            c = gui_fcolour;
            w = 1; // setup the defaults
            if (argc == 13)
                c = getint(argv[12], 0, WHITE);
            int y = ys - g_OptionBase;
            for (i = 0; i < (nc - 1); i++)
            {
                if (y >= nc)
                    break;
                if (y + yinc >= nc)
                    break;
                x1 = xs + i * xinc;
                y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, y, y1stride) : (int)STRIDE_FLOAT(y1fptr, y, y1stride));
                if (y1 < 0)
                    y1 = 0;
                if (y1 >= VRes)
                    y1 = VRes - 1;
                x2 = xs + (i + 1) * xinc;
                y2 = (y1fptr == NULL ? STRIDE_INT(y1ptr, y + yinc, y1stride) : (int)STRIDE_FLOAT(y1fptr, y + yinc, y1stride));
                if (x1 >= HRes)
                    break; // can only get worse so stop now
                if (x2 >= HRes)
                    x2 = HRes - 1;
                if (y2 < 0)
                    y2 = 0;
                if (y2 >= VRes)
                    y2 = VRes - 1;
                DrawLine(x1, y1, x2, y2, w, c);
                y += yinc;
            }
        }
        else if ((p = checkstring(cmdline, (unsigned char *)"GRAPH")))
        {
            unsigned char *pp = GetTempStrMemory();
            strcpy((char *)pp, (char *)p);
            memmove(&pp[2], pp, strlen((char *)p) + 1);
            pp[0] = '0';
            pp[1] = ',';
            polygon(pp, 0);
            return;
        }
        else if ((p = checkstring(cmdline, (unsigned char *)"AA")))
        {
            MMFLOAT x1, y1, x2, y2;
            getcsargs(&p, 11);
            c = gui_fcolour;
            ;
            w = 1; // setup the defaults
            x1 = getnumber(argv[0]);
            y1 = getnumber(argv[2]);
            x2 = getnumber(argv[4]);
            y2 = getnumber(argv[6]);
            if (argc > 7 && *argv[8])
            {
                w = getint(argv[8], 1, 100);
            }
            if (argc == 11)
                c = getint(argv[10], 0, WHITE);
            if (x1 == x2 || y1 == y2)
                DrawLine(x1, y1, x2, y2, w, c);
            else
                drawAALine(x1, y1, x2, y2, c, w);
            return;
        }
        else
        {
            long long int *x1ptr, *y1ptr, *x2ptr, *y2ptr, *wptr, *cptr;
            MMFLOAT *x1fptr, *y1fptr, *x2fptr, *y2fptr, *wfptr, *cfptr;
            int x1stride = sizeof(MMFLOAT), y1stride = sizeof(MMFLOAT), x2stride = sizeof(MMFLOAT), y2stride = sizeof(MMFLOAT);
            int wstride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT);
            getcsargs(&cmdline, 11);
            if (!(argc & 1) || argc < 3)
                StandardError(2);
            getargaddress(argv[0], &x1ptr, &x1fptr, &n, &x1stride);
            if (n != 1)
            {
                if (argc < 7)
                    StandardError(2);
                getargaddress(argv[2], &y1ptr, &y1fptr, &n, &y1stride);
                getargaddress(argv[4], &x2ptr, &x2fptr, &n, &x2stride);
                getargaddress(argv[6], &y2ptr, &y2fptr, &n, &y2stride);
            }
            if (n == 1)
            {
                c = gui_fcolour;
                w = 1; // setup the defaults
                x1 = getinteger(argv[0]);
                y1 = getinteger(argv[2]);
                if (argc >= 5 && *argv[4])
                    x2 = getinteger(argv[4]);
                else
                {
                    x2 = CurrentX;
                    CurrentX = x1;
                }
                if (argc >= 7 && *argv[6])
                    y2 = getinteger(argv[6]);
                else
                {
                    y2 = CurrentY;
                    CurrentY = y1;
                }
                if (x1 == CurrentX && y1 == CurrentY)
                {
                    CurrentX = x2;
                    CurrentY = y2;
                }
                if (argc > 7 && *argv[8])
                {
                    w = getint(argv[8], -100, 100);
                    if (!w)
                        return;
                }
                if (argc == 11)
                    c = getint(argv[10], 0, WHITE);
                DrawLine(x1, y1, x2, y2, w, c);
            }
            else
            {
                c = gui_fcolour;
                w = 1; // setup the defaults
                if (argc > 7 && *argv[8])
                {
                    getargaddress(argv[8], &wptr, &wfptr, &nw, &wstride);
                    if (nw == 1)
                        w = getint(argv[8], -100, 100);
                    else if (nw > 1)
                    {
                        if (nw > 1 && nw < n)
                            n = nw; // adjust the dimensionality
                        for (i = 0; i < nw; i++)
                        {
                            w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                            if (w < -100 || w > 100)
                                StandardErrorParam3(26, (int)w, 0, 100);
                        }
                    }
                }
                if (argc == 11)
                {
                    getargaddress(argv[10], &cptr, &cfptr, &nc, &cstride);
                    if (nc == 1)
                        c = getint(argv[10], 0, WHITE);
                    else if (nc > 1)
                    {
                        if (nc > 1 && nc < n)
                            n = nc; // adjust the dimensionality
                        for (i = 0; i < nc; i++)
                        {
                            c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                            if (c < 0 || c > WHITE)
                                StandardErrorParam3(26, (int)c, 0, WHITE);
                        }
                    }
                }
                for (i = 0; i < n; i++)
                {
                    x1 = (x1fptr == NULL ? STRIDE_INT(x1ptr, i, x1stride) : (int)STRIDE_FLOAT(x1fptr, i, x1stride));
                    y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, i, y1stride) : (int)STRIDE_FLOAT(y1fptr, i, y1stride));
                    x2 = (x2fptr == NULL ? STRIDE_INT(x2ptr, i, x2stride) : (int)STRIDE_FLOAT(x2fptr, i, x2stride));
                    y2 = (y2fptr == NULL ? STRIDE_INT(y2ptr, i, y2stride) : (int)STRIDE_FLOAT(y2fptr, i, y2stride));
                    if (nw > 1)
                        w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                    if (nc > 1)
                        c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                    if (w)
                        DrawLine(x1, y1, x2, y2, w, c);
                }
            }
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}

void cmd_box(void)
{
    int x1, y1, w = 0, c = 0, f = 0, n = 0, i, nc = 0, nw = 0, nf = 0, hmod, wmod, nwidth = 0, nheight = 0, width = 0, height = 0;
    int x1stride = sizeof(MMFLOAT), y1stride = sizeof(MMFLOAT), wistride = sizeof(MMFLOAT), hstride = sizeof(MMFLOAT);
    int wstride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT), fstride = sizeof(MMFLOAT);
    long long int *x1ptr, *y1ptr, *wiptr, *hptr, *wptr, *cptr, *fptr;
    MMFLOAT *x1fptr, *y1fptr, *wifptr, *hfptr, *wfptr, *cfptr, *ffptr;
    getcsargs(&cmdline, 13);
    CheckDisplay();
    if (!(argc & 1) || argc < 7)
        StandardError(2);
    getargaddress(argv[0], &x1ptr, &x1fptr, &n, &x1stride);
    if (n != 1)
    {
        getargaddress(argv[2], &y1ptr, &y1fptr, &n, &y1stride);
    }
    if (n == 1)
    {
        c = gui_fcolour;
        w = 1;
        f = -1; // setup the defaults
        x1 = getinteger(argv[0]);
        y1 = getinteger(argv[2]);
        width = getinteger(argv[4]);
        height = getinteger(argv[6]);
        wmod = (width > 0 ? -1 : 1);
        hmod = (height > 0 ? -1 : 1);
        if (argc > 7 && *argv[8])
            w = getint(argv[8], 0, 100);
        if (argc > 9 && *argv[10])
            c = getint(argv[10], 0, WHITE);
        if (argc == 13)
            f = getint(argv[12], -1, WHITE);
        if (width != 0 && height != 0)
            DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
    }
    else
    {
        getargaddress(argv[4], &wiptr, &wifptr, &nwidth, &wistride);
        if (nwidth == 1)
            width = getint(argv[4], 1, HRes);
        else if (nwidth > 1)
        {
            if (nwidth > 1 && nwidth < n)
                n = nwidth; // adjust the dimensionality
            for (i = 0; i < nwidth; i++)
            {
                width = (wifptr == NULL ? STRIDE_INT(wiptr, i, wistride) : (int)STRIDE_FLOAT(wifptr, i, wistride));
                if (width < 1 || width > HRes)
                    error("Width % is invalid (valid is % to %)", (int)width, 1, HRes);
            }
        }
        getargaddress(argv[6], &hptr, &hfptr, &nheight, &hstride);
        if (nheight == 1)
            height = getint(argv[6], 1, VRes);
        else if (nheight > 1)
        {
            if (nheight > 1 && nheight < n)
                n = nheight; // adjust the dimensionality
            for (i = 0; i < nheight; i++)
            {
                height = (hfptr == NULL ? STRIDE_INT(hptr, i, hstride) : (int)STRIDE_FLOAT(hfptr, i, hstride));
                if (height < 1 || height > VRes)
                    error("Height % is invalid (valid is % to %)", (int)height, 1, VRes);
            }
        }
        c = gui_fcolour;
        w = 1; // setup the defaults
        if (argc > 7 && *argv[8])
        {
            getargaddress(argv[8], &wptr, &wfptr, &nw, &wstride);
            if (nw == 1)
                w = getint(argv[8], 0, 100);
            else if (nw > 1)
            {
                if (nw > 1 && nw < n)
                    n = nw; // adjust the dimensionality
                for (i = 0; i < nw; i++)
                {
                    w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                    if (w < 0 || w > 100)
                        StandardErrorParam3(26, (int)w, 0, 100);
                }
            }
        }
        if (argc > 9 && *argv[10])
        {
            getargaddress(argv[10], &cptr, &cfptr, &nc, &cstride);
            if (nc == 1)
                c = getint(argv[10], 0, WHITE);
            else if (nc > 1)
            {
                if (nc > 1 && nc < n)
                    n = nc; // adjust the dimensionality
                for (i = 0; i < nc; i++)
                {
                    c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                    if (c < 0 || c > WHITE)
                        StandardErrorParam3(26, (int)c, 0, WHITE);
                }
            }
        }
        if (argc == 13)
        {
            getargaddress(argv[12], &fptr, &ffptr, &nf, &fstride);
            if (nf == 1)
                f = getint(argv[12], 0, WHITE);
            else if (nf > 1)
            {
                if (nf > 1 && nf < n)
                    n = nf; // adjust the dimensionality
                for (i = 0; i < nf; i++)
                {
                    f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
                    if (f < -1 || f > WHITE)
                        StandardErrorParam3(26, (int)f, -1, WHITE);
                }
            }
        }
        for (i = 0; i < n; i++)
        {
            x1 = (x1fptr == NULL ? STRIDE_INT(x1ptr, i, x1stride) : (int)STRIDE_FLOAT(x1fptr, i, x1stride));
            y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, i, y1stride) : (int)STRIDE_FLOAT(y1fptr, i, y1stride));
            if (nwidth > 1)
                width = (wifptr == NULL ? STRIDE_INT(wiptr, i, wistride) : (int)STRIDE_FLOAT(wifptr, i, wistride));
            if (nheight > 1)
                height = (hfptr == NULL ? STRIDE_INT(hptr, i, hstride) : (int)STRIDE_FLOAT(hfptr, i, hstride));
            wmod = (width > 0 ? -1 : 1);
            hmod = (height > 0 ? -1 : 1);
            if (nw > 1)
                w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
            if (nc > 1)
                c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
            if (nf > 1)
                f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
            if (width != 0 && height != 0)
                DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

/*void MIPS16 bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int c)
{
    float tmp, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, t = 0.0, xt = x0, yt = y0;
    int i, xti, yti, xtlast = -1, ytlast = -1;
    for (i = 0; i < 500; i++)
    {
        tmp = 1.0 - t;
        tmp3 = t * t;
        tmp4 = tmp * tmp;
        tmp1 = tmp3 * t;
        tmp2 = tmp4 * tmp;
        tmp5 = 3.0 * t;
        tmp6 = 3.0 * tmp3;
        tmp7 = tmp5 * tmp4;
        tmp8 = tmp6 * tmp;
        xti = (int)xt;
        yti = (int)yt;
        xt = ((((tmp2 * x0) + (tmp7 * x1)) + (tmp8 * x2)) + (tmp1 * x3));
        yt = ((((tmp2 * y0) + (tmp7 * y1)) + (tmp8 * y2)) + (tmp1 * y3));
        if ((xti != xtlast) || (yti != ytlast))
        {
            DrawBuffered(xti, yti, c, 0);
            xtlast = xti;
            ytlast = yti;
        }
        t += 0.002;
    }
    DrawBuffered(0, 0, 0, 1);
}*/

// Fixed-point configuration: 10.22 format (10 bits integer, 22 bits fraction)
#define FP_SHIFT 24
#define FP_ONE (1 << FP_SHIFT)
#define FP_HALF (1 << (FP_SHIFT - 1))

// Convert integer to fixed-point
#define INT_TO_FP(x) ((int32_t)(x) << FP_SHIFT)

// Convert fixed-point to integer (with rounding)
#define FP_TO_INT(x) (((x) + FP_HALF) >> FP_SHIFT)

// Fixed-point multiplication
#define FP_MUL(a, b) (((int64_t)(a) * (int64_t)(b)) >> FP_SHIFT)

// Binomial coefficient calculation (unchanged)
static int binomial(int n, int k)
{
    if (k > n)
        return 0;
    if (k == 0 || k == n)
        return 1;
    int result = 1;
    for (int i = 0; i < k; i++)
    {
        result *= (n - i);
        result /= (i + 1);
    }
    return result;
}

// Fast integer square root for bounding box diagonal
static int isqrt(int n)
{
    if (n < 2)
        return n;

    int x = n;
    int y = (x + 1) / 2;

    while (y < x)
    {
        x = y;
        y = (x + n / x) / 2;
    }

    return x;
}

// Function to plot n-point Bezier curve using fixed-point arithmetic
void PlotBezier(int n, int c, int64_t *x, int64_t *y)
{
    int32_t t_fp, one_minus_t_fp;
    int64_t x_val, y_val;
    int prev_x, prev_y;

    // Calculate bounding box to estimate curve length
    int min_x = x[0], max_x = x[0];
    int min_y = y[0], max_y = y[0];
    for (int i = 1; i < n; i++)
    {
        if (x[i] < min_x)
            min_x = x[i];
        if (x[i] > max_x)
            max_x = x[i];
        if (y[i] < min_y)
            min_y = y[i];
        if (y[i] > max_y)
            max_y = y[i];
    }

    // Estimate steps based on bounding box diagonal
    int bbox_width = max_x - min_x;
    int bbox_height = max_y - min_y;
    int bbox_diag = isqrt(bbox_width * bbox_width + bbox_height * bbox_height);
    int num_steps = bbox_diag * 3;

    // Clamp to reasonable range
    if (num_steps < 10)
        num_steps = 10;
    if (num_steps > 2000)
        num_steps = 2000;

    // Pre-calculate binomial coefficients
    int binom_coeffs[16]; // Assuming max 16 control points
    for (int i = 0; i < n; i++)
    {
        binom_coeffs[i] = binomial(n - 1, i);
    }

    // Calculate and draw first point
    prev_x = x[0];
    prev_y = y[0];
    DrawPixel(prev_x, prev_y, c);

    int line_start_x = prev_x;
    int line_start_y = prev_y;
    int line_dx = 0;
    int line_dy = 0;
    int line_len = 0;

    // Pre-allocate arrays for power calculations (incremental approach)
    int32_t t_powers[16];   // t^i in fixed-point
    int32_t omt_powers[16]; // (1-t)^i in fixed-point

    // Plot the Bezier curve using line segments
    for (int step = 1; step <= num_steps; step++)
    {
        // Calculate t in fixed-point: t = step / num_steps
        t_fp = ((int64_t)step << FP_SHIFT) / num_steps;
        one_minus_t_fp = FP_ONE - t_fp;

        // Calculate powers incrementally using previous values
        // t^0 = 1, t^1 = t, t^2 = t * t^1, etc.
        t_powers[0] = FP_ONE;
        omt_powers[0] = FP_ONE;

        for (int i = 1; i < n; i++)
        {
            t_powers[i] = FP_MUL(t_powers[i - 1], t_fp);
            omt_powers[i] = FP_MUL(omt_powers[i - 1], one_minus_t_fp);
        }

        x_val = 0;
        y_val = 0;

        // Calculate point using Bezier formula with fixed-point
        for (int i = 0; i < n; i++)
        {
            // basis = binomial(n-1,i) * (1-t)^(n-1-i) * t^i
            // Note: binomial coefficient is integer, powers are fixed-point
            int32_t basis_fp = FP_MUL(omt_powers[n - 1 - i], t_powers[i]);

            // Multiply by binomial coefficient (integer)
            basis_fp = basis_fp * binom_coeffs[i];

            // Accumulate: basis * coordinate
            x_val += (int64_t)basis_fp * x[i];
            y_val += (int64_t)basis_fp * y[i];
        }

        // Convert back to integer coordinates
        // Need to shift by FP_SHIFT since basis_fp is already in fixed-point
        int curr_x = (int)((x_val + FP_HALF) >> FP_SHIFT);
        int curr_y = (int)((y_val + FP_HALF) >> FP_SHIFT);

        // Only process if coordinate changed
        // Only process if coordinate changed
        if (curr_x != prev_x || curr_y != prev_y)
        {
            int dx = curr_x - prev_x;
            int dy = curr_y - prev_y;
            // If jump > 1 pixel, bridge it immediately
            if (dx > 1 || dx < -1 || dy > 1 || dy < -1)
            {
                if (line_len > 1)
                {
                    DrawLine(line_start_x, line_start_y, prev_x, prev_y, 1, c);
                }
                else if (line_len == 1)
                {
                    DrawPixel(prev_x, prev_y, c);
                }
                DrawLine(prev_x, prev_y, curr_x, curr_y, 1, c);
                line_len = 0; // reset run
            }
            else
            {

                // Check if this continues the current straight line
                if (line_len > 0 && dx == line_dx && dy == line_dy)
                {
                    line_len++;
                }
                else
                {
                    // Direction changed - draw accumulated line if any
                    if (line_len > 1)
                    {
                        DrawLine(line_start_x, line_start_y, prev_x, prev_y, 1, c);
                    }
                    else if (line_len == 1)
                    {
                        DrawPixel(prev_x, prev_y, c);
                    }

                    // Start new line segment
                    line_start_x = prev_x;
                    line_start_y = prev_y;
                    line_dx = dx;
                    line_dy = dy;
                    line_len = 1;
                }
            }
            prev_x = curr_x;
            prev_y = curr_y;
        }
    }

    // Draw final accumulated line segment
    if (line_len > 1)
    {
        DrawLine(line_start_x, line_start_y, prev_x, prev_y, 1, c);
    }
    else if (line_len == 1)
    {
        DrawPixel(prev_x, prev_y, c);
    }
}

void cmd_bezier(void)
{
    int64_t *x = NULL;
    int64_t *y = NULL;
    int countx, county;
    getcsargs(&cmdline, 7);
    if (argc < 3)
        SyntaxError();
    countx = parseintegerarray(argv[0], &x, 1, 1, NULL, false, NULL);
    county = parseintegerarray(argv[2], &y, 2, 1, NULL, false, NULL);
    if (countx != county)
        StandardError(16);
    int n = countx;
    if (argc >= 5 && *argv[4])
        n = getint(argv[4], 2, countx);
    int colour = WHITE;
    if (argc == 7)
        colour = getColour((char *)argv[6], 0);
    PlotBezier(n, colour, x, y);
}
/*  @endcond */

// Fast arc drawing using direct scanline rendering
// Draws arc segments directly without intermediate bitmap buffer

static inline int normalize_angle(int angle)
{
    angle %= 360;
    return angle < 0 ? angle + 360 : angle;
}

static inline int point_in_arc_sector(int px, int py, int cx, int cy, int start_deg, int end_deg)
{
    // Calculate angle of point relative to center
    int dx = px - cx;
    int dy = py - cy;

    if (dx == 0 && dy == 0)
        return 1;

    // Original uses: 0° = up, 90° = right, 180° = down, 270° = left (clockwise from top)
    // Standard atan2 gives: 0° = right, 90° = up, 180° = left, 270° = down (counter-clockwise from right)
    // Convert: angle = 90 - atan2_angle, which is equivalent to atan2(dx, -dy)
    float angle = atan2f(dx, -dy) * 57.29577951f; // 180/PI
    if (angle < 0)
        angle += 360.0f;

    int angle_deg = (int)angle;

    // Handle wrap-around
    if (end_deg < start_deg)
        end_deg += 360;
    if (angle_deg < start_deg)
        angle_deg += 360;

    return (angle_deg >= start_deg && angle_deg <= end_deg);
}

void cmd_arc(void)
{
    int x, y, r1, r2, c;
    int rad1, rad2;

    getcsargs(&cmdline, 13);
    if (!(argc == 11 || argc == 13))
        StandardError(2);
    CheckDisplay();

    x = getinteger(argv[0]);
    y = getinteger(argv[2]);
    r1 = getinteger(argv[4]);

    if (*argv[6])
        r2 = getinteger(argv[6]);
    else
    {
        r2 = r1;
        r1--;
    }

    if (r2 < r1)
        error("Inner radius < outer");

    rad1 = getnumber(argv[8]);
    rad2 = getnumber(argv[10]);

    // Normalize angles to 0-359
    rad1 = normalize_angle(rad1);
    rad2 = normalize_angle(rad2);

    if (rad1 == rad2)
        error("Radials");

    if (argc == 13)
        c = getint(argv[12], 0, WHITE);
    else
        c = gui_fcolour;

    // Ensure rad2 > rad1 for sweep direction
    if (rad2 < rad1)
        rad2 += 360;

    int save_refresh = Option.Refresh;
    Option.Refresh = 0;
    // Draw arc using scanline method
    // Iterate through bounding box
    int min_y = y - r2;
    int max_y = y + r2;
    for (int scan_y = min_y; scan_y <= max_y; scan_y++)
    {
        int dy = scan_y - y;
        int dy2 = dy * dy;

        // Calculate x range for outer circle at this y
        int dx_outer = (int)sqrtf(r2 * r2 - dy2);
        int dx_inner = (r1 * r1 > dy2) ? (int)sqrtf(r1 * r1 - dy2) : 0;

        // Check left and right segments
        for (int side = 0; side < 2; side++)
        {
            int x_start, x_end;

            if (side == 0)
            {
                // Left side: from -dx_outer to -dx_inner
                x_start = x - dx_outer;
                x_end = x - dx_inner;
            }
            else
            {
                // Right side: from dx_inner to dx_outer
                x_start = x + dx_inner;
                x_end = x + dx_outer;
            }

            // Find the actual segment within the arc
            int segment_start = -1;
            int segment_end = -1;

            for (int scan_x = x_start; scan_x <= x_end; scan_x++)
            {
                if (point_in_arc_sector(scan_x, scan_y, x, y, rad1, rad2))
                {
                    if (segment_start == -1)
                        segment_start = scan_x;
                    segment_end = scan_x;
                }
                else if (segment_start != -1)
                {
                    // Draw completed segment
                    DrawRectangle(segment_start, scan_y, segment_end, scan_y, c);
                    segment_start = -1;
                }
            }

            // Draw any remaining segment
            if (segment_start != -1)
            {
                DrawRectangle(segment_start, scan_y, segment_end, scan_y, c);
            }
        }
    }

    Option.Refresh = save_refresh;
    if (Option.Refresh)
        Display_Refresh();
}

/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void MIPS16 cmd_rbox(void)
{
    int x1, y1, wi, h, w = 0, c = 0, f = 0, r = 0, n = 0, i, nc = 0, nw = 0, nf = 0, hmod, wmod;
    int x1stride = sizeof(MMFLOAT), y1stride = sizeof(MMFLOAT), wistride = sizeof(MMFLOAT), hstride = sizeof(MMFLOAT);
    int wstride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT), fstride = sizeof(MMFLOAT);
    long long int *x1ptr, *y1ptr, *wiptr, *hptr, *wptr, *cptr, *fptr;
    MMFLOAT *x1fptr, *y1fptr, *wifptr, *hfptr, *wfptr, *cfptr, *ffptr;
    getcsargs(&cmdline, 13);
    CheckDisplay();
    if (!(argc & 1) || argc < 7)
        StandardError(2);
    getargaddress(argv[0], &x1ptr, &x1fptr, &n, &x1stride);
    if (n != 1)
    {
        getargaddress(argv[2], &y1ptr, &y1fptr, &n, &y1stride);
        getargaddress(argv[4], &wiptr, &wifptr, &n, &wistride);
        getargaddress(argv[6], &hptr, &hfptr, &n, &hstride);
    }
    if (n == 1)
    {
        c = gui_fcolour;
        w = 1;
        f = -1;
        r = 10; // setup the defaults
        x1 = getinteger(argv[0]);
        y1 = getinteger(argv[2]);
        w = getinteger(argv[4]);
        h = getinteger(argv[6]);
        wmod = (w > 0 ? -1 : 1);
        hmod = (h > 0 ? -1 : 1);
        if (argc > 7 && *argv[8])
            r = getint(argv[8], 0, 100);
        if (argc > 9 && *argv[10])
            c = getint(argv[10], 0, WHITE);
        if (argc == 13)
            f = getint(argv[12], -1, WHITE);
        if (w != 0 && h != 0)
            DrawRBox(x1, y1, x1 + w + wmod, y1 + h + hmod, r, c, f);
    }
    else
    {
        c = gui_fcolour;
        w = 1; // setup the defaults
        if (argc > 7 && *argv[8])
        {
            getargaddress(argv[8], &wptr, &wfptr, &nw, &wstride);
            if (nw == 1)
                w = getint(argv[8], 0, 100);
            else if (nw > 1)
            {
                if (nw > 1 && nw < n)
                    n = nw; // adjust the dimensionality
                for (i = 0; i < nw; i++)
                {
                    w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
                    if (w < 0 || w > 100)
                        StandardErrorParam3(26, (int)w, 0, 100);
                }
            }
        }
        if (argc > 9 && *argv[10])
        {
            getargaddress(argv[10], &cptr, &cfptr, &nc, &cstride);
            if (nc == 1)
                c = getint(argv[10], 0, WHITE);
            else if (nc > 1)
            {
                if (nc > 1 && nc < n)
                    n = nc; // adjust the dimensionality
                for (i = 0; i < nc; i++)
                {
                    c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                    if (c < 0 || c > WHITE)
                        StandardErrorParam3(26, (int)c, 0, WHITE);
                }
            }
        }
        if (argc == 13)
        {
            getargaddress(argv[12], &fptr, &ffptr, &nf, &fstride);
            if (nf == 1)
                f = getint(argv[12], 0, WHITE);
            else if (nf > 1)
            {
                if (nf > 1 && nf < n)
                    n = nf; // adjust the dimensionality
                for (i = 0; i < nf; i++)
                {
                    f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
                    if (f < -1 || f > WHITE)
                        StandardErrorParam3(26, (int)f, -1, WHITE);
                }
            }
        }
        for (i = 0; i < n; i++)
        {
            x1 = (x1fptr == NULL ? STRIDE_INT(x1ptr, i, x1stride) : (int)STRIDE_FLOAT(x1fptr, i, x1stride));
            y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, i, y1stride) : (int)STRIDE_FLOAT(y1fptr, i, y1stride));
            wi = (wifptr == NULL ? STRIDE_INT(wiptr, i, wistride) : (int)STRIDE_FLOAT(wifptr, i, wistride));
            h = (hfptr == NULL ? STRIDE_INT(hptr, i, hstride) : (int)STRIDE_FLOAT(hfptr, i, hstride));
            wmod = (wi > 0 ? -1 : 1);
            hmod = (h > 0 ? -1 : 1);
            if (nw > 1)
                w = (wfptr == NULL ? STRIDE_INT(wptr, i, wstride) : (int)STRIDE_FLOAT(wfptr, i, wstride));
            if (nc > 1)
                c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
            if (nf > 1)
                f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
            if (wi != 0 && h != 0)
                DrawRBox(x1, y1, x1 + wi + wmod, y1 + h + hmod, w, c, f);
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}
// this function positions the cursor within a PRINT command
void MIPS16 fun_at(void)
{
    char buf[27];
    getcsargs(&ep, 5);
    if (commandfunction(cmdtoken) != cmd_print)
        error("Invalid function");
    //	if((argc == 3 || argc == 5)) StandardError(2);
    //	AutoLineWrap = false;
    CurrentX = getinteger(argv[0]);
    if (argc >= 3 && *argv[2])
        CurrentY = getinteger(argv[2]);
    if (argc == 5)
    {
        PrintPixelMode = getinteger(argv[4]);
        if (PrintPixelMode < 0 || PrintPixelMode > 7)
        {
            PrintPixelMode = 0;
            StandardError(21);
        }
    }
    else
        PrintPixelMode = 0;

    // BJR: VT100 set cursor location: <esc>[y;xf
    //      where x and y are ASCII string integers.
    //      Assumes overall font size of 6x12 pixels (480/80 x 432/36), including gaps between characters and lines

    sprintf(buf, "\033[%d;%df", (int)CurrentY / (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 1, (int)CurrentX / (FontTable[gui_font >> 4][0] * (gui_font & 0b1111)) + 1);
    SSPrintString(buf); // send it to the USB
    if (PrintPixelMode == 2 || PrintPixelMode == 5)
        SSPrintString("\033[7m");
    targ = T_STR;
    sret = (unsigned char *)"\0"; // normally pointing sret to a string in flash is illegal
}

// these three functions were written by Peter Mather (matherp on the Back Shed forum)
// read the contents of a PIXEL out of screen memory
void fun_pixel(void)
{
    if ((void *)ReadBuffer == (void *)DisplayNotSet)
        StandardError(11);
    int p;
    int x, y;
    getcsargs(&ep, 3);
    if (argc != 3)
        StandardError(2);
    x = getinteger(argv[0]);
    y = getinteger(argv[2]);
    ReadBuffer(x, y, x, y, (unsigned char *)&p);
    iret = p & 0xFFFFFF;
    targ = T_INT;
}

void cmd_triangle(void)
{ // thanks to Peter Mather (matherp on the Back Shed forum)
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"SAVE")))
    {
        if ((void *)ReadBuffer == (void *)DisplayNotSet)
            StandardError(11);
        cmd_ReadTriangle(p);
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"RESTORE")))
    {
        if ((void *)ReadBuffer == (void *)DisplayNotSet)
            StandardError(11);
        cmd_RestoreTriangle(p);
        return;
    }
    int x1, y1, x2, y2, x3, y3, c = 0, f = 0, n = 0, i, nc = 0, nf = 0;
    int x1stride = sizeof(MMFLOAT), y1stride = sizeof(MMFLOAT), x2stride = sizeof(MMFLOAT), y2stride = sizeof(MMFLOAT);
    int x3stride = sizeof(MMFLOAT), y3stride = sizeof(MMFLOAT), cstride = sizeof(MMFLOAT), fstride = sizeof(MMFLOAT);
    long long int *x3ptr, *y3ptr, *x1ptr, *y1ptr, *x2ptr, *y2ptr, *fptr, *cptr;
    MMFLOAT *x3fptr, *y3fptr, *x1fptr, *y1fptr, *x2fptr, *y2fptr, *ffptr, *cfptr;
    getcsargs(&cmdline, 15);
    CheckDisplay();
    if (!(argc & 1) || argc < 11)
        StandardError(2);
    getargaddress(argv[0], &x1ptr, &x1fptr, &n, &x1stride);
    if (n != 1)
    {
        int cn = n;
        getargaddress(argv[2], &y1ptr, &y1fptr, &n, &y1stride);
        if (n < cn)
            cn = n;
        getargaddress(argv[4], &x2ptr, &x2fptr, &n, &x2stride);
        if (n < cn)
            cn = n;
        getargaddress(argv[6], &y2ptr, &y2fptr, &n, &y2stride);
        if (n < cn)
            cn = n;
        getargaddress(argv[8], &x3ptr, &x3fptr, &n, &x3stride);
        if (n < cn)
            cn = n;
        getargaddress(argv[10], &y3ptr, &y3fptr, &n, &y3stride);
        if (n < cn)
            cn = n;
        n = cn;
    }
    if (n == 1)
    {
        c = gui_fcolour;
        f = -1;
        x1 = getinteger(argv[0]);
        y1 = getinteger(argv[2]);
        x2 = getinteger(argv[4]);
        y2 = getinteger(argv[6]);
        x3 = getinteger(argv[8]);
        y3 = getinteger(argv[10]);
        if (argc >= 13 && *argv[12])
            c = getint(argv[12], BLACK, WHITE);
        if (argc == 15)
            f = getint(argv[14], -1, WHITE);
        DrawTriangle(x1, y1, x2, y2, x3, y3, c, f);
    }
    else
    {
        c = gui_fcolour;
        f = -1;
        if (argc >= 13 && *argv[12])
        {
            getargaddress(argv[12], &cptr, &cfptr, &nc, &cstride);
            if (nc == 1)
                c = getint(argv[12], 0, WHITE);
            else if (nc > 1)
            {
                if (nc > 1 && nc < n)
                    n = nc; // adjust the dimensionality
                for (i = 0; i < nc; i++)
                {
                    c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
                    if (c < 0 || c > WHITE)
                        StandardErrorParam3(26, (int)c, 0, WHITE);
                }
            }
        }
        if (argc == 15)
        {
            getargaddress(argv[14], &fptr, &ffptr, &nf, &fstride);
            if (nf == 1)
                f = getint(argv[14], -1, WHITE);
            else if (nf > 1)
            {
                if (nf > 1 && nf < n)
                    n = nf; // adjust the dimensionality
                for (i = 0; i < nf; i++)
                {
                    f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
                    if (f < -1 || f > WHITE)
                        StandardErrorParam3(26, (int)f, -1, WHITE);
                }
            }
        }
        for (i = 0; i < n; i++)
        {
            x1 = (x1fptr == NULL ? STRIDE_INT(x1ptr, i, x1stride) : (int)STRIDE_FLOAT(x1fptr, i, x1stride));
            y1 = (y1fptr == NULL ? STRIDE_INT(y1ptr, i, y1stride) : (int)STRIDE_FLOAT(y1fptr, i, y1stride));
            x2 = (x2fptr == NULL ? STRIDE_INT(x2ptr, i, x2stride) : (int)STRIDE_FLOAT(x2fptr, i, x2stride));
            y2 = (y2fptr == NULL ? STRIDE_INT(y2ptr, i, y2stride) : (int)STRIDE_FLOAT(y2fptr, i, y2stride));
            x3 = (x3fptr == NULL ? STRIDE_INT(x3ptr, i, x3stride) : (int)STRIDE_FLOAT(x3fptr, i, x3stride));
            y3 = (y3fptr == NULL ? STRIDE_INT(y3ptr, i, y3stride) : (int)STRIDE_FLOAT(y3fptr, i, y3stride));
            if (x1 == x2 && x1 == x3 && y1 == y2 && y1 == y3 && x1 == -1 && y1 == -1)
                return;
            if (nc > 1)
                c = (cfptr == NULL ? STRIDE_INT(cptr, i, cstride) : (int)STRIDE_FLOAT(cfptr, i, cstride));
            if (nf > 1)
                f = (ffptr == NULL ? STRIDE_INT(fptr, i, fstride) : (int)STRIDE_FLOAT(ffptr, i, fstride));
            DrawTriangle(x1, y1, x2, y2, x3, y3, c, f);
        }
    }
    if (Option.Refresh)
        Display_Refresh();
}
void cmd_cls(void)
{
    CheckDisplay();

#ifdef GUICONTROLS
    HideAllControls();
    /* The mouse cursor's save-buffer is about to point at pixels that no
       longer exist on screen. Mark it gone so the next CursorRefresh()
       re-saves & re-draws cleanly instead of restoring stale pixels.
       No-op when cursor isn't active. */
    CursorHide();
#endif
    skipspace(cmdline);
    if (!(*cmdline == 0 || *cmdline == '\''))
    { // Colour specified
#ifdef PICOMITEVGA
        if (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3)
        {
            int fc = getint(cmdline, 0, WHITE);
            unsigned char fcolour = RGB121(fc);
            fcolour |= (fcolour << 4);
            memset((void *)WriteBuf, fcolour, ScreenSize);
        }
        else
        {
            ClearScreen(getint(cmdline, 0, WHITE));
        }
#else
        ClearScreen(getint(cmdline, 0, WHITE));
#endif
    }
    else
    { // Default colour
#ifdef PICOMITEVGA
        if ((WriteBuf == LayerBuf && (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3) && LayerBuf != DisplayBuf) || (WriteBuf == SecondLayer && (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3) && SecondLayer != DisplayBuf))
        {
            uint8_t colour = (WriteBuf == LayerBuf ? transparent | (transparent << 4) : transparents | (transparents << 4));
            memset((void *)WriteBuf, colour, HRes * VRes / 2);
#ifdef HDMI
        }
        else if (WriteBuf == LayerBuf && (DISPLAY_TYPE == SCREENMODE5) && LayerBuf != DisplayBuf)
        {
            memset((void *)WriteBuf, transparent, HRes * VRes);
        }
        else if ((void *)WriteBuf == LayerBuf && (DISPLAY_TYPE == SCREENMODE4) && LayerBuf != DisplayBuf)
        {
            uint16_t *p = (uint16_t *)WriteBuf;
            for (int i = 0; i < HRes * VRes; i++)
                *p++ = RGBtransparent;
#endif
        }
        else
#endif
            ClearScreen(gui_bcolour);
    }
    CurrentX = CurrentY = 0;
    if (Option.Refresh)
        Display_Refresh();
    /* OSK redraw/drop is handled centrally inside ClearScreen() now. */
}

void fun_rgb(void)
{
    getcsargs(&ep, 5);
    if (argc == 5)
        iret = rgb(getint(argv[0], 0, 255), getint(argv[2], 0, 255), getint(argv[4], 0, 255));
    else if (argc == 1)
    {
        if (checkstring(argv[0], (unsigned char *)"WHITE"))
            iret = WHITE;
        else if (checkstring(argv[0], (unsigned char *)"YELLOW"))
            iret = YELLOW;
        else if (checkstring(argv[0], (unsigned char *)"LILAC"))
            iret = LILAC;
        else if (checkstring(argv[0], (unsigned char *)"BROWN"))
            iret = BROWN;
        else if (checkstring(argv[0], (unsigned char *)"FUCHSIA"))
            iret = FUCHSIA;
        else if (checkstring(argv[0], (unsigned char *)"RUST"))
            iret = RUST;
        else if (checkstring(argv[0], (unsigned char *)"MAGENTA"))
            iret = MAGENTA;
        else if (checkstring(argv[0], (unsigned char *)"RED"))
            iret = RED;
        else if (checkstring(argv[0], (unsigned char *)"CYAN"))
            iret = CYAN;
        else if (checkstring(argv[0], (unsigned char *)"GREEN"))
            iret = GREEN;
        else if (checkstring(argv[0], (unsigned char *)"CERULEAN"))
            iret = CERULEAN;
        else if (checkstring(argv[0], (unsigned char *)"MIDGREEN"))
            iret = MIDGREEN;
        else if (checkstring(argv[0], (unsigned char *)"COBALT"))
            iret = COBALT;
        else if (checkstring(argv[0], (unsigned char *)"MYRTLE"))
            iret = MYRTLE;
        else if (checkstring(argv[0], (unsigned char *)"BLUE"))
            iret = BLUE;
        else if (checkstring(argv[0], (unsigned char *)"BLACK"))
            iret = BLACK;
        else if (checkstring(argv[0], (unsigned char *)"GRAY"))
            iret = GRAY;
        else if (checkstring(argv[0], (unsigned char *)"GREY"))
            iret = GRAY;
        else if (checkstring(argv[0], (unsigned char *)"LIGHTGRAY"))
            iret = LITEGRAY;
        else if (checkstring(argv[0], (unsigned char *)"LIGHTGREY"))
            iret = LITEGRAY;
        else if (checkstring(argv[0], (unsigned char *)"ORANGE"))
            iret = ORANGE;
        else if (checkstring(argv[0], (unsigned char *)"PINK"))
            iret = PINK;
        else if (checkstring(argv[0], (unsigned char *)"GOLD"))
            iret = GOLD;
        else if (checkstring(argv[0], (unsigned char *)"SALMON"))
            iret = SALMON;
        else if (checkstring(argv[0], (unsigned char *)"BEIGE"))
            iret = BEIGE;
        else
            error("Invalid colour: $", argv[0]);
    }
    else
        SyntaxError();
    ;
    targ = T_INT;
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void fun_mmhres(void)
{
    iret = HRes;
    targ = T_INT;
}

void fun_mmvres(void)
{
    iret = VRes;
    targ = T_INT;
}


void MIPS16 cmd_font(void)
{
    getcsargs(&cmdline, 3);
    if (argc < 1)
        StandardError(2);
    if (*argv[0] == '#')
        ++argv[0];
    if (argc == 3)
        SetFont(((getint(argv[0], 1, FONT_TABLE_SIZE) - 1) << 4) | getint(argv[2], 1, 15));
    else
        SetFont(((getint(argv[0], 1, FONT_TABLE_SIZE) - 1) << 4) | 1);
    if (Option.DISPLAY_CONSOLE && !CurrentLinePtr)
    { // if we are at the command prompt on the LCD
#ifdef PICOMITEVGA
        if (gui_font_height >= 8 && (gui_font_width % 8) == 0)
        {
            ytileheight = gui_font_height;
            Y_TILE = (VRes + ytileheight - 1) / ytileheight;
            for (int i = 0; i < X_TILE * Y_TILE; i++)
            {
#if defined(rp2350) && defined(HDMI)
                if (FullColour)
                {
                    tilefcols[i] = tilefcols[0];
                    tilebcols[i] = tilebcols[0];
                }
                else
                {
                    tilefcols_w[i] = tilefcols_w[0];
                    tilebcols_w[i] = tilebcols_w[0];
                }
#else
                tilefcols[i] = tilefcols[0];
                tilebcols[i] = tilebcols[0];
#endif
            }
        }
#endif
        PromptFont = gui_font;
        if (CurrentY + gui_font_height >= (VRes - (VRes * OptionVResreserved / 100)))
        {
            ScrollLCD(CurrentY + gui_font_height - (VRes - (VRes * OptionVResreserved / 100))); // scroll up if the font change split the line over the bottom
            CurrentY -= (CurrentY + gui_font_height - (VRes - (VRes * OptionVResreserved / 100)));
        }
    }
}
void cmd_colourmap(void)
{
    long long int *cptr = NULL, *fptr = NULL;
    MMFLOAT *cfptr = NULL, *ffptr = NULL;
    int nf, n, i;
    int map[16];
    getcsargs(&cmdline, 5);
    memcpy((void *)map, (void *)RGB121map, 16 * sizeof(int));
    if (!(argc == 3 || argc == 5))
        StandardError(2);
    n = parsenumberarray(argv[0], &cfptr, &cptr, 1, 1, NULL, true, NULL);
    if (argc == 5)
    { // user defined mapping
        MMFLOAT *a3float = NULL;
        int64_t *a3int = NULL;
        if (parsenumberarray(argv[4], &a3float, &a3int, 3, 1, NULL, true, NULL) != 16)
            error("Array size not 16 elements");
        if (a3int != NULL)
        {
            for (i = 0; i < 16; i++)
            {
                map[i] = a3int[i];
                if (map[i] < 0 || map[i] > 0xFFFFFF)
                    error("Invalid colour");
            }
        }
        else
        {
            for (i = 0; i < 16; i++)
            {
                map[i] = a3float[i];
                if (map[i] < 0 || map[i] > 0xFFFFFF)
                    error("Invalid colour");
            }
        }
    }
    nf = parsenumberarray(argv[2], &ffptr, &fptr, 1, 1, NULL, false, NULL);
    if (nf != n)
        error("Array size mismatch %, %", n, nf);
    for (int i = 0; i < n; i++)
    {
        int in = (cptr == NULL ? (int)cfptr[i] : cptr[i]);
        if (in >= 16)
            error("Input range error on element %", i);
        if (fptr == NULL)
            ffptr[i] = map[in];
        else
            fptr[i] = map[in];
    }
}

void cmd_colour(void)
{
    getcsargs(&cmdline, 3);
    if (argc < 1)
        StandardError(2);
    gui_fcolour = getColour((char *)argv[0], 0);
    if (argc == 3)
        gui_bcolour = getColour((char *)argv[2], 0);
    last_fcolour = gui_fcolour;
    last_bcolour = gui_bcolour;
    if (!CurrentLinePtr)
    {
        PromptFC = gui_fcolour;
        PromptBC = gui_bcolour;
    }
}
#ifdef PICOMITEVGA
void fun_map(void)
{
    int cl = getint(ep, 0, 255);
    switch (DISPLAY_TYPE)
    {
    case SCREENMODE1:
    case SCREENMODE4:
        error("Invalid for Mode");
        break;
    case SCREENMODE2:
    case SCREENMODE3:
        if (cl > 15)
            error("Mode has 16 colours - 0 to 15");
        targ = T_INT;
        iret = ((cl & 0b1000) << 20) | ((cl & 0b110) << 13) | ((cl & 0b1) << 7);
        break;
    case SCREENMODE5:
        targ = T_INT;
        iret = ((cl & 0b11100000) << 16) | ((cl & 0b00011100) << 11) | ((cl & 0b11) << 6);
        break;
    }
}
#ifndef HDMI
void cmd_map(void)
{
    unsigned char *p;
    //    if(Option.CPU_Speed==126000)error("CPUSPEED >= 252000 for colour mapping");
    if (!(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3))
        StandardError(40);
    if ((p = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        while (QVgaScanLine != 0)
        {
        }
        for (int i = 0; i < 16; i++)
            remap[i] = RGB121map[i];
        for (int i = 0; i < 16; i++)
            map16[i] = RGB121(remap[i]);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MAXIMITE")))
    {
        while (QVgaScanLine != 0)
        {
        }
        for (int i = 0; i < 16; i++)
            remap[i] = CMM1map[i];
        for (int i = 0; i < 16; i++)
            map16[i] = RGB121(remap[i]);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SET")))
    {
        while (QVgaScanLine != 0)
        {
        }
        for (int i = 0; i < 16; i++)
            map16[i] = RGB121(remap[i]);
    }
    else
    {
        static bool first = true;
        int cl = getinteger(cmdline);
        while (*cmdline && tokenfunction(*cmdline) != op_equal)
            cmdline++;
        if (!*cmdline)
            SyntaxError();
        ++cmdline;
        if (!*cmdline)
            SyntaxError();
        int col = getColour((char *)cmdline, 0);
        if (first)
        {
            for (int i = 0; i < 16; i++)
                remap[i] = RGB121map[i];
            first = false;
        }
        remap[cl] = col;
    }
}

void cmd_tile(void)
{
    unsigned char *tp;
    uint32_t bcolour = 0xFFFFFFFF, fcolour = 0xFFFFFFFF;
    int xlen = 1, ylen = 1;
    if (DISPLAY_TYPE != SCREENMODE1)
        StandardError(40);
    if (checkstring(cmdline, (unsigned char *)"RESET"))
    {
        for (int x = 0; x < X_TILE; x++)
        {
            for (int y = 0; y < Y_TILE; y++)
            {
                tilefcols[y * X_TILE + x] = RGB121pack(gui_fcolour);
                tilebcols[y * X_TILE + x] = RGB121pack(gui_bcolour);
            }
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"HEIGHT")))
    {
        if (!(WriteBuf == DisplayBuf))
            error("Not available when write is set to a buffer");
        ytileheight = getint(tp, 12, VRes);
        Y_TILE = VRes / ytileheight;
        if (VRes % ytileheight)
            Y_TILE++;
        ClearScreen(Option.DefaultBC);
    }
    else
    {
        getcsargs(&cmdline, 11);
        if (!(DISPLAY_TYPE == SCREENMODE1))
            return;
        if (argc < 5)
            SyntaxError();
        ;
        int x = getint(argv[0], 0, X_TILE);
        int y = getint(argv[2], 0, Y_TILE);
        int tilebcolour, tilefcolour;
        if (*argv[4])
        {
            tilefcolour = getColour((char *)argv[4], 0);
            fcolour = RGB121pack(tilefcolour);
        }
        if (argc >= 7 && *argv[6])
        {
            tilebcolour = getColour((char *)argv[6], 0);
            bcolour = RGB121pack(tilebcolour);
        }
        if (argc >= 9 && *argv[8])
        {
            xlen = getint(argv[8], 1, X_TILE - x);
        }
        if (argc >= 11 && *argv[10])
        {
            ylen = getint(argv[10], 1, Y_TILE - y);
        }
        for (int xp = x; xp < x + xlen; xp++)
        {
            for (int yp = y; yp < y + ylen; yp++)
            {
                if (fcolour != 0xFFFFFFFF)
                    tilefcols[yp * X_TILE + xp] = (uint16_t)fcolour;
                if (bcolour != 0xFFFFFFFF)
                    tilebcols[yp * X_TILE + xp] = (uint16_t)bcolour;
            }
        }
    }
}
#else
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
void DrawRectangle555(int x1, int y1, int x2, int y2, int c)
{
    int x, y, t;
    uint16_t col = ((c & 0xf8) >> 3) | ((c & 0xf800) >> 6) | ((c & 0xf80000) >> 9);
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++)
    {
        uint16_t *p = (uint16_t *)((uint8_t *)(WriteBuf + ((y * HRes + x1) * 2)));
        for (x = x1; x <= x2; x++)
        {
            *p++ = col;
        }
    }
}
void DrawBitmap555(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    int i, j, k, m, x, y;
    //    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0)
        return;
    uint16_t fcolour = RGB555(fc);
    uint16_t bcolour = RGB555(bc);
    for (i = 0; i < height; i++)
    { // step thru the font scan line by line
        for (j = 0; j < scale; j++)
        { // repeat lines to scale the font
            for (k = 0; k < width; k++)
            { // step through each bit in a scan line
                for (m = 0; m < scale; m++)
                { // repeat pixels to scale in the x axis
                    x = x1 + k * scale + m;
                    y = y1 + i * scale + j;
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                    { // if the coordinates are valid
                        uint16_t *p = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
                        if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                        {
                            *p = fcolour;
                        }
                        else
                        {
                            if (bc >= 0)
                            {
                                *p = bcolour;
                            }
                        }
                    }
                }
            }
        }
    }
}

void DrawBuffer555(int x1, int y1, int x2, int y2, unsigned char *p)
{
    int x, y, t;
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    uint16_t fcolour;
    uint16_t *pp;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            c.rgbbytes[0] = *p++; // this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            fcolour = RGB555(c.rgb);
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            *pp = fcolour;
        }
    }
}
void DrawBuffer555Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p)
{
    int x, y, t;
    uint16_t c;
    uint16_t *pp, *qq = (uint16_t *)p;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                pp = (uint16_t *)(WriteBuf + (y * (HRes << 1)) + (x << 1));
                c = *qq++;
                if (c != sprite_transparent || blank == -1)
                    *pp = c;
            }
        }
    }
}
void DrawPixel555(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= HRes || y >= VRes)
        return;
    uint16_t colour = RGB555(c);
    uint16_t *p = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
    *p = colour;
}
void ReadBuffer555(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    uint16_t *pp;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0)
        xx1 = 0;
    if (x1 >= HRes)
        xx1 = HRes - 1;
    if (x2 < 0)
        xx2 = 0;
    if (x2 >= HRes)
        xx2 = HRes - 1;
    if (y1 < 0)
        yy1 = 0;
    if (y1 >= VRes)
        yy1 = VRes - 1;
    if (y2 < 0)
        yy2 = 0;
    if (y2 >= VRes)
        yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++)
    {
        for (x = xx1; x <= xx2; x++)
        {
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            t = *pp;
            *c++ = ((t & 0x1F) << 3);
            *c++ = (((t >> 5) & 0x1F) << 3);
            *c++ = (((t >> 10) & 0x1F) << 3);
        }
    }
}
void ReadBuffer555Fast(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    uint16_t *pp, *qq = (uint16_t *)c;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0)
        xx1 = 0;
    if (x1 >= HRes)
        xx1 = HRes - 1;
    if (x2 < 0)
        xx2 = 0;
    if (x2 >= HRes)
        xx2 = HRes - 1;
    if (y1 < 0)
        yy1 = 0;
    if (y1 >= VRes)
        yy1 = VRes - 1;
    if (y2 < 0)
        yy2 = 0;
    if (y2 >= VRes)
        yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++)
    {
        for (x = xx1; x <= xx2; x++)
        {
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            *qq++ = *pp;
        }
    }
}
void ScrollLCD555(int lines)
{
    if (lines == 0)
        return;
    if (lines >= 0)
    {
        for (int i = 0; i < (VRes - (VRes * OptionVResreserved / 100)) - lines; i++)
        {
            int d = i * (HRes << 1), s = (i + lines) * (HRes << 1);
            for (int c = 0; c < (HRes << 1); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, (VRes - (VRes * OptionVResreserved / 100)) - lines, HRes - 1, (VRes - (VRes * OptionVResreserved / 100)) - 1, PromptBC); // erase the lines to be scrolled off
    }
    else
    {
        lines = -lines;
        for (int i = (VRes - (VRes * OptionVResreserved / 100)) - 1; i >= lines; i--)
        {
            int d = i * (HRes << 1), s = (i - lines) * (HRes << 1);
            for (int c = 0; c < (HRes << 1); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
void DrawRectangle256(int x1, int y1, int x2, int y2, int c)
{
    int y, t;
    uint8_t colour = RGB332(c);
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++)
    {
        volatile uint8_t *p = WriteBuf + (y * HRes + x1);
        memset((void *)p, colour, x2 - x1 + 1);
    }
}
void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    int i, j, k, m, x, y;
    //    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0)
        return;
    uint8_t fcolour = RGB332(fc);
    uint8_t bcolour = RGB332(bc);
    for (i = 0; i < height; i++)
    { // step thru the font scan line by line
        for (j = 0; j < scale; j++)
        { // repeat lines to scale the font
            for (k = 0; k < width; k++)
            { // step through each bit in a scan line
                for (m = 0; m < scale; m++)
                { // repeat pixels to scale in the x axis
                    x = x1 + k * scale + m;
                    y = y1 + i * scale + j;
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                    { // if the coordinates are valid
                        uint8_t *p = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
                        if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                        {
                            *p = fcolour;
                        }
                        else
                        {
                            if (bc >= 0)
                            {
                                *p = bcolour;
                            }
                        }
                    }
                }
            }
        }
    }
}

void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char *p)
{
    int x, y, t;
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    uint8_t fcolour;
    uint8_t *pp;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            c.rgbbytes[0] = *p++; // this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            fcolour = RGB332(c.rgb);
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
            *pp = fcolour;
        }
    }
}
void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p)
{
    int x, y, t;
    uint8_t c;
    uint8_t *pp, *qq = (uint8_t *)p;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
                c = *qq++;
                if (c != sprite_transparent || blank == -1)
                    *pp = c;
            }
        }
    }
}
void DrawPixel256(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= HRes || y >= VRes)
        return;
    uint8_t colour = RGB332(c);
    uint8_t *p = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
    *p = colour;
}
void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    uint8_t *pp;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0)
        xx1 = 0;
    if (x1 >= HRes)
        xx1 = HRes - 1;
    if (x2 < 0)
        xx2 = 0;
    if (x2 >= HRes)
        xx2 = HRes - 1;
    if (y1 < 0)
        yy1 = 0;
    if (y1 >= VRes)
        yy1 = VRes - 1;
    if (y2 < 0)
        yy2 = 0;
    if (y2 >= VRes)
        yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++)
    {
        for (x = xx1; x <= xx2; x++)
        {
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
#ifdef PICOMITEVGA
            unsigned int q;
            uint8_t *qq = pp;
            if (WriteBuf == DisplayBuf && LayerBuf != DisplayBuf && LayerBuf != NULL)
                qq = (uint8_t *)((uint32_t)(LayerBuf + y * HRes + x));
#endif
            t = *pp;
#ifdef PICOMITEVGA
            q = *qq;
            if (!(*qq == transparent) && mergedread)
                t = q;
#endif
            *c++ = ((t & 0x3) << 6);
            *c++ = (((t >> 2) & 0x7) << 5);
            *c++ = (((t >> 5) & 0x7) << 5);
        }
    }
}
void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    uint8_t *pp, *qq = (uint8_t *)c;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0)
        xx1 = 0;
    if (x1 >= HRes)
        xx1 = HRes - 1;
    if (x2 < 0)
        xx2 = 0;
    if (x2 >= HRes)
        xx2 = HRes - 1;
    if (y1 < 0)
        yy1 = 0;
    if (y1 >= VRes)
        yy1 = VRes - 1;
    if (y2 < 0)
        yy2 = 0;
    if (y2 >= VRes)
        yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++)
    {
        for (x = xx1; x <= xx2; x++)
        {
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
            *qq++ = *pp;
        }
    }
}
void ScrollLCD256(int lines)
{
    if (lines == 0)
        return;
    if (lines >= 0)
    {
        for (int i = 0; i < (VRes - (VRes * OptionVResreserved / 100)) - lines; i++)
        {
            int d = i * HRes, s = (i + lines) * HRes;
            for (int c = 0; c < (HRes); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, (VRes - (VRes * OptionVResreserved / 100)) - lines, HRes - 1, (VRes - (VRes * OptionVResreserved / 100)) - 1, PromptBC); // erase the lines to be scrolled off
    }
    else
    {
        lines = -lines;
        for (int i = (VRes - (VRes * OptionVResreserved / 100)) - 1; i >= lines; i--)
        {
            int d = i * HRes, s = (i - lines) * HRes;
            for (int c = 0; c < (HRes << 1); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
/*  @endcond */

void cmd_tile(void)
{
    unsigned char *tp;
    uint32_t bcolour = 0xFFFFFFFF, fcolour = 0xFFFFFFFF;
    int xlen = 1, ylen = 1;
    if (DISPLAY_TYPE != SCREENMODE1)
        StandardError(40);
    if (checkstring(cmdline, (unsigned char *)"RESET"))
    {
        fcolour = (FullColour) ? RGB555(Option.DefaultFC) : RGB332(Option.DefaultFC);
        bcolour = (FullColour) ? RGB555(Option.DefaultBC) : RGB332(Option.DefaultBC);
        for (int x = 0; x < X_TILE; x++)
        {
            for (int y = 0; y < Y_TILE; y++)
            {
#ifdef HDMI
                if (FullColour)
                {
#endif
                    if (fcolour != 0xFFFFFFFF)
                        tilefcols[y * X_TILE + x] = fcolour;
                    if (bcolour != 0xFFFFFFFF)
                        tilebcols[y * X_TILE + x] = bcolour;
#ifdef HDMI
                }
                else
                {
                    if (fcolour != 0xFFFFFFFF)
                        tilefcols_w[y * X_TILE + x] = fcolour;
                    if (bcolour != 0xFFFFFFFF)
                        tilebcols_w[y * X_TILE + x] = bcolour;
                }
#endif
            }
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"HEIGHT")))
    {
        ytileheight = getint(tp, 8, VRes);
        Y_TILE = VRes / ytileheight;
        if (VRes % ytileheight)
            Y_TILE++;
        ClearScreen(Option.DefaultBC);
    }
    else
    {
        getcsargs(&cmdline, 11);
        if (!(DISPLAY_TYPE == SCREENMODE1))
            return;
        if (argc < 5)
            SyntaxError();
        ;
        int x = getint(argv[0], 0, X_TILE - 1);
        int y = getint(argv[2], 0, Y_TILE - 1);
        int tilebcolour, tilefcolour;
        if (*argv[4])
        {
            tilefcolour = getColour((char *)argv[4], 0);
            fcolour = (FullColour) ? RGB555(tilefcolour) : RGB332(tilefcolour);
        }
        if (argc >= 7 && *argv[6])
        {
            tilebcolour = getColour((char *)argv[6], 0);
            bcolour = (FullColour) ? RGB555(tilebcolour) : RGB332(tilebcolour);
        }
        if (argc >= 9 && *argv[8])
        {
            xlen = getint(argv[8], 1, X_TILE - x);
        }
        if (argc >= 11 && *argv[10])
        {
            ylen = getint(argv[10], 1, Y_TILE - y);
        }
        for (int xp = x; xp < x + xlen; xp++)
        {
            for (int yp = y; yp < y + ylen; yp++)
            {
#ifdef HDMI
                if (FullColour)
                {
#endif
                    if (fcolour != 0xFFFFFFFF)
                        tilefcols[yp * X_TILE + xp] = (uint16_t)fcolour;
                    if (bcolour != 0xFFFFFFFF)
                        tilebcols[yp * X_TILE + xp] = (uint16_t)bcolour;
#ifdef HDMI
                }
                else
                {
                    if (fcolour != 0xFFFFFFFF)
                        tilefcols_w[yp * X_TILE + xp] = (uint8_t)fcolour;
                    if (bcolour != 0xFFFFFFFF)
                        tilebcols_w[yp * X_TILE + xp] = (uint8_t)bcolour;
                }
#endif
            }
        }
    }
}
void cmd_map(void)
{
    unsigned char *p;
    if (!(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3 || DISPLAY_TYPE == SCREENMODE5))
        StandardError(40);
    if ((p = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        mapreset();
    }
    else if (checkstring(cmdline, (unsigned char *)"GRAYSCALE") || checkstring(cmdline, (unsigned char *)"GREYSCALE"))
    {
        while (v_scanline != 0)
        {
        }
        for (int i = 1; i <= 32; i++)
        {
            int j = i * 8 - (8 - i / 4 + 1);
            if (j < 0)
                j = 0;
            map256[i - 1] = remap256[i - 1] = RGB555(j * 65536 + j * 256 + j);
            map256[i + 32 - 1] = remap256[i + 32 - 1] = RGB555(j);
            map256[i + 64 - 1] = remap256[i + 64 - 1] = RGB555(j * 256);
            map256[i + 96 - 1] = remap256[i + 96 - 1] = RGB555(j * 256 + j);
            map256[i + 128 - 1] = remap256[i + 128 - 1] = RGB555(j * 65536);
            map256[i + 160 - 1] = remap256[i + 160 - 1] = RGB555(j * 65536 + j);
            map256[i + 192 - 1] = remap256[i + 192 - 1] = RGB555(j * 65536 + j * 256);
            map256[i + 224 - 1] = remap256[i + 224 - 1] = RGB555(j * 65536 + j * 256 + j);
        }
        for (int i = 1; i <= 16; i++)
        {
            int j = i * 16 - (16 - i + 1);
            map16quads[i - 1] = remap332[i - 1] = RGB332(j * 65536 + j * 256 + j) | (RGB332(j * 65536 + j * 256 + j) << 8) | (RGB332(j * 65536 + j * 256 + j) << 16) | (RGB332(j * 65536 + j * 256 + j) << 24);
            map16pairs[i - 1] = remap555[i - 1] = (RGB555(j * 65536 + j * 256 + j) | (RGB555(j * 65536 + j * 256 + j) << 16));
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MAXIMITE")))
    {
        while (v_scanline != 0)
        {
        }
        for (int i = 0; i < 16; i++)
            map256[i] = remap256[i] = RGB555(CMM1map[i]);
        for (int i = 0; i < 16; i++)
        {
            map16quads[i] = remap332[i] = RGB332(CMM1map[i]) | (RGB332(CMM1map[i]) << 8) | (RGB332(CMM1map[i]) << 16) | (RGB332(CMM1map[i]) << 24);
            map16pairs[i] = remap555[i] = RGB555(CMM1map[i]) | (RGB555(CMM1map[i]) << 8);
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SET")))
    {
        while (v_scanline != 0)
        {
        }
        for (int i = 0; i < 256; i++)
            map256[i] = remap256[i];
        for (int i = 0; i < 16; i++)
        {
            map16pairs[i] = remap555[i];
            map16quads[i] = remap332[i];
        }
    }
    else
    {
        int cl = getint(cmdline, 0, 255);
        if (DISPLAY_TYPE != SCREENMODE5 && cl > 15)
            error("Mode supports 16 colours (0-15)");
        while (*cmdline && tokenfunction(*cmdline) != op_equal)
            cmdline++;
        if (!*cmdline)
            SyntaxError();
        ++cmdline;
        if (!*cmdline)
            SyntaxError();
        int col = getColour((char *)cmdline, 0);
        remap256[cl] = RGB555(col);
        remap555[cl] = RGB555(col) | (RGB555(col) << 16);
        remap332[cl] = RGB332(col) | (RGB332(col) << 8) | (RGB332(col) << 16) | (RGB332(col) << 24);
    }
}
#endif
void setmode(int mode, bool clear)
{
#ifdef HDMICUTDOWN
    /* The special RGB332 640x480 / 720x400 only implement modes 1, 2 and 5
       (HDMIloopBTH640). Reject modes 3 and 4 rather than scan out
       an unhandled DISPLAY_TYPE. */
    if ((Option.Resolution == R640x480x8 || Option.Resolution == R720x400x8) && (mode == 3 || mode == 4))
        error("Mode not available in this resolution");
    /* The RGB332 medium resolutions (HDMIloop3) offer only modes 1 and 2:
       mode 3 (>=192000 bytes) and mode 5 (>=96000) overflow the 96000-byte
       cut-down framebuffer pool; mode 4 is already gated off as
       non-FullColour below. */
    if ((Option.Resolution == R800x600 || Option.Resolution == R848x480 || Option.Resolution == R800x480) && (mode == 3 || mode == 5))
        error("Mode not available in this resolution");
#endif
    closeframebuffer('A');
    if (clear)
        memset((void *)FRAMEBUFFER, 0, framebuffersize);
#ifdef HDMI
    /* Bounded wait: a live RESOLUTION switch can leave the scanout briefly
       stopped (or, if core1 ever wedges during a rebuild, stopped for
       good). Never spin here forever — that would hang the BASIC prompt on
       core0 with no way out. The frame-boundary sync is best-effort. */
    {
        uint64_t dl = time_us_64() + 50000;
        while (v_scanline != 0 && time_us_64() < dl)
        {
        }
    }
#else
    while (QVgaScanLine != 0)
    {
    }
#endif
    if (mode == 5)
    {
        DISPLAY_TYPE = SCREENMODE5;
        ScreenSize = MODE5SIZE;
    }
    else if (mode == 4)
    {
        if (!(FullColour))
            error("Mode not available in this resolution");
        DISPLAY_TYPE = SCREENMODE4;
        ScreenSize = MODE4SIZE;
    }
    else if (mode == 3)
    {
        DISPLAY_TYPE = SCREENMODE3;
        ScreenSize = MODE3SIZE;
    }
    else if (mode == 2)
    {
        DISPLAY_TYPE = SCREENMODE2;
        ScreenSize = MODE2SIZE;
    }
    else
    { // mode=1
#ifdef rp2350
#ifndef HDMI
        tilefcols = (uint16_t *)((uint8_t *)FRAMEBUFFER + (MODE1SIZE * 3));
        tilebcols = (uint16_t *)((uint8_t *)FRAMEBUFFER + (MODE1SIZE * 3) + (MODE1SIZE >> 1));
#else
        mapreset();
#endif
#endif
        DISPLAY_TYPE = SCREENMODE1;
        ScreenSize = MODE1SIZE;
    }
    //    uSec(10000);
    ResetDisplay();
    if (clear)
    {
        memset((void *)WriteBuf, 0, ScreenSize);
        CurrentX = CurrentY = 0;
        ClearScreen(Option.DefaultBC);
    }
#ifdef HDMI
    if (FullColour || MediumRes)
    {
#endif
        if (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE4 || DISPLAY_TYPE == SCREENMODE5)
        {
            SetFont((6 << 4) | 1);
            PromptFont = (6 << 4) | 1;
        }
        else
        {
            SetFont(1);
            PromptFont = 1;
        }
#ifdef HDMI
    }
    else
    {
        if (DISPLAY_TYPE == SCREENMODE1)
        {
            SetFont((2 << 4) | 1);
            PromptFont = (2 << 4) | 1;
        }
        else if (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5)
        {
            SetFont((6 << 4) | 1);
            PromptFont = (6 << 4) | 1;
        }
        else if (DISPLAY_TYPE == SCREENMODE3)
        {
            SetFont(1);
            PromptFont = 1;
        }
    }
#endif
    if (mode == Option.DISPLAY_TYPE - SCREENMODE1 + 1)
    {
        SetFont(Option.DefaultFont);
        PromptFont = Option.DefaultFont;
    }
    if (DISPLAY_TYPE == SCREENMODE1)
    {
        ytileheight = gui_font_height;
        Y_TILE = VRes / ytileheight;
        if (VRes % ytileheight)
            Y_TILE++;
#ifdef PICOMITEVGA
        if (DISPLAY_TYPE == SCREENMODE1 /* && WriteBuf==DisplayBuf*/)
        {
            gui_fcolour = Option.DefaultFC;
            gui_bcolour = Option.DefaultBC;
#ifdef HDMI
            settiles();
#else
            int bcolour = RGB121pack(gui_bcolour);
            int fcolour = RGB121pack(gui_fcolour);
            for (int x = 0; x < X_TILE; x++)
            {
                for (int y = 0; y < Y_TILE; y++)
                {
                    tilefcols[y * X_TILE + x] = fcolour;
                    tilebcols[y * X_TILE + x] = bcolour;
                }
            }
#endif
        }
#endif
    }

    clearrepeat();
}

void cmd_mode(void)
{
    int mode = getint(cmdline, 1, MAXMODES);
    setmode(mode, true);
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
    OSK_Invalidate();
#endif
}
#ifdef HDMICUTDOWN
/* RESOLUTION <res>[,<speed>]
   Live, no-reboot switch of the HDMIBTH/HDMIWEB display resolution:
     RESOLUTION 1024            -> 1024x600 RGB332 (always 252 MHz)
     RESOLUTION 640 [,speed]    -> 640x480  RGB332
     RESOLUTION 720             -> 720x400  RGB332 (always 283.2 MHz)
     RESOLUTION 800             -> 800x600  RGB332 (always 360 MHz, modes 1/2)
     RESOLUTION 848             -> 848x480  RGB332 (always 336 MHz, modes 1/2)
     RESOLUTION 800x480         -> 800x480  RGB332 (always 333 MHz, modes 1/2)
   640x480 takes the same optional 252000/315000/378000 CPU speed as the
   full HDMI build (252000 = 60Hz default, 315000 = 75Hz, 378000 = 60Hz);
   the command moves clk_sys via CPUSpeedRuntime() first because HDMICore
   derives clk_hstx from the clk_sys it observes when it rebuilds.
   Nothing is saved to flash — a reboot returns to OPTION RESOLUTION. */
void cmd_resolution(void)
{
    getargs(&cmdline, 3, (unsigned char *)",");
    if (argc != 1 && argc != 3)
        SyntaxError();
    int newres = -1; // every path below assigns or errors; init quietens -Wmaybe-uninitialized
    int speed = Freq252P; /* 1024x600 always runs at 252 MHz */
    if (checkstring(argv[0], (unsigned char *)"1024") || checkstring(argv[0], (unsigned char *)"1024x600"))
    {
        if (argc == 3)
            SyntaxError(); // the speed argument only applies to 640x480
        newres = R1024x600;
    }
    else if (checkstring(argv[0], (unsigned char *)"640") || checkstring(argv[0], (unsigned char *)"640x480"))
    {
        newres = R640x480x8;
        if (argc == 3)
        {
            int i = getint(argv[2], Freq252P, Freq378P);
            if (!(i == Freq252P || i == Freq480P || i == Freq378P))
                error("Invalid speed");
            speed = i;
        }
    }
    else if (checkstring(argv[0], (unsigned char *)"720") || checkstring(argv[0], (unsigned char *)"720x400"))
    {
        if (argc == 3)
            SyntaxError(); // 720x400 has a single fixed timing (283.2 MHz)
        newres = R720x400x8;
        speed = Freq400;
    }
    else if (checkstring(argv[0], (unsigned char *)"800") || checkstring(argv[0], (unsigned char *)"800x600"))
    {
        if (argc == 3)
            SyntaxError(); // single fixed timing
        newres = R800x600;
        speed = FreqSVGA;
    }
    else if (checkstring(argv[0], (unsigned char *)"848") || checkstring(argv[0], (unsigned char *)"848x480"))
    {
        if (argc == 3)
            SyntaxError(); // single fixed timing
        newres = R848x480;
        speed = Freq848;
    }
    else if (checkstring(argv[0], (unsigned char *)"800x480"))
    {
        if (argc == 3)
            SyntaxError(); // single fixed timing
        newres = R800x480;
        speed = FreqY;
    }
    else
        error("Invalid resolution");
    /* Rebuild the scanout when the physical resolution changes OR when
       only the CPU speed changes (clk_hstx is derived from clk_sys at
       rebuild, so a speed change alone still needs the teardown). The
       rebuild always drops to mode 1 internally (see HDMICore teardown),
       so the target mode is applied afterwards via setmode(). */
    int rebuild = (newres != Option.Resolution);
    if (speed != Option.CPU_Speed)
    {
        /* A clk_sys change silently corrupts user PIO timing — same guard
           as the full HDMI build / CPU SPEED. */
        if (UserPIOActive())
            error("Invalid while PIO in use");
        if (CPUSpeedRuntime(speed))
            error("Invalid clock speed");
        rebuild = 1;
    }
    if (rebuild)
        restartHDMI(newres);
    setmode(1, true);
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
    OSK_Invalidate();
#endif
}
#elif defined(HDMI)
/* RESOLUTION <res>[,<speed>]
   Live, no-reboot switch of the HDMI/HDMIUSB display resolution. Accepts
   the same resolution strings as OPTION RESOLUTION (640[x480] with the
   optional 252000/315000/378000 speed, 720[x400], 1280[x720], 1024[x768],
   1024x600, 800[x600], 848[x480], 800x480) but switches the display
   without a reboot: the resolutions run at different CPU speeds, so the
   command first moves clk_sys via CPUSpeedRuntime() (exactly what CPU
   SPEED does on the non-display builds) and then asks core1 to tear down
   and rebuild the HSTX scanout for the new geometry (restartHDMI).
   Nothing is saved to flash — a reboot returns to OPTION RESOLUTION.

   The one hard limit: 800x600, 848x480 and 800x480 need a framebuffer
   larger than the default 153600-byte pool. That extra memory is carved
   out of the MMBasic heap at boot (see main()), which cannot be done
   while the heap is live, so switching INTO them is only allowed when the
   boot resolution already allocated an equal-or-larger framebuffer;
   otherwise they need OPTION RESOLUTION (which reboots). Switching AWAY
   from them is always allowed (the smaller layout fits the bigger pool;
   the heap keeps its boot-time size either way). */
void cmd_resolution(void)
{
    getargs(&cmdline, 3, (unsigned char *)",");
    if (argc != 1 && argc != 3)
        SyntaxError();
    int newres = -1; // every path below assigns or errors; init quietens -Wmaybe-uninitialized
    if (checkstring(argv[0], (unsigned char *)"640") || checkstring(argv[0], (unsigned char *)"640x480"))
    {
        newres = R640x480f252;
        if (argc == 3)
        {
            int i = getint(argv[2], Freq252P, Freq378P);
            if (i == Freq480P)
                newres = R640x480f315;
            else if (i == Freq378P)
                newres = R640x480f378;
            else if (i != Freq252P)
                error("Invalid speed");
        }
    }
    else if (argc == 3)
        SyntaxError(); // the speed argument only applies to 640x480
    else if (checkstring(argv[0], (unsigned char *)"1280") || checkstring(argv[0], (unsigned char *)"1280x720"))
        newres = R1280x720;
    else if (checkstring(argv[0], (unsigned char *)"1024") || checkstring(argv[0], (unsigned char *)"1024x768"))
        newres = R1024x768;
    else if (checkstring(argv[0], (unsigned char *)"1024x600"))
        newres = R1024x600;
    else if (checkstring(argv[0], (unsigned char *)"720") || checkstring(argv[0], (unsigned char *)"720x400"))
        newres = R720x400;
    else if (checkstring(argv[0], (unsigned char *)"800") || checkstring(argv[0], (unsigned char *)"800x600"))
        newres = R800x600;
    else if (checkstring(argv[0], (unsigned char *)"848") || checkstring(argv[0], (unsigned char *)"848x480"))
        newres = R848x480;
    else if (checkstring(argv[0], (unsigned char *)"800x480"))
        newres = R800x480;
    else
        error("Invalid resolution");

    /* Largest mode-buffer layout the target resolution can produce, i.e.
       the framebuffer pool main() would have allocated for it at boot.
       Must fit the pool the CURRENT boot actually allocated. */
    uint32_t need = (newres == R800x600)   ? 400 * 300 * 2
                    : (newres == R848x480) ? 424 * 240 * 2
                    : (newres == R800x480) ? 400 * 240 * 2
                                           : 320 * 240 * 2;
    if (need > framebuffersize)
        error("Needs more memory: use OPTION RESOLUTION");

    if (newres != Option.Resolution)
    {
        /* A clk_sys change silently corrupts user PIO timing — same guard
           as CPU SPEED on the non-display builds. */
        if (CPUFreqs[newres] != Option.CPU_Speed && UserPIOActive())
            error("Invalid while PIO in use");
        /* Move clk_sys FIRST: HDMICore derives clk_hstx from the clk_sys
           it observes when it rebuilds. The old-resolution scanout glitches
           for the few ms until core1 notices the switch request, which is
           hidden inside the mode-change blank anyway. */
        if (CPUFreqs[newres] != Option.CPU_Speed && CPUSpeedRuntime(CPUFreqs[newres]))
            error("Invalid clock speed");
        /* RAM-only mirror of what OPTION RESOLUTION would persist, so the
           prompt font suits the new geometry; not saved to flash. */
        Option.DefaultFont = (newres == R1280x720 || newres == R1024x768) ? ((2 << 4) | 1) : 1;
        restartHDMI(newres);
    }
    setmode(1, true);
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
    OSK_Invalidate();
#endif
}
#endif
#endif
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/*  @endcond */
void fun_mmcharwidth(void)
{
    CheckDisplay();
    iret = FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
    targ = T_INT;
}

void fun_mmcharheight(void)
{
    CheckDisplay();
    iret = FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
    targ = T_INT;
}
/*  @endcond */

/****************************************************************************************************
 ****************************************************************************************************

 Basic drawing primitives for a user defined LCD display driver (ie, OPTION LCDPANEL USER)
 all drawing is done using either DrawRectangleUser() or DrawBitmapUser()

 ****************************************************************************************************
****************************************************************************************************/
void cmd_refresh(void)
{
    CheckDisplay();

#if PICOMITERP2350
    if (Option.DISPLAY_TYPE >= NEXTGEN)
    {
        if (!Option.Refresh)
        {
            multicore_fifo_push_blocking(6);
            multicore_fifo_push_blocking((uint32_t)low_x | (high_x << 16));
            multicore_fifo_push_blocking((uint32_t)low_y | (high_y << 16));
            multicore_fifo_push_blocking((uint32_t)ScrollStart);
            low_x = silly_low;
            high_y = silly_high;
            low_y = silly_low;
            high_x = silly_high;
        }
    }
    else
    {
#endif
        low_y = 0;
        high_y = DisplayVRes - 1;
        low_x = 0;
        high_x = DisplayHRes - 1;
        Display_Refresh();
#if PICOMITERP2350
    }
#endif
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#ifdef PICOMITEVGA

void Display_Refresh(void)
{
}
#endif
void DrawPixel2(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= HRes || y >= VRes)
        return;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    uint8_t *p = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 3)) + (x >> 3));
    uint8_t bit = 1 << (x % 8);
    if (c)
        *p |= bit;
    else
        *p &= ~bit;
}
void DrawRectangle2(int x1, int y1, int x2, int y2, int c)
{
    int x, y, x1p, x2p, t;
    unsigned char mask;
    volatile unsigned char *p;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 == x2)
    {
        for (y = y1; y <= y2; y++)
        {
            p = &WriteBuf[(y * (HRes >> 3)) + (x1 >> 3)];
            mask = 1 << (x1 % 8); // get the bit position for this bit
            if (c)
            {
                *p |= mask;
            }
            else
            {
                *p &= (~mask);
            }
        }
    }
    else
    {
        for (y = y1; y <= y2; y++)
        {
            x1p = x1;
            x2p = x2;
            if ((x1 % 8) != 0)
            {
                p = &WriteBuf[(y * (HRes >> 3)) + (x1 >> 3)];
                for (x = x1; x <= x2 && (x % 8) != 0; x++)
                {
                    mask = 1 << (x % 8); // get the bit position for this bit
                    if (c)
                    {
                        *p |= mask;
                    }
                    else
                    {
                        *p &= (~mask);
                    }
                    x1p++;
                }
            }
            if (x1p - 1 != x2 && (x2 % 8) != 7)
            {
                p = &WriteBuf[(y * (HRes >> 3)) + (x2p >> 3)];
                for (x = (x2 & 0xFFF8); x <= x2; x++)
                {
                    mask = 1 << (x % 8); // get the bit position for this bit
                    if (c)
                    {
                        *p |= mask;
                    }
                    else
                    {
                        *p &= (~mask);
                    }
                    x2p--;
                }
            }
            p = &WriteBuf[(y * (HRes >> 3)) + (x1p >> 3)];
            for (x = x1p; x < x2p; x += 8)
            {
                if (c)
                {
                    *p++ = 0xFF;
                }
                else
                {
                    *p++ = 0;
                }
            }
        }
    }
}

void DrawBitmap2(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    int i, j, k, m, x, y, loc;
    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0)
        return;
    int tilematch = 0;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
#ifdef PICOMITEVGA
    int xa = 8;
    int ya = ytileheight;
    if (x1 % xa == 0 && y1 % ya == 0 && width * scale % xa == 0 && height * scale % ya == 0)
        tilematch = 1;
#endif
    if (fc == 0 && bc > 0 && (tilematch == 0 || editactive))
    {
        for (i = 0; i < height; i++)
        { // step thru the font scan line by line
            for (j = 0; j < scale; j++)
            { // repeat lines to scale the font
                for (k = 0; k < width; k++)
                { // step through each bit in a scan line
                    for (m = 0; m < scale; m++)
                    { // repeat pixels to scale in the x axis
                        x = x1 + k * scale + m;
                        y = y1 + i * scale + j;
                        mask = 1 << (x % 8); // get the bit position for this bit
                        if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                        { // if the coordinates are valid
                            loc = (y * (HRes >> 3)) + (x >> 3);
                            if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                            {
                                WriteBuf[loc] &= ~mask;
                            }
                            else
                                WriteBuf[loc] |= mask;
                        }
                    }
                }
            }
        }
    }
    else
    {
#ifdef PICOMITEVGA
        if (tilematch)
        {
            // the bitmap is aligned with the tiles
            int bcolour, fcolour;
#ifdef HDMI
            fcolour = (FullColour) ? RGB555(fc) : RGB332(fc);
            bcolour = (FullColour) ? RGB555(bc) : RGB332(bc);
#else
            fcolour = RGB121pack(fc);
            bcolour = RGB121pack(bc);
#endif
            int xt = x1 / xa;
            int yt = y1 / ya;
            int w = width * scale / xa;
            int h = height * scale / ya;
            // clamp tile loop bounds to valid tile range
            int endx = (xt + w > X_TILE) ? X_TILE : xt + w;
            int endy = (yt + h > Y_TILE) ? Y_TILE : yt + h;
            if (xt < 0)
                xt = 0;
            if (yt < 0)
                yt = 0;
//            int pos;
#ifdef HDMI
            if (FullColour)
            {
#endif
                for (int yy = yt; yy < endy; yy++)
                {
                    for (int xx = xt; xx < endx; xx++)
                    {
                        tilefcols[yy * X_TILE + xx] = (uint16_t)fcolour;
                        tilebcols[yy * X_TILE + xx] = (uint16_t)bcolour;
                    }
                }
#ifdef HDMI
            }
            else
            {
                for (int yy = yt; yy < endy; yy++)
                {
                    for (int xx = xt; xx < endx; xx++)
                    {
                        tilefcols_w[yy * X_TILE + xx] = (uint8_t)fcolour;
                        tilebcols_w[yy * X_TILE + xx] = (uint8_t)bcolour;
                    }
                }
            }
#endif
        }
#endif
        if (fc == 0 && bc != 0 && fc != bc && bc != -1)
            fc = 1;
        if (bc <= 0 || fc == 0)
        {
            for (i = 0; i < height; i++)
            { // step thru the font scan line by line
                for (j = 0; j < scale; j++)
                { // repeat lines to scale the font
                    for (k = 0; k < width; k++)
                    { // step through each bit in a scan line
                        for (m = 0; m < scale; m++)
                        { // repeat pixels to scale in the x axis
                            x = x1 + k * scale + m;
                            y = y1 + i * scale + j;
                            mask = 1 << (x % 8); // get the bit position for this bit
                            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                            { // if the coordinates are valid
                                loc = (y * (HRes >> 3)) + (x >> 3);
                                if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                                {
                                    if (fc)
                                    {
                                        WriteBuf[loc] |= mask;
                                    }
                                    else
                                    {
                                        WriteBuf[loc] &= ~mask;
                                    }
                                }
                                else
                                {
                                    if (bc > 0)
                                    {
                                        WriteBuf[loc] |= mask;
                                    }
                                    else if (bc == 0)
                                    {
                                        WriteBuf[loc] &= ~mask;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            for (i = 0; i < height; i++)
            { // step thru the font scan line by line
                for (j = 0; j < scale; j++)
                { // repeat lines to scale the font
                    for (k = 0; k < width; k++)
                    { // step through each bit in a scan line
                        for (m = 0; m < scale; m++)
                        { // repeat pixels to scale in the x axis
                            x = x1 + k * scale + m;
                            y = y1 + i * scale + j;
                            mask = 1 << (x % 8); // get the bit position for this bit
                            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                            { // if the coordinates are valid
                                loc = (y * (HRes >> 3)) + (x >> 3);
                                if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                                {
                                    if (fc)
                                    {
                                        WriteBuf[loc] |= mask;
                                    }
                                    else
                                    {
                                        WriteBuf[loc] &= ~mask;
                                    }
                                }
                                else
                                    WriteBuf[loc] &= ~mask;
                            }
                        }
                    }
                }
            }
        }
    }
}

void ScrollLCD2(int lines)
{
    if (lines == 0)
        return;

    if (lines >= 0)
    {
#ifdef PICOMITEVGA
#ifndef HDMI
        while (QVgaScanLine != 0)
        {
        }
#else
        while (v_scanline != 0)
        {
        }
#endif
        int ya = ytileheight;
        if ((lines % ya == 0))
        {
            int offset = lines / ya;
            for (int y = 0; y < Y_TILE - offset; y++)
            {
                int d = y * X_TILE;
                int s = (y + offset) * X_TILE;
                for (int x = 0; x < X_TILE; x++)
                {
#ifdef HDMI
                    if (FullColour)
                    {
#endif
                        tilefcols[d + x] = tilefcols[s + x];
                        tilebcols[d + x] = tilebcols[s + x];
#ifdef HDMI
                    }
                    else
                    {
                        tilefcols_w[d + x] = tilefcols_w[s + x];
                        tilebcols_w[d + x] = tilebcols_w[s + x];
                    }
#endif
                }
            }
        }
#endif
        for (int i = 0; i < (VRes - (VRes * OptionVResreserved / 100)) - lines; i++)
        {
            int d = i * (HRes >> 3), s = (i + lines) * (HRes >> 3);
            for (int c = 0; c < (HRes >> 3); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, (VRes - (VRes * OptionVResreserved / 100)) - lines, HRes - 1, (VRes - (VRes * OptionVResreserved / 100)) - 1, 0); // erase the lines to be scrolled off
    }
    else
    {
        lines = -lines;
#ifdef PICOMITEVGA
#ifndef HDMI
        while (QVgaScanLine != 0)
        {
        }
#else
        while (v_scanline != 0)
        {
        }
#endif
        int ya = ytileheight;
        if ((lines % ya == 0))
        {
            int offset = lines / ya;
            for (int y = Y_TILE - 1; y >= offset; y--)
            {
                int d = y * X_TILE;
                int s = (y - offset) * X_TILE;
                for (int x = 0; x < X_TILE; x++)
                {
#ifdef HDMI
                    if (FullColour)
                    {
#endif
                        tilefcols[d + x] = tilefcols[s + x];
                        tilebcols[d + x] = tilebcols[s + x];
#ifdef HDMI
                    }
                    else
                    {
                        tilefcols_w[d + x] = tilefcols_w[s + x];
                        tilebcols_w[d + x] = tilebcols_w[s + x];
                    }
#endif
                }
            }
        }
#endif
        for (int i = (VRes - (VRes * OptionVResreserved / 100)) - 1; i >= lines; i--)
        {
            int d = i * (HRes >> 3), s = (i - lines) * (HRes >> 3);
            for (int c = 0; c < (HRes >> 3); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, 0); // erase the lines introduced at the top
    }
}
void DrawBuffer2(int x1, int y1, int x2, int y2, unsigned char *p)
{
    int x, y, t, loc;
    unsigned char mask;
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0)
        x1 = 0;
    if (x1 >= HRes)
        x1 = HRes - 1;
    if (x2 < 0)
        x2 = 0;
    if (x2 >= HRes)
        x2 = HRes - 1;
    if (y1 < 0)
        y1 = 0;
    if (y1 >= VRes)
        y1 = VRes - 1;
    if (y2 < 0)
        y2 = 0;
    if (y2 >= VRes)
        y2 = VRes - 1;
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            c.rgbbytes[0] = *p++;
            if (c.rgbbytes[0] < 0x40)
                c.rgbbytes[0] = 0;
            c.rgbbytes[1] = *p++;
            if (c.rgbbytes[1] < 0x40)
                c.rgbbytes[1] = 0;
            c.rgbbytes[2] = *p++;
            if (c.rgbbytes[2] < 0x40)
                c.rgbbytes[2] = 0;
            c.rgbbytes[3] = 0;
            loc = (y * (HRes >> 3)) + (x >> 3);
            mask = 1 << (x % 8); // get the bit position for this bit
            if (c.rgb)
            {
                WriteBuf[loc] |= mask;
            }
            else
            {
                WriteBuf[loc] &= (~mask);
            }
        }
    }
}
void DrawBuffer2Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p)
{
    int x, y, t, loc, toggle = 0;
    unsigned char mask;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                loc = (y * (HRes >> 3)) + (x >> 3);
                mask = 1 << (x % 8); // get the bit position for this bit
                if (toggle)
                {
                    if (*p++ & 0xF0)
                    {
                        WriteBuf[loc] |= mask;
                    }
                    else if (blank == -1)
                    {
                        WriteBuf[loc] &= (~mask);
                    }
                }
                else
                {
                    if (*p & 0xF)
                    {
                        WriteBuf[loc] |= mask;
                    }
                    else if (blank == -1)
                    {
                        WriteBuf[loc] &= (~mask);
                    }
                }
                toggle = !toggle;
            }
            else
            {
                if (toggle)
                    p++;
                toggle = !toggle;
            }
        }
    }
}
int RGB555toRGB888(int rgb555val)
{
    // Extract 5-bit channels
    int r5 = (rgb555val >> 10) & 0x1F;
    int g5 = (rgb555val >> 5) & 0x1F;
    int b5 = rgb555val & 0x1F;

    // Expand to 8-bit channels
    int r8 = (r5 << 3) | (r5 >> 2);
    int g8 = (g5 << 3) | (g5 >> 2);
    int b8 = (b5 << 3) | (b5 >> 2);

    // Pack into 0xRRGGBB
    return (r8 << 16) | (g8 << 8) | b8;
}

void ReadBuffer2(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t, loc;
    //    uint8_t *pp;
    unsigned char mask;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0)
        xx1 = 0;
    if (x1 >= HRes)
        xx1 = HRes - 1;
    if (x2 < 0)
        xx2 = 0;
    if (x2 >= HRes)
        xx2 = HRes - 1;
    if (y1 < 0)
        yy1 = 0;
    if (y1 >= VRes)
        yy1 = VRes - 1;
    if (y2 < 0)
        yy2 = 0;
    if (y2 >= VRes)
        yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++)
    {
        for (x = xx1; x <= xx2; x++)
        {
#ifdef PICOMITEVGA
            int tile = x / 8 + (y / ytileheight) * X_TILE, back, front;
#ifdef HDMI
            if (FullColour)
            {
#endif
                back = RGB121map[tilebcols[tile] & 0xF];
                front = RGB121map[tilefcols[tile] & 0xF];
#ifdef HDMI
            }
            else
            {
                back = RGB555toRGB888(map256[tilebcols_w[tile] & 0xFF]);
                front = RGB555toRGB888(map256[tilefcols_w[tile] & 0xF]);
            }
#endif
#else
            int front = 0xFFFFFF;
            int back = 0;
#endif
            loc = (y * (HRes >> 3)) + (x >> 3);
            mask = 1 << (x % 8); // get the bit position for this bit
            if (WriteBuf[loc] & mask)
            {
                *c++ = (front & 0xFF);
                *c++ = (front >> 8) & 0xFF;
                *c++ = front >> 16;
            }
            else
            {
                *c++ = (back & 0xFF);
                *c++ = (back >> 8) & 0xFF;
                *c++ = back >> 16;
            }
        }
    }
}

void ReadBuffer2Fast(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t, loc, toggle = 0;
    ;
    //    uint8_t *pp;
    unsigned char mask;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = FRAMEBUFFER;
#endif
    if (x2 <= x1)
    {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1)
    {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                loc = (y * (HRes >> 3)) + (x >> 3);
                mask = 1 << (x % 8); // get the bit position for this bit
                if (toggle)
                {
                    if (WriteBuf[loc] & mask)
                    {
                        *c++ |= 0xF0;
                    }
                    else
                    {
                        *c++ &= 0x0F;
                    }
                }
                else
                {
                    if (WriteBuf[loc] & mask)
                    {
                        *c = 0xF;
                    }
                    else
                    {
                        *c = 0x0;
                    }
                }
                toggle = !toggle;
            }
            else
            {
                if (toggle)
                    *c++ &= 0xF;
                else
                    *c = 0;
                toggle = !toggle;
            }
        }
    }
}

void MIPS16 ConfigDisplayVirtual(unsigned char *p)
{
    getcsargs(&p, 13);
    if (checkstring(argv[0], (unsigned char *)"VIRTUAL_M"))
    {
        DISPLAY_TYPE = VIRTUAL_M;
    }
    else if (checkstring(argv[0], (unsigned char *)"VIRTUAL_C"))
    {
        DISPLAY_TYPE = VIRTUAL_C;
    }
    else
        return;
    Option.DISPLAY_TYPE = DISPLAY_TYPE;
    Option.DISPLAY_ORIENTATION = LANDSCAPE;
}
void MIPS16 InitDisplayVirtual(void)
{
#if PICOMITERP2350
    if (Option.DISPLAY_TYPE == 0 || Option.DISPLAY_TYPE < VIRTUAL || Option.DISPLAY_TYPE >= VGA222)
        return;
#else
    if (Option.DISPLAY_TYPE == 0 || Option.DISPLAY_TYPE < VIRTUAL)
        return;
#endif
    DisplayHRes = HRes = display_details[Option.DISPLAY_TYPE].horizontal;
    DisplayVRes = VRes = display_details[Option.DISPLAY_TYPE].vertical;
    if (Option.DISPLAY_TYPE == VIRTUAL_M)
    {
        DrawRectangle = DrawRectangle2;
        DrawBitmap = DrawBitmap2;
        ScrollLCD = ScrollLCD2;
        DrawBuffer = DrawBuffer2;
        ReadBuffer = ReadBuffer2;
        DrawBufferFast = DrawBuffer2Fast;
        ReadBufferFast = ReadBuffer2Fast;
        DrawPixel = DrawPixel2;
    }
    else
    {
        DrawRectangle = DrawRectangle16;
        DrawBitmap = DrawBitmap16;
        ScrollLCD = ScrollLCD16;
        DrawBuffer = DrawBuffer16;
        ReadBuffer = ReadBuffer16;
        DrawBufferFast = DrawBuffer16Fast;
        ReadBufferFast = ReadBuffer16Fast;
        DrawPixel = DrawPixel16;
    }
    WriteBuf = FRAMEBUFFER;
}

/*  @endcond */

#ifdef PICOMITEVGA
#ifdef HDMI
void fun_getscanline(void)
{
    if (Option.Resolution == R1280x720)
    {
        iret = v_scanline - 30;
        if (iret < 0)
            iret += 750;
        targ = T_INT;
    }
    else if (Option.Resolution == R640x480f315)
    {
        iret = v_scanline - 20;
        if (iret < 0)
            iret += 500;
        targ = T_INT;
    }
    else if (Option.Resolution == R1024x768)
    {
        iret = v_scanline - 38;
        if (iret < 0)
            iret += 806;
        targ = T_INT;
    }
    else if (Option.Resolution == R800x600)
    {
        iret = v_scanline - 25;
        if (iret < 0)
            iret += 625;
        targ = T_INT;
    }
    else if (Option.Resolution == R640x480f252 || Option.Resolution == R640x480f378)
    {
        iret = v_scanline - 45;
        if (iret < 0)
            iret += 525;
        targ = T_INT;
    }
    else if (Option.Resolution == R848x480)
    {
        iret = v_scanline - 37;
        if (iret < 0)
            iret += 517;
        targ = T_INT;
    }
}
#else
void fun_getscanline(void)
{
    iret = QVgaScanLine;
    targ = T_INT;
}
#endif
#else
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
// Draw a filled rectangle
// this is the basic drawing primitive used by most drawing routines
//    x1, y1, x2, y2 - the coordinates
//    c - the colour
void MIPS16 DrawRectangleUser(int x1, int y1, int x2, int y2, int c)
{
    // Clamp coordinates (branchless where beneficial)
    x1 = (x1 < 0) ? 0 : (x1 >= HRes) ? HRes - 1
                                     : x1;
    x2 = (x2 < 0) ? 0 : (x2 >= HRes) ? HRes - 1
                                     : x2;
    y1 = (y1 < 0) ? 0 : (y1 >= VRes) ? VRes - 1
                                     : y1;
    y2 = (y2 < 0) ? 0 : (y2 >= VRes) ? VRes - 1
                                     : y2;

    // Swap if needed
    if (x2 < x1)
    {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    char callstr[256];
    unsigned char *nextstmtSaved = nextstmt;
    if (FindSubFun((unsigned char *)"MM.USER_RECTANGLE", 0) >= 0)
    {
        strcpy(callstr, "MM.USER_RECTANGLE");
        strcat(callstr, " ");
        IntToStr(callstr + strlen(callstr), x1, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), y1, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), x2, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), y2, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), c, 10);
        callstr[strlen(callstr) + 1] = 0; // two NULL chars required to terminate the call
        g_LocalIndex++;
#ifdef CACHE
        EnterLocalFrame();
#endif
        ExecuteProgram((unsigned char *)callstr);
        nextstmt = nextstmtSaved;
        g_LocalIndex--;
#ifdef CACHE
        LeaveLocalFrame();
#endif
        g_TempMemoryIsChanged = true; // signal that temporary memory should be checked
    }
    else
        error("MM.USER_RECTANGLE not defined");
}

// Print the bitmap of a char on the video output
//     x, y - the top left of the char
//     width, height - size of the char's bitmap
//     scale - how much to scale the bitmap
//       fc, bc - foreground and background colour
//     bitmap - pointer to the bitmap
void MIPS16 DrawBitmapUser(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    char callstr[256];
    unsigned char *nextstmtSaved = nextstmt;
    if (FindSubFun((unsigned char *)"MM.USER_BITMAP", 0) >= 0)
    {
        strcpy(callstr, "MM.USER_BITMAP");
        strcat(callstr, " ");
        IntToStr(callstr + strlen(callstr), x1, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), y1, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), width, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), height, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), scale, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), fc, 10);
        strcat(callstr, ",");
        IntToStr(callstr + strlen(callstr), bc, 10);
        strcat(callstr, ",&H");
        IntToStr(callstr + strlen(callstr), (unsigned int)bitmap, 16);
        callstr[strlen(callstr) + 1] = 0; // two NULL chars required to terminate the call
        g_LocalIndex++;
#ifdef CACHE
        EnterLocalFrame();
#endif
        ExecuteProgram((unsigned char *)callstr);
        g_LocalIndex--;
#ifdef CACHE
        LeaveLocalFrame();
#endif
        g_TempMemoryIsChanged = true; // signal that temporary memory should be checked
        nextstmt = nextstmtSaved;
    }
    else
        error("MM.USER_BITMAP not defined");
}
#endif

/****************************************************************************************************

 General purpose routines

****************************************************************************************************/

int GetFontWidth(int fnt)
{
    return FontTable[fnt >> 4][0] * (fnt & 0b1111);
}

int GetFontHeight(int fnt)
{
    return FontTable[fnt >> 4][1] * (fnt & 0b1111);
}

void SetFont(int fnt)
{
    if (FontTable[fnt >> 4] == NULL)
        error("Invalid font number #%", (fnt >> 4) + 1);
    gui_font_width = FontTable[fnt >> 4][0] * (fnt & 0b1111);
    gui_font_height = FontTable[fnt >> 4][1] * (fnt & 0b1111);
    if (Option.DISPLAY_CONSOLE)
    {
        Option.Height = (VRes - (VRes * OptionVResreserved / 100)) / gui_font_height;
        Option.Width = HRes / gui_font_width;
    }
    gui_font = fnt;
}

void MIPS16 ResetDisplay(void)
{
    SetFont(Option.DefaultFont);
    gui_fcolour = Option.DefaultFC;
    gui_bcolour = Option.DefaultBC;
    PromptFont = Option.DefaultFont;
    PromptFC = Option.DefaultFC;
    PromptBC = Option.DefaultBC;
#ifdef PICOMITEVGA
#ifdef rp2350
    if (Option.Resolution == R848x480)
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 848 : 424);
    else if (Option.Resolution == R720x400
#ifdef HDMICUTDOWN
             || Option.Resolution == R720x400x8
#endif
    )
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 720 : 360);
    else if (Option.Resolution == R800x600 || Option.Resolution == R800x480)
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 800 : 400);
    else
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 640 : 320);
#else
    if (Option.Resolution == R720x400)
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 720 : 360);
    else
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 640 : 320);
#endif
    if (Option.Resolution == R720x400
#ifdef HDMICUTDOWN
        || Option.Resolution == R720x400x8
#endif
    )
        VRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 400 : 200);
#ifdef rp2350
    else if (Option.Resolution == R800x600)
        VRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 600 : 300);
#endif
    else
        VRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 480 : 240);
#ifdef HDMI
    if (Option.Resolution == R1280x720)
    {
        HRes = (DISPLAY_TYPE == SCREENMODE1 ? 1280 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 320 : 640));
        VRes = (DISPLAY_TYPE == SCREENMODE1 ? 720 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 180 : 360));
    }
    else if (Option.Resolution == R1024x768)
    {
        HRes = (DISPLAY_TYPE == SCREENMODE1 ? 1024 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 256 : 512));
        VRes = (DISPLAY_TYPE == SCREENMODE1 ? 768 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 192 : 384));
    }
    else if (Option.Resolution == R800x600)
    {
        HRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 800 : 400);
        VRes = ((DISPLAY_TYPE == SCREENMODE1 || DISPLAY_TYPE == SCREENMODE3) ? 600 : 300);
    }
    else if (Option.Resolution == R1024x600)
    {
        HRes = (DISPLAY_TYPE == SCREENMODE1 ? 1024 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 256 : 512));
        VRes = (DISPLAY_TYPE == SCREENMODE1 ? 600 : ((DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE5) ? 150 : 300));
    }
#endif

    switch (DISPLAY_TYPE)
    {
    case SCREENMODE1:
        ScreenSize = MODE1SIZE;
        break;
    case SCREENMODE2:
        ScreenSize = MODE2SIZE;
        break;
    case SCREENMODE3:
        ScreenSize = MODE3SIZE;
        break;
    case SCREENMODE4:
        ScreenSize = MODE4SIZE;
        break;
    case SCREENMODE5:
        ScreenSize = MODE5SIZE;
        break;
    }
    if (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3)
    {
        DrawRectangle = DrawRectangle16;
        DrawBitmap = DrawBitmap16;
        ScrollLCD = ScrollLCD16;
        DrawBuffer = DrawBuffer16;
        ReadBuffer = ReadBuffer16;
        DrawBufferFast = DrawBuffer16Fast;
        ReadBufferFast = ReadBuffer16Fast;
        DrawPixel = DrawPixel16;
#ifdef HDMI
    }
    else if (DISPLAY_TYPE == SCREENMODE4)
    {
        DrawRectangle = DrawRectangle555;
        DrawBitmap = DrawBitmap555;
        ScrollLCD = ScrollLCD555;
        DrawBuffer = DrawBuffer555;
        ReadBuffer = ReadBuffer555;
        DrawBufferFast = DrawBuffer555Fast;
        ReadBufferFast = ReadBuffer555Fast;
        DrawPixel = DrawPixel555;
    }
    else if (DISPLAY_TYPE == SCREENMODE5)
    {
        DrawRectangle = DrawRectangle256;
        DrawBitmap = DrawBitmap256;
        ScrollLCD = ScrollLCD256;
        DrawBuffer = DrawBuffer256;
        ReadBuffer = ReadBuffer256;
        DrawBufferFast = DrawBuffer256Fast;
        ReadBufferFast = ReadBuffer256Fast;
        DrawPixel = DrawPixel256;
#endif
    }
    else
    {
        DrawRectangle = DrawRectangle2;
        DrawBitmap = DrawBitmap2;
        ScrollLCD = ScrollLCD2;
        DrawBuffer = DrawBuffer2;
        ReadBuffer = ReadBuffer2;
        DrawBufferFast = DrawBuffer2Fast;
        ReadBufferFast = ReadBuffer2Fast;
        DrawPixel = DrawPixel2;
        PromptFC = gui_fcolour = Option.DefaultFC;
        PromptBC = gui_bcolour = Option.DefaultBC;
    }
#ifdef HDMI
    settiles();
#else
#ifdef rp2350
    if (DISPLAY_TYPE == SCREENMODE1)
    {
        tilefcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3));
        tilebcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3) + (MODE1SIZE >> 1));
        for (int x = 0; x < X_TILE; x++)
        {
            for (int y = 0; y < Y_TILE; y++)
            {
                tilefcols[y * X_TILE + x] = RGB121pack(Option.DefaultFC);
                tilebcols[y * X_TILE + x] = RGB121pack(Option.DefaultBC);
            }
        }
    }
#else
    for (int x = 0; x < X_TILE; x++)
    {
        for (int y = 0; y < Y_TILE; y++)
        {
            tilefcols[y * X_TILE + x] = RGB121pack(Option.DefaultFC);
            tilebcols[y * X_TILE + x] = RGB121pack(Option.DefaultBC);
        }
    }
#endif
#endif
#ifdef GUICONTROLS
    /* Mouse-driven GUI on VGA/HDMI: initialise last_fcolour/last_bcolour
       and the other defaults so the first GUI control isn't drawn as
       black-on-black with zero dimensions. */
    ResetGUI();
#endif
#else
#ifdef GUICONTROLS
    ResetGUI();
#endif
#endif
}
// OPTIMIZED: Move lookup tables to file scope in RAM for fast access
__not_in_flash("data") static const uint32_t hline_mask_a[32] = {
    0xFFFFFFFF, 0x7FFFFFFF, 0x3FFFFFFF, 0x1FFFFFFF, 0xFFFFFFF, 0x7FFFFFF, 0x3FFFFFF, 0x1FFFFFF,
    0xFFFFFF, 0x7FFFFF, 0x3FFFFF, 0x1FFFFF, 0xFFFFF, 0x7FFFF, 0x3FFFF, 0x1FFFF,
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0xFFF, 0x7FF, 0x3FF, 0x1FF,
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01};

__not_in_flash("data") static const uint32_t hline_mask_b[32] = {
    0x80000000, 0xC0000000, 0xe0000000, 0xf0000000, 0xf8000000, 0xfc000000, 0xfe000000, 0xff000000,
    0xff800000, 0xffC00000, 0xffe00000, 0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000, 0xffff0000,
    0xffff8000, 0xffffC000, 0xffffe000, 0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00, 0xffffff00,
    0xffffff80, 0xffffffC0, 0xffffffe0, 0xfffffff0, 0xfffffff8, 0xfffffffc, 0xfffffffe, 0xffffffff};
// Helper function to apply hline mask to a single line buffer
// Helper function to apply hline mask to a single line buffer
void hline_to_buffer(int x0, int x1, int fill, int ints_per_line, uint32_t *restrict line_buf)
{
    if (x1 < x0)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 < 0)
        return;

    int max_x = (ints_per_line << 5) - 1;
    if (x0 > max_x)
        return;
    if (x1 > max_x)
        x1 = max_x;

    uint32_t w0 = (x0 >> 5); // Divide by 32
    uint32_t w1 = (x1 >> 5);
    uint32_t xx0 = x0 & 0x1F; // Modulo 32
    uint32_t xx1 = x1 & 0x1F;

    if (likely(w1 == w0))
    {
        // Special case - both endpoints in same word
        uint32_t mask = hline_mask_a[xx0] & hline_mask_b[xx1];

        if (fill)
            line_buf[w0] |= mask;
        else
            line_buf[w0] &= ~mask;
    }
    else
    {
        // Multiple words
        uint32_t word_count = w1 - w0;

        if (likely(word_count > 1))
        {
            // Fill full words in between
            uint32_t fill_val = fill ? 0xFFFFFFFF : 0;
            uint32_t *p = &line_buf[w0 + 1];
            uint32_t count = word_count - 1;

            // Unroll by 4 for better performance
            while (count >= 4)
            {
                p[0] = fill_val;
                p[1] = fill_val;
                p[2] = fill_val;
                p[3] = fill_val;
                p += 4;
                count -= 4;
            }

            // Handle remainder
            while (count--)
                *p++ = fill_val;
        }

        // Handle partial words at edges
        uint32_t mask0 = hline_mask_a[xx0];
        uint32_t mask1 = hline_mask_b[xx1];

        if (fill)
        {
            line_buf[w0] |= mask0;
            line_buf[w1] |= mask1;
        }
        else
        {
            line_buf[w0] &= ~mask0;
            line_buf[w1] &= ~mask1;
        }
    }
}

// Draw a horizontal line segment from the line buffer
void draw_line_from_buffer(uint32_t *line_buf, int ints_per_line, int x_base, int y_coord, int c)
{
    int xs = -1;
    int xi = 0;

    for (int i = 0; i < ints_per_line; i++)
    {
        uint32_t k = line_buf[i];
        int x_start = x_base + (i << 5);

        for (int m = 0; m < 32; m++)
        {
            if (xs == -1 && (k & 0x80000000))
            {
                xs = m;
                xi = i;
            }
            if (xs != -1 && !(k & 0x80000000))
            {
                DrawRectangle(x_base + xs + (xi << 5), y_coord,
                              x_start + m - 1, y_coord, c);
                xs = -1;
            }
            k <<= 1;
        }
    }

    if (xs != -1)
    {
        DrawRectangle(x_base + xs + (xi << 5), y_coord,
                      x_base - 1 + (ints_per_line << 5), y_coord, c);
    }
}

// Generate filled circle contributions for a specific scanline
// center_x is the x-coordinate of the circle center in buffer space
// center_y is the y-coordinate of the circle center in buffer space
void generate_circle_scanline(int radius, int center_x, int center_y, int scan_y,
                              int fill, int ints_per_line, uint32_t *line_buf, int asp)
{
    int a = 0;
    int b = radius;
    int P = 1 - radius;

    do
    {
        int A = (a * asp) >> 10;
        int B = (b * asp) >> 10;

        // Each Bresenham step generates 4 horizontal lines due to symmetry
        // Check which ones match our current scanline

        // Lines at y = center_y + b and y = center_y - b
        if (center_y + b == scan_y)
        {
            hline_to_buffer(center_x - A, center_x + A, fill, ints_per_line, line_buf);
        }
        if (center_y - b == scan_y)
        {
            hline_to_buffer(center_x - A, center_x + A, fill, ints_per_line, line_buf);
        }

        // Lines at y = center_y + a and y = center_y - a
        if (center_y + a == scan_y)
        {
            hline_to_buffer(center_x - B, center_x + B, fill, ints_per_line, line_buf);
        }
        if (center_y - a == scan_y)
        {
            hline_to_buffer(center_x - B, center_x + B, fill, ints_per_line, line_buf);
        }

        if (P < 0)
            P += 3 + 2 * a++;
        else
            P += 5 + 2 * (a++ - b--);

    } while (a <= b);
}

// OPTIMIZED: Process circles line-by-line with minimal memory
void DrawCircleRingLineByLine(int x, int y, int r1, int r2, int c, MMFLOAT aspect, MMFLOAT aspect2)
{
    // Calculate the actual width needed for the outer circle
    int ll = r2;
    if (aspect > 1.0f)
        ll = (int)((MMFLOAT)r2 * aspect);

    int ints_per_line = RoundUptoInt((ll << 1) + 1) / 32;

    // Allocate only ONE line buffer
    uint32_t *line_buf = (uint32_t *)GetTempMainMemory((ints_per_line + 1) << 2);

    // The outer circle center in buffer coordinates
    // x: stretched by aspect, so center is at (r2 * aspect)
    // y: not stretched, so center is at r2
    int center_x_outer = (int)((MMFLOAT)r2 * aspect);
    int center_y = r2;

    // The inner circle center in buffer coordinates
    // The inner circle has a different aspect ratio (aspect2)
    // but it's still centered at the same physical location
    int center_x_inner = (int)((MMFLOAT)r2 * aspect);

    // Base coordinates for final drawing (top-left of buffer maps to these screen coords)
    int x_base = x - center_x_outer;
    int y_base = y - r2;

    int asp_outer = aspect * (MMFLOAT)(1 << 10);
    int asp_inner = aspect2 * (MMFLOAT)(1 << 10);

    // Process each horizontal line
    for (int scan_y = 0; scan_y <= (r2 << 1); scan_y++)
    {
        int y_coord = y_base + scan_y;

        // Clear line buffer
        for (int i = 0; i <= ints_per_line; i++)
            line_buf[i] = 0;

        // Generate outer circle contributions for this scanline
        generate_circle_scanline(r2, center_x_outer, center_y, scan_y, 1, ints_per_line, line_buf, asp_outer);

        // Generate inner circle contributions (to subtract)
        generate_circle_scanline(r1, center_x_inner, center_y, scan_y, 0, ints_per_line, line_buf, asp_inner);

        // Draw this line
        draw_line_from_buffer(line_buf, ints_per_line, x_base, y_coord, c);
    }
}

/******************************************************************************************
 Print a char on the LCD display (SSD1963 and in landscape only).  It handles control chars
 such as newline and will wrap at the end of the line and scroll the display if necessary.

 The char is printed at the current location defined by CurrentX and CurrentY
 *****************************************************************************************/
void DisplayPutC(char c)
{

    if (!Option.DISPLAY_CONSOLE)
        return;
    // if it is printable and it is going to take us off the right hand end of the screen do a CRLF
    if (c >= FontTable[gui_font >> 4][2] && c < FontTable[gui_font >> 4][2] + FontTable[gui_font >> 4][3])
    {
        if (CurrentX + gui_font_width > HRes)
        {
            DisplayPutC('\r');
            DisplayPutC('\n');
        }
    }

    // handle the standard control chars
    switch (c)
    {
    case '\b':
        CurrentX -= gui_font_width;
        //            if (CurrentX < 0) CurrentX = 0;
        if (CurrentX < 0)
        {                                // Go to end of previous line
            CurrentY -= gui_font_height; // Go up one line
            if (CurrentY < 0)
                CurrentY = 0;
            CurrentX = (Option.Width - 1) * gui_font_width; // go to last character
        }
        return;
    case '\r':
        CurrentX = 0;
        return;
    case '\n':
        if (CurrentY + 2 * gui_font_height > (VRes - (VRes * OptionVResreserved / 100)))
        {
            if (Option.NoScroll && Option.DISPLAY_CONSOLE)
            {
                ClearScreen(gui_bcolour);
                CurrentX = 0;
                CurrentY = 0;
            }
            else
            {
                ScrollLCD(gui_font_height);
            }
        }
        else
        {
            CurrentY += gui_font_height;
        }
        return;
    case '\t':
        do
        {
            DisplayPutC(' ');
        } while ((CurrentX / gui_font_width) % Option.Tab);
        return;
    }
    GUIPrintChar(gui_font, gui_fcolour, gui_bcolour, c, ORIENT_NORMAL); // print it
    routinechecks();
}
void ShowCursor(int show)
{
    static int visible = false;
    int newstate;
    if (!Option.DISPLAY_CONSOLE)
        return;
    newstate = ((CursorTimer <= CURSOR_ON) && show); // what should be the state of the cursor?
    if (visible == newstate)
        return;         // we can skip the rest if the cursor is already in the correct state
    visible = newstate; // draw the cursor BELOW the font
    int y1 = CurrentY + gui_font_height - (gui_font_height <= 12 ? 1 : 2);
    int y2 = y1 + (gui_font_height <= 12 ? 0 : 1);
    int x1 = CurrentX;
    int x2 = CurrentX + gui_font_width - 1;
    DrawLine(x1, y1, x2, y1, (gui_font_height <= 12 ? 1 : 2), visible ? gui_fcolour : (DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour));
    if (y1 < low_y)
        low_y = y1;
    if (y2 > high_y)
        high_y = y2;
    if (x1 < low_x)
        low_x = x1;
    if (x2 > high_x)
        high_x = x2;
    routinechecks();
}

