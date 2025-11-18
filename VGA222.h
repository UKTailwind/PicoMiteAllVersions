#ifndef VGA222_H
#define VGA222_H
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
 * Function declarations - RGB222 operations
 * ============================================================================ */
void DrawPixel222(int x, int y, int c);
void DrawRectangle222(int x1, int y1, int x2, int y2, int c);
void DrawBitmap222(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
void ScrollLCD222(int lines);
void DrawBuffer222(int x1, int y1, int x2, int y2, unsigned char *p);
void DrawBuffer222Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
void ReadBuffer222(int x1, int y1, int x2, int y2, unsigned char *c);
void ReadBuffer222Fast(int x1, int y1, int x2, int y2, unsigned char *c);
void InitDisplay222(void);
void init_vga222(void);
void ConfigDisplay222(unsigned char *p);
void blit222(uint32_t *source, uint32_t *destination, int xsource, int ysource,
             int width, int height, int xdestination, int ydestination, int missingcolour);
void blit222_self(uint32_t *framebuffer, int xsource, int ysource,
                  int width, int height, int xdestination, int ydestination);
/* ============================================================================
 * Display mode and resolution constants
 * ============================================================================ */
#define VGA640 (Option.DISPLAY_TYPE == VGA222 || Option.DISPLAY_TYPE == VGA222X320)
#define hnative (VGA640 ? 640 : 720)
#define cyclesperpixel 10
#define hsync (VGA640 ? 96 : 108) / display_details[Option.DISPLAY_TYPE].bits
#define hfrontporch (VGA640 ? 16 : 18) / display_details[Option.DISPLAY_TYPE].bits
#define hvisible hnative / display_details[Option.DISPLAY_TYPE].bits
#define hbackporch (VGA640 ? 48 : 54) / display_details[Option.DISPLAY_TYPE].bits
#define vsync (VGA640 ? 2 : 2)
#define vbackporch (VGA640 ? 33 : 35)
#define vfrontporch (VGA640 ? 10 : 12)
#define vvisible (VGA640 ? 480 : 400)
#define pixelsperword 5 // RGB222

#define wordsperline hvisible\pixelsperword
#define wordstotransfer wordsperline *vvisible
#define vlines vsync + vbackporch + vvisible + vfrontporch
#define hwholeline hsync + hfrontporch + hvisible + hbackporch
#define hvisibleclock hvisible *cyclesperpixel
#define hsyncclock hsync *cyclesperpixel
#define hfrontporchclock hfrontporch *cyclesperpixel
#define hbackporchclock hbackporch *cyclesperpixel

#endif