/***********************************************************************************************************************
PicoMite MMBasic

DrawFill.c

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
 * @file DrawFill.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the area-fill MMBasic commands: the polygon
 *        scanline-fill engine (POLYGON, also driven by LINE GRAPH in
 *        Draw.c and the 3D faces in Draw3D.c), the FILL flood-fill and
 *        the MANDELBROT renderer. Split out of Draw.c.
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <math.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "DrawInternal.h"

fill_t main_fill;
fill_t backup_fill;
int main_fill_poly_vertex_count = 0; // polygon vertex count
TFLOAT *main_fill_polyX = NULL;      // polygon vertex x-coords
TFLOAT *main_fill_polyY = NULL;      // polygon vertex y-coords

void fill_set_pen_color(int red, int green, int blue)
{
    main_fill.pen_color.red = red;
    main_fill.pen_color.green = green;
    main_fill.pen_color.blue = blue;
}

void fill_set_fill_color(int red, int green, int blue)
{
    main_fill.fill_color.red = red;
    main_fill.fill_color.green = green;
    main_fill.fill_color.blue = blue;
}
/* Not static: DrawPolygon() in Draw3D.c loads the vertex arrays itself and
   drives begin/end fill directly (declared in DrawInternal.h). */
void fill_begin_fill(void)
{
    main_fill.filled = true;
    main_fill_poly_vertex_count = 0;
}

// Edge structure for polygon fill - pre-computed edge data
typedef struct
{
    TFLOAT x0, y0, x1, y1;
    TFLOAT dx, dy;
    int valid;
} FillEdge;

