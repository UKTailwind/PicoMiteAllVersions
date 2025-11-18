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
 * @brief Source for Graphics MMBasic commands and functions
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "VGA222.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
uint32_t *g_vgalinemap = NULL;
const uint16_t vga0[] = {
    0x80A0,
    0xA427,
    0xA442,
    0x0442,
    0xC019,
    0xC318,
    //
    0x80A0,
    0xA047,
    0xA022,
    0x20C1,
    0xC042,
    0x20C2,
    0xC000,
    0x004B,
    //
    0x80A0,
    0xA047,
    0xA022,
    0x29C0,
    0x80A0,
    0x6906,
    0x6906,
    0x6906,
    0x6906,
    0x6606,
    0x6062,
    0x0052,
    0xA003,
};
const uint16_t vga1[] = {
    0x90A0,
    0xB0C7,
    0x9018,
    0x90A0,
    0xB0C7,
    0x9019,
    0x90A0,
    0xB047,
    0xD007,
    0x9098,
    0xB027,
    0x30C0,
    0x20C0,
    0x004C,
    0x9099,
    0xB027,
    0x30C0,
    0x1050,
    0xD009,
    0xB022,
    0x30C0,
    0x1054,
    //
    0x90A0,
    0xB047,
    0x90A0,
    0xB027,
    0x30C1,
    0x005B,
    0xB022,
    0x105D,
    0xD00A};
const uint16_t vga2[] = {
    0x80A0,
    0xA0C7,
    0x8018,
    0x80A0,
    0xA0C7,
    0x8019,
    0x80A0,
    0xA047,
    0xC007,
    0x8098,
    0xA027,
    0x20C0,
    0x30C0,
    0x104C,
    0x8099,
    0xA027,
    0x20C0,
    0x0050,
    0xC009,
    0xA022,
    0x20C0,
    0x0054,
    //
    0x90A0,
    0xB047,
    0x90A0,
    0xB027,
    0x30C1,
    0x005B,
    0xB022,
    0x105D,
    0xD00A};

