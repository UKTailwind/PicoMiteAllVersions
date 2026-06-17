/***********************************************************************************************************************
PicoMite MMBasic

DrawInternal.h

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
/* Internal interfaces shared between the Draw subsystem source files
   (Draw.c, DrawFill.c, Draw3D.c, TileMap.c, Pointer.c). These were
   file-local to Draw.c before it was split by subsystem; they are NOT part
   of the public drawing API (that is Draw.h). Keep additions here to the
   minimum the split files genuinely share. Requires MMBasic_Includes.h /
   Hardware_Includes.h to have been included first. */
#ifndef DRAWINTERNAL_H
#define DRAWINTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* Helper macros for strided array access (used for struct member arrays) */
#define STRIDE_FLOAT(ptr, idx, stride) (*(MMFLOAT *)((char *)(ptr) + (idx) * (stride)))
#define STRIDE_INT(ptr, idx, stride) (*(long long int *)((char *)(ptr) + (idx) * (stride)))

/* ----------------------------------------------------------------------
   Polygon scanline-fill engine (DrawFill.c). Driven by the POLYGON and
   LINE GRAPH commands in Draw.c (via polygon()) and directly by
   DrawPolygon() in Draw3D.c, which loads the vertex arrays itself and
   then runs begin/end fill. */
typedef struct
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} rgb_t;
#define TFLOAT float
typedef struct
{
    TFLOAT xpos;    // current position and heading
    TFLOAT ypos;    // (uses floating-point numbers for
    TFLOAT heading; //  increased accuracy)

    rgb_t pen_color;  // current pen color
    rgb_t fill_color; // current fill color
    bool pendown;     // currently drawing?
    bool filled;      // currently filling?
} fill_t;

extern fill_t main_fill;
extern fill_t backup_fill;
extern int main_fill_poly_vertex_count; // polygon vertex count
extern TFLOAT *main_fill_polyX;         // polygon vertex x-coords
extern TFLOAT *main_fill_polyY;         // polygon vertex y-coords

void fill_set_pen_color(int red, int green, int blue);
void fill_set_fill_color(int red, int green, int blue);
void fill_begin_fill(void);
void fill_end_fill(int count, int ystart, int yend);
void polygon(unsigned char *p, int close);

/* ----------------------------------------------------------------------
   Sprite / blit shared bits. */
// Magic number to indicate buffer is a triangle buffer (not rectangular).
// Used by the triangle save/restore in Draw.c, the sprite engine
// (Sprite.c) and the BLIT command (Blit.c).
#define TRIANGLE_BUFFER_MARKER 9999
/* BMP line loader callback: defined in Sprite.c (SPRITE LOAD BMP path),
   also driven by BLIT LOADBMP in Blit.c. Callers stash their load state
   in readstate before invoking the BMP decoder. */
bool loadBMPlinecallback(int *imagewidth, int *imageheight, uint32_t *linedata, int *linenumber);
extern s_ReadBMP *readstate;

/* ----------------------------------------------------------------------
   Sprite/cursor palette tables and char-to-index helper (tables defined
   in Draw.c). Used by the legacy SPRITE LOAD command (Sprite.c) AND by
   the GUI CURSOR LOAD overlay (Pointer.c). */
extern const uint32_t sprite_color_mode0[16];
extern const uint32_t sprite_color_mode1[16];

// Helper: sprite-file char ' ', '0'..'9', 'A'..'F' -> palette index 0..15, or -1 for transparent.
static inline int spriteCharToColorIndex(unsigned char c)
{
    if (c == ' ')
        return -1;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* ----------------------------------------------------------------------
   Resolve a BASIC argument that may be a literal, a scalar variable, an
   array (or struct-member array) into a typed pointer + element count +
   stride. Shared static-inline: used throughout Draw.c and by the
   polygon engine in DrawFill.c. */
static inline void getargaddress(unsigned char *p, long long int **ip, MMFLOAT **fp, int *n, int *stride)
{
    unsigned char *ptr = NULL;
    *fp = NULL;
    *ip = NULL;
    if (stride)
        *stride = sizeof(MMFLOAT); // Default stride for normal arrays (8 bytes)
    char pp[STRINGSIZE] = {0};
    strcpy(pp, (char *)p);
    if (!isnamestart(pp[0]))
    { // found a literal
        *n = 1;
        return;
    }
    ptr = findvar((unsigned char *)pp, V_FIND | V_EMPTY_OK | V_NOFIND_NULL);
    if (ptr && g_vartbl[g_VarIndex].type & (T_NBR | T_INT))
    {
        if (g_vartbl[g_VarIndex].dims[0] <= 0)
        { // simple variable
            *n = 1;
            return;
        }
        else
        { // array or array element
            if (*n == 0)
                *n = g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase;
            else
                *n = (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase) < *n ? (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase) : *n;
            skipspace(p);
            do
            {
                p++;
            } while (isnamechar(*p));
            if (*p == '!' || *p == '%')
                p++;
            if (*p == '(')
            {
                p++;
                skipspace(p);
                if (*p != ')')
                { // array element
                    *n = 1;
                    return;
                }
            }
        }
        if (g_vartbl[g_VarIndex].dims[1] != 0)
            StandardError(6);
        if (g_vartbl[g_VarIndex].type & T_NBR)
            *fp = (MMFLOAT *)ptr;
        else
            *ip = (long long int *)ptr;
    }
#ifdef STRUCTENABLED
    // Check if this is a struct member access (g_StructMemberType set by findvar)
    else if (ptr && (g_vartbl[g_VarIndex].type & T_STRUCT) && g_StructMemberType != 0)
    {
        // Caller must handle stride for struct member arrays
        if (stride == NULL)
            StandardError(47);

        // Check if this is an array element access (e.g., boxes(i%).x) vs whole array (boxes().x)
        // We need to check if there's an index expression in the parentheses
        unsigned char *pcheck = p;
        skipspace(pcheck);
        // Skip past variable name
        while (isnamechar(*pcheck))
            pcheck++;
        if (*pcheck == '!' || *pcheck == '%' || *pcheck == '$')
            pcheck++;
        skipspace(pcheck);
        if (*pcheck == '(')
        {
            pcheck++;
            skipspace(pcheck);
            if (*pcheck != ')')
            {
                // There's an index expression - this is a single element access
                *n = 1;
                return;
            }
        }

        // This is a struct array with member access like points().x
        int struct_type = (int)g_vartbl[g_VarIndex].size;
        int struct_size = g_structtbl[struct_type]->total_size;

        // Get member type from g_StructMemberType (set by findvar/ResolveStructMember)
        int member_type = g_StructMemberType;

        // Check member type is numeric
        if (!(member_type & (T_NBR | T_INT)))
        {
            *n = 1; // Not a numeric member, treat as single value
            return;
        }

        // Calculate number of elements from array dimensions
        if (*n == 0)
            *n = g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase;
        else
            *n = (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase) < *n ? (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase) : *n;

        // Check for 2D arrays (not supported)
        if (g_vartbl[g_VarIndex].dims[1] != 0)
            StandardError(6);

        // Set stride to structure size
        *stride = struct_size;

        // Set the appropriate pointer based on member type
        if (member_type & T_NBR)
            *fp = (MMFLOAT *)ptr;
        else
            *ip = (long long int *)ptr;
    }
#endif
    else
    {
        *n = 1; // may be a function call
    }
}

#endif /* DRAWINTERNAL_H */
