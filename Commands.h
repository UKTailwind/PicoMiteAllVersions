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
#include <stdbool.h>
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#include "ffconf.h"
typedef struct s_forstack
{
    unsigned char *forptr;  // pointer to the FOR command in program memory
    unsigned char *nextptr; // pointer to the NEXT command in program memory
    void *var;              // value of the FOR variable
    unsigned char vartype;  // type of the variable
    unsigned char level;    // the sub/function level that the loop was created
    union u_totype
    {
        MMFLOAT f;       // the TO value if it is a float
        long long int i; // the TO value if it is an integer
    } tovalue;
    union u_steptype
    {
        MMFLOAT f;       // the STEP value if it is a float
        long long int i; // the STEP value if it is an integer
    } stepvalue;
} forstackval;
extern unsigned char topicbuff[STRINGSIZE];
extern unsigned char messagebuff[STRINGSIZE];
extern unsigned char addressbuff[20];

extern struct s_forstack g_forstack[MAXFORLOOPS + 1];
extern int g_forindex;
extern unsigned char cmdlinebuff[STRINGSIZE];
#ifdef MMBASIC_FM
extern int fm_program_launched_from_fm;
extern char fm_relaunch_status[STRINGSIZE * 2];
extern int fm_relaunch_status_valid;
extern int fm_suppress_error_output;
extern char fm_last_launched_bas[FF_MAX_LFN];
extern char fm_error_file[FF_MAX_LFN];
extern int fm_error_line;
extern int fm_error_char;
extern int fm_error_location_valid;
extern int fm_pending_edit_seek_valid;
extern int fm_pending_edit_seek_line;
extern int fm_pending_edit_seek_char;
extern int fm_sanitize_next_console_input;
#endif
typedef struct s_dostack
{
    unsigned char *evalptr; // pointer to the expression to be evaluated
    unsigned char *loopptr; // pointer to the loop statement
    unsigned char *doptr;   // pointer to the DO statement
    unsigned char level;    // the sub/function level that the loop was created
    unsigned char untiltest; // 1 if DO UNTIL (exits when condition becomes true)
} dostackval;

extern struct s_dostack g_dostack[MAXDOLOOPS];
extern int g_doindex;

extern unsigned char *gosubstack[MAXGOSUB];
extern unsigned char *errorstack[MAXGOSUB];
extern int gosubindex;
extern unsigned char g_DimUsed;

// extern unsigned char *GetFileName(char* CmdLinePtr, unsigned char *LastFilePtr);
// extern void mergefile(unsigned char *fname, unsigned char *MemPtr);
extern void MIPS16 ListFile(char *pp, int all);
extern void ListFilePaged(char *pp);
extern void ListProgram(unsigned char *p, int all);
extern void ListProgramPaged(unsigned char *prog);
extern unsigned char *llist(unsigned char *b, unsigned char *p);
extern unsigned char *CheckIfTypeSpecified(unsigned char *p, int *type, int AllowDefaultType);

extern void MIPS16 ListNewLine(int *ListCnt, int all);
// definitions related to setting video off and on
extern const unsigned int CaseOption;
extern volatile bool Keycomplete;
extern char *KeyInterrupt;
extern int keyselect;
extern uint64_t g_flag;
#define TRACE_BUFF_SIZE 128

extern unsigned int BusSpeed;
extern unsigned char *OnKeyGOSUB;
extern unsigned char EchoOption;
extern unsigned char *GetFileName(unsigned char *CmdLinePtr, unsigned char *LastFilePtr);
extern void mergefile(unsigned char *fname, unsigned char *MemPtr);
extern volatile unsigned int ScrewUpTimer;
struct sa_data
{
    unsigned char *SaveNextDataLine;
    int SaveNextData;
};
extern void SaveContext(void);
extern void RestoreContext(bool keep);
extern void do_end(bool ecmd);
extern struct sa_data datastore[MAXRESTORE];
extern int restorepointer;
extern unsigned short *frame, *outframe;
extern bool framecursor;
extern void closeframe(void);
extern void array_slice(unsigned char *tp);
extern void array_insert(unsigned char *tp);
extern void array_add(unsigned char *tp);
extern void array_set(unsigned char *tp);
extern int format_string(char *c, int n);

static inline CommandToken commandtbl_decode(const unsigned char *p)
{
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f) << 7);
}

#endif
/*  @endcond */