void fill_end_fill(int count, int ystart, int yend)
{
    // Dynamically allocate arrays to reduce stack usage
    TFLOAT *nodeX = GetMemory(count * sizeof(TFLOAT));
    if (nodeX == NULL)
    {
        error("Not enough memory for polygon fill");
        return;
    }

    const int f = (main_fill.fill_color.red << 16) |
                  (main_fill.fill_color.green << 8) |
                  main_fill.fill_color.blue;
    const int c = (main_fill.pen_color.red << 16) |
                  (main_fill.pen_color.green << 8) |
                  main_fill.pen_color.blue;

    const int vertex_count = main_fill_poly_vertex_count;

    // Dynamically allocate edge array
    FillEdge *pEdges = GetMemory(vertex_count * sizeof(FillEdge));
    if (pEdges == NULL)
    {
        FreeMemory((void *)nodeX);
        error("Not enough memory for polygon edges");
        return;
    }

    // Pre-process edges
    for (int i = 0; i < vertex_count; i++)
    {
        int j = (i + 1) % vertex_count;
        pEdges[i].x0 = main_fill_polyX[i];
        pEdges[i].y0 = main_fill_polyY[i];
        pEdges[i].x1 = main_fill_polyX[j];
        pEdges[i].y1 = main_fill_polyY[j];
        pEdges[i].dy = pEdges[i].y1 - pEdges[i].y0;
        pEdges[i].dx = pEdges[i].x1 - pEdges[i].x0;

        // Pre-check if edge is horizontal (can skip during scanline processing)
        pEdges[i].valid = (pEdges[i].dy != 0);
    }

    // Scanline loop
    for (int y = ystart; y < yend; y++)
    {
        int nodes = 0;
        const TFLOAT yf = (TFLOAT)y;

        // Find intercepts - optimized with pre-computed edge data
        for (int i = 0; i < vertex_count; i++)
        {
            if (!pEdges[i].valid)
                continue;

            const FillEdge *e = &pEdges[i];

            // Edge crossing check
            if ((e->y0 < yf && e->y1 >= yf) || (e->y1 < yf && e->y0 >= yf))
            {
                // Fast intercept calculation
                nodeX[nodes++] = e->x0 + (yf - e->y0) * e->dx / e->dy;
            }
        }

        // Optimized sort
        if (nodes == 2)
        {
            // Special case: just swap if needed
            if (nodeX[0] > nodeX[1])
            {
                TFLOAT temp = nodeX[0];
                nodeX[0] = nodeX[1];
                nodeX[1] = temp;
            }
        }
        else if (nodes > 2)
        {
            // Insertion sort for small n
            for (int i = 1; i < nodes; i++)
            {
                TFLOAT temp = nodeX[i];
                int j = i;
                while (j > 0 && nodeX[j - 1] > temp)
                {
                    nodeX[j] = nodeX[j - 1];
                    j--;
                }
                nodeX[j] = temp;
            }
        }

        // Fill spans
        for (int i = 0; i < nodes - 1; i += 2)
        {
            int xstart = (int)(nodeX[i] + 1.0f);
            int xend = (int)nodeX[i + 1];

            if (xstart <= xend)
            {
                DrawRectangle(xstart, y, xend, y, f);
            }
        }
    }

    main_fill.filled = false;

    // Redraw outline
    for (int i = 0; i < vertex_count; i++)
    {
        //        int next = (i + 1) % vertex_count;
        DrawLine((int)(pEdges[i].x0 + 0.5f), (int)(pEdges[i].y0 + 0.5f),
                 (int)(pEdges[i].x1 + 0.5f), (int)(pEdges[i].y1 + 0.5f), 1, c);
    }

    // Free dynamically allocated memory
    FreeMemory((void *)pEdges);
    FreeMemory((void *)nodeX);
}
void polygon(unsigned char *p, int close)
{
    int xcount = 0;
    long long int *xptr = NULL, *yptr = NULL, xptr2 = 0, yptr2 = 0, *polycount = NULL, *cptr = NULL, *fptr = NULL;
    MMFLOAT *polycountf = NULL, *cfptr = NULL, *ffptr = NULL, *xfptr = NULL, *yfptr = NULL, xfptr2 = 0, yfptr2 = 0;
    int i, f = 0, c, xtot = 0, ymax = 0, ymin = 1000000, idx = 0;
    int xstride = sizeof(MMFLOAT), ystride = sizeof(MMFLOAT);
    int n = 0, nx = 0, ny = 0, nc = 0, nf = 0;
    getcsargs(&p, 9);
    CheckDisplay();
    getargaddress(argv[0], &polycount, &polycountf, &n, NULL);
    if (n == 1)
    {
        xcount = xtot = getinteger(argv[0]);
        if ((xcount < 3 || xcount > 9999) && xcount != 0)
            error("Invalid number of vertices");
        getargaddress(argv[2], &xptr, &xfptr, &nx, &xstride);
        if (xcount == 0)
        {
            xcount = xtot = nx;
        }
        if (nx < xtot)
            error("X Dimensions %", nx);
        getargaddress(argv[4], &yptr, &yfptr, &ny, &ystride);
        if (ny < xtot)
            error("Y Dimensions %", ny);
        if (xptr)
            xptr2 = STRIDE_INT(xptr, 0, xstride);
        else
            xfptr2 = STRIDE_FLOAT(xfptr, 0, xstride);
        if (yptr)
            yptr2 = STRIDE_INT(yptr, 0, ystride);
        else
            yfptr2 = STRIDE_FLOAT(yfptr, 0, ystride);
        c = gui_fcolour; // setup the defaults
        if (argc > 5 && *argv[6])
            c = getint(argv[6], 0, WHITE);
        if (argc > 7 && *argv[8])
        {
            main_fill_polyX = (TFLOAT *)GetTempMainMemory((xtot + 1) * sizeof(TFLOAT));
            main_fill_polyY = (TFLOAT *)GetTempMainMemory((xtot + 1) * sizeof(TFLOAT));
            f = getint(argv[8], 0, WHITE);
            fill_set_fill_color((f >> 16) & 0xFF, (f >> 8) & 0xFF, f & 0xFF);
            fill_set_pen_color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            fill_begin_fill();
        }
        idx = 0;
        for (i = 0; i < xcount - 1; i++)
        {
            if (argc > 7)
            {
                main_fill_polyX[main_fill_poly_vertex_count] = (xfptr == NULL ? (TFLOAT)STRIDE_INT(xptr, idx, xstride) : (TFLOAT)STRIDE_FLOAT(xfptr, idx, xstride));
                main_fill_polyY[main_fill_poly_vertex_count] = (yfptr == NULL ? (TFLOAT)STRIDE_INT(yptr, idx, ystride) : (TFLOAT)STRIDE_FLOAT(yfptr, idx, ystride));
                idx++;
                if (main_fill_polyY[main_fill_poly_vertex_count] > ymax)
                    ymax = main_fill_polyY[main_fill_poly_vertex_count];
                if (main_fill_polyY[main_fill_poly_vertex_count] < ymin)
                    ymin = main_fill_polyY[main_fill_poly_vertex_count];
                main_fill_poly_vertex_count++;
            }
            else
            {
                int x1 = (xfptr == NULL ? STRIDE_INT(xptr, idx, xstride) : (int)STRIDE_FLOAT(xfptr, idx, xstride));
                int x2 = (xfptr == NULL ? STRIDE_INT(xptr, idx + 1, xstride) : (int)STRIDE_FLOAT(xfptr, idx + 1, xstride));
                int y1 = (yfptr == NULL ? STRIDE_INT(yptr, idx, ystride) : (int)STRIDE_FLOAT(yfptr, idx, ystride));
                int y2 = (yfptr == NULL ? STRIDE_INT(yptr, idx + 1, ystride) : (int)STRIDE_FLOAT(yfptr, idx + 1, ystride));
                idx++;
                DrawLine(x1, y1, x2, y2, 1, c);
            }
        }
        if (argc > 7)
        {
            main_fill_polyX[main_fill_poly_vertex_count] = (xfptr == NULL ? (TFLOAT)STRIDE_INT(xptr, idx, xstride) : (TFLOAT)STRIDE_FLOAT(xfptr, idx, xstride));
            main_fill_polyY[main_fill_poly_vertex_count] = (yfptr == NULL ? (TFLOAT)STRIDE_INT(yptr, idx, ystride) : (TFLOAT)STRIDE_FLOAT(yfptr, idx, ystride));
            if (main_fill_polyY[main_fill_poly_vertex_count] > ymax)
                ymax = main_fill_polyY[main_fill_poly_vertex_count];
            if (main_fill_polyY[main_fill_poly_vertex_count] < ymin)
                ymin = main_fill_polyY[main_fill_poly_vertex_count];
            if (main_fill_polyY[main_fill_poly_vertex_count] != main_fill_polyY[0] || main_fill_polyX[main_fill_poly_vertex_count] != main_fill_polyX[0])
            {
                main_fill_poly_vertex_count++;
                main_fill_polyX[main_fill_poly_vertex_count] = main_fill_polyX[0];
                main_fill_polyY[main_fill_poly_vertex_count] = main_fill_polyY[0];
            }
            main_fill_poly_vertex_count++;
            if (main_fill_poly_vertex_count > 5)
            {
                fill_end_fill(xcount, ymin, ymax);
            }
            else if (main_fill_poly_vertex_count == 5)
            {
                DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], f, f);
                DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[2], main_fill_polyY[2], main_fill_polyX[3], main_fill_polyY[3], f, f);
                if (f != c)
                {
                    DrawLine(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], 1, c);
                    DrawLine(main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], 1, c);
                    DrawLine(main_fill_polyX[2], main_fill_polyY[2], main_fill_polyX[3], main_fill_polyY[3], 1, c);
                    DrawLine(main_fill_polyX[3], main_fill_polyY[3], main_fill_polyX[4], main_fill_polyY[4], 1, c);
                }
            }
            else
            {
                DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], c, f);
            }
        }
        else if (close)
        {
            int x1 = (xfptr == NULL ? STRIDE_INT(xptr, idx, xstride) : (int)STRIDE_FLOAT(xfptr, idx, xstride));
            int x2 = (xfptr == NULL ? xptr2 : (int)xfptr2);
            int y1 = (yfptr == NULL ? STRIDE_INT(yptr, idx, ystride) : (int)STRIDE_FLOAT(yfptr, idx, ystride));
            int y2 = (yfptr == NULL ? yptr2 : (int)yfptr2);
            DrawLine(x1, y1, x2, y2, 1, c);
        }
    }
    else
    {
        int *cc = GetTempMainMemory(n * sizeof(int)); // array for foreground colours
        int *ff = GetTempMainMemory(n * sizeof(int)); // array for background colours
        int xstart, j, xmax = 0;
        for (i = 0; i < n; i++)
        {
            if ((polycountf == NULL ? polycount[i] : (int)polycountf[i]) > xmax)
                xmax = (polycountf == NULL ? polycount[i] : (int)polycountf[i]);
            if (!(polycountf == NULL ? polycount[i] : (int)polycountf[i]))
                break;
            xtot += (polycountf == NULL ? polycount[i] : (int)polycountf[i]);
            if ((polycountf == NULL ? polycount[i] : (int)polycountf[i]) < 3 || (polycountf == NULL ? polycount[i] : (int)polycountf[i]) > 9999)
                error("Invalid number of vertices, polygon %", i);
        }
        n = i;
        getargaddress(argv[2], &xptr, &xfptr, &nx, &xstride);
        if (nx < xtot)
            error("X Dimensions %", nx);
        getargaddress(argv[4], &yptr, &yfptr, &ny, &ystride);
        if (ny < xtot)
            error("Y Dimensions %", ny);
        main_fill_polyX = (TFLOAT *)GetTempMainMemory(xmax * sizeof(TFLOAT));
        main_fill_polyY = (TFLOAT *)GetTempMainMemory(xmax * sizeof(TFLOAT));
        if (argc > 5 && *argv[6])
        { // foreground colour specified
            getargaddress(argv[6], &cptr, &cfptr, &nc, NULL);
            if (nc == 1)
                for (i = 0; i < n; i++)
                    cc[i] = getint(argv[6], 0, WHITE);
            else
            {
                if (nc < n)
                    error("Foreground colour Dimensions");
                for (i = 0; i < n; i++)
                {
                    cc[i] = (cfptr == NULL ? cptr[i] : (int)cfptr[i]);
                    if (cc[i] < 0 || cc[i] > 0xFFFFFF)
                        StandardErrorParam3(26, (int)cc[i], 0, 0xFFFFFF);
                }
            }
        }
        else
            for (i = 0; i < n; i++)
                cc[i] = gui_fcolour;
        if (argc > 7)
        { // background colour specified
            getargaddress(argv[8], &fptr, &ffptr, &nf, NULL);
            if (nf == 1)
                for (i = 0; i < n; i++)
                    ff[i] = getint(argv[8], 0, WHITE);
            else
            {
                if (nf < n)
                    error("Background colour Dimensions");
                for (i = 0; i < n; i++)
                {
                    ff[i] = (ffptr == NULL ? fptr[i] : (int)ffptr[i]);
                    if (ff[i] < 0 || ff[i] > 0xFFFFFF)
                        StandardErrorParam3(26, (int)ff[i], 0, 0xFFFFFF);
                }
            }
        }
        xstart = 0;
        idx = 0;
        for (i = 0; i < n; i++)
        {
            if (xptr)
                xptr2 = STRIDE_INT(xptr, idx, xstride);
            else
                xfptr2 = STRIDE_FLOAT(xfptr, idx, xstride);
            if (yptr)
                yptr2 = STRIDE_INT(yptr, idx, ystride);
            else
                yfptr2 = STRIDE_FLOAT(yfptr, idx, ystride);
            ymax = 0;
            ymin = 1000000;
            main_fill_poly_vertex_count = 0;
            xcount = (int)(polycountf == NULL ? polycount[i] : (int)polycountf[i]);
            if (argc > 7 && *argv[8])
            {
                fill_set_pen_color((cc[i] >> 16) & 0xFF, (cc[i] >> 8) & 0xFF, cc[i] & 0xFF);
                fill_set_fill_color((ff[i] >> 16) & 0xFF, (ff[i] >> 8) & 0xFF, ff[i] & 0xFF);
                fill_begin_fill();
            }
            for (j = xstart; j < xstart + xcount - 1; j++)
            {
                if (argc > 7)
                {
                    main_fill_polyX[main_fill_poly_vertex_count] = (xfptr == NULL ? (TFLOAT)STRIDE_INT(xptr, idx, xstride) : (TFLOAT)STRIDE_FLOAT(xfptr, idx, xstride));
                    main_fill_polyY[main_fill_poly_vertex_count] = (yfptr == NULL ? (TFLOAT)STRIDE_INT(yptr, idx, ystride) : (TFLOAT)STRIDE_FLOAT(yfptr, idx, ystride));
                    idx++;
                    if (main_fill_polyY[main_fill_poly_vertex_count] > ymax)
                        ymax = main_fill_polyY[main_fill_poly_vertex_count];
                    if (main_fill_polyY[main_fill_poly_vertex_count] < ymin)
                        ymin = main_fill_polyY[main_fill_poly_vertex_count];
                    main_fill_poly_vertex_count++;
                }
                else
                {
                    int x1 = (xfptr == NULL ? STRIDE_INT(xptr, idx, xstride) : (int)STRIDE_FLOAT(xfptr, idx, xstride));
                    int x2 = (xfptr == NULL ? STRIDE_INT(xptr, idx + 1, xstride) : (int)STRIDE_FLOAT(xfptr, idx + 1, xstride));
                    int y1 = (yfptr == NULL ? STRIDE_INT(yptr, idx, ystride) : (int)STRIDE_FLOAT(yfptr, idx, ystride));
                    int y2 = (yfptr == NULL ? STRIDE_INT(yptr, idx + 1, ystride) : (int)STRIDE_FLOAT(yfptr, idx + 1, ystride));
                    idx++;
                    DrawLine(x1, y1, x2, y2, 1, cc[i]);
                }
            }
            if (argc > 7)
            {
                main_fill_polyX[main_fill_poly_vertex_count] = (xfptr == NULL ? (TFLOAT)STRIDE_INT(xptr, idx, xstride) : (TFLOAT)STRIDE_FLOAT(xfptr, idx, xstride));
                main_fill_polyY[main_fill_poly_vertex_count] = (yfptr == NULL ? (TFLOAT)STRIDE_INT(yptr, idx, ystride) : (TFLOAT)STRIDE_FLOAT(yfptr, idx, ystride));
                idx++;
                if (main_fill_polyY[main_fill_poly_vertex_count] > ymax)
                    ymax = main_fill_polyY[main_fill_poly_vertex_count];
                if (main_fill_polyY[main_fill_poly_vertex_count] < ymin)
                    ymin = main_fill_polyY[main_fill_poly_vertex_count];
                if (main_fill_polyY[main_fill_poly_vertex_count] != main_fill_polyY[0] || main_fill_polyX[main_fill_poly_vertex_count] != main_fill_polyX[0])
                {
                    main_fill_poly_vertex_count++;
                    main_fill_polyX[main_fill_poly_vertex_count] = main_fill_polyX[0];
                    main_fill_polyY[main_fill_poly_vertex_count] = main_fill_polyY[0];
                }
                main_fill_poly_vertex_count++;
                if (main_fill_poly_vertex_count > 5)
                {
                    fill_end_fill(xcount, ymin, ymax);
                }
                else if (main_fill_poly_vertex_count == 5)
                {
                    DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], ff[i], ff[i]);
                    DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[2], main_fill_polyY[2], main_fill_polyX[3], main_fill_polyY[3], ff[i], ff[i]);
                    if (ff[i] != cc[i])
                    {
                        DrawLine(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], 1, cc[i]);
                        DrawLine(main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], 1, cc[i]);
                        DrawLine(main_fill_polyX[2], main_fill_polyY[2], main_fill_polyX[3], main_fill_polyY[3], 1, cc[i]);
                        DrawLine(main_fill_polyX[3], main_fill_polyY[3], main_fill_polyX[4], main_fill_polyY[4], 1, cc[i]);
                    }
                }
                else
                {
                    DrawTriangle(main_fill_polyX[0], main_fill_polyY[0], main_fill_polyX[1], main_fill_polyY[1], main_fill_polyX[2], main_fill_polyY[2], cc[i], ff[i]);
                }
            }
            else
            {
                int x1 = (xfptr == NULL ? STRIDE_INT(xptr, idx, xstride) : (int)STRIDE_FLOAT(xfptr, idx, xstride));
                int x2 = (xfptr == NULL ? xptr2 : (int)xfptr2);
                int y1 = (yfptr == NULL ? STRIDE_INT(yptr, idx, ystride) : (int)STRIDE_FLOAT(yfptr, idx, ystride));
                int y2 = (yfptr == NULL ? yptr2 : (int)yfptr2);
                DrawLine(x1, y1, x2, y2, 1, cc[i]);
                idx++;
            }

            xstart += xcount;
        }
    }
}
/*  @endcond */
void cmd_polygon(void)
{
    polygon(cmdline, 1);
}

