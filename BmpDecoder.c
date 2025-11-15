/*
 * @cond
 * The following section will be excluded from the documentation.
 */
#define __BMPDECODER_C__
/***********************************************************************************************************************
PicoMite MMBasic

BmpDecoder.c

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

// #include "GenericTypeDefs.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
//** SD CARD INCLUDES ***********************************************************
#include "ff.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// BMP file header structures
#pragma pack(push, 1)
typedef struct
{
        uint16_t bfType;      // Must be 'BM' (0x4D42)
        uint32_t bfSize;      // File size in bytes
        uint16_t bfReserved1; // Reserved, must be 0
        uint16_t bfReserved2; // Reserved, must be 0
        uint32_t bfOffBits;   // Offset to bitmap data
} BITMAPFILEHEADER;

typedef struct
{
        uint32_t biSize;         // Size of this header (40 bytes)
        int32_t biWidth;         // Width in pixels
        int32_t biHeight;        // Height in pixels (positive = bottom-up)
        uint16_t biPlanes;       // Must be 1
        uint16_t biBitCount;     // Bits per pixel (1, 4, 8, 16, 24, 32)
        uint32_t biCompression;  // Compression type (0 = uncompressed)
        uint32_t biSizeImage;    // Image size (may be 0 for uncompressed)
        int32_t biXPelsPerMeter; // Horizontal resolution
        int32_t biYPelsPerMeter; // Vertical resolution
        uint32_t biClrUsed;      // Number of colors in palette
        uint32_t biClrImportant; // Important colors (0 = all)
} BITMAPINFOHEADER;

typedef struct
{
        uint8_t rgbBlue;
        uint8_t rgbGreen;
        uint8_t rgbRed;
        uint8_t rgbReserved;
} RGBQUAD;
#pragma pack(pop)

// Compression types
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3

// Seek origins
#define SEEK_SET 0
#define SEEK_CUR 1

// Return structure

// Structure to hold line start positions for compressed formats
typedef struct
{
        long *positions; // Array of file positions for each line
        int count;       // Number of lines
} LineStartTable;
// External read function
size_t onBMPRead(char *pBufferOut, size_t bytesToRead)
{
        unsigned int nbr;
        FileGetData(BMPfnbr, pBufferOut, bytesToRead, &nbr);
        return nbr;
}
bool onBMPSeek(int offset, bool origin)
{
        if (filesource[BMPfnbr] == FATFSFILE)
        {
                if (origin == 0)
                        FSerror = f_lseek(FileTable[BMPfnbr].fptr, offset);
                else
                        FSerror = f_lseek(FileTable[BMPfnbr].fptr, FileTable[BMPfnbr].fptr->fptr + offset);
        }
        else
        {
                if (origin == 0)
                        FSerror = lfs_file_seek(&lfs, FileTable[BMPfnbr].lfsptr, offset, LFS_SEEK_SET);
                else
                        FSerror = lfs_file_seek(&lfs, FileTable[BMPfnbr].lfsptr, offset, LFS_SEEK_CUR);
        }
        return 1;
}

// Cleanup and error function
static void cleanupAndError(char *message, RGBQUAD **palette, uint8_t **rowBuffer,
                            uint32_t **lineData, LineStartTable **lineTable)
{
        if (palette)
                FreeMemorySafe((void **)palette);
        if (rowBuffer)
                FreeMemorySafe((void **)rowBuffer);
        if (lineData)
                FreeMemorySafe((void **)lineData);
        if (lineTable && *lineTable)
        {
                if ((*lineTable)->positions)
                {
                        FreeMemorySafe((void **)&((*lineTable)->positions));
                }
                FreeMemorySafe((void **)lineTable);
        }
        error(message);
        // This function never returns
}

// Helper function to build line start table for RLE compressed images
static LineStartTable *buildLineStartTable(int height, long dataStart)
{
        LineStartTable *table = (LineStartTable *)GetMemory(sizeof(LineStartTable));
        if (!table)
                return NULL;

        table->positions = (long *)GetMemory(height * sizeof(long));
        if (!table->positions)
        {
                FreeMemorySafe((void **)&table);
                return NULL;
        }
        table->count = height;

        // Seek to start of pixel data
        onBMPSeek(dataStart, SEEK_SET);

        int currentLine = 0;
        bool done = false;

        // Scan through RLE data and record line start positions
        while (!done && currentLine < height)
        {
                // Record current file position as start of this line
                table->positions[currentLine] = dataStart;

                // Scan through this line to find its end
                while (1)
                {
                        uint8_t count, value;
                        //            long currentPos;

                        // Remember position before reading
                        if (onBMPRead((char *)&count, 1) != 1)
                        {
                                FreeMemorySafe((void **)&(table->positions));
                                FreeMemorySafe((void **)&table);
                                return NULL;
                        }
                        dataStart += 1;

                        if (count == 0)
                        {
                                // Escape code
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;

                                if (value == 0)
                                {
                                        // End of line
                                        currentLine++;
                                        break;
                                }
                                else if (value == 1)
                                {
                                        // End of bitmap
                                        done = true;
                                        break;
                                }
                                else if (value == 2)
                                {
                                        // Delta
                                        uint8_t dx, dy;
                                        if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        {
                                                FreeMemorySafe((void **)&(table->positions));
                                                FreeMemorySafe((void **)&table);
                                                return NULL;
                                        }
                                        dataStart += 2;
                                        if (dy > 0)
                                        {
                                                currentLine += dy;
                                                break;
                                        }
                                }
                                else
                                {
                                        // Absolute mode
                                        int bytesToRead = value;
                                        int padding = bytesToRead & 1; // Pad to word boundary

                                        // Skip the data
                                        for (int i = 0; i < bytesToRead + padding; i++)
                                        {
                                                uint8_t dummy;
                                                if (onBMPRead((char *)&dummy, 1) != 1)
                                                {
                                                        FreeMemorySafe((void **)&(table->positions));
                                                        FreeMemorySafe((void **)&table);
                                                        return NULL;
                                                }
                                                dataStart += 1;
                                        }
                                }
                        }
                        else
                        {
                                // Encoded mode - skip value byte
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;
                        }
                }
        }

        return table;
}

// Helper function to build line start table for RLE4
static LineStartTable *buildLineStartTableRLE4(int height, long dataStart)
{
        LineStartTable *table = (LineStartTable *)GetMemory(sizeof(LineStartTable));
        if (!table)
                return NULL;

        table->positions = (long *)GetMemory(height * sizeof(long));
        if (!table->positions)
        {
                FreeMemorySafe((void **)&table);
                return NULL;
        }
        table->count = height;

        // Seek to start of pixel data
        onBMPSeek(dataStart, SEEK_SET);

        int currentLine = 0;
        bool done = false;

        // Scan through RLE data and record line start positions
        while (!done && currentLine < height)
        {
                // Record current file position as start of this line
                table->positions[currentLine] = dataStart;

                // Scan through this line to find its end
                while (1)
                {
                        uint8_t count, value;

                        if (onBMPRead((char *)&count, 1) != 1)
                        {
                                FreeMemorySafe((void **)&(table->positions));
                                FreeMemorySafe((void **)&table);
                                return NULL;
                        }
                        dataStart += 1;

                        if (count == 0)
                        {
                                // Escape code
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;

                                if (value == 0)
                                {
                                        // End of line
                                        currentLine++;
                                        break;
                                }
                                else if (value == 1)
                                {
                                        // End of bitmap
                                        done = true;
                                        break;
                                }
                                else if (value == 2)
                                {
                                        // Delta
                                        uint8_t dx, dy;
                                        if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        {
                                                FreeMemorySafe((void **)&(table->positions));
                                                FreeMemorySafe((void **)&table);
                                                return NULL;
                                        }
                                        dataStart += 2;
                                        if (dy > 0)
                                        {
                                                currentLine += dy;
                                                break;
                                        }
                                }
                                else
                                {
                                        // Absolute mode - pixels packed 2 per byte
                                        int bytesToRead = (value + 1) / 2;
                                        int padding = bytesToRead & 1; // Pad to word boundary

                                        // Skip the data
                                        for (int i = 0; i < bytesToRead + padding; i++)
                                        {
                                                uint8_t dummy;
                                                if (onBMPRead((char *)&dummy, 1) != 1)
                                                {
                                                        FreeMemorySafe((void **)&(table->positions));
                                                        FreeMemorySafe((void **)&table);
                                                        return NULL;
                                                }
                                                dataStart += 1;
                                        }
                                }
                        }
                        else
                        {
                                // Encoded mode - skip value byte
                                if (onBMPRead((char *)&value, 1) != 1)
                                {
                                        FreeMemorySafe((void **)&(table->positions));
                                        FreeMemorySafe((void **)&table);
                                        return NULL;
                                }
                                dataStart += 1;
                        }
                }
        }

        return table;
}

// Helper function to decode a single RLE8 line
static bool decodeRLE8Line(RGBQUAD *palette, int paletteSize, int width,
                           uint32_t *lineData, uint8_t *rowBuffer)
{
        memset(rowBuffer, 0, width);
        int x = 0;
        bool lineComplete = false;

        while (!lineComplete)
        {
                uint8_t count, value;

                if (onBMPRead((char *)&count, 1) != 1)
                        return false;

                if (count == 0)
                {
                        // Escape code
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        if (value == 0)
                        {
                                // End of line
                                lineComplete = true;
                        }
                        else if (value == 1)
                        {
                                // End of bitmap
                                lineComplete = true;
                        }
                        else if (value == 2)
                        {
                                // Delta - skip
                                uint8_t dx, dy;
                                if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        return false;
                                x += dx;
                        }
                        else
                        {
                                // Absolute mode
                                for (int i = 0; i < value && x < width; i++, x++)
                                {
                                        uint8_t pixel;
                                        if (onBMPRead((char *)&pixel, 1) != 1)
                                                return false;
                                        rowBuffer[x] = pixel;
                                }
                                // Pad to word boundary
                                if (value & 1)
                                {
                                        uint8_t dummy;
                                        onBMPRead((char *)&dummy, 1);
                                }
                        }
                }
                else
                {
                        // Encoded mode
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;
                        for (int i = 0; i < count && x < width; i++, x++)
                        {
                                rowBuffer[x] = value;
                        }
                }
        }

        // Convert to RGB888
        for (int col = 0; col < width; col++)
        {
                uint8_t index = rowBuffer[col];
                if (index < paletteSize)
                {
                        lineData[col] = (palette[index].rgbRed << 16) |
                                        (palette[index].rgbGreen << 8) |
                                        palette[index].rgbBlue;
                }
                else
                {
                        lineData[col] = 0;
                }
        }

        return true;
}

// Helper function to decode a single RLE4 line
static bool decodeRLE4Line(RGBQUAD *palette, int paletteSize, int width,
                           uint32_t *lineData, uint8_t *rowBuffer)
{
        memset(rowBuffer, 0, width);
        int x = 0;
        bool lineComplete = false;

        while (!lineComplete)
        {
                uint8_t count, value;

                if (onBMPRead((char *)&count, 1) != 1)
                        return false;

                if (count == 0)
                {
                        // Escape code
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        if (value == 0)
                        {
                                // End of line
                                lineComplete = true;
                        }
                        else if (value == 1)
                        {
                                // End of bitmap
                                lineComplete = true;
                        }
                        else if (value == 2)
                        {
                                // Delta
                                uint8_t dx, dy;
                                if (onBMPRead((char *)&dx, 1) != 1 || onBMPRead((char *)&dy, 1) != 1)
                                        return false;
                                x += dx;
                        }
                        else
                        {
                                // Absolute mode
                                int pixelsToRead = value;
                                int bytesToRead = (pixelsToRead + 1) / 2;

                                for (int i = 0; i < bytesToRead; i++)
                                {
                                        uint8_t byte;
                                        if (onBMPRead((char *)&byte, 1) != 1)
                                                return false;

                                        if (x < width)
                                        {
                                                rowBuffer[x++] = (byte >> 4) & 0x0F;
                                        }
                                        if (pixelsToRead > 1 && x < width)
                                        {
                                                rowBuffer[x++] = byte & 0x0F;
                                        }
                                        pixelsToRead -= 2;
                                }

                                // Pad to word boundary
                                if (((value + 1) / 2) & 1)
                                {
                                        uint8_t dummy;
                                        onBMPRead((char *)&dummy, 1);
                                }
                        }
                }
                else
                {
                        // Encoded mode
                        if (onBMPRead((char *)&value, 1) != 1)
                                return false;

                        uint8_t pixel1 = (value >> 4) & 0x0F;
                        uint8_t pixel2 = value & 0x0F;

                        for (int i = 0; i < count && x < width; i++)
                        {
                                rowBuffer[x++] = (i & 1) ? pixel2 : pixel1;
                        }
                }
        }

        // Convert to RGB888
        for (int col = 0; col < width; col++)
        {
                uint8_t index = rowBuffer[col];
                if (index < paletteSize)
                {
                        lineData[col] = (palette[index].rgbRed << 16) |
                                        (palette[index].rgbGreen << 8) |
                                        palette[index].rgbBlue;
                }
                else
                {
                        lineData[col] = 0;
                }
        }

        return true;
}
void decodeBMPheader(int *width, int *height)
{
        BITMAPFILEHEADER fileHeader;
        BITMAPINFOHEADER infoHeader;

        // Set defaults in case of error
        *width = 0;
        *height = 0;

        // Read file header
        if (onBMPRead((char *)&fileHeader, sizeof(BITMAPFILEHEADER)) != sizeof(BITMAPFILEHEADER))
        {
                return;
        }

        // Check BMP signature
        if (fileHeader.bfType != 0x4D42)
        { // 'BM'
                return;
        }

        // Read info header
        if (onBMPRead((char *)&infoHeader, sizeof(BITMAPINFOHEADER)) != sizeof(BITMAPINFOHEADER))
        {
                return;
        }

        // Return dimensions
        *width = infoHeader.biWidth;
        *height = abs(infoHeader.biHeight);
        // Seek back to start of file for decodeBMP
        onBMPSeek(0, 0);
}
BMP_Result decodeBMP(bool topdown)
{
        BMP_Result result = {0};
        BITMAPFILEHEADER fileHeader;
        BITMAPINFOHEADER infoHeader;
        RGBQUAD *palette = NULL;
        uint8_t *rowBuffer = NULL;
        uint32_t *lineData = NULL;
        LineStartTable *lineTable = NULL;
        int col;
        int bottomUp;
        int rowSize;
        int paletteSize = 0;
        long pixelDataStart;

        // Read file header
        if (onBMPRead((char *)&fileHeader, sizeof(BITMAPFILEHEADER)) != sizeof(BITMAPFILEHEADER))
        {
                cleanupAndError("Failed to read file header", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Check BMP signature
        if (fileHeader.bfType != 0x4D42)
        { // 'BM'
                cleanupAndError("Not a valid BMP file (missing BM signature)", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Read info header
        if (onBMPRead((char *)&infoHeader, sizeof(BITMAPINFOHEADER)) != sizeof(BITMAPINFOHEADER))
        {
                cleanupAndError("Failed to read info header", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Validate header
        if (infoHeader.biSize != 40)
        {
                cleanupAndError("Unsupported BMP header format", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Check compression and bit depth combinations
        if (infoHeader.biCompression == BI_RLE8 && infoHeader.biBitCount != 8)
        {
                cleanupAndError("RLE8 compression requires 8-bit color", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biCompression == BI_RLE4 && infoHeader.biBitCount != 4)
        {
                cleanupAndError("RLE4 compression requires 4-bit color", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biCompression != BI_RGB &&
            infoHeader.biCompression != BI_RLE8 &&
            infoHeader.biCompression != BI_RLE4)
        {
                cleanupAndError("Unsupported compression format", &palette, &rowBuffer, &lineData, &lineTable);
        }

        if (infoHeader.biBitCount != 1 && infoHeader.biBitCount != 4 &&
            infoHeader.biBitCount != 8 && infoHeader.biBitCount != 16 &&
            infoHeader.biBitCount != 24)
        {
                cleanupAndError("Unsupported bit depth", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Set result dimensions
        result.width = infoHeader.biWidth;
        result.height = abs(infoHeader.biHeight);
        result.bitsPerPixel = infoHeader.biBitCount;
        bottomUp = (infoHeader.biHeight > 0);

        // Read palette if needed (1-bit, 4-bit and 8-bit)
        if (infoHeader.biBitCount <= 8)
        {
                paletteSize = infoHeader.biClrUsed;
                if (paletteSize == 0)
                {
                        paletteSize = 1 << infoHeader.biBitCount; // 2 for 1-bit, 16 for 4-bit, 256 for 8-bit
                }

                palette = (RGBQUAD *)GetMemory(paletteSize * sizeof(RGBQUAD));
                if (!palette)
                {
                        cleanupAndError("Failed to allocate palette", &palette, &rowBuffer, &lineData, &lineTable);
                }

                if (onBMPRead((char *)palette, paletteSize * sizeof(RGBQUAD)) !=
                    paletteSize * sizeof(RGBQUAD))
                {
                        cleanupAndError("Failed to read palette", &palette, &rowBuffer, &lineData, &lineTable);
                }
        }

        // Allocate line data buffer for callback (RGB888 packed into uint32_t)
        lineData = (uint32_t *)GetMemory(result.width * sizeof(uint32_t));
        if (!lineData)
        {
                cleanupAndError("Failed to allocate line data buffer", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Calculate pixel data start position
        pixelDataStart = fileHeader.bfOffBits;

        // Seek to pixel data (skip any additional headers/data)
        size_t bytesRead = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        if (palette)
                bytesRead += paletteSize * sizeof(RGBQUAD);

        while (bytesRead < fileHeader.bfOffBits)
        {
                uint8_t dummy;
                onBMPRead((char *)&dummy, 1);
                bytesRead++;
        }

        // Handle RLE compressed formats
        if (infoHeader.biCompression == BI_RLE8 || infoHeader.biCompression == BI_RLE4)
        {
                // Build line start table for random access
                if (infoHeader.biCompression == BI_RLE8)
                {
                        lineTable = buildLineStartTable(result.height, pixelDataStart);
                }
                else
                {
                        lineTable = buildLineStartTableRLE4(result.height, pixelDataStart);
                }

                if (!lineTable)
                {
                        cleanupAndError("Failed to build line start table", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Allocate row buffer for RLE decoding
                rowBuffer = (uint8_t *)GetMemory(result.width);
                if (!rowBuffer)
                {
                        cleanupAndError("Failed to allocate row buffer", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Process lines
                for (int i = 0; i < result.height; i++)
                {
                        // Determine which file line to read and which screen row to report
                        int fileRow;
                        int screenRow;

                        if (topdown)
                        {
                                // topdown=true: read backwards through file
                                fileRow = result.height - 1 - i;
                                screenRow = i; // Report screen rows 0, 1, 2, ...
                        }
                        else
                        {
                                // topdown=false: read sequentially (efficient)
                                fileRow = i;
                                // RLE is bottom-up: file line 0 = image bottom → screen row 479
                                screenRow = result.height - 1 - i;
                        }

                        // Seek to start of this line
                        if (fileRow < lineTable->count && fileRow >= 0)
                        {
                                onBMPSeek(lineTable->positions[fileRow], SEEK_SET);

                                // Decode the line
                                bool success;
                                if (infoHeader.biCompression == BI_RLE8)
                                {
                                        success = decodeRLE8Line(palette, paletteSize, result.width, lineData, rowBuffer);
                                }
                                else
                                {
                                        success = decodeRLE4Line(palette, paletteSize, result.width, lineData, rowBuffer);
                                }

                                if (!success)
                                {
                                        cleanupAndError("Failed to decode RLE line", &palette, &rowBuffer, &lineData, &lineTable);
                                }

                                // Call callback with screen row position
                                if (!linecallback(&result.width, &result.height, lineData, &screenRow))
                                {
                                        result.linesProcessed = i;
                                        FreeMemorySafe((void **)&rowBuffer);
                                        FreeMemorySafe((void **)&lineData);
                                        FreeMemorySafe((void **)&(lineTable->positions));
                                        FreeMemorySafe((void **)&lineTable);
                                        FreeMemorySafe((void **)&palette);
                                        result.success = true;
                                        return result;
                                }
                        }

                        result.linesProcessed = i + 1;
                }

                // Cleanup
                FreeMemorySafe((void **)&rowBuffer);
                FreeMemorySafe((void **)&lineData);
                FreeMemorySafe((void **)&(lineTable->positions));
                FreeMemorySafe((void **)&lineTable);
                FreeMemorySafe((void **)&palette);

                result.success = true;
                return result;
        }

        // Handle uncompressed formats (BI_RGB)
        // Calculate row size with padding (rows are padded to 4-byte boundaries)
        rowSize = ((infoHeader.biBitCount * result.width + 31) / 32) * 4;

        // Allocate row buffer for reading from file
        rowBuffer = (uint8_t *)GetMemory(rowSize);
        if (!rowBuffer)
        {
                cleanupAndError("Failed to allocate row buffer", &palette, &rowBuffer, &lineData, &lineTable);
        }

        // Read and decode pixel data
        for (int i = 0; i < result.height; i++)
        {
                // Determine which file line to read and which screen row to report
                int fileRow;
                int screenRow;

                if (topdown)
                {
                        // topdown=true: read backwards through file
                        if (bottomUp)
                        {
                                fileRow = result.height - 1 - i;
                        }
                        else
                        {
                                fileRow = i;
                        }
                        screenRow = i; // Report screen rows 0, 1, 2, ...
                }
                else
                {
                        // topdown=false: read sequentially (efficient)
                        fileRow = i;
                        if (bottomUp)
                        {
                                // File line 0 = image bottom → screen row 479
                                screenRow = result.height - 1 - i;
                        }
                        else
                        {
                                // File line 0 = image top → screen row 479 (upside down)
                                screenRow = result.height - 1 - i;
                        }
                }

                // Seek to the correct line in the file
                long linePosition = pixelDataStart + ((long)fileRow * rowSize);
                onBMPSeek(linePosition, SEEK_SET);

                // Read row from file
                if (onBMPRead((char *)rowBuffer, rowSize) != rowSize)
                {
                        cleanupAndError("Failed to read pixel data", &palette, &rowBuffer, &lineData, &lineTable);
                }

                // Decode based on bit depth into lineData buffer
                switch (infoHeader.biBitCount)
                {
                case 1: // 1-bit monochrome
                        for (col = 0; col < result.width; col++)
                        {
                                int byteIndex = col / 8;
                                int bitIndex = 7 - (col % 8);
                                int bit = (rowBuffer[byteIndex] >> bitIndex) & 1;
                                if (bit < paletteSize)
                                {
                                        lineData[col] = (palette[bit].rgbRed << 16) |
                                                        (palette[bit].rgbGreen << 8) |
                                                        palette[bit].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = bit ? 0xFFFFFF : 0x000000; // White or black
                                }
                        }
                        break;

                case 4: // 4-bit indexed
                        for (col = 0; col < result.width; col++)
                        {
                                int byteIndex = col / 2;
                                int nibble = (col & 1) ? (rowBuffer[byteIndex] & 0x0F) : (rowBuffer[byteIndex] >> 4);
                                if (nibble < paletteSize)
                                {
                                        lineData[col] = (palette[nibble].rgbRed << 16) |
                                                        (palette[nibble].rgbGreen << 8) |
                                                        palette[nibble].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = 0; // Black for invalid index
                                }
                        }
                        break;

                case 8: // 8-bit indexed
                        for (col = 0; col < result.width; col++)
                        {
                                uint8_t index = rowBuffer[col];
                                if (index < paletteSize)
                                {
                                        lineData[col] = (palette[index].rgbRed << 16) |
                                                        (palette[index].rgbGreen << 8) |
                                                        palette[index].rgbBlue;
                                }
                                else
                                {
                                        lineData[col] = 0; // Black for invalid index
                                }
                        }
                        break;

                case 16: // 16-bit RGB (assume 5-5-5)
                        for (col = 0; col < result.width; col++)
                        {
                                uint16_t pixel = *(uint16_t *)(rowBuffer + col * 2);
                                // Extract RGB components (5-5-5 format) and expand to 8-bit
                                uint8_t r = ((pixel >> 10) & 0x1F) << 3;
                                uint8_t g = ((pixel >> 5) & 0x1F) << 3;
                                uint8_t b = (pixel & 0x1F) << 3;
                                lineData[col] = (r << 16) | (g << 8) | b;
                        }
                        break;

                case 24: // 24-bit BGR
                        for (col = 0; col < result.width; col++)
                        {
                                uint8_t b = rowBuffer[col * 3 + 0];
                                uint8_t g = rowBuffer[col * 3 + 1];
                                uint8_t r = rowBuffer[col * 3 + 2];
                                lineData[col] = (r << 16) | (g << 8) | b;
                        }
                        break;
                }

                // Call the callback with screen row position
                if (!linecallback(&result.width, &result.height, lineData, &screenRow))
                {
                        // Callback requested abort - clean up and return successfully
                        result.linesProcessed = i;
                        FreeMemorySafe((void **)&rowBuffer);
                        FreeMemorySafe((void **)&lineData);
                        FreeMemorySafe((void **)&palette);
                        result.success = true;
                        return result;
                }

                result.linesProcessed = i + 1;
        }

        // Cleanup
        FreeMemorySafe((void **)&rowBuffer);
        FreeMemorySafe((void **)&lineData);
        FreeMemorySafe((void **)&palette);

        result.success = true;
        return result;
}

BYTE BMP_bDecode_memory(int x, int y, int xlen, int ylen, int fnbr, char *p)
{
        return 0;
}
/*        BMPDECODER BmpDec;
        WORD wX, wY;
        BYTE bPadding;
        unsigned int nbr;
        BDEC_vResetData(&BmpDec);
        BDEC_bReadHeader(&BmpDec, fnbr);
        if (BmpDec.blBmMarkerFlag == 0 || BmpDec.bHeaderType < 40 || (BmpDec.blCompressionType != 0 && BmpDec.blCompressionType != 3))
        {
                return 100;
        }
        IMG_wImageWidth = (WORD)BmpDec.lWidth;
        IMG_wImageHeight = (WORD)BmpDec.lHeight;
        IMG_vSetboundaries();
        char *linebuff = GetMemory(IMG_wImageWidth * 3); // get a line buffer

        //        IMG_FSEEK(pFile, BmpDec.lImageOffset, 0);
        if (BmpDec.bBitsPerPixel == 24) // True color Image
        {
                int pp;
                bPadding = (4 - ((BmpDec.lWidth * 3) % 4)) % 4;
                for (wY = 0; wY < BmpDec.lHeight; wY++)
                {
                        routinechecks();
                        IMG_vLoopCallback();
                        IMG_vCheckAndAbort();
                        FSerror = FileGetData(IMG_FILE, linebuff, BmpDec.lWidth * 3, &nbr); // B
                                                                                          //                        if((void *)DrawBuffer != (void *)DisplayNotSet) {
                                                                                                                          PInt(linebuff[0]);PIntComma(linebuff[1]);PIntComma(linebuff[2]);PRet();
                                                                                                                      DrawBuffer(x, BmpDec.lHeight - wY - 1 + y, x+BmpDec.lWidth-1, BmpDec.lHeight - wY - 1 + y,  linebuff);
                                                                                                                  } else
                        {                                                                 // must be a loadable driver so no helper function available
                                pp = 0;
                                for (wX = 0; wX < BmpDec.lWidth; wX++)
                                {
                                        colour.rgbbytes[0] = linebuff[pp++];
                                        colour.rgbbytes[1] = linebuff[pp++];
                                        colour.rgbbytes[2] = linebuff[pp++];
                                        int px = wX - x;
                                        int py = BmpDec.lHeight - wY - 1 - y;
                                        if (px < xlen && px >= 0 && py < ylen && py >= 0)
                                        {
                                                char *q = p + (py * xlen + px) * 3;
                                                *q++ = colour.rgbbytes[0];
                                                *q++ = colour.rgbbytes[1];
                                                *q++ = colour.rgbbytes[2];
                                        }
                                }
                        }
                        for (wX = 0; wX < bPadding; wX++)
                        {
                                BYTE bValue;
                                FSerror = FileGetData(IMG_FILE, &bValue, 1, &nbr);
                        }
                }
        }
        else
                error("Only 24-bit colour images supported");
        FreeMemory((void *)linebuff);
        return 0;
}*/
/*  @endcond */

