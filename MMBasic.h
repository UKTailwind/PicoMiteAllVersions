/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

MMBasic.h

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

#ifndef __MMBASIC_H
#define __MMBASIC_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Standard includes */
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* Hardware family detection */
#if defined(MAXIMITE) || defined(UBW32) || defined(DUINOMITE) || defined(COLOUR)
#define MMFAMILY
#endif

#include "configuration.h"

/* ============================================================================
 * Type definitions for data items
 * Used in tokens, variables and arguments to functions
 * ============================================================================ */
#define T_NOTYPE 0x00   // type not set or discovered
#define T_NBR 0x01      // number (or float) type
#define T_STR 0x02      // string type
#define T_INT 0x04      // 64 bit integer type
#define T_PTR 0x08      // the variable points to another variable's data
#define T_IMPLIED 0x10  // the variables type does not have to be specified with a suffix
#define T_CONST 0x20    // the contents of this variable cannot be changed
#define T_BLOCKED 0x40  // Hash table entry blocked after ERASE
#define T_EXPLICIT 0x80 // Was the variable specified with a type suffix

#define TypeMask(a) ((a) & (T_NBR | T_INT | T_STR))

/* ============================================================================
 * Token types
 * ============================================================================ */
#define T_INV 0     // an invalid token
#define T_NA 0      // an invalid token
#define T_CMD 0x10  // a command
#define T_OPER 0x20 // an operator
#define T_FUN 0x40  // a function (also used for a function that can operate as a command)
#define T_FNA 0x80  // a function that has no arguments

#define C_BASETOKEN 0x80 // the base of the token numbers

/* ============================================================================
 * Program line flags
 * ============================================================================ */
#define T_CMDEND 0  // end of a command
#define T_NEWLINE 1 // Single byte indicating the start of a new line
#define T_LINENBR 2 // three bytes for a line number
#define T_LABEL 3   // variable length indicating a label

#define E_END 255 // dummy last operator in an expression

/* ============================================================================
 * Variable finding flags (used in findvar() function)
 * ============================================================================ */
#define V_FIND 0x0000        // straight forward find, if not found it is created and set to zero
#define V_NOFIND_ERR 0x0200  // throw an error if not found
#define V_NOFIND_NULL 0x0400 // return a null pointer if not found
#define V_DIM_VAR 0x0800     // dimension an array
#define V_LOCAL 0x1000       // create a local variable
#define V_EMPTY_OK 0x2000    // allow an empty array variable. ie, var()
#define V_FUNCT 0x4000       // we are defining the name of a function

/* ============================================================================
 * Expression evaluation flags
 * ============================================================================ */
#define E_NOERROR true
#define E_ERROR 0
#define E_DONE_GETVAL 0b10

/* ============================================================================
 * Boolean type definition
 * ============================================================================ */
#if !defined(BOOL_ALREADY_DEFINED)
#define BOOL_ALREADY_DEFINED
    typedef enum _BOOL
    {
        FALSE = 0,
        TRUE
    } BOOL;
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */
#define MAXLINENBR 65001 // maximum acceptable line number
#define NOLINENBR (MAXLINENBR + 1)

/* ============================================================================
 * Utility macros
 * ============================================================================ */

/* Skip whitespace - finishes with x pointing to the next non-space char */
#define skipspace(x)  \
    while (*x == ' ') \
    x++

/* Skip to the next element - finishes pointing to the zero char that precedes an element */
#define skipelement(x) \
    while (*x)         \
    x++

/* Skip to the next line */
#define skipline(x)                                           \
    while (!(x[-1] == 0 && (x[0] == T_NEWLINE || x[0] == 0))) \
    x++

/* Find a token - finishes pointing to the token or zero char if not found in the line */
#define findtoken(x)          \
    while (*x != (tkn) && *x) \
    x++

#define IsDigitinline(a) ((a) >= '0' && (a) <= '9')

/* Character classification macros */
#ifdef rp2350
#define isnamestart(c) (name_start_tbl[((uint8_t)(c))])
#define isnamechar(c) (name_char_tbl[((uint8_t)(c))])
#define isnameend(c) (name_end_tbl[((uint8_t)(c))])
#define mytoupper(c) (charmap[(uint8_t)(c)])
#else
#define isnamestart(c) (isalpha((unsigned char)(c)) || (c) == '_')
#define isnamechar(c) (isalnum((unsigned char)(c)) || (c) == '_' || (c) == '.')
#define isnameend(c) (isalnum((unsigned char)(c)) || (c) == '_' || (c) == '.' || (c) == '$' || (c) == '!' || (c) == '%')
#define mytoupper(c) (toupper(c))
#endif