// Define your screen bounds here
#define SCREEN_WIDTH HRes
#define SCREEN_HEIGHT VRes

// Stack structure for flood fill with dynamic block allocation
#define BLOCK_SIZE_ENTRIES 256

#define STACK_BLOCK_SIZE 256 // Entries per block

typedef struct
{
    int x;
    int y;
} Point;

typedef struct StackBlock
{
    Point data[STACK_BLOCK_SIZE];
    struct StackBlock *next;
    struct StackBlock *prev;
} StackBlock;

typedef struct
{
    StackBlock *first_block;
    StackBlock *current_block;
    int current_ptr;   // Index within current block (0 to STACK_BLOCK_SIZE-1)
    int total_entries; // Total number of entries across all blocks
} FloodStack;

// Stack operations with dynamic block allocation
static bool init_stack(FloodStack *s)
{
    s->first_block = (StackBlock *)GetMemory(sizeof(StackBlock));
    if (s->first_block == NULL)
    {
        return false;
    }
    s->first_block->next = NULL;
    s->first_block->prev = NULL;
    s->current_block = s->first_block;
    s->current_ptr = 0;
    s->total_entries = 0;
    return true;
}

static void free_stack(FloodStack *s)
{
    if (s->first_block == NULL)
    {
        return;
    }
    StackBlock *block = s->first_block;
    while (block != NULL)
    {
        StackBlock *next = block->next;
        void *ptr = (void *)block;
        FreeMemorySafe(&ptr);
        block = next;
    }
    s->first_block = NULL;
    s->current_block = NULL;
}

