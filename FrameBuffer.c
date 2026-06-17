/***********************************************************************************************************************
PicoMite MMBasic

FrameBuffer.c

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
 * @file FrameBuffer.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the FRAMEBUFFER MMBasic command: framebuffer/layer
 *        creation and switching, the frame-to-LCD mirroring path and the
 *        core1 merge support. Both implementations live here — the
 *        LCD-panel builds (!PICOMITEVGA) and the VGA/HDMI builds — split
 *        out of Draw.c.
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "DrawInternal.h"
#ifndef PICOMITEWEB
#include "pico/multicore.h"
extern mutex_t frameBufferMutex;
#endif
#if PICOMITERP2350
#include "VGA222.h"
#endif
#ifndef PICOMITEVGA
extern int map[16];
#else
extern volatile int QVgaScanLine;
#endif /*                                                                \
        * @cond                                                          \
        * The following section will be excluded from the documentation. \
        */
#ifndef PICOMITEVGA
void restorepanel(void)
{
#ifdef GUICONTROLS
    /* Will rebind drawing primitives and (often) clear WriteBuf. Drop
       the cursor's save-buffer reference first so a later restore
       doesn't leak stale pixels onto the panel. */
    CursorHide();
#endif
    if (IS_VIRTUAL_DISPLAY(Option.DISPLAY_TYPE))
    {
        InitDisplayVirtual();
        return;
    }
    if (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel)
    {
        if (Option.DISPLAY_ORIENTATION == PORTRAIT)
        {
            DrawRectangle = DrawRectangleSPISCR;
            DrawBitmap = DrawBitmapSPISCR;
            DrawBuffer = DrawBufferSPISCR;
            DrawPixel = DrawPixelNormal;
            ScrollLCD = ScrollLCDSPISCR;
            DrawBLITBuffer = DrawBufferSPISCR;
            if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ST7789B || Option.DISPLAY_TYPE == ST7365P)
            {
                ReadBuffer = ReadBufferSPISCR;
                ReadBLITBuffer = ReadBufferSPISCR;
            }
        }
        else
        {
            DrawRectangle = DrawRectangleSPI;
            DrawBitmap = DrawBitmapSPI;
            DrawBuffer = DrawBufferSPI;
            DrawBLITBuffer = DrawBufferSPI;
            DrawPixel = DrawPixelNormal;
            if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ST7789B || Option.DISPLAY_TYPE == ST7365P)
            {
                ReadBLITBuffer = ReadBufferSPI;
                ReadBuffer = ReadBufferSPI;
                ScrollLCD = ScrollLCDSPI;
            }
        }
    }
    else if (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL)
    {
        if (screen320)
        {
            DrawRectangle = DrawRectangle320;
            DrawBitmap = DrawBitmap320;
            DrawBuffer = DrawBuffer320;
            ReadBuffer = ReadBuffer320;
        }
        else
        {
            DrawRectangle = DrawRectangleSSD1963;
            DrawBitmap = DrawBitmapSSD1963;
            DrawBuffer = DrawBufferSSD1963;
            ReadBuffer = ReadBufferSSD1963;
            if (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16)
            {
                DrawBLITBuffer = DrawBLITBufferSSD1963;
                ReadBLITBuffer = ReadBLITBufferSSD1963;
            }
            else
            {
                DrawBLITBuffer = DrawBufferSSD1963;
                ReadBLITBuffer = ReadBufferSSD1963;
            }
        }
        DrawPixel = DrawPixelNormal;
        if (!(Option.DISPLAY_TYPE == ILI9341_8 || Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == IPS_4_16))
            ScrollLCD = ScrollSSD1963;
        else
            ScrollLCD = ScrollLCDSPI;
#if PICOMITERP2350
    }
    else if (Option.DISPLAY_TYPE >= VGA222 && Option.DISPLAY_TYPE < NEXTGEN)
    {
        DrawRectangle = DrawRectangle222;
        DrawBitmap = DrawBitmap222;
        DrawBuffer = DrawBuffer222;
        ReadBuffer = ReadBuffer222;
        DrawBLITBuffer = DrawBuffer222;
        ReadBLITBuffer = ReadBuffer222;
        ScrollLCD = ScrollLCD222;
        DrawPixel = DrawPixel222;
    }
    else if (Option.DISPLAY_TYPE > NEXTGEN)
    {
        DrawRectangle = DrawRectangleMEM332;
        DrawBitmap = DrawBitmapMEM332;
        DrawBuffer = DrawBufferMEM332;
        ReadBuffer = ReadBufferMEM332;
        DrawBLITBuffer = DrawBlitBufferMEM332;
        ReadBLITBuffer = ReadBlitBufferMEM332;
        ScrollLCD = ScrollLCDMEM332;
        DrawPixel = DrawPixelMEM332;
#endif
    }
    WriteBuf = NULL;
}
void setframebuffer(void)
{
#if PICOMITERP2350
    if (!((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL) || Option.DISPLAY_TYPE >= VGA222))
        return;
#else
    if (!((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL)))
        return;
#endif
    DrawRectangle = DrawRectangle16;
    DrawBitmap = DrawBitmap16;
    ScrollLCD = ScrollLCD16;
    DrawBuffer = DrawBuffer16;
    ReadBLITBuffer = ReadBuffer16;
    DrawBLITBuffer = DrawBuffer16;
    ReadBuffer = ReadBuffer16;
    DrawBufferFast = DrawBuffer16Fast;
    ReadBufferFast = ReadBuffer16Fast;
    DrawPixel = DrawPixel16;
}
void closeframebuffer(char layer)
{
#ifdef GUICONTROLS
    /* About to flip WriteBuf and possibly free framebuffer memory.
       Restore cursor pixels to the current WriteBuf first. */
    CursorHide();
#endif
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
    if (FrameBuf)
        FreeMemory(FrameBuf);
    if (LayerBuf)
        FreeMemory(LayerBuf);
    if (FrameBuf || LayerBuf)
        restorepanel();
    FrameBuf = NULL;
    WriteBuf = NULL;
}
#include <stdint.h>
#include <string.h>

#include <stdint.h>

#include <stdint.h>

#include <stdint.h>

// External globals
extern uint8_t *ScreenBuffer; // RGB222 framebuffer
extern short HRes, VRes;      // screen resolution