/* Token table access macros - safer versions that evaluate parameter only once */
#define tokentype(i) ({                                      \
    unsigned int _idx = (i) - C_BASETOKEN;                   \
    (_idx < (TokenTableSize - 1)) ? tokentbl[_idx].type : 0; \
})

#define tokenfunction(i) ({                                                 \
    unsigned int _idx = (i) - C_BASETOKEN;                                  \
    (_idx < (TokenTableSize - 1)) ? tokentbl[_idx].fptr : tokentbl[0].fptr; \
})

#define tokenname(i) ({                                                        \
    unsigned int _idx = (i) - C_BASETOKEN;                                     \
    (_idx < (TokenTableSize - 1)) ? tokentbl[_idx].name : (unsigned char *)""; \
})

#define commandfunction(i) ({                                                               \
    ((unsigned int)(i) < (CommandTableSize - 1)) ? commandtbl[i].fptr : commandtbl[0].fptr; \
})

#define commandname(i) ({                                                                    \
    ((unsigned int)(i) < (CommandTableSize - 1)) ? commandtbl[i].name : (unsigned char *)""; \
})

/* Argument parsing macros */
#define getargs(x, y, s)                               \
    unsigned char argbuf[STRINGSIZE + STRINGSIZE / 2]; \
    unsigned char *argv[y];                            \
    int argc;                                          \
    makeargs(x, y, argbuf, argv, &argc, s)

#ifdef rp2350
#define getcsargs(x, y)                                \
    unsigned char argbuf[STRINGSIZE + STRINGSIZE / 2]; \
    unsigned char *argv[y];                            \
    int argc;                                          \
    MakeCommaSeparatedArgs(x, y, argbuf, argv, &argc)
#else
#define getcsargs(x, y)                                \
    unsigned char argbuf[STRINGSIZE + STRINGSIZE / 2]; \
    unsigned char *argv[y];                            \
    int argc;                                          \
    makeargs(x, y, argbuf, argv, &argc, (unsigned char *)",")