void init_vga222(void)
{
    int maplines = display_details[Option.DISPLAY_TYPE].vertical * display_details[Option.DISPLAY_TYPE].bits;
    if (display_details[Option.DISPLAY_TYPE].bits == 1)
        for (int i = 0; i < maplines; i++)
        {
            g_vgalinemap[i] = (uint32_t)FRAMEBUFFER + i * hvisible / pixelsperword * sizeof(uint32_t);
        }
    else if (display_details[Option.DISPLAY_TYPE].bits == 2)
        for (int i = 0; i < maplines; i += 2)
        {
            g_vgalinemap[i] = (uint32_t)FRAMEBUFFER + (i / 2) * hvisible / pixelsperword * sizeof(uint32_t);
            g_vgalinemap[i + 1] = g_vgalinemap[i];
        }
    pio1->irq = 255; // clear all irq in the statemachines on the pio
    pio0->irq = 255; // clear all irq in the statemachines on the pio
    gpio_set_function(PinDef[Option.VGA_HSYNC].GPno, GPIO_FUNC_PIO1);
    gpio_set_function(PinDef[Option.VGA_HSYNC].GPno + 1, GPIO_FUNC_PIO1);
    ExtCfg(Option.VGA_HSYNC, EXT_BOOT_RESERVED, 0);
    ExtCfg(PINMAP[PinDef[Option.VGA_HSYNC].GPno + 1], EXT_BOOT_RESERVED, 0);
    for (int i = 0; i < 6; i++)
    {
        gpio_set_function(PinDef[Option.VGA_BLUE].GPno + i, GPIO_FUNC_PIO0);
        gpio_set_drive_strength(PinDef[Option.VGA_BLUE].GPno + i, GPIO_DRIVE_STRENGTH_8MA);
        ExtCfg(PINMAP[PinDef[Option.VGA_BLUE].GPno + i], EXT_BOOT_RESERVED, 0);
    }
    struct pio_program program;
    program.length = sizeof(vga0) / sizeof(uint16_t);
    program.origin = 0;
    program.instructions = vga0;
    for (int sm = 0; sm < 4; sm++)
        hw_clear_bits(&pio0->ctrl, 1 << (PIO_CTRL_SM_ENABLE_LSB + sm));
    pio_clear_instruction_memory(pio0);
    pio_add_program(pio0, &program);
    program.length = sizeof(vga1) / sizeof(uint16_t);
    program.origin = 0;
    program.instructions = (VGA640 ? vga1 : vga2);
    for (int sm = 0; sm < 4; sm++)
        hw_clear_bits(&pio1->ctrl, 1 << (PIO_CTRL_SM_ENABLE_LSB + sm));
    pio_clear_instruction_memory(pio1);
    pio_add_program(pio1, &program);
    PIO0 = false;
    PIO1 = false;
    int clock = clock_get_hz(clk_sys);
    configurePIO(pio0, 0, clock / display_details[Option.DISPLAY_TYPE].bits, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1, 5, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0);
    startPIO(pio0, 0);
    configurePIO(pio0, 1, clock, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 8, 13, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0);
    startPIO(pio0, 1);
    configurePIO(pio0, 2, clock / display_details[Option.DISPLAY_TYPE].bits, 14, 0, 0, 0, 0, 0, 0, 0, PinDef[Option.VGA_BLUE].GPno, 6, 1, 0, -1, 16, 26, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0);
    startPIO(pio0, 2);
    configurePIO(pio1, 0, clock, 0, 0, PinDef[Option.VGA_HSYNC].GPno + 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, -1, 8, 21, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1);
    startPIO(pio1, 0);
    configurePIO(pio1, 1, clock / display_details[Option.DISPLAY_TYPE].bits, 22, 0, PinDef[Option.VGA_HSYNC].GPno, 1, 1, 0, 0, 0, 0, 0, 0, 0, -1, 25, 30, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0);
    startPIO(pio1, 1);
    pio_sm_put_blocking(pio0, 1, vvisible - 1);
    pio_sm_put_blocking(pio0, 2, hvisible / pixelsperword - 1);
    pio_sm_put_blocking(pio1, 0, vsync - 1);
    pio_sm_put_blocking(pio1, 0, vbackporch - 1);
    pio_sm_put_blocking(pio1, 0, vvisible + vfrontporch - 2);
    pio_sm_put_blocking(pio1, 1, hbackporchclock - 1);
    pio_sm_put_blocking(pio1, 1, hsyncclock - 1);
    syncPIO(0, 15, 0, 15);
    setup_dma_pio_lines(pio0, 2, dma_tx_chan3,
                        g_vgalinemap, maplines, hvisible / pixelsperword);
    pio_sm_put_blocking(pio0, 0, hwholeline - 1);
    while (1)
    {
        tight_loop_contents();
    }
}
// RGB222 packing: 5 pixels per 32-bit word (30 bits used, 2 unused)
// Pixel layout in word: [unused:2][p4:6][p3:6][p2:6][p1:6][p0:6]
// Pixel format: [R:2][G:2][B:2]

#define RGB222_PIXELS_PER_WORD 5
#define RGB222_BITS_PER_PIXEL 6

// Global mask and shift tables
static const uint32_t mask222[5] = {0x3F, 0xFC0, 0x3F000, 0xFC0000, 0x3F000000};
static const uint32_t invmask222[5] = {0xFFFFFFC0, 0xFFFFF03F, 0xFFFC0FFF, 0xFF03FFFF, 0xC0FFFFFF};
static const unsigned char pixel_shifts[5] = {0, 6, 12, 18, 24};

// Inline function to convert RGB888 to RGB222
static inline unsigned char RGB222(int c)
{
    return ((c >> 22) & 0x03) << 4 | // Red: bits 23-22 -> bits 5-4
           ((c >> 14) & 0x03) << 2 | // Green: bits 15-14 -> bits 3-2
           ((c >> 6) & 0x03);        // Blue: bits 7-6 -> bits 1-0
}

void DrawPixel222(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= HRes || y >= VRes)
        return;

    unsigned char colour = RGB222(c);
    int word_idx = y * (HRes / RGB222_PIXELS_PER_WORD) + (x / RGB222_PIXELS_PER_WORD);
    int pixel_pos = x % RGB222_PIXELS_PER_WORD;
    uint32_t *p = (uint32_t *)ScreenBuffer + word_idx;

    *p = (*p & invmask222[pixel_pos]) | ((uint32_t)colour << pixel_shifts[pixel_pos]);
}

