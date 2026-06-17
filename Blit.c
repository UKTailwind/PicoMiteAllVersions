/***********************************************************************************************************************
PicoMite MMBasic

Blit.c

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
 * @file Blit.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the BLIT and BLIT MEMORY MMBasic commands: blit
 *        buffers, the compressed-image blitter and screen-to-screen /
 *        memory-to-screen copies. Split out of Draw.c.
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
#endif
#if PICOMITERP2350
#include "VGA222.h"
#endif
void setframebuffer(void);
struct blitbuffer blitbuff[MAXBLITBUF + 1] = {0};

/*
 * @cond
 * The following section will be excluded from the documentation.
 */
char getnextuncompressednibble(char **s, int reset)
{
    static int toggle = 0;
    if (reset)
    {
        toggle = reset - 1;
        return 0;
    }
    if (!toggle)
    {
        toggle ^= 1;
        return **s & 0x0f;
    }
    else
    {
        toggle ^= 1;
        char r = (**s & 0xf0) >> 4;
        (*s)++;
        return r;
    }
}
static inline char getnextnibble(char **fc, int reset)
{
    static uint8_t available;
    static char out;
    if (reset)
    {
        available = 0;
    }
    if (available == 0)
    {
        available = **fc & 0xF; // number of identical pixels
        out = (**fc) >> 4;
        (*fc)++;
    }
    if (!reset)
        available--;
    return out;
}
void docompressed(char *fc, int x1, int y1, int w, int h, int8_t blank)
{
#ifndef PICOMITEVGA
    if (!WriteBuf)
    { // direct to screen
        if (blank == -1)
        {
            char tobuff[w / 2], *to;
            int ww = w;
            int xx1 = x1;
            if (x1 < 0)
            {
                ww += x1;
                xx1 = 0;
            }
            if (x1 + w > HRes)
            {
                ww = HRes - x1;
            }
            getnextnibble(&fc, 1); // reset the decoder
            for (int y = y1; y < y1 + h; y++)
            {
                to = tobuff;
                int otoggle = 0;
                for (int x = x1; x < x1 + w; x++)
                {
                    if (y < 0 || y >= VRes)
                    {
                        getnextnibble(&fc, 0);
                        continue;
                    }
                    if (x >= 0 && x < HRes)
                    {
                        if (otoggle == 0)
                        {
                            *to = getnextnibble(&fc, 0);
                            otoggle ^= 1;
                        }
                        else
                        {
                            *to |= (getnextnibble(&fc, 0) << 4);
                            otoggle ^= 1;
                            to++;
                        }
                    }
                    else
                        getnextnibble(&fc, 0);
                }
                if (ww > 0 && xx1 < HRes)
                    copyframetoscreen((unsigned char *)tobuff, xx1, xx1 + ww - 1, y, y, 0);
            }
        }
        else
        {
            char tobuff[w / 2], *to;
            getnextnibble(&fc, 1); // reset the decoder
            for (int y = y1; y < y1 + h; y++)
            {
                int x = x1;
                while (1)
                {
                    to = tobuff;
                    int otoggle = 0;
                    char c;
                    int ww = 0;
                    int xx = -1;
                    while ((c = getnextnibble(&fc, 0)) == blank)
                    {
                        x++;
                        if (x == x1 + w)
                            break;
                    }
                    if (x == x1 + w)
                        break; // nothing found so exit
                    *to = c;
                    otoggle ^= 1;
                    xx = x;
                    x++;
                    ww = 1;
                    if (xx != x1 + w - 1)
                    {
                        while ((c = getnextnibble(&fc, 0)) != blank)
                        {
                            x++;
                            ww++;
                            if (otoggle == 0)
                            {
                                *to = c;
                                otoggle ^= 1;
                            }
                            else
                            {
                                *to |= (c << 4);
                                otoggle ^= 1;
                                to++;
                            }
                            if (x == x1 + w)
                                break;
                        }
                    }
                    x++;
                    if (xx + ww > HRes)
                    {
                        ww = HRes - xx;
                    }
                    if (xx >= 0 && ww > 0 && y >= 0 && y < VRes)
                        copyframetoscreen((unsigned char *)tobuff, xx, xx + ww - 1, y, y, 0);
                    if (xx < 0 && xx + ww >= 0)
                    {
                        char *t = tobuff - (xx / 2) - (xx & 1);
                        ww += xx;
                        if (ww > 0)
                            copyframetoscreen((unsigned char *)t, 0, ww - 1, y, y, xx & 1);
                    }
                    if (x >= x1 + w)
                        break;
                }
            }
        }
    }
    else
#endif
        if (x1 % 2 == 0 && w % 2 == 0 && blank == -1)
    {
        char c, *to;
        c = getnextnibble(&fc, 1); // reset the decoder
        for (int y = y1; y < y1 + h; y++)
        {
            to = (char *)WriteBuf + y * (HRes >> 1) + (x1 >> 1);
            for (int x = x1; x < x1 + w; x += 2)
            {
                c = getnextnibble(&fc, 0);
                c |= (getnextnibble(&fc, 0) << 4);
                if (y < 0 || y >= VRes)
                    continue;
                if (x >= 0 && x < HRes)
                    *to = c;
                to++;
            }
        }
    }
    else
    {
        int otoggle = 0, itoggle = 0; // input will always start on a byte boundary
        char c, *to;
        c = getnextnibble(&fc, 1); // reset the decoder
        for (int y = y1; y < y1 + h; y++)
        {                                                        // loop though all of the output lines
            to = (char *)WriteBuf + y * (HRes >> 1) + (x1 >> 1); // get the byte that will start the output
            if (x1 & 1)
                otoggle = 1; // if x1 is odd then we will start on the high nibble
            else
                otoggle = 0;
            for (int x = x1; x < x1 + w; x++)
            {
                if (itoggle == 0)
                {
                    c = getnextnibble(&fc, 0);
                    itoggle = 1;
                }
                else
                {
                    c = getnextnibble(&fc, 0);
                    itoggle = 0;
                }
                if (y < 0 || y >= VRes)
                    continue;
                if (otoggle == 0)
                {
                    if (x >= 0 && x < HRes)
                    {
                        if (c != blank)
                        {
                            *to &= 0xF0;
                            *to |= c;
                        }
                    }
                }
                else
                {
                    if (x >= 0 && x < HRes)
                    {
                        if (c != blank)
                        {
                            *to &= 0x0f;
                            *to |= (c << 4);
                        }
                    }
                    to++;
                }
                otoggle ^= 1;
            }
        }
    }
}
/*  @endcond */