static bool push(FloodStack *s, int x, int y)
{
    // Check if current block is full
    if (s->current_ptr >= STACK_BLOCK_SIZE)
    {
        // Allocate a new block
        StackBlock *new_block = (StackBlock *)GetMemory(sizeof(StackBlock));
        if (new_block == NULL)
        {
            return false; // Out of memory
        }

        // Link the new block
        new_block->next = NULL;
        new_block->prev = s->current_block;
        s->current_block->next = new_block;

        // Move to the new block
        s->current_block = new_block;
        s->current_ptr = 0;
    }

    // Add entry to current block
    s->current_block->data[s->current_ptr].x = x;
    s->current_block->data[s->current_ptr].y = y;
    s->current_ptr++;
    s->total_entries++;
    return true;
}

static bool pop(FloodStack *s, int *x, int *y)
{
    // Check if stack is empty
    if (s->total_entries == 0)
    {
        return false;
    }

    // If current block is empty, move to previous block
    if (s->current_ptr == 0)
    {
        // Must have a previous block if total_entries > 0
        if (s->current_block->prev == NULL)
        {
            // This should never happen if total_entries is correct
            return false;
        }

        // Move to previous block
        s->current_block = s->current_block->prev;
        s->current_ptr = STACK_BLOCK_SIZE;
    }

    // Pop from current block
    s->current_ptr--;
    *x = s->current_block->data[s->current_ptr].x;
    *y = s->current_block->data[s->current_ptr].y;
    s->total_entries--;
    return true;
}