void DrawRectangle222(int x1, int y1, int x2, int y2, int c)
{
    int y, t;
    unsigned char colour = RGB222(c);

    // Clamp coordinates
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

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;

    // Precompute solid word pattern (all 5 pixels same color)
    uint32_t solid_word = (uint32_t)colour |
                          ((uint32_t)colour << 6) |
                          ((uint32_t)colour << 12) |
                          ((uint32_t)colour << 18) |
                          ((uint32_t)colour << 24);

    for (y = y1; y <= y2; y++)
    {
        int start_word = x1 / RGB222_PIXELS_PER_WORD;
        int end_word = x2 / RGB222_PIXELS_PER_WORD;
        int start_pixel = x1 % RGB222_PIXELS_PER_WORD;
        int end_pixel = x2 % RGB222_PIXELS_PER_WORD;

        uint32_t *p = (uint32_t *)ScreenBuffer + y * words_per_line + start_word;

        if (start_word == end_word)
        {
            // All pixels in same word - build composite mask
            uint32_t composite_mask = 0;
            for (int px = start_pixel; px <= end_pixel; px++)
                composite_mask |= mask222[px];
            *p = (*p & ~composite_mask) | (solid_word & composite_mask);
        }
        else
        {
            // Handle first partial word
            if (start_pixel != 0)
            {
                uint32_t first_mask = 0;
                for (int px = start_pixel; px < RGB222_PIXELS_PER_WORD; px++)
                    first_mask |= mask222[px];
                *p = (*p & ~first_mask) | (solid_word & first_mask);
                p++;
                start_word++;
            }

            // Fill complete words
            int full_words = end_word - start_word;
            for (int i = 0; i < full_words; i++)
                *p++ = solid_word;

            // Handle last partial word
            uint32_t last_mask = 0;
            for (int px = 0; px <= end_pixel; px++)
                last_mask |= mask222[px];
            *p = (*p & ~last_mask) | (solid_word & last_mask);
        }
    }
}

void DrawBitmap222(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    int i, j, k, m, x, y;

    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0)
        return;

    unsigned char fcolour = RGB222(fc);
    unsigned char bcolour = RGB222(bc);

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;

    // Precompute shifted color values
    uint32_t fcolour_shifted[5], bcolour_shifted[5];
    for (int p = 0; p < 5; p++)
    {
        fcolour_shifted[p] = (uint32_t)fcolour << pixel_shifts[p];
        bcolour_shifted[p] = (uint32_t)bcolour << pixel_shifts[p];
    }

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < scale; j++)
        {
            for (k = 0; k < width; k++)
            {
                int bit_val = (bitmap[((i * width) + k) / 8] >>
                               (((height * width) - ((i * width) + k) - 1) % 8)) &
                              1;

                if (bc < 0 && !bit_val)
                    continue; // Skip background if transparent

                uint32_t *colour_shifted = bit_val ? fcolour_shifted : bcolour_shifted;

                for (m = 0; m < scale; m++)
                {
                    x = x1 + k * scale + m;
                    y = y1 + i * scale + j;

                    if (x >= 0 && x < HRes && y >= 0 && y < VRes)
                    {
                        int word_idx = y * words_per_line + (x / RGB222_PIXELS_PER_WORD);
                        int pixel_pos = x % RGB222_PIXELS_PER_WORD;
                        uint32_t *p = (uint32_t *)ScreenBuffer + word_idx;

                        *p = (*p & invmask222[pixel_pos]) | colour_shifted[pixel_pos];
                    }
                }
            }
        }
    }
}

void ScrollLCD222(int lines)
{
    if (lines == 0)
        return;

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;
    uint32_t *buf = (uint32_t *)ScreenBuffer;

    if (lines >= 0)
    {
        // Scroll up
        int words_to_copy = (VRes - lines) * words_per_line;
        uint32_t *dst = buf;
        uint32_t *src = buf + lines * words_per_line;

        // Fast word copy
        for (int i = 0; i < words_to_copy; i++)
            *dst++ = *src++;

        DrawRectangle222(0, VRes - lines, HRes - 1, VRes - 1, PromptBC);
    }
    else
    {
        // Scroll down
        lines = -lines;
        int words_to_copy = (VRes - lines) * words_per_line;
        uint32_t *dst = buf + (VRes - 1) * words_per_line;
        uint32_t *src = buf + (VRes - 1 - lines) * words_per_line;

        // Fast word copy backwards
        for (int i = 0; i < words_to_copy; i++)
            *dst-- = *src--;

        DrawRectangle222(0, 0, HRes - 1, lines - 1, PromptBC);
    }
}