void cmd_blitmemory(void)
{
    int x1, y1, w, h;
    int8_t blank = -1;
    getcsargs(&cmdline, 7);
    if (argc < 5)
        SyntaxError();
    ;
    char *from = (char *)GetPeekAddr(argv[0]);
    x1 = (int)getinteger(argv[2]);
    y1 = (int)getinteger(argv[4]);
    uint16_t *size = (uint16_t *)from;
    w = (size[0] & 0x7FFF);
    h = (size[1] & 0x7FFF);
    from += 4;
    if (argc == 7)
        blank = getint(argv[6], -1, 15);
    if (size[0] & 0x8000 || size[1] & 0x8000)
    {
        docompressed(from, x1, y1, w, h, blank);
    }
    else
    {
#ifndef PICOMITEVGA
        if (!WriteBuf)
        {
            if (blank == -1)
            {
                char *fc = from;
                char tobuff[w / 2], *to;
                int ww = w;
                int xx1 = x1;
                if (x1 < 0)
                {
                    ww += x1;
                    xx1 = 0;
                }
                if (x1 + w > HRes)
                {
                    ww = HRes - x1;
                }
                getnextuncompressednibble(&fc, 1); // reset the decoder
                for (int y = y1; y < y1 + h; y++)
                {
                    to = tobuff;
                    int otoggle = 0;
                    for (int x = x1; x < x1 + w; x++)
                    {
                        if (y < 0 || y >= VRes)
                        {
                            getnextuncompressednibble(&fc, 0);
                            continue;
                        }
                        if (x >= 0 && x < HRes)
                        {
                            if (otoggle == 0)
                            {
                                *to = getnextuncompressednibble(&fc, 0);
                                otoggle ^= 1;
                            }
                            else
                            {
                                *to |= (getnextuncompressednibble(&fc, 0) << 4);
                                otoggle ^= 1;
                                to++;
                            }
                        }
                        else
                            getnextuncompressednibble(&fc, 0);
                    }
                    if (ww > 0 && xx1 < HRes)
                        copyframetoscreen((unsigned char *)tobuff, xx1, xx1 + ww - 1, y, y, 0);
                }
            }
            else
            {
                char *fc = from;
                char tobuff[w / 2], *to;
                getnextuncompressednibble(&fc, 1); // reset the decoder
                for (int y = y1; y < y1 + h; y++)
                {
                    int x = x1;
                    while (1)
                    {
                        to = tobuff;
                        int otoggle = 0;
                        char c;
                        int ww = 0;
                        int xx = -1;
                        while ((c = getnextuncompressednibble(&fc, 0)) == blank)
                        {
                            x++;
                            if (x == x1 + w)
                                break;
                        }
                        if (x == x1 + w)
                            break; // nothing found so exit
                        *to = c;
                        otoggle ^= 1;
                        xx = x;
                        x++;
                        ww = 1;
                        if (xx != x1 + w - 1)
                        {
                            while ((c = getnextuncompressednibble(&fc, 0)) != blank)
                            {
                                x++;
                                ww++;
                                if (otoggle == 0)
                                {
                                    *to = c;
                                    otoggle ^= 1;
                                }
                                else
                                {
                                    *to |= (c << 4);
                                    otoggle ^= 1;
                                    to++;
                                }
                                if (x == x1 + w)
                                    break;
                            }
                        }
                        x++;
                        if (xx + ww > HRes)
                        {
                            ww = HRes - xx;
                        }
                        if (xx >= 0 && ww > 0 && y >= 0 && y < VRes)
                            copyframetoscreen((unsigned char *)tobuff, xx, xx + ww - 1, y, y, 0);
                        if (xx < 0 && xx + ww >= 0)
                        {
                            char *t = tobuff - (xx / 2) - (xx & 1);
                            ww += xx;
                            if (ww > 0)
                                copyframetoscreen((unsigned char *)t, 0, ww - 1, y, y, xx & 1);
                        }
                        if (x >= x1 + w)
                            break;
                    }
                }
            }
        }
        else
#endif
            if (x1 % 2 == 0 && w % 2 == 0 && blank == -1)
        {
            char c, *to;
            for (int y = y1; y < y1 + h; y++)
            {
                to = (char *)WriteBuf + y * (HRes >> 1) + (x1 >> 1);
                for (int x = x1; x < x1 + w; x += 2)
                {
                    c = *from++;
                    if (y < 0 || y >= VRes)
                        continue;
                    if (x >= 0 && x < HRes)
                        *to = c;
                    to++;
                }
            }
        }
        else
        {
            int otoggle = 0, itoggle = 0; // input will always start on a byte boundary
            char c, *to;
            for (int y = y1; y < y1 + h; y++)
            {                                                        // loop though all of the output lines
                to = (char *)WriteBuf + y * (HRes >> 1) + (x1 >> 1); // get the byte that will start the output
                if (x1 & 1)
                    otoggle = 1; // if x1 is odd then we will start on the high nibble
                else
                    otoggle = 0;
                for (int x = x1; x < x1 + w; x++)
                {
                    if (itoggle == 0)
                    {
                        c = *from & 0x0f;
                        itoggle = 1;
                    }
                    else
                    {
                        c = *from >> 4;
                        from++;
                        itoggle = 0;
                    }
                    if (y < 0 || y >= VRes)
                        continue;
                    if (otoggle == 0)
                    {
                        if (x >= 0 && x < HRes)
                        {
                            if (c != blank)
                            {
                                *to &= 0xF0;
                                *to |= c;
                            }
                        }
                    }
                    else
                    {
                        if (x >= 0 && x < HRes)
                        {
                            if (c != blank)
                            {
                                *to &= 0x0f;
                                *to |= (c << 4);
                            }
                        }
                        to++;
                    }
                    otoggle ^= 1;
                }
            }
        }
    }
}
/* RGB332 count+value RLE blitter for BLIT MEMORY332. After the 4-byte w/h
   header the data is [count 1..255][value] runs that may cross scanline
   boundaries and total exactly w*h pixels. blank = -1 draws everything; 0..255
   treats that RGB332 value as transparent.
   Two destinations:
     - WriteBuf != NULL (HDMI MODE 5, or a created RGB332 framebuffer): one
       byte/pixel at WriteBuf + y*HRes + x (as DrawRectangle256 does).
     - WriteBuf == NULL on a buffered RGB332 panel (Option.DISPLAY_TYPE >=
       NEXTGEN): there is no linear WriteBuf, so decode a row at a time and push
       it through the display's native blitter DrawBLITBuffer(), which writes
       ScreenBuffer with the hardware scroll offset and marks the dirty
       rectangle for the auto-refresh. Mirrors docompressed()'s !WriteBuf path. */
#if BLITMEMORY332
static void docompressed332(char *fc, int x1, int y1, int w, int h, int blank)
{
    uint8_t *p = (uint8_t *)fc;
    if (w <= 0 || h <= 0)
        return;
#ifndef PICOMITEVGA
    if (!WriteBuf)
    {
        uint8_t rowbuf[w];
        int c = 0;
        uint8_t v = 0;
        for (int row = 0; row < h; row++)
        {
            for (int col = 0; col < w; col++) /* runs carry across rows via c/v */
            {
                if (c == 0)
                {
                    c = *p++;
                    v = *p++;
                }
                rowbuf[col] = v;
                c--;
            }
            int yy = y1 + row;
            if ((unsigned)yy >= (unsigned)VRes)
                continue;
            int col = 0; /* draw maximal on-screen, non-transparent spans */
            while (col < w)
            {
                int xx = x1 + col;
                if ((blank >= 0 && rowbuf[col] == (uint8_t)blank) || xx < 0 || xx >= HRes)
                {
                    col++;
                    continue;
                }
                int start = col;
                while (col < w)
                {
                    int x = x1 + col;
                    if ((blank >= 0 && rowbuf[col] == (uint8_t)blank) || x < 0 || x >= HRes)
                        break;
                    col++;
                }
                DrawBLITBuffer(x1 + start, yy, x1 + col - 1, yy, rowbuf + start);
            }
        }
        return;
    }
#endif
    uint8_t *fb = (uint8_t *)WriteBuf;
    if (!fb)
        error("Display buffer not available");
    long total = (long)w * h, done = 0;
    /* Fast path: full-width, x-aligned and fully on-screen -> linear fill. */
    if (x1 == 0 && w == HRes && y1 >= 0 && y1 + h <= VRes)
    {
        uint8_t *to = fb + (long)y1 * HRes;
        while (done < total)
        {
            int c = *p++;
            uint8_t v = *p++;
            if (done + c > total)
                c = (int)(total - done); /* guard malformed data */
            if (blank < 0 || v != (uint8_t)blank)
                memset(to, v, c);
            to += c;
            done += c;
        }
        return;
    }
    /* General path: arbitrary x1/y1, clipped, runs may span row boundaries. */
    while (done < total)
    {
        int c = *p++;
        uint8_t v = *p++;
        int draw = (blank < 0 || v != (uint8_t)blank);
        while (c > 0 && done < total)
        {
            int row = (int)(done / w);
            int col = (int)(done % w);
            int span = w - col; /* pixels left on this output row */
            if (span > c)
                span = c;
            int yy = y1 + row;
            int xx = x1 + col;
            if (draw && (unsigned)yy < (unsigned)VRes)
            {
                int s = 0, e = span; /* clip the run to [0, HRes) */
                if (xx < 0)
                    s = -xx;
                if (xx + e > HRes)
                    e = HRes - xx;
                if (e > s)
                    memset(fb + (long)yy * HRes + xx + s, v, e - s);
            }
            done += span;
            c -= span;
        }
    }
}
#endif
int blitother(void)
{
    int x1, y1, x2, y2, w, h;
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"COMPRESSED")))
    {

        int8_t blank = -1;
        getcsargs(&p, 7);
        if (argc < 5)
            SyntaxError();
        ;
        char *fc = (char *)GetPeekAddr(argv[0]);
        x1 = (int)getinteger(argv[2]);
        y1 = (int)getinteger(argv[4]);
        uint16_t *size = (uint16_t *)fc;
        w = size[0];
        h = size[1];
        if (w > HRes || h > VRes)
            error("Invalid dimensions, w=%, h=%", w, h);
        fc += 4;
        if (argc == 7)
            blank = getint(argv[6], -1, 15);
        docompressed(fc, x1, y1, w, h, blank);
        return 1;
    }