// Read a scanline segment efficiently
static void read_scanline(int x_start, int x_end, int y, unsigned char *buffer)
{
    ReadBuffer(x_start, y, x_end, y, buffer);
}

// Get color from buffer
// ReadBuffer returns pixels in B,G,R order (little endian RGB888)
static inline uint32_t get_color_from_buffer(unsigned char *buffer, int index)
{
    int idx = index * 3;
    uint32_t b = buffer[idx];
    uint32_t g = buffer[idx + 1];
    uint32_t r = buffer[idx + 2];
    return (r << 16) | (g << 8) | b;
}

// Set color in buffer
// DrawBuffer expects pixels in B,G,R order (little endian RGB888)
static inline void set_color_in_buffer(unsigned char *buffer, int index, uint32_t color)
{
    int idx = index * 3;
    buffer[idx] = color & 0xFF;             // B
    buffer[idx + 1] = (color >> 8) & 0xFF;  // G
    buffer[idx + 2] = (color >> 16) & 0xFF; // R
}

// Scanline flood fill algorithm with block reading
// Supports two modes:
// 1. Replace mode: if boundary_colour == -1, replace all pixels matching color at (x,y) with internal_colour
// 2. Boundary mode: if boundary_colour != -1, fill with internal_colour up to boundary_colour
void floodfill(int x, int y, int internal_colour, int boundary_colour)
{
    // Determine which mode we're in
    bool boundary_mode = (boundary_colour != -1);

    // internal_colour must always be valid (not -1)
    if (internal_colour == -1)
    {
        return; // Invalid: must specify fill color
    }

    uint32_t c_new = (uint32_t)internal_colour;

    // Bounds check
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
    {
        return;
    }

    // Initialize dynamic stack
    FloodStack stack;
    if (!init_stack(&stack))
    {
        return; // Memory allocation failed
    }

    // Allocate buffer for scanline reads (one full row)
    unsigned char *line_buffer = (unsigned char *)GetMemory(SCREEN_WIDTH * 3);
    if (line_buffer == NULL)
    {
        free_stack(&stack);
        return; // Memory allocation failed
    }

    // Allocate persistent buffers for checking adjacent lines
    unsigned char *above_buffer = (unsigned char *)GetMemory(SCREEN_WIDTH * 3);
    unsigned char *below_buffer = (unsigned char *)GetMemory(SCREEN_WIDTH * 3);
    if (above_buffer == NULL || below_buffer == NULL)
    {
        FreeMemorySafe((void **)&above_buffer);
        FreeMemorySafe((void **)&below_buffer);
        FreeMemorySafe((void **)&line_buffer);
        free_stack(&stack);
        return; // Memory allocation failed
    }

    // Allocate a "filled" bitmap to track which pixels we've already processed
    int bitmap_size = (SCREEN_WIDTH * SCREEN_HEIGHT + 7) / 8;
    unsigned char *filled_bitmap = (unsigned char *)GetMemory(bitmap_size);
    if (filled_bitmap == NULL)
    {
        FreeMemorySafe((void **)&below_buffer);
        FreeMemorySafe((void **)&above_buffer);
        FreeMemorySafe((void **)&line_buffer);
        free_stack(&stack);
        return; // Memory allocation failed
    }

    // Read the initial scanline to get origin color
    read_scanline(0, SCREEN_WIDTH - 1, y, line_buffer);
    uint32_t c_origin = get_color_from_buffer(line_buffer, x);

    // Mode-specific validation
    if (boundary_mode)
    {
        // In boundary mode, if starting pixel is the boundary color, nothing to do
        uint32_t c_boundary = (uint32_t)boundary_colour;
        if (c_origin == c_boundary)
        {
            FreeMemorySafe((void **)&filled_bitmap);
            FreeMemorySafe((void **)&below_buffer);
            FreeMemorySafe((void **)&above_buffer);
            FreeMemorySafe((void **)&line_buffer);
            free_stack(&stack);
            return;
        }
    }
    else
    {
        // In replace mode, if starting pixel is already the target color, nothing to do
        if (c_origin == c_new)
        {
            FreeMemorySafe((void **)&filled_bitmap);
            FreeMemorySafe((void **)&below_buffer);
            FreeMemorySafe((void **)&above_buffer);
            FreeMemorySafe((void **)&line_buffer);
            free_stack(&stack);
            return;
        }
    }

    // Push initial point
    if (!push(&stack, x, y))
    {
        FreeMemorySafe((void **)&filled_bitmap);
        FreeMemorySafe((void **)&below_buffer);
        FreeMemorySafe((void **)&above_buffer);
        FreeMemorySafe((void **)&line_buffer);
        free_stack(&stack);
        return;
    }

    int current_y = -1; // Track which line is currently buffered

    while (pop(&stack, &x, &y))
    {
        // Read the scanline if we don't have it buffered
        if (y != current_y)
        {
            read_scanline(0, SCREEN_WIDTH - 1, y, line_buffer);
            current_y = y;
        }

        // Calculate bitmap position
        int bit_pos = y * SCREEN_WIDTH + x;
        int byte_idx = bit_pos / 8;
        int bit_idx = bit_pos % 8;

        // Check if we've already processed this pixel
        if (filled_bitmap[byte_idx] & (1 << bit_idx))
        {
            continue;
        }

        // Check if this pixel should be filled based on mode
        uint32_t pixel_color = get_color_from_buffer(line_buffer, x);
        bool should_fill;

        if (boundary_mode)
        {
            // Boundary mode: fill if pixel is NOT the boundary color
            uint32_t c_boundary = (uint32_t)boundary_colour;
            should_fill = (pixel_color != c_boundary);
        }
        else
        {
            // Replace mode: only fill if pixel matches origin color
            should_fill = (pixel_color == c_origin);
        }

        if (!should_fill)
        {
            continue;
        }

        // Find leftmost pixel in this row
        int x1 = x;
        while (x1 > 0)
        {
            uint32_t left_color = get_color_from_buffer(line_buffer, x1 - 1);
            bool can_extend;

            if (boundary_mode)
            {
                uint32_t c_boundary = (uint32_t)boundary_colour;
                can_extend = (left_color != c_boundary);
            }
            else
            {
                can_extend = (left_color == c_origin);
            }

            if (!can_extend)
                break;
            x1--;
        }

        // Find rightmost pixel in this row
        int x2 = x;
        while (x2 < SCREEN_WIDTH - 1)
        {
            uint32_t right_color = get_color_from_buffer(line_buffer, x2 + 1);
            bool can_extend;

            if (boundary_mode)
            {
                uint32_t c_boundary = (uint32_t)boundary_colour;
                can_extend = (right_color != c_boundary);
            }
            else
            {
                can_extend = (right_color == c_origin);
            }

            if (!can_extend)
                break;
            x2++;
        }

        // Fill the scanline
        int span_width = x2 - x1 + 1;
        unsigned char *draw_buffer = (unsigned char *)GetMemory(span_width * 3);
        if (draw_buffer != NULL)
        {
            // Prepare the buffer with new color in B,G,R order
            for (int i = 0; i < span_width; i++)
            {
                draw_buffer[i * 3] = c_new & 0xFF;
                draw_buffer[i * 3 + 1] = (c_new >> 8) & 0xFF;
                draw_buffer[i * 3 + 2] = (c_new >> 16) & 0xFF;
            }

            // Write the entire span at once
            DrawBuffer(x1, y, x2, y, draw_buffer);
            FreeMemorySafe((void **)&draw_buffer);

            // Mark all pixels in the span as filled in the bitmap
            for (int i = x1; i <= x2; i++)
            {
                int pos = y * SCREEN_WIDTH + i;
                int b_idx = pos / 8;
                int bit = pos % 8;
                filled_bitmap[b_idx] |= (1 << bit);

                // Also update line buffer
                set_color_in_buffer(line_buffer, i, c_new);
            }
        }

        // Check pixels above and below, adding spans to stack
        bool span_above = false;
        bool span_below = false;

        // Read line above if needed
        if (y > 0)
        {
            read_scanline(0, SCREEN_WIDTH - 1, y - 1, above_buffer);

            for (int i = x1; i <= x2; i++)
            {
                // Check bitmap first
                int pos = (y - 1) * SCREEN_WIDTH + i;
                int b_idx = pos / 8;
                int bit = pos % 8;

                if (!(filled_bitmap[b_idx] & (1 << bit)))
                {
                    uint32_t c = get_color_from_buffer(above_buffer, i);
                    bool can_fill;

                    if (boundary_mode)
                    {
                        uint32_t c_boundary = (uint32_t)boundary_colour;
                        can_fill = (c != c_boundary);
                    }
                    else
                    {
                        can_fill = (c == c_origin);
                    }

                    if (can_fill)
                    {
                        if (!span_above)
                        {
                            if (!push(&stack, i, y - 1))
                            {
                                FreeMemorySafe((void **)&filled_bitmap);
                                FreeMemorySafe((void **)&below_buffer);
                                FreeMemorySafe((void **)&above_buffer);
                                FreeMemorySafe((void **)&line_buffer);
                                free_stack(&stack);
                                return;
                            }
                            span_above = true;
                        }
                    }
                    else
                    {
                        span_above = false;
                    }
                }
                else
                {
                    span_above = false;
                }
            }
        }

        // Read line below if needed
        if (y < SCREEN_HEIGHT - 1)
        {
            read_scanline(0, SCREEN_WIDTH - 1, y + 1, below_buffer);

            for (int i = x1; i <= x2; i++)
            {
                // Check bitmap first
                int pos = (y + 1) * SCREEN_WIDTH + i;
                int b_idx = pos / 8;
                int bit = pos % 8;

                if (!(filled_bitmap[b_idx] & (1 << bit)))
                {
                    uint32_t c = get_color_from_buffer(below_buffer, i);
                    bool can_fill;

                    if (boundary_mode)
                    {
                        uint32_t c_boundary = (uint32_t)boundary_colour;
                        can_fill = (c != c_boundary);
                    }
                    else
                    {
                        can_fill = (c == c_origin);
                    }

                    if (can_fill)
                    {
                        if (!span_below)
                        {
                            if (!push(&stack, i, y + 1))
                            {
                                FreeMemorySafe((void **)&filled_bitmap);
                                FreeMemorySafe((void **)&below_buffer);
                                FreeMemorySafe((void **)&above_buffer);
                                FreeMemorySafe((void **)&line_buffer);
                                free_stack(&stack);
                                return;
                            }
                            span_below = true;
                        }
                    }
                    else
                    {
                        span_below = false;
                    }
                }
                else
                {
                    span_below = false;
                }
            }
        }
    }

    // Clean up heap memory
    FreeMemorySafe((void **)&filled_bitmap);
    FreeMemorySafe((void **)&below_buffer);
    FreeMemorySafe((void **)&above_buffer);
    FreeMemorySafe((void **)&line_buffer);
    free_stack(&stack);
}