#endif

    /* ============================================================================
     * Type definitions
     * ============================================================================ */
    typedef uint16_t CommandToken;

    /* Function table structure */
    struct s_funtbl
    {
        char name[MAXVARLEN];
        uint32_t index;
    };

    /* Variable table structure */
    typedef struct s_vartbl
    {
        unsigned char name[MAXVARLEN];
        unsigned char type;
        unsigned char level;
        unsigned char size;
        unsigned char namelen;
#ifdef rp2350
        int __attribute__((aligned(4))) dims[MAXDIM];
#else
    short __attribute__((aligned(4))) dims[MAXDIM];
#endif
        union u_val
        {
            MMFLOAT f;
            long long int i;
            MMFLOAT *fa;
            long long int *ia;
            unsigned char *s;
        } val;
    } vartbl_val;

    /* Hash table structure */
    typedef struct s_hash
    {
        short hash;
        short level;
    } hash_val;

    /* Token table structure */
    struct s_tokentbl
    {
        unsigned char *name;
        unsigned char type;
        unsigned char precedence;
        void (*fptr)(void);
    };

    /* ============================================================================
     * External variables - Character classification tables
     * ============================================================================ */
    extern const unsigned char name_end_tbl[256];
    extern const unsigned char name_start_tbl[256];
    extern const unsigned char name_char_tbl[256];
    extern const unsigned char charmap[256];

    /* ============================================================================
     * External variables - Variable management
     * ============================================================================ */
    extern struct s_vartbl s_vartbl_val;
    extern struct s_funtbl funtbl[MAXSUBFUN];
    extern struct s_vartbl g_vartbl[];

    extern int g_varcnt;
    extern int g_Globalvarcnt;
    extern int g_Localvarcnt;
    extern int g_VarIndex;
    extern int g_LocalIndex;

    /* ============================================================================
     * External variables - Options and settings
     * ============================================================================ */
    extern int g_OptionBase;
    extern unsigned char OptionExplicit, OptionEscape, OptionConsole;
    extern bool OptionNoCheck;
    extern unsigned char DefaultType;
    extern int OptionErrorSkip;

    /* ============================================================================
     * External variables - Token tables
     * ============================================================================ */
    extern int CommandTableSize, TokenTableSize;
    extern const struct s_tokentbl tokentbl[];
    extern const struct s_tokentbl commandtbl[];

    extern unsigned char tokenTHEN, tokenELSE, tokenGOTO, tokenEQUAL, tokenTO, tokenSTEP;
    extern unsigned char tokenWHILE, tokenUNTIL, tokenGOSUB, tokenAS, tokenFOR;

    extern unsigned short cmdIF, cmdENDIF, cmdEND_IF, cmdELSEIF, cmdELSE_IF, cmdELSE;
    extern unsigned short cmdSELECT_CASE, cmdFOR, cmdNEXT, cmdWHILE, cmdENDSUB, cmdENDFUNCTION;
    extern unsigned short cmdLOCAL, cmdSTATIC, cmdCASE, cmdDO, cmdLOOP, cmdCASE_ELSE, cmdEND_SELECT;
    extern unsigned short cmdSUB, cmdFUN, cmdCSUB, cmdIRET, cmdComment, cmdEndComment;

    /* ============================================================================
     * External variables - Error handling
     * ============================================================================ */
    extern volatile int MMAbort;
    extern jmp_buf mark;
    extern jmp_buf jmprun;
    extern jmp_buf ErrNext;
    extern unsigned char BreakKey;
    extern int MMerrno;
    extern char MMErrMsg[MAXERRMSG];

    /* ============================================================================
     * External variables - Program execution
     * ============================================================================ */
    extern int ProgMemSize;
    extern int NextData;
    extern unsigned char *NextDataLine;
    extern unsigned char *CurrentLinePtr, *SaveCurrentLinePtr;
    extern unsigned char *ContinuePoint;
    extern int ProgramChanged;

    extern unsigned char *LibMemory;
    extern unsigned char *ProgMemory;
    extern int PSize;

    extern unsigned char *subfun[];
    extern char CurrentSubFunName[MAXVARLEN + 1];
    extern char CurrentInterruptName[MAXVARLEN + 1];

    /* ============================================================================
     * External variables - Buffers
     * ============================================================================ */
    extern unsigned char inpbuf[];
    extern unsigned char tknbuf[];
    extern unsigned char lastcmd[];
    extern unsigned char PromptString[MAXPROMPTLEN];

    /* ============================================================================
     * External variables - Operator arguments and results
     * ============================================================================ */
    extern MMFLOAT farg1, farg2, fret;
    extern long long int iarg1, iarg2, iret;
    extern unsigned char *sarg1, *sarg2, *sret;
    extern int targ;

    /* ============================================================================
     * External variables - Command execution
     * ============================================================================ */
    extern int cmdtoken;
    extern unsigned char *cmdline;
    extern unsigned char *nextstmt;
    extern unsigned char *ep;

    /* ============================================================================
     * External variables - Miscellaneous
     * ============================================================================ */
    extern int multi;
    extern int emptyarray;
    extern int TempStringClearStart;

#if defined(MMFAMILY)
    extern unsigned char FunKey[NBRPROGKEYS][MAXKEYLEN + 1];
#endif

#if defined(MMFAMILY) || defined(DOS)
    extern unsigned char *ModuleTable[MAXMODULES];
    extern int NbrModules;