// Dithering modes - bit layout: [method(bit2)][format(bits 1-0)]
// Bit 2: 0=Floyd-Steinberg, 1=Atkinson
// Bits 1-0: 00=RGB121, 01=RGB222, 10=RGB332, 11=reserved for RGB565
#define DITHER_METHOD(mode) ((mode) >> 2)  // 0=Floyd-Steinberg, 1=Atkinson
#define DITHER_FORMAT(mode) ((mode) & 0x3) // 0=RGB121, 1=RGB222, 2=RGB332, 3=RGB565

// Format identifiers
#define FORMAT_RGB121 0
#define FORMAT_RGB222 1
#define FORMAT_RGB332 2
#define FORMAT_RGB565 3

// Method identifiers
#define METHOD_FLOYD_STEINBERG 0
#define METHOD_ATKINSON 1

// Convert RGB121 to RGB888
void rgb121_to_rgb888_components(uint8_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
        uint8_t r1 = (color >> 3) & 1;
        uint8_t g2 = (color >> 1) & 3;
        uint8_t b1 = color & 1;

        *r = r1 * 255;
        *g = g2 * 85; // 255/3
        *b = b1 * 255;
}

// Convert RGB222 to RGB888
// Convert RGB222 to RGB888
void rgb222_to_rgb888_components(uint8_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
        uint8_t r2 = (color >> 4) & 3;
        uint8_t g2 = (color >> 2) & 3;
        uint8_t b2 = color & 3;

        *r = r2 * 85; // 0->0, 1->85, 2->170, 3->255
        *g = g2 * 85;
        *b = b2 * 85;
}
// Convert RGB332 to RGB888
void rgb332_to_rgb888_components(uint8_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
        uint8_t r3 = (color >> 5) & 7;
        uint8_t g3 = (color >> 2) & 7;
        uint8_t b2 = color & 3;

        *r = (r3 * 255) / 7;
        *g = (g3 * 255) / 7;
        *b = (b2 * 255) / 3;
}

