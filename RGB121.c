/***********************************************************************************************************************
PicoMite MMBasic

RGB121.c

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
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "RGB121.h"
#include "stdint.h"
int HResD = 0;
int VResD = 0;
int HResS = 0;
int VResS = 0;
void blit121(uint8_t *source, uint8_t *destination, int xsource, int ysource,
             int width, int height, int xdestination, int ydestination, int missingcolour)
{

    // Check if we have a valid missing colour (transparency)
    int has_transparency = (missingcolour >= 0 && missingcolour <= 15);

    // Check for alignment optimization opportunity
    int aligned = ((xsource & 1) == (xdestination & 1)) && !has_transparency;

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

    int src_stride = (HResS + 1) >> 1; // bytes per row in source
    int dst_stride = (HResD + 1) >> 1; // bytes per row in destination

    if (aligned)
    {
        // Fast path: aligned copy with no transparency
        // Pixel packing: lower nibble = even pixel (left), upper nibble = odd pixel (right)

        int first_src_x = xsource + start_x;
        int first_dst_x = xdestination + start_x;
        int num_pixels = end_x - start_x;

        int starts_on_odd = first_src_x & 1;
        int pixel_count_is_odd = num_pixels & 1;

        for (int y = start_y; y < end_y; y++)
        {
            uint8_t *src_row = source + (ysource + y) * src_stride;
            uint8_t *dst_row = destination + (ydestination + y) * dst_stride;

            int src_byte = first_src_x >> 1;
            int dst_byte = first_dst_x >> 1;

            if (starts_on_odd)
            {
                // Cases 3 & 4: Odd to Odd
                // Step 1: Copy first upper nibble
                dst_row[dst_byte] = (dst_row[dst_byte] & 0x0F) | (src_row[src_byte] & 0xF0);
                src_byte++;
                dst_byte++;

                // Step 2: Calculate complete bytes to copy
                int complete_bytes;
                if (pixel_count_is_odd)
                {
                    // Case 4: Odd count - after first pixel, we have even pixels left, all complete bytes
                    complete_bytes = (num_pixels - 1) >> 1; // (num_pixels - 1) / 2
                }
                else
                {
                    // Case 3: Even count - after first pixel, we have odd pixels left
                    complete_bytes = (num_pixels - 1) >> 1; // One less than the total pairs
                }

                if (complete_bytes > 0)
                {
                    memcpy(dst_row + dst_byte, src_row + src_byte, complete_bytes);
                    src_byte += complete_bytes;
                    dst_byte += complete_bytes;
                }

                // Step 3: Handle last pixel if even count (last pixel is even = lower nibble)
                if (!pixel_count_is_odd)
                {
                    dst_row[dst_byte] = (dst_row[dst_byte] & 0xF0) | (src_row[src_byte] & 0x0F);
                }
            }
            else
            {
                // Cases 1 & 2: Even to Even
                int complete_bytes = num_pixels >> 1; // num_pixels / 2

                // Step 1: Copy complete bytes
                if (complete_bytes > 0)
                {
                    memcpy(dst_row + dst_byte, src_row + src_byte, complete_bytes);
                    src_byte += complete_bytes;
                    dst_byte += complete_bytes;
                }

                // Step 2: Handle last pixel if odd count (last pixel is even = lower nibble)
                if (pixel_count_is_odd)
                {
                    dst_row[dst_byte] = (dst_row[dst_byte] & 0xF0) | (src_row[src_byte] & 0x0F);
                }
            }
        }
    }
    else
    {
        // Slow path: pixel-by-pixel copy (misaligned) - Cases 5, 6, 7, 8
        // Pixel packing: lower nibble = even pixel (left), upper nibble = odd pixel (right)
        for (int y = start_y; y < end_y; y++)
        {
            for (int x = start_x; x < end_x; x++)
            {
                int src_x = xsource + x;
                int src_y = ysource + y;
                int dst_x = xdestination + x;
                int dst_y = ydestination + y;

                // Get source pixel
                int src_byte_idx = src_y * src_stride + (src_x >> 1);
                uint8_t src_byte = source[src_byte_idx];
                uint8_t src_pixel;

                if (src_x & 1)
                {
                    src_pixel = (src_byte >> 4) & 0x0F; // Odd pixel (upper nibble)
                }
                else
                {
                    src_pixel = src_byte & 0x0F; // Even pixel (lower nibble)
                }

                // Check transparency
                if (has_transparency && src_pixel == missingcolour)
                {
                    continue; // Skip this pixel
                }

                // Write to destination
                int dst_byte_idx = dst_y * dst_stride + (dst_x >> 1);

                if (dst_x & 1)
                {
                    // Odd pixel (upper nibble)
                    destination[dst_byte_idx] = (destination[dst_byte_idx] & 0x0F) | (src_pixel << 4);
                }
                else
                {
                    // Even pixel (lower nibble)
                    destination[dst_byte_idx] = (destination[dst_byte_idx] & 0xF0) | src_pixel;
                }
            }
        }
    }
}
void blit121_self(uint8_t *framebuffer, int xsource, int ysource,
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

    // If no overlap, use the standard blit121 function
    if (!overlap)
    {
        blit121(framebuffer, framebuffer, xsource, ysource, width, height,
                xdestination, ydestination, -1);
        return;
    }

    int stride = (HRes + 1) >> 1;

    // Check if we can use memmove (same X positions, aligned, and copying full bytes)
    bool same_x = (xsource == xdestination);
    bool aligned = ((xsource & 1) == 0) && ((width & 1) == 0); // Even start and even width

    if (same_x && aligned)
    {
        // Fast path: use memmove for entire rows
        int src_byte_start = xsource >> 1;
        int byte_width = width >> 1;

        if (ydestination > ysource)
        {
            // Copy from bottom to top
            for (int y = height - 1; y >= 0; y--)
            {
                int src_y = ysource + y;
                int dst_y = ydestination + y;
                memmove(framebuffer + dst_y * stride + src_byte_start,
                        framebuffer + src_y * stride + src_byte_start,
                        byte_width);
            }
        }
        else
        {
            // Copy from top to bottom
            for (int y = 0; y < height; y++)
            {
                int src_y = ysource + y;
                int dst_y = ydestination + y;
                memmove(framebuffer + dst_y * stride + src_byte_start,
                        framebuffer + src_y * stride + src_byte_start,
                        byte_width);
            }
        }
        return;
    }

    // Allocate a line buffer - one byte per pixel for simplicity
    uint8_t *line_buffer = (uint8_t *)GetMemory(width);
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

            // Read source pixels into line buffer (one byte per pixel)
            for (int x = 0; x < width; x++)
            {
                int src_x = xsource + x;
                int src_byte_idx = src_y * stride + (src_x >> 1);
                uint8_t src_byte = framebuffer[src_byte_idx];

                // Extract pixel: even x = low nibble, odd x = high nibble
                if (src_x & 1)
                {
                    line_buffer[x] = (src_byte >> 4) & 0x0F;
                }
                else
                {
                    line_buffer[x] = src_byte & 0x0F;
                }
            }

            // Write from line buffer to destination
            for (int x = 0; x < width; x++)
            {
                int dst_x = xdestination + x;
                int dst_byte_idx = dst_y * stride + (dst_x >> 1);
                uint8_t pixel = line_buffer[x];

                // Write pixel: even x = low nibble, odd x = high nibble
                if (dst_x & 1)
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0x0F) | (pixel << 4);
                }
                else
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0xF0) | pixel;
                }
            }
        }
    }
    else if (ydestination < ysource || xdestination >= xsource)
    {
        // Copy from top to bottom and/or left to right
        for (int y = 0; y < height; y++)
        {
            int src_y = ysource + y;
            int dst_y = ydestination + y;

            // Read source pixels into line buffer (one byte per pixel)
            for (int x = 0; x < width; x++)
            {
                int src_x = xsource + x;
                int src_byte_idx = src_y * stride + (src_x >> 1);
                uint8_t src_byte = framebuffer[src_byte_idx];

                // Extract pixel: even x = low nibble, odd x = high nibble
                if (src_x & 1)
                {
                    line_buffer[x] = (src_byte >> 4) & 0x0F;
                }
                else
                {
                    line_buffer[x] = src_byte & 0x0F;
                }
            }

            // Write from line buffer to destination
            for (int x = 0; x < width; x++)
            {
                int dst_x = xdestination + x;
                int dst_byte_idx = dst_y * stride + (dst_x >> 1);
                uint8_t pixel = line_buffer[x];

                // Write pixel: even x = low nibble, odd x = high nibble
                if (dst_x & 1)
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0x0F) | (pixel << 4);
                }
                else
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0xF0) | pixel;
                }
            }
        }
    }
    else
    {
        // Same Y position and destination is to the LEFT: copy right to left
        for (int y = 0; y < height; y++)
        {
            int src_y = ysource + y;
            int dst_y = ydestination + y;

            // Read source pixels into line buffer (one byte per pixel)
            for (int x = 0; x < width; x++)
            {
                int src_x = xsource + x;
                int src_byte_idx = src_y * stride + (src_x >> 1);
                uint8_t src_byte = framebuffer[src_byte_idx];

                // Extract pixel: even x = low nibble, odd x = high nibble
                if (src_x & 1)
                {
                    line_buffer[x] = (src_byte >> 4) & 0x0F;
                }
                else
                {
                    line_buffer[x] = src_byte & 0x0F;
                }
            }

            // Write from line buffer to destination (right to left)
            for (int x = width - 1; x >= 0; x--)
            {
                int dst_x = xdestination + x;
                int dst_byte_idx = dst_y * stride + (dst_x >> 1);
                uint8_t pixel = line_buffer[x];

                // Write pixel: even x = low nibble, odd x = high nibble
                if (dst_x & 1)
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0x0F) | (pixel << 4);
                }
                else
                {
                    framebuffer[dst_byte_idx] = (framebuffer[dst_byte_idx] & 0xF0) | pixel;
                }
            }
        }
    }

    FreeMemory(line_buffer);
}
void DrawPixel16(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= HRes || y >= VRes)
        return;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#endif
    unsigned char colour = RGB121(c);
    uint8_t *p = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 1)) + (x >> 1));
    if (x & 1)
    {
        *p &= 0x0F;
        *p |= (colour << 4);
    }
    else
    {
        *p &= 0xF0;
        *p |= colour;
    }
}
void MIPS32 DrawRectangle16(int x1, int y1, int x2, int y2, int c)
{
    int y, x1p, x2p, width_bytes;
    unsigned char colour = RGB121(c);
    unsigned char bcolour = (colour << 4) | colour;

#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#endif

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

    // Precompute row stride
    int row_stride = HRes >> 1;

    for (y = y1; y <= y2; y++)
    {
        x1p = x1;
        x2p = x2;
        uint8_t *p = WriteBuf + (y * row_stride) + (x1 >> 1);

        // Handle odd left edge
        if (x1 & 1)
        {
            *p = (*p & 0x0F) | (colour << 4);
            p++;
            x1p++;
        }

        // Handle even right edge
        if ((x2 & 1) == 0)
        {
            uint8_t *q = WriteBuf + (y * row_stride) + (x2 >> 1);
            *q = (*q & 0xF0) | colour;
            x2p--;
        }

        // Fill middle section
        width_bytes = ((x2p - x1p) >> 1) + 1;
        if (width_bytes > 0)
            memset(p, bcolour, width_bytes);
    }
}
void DrawBitmap16(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap)
{
    int i, j, k, m, x, y;
    //    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0)
        return;
    unsigned char fcolour = RGB121(fc);
    unsigned char bcolour = RGB121(bc);
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#endif
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
                        uint8_t *p = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 1)) + (x >> 1));
                        if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1)
                        {
                            if (x & 1)
                            {
                                *p &= 0x0F;
                                *p |= (fcolour << 4);
                            }
                            else
                            {
                                *p &= 0xF0;
                                *p |= fcolour;
                            }
                        }
                        else
                        {
                            if (bc >= 0)
                            {
                                if (x & 1)
                                {
                                    *p &= 0x0F;
                                    *p |= (bcolour << 4);
                                }
                                else
                                {
                                    *p &= 0xF0;
                                    *p |= bcolour;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void ScrollLCD16(int lines)
{
    if (lines == 0)
        return;
    if (lines >= 0)
    {
        for (int i = 0; i < VRes - lines; i++)
        {
            int d = i * (HRes >> 1), s = (i + lines) * (HRes >> 1);
            for (int c = 0; c < (HRes >> 1); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, VRes - lines, HRes - 1, VRes - 1, PromptBC); // erase the lines to be scrolled off
    }
    else
    {
        lines = -lines;
        for (int i = VRes - 1; i >= lines; i--)
        {
            int d = i * (HRes >> 1), s = (i - lines) * (HRes >> 1);
            for (int c = 0; c < (HRes >> 1); c++)
                WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
void DrawBuffer16(int x1, int y1, int x2, int y2, unsigned char *p)
{
    int x, y, t;
    union colourmap
    {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    unsigned char fcolour;
    uint8_t *pp;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
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
            c.rgbbytes[0] = *p++; // this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            fcolour = RGB121(c.rgb);
            pp = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 1)) + (x >> 1));
            if (x & 1)
            {
                *pp &= 0x0F;
                *pp |= (fcolour << 4);
            }
            else
            {
                *pp &= 0xF0;
                *pp |= fcolour;
            }
        }
    }
}
void DrawBuffer16Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p)
{
    int x, y, t, toggle = 0;
    unsigned char c, w;
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
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#endif
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                pp = (uint8_t *)(WriteBuf + (y * (HRes >> 1)) + (x >> 1));
                if (x & 1)
                {
                    w = *pp & 0xF0;
                    *pp &= 0x0F;
                    if (toggle)
                    {
                        c = ((*p++) & 0xF0);
                    }
                    else
                    {
                        c = (*p << 4);
                    }
                }
                else
                {
                    w = *pp & 0xF;
                    *pp &= 0xF0;
                    if (toggle)
                    {
                        c = ((*p++) >> 4);
                    }
                    else
                    {
                        c = (*p & 0xF);
                    }
                }
                if ((!(c == sprite_transparent || c == sprite_transparent << 4)) || blank == -1)
                    *pp |= c;
                else
                    *pp |= w;
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
void ReadBuffer16(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t;
    uint8_t *pp;
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
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
            pp = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 1)) + (x >> 1));
#ifdef PICOMITEVGA
            unsigned int q;
            uint8_t *qq = pp;
            if (WriteBuf == DisplayBuf && LayerBuf != DisplayBuf && LayerBuf != NULL)
                qq = (uint8_t *)(((uint32_t)LayerBuf) + (y * (HRes >> 1)) + (x >> 1));
#endif
            if (x & 1)
            {
                t = colours[(*pp) >> 4];
#ifdef PICOMITEVGA
                q = colours[(*qq) >> 4];
                if (!(((*qq) >> 4) == transparent) && mergedread)
                    t = q;
#endif
            }
            else
            {
                t = colours[(*pp) & 0x0F];
#ifdef PICOMITEVGA
                q = colours[(*qq) & 0x0F];
                if (!(((*qq) & 0x0F) == transparent) && mergedread)
                    t = q;
#endif
            }
            *c++ = (t & 0xFF);
            *c++ = (t >> 8) & 0xFF;
            *c++ = t >> 16;
        }
    }
}
void ReadBuffer16Fast(int x1, int y1, int x2, int y2, unsigned char *c)
{
    int x, y, t, toggle = 0;
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
#if PICOMITERP2350
    if ((Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE < VGA222) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#else
    if ((Option.DISPLAY_TYPE >= VIRTUAL) && WriteBuf == NULL)
        WriteBuf = GetMemory(VMaxH * VMaxV / 8);
#endif
    for (y = y1; y <= y2; y++)
    {
        for (x = x1; x <= x2; x++)
        {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes)
            {
                pp = (uint8_t *)(((uint32_t)WriteBuf) + (y * (HRes >> 1)) + (x >> 1));
                if (!(x & 1))
                {
                    if (toggle)
                        *c++ |= (((*pp) & 0x0F)) << 4;
                    else
                        *c = ((*pp) & 0x0F);
                }
                else
                {
                    if (toggle)
                        *c++ |= ((*pp) & 0xF0);
                    else
                        *c = ((*pp) >> 4);
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
