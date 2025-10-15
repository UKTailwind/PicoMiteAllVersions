/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Memory.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

#ifndef MEMORY_HEADER
#define MEMORY_HEADER

/* ============================================================================
 * Standard includes
 * ============================================================================ */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "configuration.h"

/* ============================================================================
 * Token table section
 * ============================================================================ */
#ifdef INCLUDE_TOKEN_TABLE
/* All other tokens (keywords, functions, operators) should be inserted in this table
 * Format:
 *    TEXT      TYPE                P  FUNCTION TO CALL
 * where type is T_NA, T_FUN, T_FNA or T_OPER augmented by the types T_STR and/or T_NBR
 * and P is the precedence (which is only used for operators)
 */
#endif

/* ============================================================================
 * Main header content
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

/* ============================================================================
 * Constants - Memory page management
 * ============================================================================ */
#define PAGESIZE 256
#define PAGEBITS 2
#define PUSED 1 // Page is in use
#define PLAST 2 // Last page in allocation
#define PAGESPERWORD ((sizeof(unsigned int) * 8) / PAGEBITS)

/* ============================================================================
 * Macros - Memory alignment
 * ============================================================================ */
#define MRoundUp(a) (((a) + (PAGESIZE - 1)) & (~(PAGESIZE - 1)))
#define MRoundUpK2(a) (((a) + (PAGESIZE * 8 - 1)) & (~(PAGESIZE * 8 - 1)))

/* ============================================================================
 * Type definitions - Memory request types
 * ============================================================================ */
typedef enum _M_Req
{
    M_PROG,
    M_VAR,
    M_LIMITED
} M_Req;

/* ============================================================================
 * Type definitions - Control structure
 * ============================================================================ */
struct s_ctrl
{
    short int x1, y1, x2, y2; // Coordinates of the touch sensitive area
    int fc, bc;               // Foreground and background colors
    int fcc;                  // Foreground color for caption (default when control was created)
    float value;              // Current value
    float min, max, inc;      // Spinbox min/max and increment (also used by radio buttons, gauge, LEDs)
    unsigned char *s;         // Caption string
    unsigned char *fmt;       // Format string for FORMATBOX
    unsigned char page;       // Display page
    unsigned char ref;        // Reference number
    unsigned char type;       // Control type (button, etc)
    unsigned char state;      // State (disabled, etc)
    unsigned char font;       // Font in use when control was created (used when redrawing)
    unsigned char dummy[3];   // Padding
};

/* ============================================================================
 * External variables - Memory management
 * ============================================================================ */
extern unsigned char *strtmp[];    // Track temporary string space on the heap
extern int TempMemoryTop;          // Last index used for allocating temp memory
extern bool g_TempMemoryIsChanged; // Prevent unnecessary scanning of strtmp[]

/* ============================================================================
 * External variables - Heap memory
 * ============================================================================ */
extern unsigned char *MMHeap;
extern unsigned char *DOS_ProgMemory;
extern uint32_t heap_memory_size;
extern unsigned char __attribute__((aligned(256))) AllMemory[];

/* ============================================================================
 * External variables - Frame buffers (platform-specific)
 * ============================================================================ */
#ifdef PICOMITEVGA
extern unsigned char *WriteBuf;
extern unsigned char *FrameBuf;
extern unsigned char *SecondFrame;
extern unsigned char *DisplayBuf;
extern unsigned char *LayerBuf;
extern unsigned char *SecondLayer;
#else
extern unsigned char *WriteBuf;
extern unsigned char *FrameBuf;
extern unsigned char *LayerBuf;
#endif

extern unsigned char *FRAMEBUFFER;
extern uint32_t framebuffersize;

/* ============================================================================
 * External variables - GUI controls
 * ============================================================================ */
extern struct s_ctrl *Ctrl; // List of GUI controls

/* ============================================================================
 * Function declarations - Heap initialization
 * ============================================================================ */
void InitHeap(bool all);
unsigned char *HeapBottom(void);

/* ============================================================================
 * Function declarations - Memory allocation
 * ============================================================================ */
void m_alloc(int type);
void *GetMemory(int msize);
void *GetSystemMemory(int msize);
void *GetTempMemory(int NbrBytes);
void *GetTempStrMemory(void);
void *GetAlignedMemory(int size);
void *ReAllocMemory(void *addr, size_t msize);

/* ============================================================================
 * Function declarations - Memory deallocation
 * ============================================================================ */
void FreeMemory(unsigned char *addr);
void FreeMemorySafe(void **addr);
void ClearTempMemory(void);
void ClearSpecificTempMemory(void *addr);

/* ============================================================================
 * Function declarations - Memory information
 * ============================================================================ */
int FreeSpaceOnHeap(void);
int LargestContiguousHeap(void);
int MemSize(void *addr);

#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

#endif /* MEMORY_HEADER */

/*  @endcond */