// Structure to hold dithering state
typedef struct
{
        int16_t *error_buffer_r;
        int16_t *error_buffer_g;
        int16_t *error_buffer_b;
        int16_t *curr_error_r;
        int16_t *curr_error_g;
        int16_t *curr_error_b;
        int16_t *next_error_r;
        int16_t *next_error_g;
        int16_t *next_error_b;
        int16_t *next2_error_r;
        int16_t *next2_error_g;
        int16_t *next2_error_b;
        uint8_t *output_buffer;
        int buffer_width;
        int dither_method;
        int dither_format;
        int img_x_offset;
        int img_y_offset;
        int x_display;
        int y_display;
        int image_height;
        int max_visible_width;
} DitherDisplayState;

// Global state for the callback
static DitherDisplayState *g_display_state = NULL;

// Simple line callback for non-dithered display
bool displaySimpleLinecallback(int *imagewidth, int *imageheight, uint32_t *linedata, int *linenumber)
{
        DitherDisplayState *state = g_display_state;
        int width = *imagewidth;
        int img_y = *linenumber;

        // Check if this image row is visible (after cropping and positioning)
        if (img_y < state->img_y_offset)
        {
                return true; // Skip cropped rows
        }

        // Calculate screen position
        int screen_y = (img_y - state->img_y_offset) + state->y_display;

        // Skip if line is outside display bounds
        if (screen_y < 0 || screen_y >= VRes)
        {
                return true;
        }

        // Track visible pixels
        int visible_pixels = 0;
        int first_screen_x = -1;

        // Process each pixel in the line
        for (int x = 0; x < width; x++)
        {
                // Skip cropped pixels
                if (x < state->img_x_offset)
                {
                        continue;
                }

                // Calculate screen position
                int screen_x = (x - state->img_x_offset) + state->x_display;

                // Store pixel if it's visible on screen
                if (screen_x >= 0 && screen_x < HRes)
                {
                        if (first_screen_x == -1)
                        {
                                first_screen_x = screen_x;
                        }

                        uint32_t pixel = linedata[x];
                        uint8_t r = (pixel >> 16) & 0xFF;
                        uint8_t g = (pixel >> 8) & 0xFF;
                        uint8_t b = pixel & 0xFF;

                        // Store as BGR triplets (packed RGB format for DrawBuffer)
                        state->output_buffer[visible_pixels * 3] = b;
                        state->output_buffer[visible_pixels * 3 + 1] = g;
                        state->output_buffer[visible_pixels * 3 + 2] = r;
                        visible_pixels++;
                }
        }

        // Draw the line if any pixels are visible
        if (visible_pixels > 0)
        {
                int last_screen_x = first_screen_x + visible_pixels - 1;
                DrawBuffer(first_screen_x, screen_y, last_screen_x, screen_y, state->output_buffer);
        }

        return true;
}