// 4-bit RGB121 → 6-bit RGB222 lookup table
static uint8_t rgb121_to_rgb222_lut[16];
static int lut_initialized = 0;

static inline uint32_t pack5(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 6) |
           ((uint32_t)p[2] << 12) |
           ((uint32_t)p[3] << 18) |
           ((uint32_t)p[4] << 24);
}

// Read-modify-write helper for arbitrary X
static inline void store_pixel_rmw(uint32_t *dst_line, int x, uint8_t pix6)
{
    int word = x / 5;
    int bit = (x % 5) * 6;
    uint32_t mask = (uint32_t)0x3F << bit;
    uint32_t val = ((uint32_t)pix6 << bit);
    dst_line[word] = (dst_line[word] & ~mask) | (val & mask);
}

void copy_rgb121_to_rgb222(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd)
{
    // Initialize lookup table once
    if (!lut_initialized)
    {
        for (int i = 0; i < 16; ++i)
        {
            uint32_t r = ((i >> 3) & 0x1) * 3; // 1-bit → 2-bit
            uint32_t g = ((i >> 1) & 0x3);     // 2-bit unchanged
            uint32_t b = ((i >> 0) & 0x1) * 3; // 1-bit → 2-bit
            rgb121_to_rgb222_lut[i] = (uint8_t)((r << 4) | (g << 2) | b);
        }
        lut_initialized = 1;
    }

    // Clamp bounds
    if (xstart < 0)
        xstart = 0;
    if (ystart < 0)
        ystart = 0;
    if (xend >= (int)HRes)
        xend = HRes - 1;
    if (yend >= (int)VRes)
        yend = VRes - 1;
    if (xstart > xend || ystart > yend)
        return;

    // --- FAST PATH: full-frame copy, aligned, odd==0 ---
    if (xstart == 0 && ystart == 0 && xend == HRes - 1 && yend == VRes - 1 && odd == 0)
    {
        uint32_t *src = (uint32_t *)s;
        uint32_t *dst = (uint32_t *)(void *)ScreenBuffer;

        int total_pixels = (int)HRes * (int)VRes;
        int blocks = total_pixels / 40;

        for (int blk = 0; blk < blocks; blk++)
        {
            uint32_t sv[5];
            for (int i = 0; i < 5; i++)
                sv[i] = *src++;

            uint8_t p[40];
            int k = 0;
            for (int i = 0; i < 5; i++)
            {
                uint32_t v = sv[i];
                for (int n = 0; n < 8; n++, v >>= 4)
                    p[k++] = rgb121_to_rgb222_lut[v & 0xF];
            }

            for (int i = 0; i < 8; i++)
                *dst++ = pack5(&p[i * 5]);
        }

        // Handle leftovers if total pixels not multiple of 40
        int leftover = total_pixels % 40;
        if (leftover)
        {
            uint8_t *srcb = s + (blocks * 20); // each 40 pixels = 20 bytes (40 nibbles)
            int nibstate = 0;
            for (int i = 0; i < leftover; i++)
            {
                uint8_t nib;
                if (nibstate == 0)
                {
                    nib = (*srcb & 0x0F);
                    nibstate = 1;
                }
                else
                {
                    nib = (*srcb >> 4);
                    srcb++;
                    nibstate = 0;
                }
                uint8_t pix6 = rgb121_to_rgb222_lut[nib];
                int pix_index = (blocks * 40) + i;
                int y = pix_index / HRes;
                int x = pix_index % HRes;
                uint32_t *dst_line = (uint32_t *)(void *)(ScreenBuffer + (y * ((HRes + 4) / 5) * 4));
                store_pixel_rmw(dst_line, x, pix6);
            }
        }
        return;
    }

    // --- GENERAL PATH: arbitrary subrectangle and/or odd nibble start ---
    uint8_t *src = s;
    int dst_words_per_line = ((int)HRes + 4) / 5;

    for (int y = ystart; y <= yend; ++y)
    {
        uint32_t *dst_line = (uint32_t *)(void *)(ScreenBuffer + (y * dst_words_per_line * 4));
        int x = xstart;
        while (x <= xend)
        {
            uint8_t pix6buf[5];
            int cnt = 0;

            for (; cnt < 5 && x <= xend; ++cnt, ++x)
            {
                uint8_t nib;
                if (odd == 0)
                {
                    nib = (*src) & 0x0F; // low nibble
                    odd = 1;             // next read high nibble
                }
                else
                {
                    nib = ((*src) >> 4) & 0x0F; // high nibble
                    src++;
                    odd = 0; // next read low nibble
                }
                pix6buf[cnt] = rgb121_to_rgb222_lut[nib];
            }

            if (cnt == 5)
            {
                int word_index = (x - 5) / 5;
                dst_line[word_index] = pack5(pix6buf);
            }
            else
            {
                int startx = x - cnt;
                for (int i = 0; i < cnt; ++i)
                    store_pixel_rmw(dst_line, startx + i, pix6buf[i]);
            }
        }
    }
}
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd)
{
#ifndef PICOMITEWEB
#if PICOMITERP2350
    if (Option.DISPLAY_TYPE >= VGA222 && Option.DISPLAY_TYPE < NEXTGEN)
    {
        copy_rgb121_to_rgb222(s, xstart, xend, ystart, yend, odd);
        return;
    }
#endif
#endif
    low_x = xstart;
    low_y = ystart;
    high_x = xend;
    high_y = yend;
    unsigned char col[3] = {0};
    uint64_t c;
    if (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel)
        DefineRegionSPI(xstart, ystart, xend, yend, 1);
    else if (Option.DISPLAY_TYPE == ILI9341_8)
    {
        SetAreaILI9341(xstart, ystart, xend, yend, 1);
    }
    else if (Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16)
    {
        if (Option.DISPLAY_TYPE == ILI9486_16)
        {
            Write16bitCommand(ILI9341_PIXELFORMAT);
            WriteData16bit(0x55);
        }
        SetAreaILI9341(xstart, ystart, xend, yend, 1);
    }
    else if (Option.DISPLAY_TYPE == IPS_4_16)
    {
        if (LCDAttrib == 1)
            WriteCmdDataIPS_4_16(0x3A00, 1, 0x55);
        if (screen320)
        {
            SetAreaIPS_4_16(xstart + 80, ystart * 2, xend * 2 - xstart + 81, yend * 2 + 1, 1); // setup the area to be filled
        }
        else
        {
            SetAreaIPS_4_16(xstart, ystart, xend, yend, 1); // setup the area to be filled
        }
#if PICOMITERP2350
    }
    else if (Option.DISPLAY_TYPE >= VGA222)
    {
        // nothing to do
#endif
    }
    else
    {
        if (screen320)
        {
            if (Option.DISPLAY_TYPE != SSD1963_4_16)
                SetAreaSSD1963(xstart + 80, ystart * 2, xend * 2 - xstart + 81, yend * 2 + 1); // setup the area to be filled
            else
                SetAreaSSD1963(xstart + 80, ystart + 16, xend + 80, yend + 16);
        }
        else
        {
            SetAreaSSD1963(xstart, ystart, xend, yend); // setup the area to be filled
        }
        WriteComand(CMD_WR_MEMSTART);
    }
    int i;
    int cnt = 2;
    if (Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ILI9481IPS || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE <= SSD_PANEL_8))
    {
        cnt = 3;
    }
    if (map[15] == 0)
    {
        for (i = 0; i < 16; i++)
        {
            if (Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ILI9481IPS)
            {
                col[0] = (RGB121map[i] >> 16);
                col[1] = (RGB121map[i] >> 8) & 0xFF;
                col[2] = (RGB121map[i] & 0xFF);
            }
            else if (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE <= SSD_PANEL_8)
            {
                col[2] = (RGB121map[i] >> 16);
                col[1] = (RGB121map[i] >> 8) & 0xFF;
                col[0] = (RGB121map[i] & 0xFF);
#if PICOMITERP2350
            }
            else if (Option.DISPLAY_TYPE > SSD_PANEL_8 && Option.DISPLAY_TYPE < NEXTGEN)
            {
#else
            }
            else if (Option.DISPLAY_TYPE > SSD_PANEL_8)
            {
#endif
                map[i] = ((RGB121map[i] >> 8) & 0xf800) | ((RGB121map[i] >> 5) & 0x07e0) | ((RGB121map[i] >> 3) & 0x001f);
                continue;
#if PICOMITERP2350
            }
            else if (Option.DISPLAY_TYPE >= NEXTGEN)
            {
                map[i] = RGB332(RGB121map[i]);
                continue;
#endif
            }
            else
            {
                col[0] = ((RGB121map[i] >> 16) & 0b11111000) | ((RGB121map[i] >> 13) & 0b00000111);
                col[1] = ((RGB121map[i] >> 5) & 0b11100000) | ((RGB121map[i] >> 3) & 0b00011111);
            }
            if (Option.DISPLAY_TYPE == GC9A01)
            {
                col[0] = ~col[0];
                col[1] = ~col[1];
            }
            map[i] = col[0] | (col[1] << 8) | (col[2] << 16);
        }
    }
    i = (xend - xstart + 1) * (yend - ystart + 1);
    if (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel)
    {
#if PICOMITERP2350
        if (PinDef[Option.LCD_CLK].mode & SPI0SCK)
        {
#else
        if (PinDef[Option.SYSTEM_CLK].mode & SPI0SCK)
        {
#endif
            if (odd)
            {
                c = map[(*s & 0xF0) >> 4];
                spi_write_fast(spi0, (uint8_t *)&c, cnt);
                s++;
                i--;
            }
            while (i > 0)
            {
                c = map[*s & 0xF];
                spi_write_fast(spi0, (uint8_t *)&c, cnt);
                if (i > 1)
                {
                    c = map[(*s & 0xF0) >> 4];
                    spi_write_fast(spi0, (uint8_t *)&c, cnt);
                }
                s++;
                i -= 2;
            }
        }
        else
        {
            if (odd)
            {
                c = map[(*s & 0xF0) >> 4];
                spi_write_fast(spi1, (uint8_t *)&c, cnt);
                s++;
                i--;
            }
            while (i > 0)
            {
                c = map[*s & 0xF];
                spi_write_fast(spi1, (uint8_t *)&c, cnt);
                if (i > 1)
                {
                    c = map[(*s & 0xF0) >> 4];
                    spi_write_fast(spi1, (uint8_t *)&c, cnt);
                }
                s++;
                i -= 2;
            }
        }
#if PICOMITERP2350
        if (PinDef[Option.LCD_CLK].mode & SPI0SCK)
            spi_finish(spi0);
#else
        if (PinDef[Option.SYSTEM_CLK].mode & SPI0SCK)
            spi_finish(spi0);
#endif
        else
            spi_finish(spi1);
        ClearCS(Option.LCD_CS); // set CS high
#if PICOMITERP2350
    }
    else if (Option.DISPLAY_TYPE >= VGA222 && Option.DISPLAY_TYPE < NEXTGEN)
    {
    }
    else if (Option.DISPLAY_TYPE >= NEXTGEN)
    {
        unsigned char *screen = (unsigned char *)(ScreenBuffer);
        for (int y = ystart; y <= yend; y++)
        {
            unsigned char *p = screen + (y + ScrollStart < VRes ? y + ScrollStart : y + ScrollStart - VRes) * HRes;
            for (int x = xstart; x <= xend; x++)
            {
                if (odd)
                {
                    c = map[(*s & 0xF0) >> 4];
                    s++;
                    odd ^= 1;
                }
                else
                {
                    c = map[*s & 0xF];
                    odd ^= 1;
                }
                p[x] = c;
            }
        }
#endif
    }
    else
    {
        if (screen320 && Option.DISPLAY_TYPE != SSD1963_4_16)
        {
            unsigned char *q = buff320;
            HRes = 720;
            VRes = 480;
            uint16_t *pp = (uint16_t *)q;
            if (odd)
            { // only used for a single line
                if (odd)
                {
                    c = map[(*s & 0xF0) >> 4];
                    *pp++ = c;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    s++;
                    i--;
                    int x = 1;
                    while (x <= xend - xstart)
                    {
                        c = map[*s & 0xF];
                        *pp++ = c;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        if (i > 1)
                        {
                            c = map[(*s & 0xF0) >> 4];
                            *pp++ = c;
                            gpio_put(SSD1963_WR_GPPIN, 0);
                            gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 1);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 0);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 1);
                        }
                        s++;
                        i -= 2;
                        x += 2;
                    }
                    pp = (uint16_t *)q;
                    for (int x = xstart; x <= xend; x++)
                    {
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)(*pp++) << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                    }
                }
            }
            else
            {
                for (int y = ystart; y <= yend; y++)
                {
                    pp = (uint16_t *)q;
                    int x = 0;
                    while (x <= xend - xstart)
                    {
                        c = map[*s & 0xF];
                        *pp++ = c;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        if (i > 1)
                        {
                            c = map[(*s & 0xF0) >> 4];
                            *pp++ = c;
                            gpio_put(SSD1963_WR_GPPIN, 0);
                            gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 1);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 0);
                            nop;
                            gpio_put(SSD1963_WR_GPPIN, 1);
                        }
                        s++;
                        i -= 2;
                        x += 2;
                    }
                    pp = (uint16_t *)q;
                    for (int x = xstart; x <= xend; x++)
                    {
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)(*pp++) << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                    }
                }
            }
            HRes = 320;
            VRes = 240;
        }
        else
        {
            if (Option.DISPLAY_TYPE > SSD_PANEL_8)
            {
                if (odd)
                {
                    c = map[(*s & 0xF0) >> 4];
                    gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    s++;
                    i--;
                }
                while (i > 0)
                {
                    c = map[*s & 0xF];
                    gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    if (i > 1)
                    {
                        c = map[(*s & 0xF0) >> 4];
                        gpio_put_masked64((uint64_t)0xFFFF << SSD1963data, (uint64_t)c << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                    }
                    s++;
                    i -= 2;
                }
            }
            else
            {
                if (odd)
                {
                    c = map[(*s & 0xF0) >> 4];
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 16) & 0xff) << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 8) & 0xff) << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    nop;
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)(c & 0xff) << SSD1963data);
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    s++;
                    i--;
                }
                while (i > 0)
                {
                    c = map[*s & 0xF];
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 16) & 0xff) << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 8) & 0xff) << SSD1963data);
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    nop;
                    gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)(c & 0xff) << SSD1963data);
                    gpio_put(SSD1963_WR_GPPIN, 0);
                    nop;
                    nop;
                    gpio_put(SSD1963_WR_GPPIN, 1);
                    if (i > 1)
                    {
                        c = map[(*s & 0xF0) >> 4];
                        gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 16) & 0xff) << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)((c >> 8) & 0xff) << SSD1963data);
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                        nop;
                        gpio_put_masked64((uint64_t)0b11111111 << SSD1963data, (uint64_t)(c & 0xff) << SSD1963data);
                        gpio_put(SSD1963_WR_GPPIN, 0);
                        nop;
                        nop;
                        gpio_put(SSD1963_WR_GPPIN, 1);
                    }
                    s++;
                    i -= 2;
                }
            }
        }
    }
}
// Batch size for scanline accumulation (tune based on available RAM)
#define MERGE_BATCH_LINES 8

