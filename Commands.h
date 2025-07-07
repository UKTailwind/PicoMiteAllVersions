/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

commands.h

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
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#include <stdbool.h>

#ifdef __cplusplus
#include "PicoMite.h"
extern "C" {
typedef struct s_forstack {
    CombinedPtr forptr;                           // pointer to the FOR command in program memory
    CombinedPtr nextptr;                          // pointer to the NEXT command in program memory
    void *var;                              // value of the FOR variable
    unsigned char vartype;                           // type of the variable
    unsigned char level;                             // the sub/function level that the loop was created
    union u_totype {
        MMFLOAT f;                            // the TO value if it is a float
        long long int  i;                    // the TO value if it is an integer
    } tovalue;
    union u_steptype {
        MMFLOAT f;                            // the STEP value if it is a float
        long long int  i;                    // the STEP value if it is an integer
    } stepvalue;
} forstackval;
extern forstackval g_forstack[MAXFORLOOPS + 1] ;
typedef struct s_dostack {
    CombinedPtr evalptr;                          // pointer to the expression to be evaluated
    CombinedPtr loopptr;                          // pointer to the loop statement
    CombinedPtr doptr;                            // pointer to the DO statement
    unsigned char level;                             // the sub/function level that the loop was created
} dostackval;
extern struct s_dostack g_dostack[MAXDOLOOPS];
extern CombinedPtr gosubstack[MAXGOSUB];
extern CombinedPtr errorstack[MAXGOSUB];
extern void ListProgram(CombinedPtr p, int all);
extern CombinedPtr llist(unsigned char *b, CombinedPtr p);
extern void array_set(CombinedPtr tp);
extern CombinedPtr CheckIfTypeSpecified(CombinedPtr p, int *type, int AllowDefaultType);
extern void array_slice(CombinedPtr tp);
extern void array_add(CombinedPtr tp);
extern void array_insert(CombinedPtr tp);
extern CombinedPtr KeyInterrupt;
extern CombinedPtr OnKeyGOSUB;
struct sa_data {
    CombinedPtr SaveNextDataLine;
    int SaveNextData;
};
extern struct sa_data datastore[MAXRESTORE];
#endif

extern unsigned char topicbuff[STRINGSIZE];
extern unsigned char messagebuff[STRINGSIZE];
extern unsigned char addressbuff[20];
extern int g_forindex;
extern unsigned char cmdlinebuff[STRINGSIZE];
extern int g_doindex;
extern int gosubindex;
extern unsigned char g_DimUsed;

extern void MIPS16 ListNewLine(int *ListCnt, int all);
// definitions related to setting video off and on
extern const unsigned int CaseOption;
extern volatile bool Keycomplete;
extern int keyselect;
extern uint64_t g_flag;
#define TRACE_BUFF_SIZE  128

extern unsigned int BusSpeed;
extern unsigned char EchoOption;
extern unsigned char *GetFileName(unsigned char* CmdLinePtr, unsigned char *LastFilePtr);
extern void mergefile(unsigned char *fname, unsigned char *MemPtr);
extern volatile unsigned int ScrewUpTimer;
extern void SaveContext(void);
extern void RestoreContext(bool keep);
extern void do_end(bool ecmd);
extern int restorepointer;
extern unsigned short *frame, *outframe;
extern bool framecursor;
extern int format_string(char *c, int n);

#ifdef __cplusplus
}
#endif

#endif
/*  @endcond */