#endif

    /* ============================================================================
     * Function declarations - Error handling
     * ============================================================================ */
    void error(char *msg, ...);
    void SyntaxError(void);
    void StandardError(int n);
    void StandardErrorParam(int n, int m);
    void StandardErrorParam2(int n, int m, int l);
    void StandardErrorParam3(int n, int m, int l, int h);
    void StandardErrorParamS(int n, char *m);

    /* ============================================================================
     * Function declarations - Initialization and cleanup
     * ============================================================================ */
    void InitBasic(void);
    void ClearVars(int level, bool all);
    void ClearStack(void);
    void ClearRuntime(bool all);
    void ClearProgram(bool psram);

    /* ============================================================================
     * Function declarations - Type conversions
     * ============================================================================ */
    int FloatToInt32(MMFLOAT);
    long long int FloatToInt64(MMFLOAT x);
    void IntToStrPad(char *p, long long int nbr, signed char padch, int maxch, int radix);
    void IntToStr(char *strr, long long int nbr, unsigned int base);
    void FloatToStr(char *p, MMFLOAT f, int m, int n, unsigned char ch);

    /* ============================================================================
     * Function declarations - Argument and expression parsing
     * ============================================================================ */
    void makeargs(unsigned char **tp, int maxargs, unsigned char *argbuf, unsigned char *argv[], int *argc, unsigned char *delim);
    void MakeCommaSeparatedArgs(unsigned char **tp, int maxargs, unsigned char *argbuf, unsigned char *argv[], int *argc);
    void *DoExpression(unsigned char *p, int *t);
    unsigned char *evaluate(unsigned char *p, MMFLOAT *fa, long long int *ia, unsigned char **sa, int *ta, int noerror);
    unsigned char *doexpr(unsigned char *p, MMFLOAT *fa, long long int *ia, unsigned char **sa, int *oo, int *t);
    MMFLOAT getnumber(unsigned char *p);
    long long int getinteger(unsigned char *p);
    long long int getint(unsigned char *p, long long int min, long long int max);
    unsigned char *getstring(unsigned char *p);
    unsigned char *skipvar(unsigned char *p, int noerror);
    unsigned char *skipexpression(unsigned char *p);
    unsigned char *getclosebracket(unsigned char *p);

    /* ============================================================================
     * Function declarations - Variable management
     * ============================================================================ */
    void *findvar(unsigned char *, int);
    void erasearray(unsigned char *n);
    int FunctionType(unsigned char *p);

    /* ============================================================================
     * Function declarations - Tokenization and execution
     * ============================================================================ */
    void tokenise(int console);
    void ExecuteProgram(unsigned char *);
    void AddProgramLine(int append);
    int GetCommandValue(unsigned char *n);
    int GetTokenValue(unsigned char *n);

    /* ============================================================================
     * Function declarations - Program navigation
     * ============================================================================ */
    unsigned char *findline(int, int);
    unsigned char *findlabel(unsigned char *labelptr);
    unsigned char *GetNextCommand(unsigned char *p, unsigned char **CLine, unsigned char *EOFMsg);
    int GetLineLength(unsigned char *p);
    int IsValidLine(int line);
    int CountLines(unsigned char *target);

    /* ============================================================================
     * Function declarations - String operations
     * ============================================================================ */
    void makeupper(unsigned char *p);
    unsigned char *checkstring(unsigned char *p, unsigned char *tkn);
    unsigned char *MtoC(unsigned char *p);
    unsigned char *CtoM(unsigned char *p);
    void Mstrcpy(unsigned char *dest, unsigned char *src);
    void Mstrcat(unsigned char *restrict dest, const unsigned char *restrict src);
    int Mstrcmp(const unsigned char *s1, const unsigned char *s2);
    unsigned char *getCstring(unsigned char *p);
    unsigned char *getFstring(unsigned char *p);
    char *fstrstr(const char *s1, const char *s2);
    void str_replace(char *target, const char *needle, const char *replacement, uint8_t ignore);
    void MIPS16 STR_REPLACE(char *target, const char *needle, const char *replacement, uint8_t ignore);

/* ============================================================================
 * Function declarations - Comparison and utilities
 * ============================================================================ */
#if defined(__PIC32MX__)
    inline int str_equal(const unsigned char *s1, const unsigned char *s2);
#else
int str_equal(const unsigned char *s1, const unsigned char *s2);
#endif
    int mem_equal(unsigned char *s1, unsigned char *s2, int i);

    /* ============================================================================
     * Function declarations - Subroutines and functions
     * ============================================================================ */
    void DefinedSubFun(int iscmd, unsigned char *cmd, int index, MMFLOAT *fa, long long int *i64, unsigned char **sa, int *t);
    int FindSubFun(unsigned char *p, int type);
    void PrepareProgram(int ErrAbort);

    /* ============================================================================
     * Function declarations - I/O
     * ============================================================================ */
    void MMPrintString(char *s);
    void MMfputs(unsigned char *p, int filenbr);

    /* ============================================================================
     * Function declarations - Miscellaneous
     * ============================================================================ */
    int CheckEmpty(char *p);
    void checkend(unsigned char *p);
    void InsertLastcmd(unsigned char *s);
    unsigned char *GetIntAddress(unsigned char *p);

#ifdef __cplusplus
}
#endif

#endif /* __MMBASIC_H */
       /*  @endcond */