void DrawBuffer222(int x1, int y1, int x2, int y2, unsigned char *p)
{
    int x, y, t;
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;

    // Clamp and swap coordinates
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

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;

    for (y = y1; y <= y2; y++)
    {
        int word_idx = y * words_per_line + (x1 / RGB222_PIXELS_PER_WORD);
        int pixel_pos = x1 % RGB222_PIXELS_PER_WORD;
        uint32_t *pp = (uint32_t *)ScreenBuffer + word_idx;

        for (x = x1; x <= x2; x++)
        {
            c.rgbbytes[0] = *p++;
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;

            unsigned char colour = RGB222(c.rgb);
            *pp = (*pp & invmask222[pixel_pos]) | ((uint32_t)colour << pixel_shifts[pixel_pos]);

            // Move to next pixel position
            if (++pixel_pos >= RGB222_PIXELS_PER_WORD)
            {
                pixel_pos = 0;
                pp++;
            }
        }
    }
}

void DrawBuffer222Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p)
{
    int x, y, t;

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

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;
    int nibble_toggle = 0;

    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                unsigned char colour;
                if (nibble_toggle)
                {
                    colour = (*p++ >> 4) & 0x0F;
                }
                else
                {
                    colour = *p & 0x0F;
                }

                // Expand 4-bit to 6-bit (RGB222)
                colour = ((colour & 0x0C) << 2) | // RR from upper 2 bits
                         ((colour & 0x03) << 2) | // GG from lower 2 bits
                         (colour & 0x03);         // BB from lower 2 bits

                int word_idx = y * words_per_line + (x / RGB222_PIXELS_PER_WORD);
                int pixel_pos = x % RGB222_PIXELS_PER_WORD;
                uint32_t *pp = (uint32_t *)ScreenBuffer + word_idx;

                if (colour != sprite_transparent || blank == -1)
                {
                    *pp = (*pp & invmask222[pixel_pos]) | ((uint32_t)colour << pixel_shifts[pixel_pos]);
                }
                // else keep old pixel (transparent)

                nibble_toggle = !nibble_toggle;
            }
            else
            {
                if (nibble_toggle)
                    p++;
                nibble_toggle = !nibble_toggle;
            }
        }
    }
}

void ReadBuffer222(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;

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

    int xx1 = (x1 < 0) ? 0 : (x1 >= HRes) ? HRes - 1
                                          : x1;
    int xx2 = (x2 < 0) ? 0 : (x2 >= HRes) ? HRes - 1
                                          : x2;
    int yy1 = (y1 < 0) ? 0 : (y1 >= VRes) ? VRes - 1
                                          : y1;
    int yy2 = (y2 < 0) ? 0 : (y2 >= VRes) ? VRes - 1
                                          : y2;

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;

    // Precompute colour expansion table (6-bit RGB222 to 24-bit RGB888)
    static uint32_t colour_table[64];
    static int table_initialized = 0;
    if (!table_initialized)
    {
        for (int i = 0; i < 64; i++)
        {
            int r = ((i >> 4) & 0x03) * 85; // 0,85,170,255
            int g = ((i >> 2) & 0x03) * 85;
            int b = (i & 0x03) * 85;
            colour_table[i] = (r << 16) | (g << 8) | b;
        }
        table_initialized = 1;
    }

    for (y = yy1; y <= yy2; y++)
    {
        int word_idx = y * words_per_line + (xx1 / RGB222_PIXELS_PER_WORD);
        int pixel_pos = xx1 % RGB222_PIXELS_PER_WORD;
        uint32_t *pp = (uint32_t *)ScreenBuffer + word_idx;
        uint32_t word = *pp;

        for (x = xx1; x <= xx2; x++)
        {
            unsigned char pixel = (word & mask222[pixel_pos]) >> pixel_shifts[pixel_pos];

            uint32_t rgb = colour_table[pixel];
            *c++ = rgb & 0xFF;
            *c++ = (rgb >> 8) & 0xFF;
            *c++ = (rgb >> 16) & 0xFF;

            // Move to next pixel
            if (++pixel_pos >= RGB222_PIXELS_PER_WORD)
            {
                pixel_pos = 0;
                pp++;
                word = *pp;
            }
        }
    }
}

