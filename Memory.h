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

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "configuration.h"

/* ********************************************************************************
 All other tokens (keywords, functions, operators) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_TOKEN_TABLE
// the format is:
//    TEXT      	TYPE                P  FUNCTION TO CALL
// where type is T_NA, T_FUN, T_FNA or T_OPER argumented by the types T_STR and/or T_NBR
// and P is the precedence (which is only used for operators)

#endif


#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
// General definitions used by other modules

#ifndef MEMORY_HEADER
#define MEMORY_HEADER

extern unsigned char *strtmp[];                                       // used to track temporary string space on the heap
extern int TempMemoryTop;                                           // this is the last index used for allocating temp memory
extern bool g_TempMemoryIsChanged;						                // used to prevent unnecessary scanning of strtmp[]

typedef enum _M_Req {M_PROG, M_VAR, M_LIMITED} M_Req;

extern void m_alloc(int type);
extern void *GetMemory(int  msize);
extern void *GetTempMemory(int NbrBytes);
extern void *GetTempStrMemory(void);
extern void ClearTempMemory(void);
extern void ClearSpecificTempMemory(void *addr);
extern void TestStackOverflow(void);
extern void FreeMemory(unsigned char *addr);
extern void InitHeap(bool all);
extern unsigned char *HeapBottom(void);
extern int FreeSpaceOnHeap(void);
extern int LargestContiguousHeap(void);
extern unsigned char *DOS_ProgMemory;
extern void *ReAllocMemory(void *addr, size_t msize);
extern void FreeMemorySafe(void **addr);
extern void *GetAlignedMemory(int size);
extern void FreeMemorySafe(void **addr);
extern int MemSize(void *addr);
extern unsigned char *MMHeap;
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
extern uint32_t heap_memory_size;
extern unsigned char *FRAMEBUFFER;
extern uint32_t framebuffersize;
extern unsigned char __attribute__ ((aligned (256))) AllMemory[];
struct s_ctrl {
    short int x1, y1, x2, y2;           // the coordinates of the touch sensitive area
    int fc, bc;                         // foreground and background colours
    int fcc;                            // foreground colour for the caption (default colour when the control was created)
    float value;
    float min, max, inc;              // the spinbox minimum/maximum and the increment value. NOTE:  Radio buttons, gauge and LEDs also store data in these variables
    unsigned char *s;                            // the caption
    unsigned char *fmt;                          // pointer to the format string for FORMATBOX
    unsigned char page;                          // the display page
    unsigned char ref, type, state;              // reference nbr, type (button, etc) and the state (disabled, etc)
    unsigned char font;                          // the font in use when the control was created (used when redrawing)
    unsigned char dummy[3];
};

extern struct s_ctrl *Ctrl;             // list of the controls
#define PAGESIZE        256                                         // the allocation granuality
#define PAGEBITS        2                                           // nbr of status bits per page of allocated memory, must be a power of 2

#define PUSED           1                                           // flag that indicates that the page is in use
#define PLAST           2                                           // flag to show that this is the last page in a single allocation

#define PAGESPERWORD    ((sizeof(unsigned int) * 8)/PAGEBITS)


#define MRoundUp(a)     (((a) + (PAGESIZE - 1)) & (~(PAGESIZE - 1)))// round up to the nearest page size      [position 131:9]	
#define MRoundUpK2(a)     (((a) + (PAGESIZE*8 - 1)) & (~(PAGESIZE*8 - 1)))// round up to the nearest page size      [position 131:9]	
#endif
#endif

/*  @endcond */