// Core merge logic - optimized with reduced branching
static inline void merge_scanline(uint8_t *dst, const uint8_t *src, int width, uint8_t colour)
{
    uint8_t highcolour = colour << 4;

    for (int x = 0; x < width; x++)
    {
        uint8_t src_pixel = src[x];
        uint8_t top = src_pixel & 0xF0;
        uint8_t bottom = src_pixel & 0x0F;

        // Skip if both pixels are transparent
        if (top == highcolour && bottom == colour)
            continue;

        // Branchless merge: use source pixel if not transparent, else keep destination
        uint8_t new_top = (top != highcolour) ? top : (dst[x] & 0xF0);
        uint8_t new_bottom = (bottom != colour) ? bottom : (dst[x] & 0x0F);
        dst[x] = new_top | new_bottom;
    }
}

void merge(uint8_t colour)
{
    if (LayerBuf == NULL || FrameBuf == NULL)
        return;
    uint8_t *s = LayerBuf;
    uint8_t *d = FrameBuf;
    int bytes_per_line = HRes / 2;

    // Allocate batch buffer for multiple scanlines
    uint8_t BatchBuf[MERGE_BATCH_LINES * (HRes / 2)];

#if defined(PICOMITE) || defined(PICOMITEMIN)
    mutex_enter_blocking(&frameBufferMutex);
#endif

    if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP ||
        Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B ||
        Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P)
    {
        while (GetLineILI9341() != 0)
        {
        }
    }

    // Process in batches
    for (int y = 0; y < VRes; y += MERGE_BATCH_LINES)
    {
        int batch_size = (y + MERGE_BATCH_LINES > VRes) ? (VRes - y) : MERGE_BATCH_LINES;

        // Process batch of scanlines
        for (int i = 0; i < batch_size; i++)
        {
            int curr_y = y + i;
            uint8_t *dst_line = BatchBuf + i * bytes_per_line;
            uint8_t *src_line = s + curr_y * bytes_per_line;
            uint8_t *frame_line = d + curr_y * bytes_per_line;

            // Copy framebuffer line to batch buffer
            memcpy(dst_line, frame_line, bytes_per_line);

            // Merge layer into batch buffer
            merge_scanline(dst_line, src_line, bytes_per_line, colour);
        }

        // Send entire batch to screen in one call
        int y_start = y;
        int y_end = y + batch_size - 1;
        copyframetoscreen(BatchBuf, 0, HRes - 1, y_start, y_end, 0);
    }