void cmd_fill(void)
{
    uint32_t c = -1, b = -1;
    getcsargs(&cmdline, 7);
    if ((void *)ReadBuffer == (void *)DisplayNotSet)
        StandardError(11);
    if (!(Option.DISPLAY_TYPE))
        error("No display");
    if (!(argc == 5 || argc == 7))
        SyntaxError();

    int x = getint(argv[0], 0, HRes - 1);
    int y = getint(argv[2], 0, VRes - 1);
    c = (uint32_t)getColour((char *)argv[4], 0);
    if (argc == 7)
        b = (uint32_t)getColour((char *)argv[6], 0);
    // Call with replace mode (boundary_colour = -1)
    floodfill(x, y, c, b);
}

#if defined(PICOMITEVGA) || defined(rp2350)
/*****************************************************************************
 * MANDELBROT command - fast fixed-point Mandelbrot set renderer
 *
 * Syntax:
 *   MANDELBROT DRAW [maxiter]       - Draw at current view (default 64 iters)
 *   MANDELBROT PAN dx, dy           - Pan by dx,dy pixels and redraw
 *   MANDELBROT ZOOM factor          - Zoom by factor (>1 in, <1 out), redraw
 *   MANDELBROT CENTRE x, y          - Re-centre on pixel x,y from last draw
 *   MANDELBROT RESET                - Reset to default view
 *
 * Uses 32-bit fixed-point (8.24) for speed on RP2040/RP2350.
 *****************************************************************************/