void ReadBuffer222Fast(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    int nibble_toggle = 0;

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

    int words_per_line = HRes / RGB222_PIXELS_PER_WORD;

    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            unsigned char pixel_val = 0;

            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                int word_idx = y * words_per_line + (x / RGB222_PIXELS_PER_WORD);
                int pixel_pos = x % RGB222_PIXELS_PER_WORD;
                uint32_t *pp = (uint32_t *)ScreenBuffer + word_idx;

                unsigned char pixel6 = (*pp & mask222[pixel_pos]) >> pixel_shifts[pixel_pos];

                // Convert 6-bit to 4-bit (lose 1 bit of color depth)
                pixel_val = ((pixel6 >> 2) & 0x0C) | (pixel6 & 0x03);
            }

            if (nibble_toggle)
            {
                *c++ |= (pixel_val << 4);
            }
            else
            {
                *c = pixel_val;
            }
            nibble_toggle = !nibble_toggle;
        }
    }
}
void MIPS16 InitDisplay222(void)
{
    if (!(Option.DISPLAY_TYPE >= VGA222 && Option.DISPLAY_TYPE < NEXTGEN))
        return;
    DisplayHRes = display_details[Option.DISPLAY_TYPE].horizontal;
    DisplayVRes = display_details[Option.DISPLAY_TYPE].vertical;
    DrawRectangle = DrawRectangle222;
    DrawBitmap = DrawBitmap222;
    DrawBuffer = DrawBuffer222;
    ReadBuffer = ReadBuffer222;
    DrawBLITBuffer = DrawBuffer222;
    ReadBLITBuffer = ReadBuffer222;
    ScrollLCD = ScrollLCD222;
    DrawPixel = DrawPixel222;
    HRes = DisplayHRes;
    VRes = DisplayVRes;
    return;
}
void MIPS16 ConfigDisplay222(unsigned char *p)
{
    getcsargs(&p, 13);
    if (checkstring(argv[0], (unsigned char *)"VGA222_640x480") || checkstring(argv[0], (unsigned char *)"VGA222_640"))
    {
        DISPLAY_TYPE = VGA222;
    }
    else if (checkstring(argv[0], (unsigned char *)"VGA222_320x240") || checkstring(argv[0], (unsigned char *)"VGA222_320"))
    {
        DISPLAY_TYPE = VGA222X320;
    }
    else if (checkstring(argv[0], (unsigned char *)"VGA222_720x400") || checkstring(argv[0], (unsigned char *)"VGA222_720"))
    {
        DISPLAY_TYPE = VGA222X720;
    }
    else if (checkstring(argv[0], (unsigned char *)"VGA222_360x200") || checkstring(argv[0], (unsigned char *)"VGA222_360"))
    {
        DISPLAY_TYPE = VGA222X400;
    }
    else
        return;
    int hspin, datapin, code;
    Option.DISPLAY_TYPE = DISPLAY_TYPE;
    Option.DISPLAY_ORIENTATION = LANDSCAPE;
    if (!(code = codecheck(argv[2])))
        argv[2] += 2;
    hspin = getinteger(argv[2]);
    if (!code)
        hspin = codemap(hspin);
    if (!(code = codecheck(argv[4])))
        argv[4] += 2;
    datapin = getinteger(argv[4]);
    if (!code)
        datapin = codemap(datapin);
    CheckPin(hspin, CP_IGNORE_INUSE);
    CheckPin(datapin, CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[hspin].GPno + 1], CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[datapin].GPno + 1], CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[datapin].GPno + 2], CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[datapin].GPno + 3], CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[datapin].GPno + 4], CP_IGNORE_INUSE);
    CheckPin(PINMAP[PinDef[datapin].GPno + 5], CP_IGNORE_INUSE);
    Option.VGA_HSYNC = hspin;
    Option.VGA_BLUE = datapin;
    Option.CPU_Speed = DISPLAY_TYPE <= VGA222X320 ? 252000 : 283200;
    Option.DISPLAY_TYPE = DISPLAY_TYPE;
    return;
}
void blit222(uint32_t *source, uint32_t *destination, int xsource, int ysource,
             int width, int height, int xdestination, int ydestination, int missingcolour)
{
    // RGB222: 6 bits per pixel, 5 pixels per uint32_t (30 bits used, 2 bits unused)
    // Pixel layout in uint32_t (little endian):
    // Bits 0-5: pixel 0, Bits 6-11: pixel 1, Bits 12-17: pixel 2,
    // Bits 18-23: pixel 3, Bits 24-29: pixel 4, Bits 30-31: unused

    // Check if we have a valid missing colour (transparency)
    int has_transparency = (missingcolour >= 0 && missingcolour <= 63);

    // Determine clipping boundaries for destination
    int start_x = 0;
    int start_y = 0;
    int end_x = width;
    int end_y = height;

    // Clip to destination framebuffer bounds
    if (xdestination < 0)
    {
        start_x = -xdestination;
    }
    if (ydestination < 0)
    {
        start_y = -ydestination;
    }
    if (xdestination + width > HResD)
    {
        end_x = HResD - xdestination;
    }
    if (ydestination + height > VResD)
    {
        end_y = VResD - ydestination;
    }

    // Nothing to draw if completely clipped
    if (start_x >= end_x || start_y >= end_y)
    {
        return;
    }

    int src_stride = (HResS + 4) / 5; // uint32_t words per row in source
    int dst_stride = (HResD + 4) / 5; // uint32_t words per row in destination

    // Check for whole-word alignment (both positions divisible by 5, no transparency)
    int src_start_x = xsource + start_x;
    int dst_start_x = xdestination + start_x;
    int num_pixels = end_x - start_x;

    bool word_aligned = ((src_start_x % 5) == (dst_start_x % 5)) && !has_transparency;

    if (word_aligned && (src_start_x % 5 == 0) && (num_pixels % 5 == 0))
    {
        // Fast path: perfectly aligned, copy complete uint32_t words
        int src_word_start = src_start_x / 5;
        int dst_word_start = dst_start_x / 5;
        int num_words = num_pixels / 5;

        for (int y = start_y; y < end_y; y++)
        {
            uint32_t *src_row = source + (ysource + y) * src_stride;
            uint32_t *dst_row = destination + (ydestination + y) * dst_stride;

            memcpy(dst_row + dst_word_start, src_row + src_word_start, num_words * sizeof(uint32_t));
        }
    }
    else
    {
        // Pixel-by-pixel copy for all other cases
        for (int y = start_y; y < end_y; y++)
        {
            for (int x = start_x; x < end_x; x++)
            {
                int src_x = xsource + x;
                int src_y = ysource + y;
                int dst_x = xdestination + x;
                int dst_y = ydestination + y;

                // Get source pixel
                int src_word_idx = src_y * src_stride + (src_x / 5);
                int src_pixel_pos = src_x % 5;
                uint32_t src_word = source[src_word_idx];
                uint8_t src_pixel = (src_word >> (src_pixel_pos * 6)) & 0x3F;

                // Check transparency
                if (has_transparency && src_pixel == missingcolour)
                {
                    continue; // Skip this pixel
                }

                // Write to destination
                int dst_word_idx = dst_y * dst_stride + (dst_x / 5);
                int dst_pixel_pos = dst_x % 5;
                uint32_t mask = ~(0x3F << (dst_pixel_pos * 6));

                destination[dst_word_idx] = (destination[dst_word_idx] & mask) |
                                            ((uint32_t)src_pixel << (dst_pixel_pos * 6));
            }
        }
    }
}

