#ifndef RGB121_H
#define RGB121_H
/***********************************************************************************************************************
PicoMite MMBasic

Turtle.c

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
/* ============================================================================
 * Function declarations - Pixel operations (16-bit)
 * ============================================================================ */
void DrawPixel16(int x, int y, int c);
void DrawRectangle16(int x1, int y1, int x2, int y2, int c);
void DrawBitmap16(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
void DrawBuffer16(int x1, int y1, int x2, int y2, unsigned char *p);
void DrawBuffer16Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
void ReadBuffer16(int x1, int y1, int x2, int y2, unsigned char *c);
void ReadBuffer16Fast(int x1, int y1, int x2, int y2, unsigned char *c);
void ScrollLCD16(int lines);
void blit121_self(uint8_t *framebuffer, int xsource, int ysource,
                  int width, int height, int xdestination, int ydestination);
void blit121(uint8_t *source, uint8_t *destination, int xsource, int ysource,
             int width, int height, int xdestination, int ydestination, int missingcolour);
static inline uint8_t RGB121(uint32_t c)
{
    return ((c & 0x800000) >> 20) | ((c & 0xC000) >> 13) | ((c & 0x80) >> 7);
}
static inline uint16_t RGB121pack(uint32_t c)
{
    return (RGB121(c) << 12) | (RGB121(c) << 8) | (RGB121(c) << 4) | RGB121(c);
}
extern int HResD;
extern int VResD;
extern int HResS;
extern int VResS;
extern const int colours[16];
#endif