// Line callback for displaying with dithering
bool displayDitheredLinecallback(int *imagewidth, int *imageheight, uint32_t *linedata, int *linenumber)
{
        DitherDisplayState *state = g_display_state;
        int width = *imagewidth;
        int img_y = *linenumber;

        // Check if this image row is visible (after cropping and positioning)
        if (img_y < state->img_y_offset)
        {
                return true; // Skip cropped rows
        }

        // Calculate screen position
        int screen_y = (img_y - state->img_y_offset) + state->y_display;

        // Skip if line is outside display bounds
        if (screen_y < 0 || screen_y >= VRes)
        {
                return true;
        }

        // Clear next line(s) error buffer
        for (int i = 0; i < width; i++)
        {
                state->next_error_r[i] = 0;
                state->next_error_g[i] = 0;
                state->next_error_b[i] = 0;
        }
        if (state->dither_method == METHOD_ATKINSON)
        {
                for (int i = 0; i < width; i++)
                {
                        state->next2_error_r[i] = 0;
                        state->next2_error_g[i] = 0;
                        state->next2_error_b[i] = 0;
                }
        }

        // Track visible pixels
        int visible_pixels = 0;
        int first_screen_x = -1;

        // Process each pixel in the line
        for (int x = 0; x < width; x++)
        {
                uint32_t pixel = linedata[x];
                uint8_t r = (pixel >> 16) & 0xFF;
                uint8_t g = (pixel >> 8) & 0xFF;
                uint8_t b = pixel & 0xFF;

                // Apply accumulated error from previous pixels
                int16_t old_r = r + state->curr_error_r[x];
                int16_t old_g = g + state->curr_error_g[x];
                int16_t old_b = b + state->curr_error_b[x];

                // Convert to display format based on dither_format
                uint8_t display_color;
                uint8_t new_r, new_g, new_b;

                switch (state->dither_format)
                {
                case FORMAT_RGB121:
                        display_color = rgb888_to_rgb121_dither(old_r, old_g, old_b);
                        rgb121_to_rgb888_components(display_color, &new_r, &new_g, &new_b);
                        break;
                case FORMAT_RGB222:
                        display_color = rgb888_to_rgb222_dither(old_r, old_g, old_b);
                        rgb222_to_rgb888_components(display_color, &new_r, &new_g, &new_b);
                        break;
                case FORMAT_RGB332:
                        display_color = rgb888_to_rgb332_dither(old_r, old_g, old_b);
                        rgb332_to_rgb888_components(display_color, &new_r, &new_g, &new_b);
                        break;
                default: // FORMAT_RGB565 - not yet implemented
                        new_r = (old_r < 0) ? 0 : (old_r > 255) ? 255
                                                                : old_r;
                        new_g = (old_g < 0) ? 0 : (old_g > 255) ? 255
                                                                : old_g;
                        new_b = (old_b < 0) ? 0 : (old_b > 255) ? 255
                                                                : old_b;
                        break;
                }

                // Clamp old values for error calculation
                old_r = (old_r < 0) ? 0 : (old_r > 255) ? 255
                                                        : old_r;
                old_g = (old_g < 0) ? 0 : (old_g > 255) ? 255
                                                        : old_g;
                old_b = (old_b < 0) ? 0 : (old_b > 255) ? 255
                                                        : old_b;

                // Calculate error
                int16_t err_r = old_r - new_r;
                int16_t err_g = old_g - new_g;
                int16_t err_b = old_b - new_b;

                // Distribute error based on dithering method
                if (state->dither_method == METHOD_FLOYD_STEINBERG)
                {
                        // Floyd-Steinberg dithering
                        //     X   7/16
                        // 3/16 5/16 1/16
                        if (x + 1 < width)
                        {
                                state->curr_error_r[x + 1] += (err_r * 7) / 16;
                                state->curr_error_g[x + 1] += (err_g * 7) / 16;
                                state->curr_error_b[x + 1] += (err_b * 7) / 16;
                        }
                        if (x > 0)
                        {
                                state->next_error_r[x - 1] += (err_r * 3) / 16;
                                state->next_error_g[x - 1] += (err_g * 3) / 16;
                                state->next_error_b[x - 1] += (err_b * 3) / 16;
                        }
                        state->next_error_r[x] += (err_r * 5) / 16;
                        state->next_error_g[x] += (err_g * 5) / 16;
                        state->next_error_b[x] += (err_b * 5) / 16;
                        if (x + 1 < width)
                        {
                                state->next_error_r[x + 1] += (err_r * 1) / 16;
                                state->next_error_g[x + 1] += (err_g * 1) / 16;
                                state->next_error_b[x + 1] += (err_b * 1) / 16;
                        }
                }
                else
                { // METHOD_ATKINSON
                        // Atkinson dithering (divides error by 8, distributes 6/8)
                        //     X   1/8 1/8
                        // 1/8 1/8 1/8
                        //     1/8
                        if (x + 1 < width)
                        {
                                state->curr_error_r[x + 1] += err_r / 8;
                                state->curr_error_g[x + 1] += err_g / 8;
                                state->curr_error_b[x + 1] += err_b / 8;
                        }
                        if (x + 2 < width)
                        {
                                state->curr_error_r[x + 2] += err_r / 8;
                                state->curr_error_g[x + 2] += err_g / 8;
                                state->curr_error_b[x + 2] += err_b / 8;
                        }
                        if (x > 0)
                        {
                                state->next_error_r[x - 1] += err_r / 8;
                                state->next_error_g[x - 1] += err_g / 8;
                                state->next_error_b[x - 1] += err_b / 8;
                        }
                        state->next_error_r[x] += err_r / 8;
                        state->next_error_g[x] += err_g / 8;
                        state->next_error_b[x] += err_b / 8;
                        if (x + 1 < width)
                        {
                                state->next_error_r[x + 1] += err_r / 8;
                                state->next_error_g[x + 1] += err_g / 8;
                                state->next_error_b[x + 1] += err_b / 8;
                        }
                        state->next2_error_r[x] += err_r / 8;
                        state->next2_error_g[x] += err_g / 8;
                        state->next2_error_b[x] += err_b / 8;
                }

                // Skip cropped pixels
                if (x < state->img_x_offset)
                {
                        continue;
                }

                // Calculate screen position
                int screen_x = (x - state->img_x_offset) + state->x_display;

                // Store pixel if it's visible on screen
                if (screen_x >= 0 && screen_x < HRes)
                {
                        if (first_screen_x == -1)
                        {
                                first_screen_x = screen_x;
                        }
                        // Store as BGR triplets (packed RGB format for DrawBuffer)
                        state->output_buffer[visible_pixels * 3] = new_b;
                        state->output_buffer[visible_pixels * 3 + 1] = new_g;
                        state->output_buffer[visible_pixels * 3 + 2] = new_r;
                        visible_pixels++;
                }
        }

        // Draw the line if any pixels are visible
        if (visible_pixels > 0)
        {
                int last_screen_x = first_screen_x + visible_pixels - 1;
                DrawBuffer(first_screen_x, screen_y, last_screen_x, screen_y, state->output_buffer);
        }

        // Swap error buffers
        if (state->dither_method == METHOD_FLOYD_STEINBERG)
        {
                int16_t *temp;
                temp = state->curr_error_r;
                state->curr_error_r = state->next_error_r;
                state->next_error_r = temp;
                temp = state->curr_error_g;
                state->curr_error_g = state->next_error_g;
                state->next_error_g = temp;
                temp = state->curr_error_b;
                state->curr_error_b = state->next_error_b;
                state->next_error_b = temp;
        }
        else
        { // METHOD_ATKINSON
                int16_t *temp;
                temp = state->curr_error_r;
                state->curr_error_r = state->next_error_r;
                state->next_error_r = state->next2_error_r;
                state->next2_error_r = temp;
                temp = state->curr_error_g;
                state->curr_error_g = state->next_error_g;
                state->next_error_g = state->next2_error_g;
                state->next2_error_g = temp;
                temp = state->curr_error_b;
                state->curr_error_b = state->next_error_b;
                state->next_error_b = state->next2_error_b;
                state->next2_error_b = temp;
        }

        return true;
}