/* Fixed-point format: 8 bits integer, 24 bits fraction */
#define MB_FRAC 24
#define MB_ONE (1 << MB_FRAC)
#define MB_FOUR (4 << MB_FRAC)

/* Convert double to fixed-point */
#define MB_FROM_DBL(d) ((int32_t)((d) * (double)MB_ONE))

/* Default view: real [-2.5, 1.0], imag [-1.5, 1.5] centred on (-0.5, 0) */
static int32_t mb_centre_r = 0; /* initialised in mb_reset() */
static int32_t mb_centre_i = 0;
static int32_t mb_scale = 0; /* units per pixel */
static int mb_maxiter = 64;
static int mb_initialised = 0;

static void mb_reset(void)
{
    mb_centre_r = MB_FROM_DBL(-0.5);
    mb_centre_i = 0;
    /* scale so the full set fits: 3.5 units across HRes pixels */
    /* but also check vertical: 3.0 units across VRes pixels */
    double hscale = 3.5 / (double)HRes;
    double vscale = 3.0 / (double)VRes;
    double s = (hscale > vscale) ? hscale : vscale;
    mb_scale = MB_FROM_DBL(s);
    if (mb_scale < 1)
        mb_scale = 1;
    mb_maxiter = 64;
    mb_initialised = 1;
}