#if BLITMEMORY332
    else if ((p = checkstring(cmdline, (unsigned char *)"MEMORY332")))
    {
        int blank = -1;
        getcsargs(&p, 7);
        if (argc < 5)
            SyntaxError();
        ;
#ifdef PICOMITEVGA
        if (!(DISPLAY_TYPE == SCREENMODE5))
            error("Only available with an RGB332 display");
#else
        if (!(Option.DISPLAY_TYPE >= NEXTGEN))
            error("Only available with an RGB332 display");
#endif

        char *fc = (char *)GetPeekAddr(argv[0]);
        x1 = (int)getinteger(argv[2]);
        y1 = (int)getinteger(argv[4]);
        uint16_t *size = (uint16_t *)fc;
        w = size[0] & 0x7FFF;
        h = size[1] & 0x7FFF;
        if (w > HRes || h > VRes)
            error("Invalid dimensions, w=%, h=%", w, h);
        fc += 4;
        if (argc == 7)
            blank = getint(argv[6], 0, 255);
        docompressed332(fc, x1, y1, w, h, blank);
        return 1;
    }
#endif
    else if ((p = checkstring(cmdline, (unsigned char *)"FLASH")))
    {
        unsigned char *d = NULL;
        unsigned char *s = NULL;
        int blank = -1;
        getcsargs(&p, 17);
        if (!(argc == 15 || argc == 17))
            SyntaxError();
        ;
        int i = getint(argv[0], 1, MAXFLASHSLOTS);
        s = (unsigned char *)(flash_target_contents + (i - 1) * MAX_PROG_SIZE);
        uint32_t *x = (uint32_t *)s;
        HResS = x[0];
        VResS = x[1];
        if (HResS < 0 || HResS > 3840 || VResS < 0 || VResS > 2160)
            error("Invalid Image");
        HResD = HRes;
        VResD = VRes;
        s += 8;
        if (checkstring(argv[2], (unsigned char *)"L"))
            d = LayerBuf;
        else if (checkstring(argv[2], (unsigned char *)"F"))
            d = FrameBuf;
#ifdef PICOMITEVGA
        else if (checkstring(argv[2], (unsigned char *)"N"))
            d = DisplayBuf;
#ifdef rp2350
        else if (checkstring(argv[2], (unsigned char *)"T"))
            d = SecondLayer;
#endif
#else
        else if (checkstring(argv[2], (unsigned char *)"N"))
            StandardError(1);
#endif
        else
            SyntaxError();
        x1 = getinteger(argv[4]);
        y1 = getinteger(argv[6]);
        x2 = getinteger(argv[8]);
        y2 = getinteger(argv[10]);
        w = getinteger(argv[12]);
        h = getinteger(argv[14]);
        if (x1 < 0 || y1 < 0 || x1 + w > HResS || y1 + h > VResS)
            StandardError(21);
        if (argc == 17)
            blank = getint(argv[16], -1, 15);
        blit121(s, d, x1, y1, w, h, x2, y2, blank);
        return 1;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"FRAMEBUFFER")))
    {
        int blank = -1;
#ifndef PICOMITEVGA
        int otoggle = 0, itoggle = 0; // input will always start on a byte boundary
        volatile unsigned char c, *to;
#endif
        volatile unsigned char *s = NULL, *d = NULL;
        getcsargs(&p, 17);
        if (argc < 15)
            SyntaxError();
        ;
        if (checkstring(argv[0], (unsigned char *)"L"))
            s = LayerBuf;
        else if (checkstring(argv[0], (unsigned char *)"F"))
            s = FrameBuf;
#ifdef PICOMITEVGA
        else if (checkstring(argv[0], (unsigned char *)"N"))
            s = DisplayBuf;
#ifdef rp2350
        else if (checkstring(argv[0], (unsigned char *)"T"))
            s = SecondLayer;
#endif
#else
        else if (checkstring(argv[0], (unsigned char *)"N"))
            s = NULL;
#endif
        else
            SyntaxError();
        ;
        if (checkstring(argv[2], (unsigned char *)"L"))
            d = LayerBuf;
        else if (checkstring(argv[2], (unsigned char *)"F"))
            d = FrameBuf;
#ifdef PICOMITEVGA
        else if (checkstring(argv[2], (unsigned char *)"N"))
            d = DisplayBuf;
#ifdef rp2350
        else if (checkstring(argv[2], (unsigned char *)"T"))
            d = SecondLayer;
#endif
#else
        else if (checkstring(argv[2], (unsigned char *)"N"))
            d = NULL;
#endif
        else
            SyntaxError();
        ;
        if (s == d)
            error("Same framebuffer");
        if (s == NULL && (void *)ReadBuffer == (void *)DisplayNotSet)
            StandardError(11);
        x1 = (int)getinteger(argv[4]);
        y1 = (int)getinteger(argv[6]);
        x2 = (int)getinteger(argv[8]);
        y2 = (int)getinteger(argv[10]);
        w = (int)getinteger(argv[12]);
        h = (int)getinteger(argv[14]);
        if (argc == 17)
        {
#ifdef HDMI
            if (DISPLAY_TYPE == SCREENMODE4)
                blank = getint(argv[16], -1, 0x7FFF);
            else if (DISPLAY_TYPE == SCREENMODE5)
                blank = getint(argv[16], -1, 0xFF);
            else
#endif
                blank = getint(argv[16], -1, 15);
        }
        if (d != NULL && s != NULL)
        {
#ifdef PICOMITEVGA
            if (DISPLAY_TYPE == SCREENMODE1)
                error("Not available in mode 1");
#endif
#ifdef HDMI
            if (DISPLAY_TYPE == SCREENMODE4)
            {
                // RGB555 blit - 2 bytes per pixel
                int has_transparency = (blank >= 0);
                uint16_t tcolor = (uint16_t)blank;
                int sx_start = 0, sy_start = 0, sx_end = w, sy_end = h;
                if (x1 < 0)
                    sx_start = -x1;
                if (y1 < 0)
                    sy_start = -y1;
                if (x1 + w > HRes)
                    sx_end = HRes - x1;
                if (y1 + h > VRes)
                    sy_end = VRes - y1;
                if (x2 < 0)
                {
                    int adj = -x2;
                    if (adj > sx_start)
                        sx_start = adj;
                }
                if (y2 < 0)
                {
                    int adj = -y2;
                    if (adj > sy_start)
                        sy_start = adj;
                }
                if (x2 + w > HRes)
                {
                    int adj = HRes - x2;
                    if (adj < sx_end)
                        sx_end = adj;
                }
                if (y2 + h > VRes)
                {
                    int adj = VRes - y2;
                    if (adj < sy_end)
                        sy_end = adj;
                }
                if (sx_start >= sx_end || sy_start >= sy_end)
                    return 1;
                int num_pixels = sx_end - sx_start;
                for (int i = sy_start; i < sy_end; i++)
                {
                    uint16_t *src_row = (uint16_t *)s + (y1 + i) * HRes + (x1 + sx_start);
                    uint16_t *dst_row = (uint16_t *)d + (y2 + i) * HRes + (x2 + sx_start);
                    if (!has_transparency)
                    {
                        memcpy(dst_row, src_row, num_pixels * sizeof(uint16_t));
                    }
                    else
                    {
                        for (int j = 0; j < num_pixels; j++)
                        {
                            if (src_row[j] != tcolor)
                                dst_row[j] = src_row[j];
                        }
                    }
                }
                return 1;
            }
            else if (DISPLAY_TYPE == SCREENMODE5)
            {
                // RGB332 blit - 1 byte per pixel
                int has_transparency = (blank >= 0);
                uint8_t tcolor = (uint8_t)blank;
                int sx_start = 0, sy_start = 0, sx_end = w, sy_end = h;
                if (x1 < 0)
                    sx_start = -x1;
                if (y1 < 0)
                    sy_start = -y1;
                if (x1 + w > HRes)
                    sx_end = HRes - x1;
                if (y1 + h > VRes)
                    sy_end = VRes - y1;
                if (x2 < 0)
                {
                    int adj = -x2;
                    if (adj > sx_start)
                        sx_start = adj;
                }
                if (y2 < 0)
                {
                    int adj = -y2;
                    if (adj > sy_start)
                        sy_start = adj;
                }
                if (x2 + w > HRes)
                {
                    int adj = HRes - x2;
                    if (adj < sx_end)
                        sx_end = adj;
                }
                if (y2 + h > VRes)
                {
                    int adj = VRes - y2;
                    if (adj < sy_end)
                        sy_end = adj;
                }
                if (sx_start >= sx_end || sy_start >= sy_end)
                    return 1;
                int num_pixels = sx_end - sx_start;
                for (int i = sy_start; i < sy_end; i++)
                {
                    uint8_t *src_row = (uint8_t *)s + (y1 + i) * HRes + (x1 + sx_start);
                    uint8_t *dst_row = (uint8_t *)d + (y2 + i) * HRes + (x2 + sx_start);
                    if (!has_transparency)
                    {
                        memcpy(dst_row, src_row, num_pixels);
                    }
                    else
                    {
                        for (int j = 0; j < num_pixels; j++)
                        {
                            if (src_row[j] != tcolor)
                                dst_row[j] = src_row[j];
                        }
                    }
                }
                return 1;
            }
#endif
            // RGB121 blit
            HResD = HRes;
            VResD = VRes;
            HResS = HRes;
            VResS = VRes;
            blit121((uint8_t *)s, (uint8_t *)d, x1, y1, w, h, x2, y2, blank);
            return 1;
        }
#ifndef PICOMITEVGA
        else if (s != NULL)
        { // writing to a physical LCD display
#if PICOMITERP2350
            if (Option.DISPLAY_TYPE >= NEXTGEN)
            {
                if (w < 1 || h < 1)
                    return 1;
                for (int sy = y1, dy = y2; sy < y1 + h; sy++, dy++)
                {
                    if (sy < 0 || sy >= VRes)
                        continue;
                    uint8_t *src_row = (uint8_t *)s + sy * (HRes >> 1);
                    for (int sx = x1, dx = x2; sx < x1 + w; sx++, dx++)
                    {
                        uint8_t pix;
                        if (sx < 0 || sx >= HRes)
                            continue;
                        pix = src_row[sx >> 1];
                        pix = (sx & 1) ? ((pix >> 4) & 0x0F) : (pix & 0x0F);
                        if (blank != -1 && pix == (uint8_t)blank)
                            continue;
                        DrawPixelMEM332(dx, dy, RGB121map[pix]);
                    }
                }
                return 1;
            }
#endif
            if (x1 == 0 && x2 == 0 && w == HRes && blank == -1)
            {
                s += y1 * HRes / 2;
                copyframetoscreen((void *)s, 0, HRes - 1, y2, y2 + h - 1, 0);
            }
            else
            {
                if (screen320 && Option.DISPLAY_TYPE != SSD1963_4_16)
                    x2 *= 2;
                char tobuff[w / 2], *to;
                for (int y = y1, yo = y2; y < y1 + h; y++, yo++)
                {
                    char *fc = (char *)s + y * HRes / 2 + x1 / 2;
                    getnextuncompressednibble(&fc, 1 + (x1 & 1)); // reset the decoder
                    int x = x1;
                    while (1)
                    {
                        to = tobuff;
                        int otoggle = 0;
                        char c;
                        int ww = 0;
                        int xx = -1;
                        while ((c = getnextuncompressednibble(&fc, 0)) == blank)
                        {
                            x++;
                            if (x == x1 + w)
                                break;
                        }
                        if (x == x1 + w)
                            break; // nothing found so exit
                        *to = c;
                        otoggle ^= 1;
                        xx = x;
                        x++;
                        ww = 1;
                        if (xx != x1 + w - 1)
                        {
                            while ((c = getnextuncompressednibble(&fc, 0)) != blank)
                            {
                                x++;
                                ww++;
                                if (otoggle == 0)
                                {
                                    *to = c;
                                    otoggle ^= 1;
                                }
                                else
                                {
                                    *to |= (c << 4);
                                    otoggle ^= 1;
                                    to++;
                                }
                                if (x == x1 + w)
                                    break;
                            }
                        }
                        x++;
                        if (xx + ww > HRes)
                        {
                            ww = HRes - xx;
                        }
                        if (x2 >= 0 && ww > 0 && yo >= 0 && yo < VRes)
                        {
                            copyframetoscreen((unsigned char *)tobuff, x2, x2 + ww - 1, yo, yo, 0);
                        }
                        if (x2 < 0 && x2 + ww >= 0)
                        {
                            char *t = tobuff - ((x2) / 2) - (x2 & 1);
                            ww += x2;
                            if (ww > 0)
                            {
                                copyframetoscreen((unsigned char *)t, 0, ww - 1, yo, yo, x2 & 1);
                            }
                        }
                        if (x >= x1 + w)
                            break;
                    }
                }
            }
            return 1;
        }
        else if (d != NULL)
        { // reading from a physical LCD display
#if PICOMITERP2350
            if (Option.DISPLAY_TYPE >= NEXTGEN)
            {
                union colourmap
                {
                    char rgbbytes[4];
                    unsigned int rgb;
                } cb;
                unsigned char *screen = (unsigned char *)(ScreenBuffer);
                if (w < 1 || h < 1)
                    return 1;
                for (int sy = y1, dy = y2; sy < y1 + h; sy++, dy++)
                {
                    if (sy < 0 || sy >= VRes)
                        continue;
                    int scroll_y = sy + ScrollStart;
                    if (scroll_y >= VRes)
                        scroll_y -= VRes;
                    unsigned char *src_row = screen + scroll_y * HRes;
                    for (int sx = x1, dx = x2; sx < x1 + w; sx++, dx++)
                    {
                        if (sx < 0 || sx >= HRes)
                            continue;
                        if (dx < 0 || dx >= HRes || dy < 0 || dy >= VRes)
                            continue;
                        uint8_t p332 = src_row[sx];
                        cb.rgbbytes[0] = ((p332 & 0x03) << 6);
                        cb.rgbbytes[1] = ((p332 & 0x1C) << 3);
                        cb.rgbbytes[2] = (p332 & 0xE0);
                        cb.rgbbytes[3] = 0;
                        uint8_t fcolour = (uint8_t)RGB121(cb.rgb);
                        if (blank != -1 && fcolour == (uint8_t)blank)
                            continue;
                        volatile unsigned char *to = d + dy * (HRes >> 1) + (dx >> 1);
                        if (dx & 1)
                        {
                            *to &= 0x0F;
                            *to |= (fcolour << 4);
                        }
                        else
                        {
                            *to &= 0xF0;
                            *to |= fcolour;
                        }
                    }
                }
                return 1;
            }
#endif
            union colourmap
            {
                char rgbbytes[4];
                unsigned int rgb;
            } cb;
            unsigned char *rbuff = (unsigned char *)GetTempMainMemory(w * 4);
            char *from = GetTempMainMemory((w + 1) / 2);
            for (int y = y1, toy = y2; y < y1 + h; y++, toy++)
            { // loop though all of the output lines
                ReadBuffer(x1, y, x1 + w - 1, y, rbuff);
                uint8_t *p = rbuff;
                char *pp = from;
                for (int x = x1; x < x1 + w; x++)
                {
                    cb.rgbbytes[0] = *p++; // this order swaps the bytes to match the .BMP file
                    cb.rgbbytes[1] = *p++;
                    cb.rgbbytes[2] = *p++;
                    int fcolour = RGB121(cb.rgb);
                    if (x & 1)
                    {
                        *pp &= 0x0F;
                        *pp |= (fcolour << 4);
                        pp++;
                    }
                    else
                    {
                        *pp &= 0xF0;
                        *pp |= fcolour;
                    }
                }
                to = d + toy * (HRes >> 1) + (x2 >> 1); // get the byte that will start the output
                itoggle = 0;
                pp = from;
                if (x2 & 1)
                    otoggle = 1; // if x1 is odd then we will start on the high nibble
                else
                    otoggle = 0;
                for (int x = x1, tox = x2; x < x1 + w; x++, tox++)
                {
                    if (itoggle == 0)
                    {
                        if (tox >= 0 && tox < HRes)
                            c = *pp & 0x0f;
                        else
                            c = 0;
                        itoggle = 1;
                    }
                    else
                    {
                        if (tox >= 0 && tox < HRes)
                            c = *pp >> 4;
                        else
                            c = 0;
                        pp++;
                        itoggle = 0;
                    }
                    if (y < 0 || y >= VRes)
                        continue;
                    if (otoggle == 0)
                    {
                        if (tox >= 0 && tox < HRes)
                        {
                            if (c != blank)
                            {
                                *to &= 0xF0;
                                *to |= c;
                            }
                        }
                    }
                    else
                    {
                        if (tox >= 0 && tox < HRes)
                        {
                            if (c != blank)
                            {
                                *to &= 0x0f;
                                *to |= (c << 4);
                            }
                        }
                        to++;
                    }
                    otoggle ^= 1;
                }
            }
            return 1;
        }
#endif
    }
    return 0;
}
/*  @endcond */