int ReadAndDisplayBMP(int fnbr, int dither_mode, int img_x_offset,
                      int img_y_offset, int x_display, int y_display)
{
        int width, height;
        DitherDisplayState state = {0};
        bool use_dithering = (dither_mode != -1);

        // Get image dimensions using new function
        decodeBMPheader(&width, &height);

        if (width == 0 || height == 0)
        {
                error("BMP: Invalid file signature (not a BMP file)");
                return -2;
        }

        // Check size limits
        if (width > 3840 || height > 2160)
        {
                error("BMP: Image dimensions invalid or exceed 3840x2160");
                return -6;
        }

        if (use_dithering)
        {
                // Extract dithering method and format from mode
                int dither_method = DITHER_METHOD(dither_mode);
                int dither_format = DITHER_FORMAT(dither_mode);

                // Allocate buffers for dithering error diffusion
                int error_lines = (dither_method == METHOD_ATKINSON) ? 3 : 2;
                state.error_buffer_r = (int16_t *)GetMemory(width * error_lines * sizeof(int16_t));
                state.error_buffer_g = (int16_t *)GetMemory(width * error_lines * sizeof(int16_t));
                state.error_buffer_b = (int16_t *)GetMemory(width * error_lines * sizeof(int16_t));

                if (!state.error_buffer_r || !state.error_buffer_g || !state.error_buffer_b)
                {
                        if (state.error_buffer_r)
                                FreeMemory((void *)state.error_buffer_r);
                        if (state.error_buffer_g)
                                FreeMemory((void *)state.error_buffer_g);
                        if (state.error_buffer_b)
                                FreeMemory((void *)state.error_buffer_b);
                        error("BMP: Failed to allocate error buffers");
                        return -8;
                }

                // Initialize error buffers to zero
                memset(state.error_buffer_r, 0, width * error_lines * sizeof(int16_t));
                memset(state.error_buffer_g, 0, width * error_lines * sizeof(int16_t));
                memset(state.error_buffer_b, 0, width * error_lines * sizeof(int16_t));

                // Set up error buffer pointers
                state.curr_error_r = state.error_buffer_r;
                state.next_error_r = state.error_buffer_r + width;
                state.next2_error_r = (dither_method == METHOD_ATKINSON) ? (state.error_buffer_r + width * 2) : NULL;
                state.curr_error_g = state.error_buffer_g;
                state.next_error_g = state.error_buffer_g + width;
                state.next2_error_g = (dither_method == METHOD_ATKINSON) ? (state.error_buffer_g + width * 2) : NULL;
                state.curr_error_b = state.error_buffer_b;
                state.next_error_b = state.error_buffer_b + width;
                state.next2_error_b = (dither_method == METHOD_ATKINSON) ? (state.error_buffer_b + width * 2) : NULL;

                state.dither_method = dither_method;
                state.dither_format = dither_format;
        }

        // Allocate output buffer for visible portion
        int max_visible_width = (HRes < width) ? HRes : width;
        state.output_buffer = (uint8_t *)GetMemory(max_visible_width * 3);
        if (!state.output_buffer)
        {
                if (use_dithering)
                {
                        FreeMemory((void *)state.error_buffer_r);
                        FreeMemory((void *)state.error_buffer_g);
                        FreeMemory((void *)state.error_buffer_b);
                }
                error("BMP: Failed to allocate output buffer");
                return -10;
        }

        // Initialize state
        state.buffer_width = width;
        state.img_x_offset = img_x_offset;
        state.img_y_offset = img_y_offset;
        state.x_display = x_display;
        state.y_display = y_display;
        state.image_height = height;
        state.max_visible_width = max_visible_width;

        // Set global state pointer for callback
        g_display_state = &state;

        // Set callback and decode BMP
        BMP_Result result;
        if (use_dithering)
        {
                // Set dithering callback and use topdown=true for proper display order
                linecallback = displayDitheredLinecallback;
                result = decodeBMP(true);
        }
        else
        {
                // Set simple callback and use topdown=false for efficient sequential read
                linecallback = displaySimpleLinecallback;
                result = decodeBMP(false);
        }

        // Cleanup
        FreeMemory((void *)state.output_buffer);
        if (use_dithering)
        {
                FreeMemory((void *)state.error_buffer_r);
                FreeMemory((void *)state.error_buffer_g);
                FreeMemory((void *)state.error_buffer_b);
        }

        g_display_state = NULL;
        linecallback = NULL;

        if (!result.success)
        {
                error(result.errorMsg);
                return -11;
        }

        return 0;
}