/* Palette using the 16 predefined colours from Draw.h */
static const int mb_palette[16] = {
    BLUE, COBALT, CERULEAN, CYAN,
    MIDGREEN, GREEN, MYRTLE, YELLOW,
    BROWN, RUST, RED, MAGENTA,
    FUCHSIA, LILAC, WHITE, BLUE};

static int mb_colour(int iter, int max_iter)
{
    if (iter >= max_iter)
        return BLACK;
    return mb_palette[iter & 15];
}

static void mb_draw(void)
{
    int w = HRes, h = VRes;
    int32_t scale = mb_scale;
    int max_iter = mb_maxiter;

    /* Top-left corner in complex plane */
    int32_t origin_r = mb_centre_r - (int32_t)((int64_t)scale * (w >> 1));
    int32_t origin_i = mb_centre_i - (int32_t)((int64_t)scale * (h >> 1));

    for (int py = 0; py < h; py++)
    {
        int32_t ci = origin_i + (int32_t)((int64_t)scale * py);
        int32_t cr_row = origin_r;

        for (int px = 0; px < w; px++)
        {
            int32_t cr = cr_row;
            int32_t zr = 0, zi = 0;
            int iter = 0;

            /* Main iteration loop - tight fixed-point arithmetic */
            while (iter < max_iter)
            {
                int32_t zr2 = (int32_t)(((int64_t)zr * zr) >> MB_FRAC);
                int32_t zi2 = (int32_t)(((int64_t)zi * zi) >> MB_FRAC);
                if (zr2 + zi2 > MB_FOUR)
                    break;
                int32_t new_zi = (int32_t)(((int64_t)zr * zi) >> (MB_FRAC - 1)) + ci;
                zr = zr2 - zi2 + cr;
                zi = new_zi;
                iter++;
            }

            DrawPixel(px, py, mb_colour(iter, max_iter));
            cr_row += scale;
        }
        /* Allow user to break with Ctrl-C every scanline */
        CheckAbort();
    }
    if (Option.Refresh)
        Display_Refresh();
}

void MIPS16 cmd_mandelbrot(void)
{
    unsigned char *p;

    CheckDisplay();

    if (!mb_initialised)
        mb_reset();

    if ((p = checkstring(cmdline, (unsigned char *)"DRAW")))
    {
        /* MANDELBROT DRAW [maxiter] */
        if (*p)
        {
            mb_maxiter = getint(p, 1, 10000);
        }
        mb_draw();
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"PAN")))
    {
        /* MANDELBROT PAN dx, dy */
        getcsargs(&p, 3);
        if (argc != 3)
            SyntaxError();
        int dx = getinteger(argv[0]);
        int dy = getinteger(argv[2]);
        mb_centre_r += (int32_t)((int64_t)mb_scale * dx);
        mb_centre_i += (int32_t)((int64_t)mb_scale * dy);
        mb_draw();
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"ZOOM")))
    {
        /* MANDELBROT ZOOM factor  (>1 = zoom in, <1 = zoom out) */
        MMFLOAT factor = getnumber(p);
        if (factor <= 0.0)
            error("Zoom factor must be > 0");
        /* Divide scale by factor to zoom in */
        double new_scale = (double)mb_scale / factor;
        if (new_scale < 1.0)
            new_scale = 1.0;
        mb_scale = (int32_t)new_scale;
        if (mb_scale < 1)
            mb_scale = 1;
        mb_draw();
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CENTRE")) ||
             (p = checkstring(cmdline, (unsigned char *)"CENTER")))
    {
        /* MANDELBROT CENTRE x, y - re-centre on pixel coords from last draw */
        getcsargs(&p, 3);
        if (argc != 3)
            SyntaxError();
        int px = getint(argv[0], 0, HRes - 1);
        int py = getint(argv[2], 0, VRes - 1);
        /* Convert pixel to complex coordinate */
        int32_t origin_r = mb_centre_r - (int32_t)((int64_t)mb_scale * (HRes >> 1));
        int32_t origin_i = mb_centre_i - (int32_t)((int64_t)mb_scale * (VRes >> 1));
        mb_centre_r = origin_r + (int32_t)((int64_t)mb_scale * px);
        mb_centre_i = origin_i + (int32_t)((int64_t)mb_scale * py);
        mb_draw();
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        /* MANDELBROT RESET */
        mb_reset();
    }
    else
    {
        /* Bare MANDELBROT with no subcommand - just draw */
        if (*cmdline)
        {
            mb_maxiter = getint(cmdline, 1, 10000);
        }
        mb_draw();
    }
}
#endif

/*  @endcond */