void cmd_blit(void)
{
    int x1, y1, x2, y2, w, h, bnbr;
    unsigned char *buff = NULL;
    unsigned char *p;
    CheckDisplay();
    if (blitother())
        return;
    p = checkstring(cmdline, (unsigned char *)"LOADBMP");
    if (p == NULL)
        p = checkstring(cmdline, (unsigned char *)"LOAD");
    if (p)
    {
        // get the command line arguments
        getcsargs(&p, 11); // this MUST be the first executable line in the function
        s_ReadBMP state;
        readstate = &state; // store the various variables for use in the callback
        state.img_x_offset = 0;
        state.img_y_offset = 0;
        if (*argv[0] == '#')
            argv[0]++;                             // check if the first arg is prefixed with a #
        bnbr = getint(argv[0], 1, MAXBLITBUF) - 1; // get the buffer number
        if (blitbuff[bnbr].blitbuffptr)
            error("Buffer % in use", bnbr);
        if (argc == 0)
            StandardError(2);
        if (!InitSDCard())
            return;
        p = getCstring(argv[2]); // get the file name
        state.img_x_offset = state.img_y_offset = 0;
        if (argc >= 5 && *argv[4])
            state.img_x_offset = getinteger(argv[4]); // get the x origin (optional) argument
        if (argc >= 7 && *argv[6])
            state.img_y_offset = getinteger(argv[6]); // get the y origin (optional) argument
        if (state.img_x_offset < 0 || state.img_y_offset < 0)
            StandardError(34);
        state.width = state.height = -1;
        if (argc >= 9 && *argv[8])
            state.width = getinteger(argv[8]); // get the x length (optional) argument
        if (argc == 11)
            state.height = getinteger(argv[10]); // get the y length (optional) argument
        // open the file
        AppendDefaultExtension((char *)p, ".bmp");
        BMPfnbr = FindFreeFileNbr();
        if (!BasicFileOpen((char *)p, BMPfnbr, FA_READ))
            return;
        decodeBMPheader(&state.image_width, &state.image_height);
        if (state.width == -1)
            state.width = state.image_width - state.img_x_offset;
        if (state.height == -1)
            state.height = state.image_height - state.img_y_offset;
        if (state.width + state.img_x_offset > state.image_width || state.height + state.img_y_offset > state.image_height)
            StandardError(34);
        blitbuff[bnbr].blitbuffptr = GetMemory(state.width * state.height * 3 + 4);
        memset(blitbuff[bnbr].blitbuffptr, 0xFF, state.width * state.height * 3 + 4);
        state.output_buffer = (uint8_t *)blitbuff[bnbr].blitbuffptr;
        linecallback = loadBMPlinecallback;
        decodeBMP(0);
        blitbuff[bnbr].w = state.width;
        blitbuff[bnbr].h = state.height;
        FileClose(BMPfnbr);
        return;
    }
#ifndef PICOMITEVGA
    if ((p = checkstring(cmdline, (unsigned char *)"MERGE")))
    { // merge the layer onto the physical display
        if (!LayerBuf)
            error("Layer not created");
        if (!FrameBuf)
            error("Framebuffer not created");
        uint8_t colour = 0;
        getcsargs(&p, 13);
        if (argc >= 1 && *argv[0])
        {
            colour = getint(argv[0], 0, 15);
        }
        x1 = getinteger(argv[2]);
        y1 = getinteger(argv[4]);
        w = getinteger(argv[6]);
        h = getinteger(argv[8]);
#ifdef PICOMITE
        uint8_t background = 0;
        if (argc >= 11 && *argv[10])
        {
            if (checkstring(argv[10], (unsigned char *)"B"))
                background = 1;
            else if (checkstring(argv[10], (unsigned char *)"R"))
                background = 2;
            else if (checkstring(argv[10], (unsigned char *)"A"))
                background = 3;
            else
                SyntaxError();
            ;
        }
        if (background == 1)
        {
            if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                StandardError(1);
            ;
            if (diskchecktimer < 200 && SPIatRisk)
                diskchecktimer = 200;
            multicore_fifo_push_blocking(4);
            multicore_fifo_push_blocking(x1);
            multicore_fifo_push_blocking(y1);
            multicore_fifo_push_blocking(w);
            multicore_fifo_push_blocking(h);
            multicore_fifo_push_blocking((uint32_t)colour);
        }
        else if (background == 2)
        {
            mergetimer = 0;
            if (argc == 13)
                mergetimer = getint(argv[12], 0, 60 * 10 * 1000);
            if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL))))
                StandardError(1);
            ;
            if (WriteBuf == NULL)
                WriteBuf = FrameBuf;
            setframebuffer();
            multicore_fifo_push_blocking(5);
            multicore_fifo_push_blocking(x1);
            multicore_fifo_push_blocking(y1);
            multicore_fifo_push_blocking(w);
            multicore_fifo_push_blocking(h);
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
            blitmerge(x1, y1, w, h, colour);
        return;
    }