void blit222_self(uint32_t *framebuffer, int xsource, int ysource,
                  int width, int height, int xdestination, int ydestination)
{
    // First, clip both source and destination to framebuffer bounds
    // Clip source
    if (xsource < 0)
    {
        width += xsource;
        xdestination -= xsource;
        xsource = 0;
    }
    if (ysource < 0)
    {
        height += ysource;
        ydestination -= ysource;
        ysource = 0;
    }
    if (xsource + width > HRes)
    {
        width = HRes - xsource;
    }
    if (ysource + height > VRes)
    {
        height = VRes - ysource;
    }

    // Clip destination
    if (xdestination < 0)
    {
        width += xdestination;
        xsource -= xdestination;
        xdestination = 0;
    }
    if (ydestination < 0)
    {
        height += ydestination;
        ysource -= ydestination;
        ydestination = 0;
    }
    if (xdestination + width > HRes)
    {
        width = HRes - xdestination;
    }
    if (ydestination + height > VRes)
    {
        height = VRes - ydestination;
    }

    // Check if anything left to copy
    if (width <= 0 || height <= 0)
    {
        return;
    }

    // Check if regions overlap
    bool overlap_x = (xsource < xdestination + width) && (xdestination < xsource + width);
    bool overlap_y = (ysource < ydestination + height) && (ydestination < ysource + height);
    bool overlap = overlap_x && overlap_y;

    // If no overlap, use the standard blit222 function
    if (!overlap)
    {
        blit222(framebuffer, framebuffer, xsource, ysource, width, height,
                xdestination, ydestination, -1);
        return;
    }

    int stride = (HRes + 4) / 5; // uint32_t words per row

    // Check if we can use memmove (same X positions, word-aligned, and copying full words)
    bool same_x = (xsource == xdestination);
    bool word_aligned = ((xsource % 5) == 0) && ((width % 5) == 0);

    if (same_x && word_aligned)
    {
        // Fast path: use memmove for entire rows
        int src_word_start = xsource / 5;
        int word_width = width / 5;

        if (ydestination > ysource)
        {
            // Copy from bottom to top
            for (int y = height - 1; y >= 0; y--)
            {
                int src_y = ysource + y;
                int dst_y = ydestination + y;
                memmove(framebuffer + dst_y * stride + src_word_start,
                        framebuffer + src_y * stride + src_word_start,
                        word_width * sizeof(uint32_t));
            }
        }
        else
        {
            // Copy from top to bottom
            for (int y = 0; y < height; y++)
            {
                int src_y = ysource + y;
                int dst_y = ydestination + y;
                memmove(framebuffer + dst_y * stride + src_word_start,
                        framebuffer + src_y * stride + src_word_start,
                        word_width * sizeof(uint32_t));
            }
        }
        return;
    }

    // Need to use line buffer for overlapping regions with different X or unaligned

    // Calculate word width needed for the source region
    int src_word_start = xsource / 5;
    int src_word_end = (xsource + width - 1) / 5;
    int buffer_words = src_word_end - src_word_start + 1;

    // Allocate a line buffer
    uint32_t *line_buffer = (uint32_t *)GetMemory(buffer_words * sizeof(uint32_t));
    if (!line_buffer)
    {
        return; // Allocation failed
    }

    // Determine copy direction based on overlap
    if (ydestination > ysource)
    {
        // Copy from bottom to top
        for (int y = height - 1; y >= 0; y--)
        {
            int src_y = ysource + y;
            int dst_y = ydestination + y;

            // Copy needed words from source row to line buffer
            memcpy(line_buffer, framebuffer + src_y * stride + src_word_start,
                   buffer_words * sizeof(uint32_t));

            // Now copy from line buffer to destination pixel by pixel
            for (int x = 0; x < width; x++)
            {
                int dst_x = xdestination + x;
                int actual_src_x = xsource + x;

                // Get source pixel from line buffer
                int buffer_word_idx = (actual_src_x / 5) - src_word_start;
                int src_pixel_pos = actual_src_x % 5;
                uint32_t src_word = line_buffer[buffer_word_idx];
                uint8_t src_pixel = (src_word >> (src_pixel_pos * 6)) & 0x3F;

                // Write to destination
                int dst_word_idx = dst_y * stride + (dst_x / 5);
                int dst_pixel_pos = dst_x % 5;
                uint32_t mask = ~(0x3F << (dst_pixel_pos * 6));

                framebuffer[dst_word_idx] = (framebuffer[dst_word_idx] & mask) |
                                            ((uint32_t)src_pixel << (dst_pixel_pos * 6));
            }
        }
    }
    else
    {
        // Copy from top to bottom (includes same Y position)
        for (int y = 0; y < height; y++)
        {
            int src_y = ysource + y;
            int dst_y = ydestination + y;

            // Copy needed words from source row to line buffer
            memcpy(line_buffer, framebuffer + src_y * stride + src_word_start,
                   buffer_words * sizeof(uint32_t));

            // Now copy from line buffer to destination pixel by pixel
            for (int x = 0; x < width; x++)
            {
                int dst_x = xdestination + x;
                int actual_src_x = xsource + x;

                // Get source pixel from line buffer
                int buffer_word_idx = (actual_src_x / 5) - src_word_start;
                int src_pixel_pos = actual_src_x % 5;
                uint32_t src_word = line_buffer[buffer_word_idx];
                uint8_t src_pixel = (src_word >> (src_pixel_pos * 6)) & 0x3F;

                // Write to destination
                int dst_word_idx = dst_y * stride + (dst_x / 5);
                int dst_pixel_pos = dst_x % 5;
                uint32_t mask = ~(0x3F << (dst_pixel_pos * 6));

                framebuffer[dst_word_idx] = (framebuffer[dst_word_idx] & mask) |
                                            ((uint32_t)src_pixel << (dst_pixel_pos * 6));
            }
        }
    }

    FreeMemory((void *)line_buffer);
}