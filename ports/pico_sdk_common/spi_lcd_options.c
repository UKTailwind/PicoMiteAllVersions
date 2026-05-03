/*
 * ports/pico_sdk_common/spi_lcd_options.c — SPI-LCD-only display
 * helpers (ConfigDisplayUser, clear320) and the OPTION LCD320 setter.
 * Compiled on every device build but the bodies are `#ifndef
 * PICOMITEVGA`-gated — VGA / HDMI ports get empty stubs at link time.
 *
 * Lifted out of MM_Misc.c so the core file's port_lcd320_setter() and
 * MMBasic.c's clear320() callers stay preprocessor-clean.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#if !HAL_PORT_IS_VGA

void MIPS16 ConfigDisplayUser(unsigned char *tp)
{
    getargs(&tp, 13, (unsigned char *)",");
    if (str_equal(argv[0], (unsigned char *)"USER")) {
        if (Option.DISPLAY_TYPE) error("Display already configured");
        if (argc != 5) error("Argument count");
        HRes = DisplayHRes = getint(argv[2], 1, 10000);
        VRes = DisplayVRes = getint(argv[4], 1, 10000);
        Option.DISPLAY_TYPE = DISP_USER;
        DrawRectangle = DrawRectangleUser;
        DrawBitmap = DrawBitmapUser;
        return;
    }
}

void MIPS16 clear320(void)
{
    if (SPI480) {
#ifdef PICOCALC
        HRes = 320;
        VRes = 480;
#else
        if (Option.DISPLAY_ORIENTATION & 1) {
            HRes = DisplayHRes;
            VRes = DisplayVRes;
        } else {
            HRes = DisplayVRes;
            VRes = DisplayHRes;
        }
#endif
        return;
    }
    screen320 = 0;
    DrawRectangle = DrawRectangleSSD1963;
    DrawBitmap = DrawBitmapSSD1963;
    DrawBuffer = DrawBufferSSD1963;
    ReadBuffer = ReadBufferSSD1963;
    if (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16) {
        DrawBLITBuffer = DrawBLITBufferSSD1963;
        ReadBLITBuffer = ReadBLITBufferSSD1963;
    } else {
        DrawBLITBuffer = DrawBufferSSD1963;
        ReadBLITBuffer = ReadBufferSSD1963;
    }
    if (Option.DISPLAY_TYPE != SSD1963_4_16) {
        if (Option.DISPLAY_ORIENTATION & 1) { HRes = 800; VRes = 480; }
        else                                { HRes = 480; VRes = 800; }
    } else {
        if (Option.DISPLAY_ORIENTATION & 1) { HRes = 480; VRes = 272; }
        else                                { HRes = 272; VRes = 480; }
    }
    FreeMemorySafe((void **)&buff320);
}

int port_lcd320_option_setter(unsigned char *cmdline)
{
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"LCD320");
    if (!tp) return 0;
    if (!(SSD16TYPE && (Option.DISPLAY_ORIENTATION == LANDSCAPE || Option.DISPLAY_ORIENTATION == RLANDSCAPE)))
        error("Only available on 16-bit SSD1963 and IPS_4_16 displays in Landscape");
    if (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16) {
        if (checkstring(tp, (unsigned char *)"OFF")) {
            clear320();
        } else if (checkstring(tp, (unsigned char *)"ON")) {
            screen320 = 1;
            DrawRectangle = DrawRectangle320;
            DrawBitmap = DrawBitmap320;
            DrawBuffer = DrawBuffer320;
            ReadBuffer = ReadBuffer320;
            DrawBLITBuffer = DrawBLITBuffer320;
            ReadBLITBuffer = ReadBLITBuffer320;
            HRes = 320;
            VRes = 240;
            buff320 = GetMemory(320 * 6);
            return 1;
        } else error("Syntax");
    } else error("Invalid display type");
    return 1;
}

#else  /* PICOMITEVGA — stubs */

void MIPS16 ConfigDisplayUser(unsigned char *tp) { (void)tp; }
void MIPS16 clear320(void) {}
int  port_lcd320_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

#endif