#endif
    if ((p = checkstring(cmdline, (unsigned char *)"RESIZE")))
    {
        uint8_t *s = NULL, *d = NULL;
        int src_is_n = 0, dst_is_n = 0;
        int sx, sy, sw, sh, dx, dy, dw, dh;
        int transparent = -1;
        int start_x, start_y, end_x, end_y;

        getcsargs(&p, 21);
        if (!(argc == 19 || argc == 21))
            SyntaxError();

        if (checkstring(argv[0], (unsigned char *)"L"))
            s = (uint8_t *)LayerBuf;
        else if (checkstring(argv[0], (unsigned char *)"F"))
            s = (uint8_t *)FrameBuf;
#ifdef PICOMITEVGA
        else if (checkstring(argv[0], (unsigned char *)"2"))
            s = (uint8_t *)SecondFrame;
#endif
#ifdef PICOMITEVGA
        else if (checkstring(argv[0], (unsigned char *)"N"))
        {
            s = (uint8_t *)DisplayBuf;
            src_is_n = 1;
        }
#ifdef rp2350
        else if (checkstring(argv[0], (unsigned char *)"T"))
            s = (uint8_t *)SecondLayer;
#endif
#else
        else if (checkstring(argv[0], (unsigned char *)"N"))
            StandardError(1);
#endif
        else
            SyntaxError();

        if (checkstring(argv[2], (unsigned char *)"L"))
            d = (uint8_t *)LayerBuf;
        else if (checkstring(argv[2], (unsigned char *)"F"))
            d = (uint8_t *)FrameBuf;
#ifdef PICOMITEVGA
        else if (checkstring(argv[2], (unsigned char *)"2"))
            d = (uint8_t *)SecondFrame;
#endif
#ifdef PICOMITEVGA
        else if (checkstring(argv[2], (unsigned char *)"N"))
        {
            d = (uint8_t *)DisplayBuf;
            dst_is_n = 1;
        }
#ifdef rp2350
        else if (checkstring(argv[2], (unsigned char *)"T"))
            d = (uint8_t *)SecondLayer;
#endif
#else
        else if (checkstring(argv[2], (unsigned char *)"N"))
            StandardError(1);
#endif
        else
            SyntaxError();

        if ((src_is_n || dst_is_n) && !(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3))
            error("N buffer requires RGB121 mode (MODE 2/3)");

        if (s == NULL)
            error("Source buffer not created");
        if (d == NULL)
            error("Destination buffer not created");

        sx = getinteger(argv[4]);
        sy = getinteger(argv[6]);
        sw = getinteger(argv[8]);
        sh = getinteger(argv[10]);
        dx = getinteger(argv[12]);
        dy = getinteger(argv[14]);
        dw = getinteger(argv[16]);
        dh = getinteger(argv[18]);
        if (argc == 21)
            transparent = getint(argv[20], -1, 15);

        if (sw < 1 || sh < 1 || dw < 1 || dh < 1)
            return;

        if (sx < 0 || sy < 0 || sx + sw > HRes || sy + sh > VRes)
            StandardError(21);

        start_x = dx < 0 ? 0 : dx;
        start_y = dy < 0 ? 0 : dy;
        end_x = dx + dw;
        end_y = dy + dh;
        if (end_x > HRes)
            end_x = HRes;
        if (end_y > VRes)
            end_y = VRes;
        if (start_x >= end_x || start_y >= end_y)
            return;

        {
            int src_stride = (sw + 1) >> 1;
            int dst_stride = HRes >> 1;
            uint8_t *src_copy = NULL;
            int overlap = 0;

            if (s == d)
            {
                if (start_x < sx + sw && end_x > sx && start_y < sy + sh && end_y > sy)
                    overlap = 1;
            }

            if (overlap)
            {
                src_copy = (uint8_t *)GetMemory(src_stride * sh);
                for (int y = 0; y < sh; y++)
                {
                    for (int x = 0; x < sw; x++)
                    {
                        uint8_t src_byte = s[(sy + y) * dst_stride + ((sx + x) >> 1)];
                        uint8_t pix = ((sx + x) & 1) ? ((src_byte >> 4) & 0x0F) : (src_byte & 0x0F);
                        uint8_t *dstp = &src_copy[y * src_stride + (x >> 1)];
                        if (x & 1)
                            *dstp = (*dstp & 0x0F) | (pix << 4);
                        else
                            *dstp = (*dstp & 0xF0) | (pix & 0x0F);
                    }
                }
            }

            int64_t y_step = ((int64_t)sh << 16) / dh;
            int64_t y_fp = (((int64_t)(start_y - dy) * sh) << 16) / dh;

            for (int y = start_y; y < end_y; y++, y_fp += y_step)
            {
                int src_y = (int)(y_fp >> 16);
                if (src_y < 0)
                    src_y = 0;
                else if (src_y >= sh)
                    src_y = sh - 1;

                int64_t x_step = ((int64_t)sw << 16) / dw;
                int64_t x_fp = (((int64_t)(start_x - dx) * sw) << 16) / dw;

                for (int x = start_x; x < end_x; x++, x_fp += x_step)
                {
                    int src_x = (int)(x_fp >> 16);
                    if (src_x < 0)
                        src_x = 0;
                    else if (src_x >= sw)
                        src_x = sw - 1;

                    uint8_t src_byte;
                    uint8_t pix;
                    if (src_copy)
                    {
                        src_byte = src_copy[src_y * src_stride + (src_x >> 1)];
                        pix = (src_x & 1) ? ((src_byte >> 4) & 0x0F) : (src_byte & 0x0F);
                    }
                    else
                    {
                        int abs_src_x = sx + src_x;
                        int abs_src_y = sy + src_y;
                        src_byte = s[abs_src_y * dst_stride + (abs_src_x >> 1)];
                        pix = (abs_src_x & 1) ? ((src_byte >> 4) & 0x0F) : (src_byte & 0x0F);
                    }
                    if (transparent >= 0 && pix == transparent)
                        continue;
                    uint8_t *dstp = &d[y * dst_stride + (x >> 1)];
                    if (x & 1)
                        *dstp = (*dstp & 0x0F) | (pix << 4);
                    else
                        *dstp = (*dstp & 0xF0) | (pix & 0x0F);
                }
            }

            if (src_copy)
                FreeMemory(src_copy);
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"READ")))
    {
        getcsargs(&p, 9);
        if ((void *)ReadBuffer == (void *)DisplayNotSet)
            StandardError(11);
        if (argc != 9)
            SyntaxError();
        ;
        if (*argv[0] == '#')
            argv[0]++;                             // check if the first arg is prefixed with a #
        bnbr = getint(argv[0], 1, MAXBLITBUF) - 1; // get the buffer number
        x1 = getinteger(argv[2]);
        y1 = getinteger(argv[4]);
        w = getinteger(argv[6]);
        h = getinteger(argv[8]);
        if (w < 1 || h < 1)
            return;
        if (x1 < 0)
        {
            x2 -= x1;
            w += x1;
            x1 = 0;
        }
        if (y1 < 0)
        {
            y2 -= y1;
            h += y1;
            y1 = 0;
        }
        if (x1 + w > HRes)
            w = HRes - x1;
        if (y1 + h > VRes)
            h = VRes - y1;
        if (w < 1 || h < 1 || x1 < 0 || x1 + w > HRes || y1 < 0 || y1 + h > VRes)
            return;
        if (blitbuff[bnbr].blitbuffptr == NULL)
        {
            blitbuff[bnbr].blitbuffptr = GetMemory(w * h * 3);
            ReadBuffer(x1, y1, x1 + w - 1, y1 + h - 1, (unsigned char *)blitbuff[bnbr].blitbuffptr);
            blitbuff[bnbr].w = w;
            blitbuff[bnbr].h = h;
        }
        else
            error("Buffer in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
    {
        int mode = 0;
        getcsargs(&p, 7);
        if (!(argc == 5 || argc == 7))
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF) - 1; // get the number
        if (blitbuff[bnbr].h == TRIANGLE_BUFFER_MARKER)
            StandardError(37);
        if (blitbuff[bnbr].blitbuffptr != NULL)
        {
            x1 = (int)getint(argv[2], -blitbuff[bnbr].w + 1, HRes);
            y1 = (int)getint(argv[4], -blitbuff[bnbr].h + 1, VRes);
            if (argc == 7)
                mode = (char)getint(argv[6], 0, 7);
            w = blitbuff[bnbr].w;
            h = blitbuff[bnbr].h;
            //            int cursorhidden = 0;
            int rotation = mode & 3;
            if (x1 >= HRes || x1 + w < 0 || y1 >= VRes || y1 + h < 0)
                return;
            if (mode == 0)
            {
                int src_x = 0, src_y = 0;
                int draw_x = x1, draw_y = y1;
                int draw_w = w, draw_h = h;
                unsigned char *src = (unsigned char *)blitbuff[bnbr].blitbuffptr;

                if (draw_x < 0)
                {
                    src_x = -draw_x;
                    draw_w += draw_x;
                    draw_x = 0;
                }
                if (draw_y < 0)
                {
                    src_y = -draw_y;
                    draw_h += draw_y;
                    draw_y = 0;
                }
                if (draw_x + draw_w > HRes)
                    draw_w = HRes - draw_x;
                if (draw_y + draw_h > VRes)
                    draw_h = VRes - draw_y;
                if (draw_w < 1 || draw_h < 1)
                    return;

                if (src_x == 0 && src_y == 0 && draw_w == w && draw_h == h)
                {
                    DrawBuffer(draw_x, draw_y, draw_x + draw_w - 1, draw_y + draw_h - 1, src);
                }
                else
                {
                    unsigned char *row_src = src + (src_y * w + src_x) * 3;
                    for (int row = 0; row < draw_h; row++)
                    {
                        DrawBuffer(draw_x, draw_y + row, draw_x + draw_w - 1, draw_y + row, row_src + row * w * 3);
                    }
                }
                return;
            }
            else
            {
                buff = GetTempMainMemory(w * h * 4);
                for (int j = w * h * 4 - 1, i = w * h * 3 - 1; j >= 0; j -= 4)
                {
                    buff[j] = 0;
                    buff[j - 1] = blitbuff[bnbr].blitbuffptr[i--];
                    buff[j - 2] = blitbuff[bnbr].blitbuffptr[i--];
                    buff[j - 3] = blitbuff[bnbr].blitbuffptr[i--];
                }
                int *d = (int *)buff;
                if (rotation & 1)
                { // swap left/write
                    for (int y = 0; y < h; y++)
                    {
                        for (int x = 0, xx = w - 1; x < (w >> 1); x++, xx--)
                        {
                            swap(d[y * w + x], d[y * w + xx]);
                        }
                    }
                }
                if (rotation & 2)
                {
                    for (int x = 0; x < w; x++)
                    {
                        for (int y = 0, yy = h - 1; y < (h >> 1); y++, yy--)
                        {
                            swap(d[x + y * w], d[x + yy * w]);
                        }
                    }
                }
                if (x1 < 0)
                { // now deal with situation where you are blitting part off the left of the screen
                    int *s = (int *)buff;
                    d = (int *)buff;
                    int start = -x1;
                    for (int y = 0; y < h; y++)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            if (x >= start)
                                *d++ = *s++;
                            else
                                s++;
                        }
                    }
                    w -= start;
                    x1 = 0;
                }
                if (x1 + w >= HRes)
                { // now deal with situation where you are blitting part off the right of the screen
                    int *s = (int *)buff;
                    d = (int *)buff;
                    int over = ((x1 + w) - HRes);
                    int end = w - over;
                    for (int y = 0; y < h; y++)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            if (x >= end)
                                s++;
                            else
                                *d++ = *s++;
                        }
                    }
                    w -= over;
                }
                for (int i = 0, j = 0; i < w * h * 3; i += 3)
                {
                    buff[i] = buff[j++];
                    buff[i + 1] = buff[j++];
                    buff[i + 2] = buff[j++];
                    j++;
                }
            }
            if (!(mode & 4))
            {
                if (y1 < 0)
                {
                    buff -= (y1 * 3 * w);
                    h += y1;
                    y1 = 0;
                }
                DrawBuffer(x1, y1, x1 + w - 1, y1 + h - 1, buff);
            }
            else
            {
                if ((void *)ReadBuffer == (void *)DisplayNotSet)
                    StandardError(11);
                unsigned char *current = GetTempMainMemory(w * h * 3);
                if (y1 < 0)
                {
                    buff -= (y1 * 3 * w);
                    h += y1;
                    y1 = 0;
                }
                ReadBuffer(x1, y1, x1 + w - 1, y1 + h - 1, current);
                for (int i = 0; i < w * h * 3; i += 3)
                {
                    if (buff[i] || buff[i + 1] || buff[i + 2])
                    {
                        current[i] = buff[i];
                        current[i + 1] = buff[i + 1];
                        current[i + 2] = buff[i + 2];
                    }
                }
                DrawBuffer(x1, y1, x1 + w - 1, y1 + h - 1, current);
            }
        }
        else
            error((char *)"Buffer not in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        getcsargs(&p, 1);
        if (*argv[0] == '#')
            argv[0]++;                             // check if the first arg is prefixed with a #
        bnbr = getint(argv[0], 1, MAXBLITBUF) - 1; // get the buffer number
        if (blitbuff[bnbr].blitbuffptr != NULL)
        {
            FreeMemory((unsigned char *)blitbuff[bnbr].blitbuffptr);
            blitbuff[bnbr].blitbuffptr = NULL;
        }
        else
            error("Buffer not in use");
        // get the number
    }
    else
    {
        getcsargs(&cmdline, 11);
        if ((void *)ReadBuffer == (void *)DisplayNotSet)
            StandardError(11);
        if (argc != 11)
            SyntaxError();
        ;
        x1 = getinteger(argv[0]);
        y1 = getinteger(argv[2]);
        x2 = getinteger(argv[4]);
        y2 = getinteger(argv[6]);
        w = getinteger(argv[8]);
        h = getinteger(argv[10]);
        //        PInt(x1);PIntComma(y1);PIntComma(x2);PIntComma(y2);PIntComma(w);PIntComma(h);PRet();
        if (w < 1 || h < 1)
            return;
        if (x1 < 0)
        {
            x2 -= x1;
            w += x1;
            x1 = 0;
        }
        if (x2 < 0)
        {
            x1 -= x2;
            w += x2;
            x2 = 0;
        }
        if (y1 < 0)
        {
            y2 -= y1;
            h += y1;
            y1 = 0;
        }
        if (y2 < 0)
        {
            y1 -= y2;
            h += y2;
            y2 = 0;
        }
        if (x1 + w > HRes)
            w = HRes - x1;
        if (x2 + w > HRes)
            w = HRes - x2;
        if (y1 + h > VRes)
            h = VRes - y1;
        if (y2 + h > VRes)
            h = VRes - y2;
        if (w < 1 || h < 1 || x1 < 0 || x1 + w > HRes || x2 < 0 || x2 + w > HRes || y1 < 0 || y1 + h > VRes || y2 < 0 || y2 + h > VRes)
            return;
#ifdef PICOMITEVGA
        if (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3)
        { // RGB121 blit
            HResD = HRes;
            VResD = VRes;
            HResS = HRes;
            VResS = VRes;
            blit121_self(WriteBuf, x1, y1, w, h, x2, y2);
        }
        else if (DISPLAY_TYPE && (DISPLAY_TYPE == SCREENMODE4 || DISPLAY_TYPE == SCREENMODE5))
        {
            unsigned char *buff = NULL;
            int max_x;
            if (x1 >= x2)
            {
                max_x = 1;
                buff = GetMemory((max_x * h) * (DISPLAY_TYPE == SCREENMODE4 ? 2 : 1));
                while (w > max_x)
                {
                    ReadBufferFast(x1, y1, x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(x2, y2, x2 + max_x - 1, y2 + h - 1, -1, buff);
                    x1 += max_x;
                    x2 += max_x;
                    w -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
            }
            if (x1 < x2)
            {
                int start_x1, start_x2;
                max_x = 1;
                buff = GetMemory((max_x * h) * (DISPLAY_TYPE == SCREENMODE4 ? 2 : 1));
                start_x1 = x1 + w - max_x;
                start_x2 = x2 + w - max_x;
                while (w > max_x)
                {
                    ReadBufferFast(start_x1, y1, start_x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(start_x2, y2, start_x2 + max_x - 1, y2 + h - 1, -1, buff);
                    w -= max_x;
                    start_x1 -= max_x;
                    start_x2 -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
            }
        }
        else if (DISPLAY_TYPE && DISPLAY_TYPE == SCREENMODE1)
        {
            unsigned char *buff = NULL;
            int max_x, ww;
            ww = w;
            if (x1 >= x2)
            {
                max_x = 1;
                buff = GetMemory((max_x * h) >> 1);
                while (w > max_x)
                {
                    ReadBufferFast(x1, y1, x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(x2, y2, x2 + max_x - 1, y2 + h - 1, -1, buff);
                    x1 += max_x;
                    x2 += max_x;
                    w -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
                if ((x1 % 8 == 0) && (x2 % 8 == 0) && (y1 % ytileheight == 0) && (y2 % ytileheight == 0) && (ww % 8 == 0) && (h % ytileheight == 0))
                {
                    int tx1 = x1 / 8;
                    int xc = ww / 8;
                    int ty1 = y1 / ytileheight;
                    int yc = h / ytileheight;
                    int tx2 = x2 / 8;
                    int ty2 = y2 / ytileheight;
                    for (int x = 0; x < xc; x++)
                    {
                        for (int y = 0; y < yc; y++)
                        {
                            int s = (y + ty1) * X_TILE + x + tx1;
                            int d = (y + ty2) * X_TILE + x + tx2;
                            tilefcols[d] = tilefcols[s];
                            tilebcols[d] = tilebcols[s];
                        }
                    }
                }
                return;
            }
            if (x1 < x2)
            {
                int start_x1, start_x2;
                max_x = 1;
                buff = GetMemory(max_x * h);
                start_x1 = x1 + w - max_x;
                start_x2 = x2 + w - max_x;
                while (w > max_x)
                {
                    ReadBufferFast(start_x1, y1, start_x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(start_x2, y2, start_x2 + max_x - 1, y2 + h - 1, -1, buff);
                    w -= max_x;
                    start_x1 -= max_x;
                    start_x2 -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
                if ((x1 % 8 == 0) && (x2 % 8 == 0) && (y1 % ytileheight == 0) && (y2 % ytileheight == 0) && (ww % 8 == 0) && (h % ytileheight == 0))
                {
                    int tx1 = x1 / 8;
                    int xc = ww / 8;
                    int ty1 = y1 / ytileheight;
                    int yc = h / ytileheight;
                    int tx2 = x2 / 8;
                    int ty2 = y2 / ytileheight;
                    for (int x = xc - 1; x >= 0; x--)
                    {
                        for (int y = 0; y < yc; y++)
                        {
                            int s = (y + ty1) * X_TILE + x + tx1;
                            int d = (y + ty2) * X_TILE + x + tx2;
                            tilefcols[d] = tilefcols[s];
                            tilebcols[d] = tilebcols[s];
                        }
                    }
                }
                return;
            }
        }
    }
#else
        int max_x;
        if ((WriteBuf == LayerBuf || WriteBuf == FrameBuf) && WriteBuf)
        {
            if ((w & 1) == 0 && (x1 & 1) == 0 && (x2 & 1) == 0)
            { // Easiest case - byte move in the x direction with w even
                if (y1 < y2)
                {
                    for (int y = h - 1; y >= 0; y--)
                    {
                        uint8_t *in = WriteBuf + ((y + y1) * HRes + x1) / 2;
                        uint8_t *out = WriteBuf + ((y + y2) * HRes + x2) / 2;
                        memcpy(out, in, w / 2);
                    }
                }
                else if (y1 > y2)
                {
                    for (int y = 0; y < h; y++)
                    {
                        uint8_t *in = WriteBuf + ((y + y1) * HRes + x1) / 2;
                        uint8_t *out = WriteBuf + ((y + y2) * HRes + x2) / 2;
                        memcpy(out, in, w / 2);
                    }
                }
                else
                {
                    for (int y = 0; y < h; y++)
                    {
                        uint8_t *in = WriteBuf + ((y + y1) * HRes + x1) / 2;
                        uint8_t *out = WriteBuf + ((y + y2) * HRes + x2) / 2;
                        memmove(out, in, w / 2);
                    }
                }
                return;
            }
            else
            { // nibble move not as easy
                uint8_t *inbuff = GetTempMainMemory(HRes / 2);
                int intoggle = x1 & 1;
                int outtoggle = x2 & 1;
                int n = w / 2;
                if (w & 1)
                    n++;
                if (y1 > y2)
                {
                    for (int y = 0; y < h; y++)
                    {
                        if (!intoggle)
                            memcpy(inbuff, WriteBuf + ((y + y1) * HRes + x1) / 2, n);
                        else
                        {
                            int toggle = 1;
                            uint8_t *in = WriteBuf + ((y + y1) * HRes + x1) / 2;
                            uint8_t *out = inbuff;
                            for (int x = 0; x < w; x++)
                            {
                                if (toggle)
                                {
                                    uint8_t t = *in >> 4;
                                    *out = t;
                                    in++;
                                }
                                else
                                {
                                    uint8_t t = (*in & 0xf) << 4;
                                    *out |= t;
                                    out++;
                                }
                                toggle ^= 1;
                            }
                        }
                        if (!outtoggle)
                        {
                            memcpy(WriteBuf + ((y + y2) * HRes + x2) / 2, inbuff, w / 2);
                            if (w & 1)
                            {
                                uint8_t *lastnibble = WriteBuf + ((y + y2) * HRes + x2 + w) / 2;
                                *lastnibble &= 0xf0;
                                *lastnibble |= (inbuff[w / 2] & 0xf);
                            }
                        }
                        else
                        {
                            int toggle = 1;
                            uint8_t *in = inbuff;
                            uint8_t *out = WriteBuf + ((y + y2) * HRes + x2) / 2;
                            for (int x = 0; x < w; x++)
                            {
                                if (toggle)
                                {
                                    uint8_t t = (*in & 0xf) << 4;
                                    *out &= 0x0f; // clear the top byte of the output
                                    *out |= t;
                                    out++;
                                }
                                else
                                {
                                    uint8_t t = (*in >> 4);
                                    *out &= 0xf0;
                                    *out |= t;
                                    in++;
                                }
                                toggle ^= 1;
                            }
                        }
                    }
                }
                else
                {
                    for (int y = h - 1; y >= 0; y--)
                    {
                        if (!intoggle)
                            memcpy(inbuff, WriteBuf + ((y + y1) * HRes + x1) / 2, n);
                        else
                        {
                            int toggle = 1;
                            uint8_t *in = WriteBuf + ((y + y1) * HRes + x1) / 2;
                            uint8_t *out = inbuff;
                            for (int x = 0; x < w; x++)
                            {
                                if (toggle)
                                {
                                    uint8_t t = *in >> 4;
                                    *out = t;
                                    in++;
                                }
                                else
                                {
                                    uint8_t t = (*in & 0xf) << 4;
                                    *out |= t;
                                    out++;
                                }
                                toggle ^= 1;
                            }
                        }
                        if (!outtoggle)
                        {
                            memcpy(WriteBuf + ((y + y2) * HRes + x2) / 2, inbuff, w / 2);
                            if (w & 1)
                            {
                                uint8_t *lastnibble = WriteBuf + ((y + y2) * HRes + x2 + w) / 2;
                                *lastnibble &= 0xf0;
                                *lastnibble |= (inbuff[w / 2] & 0xf);
                            }
                        }
                        else
                        {
                            int toggle = 1;
                            uint8_t *in = inbuff;
                            uint8_t *out = WriteBuf + ((y + y2) * HRes + x2) / 2;
                            for (int x = 0; x < w; x++)
                            {
                                if (toggle)
                                {
                                    uint8_t t = (*in & 0xf) << 4;
                                    *out &= 0x0f; // clear the top byte of the output
                                    *out |= t;
                                    out++;
                                }
                                else
                                {
                                    uint8_t t = (*in >> 4);
                                    *out &= 0xf0;
                                    *out |= t;
                                    in++;
                                }
                                toggle ^= 1;
                            }
                        }
                    }
                }
                return;
            }
        }
        else
        {
            if (x1 >= x2)
            {
                max_x = 1;
#if PICOMITERP2350
                buff = GetMemory(max_x * h * (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16 ? 2 : (Option.DISPLAY_TYPE >= NEXTGEN ? 1 : 3)));
#else
                buff = GetMemory(max_x * h * (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16 ? 2 : 3));
#endif
                while (w > max_x)
                {
                    ReadBLITBuffer(x1, y1, x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBLITBuffer(x2, y2, x2 + max_x - 1, y2 + h - 1, buff);
                    x1 += max_x;
                    x2 += max_x;
                    w -= max_x;
                }
                ReadBLITBuffer(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBLITBuffer(x2, y2, x2 + w - 1, y2 + h - 1, buff);
                FreeMemory(buff);
                if (Option.Refresh)
                    Display_Refresh();
                return;
            }
            if (x1 < x2)
            {
                int start_x1, start_x2;
                max_x = 1;
#if PICOMITERP2350
                buff = GetMemory(max_x * h * (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16 ? 2 : (Option.DISPLAY_TYPE >= NEXTGEN ? 1 : 3)));
#else
                buff = GetMemory(max_x * h * (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16 ? 2 : 3));
#endif
                start_x1 = x1 + w - max_x;
                start_x2 = x2 + w - max_x;
                while (w > max_x)
                {
                    ReadBLITBuffer(start_x1, y1, start_x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBLITBuffer(start_x2, y2, start_x2 + max_x - 1, y2 + h - 1, buff);
                    w -= max_x;
                    start_x1 -= max_x;
                    start_x2 -= max_x;
                }
                ReadBLITBuffer(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBLITBuffer(x2, y2, x2 + w - 1, y2 + h - 1, buff);
                FreeMemory(buff);
                if (Option.Refresh)
                    Display_Refresh();
                return;
            }
        }
    }
#endif
}

/*  @endcond */