#if defined(PICOMITE) || defined(PICOMITEMIN)
    mutex_exit(&frameBufferMutex);
    mergedone = true;
    __dmb();
    low_x = 0;
    low_y = 0;
    high_x = HRes - 1;
    high_y = VRes - 1;
#endif
}

void blitmerge(int x0, int y0, int w, int h, uint8_t colour)
{
    if (LayerBuf == NULL || FrameBuf == NULL)
        return;

    uint8_t *s = LayerBuf;
    uint8_t *d = FrameBuf;
    int bytes_per_line = HRes / 2;
    int x0_bytes = x0 / 2;
    int w_bytes = w / 2;

    // Allocate batch buffer for multiple scanlines
    uint8_t BatchBuf[MERGE_BATCH_LINES * (HRes / 2)];

#ifdef PICOMITE
    mutex_enter_blocking(&frameBufferMutex);
#endif

    if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP ||
        Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B ||
        Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P)
    {
        while (GetLineILI9341() != 0)
        {
        }
    }

    int y_end = (y0 + h > VRes) ? VRes : (y0 + h);

    // Process in batches
    for (int y = y0; y < y_end; y += MERGE_BATCH_LINES)
    {
        int batch_size = (y + MERGE_BATCH_LINES > y_end) ? (y_end - y) : MERGE_BATCH_LINES;

        // Process batch of scanlines
        for (int i = 0; i < batch_size; i++)
        {
            int curr_y = y + i;
            uint8_t *dst_line = BatchBuf + i * bytes_per_line;
            uint8_t *src_line = s + curr_y * bytes_per_line;
            uint8_t *frame_line = d + curr_y * bytes_per_line;

            // Copy entire framebuffer line to batch buffer
            memcpy(dst_line, frame_line, bytes_per_line);

            // Merge only the specified region
            merge_scanline(dst_line + x0_bytes, src_line + x0_bytes, w_bytes, colour);
        }

        // Send entire batch to screen in one call
        int y_start = y;
        int y_batch_end = y + batch_size - 1;
        copyframetoscreen(&BatchBuf[x0_bytes], x0, x0 + w - 1, y_start, y_batch_end, 0);
    }

#ifdef PICOMITE
    mutex_exit(&frameBufferMutex);
    mergedone = true;
    __dmb();
#endif
}
/*  @endcond */
void cmd_framebuffer(void)
{
    unsigned char *p = NULL;
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE")))
    {
        if (FrameBuf == NULL)
        {
            FrameBuf = GetMemory(HRes * VRes / 2);
        }
        else
            error("Framebuffer already exists");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
    {
#ifdef GUICONTROLS
        /* Hide the cursor before WriteBuf flips. If the cursor's save
           buffer references the old buffer, restoring later would
           leak old pixels onto the new buffer. */
        CursorHide();
#endif
        if (checkstring(p, (unsigned char *)"N"))
        {
#ifdef PICOMITE
            if (mergerunning)
                error("Display in use for merged operation");
#endif
            restorepanel();
            return;
        }
        else if (checkstring(p, (unsigned char *)"L"))
        {
            if (!LayerBuf)
                error("Layer buffer not created");
            WriteBuf = LayerBuf;
            setframebuffer();
            return;
        }
        else if (checkstring(p, (unsigned char *)"F"))
        {
            if (!FrameBuf)
                StandardError(38);
            WriteBuf = FrameBuf;
            setframebuffer();
            return;
        }
        {
            getcsargs(&p, 1);
            if (argc != 1)
                SyntaxError();
            ;
            char *q = (char *)getCstring(argv[0]);
            if (strcasecmp(q, "N") == 0)
            {
#ifdef PICOMITE
                if (mergerunning)
                    error("Display in use for merged operation");
#endif
                restorepanel();
            }
            else if (strcasecmp(q, "L") == 0)
            {
                if (!LayerBuf)
                    error("Layer buffer not created");
                WriteBuf = LayerBuf;
                setframebuffer();
            }
            else if (strcasecmp(q, "F") == 0)
            {
                if (!FrameBuf)
                    StandardError(38);
                WriteBuf = FrameBuf;
                setframebuffer();
            }
            else
                SyntaxError();
            ;
        }
#ifndef PICOMITEVGA
#ifdef PICOMITE
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SYNC")))
    { // merge the layer onto the physical display
        uint64_t time = time_us_64() + 100000;
        mergedone = false;
        while (mergedone == false && time_us_64() < time)
        {
            CheckAbort();
        }
#endif
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MERGE")))
    { // merge the layer onto the physical display
        if (!LayerBuf)
            error("Layer not created");
        if (!FrameBuf)
            error("Framebuffer not created");
        uint8_t colour = 0;
        getcsargs(&p, 5);
        if (argc >= 1 && *argv[0])
        {
            colour = getint(argv[0], 0, 15);
        }
#ifdef PICOMITE
        uint8_t background = 0;
        if (argc >= 3 && *argv[2])
        {
            if (checkstring(argv[2], (unsigned char *)"B"))
                background = 1;
            else if (checkstring(argv[2], (unsigned char *)"R"))
                background = 2;
            else if (checkstring(argv[2], (unsigned char *)"A"))
                background = 3;
            else
                SyntaxError();
            ;
        }
#if defined(rp2350)
        if (Option.DISPLAY_TYPE >= VGA222 && Option.DISPLAY_TYPE < NEXTGEN)
            background = 0;
        if (background == 1)
        {
            if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || Option.DISPLAY_TYPE >= VGA222 || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                StandardError(1);
            ;
#else
        if (background == 1)
        {
            if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                StandardError(1);
            ;
#endif
            if (diskchecktimer < 200 && SPIatRisk)
                diskchecktimer = 200;
            multicore_fifo_push_blocking(2);
            multicore_fifo_push_blocking((uint32_t)colour);
        }
        else if (background == 2)
        {
            mergetimer = 0;
            if (argc == 5)
                mergetimer = getint(argv[4], 0, 60 * 10 * 1000);
            if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                StandardError(1);
            ;
            if (WriteBuf == NULL)
                WriteBuf = FrameBuf;
            setframebuffer();
            multicore_fifo_push_blocking(3);
            multicore_fifo_push_blocking((uint32_t)colour);
            multicore_fifo_push_blocking((uint32_t)mergetimer * 1000);
        }
        else if (background == 3)
        {
            if (mergerunning)
            {
                multicore_fifo_push_blocking(0xFF);
                busy_wait_ms(mergetimer + 200);
                if (mergerunning)
                {
                    SoftReset(SOFT_RESET);
                }
            }
        }
        else
#endif
            merge(colour);

#endif
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LAYER")))
    {
        if (LayerBuf == NULL)
        {
            LayerBuf = GetMemory(HRes * VRes / 2);
        }
        else
            error("Layer already exists");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WAIT")))
    {
        if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B || Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P)
        {
            while (GetLineILI9341() != 0)
            {
            }
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        if (checkstring(p, (unsigned char *)"F"))
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
            if (WriteBuf != LayerBuf)
                restorepanel();
            if (FrameBuf)
                FreeMemory(FrameBuf);
            FrameBuf = NULL;
        }
        else if (checkstring(p, (unsigned char *)"L"))
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
            if (WriteBuf != FrameBuf)
                restorepanel();
            if (LayerBuf)
                FreeMemory(LayerBuf);
            LayerBuf = NULL;
        }
        else
            closeframebuffer('A');
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"COPY")))
    {
#ifdef PICOMITE
        int complex = 0, background = 0;
        unsigned char *buff = WriteBuf;
        getcsargs(&p, 5);
        if (!(argc == 3 || argc == 5))
            SyntaxError();
        ;
        if (argc == 5)
        {
            if (checkstring(argv[4], (unsigned char *)"B"))
                background = 1;
            else
                SyntaxError();
            ;
        }
#else
        int complex = 0;
        unsigned char *buff = WriteBuf;
        getcsargs(&p, 3);
        if (!(argc == 3))
            SyntaxError();
        ;
#endif
        uint8_t *s = NULL, *d = NULL;
        if (checkstring(argv[0], (unsigned char *)"N"))
        {
            complex = 1;
            if ((void *)ReadBuffer == (void *)DisplayNotSet)
                StandardError(11);
        }
        else if (checkstring(argv[0], (unsigned char *)"L"))
            s = LayerBuf;
        else if (checkstring(argv[0], (unsigned char *)"F"))
            s = FrameBuf;
        else
            SyntaxError();
        ;
        if (checkstring(argv[2], (unsigned char *)"N"))
        {
            complex = 2;
        }
        else if (checkstring(argv[2], (unsigned char *)"L"))
            d = LayerBuf;
        else if (checkstring(argv[2], (unsigned char *)"F"))
            d = FrameBuf;
        else
            SyntaxError();
        ;

        if (complex != 1)
        {
            if (s == NULL)
                error("Source buffer not created");
        }
        if (complex != 2)
        {
            if (d == NULL)
                error("Destination buffer not created");
        }

        if (d != s)
        {
            if (!complex)
                memcpy(d, s, HRes * VRes / 2);
            else
            {
                if (complex == 1)
                { // copying from the real display
                    char *LCDBuffer = GetTempMainMemory(HRes * 3);
                    WriteBuf = d;
                    for (int y = 0; y < VRes; y++)
                    {
                        restorepanel();
                        ReadBuffer(0, y, HRes - 1, y, (unsigned char *)LCDBuffer);
                        WriteBuf = d;
                        setframebuffer();
                        DrawBuffer(0, y, HRes - 1, y, (unsigned char *)LCDBuffer);
                    }
                    // Always restore panel callbacks after temporarily switching
                    // to framebuffer primitives inside the loop.
                    restorepanel();
                }
                else
                { // copying to the real display
#ifdef PICOMITE
                    if (background)
                    {
#ifdef rp2350
                        if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || Option.DISPLAY_TYPE >= NEXTGEN || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                            StandardError(1);
                        ;
#else
                        if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                            StandardError(1);
                        ;
#endif
                        if (diskchecktimer < 100 && SPIatRisk)
                            diskchecktimer = 100;
                        multicore_fifo_push_blocking(1);
                        multicore_fifo_push_blocking((uint32_t)s);
                    }
                    else
                    {
#endif
                        copyframetoscreen(s, 0, HRes - 1, 0, VRes - 1, 0);
#ifdef PICOMITE
                    }
#endif
                }
            }
        }
        WriteBuf = buff;
    }
    else
        SyntaxError();
    ;
}
#endif

/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#ifdef PICOMITEVGA
void closeframebuffer(char layer)
{
#ifdef PICOMITEVGA
    /* About to flip WriteBuf and possibly free framebuffer memory.
       Restore cursor pixels to the current WriteBuf first. */
    CursorHide();
#endif
    if (layer == 'A')
        WriteBuf = DisplayBuf;
    if (FrameBuf != DisplayBuf && (layer == 'A' || layer == 'F'))
    {
        if (WriteBuf == FrameBuf)
            WriteBuf = DisplayBuf;
        switch (DISPLAY_TYPE)
        {
        case SCREENMODE1:
        case SCREENMODE2:
#ifdef rp2350
            if (ScreenSize < framebuffersize / 3)
                FrameBuf = DisplayBuf;
            else
                FreeMemory((void *)FrameBuf);
#else
            FreeMemory((void *)FrameBuf);
#endif
            break;
#ifdef rp2350
        case SCREENMODE3:
            FreeMemory((void *)FrameBuf);
            break;
#ifdef HDMI
        case SCREENMODE4:
        case SCREENMODE5:
            FreeMemory((void *)FrameBuf);
            break;
#endif
#endif
        }
    }
    if (LayerBuf != DisplayBuf && (layer == 'A' || layer == 'L'))
    {
        if (WriteBuf == LayerBuf)
            WriteBuf = DisplayBuf;
        volatile unsigned char *temp = LayerBuf;
        switch (DISPLAY_TYPE)
        {
        case SCREENMODE2:
            transparent = 0;
        case SCREENMODE1:
#ifdef rp2350
            if (ScreenSize < framebuffersize / 2)
                LayerBuf = DisplayBuf;
            else
            {
                LayerBuf = DisplayBuf;
                FreeMemory((void *)temp);
            }
#else
            LayerBuf = DisplayBuf;
            FreeMemory((void *)temp);
#endif
            break;
#ifdef rp2350
        case SCREENMODE3:
            LayerBuf = DisplayBuf;
            FreeMemory((void *)temp);
            break;
#ifdef HDMI
        case SCREENMODE4:
            LayerBuf = DisplayBuf;
            FreeMemory((void *)temp);
            break;
        case SCREENMODE5:
            LayerBuf = DisplayBuf;
            transparent = 0;
            break;
#endif
#endif
        }
    }
    if (SecondFrame != DisplayBuf && (layer == 'A' || layer == '2'))
    {
        FreeMemory((void *)SecondFrame);
    }
    if (SecondLayer != DisplayBuf && (layer == 'A' || layer == 'T'))
    {
        if (WriteBuf == LayerBuf)
            WriteBuf = DisplayBuf;
        volatile unsigned char *temp = SecondLayer;
        switch (DISPLAY_TYPE)
        {
        case SCREENMODE2:
            transparents = 0;
            SecondLayer = DisplayBuf;
            break;
        case SCREENMODE1:
            SecondLayer = DisplayBuf;
            FreeMemory((void *)temp);
            break;
#ifdef rp2350
        case SCREENMODE3:
            SecondLayer = DisplayBuf;
            FreeMemory((void *)temp);
            break;
#ifdef HDMI
        case SCREENMODE4:
            SecondLayer = DisplayBuf;
            FreeMemory((void *)temp);
            break;
        case SCREENMODE5:
            SecondLayer = DisplayBuf;
            transparents = 0;
            break;
#endif
#endif
        }
    }
    WriteBuf = (unsigned char *)FRAMEBUFFER;
    DisplayBuf = (unsigned char *)FRAMEBUFFER;
    LayerBuf = (unsigned char *)FRAMEBUFFER;
    FrameBuf = (unsigned char *)FRAMEBUFFER;
    SecondLayer = (unsigned char *)FRAMEBUFFER;
    SecondFrame = (unsigned char *)FRAMEBUFFER;
    transparent = 0;
}
/*  @endcond */

void cmd_framebuffer(void)
{
    /*
    RP2040 supports just modes 1 and 2. RP2350 supports modes 1-5.

    Every mode can have a framebuffer (CREATE) and a layer buffer
    (LAYER); only modes 2 and 5 composite the layer over the main
    display automatically — in other modes the layer is just another
    offscreen surface to BLIT from. For VGA/HDMI all buffers have the
    same resolution as the main display (unlike SPI TFT panels).

    Memory: each build statically allocates a framebuffer pool sized
    for its display layout (see Memory.c — typically the largest
    framebuffer plus, for VGA/HDMI mode 1, the tile-colour arrays).
    cmd_framebuffer's allocator tries to place extra buffers inside
    that pool when the per-mode ScreenSize × N fits within
    framebuffersize, otherwise it falls back to GetMemory() which
    pulls from the MMBasic heap. The exact resolution / pool budget
    is build-specific; resolution-specific tables that used to live
    here got stale and were removed — read the Memory.c declarations
    of AllMemory[] and framebuffersize for the current build's
    layout.

    On RP2350 builds with PSRAM enabled, GetMemory() may return a
    pointer into external PSRAM; the LAYER / LAYER TOP commands
    explicitly reject that for modes that need tightly-coupled RAM
    bandwidth (SCREENMODE3, SCREENMODE5, and the post-allocation
    check below).
    */
    unsigned char *p;
#ifdef rp2350
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE 2")))
    {
        int colour = 0;
        if (SecondFrame == DisplayBuf)
        {
            getcsargs(&p, 1);
            switch (DISPLAY_TYPE)
            {
            case SCREENMODE2:
            case SCREENMODE1:
                SecondFrame = GetMemory(ScreenSize);
                break;
#ifdef rp2350
            case SCREENMODE3:
                SecondFrame = GetMemory(ScreenSize);
                break;
#ifdef HDMI
            case SCREENMODE4:
                SecondFrame = GetMemory(ScreenSize);
                break;
            case SCREENMODE5:
                SecondFrame = GetMemory(ScreenSize);
                break;
#endif
#endif
            }
        }
        else
            error("Framebuffer 2 already exists");
        memset((void *)SecondFrame, colour, ScreenSize);
    }
    else
#endif
        if ((p = checkstring(cmdline, (unsigned char *)"CREATE")))
    {
        if (FrameBuf == DisplayBuf)
        {
            switch (DISPLAY_TYPE)
            {
            case SCREENMODE1:
            case SCREENMODE2:
#ifdef rp2350
                if (ScreenSize < framebuffersize / 3)
                    FrameBuf = DisplayBuf + 2 * ScreenSize;
                else
                    FrameBuf = GetMemory(ScreenSize);
#else
                FrameBuf = GetMemory(ScreenSize);
#endif
                break;
#ifdef rp2350
            case SCREENMODE3:
                FrameBuf = GetMemory(ScreenSize);
                break;
#ifdef HDMI
            case SCREENMODE4:
            case SCREENMODE5:
                FrameBuf = GetMemory(ScreenSize);
                break;
#endif
#endif
            }
        }
        else
            error("Framebuffer already exists");
        memset((void *)FrameBuf, 0, ScreenSize);

#ifdef rp2350
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LAYER TOP")))
    {
        int colour = 0;
        if (SecondLayer == DisplayBuf)
        {
            getcsargs(&p, 1);
            switch (DISPLAY_TYPE)
            {
            case SCREENMODE2:
                if (argc == 1)
                    transparents = getint(argv[0], 0, 15);
                colour = transparents | (transparents << 4);
                if (ScreenSize < framebuffersize / 4)
                    SecondLayer = DisplayBuf + 3 * ScreenSize;
                else
                    SecondLayer = GetMemory(ScreenSize);
                break;
            case SCREENMODE1:
                SecondLayer = GetMemory(ScreenSize);
                break;
            case SCREENMODE3:
                if (argc == 1)
                    transparents = getint(argv[0], 0, 15);
                SecondLayer = GetMemory(ScreenSize);
                if (SecondLayer >= (uint8_t *)PSRAMbase && SecondLayer < (uint8_t *)(PSRAMbase + 1024 * 1024 * 16))
                {
                    FreeMemory((void *)SecondLayer);
                    error("Second Layer must be in tightly coupled RAM");
                }
                colour = transparents | (transparents << 4);
                break;
#ifdef HDMI
            case SCREENMODE4:
                SecondLayer = GetMemory(ScreenSize);
                break;
            case SCREENMODE5:
                SecondLayer = GetMemory(ScreenSize);
                if (SecondLayer >= (uint8_t *)PSRAMbase && SecondLayer < (uint8_t *)(PSRAMbase + 1024 * 1024 * 16))
                {
                    FreeMemory((void *)SecondLayer);
                    error("Second Layer must be in tightly coupled RAM");
                }
                if (argc == 1)
                    transparents = getint(argv[0], 0, 255);
                colour = transparents;
                break;
#endif
            }
        }
        else
            error("Framebuffer already exists");
        memset((void *)SecondLayer, colour, ScreenSize);
#endif
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LAYER")))
    {
        int colour = 0;
        if (LayerBuf == DisplayBuf)
        {
            getcsargs(&p, 1);
            switch (DISPLAY_TYPE)
            {
            case SCREENMODE2:
                if (argc == 1)
                    transparent = getint(argv[0], 0, 15);
                colour = transparent | (transparent << 4);
            case SCREENMODE1:
#ifdef rp2350
                if (ScreenSize < framebuffersize / 2)
                    LayerBuf = DisplayBuf + ScreenSize;
                else
                    LayerBuf = GetMemory(ScreenSize);
#else
                LayerBuf = GetMemory(ScreenSize);
#endif
                break;
#ifdef rp2350
            case SCREENMODE3:
                if (argc == 1)
                    transparent = getint(argv[0], 0, 15);
                LayerBuf = GetMemory(ScreenSize);
                colour = transparent | (transparent << 4);
                break;
#ifdef HDMI
            case SCREENMODE4:
                LayerBuf = GetMemory(ScreenSize);
                if (argc == 1)
                    RGBtransparent = RGB555(getColour((char *)argv[0], 0));
                else
                    RGBtransparent = 0;
                break;
            case SCREENMODE5:
                if (ScreenSize < framebuffersize / 2)
                    LayerBuf = DisplayBuf + ScreenSize;
                else
                    LayerBuf = GetMemory(ScreenSize);
                if (argc == 1)
                    transparent = getint(argv[0], 0, 255);
                colour = transparent;
                break;
#endif
#endif
            }
#ifdef rp2350
            if (LayerBuf > (uint8_t *)PSRAMbase && LayerBuf < (uint8_t *)(PSRAMbase + 1024 * 1024 * 16))
            {
                FreeMemory((void *)LayerBuf);
                error("Layer Buffer must be in tightly coupled RAM");
            }
#endif
        }
        else
            error("Framebuffer already exists");
        if (DISPLAY_TYPE != SCREENMODE4)
            memset((void *)LayerBuf, colour, ScreenSize);
        else
        {
            uint16_t *p = (uint16_t *)LayerBuf;
            for (int i = 0; i < HRes * VRes; i++)
                *p++ = RGBtransparent;
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        if (checkstring(p, (unsigned char *)"F"))
        {
            closeframebuffer('F');
        }
        else if (checkstring(p, (unsigned char *)"L"))
        {
            closeframebuffer('T');
#ifdef rp2350
        }
        else if (checkstring(p, (unsigned char *)"T"))
        {
            closeframebuffer('L');
        }
        else if (checkstring(p, (unsigned char *)"2"))
        {
            closeframebuffer('2');
#endif
        }
        else
            closeframebuffer('A');
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
    {
#ifdef PICOMITEVGA
        /* Restore cursor pixels to the current WriteBuf before flipping
           it to a different layer/frame buffer. Otherwise the cursor
           leaves a ghost on the old buffer and the next refresh saves
           the wrong pixels on the new buffer. */
        CursorHide();
#endif
        if (checkstring(p, (unsigned char *)"N"))
            WriteBuf = DisplayBuf;
        else if (checkstring(p, (unsigned char *)"L"))
        {
            if (LayerBuf == DisplayBuf)
                error("Layer not created");
            WriteBuf = LayerBuf;
        }
#ifdef rp2350
        else if (checkstring(p, (unsigned char *)"T"))
        {
            if (SecondLayer == DisplayBuf)
                error("Layer 2 not created");
            WriteBuf = SecondLayer;
        }
        else if (checkstring(p, (unsigned char *)"2"))
        {
            if (SecondFrame == DisplayBuf)
                error("Frame 2 not created");
            WriteBuf = SecondFrame;
        }
#endif
        else if (checkstring(p, (unsigned char *)"F"))
        {
            if (FrameBuf == DisplayBuf)
                StandardError(38);
            WriteBuf = FrameBuf;
        }
        else
        {
            getcsargs(&p, 1);
            char *q = (char *)getCstring(argv[0]);
            if (strcasecmp(q, "N") == 0)
                WriteBuf = DisplayBuf;
            else if (strcasecmp(q, "L") == 0)
            {
                if (LayerBuf == DisplayBuf)
                    error("Layer not created");
                WriteBuf = LayerBuf;
            }
            else if (strcasecmp(q, "F") == 0)
            {
                if (FrameBuf == DisplayBuf)
                    StandardError(38);
                WriteBuf = FrameBuf;
            }
            else if (strcasecmp(q, "2") == 0)
            {
                if (SecondFrame == DisplayBuf)
                    error("Frame buffer 2 not created");
                WriteBuf = SecondFrame;
            }
            else if (strcasecmp(q, "T") == 0)
            {
                if (SecondLayer == DisplayBuf)
                    error("Layer Top not created");
                WriteBuf = SecondLayer;
            }
            else
                SyntaxError();
            ;
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WAIT")))
    {
#ifdef HDMI
        while (v_scanline != 0)
        {
        }
#else
        while (QVgaScanLine != 0)
        {
        }
#endif
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"COPY")))
    {
        getcsargs(&p, 5);
        if (!(argc == 3 || argc == 5))
            SyntaxError();
        ;
        volatile uint8_t *s = NULL, *d = NULL;
        if (checkstring(argv[0], (unsigned char *)"N"))
            s = DisplayBuf;
        else if (checkstring(argv[0], (unsigned char *)"L"))
            s = LayerBuf;
        else if (checkstring(argv[0], (unsigned char *)"F"))
            s = FrameBuf;
        else if (checkstring(argv[0], (unsigned char *)"2"))
            s = SecondFrame;
        else if (checkstring(argv[0], (unsigned char *)"T"))
            s = SecondLayer;
        else
            SyntaxError();
        ;
        if (checkstring(argv[2], (unsigned char *)"N"))
            d = DisplayBuf;
        else if (checkstring(argv[2], (unsigned char *)"L"))
            d = LayerBuf;
        else if (checkstring(argv[2], (unsigned char *)"F"))
            d = FrameBuf;
        else if (checkstring(argv[2], (unsigned char *)"2"))
            d = SecondFrame;
        else if (checkstring(argv[2], (unsigned char *)"T"))
            d = SecondLayer;
        else
            SyntaxError();
        ;
        if (argc == 5)
        {
            if (checkstring(argv[4], (unsigned char *)"B"))
            {
#ifdef HDMI
                while (v_scanline != 0)
                {
                }
#else
                while (QVgaScanLine != 0)
                {
                }
#endif
            }
            else
                SyntaxError();
            ;
        }
        if (d != s)
            //            #ifdef rp2350
            //                _Z10copy_wordsPKmPmm((uint32_t *)s, (uint32_t *)d, ScreenSize>>2);
            //            #else
            memcpy((void *)d, (void *)s, ScreenSize);
        //            #endif
        else
            error("Buffer not created");
    }
    else
        SyntaxError();
    ;
}
#endif

/*  @endcond */
