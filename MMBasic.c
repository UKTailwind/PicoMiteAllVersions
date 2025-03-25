/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/***********************************************************************************************************************
PicoMite MMBasic

MMBasic.c

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

#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include "MMBasic.h"
#include "pico/stdlib.h"
#include "Functions.h"
#include "Commands.h"
#include "Operators.h"
#include "Custom.h"
#include "Hardware_Includes.h"
#include "hardware/flash.h"
#ifndef PICOMITEWEB
#include "pico/multicore.h"
#endif

// this is the command table that defines the various tokens for commands in the source code
// most of them are listed in the .h files so you should not add your own here but instead add
// them to the appropiate .h file
#define INCLUDE_COMMAND_TABLE
const struct s_tokentbl commandtbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
};
#undef INCLUDE_COMMAND_TABLE



// this is the token table that defines the other tokens in the source code
// most of them are listed in the .h files so you should not add your own here
// but instead add them to the appropiate .h file
#define INCLUDE_TOKEN_TABLE
const struct s_tokentbl tokentbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
};
#undef INCLUDE_TOKEN_TABLE


static inline CommandToken commandtbl_decode(const unsigned char *p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}
// these are initialised at startup
int CommandTableSize, TokenTableSize;
#ifdef rp2350
struct s_funtbl funtbl[MAXSUBFUN];
//void hashlabels(int errabort);
void hashlabels(unsigned char *p,int ErrAbort);
#endif
struct s_vartbl __attribute__ ((aligned (64))) g_vartbl[MAXVARS]={0};                                            // this table stores all variables
int g_varcnt=0;                                                         // number of variables
int g_VarIndex;                                                       // Global set by findvar after a variable has been created or found
int g_Localvarcnt;                                                         // number of LOCAL variables
int g_Globalvarcnt;                                                         // number of GLOBAL variables
int g_LocalIndex;                                                     // used to track the level of local variables
unsigned char OptionExplicit, OptionEscape, OptionConsole;                                                // used to force the declaration of variables before their use
bool OptionNoCheck=false;
unsigned char DefaultType;                                                   // the default type if a variable is not specifically typed
int emptyarray=0;
int TempStringClearStart;                                           // used to prevent clearing of space in an expression that called a FUNCTION
unsigned char *subfun[MAXSUBFUN];                                            // table used to locate all subroutines and functions
char CurrentSubFunName[MAXVARLEN + 1];                              // the name of the current sub or fun
char CurrentInterruptName[MAXVARLEN + 1];                           // the name of the current interrupt function
jmp_buf jmprun;
jmp_buf mark;                                                       // longjump to recover from an error and abort
jmp_buf ErrNext;                                                    // longjump to recover from an error and continue
unsigned char inpbuf[STRINGSIZE];                                            // used to store user keystrokes until we have a line
unsigned char tknbuf[STRINGSIZE];                                            // used to store the tokenised representation of the users input line
//unsigned char lastcmd[STRINGSIZE];                                           // used to store the last command in case it is needed by the EDIT command
unsigned char PromptString[MAXPROMPTLEN];                                    // the prompt for input, an empty string means use the default
int ProgramChanged;                                                 // true if the program in memory has been changed and not saved
struct s_hash g_hashlist[MAXVARS/2]={0};
int g_hashlistpointer=0;
unsigned char *LibMemory;                                           //This is where the library is stored. At the last flash slot (4)
int multi=false;
unsigned char *ProgMemory;                                                      // program memory, this is where the program is stored
int PSize;                                                          // the size of the program stored in ProgMemory[]

int NextData;                                                       // used to track the next item to read in DATA & READ stmts
unsigned char *NextDataLine;                                                 // used to track the next line to read in DATA & READ stmts
int g_OptionBase;                                                     // track the state of OPTION BASE
int  PrepareProgramExt(unsigned char *, int, unsigned char **, int);
extern uint32_t core1stack[];;

#if defined(MMFAMILY)
unsigned char FunKey[NBRPROGKEYS][MAXKEYLEN + 1];                            // data storage for the programmable function keys
#endif

char digit[256]={
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0x10
		0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,0, //0x20
		1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0, //0x30
		0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0, //0x40
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0x50
		0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0, //0x60
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0x70
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0x80
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0x90
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0xA0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0xB0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0xC0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0xD0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //0xE0
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  //0xF0
};
///////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by operators and functions
//
int targ;                                                           // the type of the returned value
MMFLOAT farg1, farg2, fret;                                           // the two float arguments and returned value
long long int  iarg1, iarg2, iret;                                   // the two integer arguments and returned value
unsigned char *sarg1, *sarg2, *sret;                                         // the two string arguments and returned value

////////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by functions
// functions use targ, fret and sret as defined for operators (above)
unsigned char *ep;                                                           // pointer to the argument to the function terminated with a zero byte.
                                                                    // it is NOT trimmed of spaces

////////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by commands
//
int cmdtoken;                                                       // Token number of the command
unsigned char *cmdline;                                                      // Command line terminated with a zero unsigned char and trimmed of spaces
unsigned char *nextstmt;                                                     // Pointer to the next statement to be executed.
unsigned char *CurrentLinePtr, *SaveCurrentLinePtr;                                               // Pointer to the current line (used in error reporting)
unsigned char *ContinuePoint;                                                // Where to continue from if using the continue statement

extern int TraceOn;
extern unsigned char *TraceBuff[TRACE_BUFF_SIZE];
extern int TraceBuffIndex;                                          // used for listing the contents of the trace buffer
extern long long int CallCFunction(unsigned char *CmdPtr, unsigned char *ArgList, unsigned char *DefP, unsigned char *CallersLinePtr);

/////////////////////////////////////////////////////////////////////////////////////////////////
// Functions only used within MMBasic.c
//
//void getexpr(unsigned char *);
//void checktype(int *, int);
unsigned char __not_in_flash_func(*getvalue)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta);
unsigned char tokenTHEN, tokenELSE, tokenGOTO, tokenEQUAL, tokenTO, tokenSTEP, tokenWHILE, tokenUNTIL, tokenGOSUB, tokenAS, tokenFOR;
unsigned short cmdIF, cmdENDIF, cmdEND_IF, cmdELSEIF, cmdELSE_IF, cmdELSE, cmdSELECT_CASE, cmdFOR, cmdNEXT, cmdWHILE, cmdENDSUB, cmdENDFUNCTION, cmdLOCAL, cmdSTATIC, cmdCASE, cmdDO, cmdLOOP, cmdCASE_ELSE, cmdEND_SELECT;
unsigned short cmdSUB, cmdFUN, cmdCFUN, cmdCSUB, cmdIRET, cmdComment, cmdEndComment;

/********************************************************************************************************************************************
 Program management
 Includes the routines to initialise MMBasic, start running the interpreter, and to run a program in memory
*********************************************************************************************************************************************/


// Initialise MMBasic
void   MIPS16 InitBasic(void) {
    DefaultType = T_NBR;
    CommandTableSize =  (sizeof(commandtbl)/sizeof(struct s_tokentbl));
    TokenTableSize =  (sizeof(tokentbl)/sizeof(struct s_tokentbl));

    ClearProgram();

    // load the commonly used tokens
    // by looking them up once here performance is improved considerably
    tokenTHEN  = GetTokenValue( (unsigned char *)"Then");
    tokenELSE  = GetTokenValue( (unsigned char *)"Else");
    tokenGOTO  = GetTokenValue( (unsigned char *)"GoTo");
    tokenEQUAL = GetTokenValue( (unsigned char *)"=");
    tokenTO    = GetTokenValue( (unsigned char *)"To");
    tokenSTEP  = GetTokenValue( (unsigned char *)"Step");
    tokenWHILE = GetTokenValue( (unsigned char *)"While");
    tokenUNTIL = GetTokenValue( (unsigned char *)"Until");
    tokenGOSUB = GetTokenValue( (unsigned char *)"GoSub");
    tokenAS    = GetTokenValue( (unsigned char *)"As");
    tokenFOR   = GetTokenValue( (unsigned char *)"For");
    cmdLOOP  = GetCommandValue( (unsigned char *)"Loop");
    cmdIF      = GetCommandValue( (unsigned char *)"If");
    cmdENDIF   = GetCommandValue( (unsigned char *)"EndIf");
    cmdEND_IF  = GetCommandValue( (unsigned char *)"End If");
    cmdELSEIF  = GetCommandValue( (unsigned char *)"ElseIf");
    cmdELSE_IF = GetCommandValue( (unsigned char *)"Else If");
    cmdELSE    = GetCommandValue( (unsigned char *)"Else");
    cmdSELECT_CASE = GetCommandValue( (unsigned char *)"Select Case");
    cmdCASE        = GetCommandValue( (unsigned char *)"Case");
    cmdCASE_ELSE   = GetCommandValue( (unsigned char *)"Case Else");
    cmdEND_SELECT  = GetCommandValue( (unsigned char *)"End Select");
	cmdSUB = GetCommandValue( (unsigned char *)"Sub");
	cmdFUN = GetCommandValue( (unsigned char *)"Function");
    cmdLOCAL = GetCommandValue( (unsigned char *)"Local");
    cmdSTATIC = GetCommandValue( (unsigned char *)"Static");
    cmdENDSUB= GetCommandValue( (unsigned char *)"End Sub");
    cmdENDFUNCTION = GetCommandValue( (unsigned char *)"End Function");
    cmdDO=  GetCommandValue( (unsigned char *)"Do");
    cmdFOR=  GetCommandValue( (unsigned char *)"For");
    cmdNEXT= GetCommandValue( (unsigned char *)"Next");
	cmdIRET = GetCommandValue( (unsigned char *)"IReturn");
    cmdCSUB = GetCommandValue( (unsigned char *)"CSub");
    cmdComment = GetCommandValue( (unsigned char *)"/*");
    cmdEndComment = GetCommandValue( (unsigned char *)"*/");
//  SInt(CommandTableSize);
//  SIntComma(TokenTableSize);
//  SSPrintString("\r\n");

}

int CheckEmpty(char *p){
        int emptyarray=0;
        char *pp = strchr((char *)p, '(');
        if(pp){
            pp++;
            skipspace(pp);
            if(*pp == ')')emptyarray=1;
        }
        while(*(++pp)){
            if(*pp=='(')return 1; // can't be a function call with an implied opening
            if(*pp==')')return 0; // closing bracket without open so much be implied in a function call e.g. PEEK(
        }
        return emptyarray;
}


// run a program
// this will continuously execute a program until the end (marked by TWO zero chars)
// the argument p must point to the first line to be executed
void MIPS16 __not_in_flash_func(ExecuteProgram)(unsigned char *p) {
    int i, SaveLocalIndex = 0;
    jmp_buf SaveErrNext;
    memcpy(SaveErrNext, ErrNext, sizeof(jmp_buf));                  // we call ExecuteProgram() recursively so we need to store/restore old jump buffer between calls
    skipspace(p);                                                   // just in case, skip any whitespace

    while(1) {
        if(*p == 0) p++;                                            // step over the zero byte marking the beginning of a new element
        if(*p == T_NEWLINE) {
            CurrentLinePtr = p;                                     // and pointer to the line for error reporting
            TraceBuff[TraceBuffIndex] = p;                          // used by TRACE LIST
            if(++TraceBuffIndex >= TRACE_BUFF_SIZE) TraceBuffIndex = 0;
            if(TraceOn && p > ProgMemory && p < ProgMemory + MAX_PROG_SIZE) {
                inpbuf[0] = '[';
                IntToStr((char *)inpbuf + 1, CountLines(p), 10);
                strcat((char *)inpbuf, "]");
                MMPrintString((char *)inpbuf);
                uSec(1000);
            }
            p++;                                                    // and step over the token
        }
        if(*p == T_LINENBR) p += 3;                                 // and step over the number
        skipspace(p);                                               // and skip any trailing white space
        if(p[0] == T_LABEL) {                                       // got a label
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }

        if(*p) {                                                    // if p is pointing to a command
            if(*p=='\'')nextstmt = cmdline = p + 1;
            else nextstmt = cmdline = p + sizeof(CommandToken);
            skipspace(cmdline);
            skipelement(nextstmt);
            if(*p && *p != '\'') {                                  // ignore a comment line
                SaveLocalIndex = g_LocalIndex;                        // save this if we need to cleanup after an error
                if(setjmp(ErrNext) == 0) {                          // return to the else leg of this if error and OPTION ERROR SKIP/IGNORE is in effect
                    if(p[0]>= C_BASETOKEN && p[1]>=C_BASETOKEN){
                        cmdtoken=commandtbl_decode(p);
                        targ = T_CMD;
                        commandtbl[cmdtoken].fptr(); // execute the command
                    } else {
                        if(!isnamestart(*p) && *p=='~') error("Unknown command");
                        else if(!isnamestart(*p)) error("Invalid character: @", (int)(*p));
                        i = FindSubFun(p, false);                   // it could be a defined command
                        if(i >= 0) {                                // >= 0 means it is a user defined command
                            DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
                        }
                        else
                            error("Unknown command");
                    }
                } else {
                    g_LocalIndex = SaveLocalIndex;                    // restore so that we can clean up any memory leaks
                    ClearTempMemory();
                }
                if(OptionErrorSkip > 0) OptionErrorSkip--;        // if OPTION ERROR SKIP decrement the count - we do not error if it is greater than zero
                if(g_TempMemoryIsChanged) ClearTempMemory();          // at the end of each command we need to clear any temporary string vars
#ifndef PICOMITEWEB
                if(core1stack[0]!=0x12345678)error("CPU2 Stack overflow");
#endif
                if(!OptionNoCheck){
                    CheckAbort();
                    check_interrupt();                                  // check for an MMBasic interrupt or touch event and handle it
                }
            }
            p = nextstmt;
        }
        if((p[0] == 0 && p[1] == 0) || (p[0] == 0xff && p[1] == 0xff)) break;      // the end of the program is marked by TWO zero chars, empty flash by two 0xff
    }
    memcpy(ErrNext, SaveErrNext, sizeof(jmp_buf));                  // restore old jump buffer
}


/********************************************************************************************************************************************
 Code associated with processing user defined subroutines and functions
********************************************************************************************************************************************/

// Scan through the program loaded in flash and build a table pointing to the definition of all user defined subroutines and functions.
// This pre processing speeds up the program when using defined subroutines and functions
// this routine also looks for embedded fonts and adds them to the font table
void   MIPS16 PrepareProgram(int ErrAbort) {
    int i, j, NbrFuncts;
#ifdef rp2350
    int u, namelen;
    uint32_t hash=FNV_offset_basis;
    char printvar[MAXVARLEN+1];
#endif
    unsigned char *p1, *p2;
    for(i = FONT_BUILTIN_NBR; i < FONT_TABLE_SIZE-1; i++)
        FontTable[i] = NULL;                                        // clear the font table

    
    NbrFuncts = 0;
    CFunctionFlash = CFunctionLibrary = NULL;
    if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
         NbrFuncts = PrepareProgramExt(LibMemory , 0, &CFunctionLibrary, ErrAbort);
    PrepareProgramExt(ProgMemory, NbrFuncts,&CFunctionFlash, ErrAbort);
    
    // check the sub/fun table for duplicates
#ifdef rp2350
    memset(funtbl,0,sizeof(struct s_funtbl)*MAXSUBFUN);
    for(i = 0; i < MAXSUBFUN && subfun[i] != NULL; i++) {
    	// First we will hash the function name and add it to the function table
    	// This allows for a fast check of a variable name being the same as a function name
    	// It also allows a hash look up for function name matching
    	p1 = subfun[i];
        p1+=sizeof(CommandToken);
        skipspace(p1);
        p2 = (unsigned char *)printvar; namelen = 0;
        hash=FNV_offset_basis;
    	do {
            u=mytoupper(*p1);
    		hash ^= u;
    		hash*=FNV_prime;
    		*p2++ = u;
            p1++;
    		if(++namelen > MAXVARLEN){
    			if(ErrAbort) error("Function name too long");
    		}

    	} while(isnamechar(*p1));
    	if(namelen!=MAXVARLEN)*p2=0;
    	hash %= MAXSUBHASH; //scale to size of table
		while(funtbl[hash].name[0]!=0){
			hash++;
			if(hash==MAXSUBFUN)hash=0;
		}
		funtbl[hash].index=i;
		memcpy(funtbl[hash].name,printvar,(namelen == MAXVARLEN ? namelen :namelen+1));
    }
        if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE){
            hashlabels(LibMemory,ErrAbort);
           // if(!ErrAbort) return;
        }   
          hashlabels(ProgMemory,ErrAbort);
          //if(!ErrAbort) return;

#endif
    if(!ErrAbort) return;

    for(i = 0; i < MAXSUBFUN && subfun[i] != NULL; i++) {
        for(j = i + 1; j < MAXSUBFUN && subfun[j] != NULL; j++) {
            CurrentLinePtr = p1 = subfun[i];
            p1+=sizeof(CommandToken);
            skipspace(p1);
            p2 = subfun[j];
            p2+=sizeof(CommandToken);
            skipspace(p2);
            while(1) {
                if(!isnamechar(*p1) && !isnamechar(*p2)) {
                    if(ErrAbort) error("Duplicate name");
                    return;
                }
                if(mytoupper(*p1) != mytoupper(*p2)) break;
                p1++; p2++;
            }
        }
    }
//    for(i=0;i<MAXSUBFUN;i++){
//    	if(funtbl[i].name[0]!=0){
//    		MMPrintString(funtbl[i].name);PIntHC(funtbl[i].index);PIntComma(i);PRet();
//    	}
//    }
}


// This scans one area (main program or the library area) for user defined subroutines and functions.
// It is only used by PrepareProgram() above.
int   MIPS16 PrepareProgramExt(unsigned char *p, int i, unsigned char **CFunPtr, int ErrAbort) {
    unsigned int *cfp;
    while(*p != 0xff) {
        p = GetNextCommand(p, &CurrentLinePtr, NULL);
        if(*p == 0) break;                                          // end of the program or module
        CommandToken tkn=commandtbl_decode(p);
        if(tkn == cmdSUB || tkn == cmdFUN /*|| tkn == cmdCFUN*/ || tkn == cmdCSUB) {         // found a SUB, FUN, CFUNCTION or CSUB token
            if(i >= MAXSUBFUN) {
                FlashWriteInit(PROGRAM_FLASH);
                flash_range_erase(realflashpointer, MAX_PROG_SIZE);
                int j=MAX_PROG_SIZE/4;
                int *pp=(int *)(flash_progmemory);
                    while(j--)if(*pp++ != 0xFFFFFFFF){
                        enable_interrupts_pico();
                        error("Flash erase problem");
                    }
                enable_interrupts_pico();
                MMPrintString("Error: Too many subroutines and functions - erasing program\r\n");
                uSec(100000);
                ClearProgram();
                cmdline=NULL;
                do_end(false);
                longjmp(mark, 1);												// jump back to the input prompt
            }
            subfun[i++] = p++;                                      // save the address and step over the token
            p++; //step past rest of command token
            skipspace(p);
            if(!isnamestart(*p)) {
                if(ErrAbort) error("Invalid identifier");
                i--;
                continue;
            }
        }
        while(*p) p++;                                              // look for the zero marking the start of the next element
    }
    while(*p == 0) p++;                                             // the end of the program can have multiple zeros
    p++;                                                            // step over the terminating 0xff
    *CFunPtr = (unsigned char *)(((unsigned int)p + 0b11) & ~0b11); // CFunction flash (if it exists) starts on the next word address after the program in flash
    if(i < MAXSUBFUN) subfun[i] = NULL;
    CurrentLinePtr = NULL;
    // now, step through the CFunction area looking for fonts to add to the font table
    //Bit 7 on the last address byte is used to identify a font.
    cfp = *(unsigned int **)CFunPtr;
    while(*cfp != 0xffffffff) {
        if(*cfp & 0x80000000)
            FontTable[*cfp & (FONT_TABLE_SIZE-1)] = (unsigned char *)(cfp + 2);
        cfp++;
        cfp += (*cfp + 4) / sizeof(unsigned int);
    }
    return i;
}

// searches the subfun[] table to locate a defined sub or fun
// returns with the index of the sub/function in the table or -1 if not found
// if type = 0 then look for a sub otherwise a function
#ifdef rp2350
int __not_in_flash_func(FindSubFun)(unsigned char *p, int type) {
    unsigned char *s;
    unsigned char name[MAXVARLEN + 1];
    int j, u, namelen;
    unsigned int hash=FNV_offset_basis;
	unsigned char *tp, *ip;

// copy the variable name into name
    s = name; namelen = 0;
	do {
        u=mytoupper(*p);
		hash ^= u;
//        PIntComma(u);
		hash*=FNV_prime;
		*s++ = u;
        p++;
		if(++namelen > MAXVARLEN) error("Variable name too long");
	} while(isnamechar(*p));
//    PRet();
	*s=0;
	hash %= MAXSUBHASH; //scale 0-512
//	MMPrintString("Searching for function: ");MMPrintString((char *)name);PIntComma(hash);PRet();
	while(funtbl[hash].name[0]!=0){
		ip=name;
		tp=(unsigned char *)funtbl[hash].name;
//		MMPrintString("Testing : ");MMPrintString((char *)tp);PRet();
		if(*ip++ == *tp++) {                 // preliminary quick check
			j = namelen-1;
			while(j > 0 && *ip == *tp) {                              // compare each letter
				j--; ip++; tp++;
			}
			if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN) && funtbl[hash].index<MAXSUBFUN) {       // found a matching name
//				MMPrintString("Found : ");MMPrintString((char *)name);MMPrintString(", hash key : ");PInt(hash);PRet();
					return funtbl[hash].index;
					break;
			}
		}
		hash++;
		if(hash==MAXSUBFUN)hash=0;
	}
    return -1;
}
#else
int __not_in_flash_func(FindSubFun)(unsigned char *p, int type) {
    unsigned char *p1, *p2;
    int i;

    for(i = 0;  i < MAXSUBFUN && subfun[i] != NULL; i++) {
        p2 = subfun[i];                                             // point to the command token
        CommandToken tkn=commandtbl_decode(p2);
        if(type == 0) {                                             // if it is a sub and we want a fun or vice versa skip this one
            if(!(tkn == cmdSUB || tkn == cmdCSUB)) continue;
        } else {
            if(!(tkn == cmdFUN /*|| tkn == cmdCFUN*/)) continue;
        }
        p2+=sizeof(CommandToken); skipspace(p2);                                        // point to the identifier
        if(toupper(*p) != toupper(*p2)) continue;                   // quick first test
        p1 = p + 1;  p2++;
        while(isnamechar(*p1) && toupper(*p1) == toupper(*p2)) { p1++; p2++; };
        if((*p1 == '$' && *p2 == '$') || (*p1 == '%' && *p2 == '%') || (*p1 == '!' && *p2 == '!') || (!isnamechar(*p1) && !isnamechar(*p2))) return i;          // found it !
    }
    return -1;
}
#endif

// This function is responsible for executing a defined subroutine or function.
// As these two are similar they are processed in the one lump of code.
//
// The arguments when called are:
//   isfun    = true if we are executing a function
//   cmd      = pointer to the command name used by the caller (in program memory)
//   index    = index into subfun[i] which points to the definition of the sub or funct
//   fa, i64a, sa and typ are pointers to where the return value is to be stored (used by functions only)
#if defined(PICOMITEWEB) || defined(PICOMITEVGA)
#ifdef rp2350
void MIPS16 __not_in_flash_func(DefinedSubFun)(int isfun, unsigned char *cmd, int index, MMFLOAT *fa, long long int  *i64a, unsigned char **sa, int *typ) {
#else
void MIPS16 DefinedSubFun(int isfun, unsigned char *cmd, int index, MMFLOAT *fa, long long int  *i64a, unsigned char **sa, int *typ) {
#endif
#else
void MIPS16 __not_in_flash_func(DefinedSubFun)(int isfun, unsigned char *cmd, int index, MMFLOAT *fa, long long int  *i64a, unsigned char **sa, int *typ) {
#endif
	unsigned char *p, *s, *tp, *ttp, tcmdtoken;
	unsigned char *CallersLinePtr, *SubLinePtr = NULL;
    unsigned char *argbuf1; unsigned char **argv1; int argc1;
    unsigned char *argbuf2; unsigned char **argv2; int argc2;
    unsigned char fun_name[MAXVARLEN + 1];
    unsigned char *argbyref;
	int i;
    int ArgType, FunType;
    int *argtype;
    union u_argval {
        MMFLOAT f;                                                    // the value if it is a float
        long long int  i;                                            // the value if it is an integer
        MMFLOAT *fa;                                                  // pointer to the allocated memory if it is an array of floats
        long long int  *ia;                                          // pointer to the allocated memory if it is an array of integers
        unsigned char *s;                                                    // pointer to the allocated memory if it is a string
    } *argval;
    int *argVarIndex;

    CallersLinePtr = CurrentLinePtr;
    SubLinePtr = subfun[index];                                     // used for error reporting
    p =  SubLinePtr + sizeof(CommandToken);                                            // point to the sub or function definition
    skipspace(p);
    ttp = p;
    
    // copy the sub/fun name from the definition into temp storage and terminate
    // p is left pointing to the end of the name (ie, start of the argument list in the definition)
    CurrentLinePtr = SubLinePtr;                                    // report errors at the definition
    tp = fun_name;
    *tp++ = *p++; while(isnamechar(*p)) *tp++ = *p++;
    if(*p == '$' || *p == '%' || *p == '!') {
        if(!isfun) {
            error("Type specification is invalid: @", (int)(*p));
        }
        *tp++ = *p++;
    }
    *tp = 0;
    
    if(isfun && *p != '(' /*&& (*SubLinePtr != cmdCFUN)*/) error("Function definition");

    // find the end of the caller's identifier, tp is left pointing to the start of the caller's argument list
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    tp = cmd + 1;
    while(isnamechar(*tp)) tp++;
    if(*tp == '$' || *tp == '%' || *tp == '!') {
        if(!isfun) error("Type specification");
        tp++;
    }
    if(mytoupper(*(p-1)) != mytoupper(*(tp-1))) error("Inconsistent type suffix");

    // if this is a function we check to find if the function's type has been specified with AS <type> and save it
    CurrentLinePtr = SubLinePtr;                                    // report errors at the definition
    FunType = T_NOTYPE;
    if(isfun) {
        ttp = skipvar(ttp, false);                                  // point to after the function name and bracketed arguments
        skipspace(ttp);
        if(*ttp == tokenAS) {                                       // are we using Microsoft syntax (eg, AS INTEGER)?
            ttp++;                                                  // step over the AS token
            ttp = CheckIfTypeSpecified(ttp, &FunType, true);        // get the type
            if(!(FunType & T_IMPLIED)) error("Variable type");
        }
        FunType |= (V_FIND | V_DIM_VAR | V_LOCAL | V_EMPTY_OK);
    }


    // from now on
    // tp  = the caller's argument list
    // p   = the argument list for the definition
    skipspace(tp); skipspace(p);

     // similar if this is a CSUB
    CommandToken tkn=commandtbl_decode(SubLinePtr);
    if(tkn == cmdCSUB) {
        CallCFunction(SubLinePtr, tp, p, CallersLinePtr);           // run the CSUB
        g_TempMemoryIsChanged = true;                                 // signal that temporary memory should be checked
        return;
    }
   // from now on we have a user defined sub or function (not a C routine)
    
    if(gosubindex >= MAXGOSUB) error("Too many nested SUB/FUN");
    errorstack[gosubindex] = CallersLinePtr;
	gosubstack[gosubindex++] = isfun ? NULL : nextstmt;             // NULL signifies that this is returned to by ending ExecuteProgram()
    #define buffneeded MAX_ARG_COUNT*(sizeof(union u_argval)+ 2*sizeof(int)+3*sizeof(unsigned char *)+sizeof(unsigned char))+ 2*STRINGSIZE 
    // allocate memory for processing the arguments
    argval=GetMemory(buffneeded);
    argtype=(void *)argval+MAX_ARG_COUNT * sizeof(union u_argval);
    argVarIndex = (void *)argtype+MAX_ARG_COUNT * sizeof(int);
    argbuf1 = (void *)argVarIndex+MAX_ARG_COUNT * sizeof(int);
    argv1 = (void *)argbuf1+STRINGSIZE;
    argbuf2 = (void *)argv1+MAX_ARG_COUNT * sizeof(unsigned char *);
    argv2 = (void *)argbuf2+STRINGSIZE;
    argbyref=(void *)argv2+MAX_ARG_COUNT * sizeof(unsigned char *);
/*    argval = GetMemory(MAX_ARG_COUNT * sizeof(union u_argval));
    argtype = GetMemory(MAX_ARG_COUNT * sizeof(int));
    argVarIndex = GetMemory(MAX_ARG_COUNT * sizeof(int));
    argbuf1 = GetMemory(STRINGSIZE); 
    argv1 = GetMemory(MAX_ARG_COUNT * sizeof(unsigned char *));  // these are for the caller
    argbuf2 = GetMemory(STRINGSIZE); 
    argv2 = GetMemory(MAX_ARG_COUNT * sizeof(unsigned char *));  // and these for the definition of the sub or function
    argbyref=GetMemory(MAX_ARG_COUNT * sizeof(unsigned char));  // these are BYREF*/
    // now split up the arguments in the caller
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    argc1 = 0;
    if(*tp) makeargs(&tp, MAX_ARG_COUNT, argbuf1, argv1, &argc1, (*tp == '(') ? (unsigned char *)"(," : (unsigned char *)",");

    // split up the arguments in the definition
    CurrentLinePtr = SubLinePtr;                                    // any errors must be at the definition
    argc2 = 0;
    if(*p) makeargs(&p, MAX_ARG_COUNT, argbuf2, argv2, &argc2, (*p == '(') ? (unsigned char *)"(," : (unsigned char *)",");

    // error checking
    if(argc2 && (argc2 & 1) == 0) error("Argument list");
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    if(argc1 > argc2 || (argc1 && (argc1 & 1) == 0)) error("Argument list");

	// step through the arguments supplied by the caller and get the value supplied
    // these can be:
    //    - missing (ie, caller did not supply that parameter)
    //    - a variable, in which case we need to get a pointer to that variable's data and save its index so later we can get its type
    //    - an expression, in which case we evaluate the expression and get its value and type
    for(i = 0; i < argc2; i += 2) {                                 // count through the arguments in the definition of the sub/fun       
        if(i < argc1 && *argv1[i]) {
            // check if the argument is a valid variable
            if(i < argc1 && isnamestart(*argv1[i]) && *skipvar(argv1[i], false) == 0) {
                // yes, it is a variable (or perhaps a user defined function which looks the same)?
                if(!(FindSubFun(argv1[i], 1) >= 0 && strchr((char *)argv1[i], '(') != NULL)) {
                    // yes, this is a valid variable.  set argvalue to point to the variable's data and argtype to its type
                    argval[i].s = findvar(argv1[i], V_FIND | V_EMPTY_OK);        // get a pointer to the variable's data
                    argtype[i] = g_vartbl[g_VarIndex].type;                          // and the variable's type
                    argVarIndex[i] = g_VarIndex;
                    if(argtype[i] & T_CONST) {
                        argtype[i] = 0;                                          // we don't want to point to a constant
                    } else {
                        argtype[i] |= T_PTR;                                     // flag this as a pointer
                    }
                }
            }

            // check for BYVAL or BYREF in sub/fun definition
            argbyref[i]=0;
            skipspace(argv2[i]);
			if(toupper(*argv2[i]) == 'B' && toupper(*(argv2[i]+1)) == 'Y') {
				if((checkstring(argv2[i] + 2, (unsigned char *)"VAL")) != NULL) {        // if BYVAL
					argtype[i] = 0;	                                    // remove any pointer flag in the caller
					argv2[i] += 5;										// skip to the variable start
				} else {
					if((checkstring(argv2[i] + 2, (unsigned char *)"REF")) != NULL) {    // if BYREF
						if((argtype[i] & T_PTR) == 0) error("Variable required for BYREF");
						argv2[i] += 5;									// skip to the variable start
					}
                    argbyref[i]=1;
                }
				skipspace(argv2[i]);
			}

            // if argument is present and is not a pointer to a variable then evaluate it as an expression
            if(argtype[i] == 0) {
                long long int  ia;
                evaluate(argv1[i], &argval[i].f, &ia, &s, &argtype[i], false);   // get the value and type of the argument
                if(argtype[i] & T_INT)
                    argval[i].i = ia;
                else if(argtype[i] & T_STR) {
                    argval[i].s = GetMemory(STRINGSIZE);
                    Mstrcpy(argval[i].s, s);
                }
            }
        }
    }

    // now we step through the parameters in the definition of the sub/fun
    // for each one we create the local variable and compare its type to that supplied in the callers list
    CurrentLinePtr = SubLinePtr;                                    // any errors must be at the definition
    g_LocalIndex++;
    for(i = 0; i < argc2; i += 2) {                                 // count through the arguments in the definition of the sub/fun
        ArgType = T_NOTYPE;
        tp = skipvar(argv2[i], false);                              // point to after the variable
        skipspace(tp);
        if(*tp == tokenAS) {                                        // are we using Microsoft syntax (eg, AS INTEGER)?
            *tp++ = 0;                                              // terminate the string and step over the AS token
            tp = CheckIfTypeSpecified(tp, &ArgType, true);          // and get the type
            if(!(ArgType & T_IMPLIED)) error("Variable type");
        }
        ArgType |= (V_FIND | V_DIM_VAR | V_LOCAL | V_EMPTY_OK);
        tp = findvar(argv2[i], ArgType);                            // declare the local variable
        if(g_vartbl[g_VarIndex].dims[0] > 0) error("Argument list");    // if it is an array it must be an empty array
        
        CurrentLinePtr = CallersLinePtr;                            // report errors at the caller

        // if the definition called for an array, special processing and checking will be required
        if(g_vartbl[g_VarIndex].dims[0] == -1) {
            int j;
            if(g_vartbl[argVarIndex[i]].dims[0] == 0) error("Expected an array");
            if(TypeMask(g_vartbl[g_VarIndex].type) != TypeMask(argtype[i])) error("Incompatible type: $", argv1[i]);
            g_vartbl[g_VarIndex].val.s = NULL;
            for(j = 0; j < MAXDIM; j++)                             // copy the dimensions of the supplied variable into our local variable
                g_vartbl[g_VarIndex].dims[j] = g_vartbl[argVarIndex[i]].dims[j];
        }

        // if this is a pointer check and the type is NOT the same as that requested in the sub/fun definition
        if((argtype[i] & T_PTR) && TypeMask(g_vartbl[g_VarIndex].type) != TypeMask(argtype[i])) {
                if(argbyref[i]){ error("BYREF requires same types: $", argv1[i]);}
                if((TypeMask(g_vartbl[g_VarIndex].type) & T_STR) || (TypeMask(argtype[i]) & T_STR))
                error("Incompatible type: $", argv1[i]);
            // make this into an ordinary argument
            if(g_vartbl[argVarIndex[i]].type & T_PTR) {
                argval[i].i = *g_vartbl[argVarIndex[i]].val.ia;       // get the value if the supplied argument is a pointer
            } else {
                argval[i].i = *(long long int *)argval[i].s;        // get the value if the supplied argument is an ordinary variable
            }
            argtype[i] &= ~T_PTR;                                   // and remove the pointer flag
        }
        
        // if this is a pointer (note: at this point the caller type and the required type must be the same)
        if(argtype[i] & T_PTR) {
            // the argument supplied was a variable so we must setup the local variable as a pointer
            if((g_vartbl[g_VarIndex].type & T_STR) && g_vartbl[g_VarIndex].val.s != NULL) {
                FreeMemorySafe((void **)&g_vartbl[g_VarIndex].val.s);                            // free up the local variable's memory if it is a pointer to a string
                }
            g_vartbl[g_VarIndex].val.s = argval[i].s;                              // point to the data of the variable supplied as an argument
            g_vartbl[g_VarIndex].type |= T_PTR;                                    // set the type to a pointer
            g_vartbl[g_VarIndex].size = g_vartbl[argVarIndex[i]].size;               // just in case it is a string copy the size
        // this is not a pointer
        } else if(argtype[i] != 0) {                                           // in getting the memory argtype[] is initialised to zero
            // the parameter was an expression or a just straight variables with different types (therefore not a pointer))
            if((g_vartbl[g_VarIndex].type & T_STR) && (argtype[i] & T_STR)) {      // both are a string
                Mstrcpy(g_vartbl[g_VarIndex].val.s, argval[i].s);
                FreeMemorySafe((void **)&argval[i].s);
            } else if((g_vartbl[g_VarIndex].type & T_NBR) && (argtype[i] & T_NBR)) // both are a float
                g_vartbl[g_VarIndex].val.f = argval[i].f;
            else if((g_vartbl[g_VarIndex].type & T_NBR) && (argtype[i] & T_INT))   // need a float but supplied an integer
                g_vartbl[g_VarIndex].val.f = argval[i].i;
            else if((g_vartbl[g_VarIndex].type & T_INT) && (argtype[i] & T_INT))   // both are integers
                g_vartbl[g_VarIndex].val.i = argval[i].i;
            else if((g_vartbl[g_VarIndex].type & T_INT) && (argtype[i] & T_NBR))   // need an integer but was supplied with a float
                g_vartbl[g_VarIndex].val.i = FloatToInt64(argval[i].f);
            else
                error("Incompatible type: $", argv1[i]);
        }
    }

    // temp memory used in setting up the arguments can be deleted now
/*    FreeMemory((unsigned char *)argval);
    FreeMemory((unsigned char *)argtype); FreeMemory((unsigned char *)argVarIndex);
    FreeMemory(argbuf1); FreeMemory((unsigned char *)argv1);
    FreeMemory(argbuf2); FreeMemory((unsigned char *)argv2);
    FreeMemory((unsigned char *)argbyref);*/
    FreeMemory((void*)argval);
    strcpy((char *)CurrentSubFunName, (char *)fun_name);
    // if it is a defined command we simply point to the first statement in our command and allow ExecuteProgram() to carry on as before
    // exit from the sub is via cmd_return which will decrement g_LocalIndex
    if(!isfun) {
        skipelement(p);
        nextstmt = p;                                               // point to the body of the subroutine
        return;
    }

    // if it is a defined function we have a lot more work to do.  We must:
    //   - Create a local variable for the function's name
    //   - Save the globals being used by the current command that caused the function to be called
    //   - Invoke another instance of ExecuteProgram() to execute the body of the function
    //   - When that returns we need to restore the global variables
    //   - Get the variable's value and save that in the return value globals (fret or sret)
    //   - Return to the expression parser
    tp = findvar(fun_name, FunType | V_FUNCT);                      // declare the local variable
    FunType = g_vartbl[g_VarIndex].type;
    if(FunType & T_STR) {
        FreeMemorySafe((void **)&g_vartbl[g_VarIndex].val.s);                         // free the memory if it is a string
        g_vartbl[g_VarIndex].type |= T_PTR;
        g_LocalIndex--;                                               // allocate the memory at the previous level
        g_vartbl[g_VarIndex].val.s = tp = GetTempMemory(STRINGSIZE);    // and use our own memory
        g_LocalIndex++;
    }
    skipelement(p);                                                 // point to the body of the function

    ttp = nextstmt;                                                 // save the globals used by commands
    tcmdtoken = cmdtoken;
    s = cmdline;

    ExecuteProgram(p);                                              // execute the function's code
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller

    cmdline = s;                                                    // restore the globals
    cmdtoken = tcmdtoken;
    nextstmt = ttp;

    // return the value of the function's variable to the caller
    if(FunType & T_NBR)
        *fa = *(MMFLOAT *)tp;
    else if(FunType & T_INT)
        *i64a = *(long long int  *)tp;
    else
        *sa = tp;                                                   // for a string we just need to return the local memory
    *typ = FunType;                                                 // save the function type for the caller
	ClearVars(g_LocalIndex--, true);                                        // delete any local variables
    g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
	gosubindex--;
}

char  MIPS16 *strcasechr(const char *p, int ch)
{
	char c;

	c = mytoupper(ch);
	for (;; ++p) {
		if (mytoupper(*p) == c)
			return ((char *)p);
		if (*p == '\0')
			return (NULL);
	}
	/* NOTREACHED */
}

char  MIPS16 *fstrstr (const char *s1, const char *s2)
{
  const char *p = s1;
  const size_t len = strlen (s2);

  for (; (p = strcasechr (p, *s2)) != 0; p++)
    {
      if (strncasecmp (p, s2, len) == 0)
	return (char *)p;
    }
  return (0);
}

void  MIPS16 str_replace(char *target, const char *needle, const char *replacement, uint8_t ignoresurround)
{
    char buffer[288] = { 0 };
    char *insert_point = &buffer[0];
    const char *tmp = target;
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);

    while (1) {
        const char *p = fstrstr(tmp, needle);

        // walked past last occurrence of needle; copy remaining part
        if (p == NULL) {
            strcpy(insert_point, tmp);
            break;
        }
        char *q;
        if(p==target){
            ignoresurround|=1;
            q=(char *)p;
        } else q=(char *)p-1;
        if( (isnamechar(*q) && !(ignoresurround & 1)) || (isnameend(p[strlen(needle)]) && !(ignoresurround & 2))){
            // copy part before needle
            memcpy(insert_point, tmp, p - tmp);
            insert_point += p - tmp;

            // copy replacement string
            memcpy(insert_point, needle, needle_len);
            insert_point += needle_len;

            // adjust pointers, move on
            tmp = p + needle_len;
        } else {
            // copy part before needle
            memcpy(insert_point, tmp, p - tmp);
            insert_point += p - tmp;

            // copy replacement string
            memcpy(insert_point, replacement, repl_len);
            insert_point += repl_len;

            // adjust pointers, move on
            tmp = p + needle_len;
        }
    }

    // write altered string back to target
    strcpy(target, buffer);
}

void  MIPS16 STR_REPLACE(char *target, const char *needle, const char *replacement, uint8_t ignoresurround){
	char *ip=target;
	int toggle=0;
    char comment[STRINGSIZE]={0};
    skipspace(ip);
    if(!(toupper(*ip)=='R' && toupper(ip[1])=='E' && toupper(ip[2])=='M' )){
        while(*ip){
            if(*ip==34){
                if(toggle==0)toggle=1;
                else toggle=0;
            }
            if(toggle && *ip==' '){
                *ip=0xFF;
            }
            if(toggle && *ip=='.'){
                *ip=0xFE;
            }
            if(toggle && *ip=='='){
                *ip=0xFD;
            }
            if(toggle && *ip=='\\'){
                *ip=0xFC;
            }
            if(toggle==0 && *ip=='\''){
                strcpy(comment,ip);
                *ip=0;
                break;
            }
            ip++;
        }
        str_replace(target, needle, replacement, ignoresurround);
        ip=target;
        if(comment[0]=='\''){
            strcat(target,comment);
        }
        while(*ip){
            if(*ip==0xFF)*ip=' ';
            if(*ip==0xFE)*ip='.';
            if(*ip==0xFD)*ip='=';
            if(*ip==0xFC)*ip='\\';
            ip++;
        }
    }
}

/********************************************************************************************************************************************
 take an input line and turn it into a line with tokens suitable saving into memory
********************************************************************************************************************************************/

//take an input string in inpbuf[] and copy it to tknbuf[] and:
// - convert the line number to a binary number
// - convert a label to the token format
// - convert keywords to tokens
// - convert the colon to a zero char
//the result in tknbuf[] is terminated with MMFLOAT zero chars
// if the arg console is true then do not add a line number

void  MIPS16 tokenise(int console) {
    unsigned char *p, *op, *tp;
    int i=0;
    int firstnonwhite;
    int labelvalid;

    // first, make sure that only printable characters are in the line
    p = inpbuf;
    while(*p) {
        *p = *p & 0x7f;
        if(*p < ' ' || *p == 0x7f)  *p = ' ';
        p++;
    }
    tp = inpbuf;
    skipspace(tp);
    if(toupper(tp[0])=='R' && toupper(tp[1])=='E' && toupper(tp[2])=='M' && tp[3]==' ')i=1;
    if(multi==false && i==false){
        int i=0;
        while(i<MMEND){
            char buff[]="~( )";
            buff[2]=i+'A';
            STR_REPLACE((char *)inpbuf,overlaid_functions[i],buff, false);
            i++;
        }
        STR_REPLACE((char *)inpbuf,"MM.INFO$","MM.INFO",0);
        STR_REPLACE((char *)inpbuf,"=>",">=", 3);
        STR_REPLACE((char *)inpbuf,"=<","<=", 3);
        STR_REPLACE((char *)inpbuf,"SPRITE MEMORY","BLIT MEMORY",0);
        STR_REPLACE((char *)inpbuf,"PEEK(BYTE","PEEK(INT8",0);
    }
    // setup the input and output buffers
    p = inpbuf;
    op = tknbuf;
    if(!console) *op++ = T_NEWLINE;

    // get the line number if it exists
    tp = p;
    skipspace(tp);
    for(i = 0; i < 8; i++) if(!isxdigit(tp[i])) break;              // test if this is eight hex digits
    if(IsDigitinline(*tp) && i < 8) {                                     // if it a digit and not an 8 digit hex number (ie, it is CFUNCTION data) then try for a line number
        i = strtol((char *)tp, (char **)&tp, 10);
        if(!console && i > 0 && i <= MAXLINENBR) {
            *op++ = T_LINENBR;
            *op++ = (i>>8);
            *op++ = (i & 0xff);
        }
        p = tp;
    }

    // process the rest of the line
    firstnonwhite = true;
    labelvalid = true;
    tp=p;
    skipspace(tp);
    if(*tp=='.'){
        if(!strncasecmp((char *)tp,".SIDE SET ",10) ||
            !strncasecmp((char *)tp,".END PROGRAM",12) ||
            !strncasecmp((char *)tp,".WRAP",4) ||
            !strncasecmp((char *)tp,".LINE ",5) ||
            !strncasecmp((char *)tp,".PROGRAM ",9) ||
            !strncasecmp((char *)tp,".LABEL ",6)
        ) *tp='_';
    }

    while(*p) {
	    if(*p=='*' && p[1]=='/'){
            multi=false;
        }
        // just copy a space
        if(*p == ' ') {
            *op++ = *p++;
            continue;
        }

        // first look for quoted text and copy it across
        // this will also accept a string without the closing quote and it will add the quote in
        if(*p == '"') {
            do {
                *op++ = *p++;
            } while(*p != '"' && *p);
            *op++ = '"';
            if(*p == '"') p++;
            continue;
        }

        // copy anything after a comment (')
        if(*p == '\'' || multi==true) {
            do {
                *op++=*p++;
            } while(*p);
            continue;
        }

        // check for multiline separator (colon) and replace with a zero char
        if(*p == ':') {
            *op++ = 0;
            p++;
            while(*p == ':') {                                      // insert a space between consecutive colons
                *op++ = ' ';
                *op++ = 0;
                p++;
            }
            firstnonwhite = true;
            continue;
        }

        // not whitespace or string or comment  - try a number
        if(IsDigitinline(*p) || *p == '.') {                              // valid chars at the start of a number
            while(IsDigitinline(*p) || *p == '.' || *p == 'E' || *p == 'e')
                if (*p == 'E' || *p == 'e') {   // check for '+' or '-' as part of the exponent
                    *op++ = *p++;                                   // copy the number
                    if (*p == '+' || *p == '-') {                   // BUGFIX by Gerard Sexton
                        *op++ = *p++;                               // copy the '+' or '-'
                    }
                } else {
                    *op++ = *p++;                                   // copy the number
                }
            firstnonwhite = false;
            continue;
        }

        // not whitespace or string or comment or number - see if we can find a label or a token identifier
         if(firstnonwhite) {                                         // first entry on the line must be a command
            // these variables are only used in the search for a command code
            unsigned char *tp2, *match_p = NULL;
            int match_i = -1, match_l = 0;
            // first test if it is a print shortcut char (?) - this needs special treatment
            if(*p == '?') {
                match_i = GetCommandValue((unsigned char *)"Print");
                if(*++p == ' ') p++;                                // eat a trailing space
                match_p = p;
            } else if((tp2 = checkstring(p, (unsigned char *)"BITBANG")) != NULL) {
                    match_i = GetCommandValue((unsigned char *)"Device");
                    match_p = p = tp2;
            } else {
                // now try for a command in the command table
                // this works by scanning the entire table looking for the match with the longest command name
                // this is needed because we need to differentiate between END and END SUB for example.
                // without looking for the longest match we might think that we have a match when we found just END.
            for(i = 0 ; i < CommandTableSize - 1; i++) {
                    tp2 = p;
                    tp = commandtbl[i].name;
                    while(mytoupper(*tp2) == mytoupper(*tp) && *tp != 0) {
                        if(*tp == ' ')
                            skipspace(tp2);                         // eat up any extra spaces between keywords
                        else
                            tp2++;
                        tp++;
                        if(*tp == '(') skipspace(tp2);              // eat up space between a keyword and bracket
                    }
                    // we have a match
                    if(*tp == 0 && (!isnamechar(*tp2) || (commandtbl[i].type & T_FUN))) {
                        if(*(tp - 1) != '(' && isnamechar(*tp2)) continue;   // skip if not the function
                        // save the details if it is the longest command found so far
                        if(strlen((char *)commandtbl[i].name) > match_l) {
                            match_p = tp2;
                            match_l = strlen((char *)commandtbl[i].name);
                            match_i = i;
                        }
                    }
                }
            }

            if(match_i > -1) {
                // we have found a command
//                *op++ = match_i + C_BASETOKEN;                      // insert the token found
                *op++ = (match_i & 0x7f ) + C_BASETOKEN;
                *op++ = (match_i >> 7) + C_BASETOKEN; //tokens can be 14-bit
                p = match_p;                                        // step over the command in the source
                if(isalpha(*(p-1)) && *p == ' ') p++;               // if the command is followed by a space skip over it
                if(match_i == GetCommandValue((unsigned char *)"Rem")) // check if it is a REM command
                    while(*p) *op++ = *p++;                         // and in that case just copy everything
                firstnonwhite = false;
                labelvalid = false;                                 // we do not want any labels after this
                if(match_i == GetCommandValue((unsigned char *)"/*")){
                    multi= true;
                }
                if(match_i == GetCommandValue((unsigned char *)"*/"))multi= false;
                if(match_i == GetCommandValue((unsigned char *)"OPTION") || match_i == GetCommandValue((unsigned char *)"CONFIGURE" )){
                    STR_REPLACE((char *)inpbuf,"GAME*MITE","GAMEMITE", false);
                    STR_REPLACE((char *)inpbuf,"PICO-RESTOUCH-LCD-3.5","PICORESTOUCHLCD3.5",false);
                    STR_REPLACE((char *)inpbuf,"PICO-RESTOUCH-LCD-2.8","PICORESTOUCHLCD2.8",false);
                    STR_REPLACE((char *)inpbuf,"RP2040-LCD-1.28","RP2040LCD1.28",false);
                    STR_REPLACE((char *)inpbuf,"RP2040-LCD-0.96","RP2040LCD0.96",false);
                    STR_REPLACE((char *)inpbuf,"RP2040-GEEK","RP2040GEEK",false);
                    STR_REPLACE((char *)inpbuf,"PICOGAME 4-PWM","PICOGAME 4PWM",false);
                    STR_REPLACE((char *)inpbuf,"OLIMEX USB","OLIMEXUSB",false);
                }
                continue;
            }

            // next test if it is a label
            if(labelvalid && isnamestart(*p)) {
                for(i = 0, tp = p + 1; i < MAXVARLEN - 1; i++, tp++)
                    if(!isnamechar(*tp)) break;                     // search for the first invalid char
                if(*tp == ':') {                                    // Yes !!  It is a label
                    labelvalid = false;                             // we do not want any more labels
                    *op++ = T_LABEL;                                // insert the token
                    *op++ = tp - p;                                 // insert the length of the label
                    for(i = tp - p; i > 0; i--) *op++ = *p++;       // copy the label
                    p++;                                            // step over the terminating colon
                    continue;
                }
            }
        } else {
            // check to see if it is a function or keyword
            unsigned char *tp2 = NULL;
            for(i = 0 ; i < TokenTableSize - 1; i++) {
                tp2 = p;
                tp = tokentbl[i].name;
                // check this entry
                while(mytoupper(*tp2) == mytoupper(*tp) && *tp != 0) {
                    tp++; tp2++;
                    if(*tp == '(') skipspace(tp2);
                }
                if(*tp == 0 && (!isnameend(*(tp - 1)) || !isnamechar(*tp2))) break;
            }
            if(i != TokenTableSize - 1) {
                // we have a  match
                i += C_BASETOKEN;
                *op++ = i;                                          // insert the token found
                p = tp2;                                            // and step over it in the source text
                        if(i == tokenTHEN || i == tokenELSE)
                            firstnonwhite = true;                   // a command is valid after a THEN or ELSE
                        else
                            firstnonwhite = false;
                continue;
            }
        }

        // not whitespace or string or comment or token identifier or number
        // try for a variable name which could be a user defined subroutine or an implied let
        if(isnamestart(*p)) {                                       // valid chars at the start of a variable name
            if(firstnonwhite) {                                     // first entry on the line?
                tp = skipvar(p, true);                              // find the char after the variable
                skipspace(tp);
                if(*tp == '=') {  
                    unsigned short tkn = GetCommandValue((unsigned char *)"Let");                                   // is it an implied let?
                    *op++ = (tkn & 0x7f ) + C_BASETOKEN;
                    *op++ = (tkn >> 7) + C_BASETOKEN; //tokens can be 14-bit
                }
            }
            while(isnamechar(*p)) *op++ = *p++;                     // copy the variable name
            firstnonwhite = false;
            labelvalid = false;                                     // we do not want any labels after this
            continue;
        }

        // special case where the character to copy is an opening parenthesis
        // we search back to see if the previous non space char was the end of an identifier and, if it is, we remove any spaces following the identifier
        // this enables the programmer to put spaces after a function name or array identifier without causing a confusing error
        if(*p == '(') {
            tp = op - 1;
            if(*tp == ' ') {
                while(*tp == ' ') tp--;
                if(isnameend(*tp)) op = tp + 1;
            }
        }

        // something else, so just copy the one character
        *op++ = *p++;
       labelvalid = false;                                          // we do not want any labels after this
       firstnonwhite = false;

    }
    // end of loop, trim any trailing blanks (but not part of a line number)
    while(*(op - 1) == ' ' && op > tknbuf + 3) *--op = 0;
    // make sure that it is terminated properly
    *op++ = 0;  *op++ = 0;  *op++ = 0;                              // terminate with  zero chars
}




/********************************************************************************************************************************************
 routines for evaluating expressions
 the main functions are getnumber(), getinteger() and getstring()
********************************************************************************************************************************************/



// A convenient way of evaluating an expression
// it takes two arguments:
//     p = pointer to the expression in memory (leading spaces will be skipped)
//     t = pointer to the type
//         if *t = T_STR or T_NBR or T_INT will throw an error if the result is not the correct type
//         if *t = T_NOTYPE it will not throw an error and will return the type found in *t
// it returns with a void pointer to a float, integer or string depending on the value returned in *t
// this will check that the expression is terminated correctly and throw an error if not
void __not_in_flash_func(*DoExpression)(unsigned char *p, int *t) {
    static MMFLOAT f;
    static long long int  i64;
    static unsigned char *s;

    evaluate(p, &f, &i64, &s, t, false);
    if(*t & T_INT) return &i64;
    if(*t & T_NBR) return &f;
    if(*t & T_STR) return s;

    error("Internal fault 1(sorry)");
    return NULL;                                                    // to keep the compiler happy
}



// evaluate an expression.  p points to the start of the expression in memory
// returns either the float or string in the pointer arguments
// *t points to an integer which holds the type of variable we are looking for
//  if *t = T_STR or T_NBR or T_INT will throw an error if the result is not the correct type
//  if *t = T_NOTYPE it will not throw an error and will return the type found in *t
// this will check that the expression is terminated correctly and throw an error if not.  flags & E_NOERROR will suppress that check
unsigned char MIPS16 __not_in_flash_func(*evaluate)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *ta, int flags) {
    int o;
    int t = *ta;
    unsigned char *s;
    p = getvalue(p, fa, ia, &s, &o, &t);                            // get the left hand side of the expression, the operator is returned in o
    while(o != E_END) p = doexpr(p, fa, ia, &s, &o, &t);            // get the right hand side of the expression and evaluate the operator in o

    // check that the types match and convert them if we can
    if((*ta & (T_NBR |T_INT)) && t & T_STR) error("Expected a number");
    if(*ta & T_STR && (t & (T_NBR | T_INT))) error("Expected a string");
    if(o != E_END) error("Argument count");
    if((*ta & T_NBR) && (t & T_INT)) *fa = *ia;
    if((*ta & T_INT) && (t & T_NBR)) *ia = FloatToInt64(*fa);
    *ta = t;
    *sa = s;

    // check that the expression is terminated correctly
    if(!(flags & E_NOERROR)) {
        skipspace(p);
        if(!(*p == 0 || *p == ',' || *p == ')' || *p == '\''))  error("Expression syntax");
    }
    return p;
}


// evaluate an expression to get a number
MMFLOAT __not_in_flash_func(getnumber)(unsigned char *p) {
    int t = T_NBR;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;
    evaluate(p, &f, &i64, &s, &t, false);
    if(t & T_INT) return (MMFLOAT)i64;
    return f;
}


// evaluate an expression and return a 64 bit integer
long long int  __not_in_flash_func(getinteger)(unsigned char *p) {
    int t = T_INT;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;

    evaluate(p, &f, &i64, &s, &t, false);
    if(t & T_NBR) return FloatToInt64(f);
    return i64;
}



// evaluate an expression and return an integer
// this will throw an error is the integer is outside a specified range
// this will correctly round the number if it is a fraction of an integer
long long int __not_in_flash_func(getint)(unsigned char *p, long long int min, long long int max) {
    long long int  i;
    int t = T_INT;
    MMFLOAT f;
    long long int i64;
    unsigned char *s;
    evaluate(p, &f, &i64, &s, &t, false);
    if(t & T_NBR) i= FloatToInt64(f);
    else i=i64;
    if(i < min || i > max) error("~ is invalid (valid is ~ to ~)", i, min, max);
    return i;
}



// evaluate an expression to get a string
unsigned char __not_in_flash_func(*getstring)(unsigned char *p) {
    int t = T_STR;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;

    evaluate(p, &f, &i64, &s, &t, false);
    return s;
}



// evaluate an expression to get a string using the C style for a string
// as against the MMBasic style returned by getstring()
unsigned char __not_in_flash_func(*getCstring)(unsigned char *p) {
    unsigned char *tp;
    tp = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
    Mstrcpy(tp, getstring(p));                                      // get the string and save in a temp place
    MtoC(tp);                                                       // convert to a C style string
    return tp;
}
unsigned char __not_in_flash_func(*getFstring)(unsigned char *p) {
    unsigned char *tp;
    tp = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
    Mstrcpy(tp, getstring(p));                                      // get the string and save in a temp place
    for(int i=1;i<=*tp;i++)if(tp[i]=='\\')tp[i]='/';
    if((toupper(tp[1])=='A' || toupper(tp[1])=='B') && tp[2]==':' && !(tp[3]=='/')){
        memmove(&tp[4],&tp[3],tp[0]-2);
        tp[3]='/';
        tp[0]++;
    }
    MtoC(tp);
    return tp;
}



// recursively evaluate an expression observing the rules of operator precedence
unsigned char MIPS16 __not_in_flash_func(*doexpr)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta) {
    MMFLOAT fa1, fa2;
    long long int  ia1, ia2;
    int o1, o2;
    int t1, t2;
    unsigned char *sa1, *sa2;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    fa1 = *fa;
    ia1 = *ia;
    sa1 = *sa;
    t1 = TypeMask(*ta);
    o1 = *oo;
    p = getvalue(p, &fa2, &ia2, &sa2, &o2, &t2);
    while(1) {
        if(o2 == E_END || tokentbl[o1].precedence <= tokentbl[o2].precedence) {
            if((t1 & T_STR) != (t2 & T_STR)) error("Incompatible types in expression");
            targ = tokentbl[o1].type & (T_NBR | T_INT);
            if(targ == T_NBR) {                                     // if the operator does not work with ints convert the args to floats
                if(t1 & T_INT) { fa1 = ia1; t1 = T_NBR; }           // at this time the only example of this is op_div (/)
                if(t2 & T_INT) { fa2 = ia2; t2 = T_NBR; }
            }
            if(targ == T_INT) {                                     // if the operator does not work with floats convert the args to ints
                if(t1 & T_NBR) { ia1 = FloatToInt64(fa1); t1 = T_INT; }
                if(t2 & T_NBR) { ia2 = FloatToInt64(fa2); t2 = T_INT; }
            }
            if(targ == (T_NBR | T_INT)) {                               // if the operator will work with both floats and ints
                if(t1 & T_NBR && t2 & T_INT) { fa2 = ia2; t2 = T_NBR; } // if one arg is float convert the other to a float
                if(t1 & T_INT && t2 & T_NBR) { fa1 = ia1; t1 = T_NBR; }
            }
            if(!(tokentbl[o1].type & T_OPER) || !(tokentbl[o1].type & t1)) {
                error("Invalid operator");
            }
            farg1 = fa1; farg2 = fa2;                               // setup the float args (incase it is a float)
            sarg1 = sa1; sarg2 = sa2;                               // ditto string args
            iarg1 = ia1; iarg2 = ia2;                               // ditto integer args
            targ = t1;                                              // this is what both args are
            tokentbl[o1].fptr();                                    // call the operator function
            *fa = fret;
            *ia = iret;
            *sa = sret;
            *oo = o2;
            *ta = targ;
            return p;
        }
        // the next operator has a higher precedence, recursive call to evaluate it
        else
            p = doexpr(p, &fa2, &ia2, &sa2, &o2, &t2);
    }
}



// get a value, either from a constant, function or variable
// also returns the next operator to the right of the value or E_END if no operator
unsigned char MIPS16 __not_in_flash_func(*getvalue)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta) {
    MMFLOAT f = 0;
    long long int  i64 = 0;
    unsigned char *s = NULL;
    int t = T_NOTYPE;
    unsigned char *tp, *p1, *p2;
    int i;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    skipspace(p);
    if(*p>=C_BASETOKEN){ //don't waste time if not a built-in function
    // special processing for the NOT operator
    // just get the next value and invert its logical value
        if(*p<=131){
            // special processing for the unary operators
            if(tokenfunction(*p) == op_not) {
                int ro;
                p++; t = T_NOTYPE;
                p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
                if(t &T_NBR)
                    f = (MMFLOAT)((f != 0)?0:1);                              // invert the value returned
                else if(t & T_INT)
                    i64 = ((i64 != 0)?0:1);
                else
                    error("Expected a number");
                skipspace(p);
                *fa = f;                                                    // save what we have
                *ia = i64;
                *sa = s;
                *ta = t;
                *oo = ro;
                return p;                                                   // return straight away as we already have the next operator
            } else if(tokenfunction(*p) == op_inv) {
                int ro;
                p++; t = T_NOTYPE;
                p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
                if(t & T_NBR)
                    i64 = FloatToInt64(f);
                else if(!(t & T_INT))
                    error("Expected a number");
                i64 = ~i64;
                t = T_INT;
                skipspace(p);
                *fa = f;                                                    // save what we have
                *ia = i64;
                *sa = s;
                *ta = t;
                *oo = ro;
                return p;                                                   // return straight away as we already have the next operator
            } else  if(tokenfunction(*p) == op_subtract) {
                int ro;
                p++; t = T_NOTYPE;
                p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
                if(t & T_NBR)
                    f = -f;                                                 // negate the MMFLOAT returned
                else if(t & T_INT)
                    i64 = -i64;                                             // negate the integer returned
                else
                    error("Expected a number");
                skipspace(p);
                *fa = f;                                                    // save what we have
                *ia = i64;
                *sa = s;
                *ta = t;
                *oo = ro;
                return p;                                                   // return straight away as we already have the next operator
            } else if(tokenfunction(*p) == op_add) {
                int ro;
                p++; t = T_NOTYPE;
                p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
                skipspace(p);
                *fa = f;                                                    // save what we have
                *ia = i64;
                *sa = s;
                *ta = t;
                *oo = ro;
                return p;                                                   // return straight away as we already have the next operator
            }
        } 
        if(tokentype(*p) & (T_FUN | T_FNA)) {
    // if a function execute it and save the result
            int tmp;
            tp = p;
            // if it is a function with arguments we need to locate the closing bracket and copy the argument to
            // a temporary variable so that functions like getarg() will work.
            if(tokentype(*p) & T_FUN) {
                p1 = p + 1;
                p = getclosebracket(p);                             // find the closing bracket
                p2 = ep = GetTempMemory(STRINGSIZE);                           // this will last for the life of the command
                while(p1 != p) *p2++ = *p1++;
            }
            p++;                                                        // point to after the function (without argument) or after the closing bracket
            tmp = targ = TypeMask(tokentype(*tp));                      // set the type of the function (which might need to know this)
            tokenfunction(*tp)();                                       // execute the function
            if((tmp & targ) == 0) error("Internal fault 2(sorry)");      // as a safety check the function must return a type the same as set in the header
            t = targ;                                                   // save the type of the function
            f = fret; i64 = iret; s = sret;                             // save the result
        }
    } else {
    // if it is a variable or a defined function, find it and get its value
    	if(isnamestart(*p)) {
        // first check if it is terminated with a bracket
        tp = p + 1;
        while(isnamechar(*tp)) tp++;                                // search for the end of the identifier
        if(*tp == '$' || *tp == '%' || *tp == '!') tp++;
        i = -1;
        if(*tp == '(') i = FindSubFun(p, 1);                        // if terminated with a bracket it could be a function
        if(i >= 0) {                                                // >= 0 means it is a user defined function
            unsigned char *SaveCurrentLinePtr = CurrentLinePtr;              // in case the code in DefinedSubFun messes with this
            DefinedSubFun(true, p, i, &f, &i64, &s, &t);
            CurrentLinePtr = SaveCurrentLinePtr;
        } else {
            s = (unsigned char *)findvar(p, V_FIND);                         // if it is a string then the string pointer is automatically set
            t = TypeMask(g_vartbl[g_VarIndex].type);
            if(t & T_NBR) f = (*(MMFLOAT *)s);
            if(t & T_INT) i64 = (*(long long int  *)s);
        }
        p = skipvar(p, false);
    }
   // is it an ordinary numeric constant?  get its value if yes
    // a leading + or - might have been converted to a token so we need to check for them also
    else if(IsDigitinline(*p) || *p == '.') {
			char ts[31], *tsp;
			int isi64 = true;
			tsp = ts;
			int isf=true;
			long long int scale=0;
			// copy the first digit of the string to a temporary place
			if(*p == '.') {
				isi64 = false;
				scale=1;
			} else if(IsDigitinline(*p)){
				i64=(*p - '0');
			}
			*tsp++ = *p++;

			// now concatenate the remaining digits
			while((digit[(uint8_t)*p]) && (tsp - ts) < 30) {
				if(*p >= '0' && *p <= '9'){
					i64 = i64 * 10 + (*p - '0');
					if(scale)scale*=10;
				} else {
					if((*p) == '.'){
						isi64 = false;
						scale =1;
					} else {
						if(mytoupper(*p) == 'E' || *p == '-' || *p == '+' ){
							isi64 = false;
							isf=false;
						}
					}
				}
				*tsp++ = *p++;                                          // copy the string to a temporary place
			}
			*tsp = 0;                                                   // terminate it
			if(isi64) {
				t = T_INT;
			} else if(isf && (tsp - ts) < 18) {
				f=(MMFLOAT)i64/(MMFLOAT)scale;
				t = T_NBR;
			} else {
				f = (MMFLOAT)strtod(ts, &tsp);                          // and convert to a MMFLOAT
				t = T_NBR;
			}
    }


   // if it is a numeric constant starting with the & character then get its base and convert to an integer
    else if(*p == '&') {
        p++; i64 = 0;
        switch(mytoupper(*p++)) {
            case 'H':   while(isxdigit(*p)) {
                            i64 = (i64 << 4) | ((mytoupper(*p) >= 'A') ? mytoupper(*p) - 'A' + 10 : *p - '0');
                            p++;
                        } break;
            case 'O':   while(*p >= '0' && *p <= '7') {
                            i64 = (i64 << 3) | (*p++ - '0');
                        } break;
            case 'B':   while(*p == '0' || *p == '1') {
                            i64 = (i64 << 1) | (*p++ - '0');
                        } break;
            default:    error("Type prefix");
        }
        t = T_INT;
    }
		// if opening bracket then first evaluate the contents of the bracket
    	else if(*p == '(') {
			p++;                                                        // step over the bracket
			p = evaluate(p, &f, &i64, &s, &t, true);                    // recursively get the contents
			if(*p != ')') error("No closing bracket");
			++p;                                                        // step over the closing bracket
		}
		// if it is a string constant, return a pointer to that.  Note: tokenise() guarantees that strings end with a quote
		else if(*p == '"') {
			p++;                                                        // step over the quote
			p1 = s = GetTempMemory(STRINGSIZE);                                // this will last for the life of the command
			tp = (unsigned char *)strchr((char *)p, '"');
                int toggle=0;
                while(p != tp){
                    if(*p=='\\' && tp>p+1 && OptionEscape)toggle^=1;
                    if(toggle){
                        if(*p=='\\' && isdigit(p[1]) && isdigit(p[2]) && isdigit(p[3])){
                            p++;
                            i=(*p++)-48;
                            i*=10;
                            i+=(*p++)-48;
                            i*=10;
                            i+=(*p++)-48;
                            if(i==0)error("Null character \\000 in escape sequence - use CHR$(0)","$");
                            *p1++=i;
                        } else {
                            p++;
                            switch(*p){
                                case '\\':
                                    *p1++='\\';
                                    p++;
                                    break;
                                case 'a':
                                    *p1++='\a';
                                    p++;
                                    break;
                                case 'b':
                                    *p1++='\b';
                                    p++;
                                    break;
                                case 'e':
                                    *p1++='\e';
                                    p++;
                                    break;
                                case 'f':
                                    *p1++='\f';
                                    p++;
                                    break;
                                case 'n':
                                    *p1++='\n';
                                    p++;
                                    break;
                                case 'q':
                                    *p1++='\"';
                                    p++;
                                    break;
                                case 'r':
                                    *p1++='\r';
                                    p++;
                                    break;
                                case 't':
                                    *p1++='\t';
                                    p++;
                                    break;
                                case 'v':
                                    *p1++='\v';
                                    p++;
                                    break;
                                case '&':
                                    p++;
                                    if(isxdigit(*p) && isxdigit(p[1])){
                                        i=0;
                                        i = (i << 4) | ((mytoupper(*p) >= 'A') ? mytoupper(*p) - 'A' + 10 : *p - '0');
                                        p++;
                                        i = (i << 4) | ((mytoupper(*p) >= 'A') ? mytoupper(*p) - 'A' + 10 : *p - '0');
                                        p++;
                                        if(i==0)error("Null character \\&00 in escape sequence - use CHR$(0)","$");
                                        *p1++=i;
                                    } else *p1++='x';
                                    break;
                                default:
                                    *p1++=*p++;
                            }
                        }
                        toggle=0;
                    } else *p1++ = *p++;
                } 
			p++;
			CtoM(s);                                                    // convert to a MMBasic string
			t = T_STR;
    }
    else
        error("Syntax");
    }
    skipspace(p);
    *fa = f;                                                        // save what we have
    *ia = i64;
    *sa = s;
    *ta = t;

    // get the next operator, if there is not an operator set the operator to end of expression (E_END)
    if(tokentype(*p) & T_OPER)
        *oo = *p++ - C_BASETOKEN;
    else
        *oo = E_END;

    return p;
}





// search through program memory looking for a line number. Stops when it has a matching or larger number
// returns a pointer to the T_NEWLINE token or a pointer to the two zero characters representing the end of the program
unsigned char MIPS16 *findline(int nbr, int mustfind) {
    unsigned char *p;
    unsigned char *next;
    int i,j=0;
    p = ProgMemory;
    next=LibMemory;
    if (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE){
       if (CurrentLinePtr >= LibMemory && CurrentLinePtr <= LibMemory + MAX_PROG_SIZE){
          p=LibMemory;
          next=ProgMemory;
       }
    }
   while(1) {
        if(p[0] == 0 && p[1] == 0) {
             
             if (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE){
                if(j==0){
                  j=1;  
                  p = next;
                }else{
                  i = MAXLINENBR;
                  break;   
                } 
             } else{
               i = MAXLINENBR;
               break;
             } 
        }

        if(p[0] == T_NEWLINE) {
            p++;
            continue;
        }

        if(p[0] == T_LINENBR) {
            i = (p[1] << 8) | p[2];
            if(mustfind) {
                if(i == nbr) break;
            } else {
                if(i >= nbr) break;
            }
            p += 3;
            continue;
        }

        if(p[0] == T_LABEL) {
            p += p[1] + 2;
            continue;
        }

        p++;
    }
    if(mustfind && i != nbr)
        error("Line number");
    return p;
}
#ifdef rp2350
void hashlabels(unsigned char *p,int ErrAbort){   
    //unsigned char *p = (unsigned char *)ProgMemory;
    int j, u, namelen;
    uint32_t originalhash,hash=FNV_offset_basis;
   // char *lastp = (char *)ProgMemory + 1;
    char *lastp = (char *)p + 1;
    // now do the search
    while(1) {
        if(p[0] == 0 && p[1] == 0)                                  // end of the program
            break;

        if(p[0] == T_NEWLINE) {
            lastp = (char *)p;                                              // save in case this is the right line
            p++;                                                    // and step over the line number
            continue;
        }

        if(p[0] == T_LINENBR) {
            p += 3;                                                 // and step over the line number
            continue;
        }

        if(p[0] == T_LABEL) {
            p++;                                                    // point to the length of the label
            hash=FNV_offset_basis;
            namelen=0;
        	for(j=1;j<=p[0];j++) {
                u=mytoupper(p[j]);
        		hash ^= u;
        		hash*=FNV_prime;
        		namelen++;
        	}
        	hash %= MAXSUBHASH; //scale to size of table
            originalhash=hash-1;
            if(originalhash<0)originalhash+=MAXSUBFUN;
    		while(funtbl[hash].name[0]!=0 && hash!=originalhash){
     			hash++;
     			hash %= MAXSUBFUN;
    		}
            if(hash==originalhash){
                MMPrintString("Error: Too many labels - erasing program\r\n");
                unsigned char dummy=0;
                cmdline=&dummy;
                cmd_new();
                // jump back to the input prompt
            }
    		funtbl[hash].index=(uint32_t)lastp;
            for(j=0;j<p[0];j++)funtbl[hash].name[j]=mytoupper(p[j+1]);
            p += p[0] + 1;                                          // still looking! skip over the label
            continue;
        }
        p++;
    }
}

// search through program memory looking for a label.
// returns a pointer to the T_NEWLINE token or throws an error if not found
// non cached version
unsigned char *findlabel(unsigned char *labelptr) {
//    char *p, *lastp = (char *)ProgMemory + 1;
	unsigned char  *tp, *ip;
    int i;
    uint32_t hash=FNV_offset_basis;
    char label[MAXVARLEN + 1];

    // first, just exit we have a NULL argument
    if(labelptr == NULL) return NULL;

    // convert the label to the token format and load into label[]
    // this assumes that the first character has already been verified as a valid label character
    label[1] = mytoupper(*labelptr++);
	hash ^= label[1];
	hash*=FNV_prime;
    for(i = 2; ; i++) {
        if(!isnamechar(*labelptr)) break;                           // the end of the label
        if(i > MAXVARLEN ) error("Label too long");                 // too long, not a correctly formed label
        label[i]=mytoupper(*labelptr++);
		hash ^= label[i];
		hash*=FNV_prime;
    }
    label[0] = i - 1;                                               // the length byte
	hash %= MAXSUBHASH; //scale to size of table
	if(funtbl[hash].name[0]==0)error("Cannot find label");
	while(funtbl[hash].name[0]!=0){
		//if(funtbl[hash].index>=(uint32_t)ProgMemory){  //Is there a need to test this? Without this we can find labels in the Library
			tp=(unsigned char *)funtbl[hash].name;
			ip=(unsigned char *)&label[1];
			if(*ip++ == *tp++) {                 // preliminary quick check
				i = label[0]-1;
				while(i > 0 && *ip == *tp) {                              // compare each letter
					i--; ip++; tp++;
				}
				if(i == 0  && (*(char *)tp == 0)) {       // found a matching name
					return (unsigned char *)funtbl[hash].index;
				}
			}
		//}
		hash++;
		hash %= MAXSUBFUN;
	}
	if(funtbl[hash].name[0]==0)error("Cannot find label");
	return 0;

}
#else
unsigned char MIPS16 *findlabel(unsigned char *labelptr) {
    char *p, *lastp = (char *)ProgMemory + 1;
    char *next;
    int i,j=0;
    char label[MAXVARLEN + 1];

    // first, just exit we have a NULL argument
    if(labelptr == NULL) return NULL;

    // convert the label to the token format and load into label[]
    // this assumes that the first character has already been verified as a valid label character
    label[1] = *labelptr++;
    for(i = 2; ; i++) {
        if(!isnamechar(*labelptr)) break;                           // the end of the label
        if(i > MAXVARLEN ) error("Label too long");                 // too long, not a correctly formed label
        label[i] = *labelptr++;
    }
    label[0] = i - 1;                                               // the length byte
   
    p = (char *)ProgMemory;
    next=(char *)LibMemory;
    if (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE){
       if (CurrentLinePtr >= LibMemory && CurrentLinePtr <= LibMemory + MAX_PROG_SIZE){
          p=(char *)LibMemory;
          next=(char *)ProgMemory;
       }
    }

    // now do the search
    while(1) {
        if(p[0] == 0 && p[1] == 0) {                                // end of the program
            if (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE){
                if(j==0){
                  j=1;  
                  p = next;
                }else{
                  error("Cannot find label"); 
                } 
             } else{
               error("Cannot find label");
             } 
            
        }
        if(p[0] == T_NEWLINE) {
            lastp = p;                                              // save in case this is the right line
            p++;                                                    // and step over the line number
            continue;
        }

        if(p[0] == T_LINENBR) {
            p += 3;                                                 // and step over the line number
            continue;
        }

        if(p[0] == T_LABEL) {
            p++;                                                    // point to the length of the label
            if(mem_equal((unsigned char *)p, (unsigned char *)label, label[0] + 1))                   // compare the strings including the length byte
                return (unsigned char *)lastp;                                       // and if successful return pointing to the beginning of the line
            p += p[0] + 1;                                          // still looking! skip over the label
            continue;
        }

        p++;
    }
}
#endif

// returns true if 'line' is a valid line in the program
int IsValidLine(int nbr) {
    unsigned char *p;
    p = findline(nbr, false);
    if(*p == T_NEWLINE) p++;
    if(*p == T_LINENBR) {
        if(((p[1] << 8) | p[2]) == nbr) return true;
    }
    return false;
}


// count the number of lines up to and including the line pointed to by the argument
// used for error reporting in programs that do not use line numbers
int MIPS16 CountLines(unsigned char *target) {
    unsigned char *p;
    int cnt;

    p = ProgMemory;
    if(ProgMemory[0]==1 && ProgMemory[1]==39 && ProgMemory[2]==35)cnt=-1;
    else cnt = 0;

    while(1) {
        if(*p == 0xff || (p[0] == 0 && p[1] == 0))                   // end of the program
            return cnt;

        if(*p == T_NEWLINE) {
            p++;                                                 // and step over the line number
            cnt++;
            if(p >= target) return cnt;
            continue;
        }

        if(*p == T_LINENBR) {
            p += 3;                                                 // and step over the line number
            continue;
        }

        if(*p == T_LABEL) {
            p += p[0] + 2;                                          // still looking! skip over the label
            continue;
        }

        if(p++ > target) return cnt;

    }
}



/********************************************************************************************************************************************
routines for storing and manipulating variables
********************************************************************************************************************************************/


// find or create a variable
// the action parameter can be the following (these can be ORed together)
// - V_FIND    a straight forward find, if the variable is not found it is created and set to zero
// - V_NOFIND_ERR    throw an error if not found
// - V_NOFIND_NULL   return a null pointer if not found
// - V_DIM_VAR    dimension an array
// - V_LOCAL   create a local variable
//
// there are four types of variable:
//  - T_NOTYPE a free slot that was used but is now free for reuse
//  - T_STR string variable
//  - T_NBR holds a float
//  - T_INT integer variable
//
// A variable can have a number of characteristics
//  - T_PTR the variable points to another variable's data
//  - T_IMPLIED  the variables type does not have to be specified with a suffix
//  - T_CONST the contents of this variable cannot be changed
//  - T_FUNCT this variable represents the return value from a function
//
// storage of the variable's data:
//      if it is type T_NBR or T_INT the value is held in the variable slot
//      for T_STR a block of memory of MAXSTRLEN size (or size determined by the LENGTH keyword) will be malloc'ed and the pointer stored in the variable slot.
#ifdef PICOMITEWEB
#ifdef rp2350
void MIPS16 __not_in_flash_func(*findvar)(unsigned char *p, int action) {
#else
void MIPS16 *findvar(unsigned char *p, int action) {
#endif
#else
void MIPS16 __not_in_flash_func(*findvar)(unsigned char *p, int action) {
#endif
    unsigned char name[MAXVARLEN + 1];
    int i=0, j, size, ifree, globalifree, localifree, nbr, vtype, vindex, namelen, tmp;
    unsigned char *s, *x, u, suffix=0;
    void *mptr;
//    int hashIndex=0;
    int GlobalhashIndex, OriginalGlobalHash;
    int LocalhashIndex, OriginalLocalHash;
    uint32_t hash=FNV_offset_basis;
#ifdef rp2350
	uint32_t funhash;
#endif
	char  *tp, *ip;
    int dim[MAXDIM]={0}, dnbr;
//	if(__get_MSP() < (uint32_t)&stackcheck-0x5000){
//		error("Expression is too complex at depth %",g_LocalIndex);
//	}
    vtype = dnbr = emptyarray = 0;
    // first zero the array used for holding the dimension values
//    for(i = 0; i < MAXDIM; i++) dim[i] = 0;
    ifree = -1;

    // check the first char for a legal variable name
    skipspace(p);
    if(!isnamestart(*p)) error("Variable name");

    // copy the variable name into name
    s = name; namelen = 0;
	do {
		u=mytoupper(*p++);
		hash ^= u;
		hash*=FNV_prime;
		*s++ = u;
		if(++namelen > MAXVARLEN) error("Variable name too long");
	} while(isnamechar(*p));
#ifdef rp2350
    funhash=hash % MAXSUBHASH;
#endif
	hash %= MAXVARHASH; //scale 0-255
    
	if(namelen!=MAXVARLEN)*s=0;
    // check the terminating char and set the type
    if(*p == '$') {
        if((action & T_IMPLIED) && !(action & T_STR)) error("Conflicting variable type");
        vtype = T_STR;
        suffix=1;
        p++;
    } else if(*p == '%') {
        if((action & T_IMPLIED) && !(action & T_INT)) error("Conflicting variable type");
        vtype = T_INT;
        suffix=1;
        p++;
    } else if(*p == '!') {
        if((action & T_IMPLIED) && !(action & T_NBR)) error("Conflicting variable type");
        vtype = T_NBR;
        suffix=1;
        p++;
    } else if((action & V_DIM_VAR) && DefaultType == T_NOTYPE && !(action & T_IMPLIED))
        error("Variable type not specified");
    else
        vtype = 0;

    // check if this is an array
    if(*p == '(') {
        char *pp = (char *)p + 1;
        skipspace(pp);
        if(action & V_EMPTY_OK && *pp == ')')  {                     // if this is an empty array.  eg  ()
        	emptyarray=1;
            dnbr = -1;                                              // flag this
        }  else {                                                      // else, get the dimensions
            // start a new block - getargs macro must be the first executable stmt in a block
            // split the argument into individual elements
            // find the value of each dimension and store in dims[]
            // the bracket in "(," is a signal to getargs that the list is in brackets
            getargs(&p, MAXDIM * 2, (unsigned char *)"(,");
            if((argc & 0x01) == 0) error("Dimensions");
            dnbr = argc/2 + 1;
            if(dnbr > MAXDIM) error("Dimensions");
            for(i = 0; i < argc; i += 2) {
                MMFLOAT f;
                long long int in;
                char *s;
                int targ = T_NOTYPE;
                evaluate(argv[i], &f, &in, (unsigned char **)&s, &targ, false);       // get the value and type of the argument
                if(targ == T_STR) dnbr = MAXDIM;                    // force an error to be thrown later (with the correct message)
                if(targ == T_NBR) in = FloatToInt32(f);
                dim[i/2] = in;
                if(dim[i/2] < g_OptionBase) error("Dimensions");
            }
        }
    }

    // we now have the variable name and, if it is an array, the parameters
    // search the table looking for a match

    LocalhashIndex=hash;
    OriginalLocalHash=LocalhashIndex-1;
    if(OriginalLocalHash<0)OriginalLocalHash+=MAXVARS/2;
    localifree=-1;
    GlobalhashIndex=hash+MAXVARS/2;
    OriginalGlobalHash=GlobalhashIndex-1;
    if(OriginalGlobalHash<MAXVARS/2)OriginalGlobalHash+=MAXVARS/2;
	globalifree=-1;
	tmp=-1;
    if(g_LocalIndex){ //search
		if(g_vartbl[LocalhashIndex].type == T_NOTYPE){
			localifree = LocalhashIndex;
		} else {
			while(g_vartbl[LocalhashIndex].name[0]!=0){
				ip=(char *)name;
				tp=(char *)g_vartbl[LocalhashIndex].name;
				if(g_vartbl[LocalhashIndex].type==T_BLOCKED)tmp=LocalhashIndex;
				if(*ip++ == *tp++) {                 // preliminary quick check
					j = namelen-1;
					while(j > 0 && *ip == *tp) {                              // compare each letter
						j--; ip++; tp++;
					}
					if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
						if(g_vartbl[LocalhashIndex].level == g_LocalIndex) break; //matching global while not in a subroutine
					}
				}
				LocalhashIndex++;
				LocalhashIndex %= MAXVARS/2;
                if(LocalhashIndex==OriginalLocalHash)error("Too many local variables");
			}
			if(g_vartbl[LocalhashIndex].name[0]==0){ // not found
				localifree=LocalhashIndex;
				if(tmp!=-1){
					localifree=tmp;
					g_vartbl[LocalhashIndex].type=T_NOTYPE;
					g_vartbl[LocalhashIndex].name[0]=0;
				}
			}
		}
		if(g_vartbl[LocalhashIndex].name[0]==0){ // not found in the local table so try the global
			tmp=-1;
			globalifree=-1;
			if(g_vartbl[GlobalhashIndex].type == T_NOTYPE){
				globalifree = GlobalhashIndex;
			} else {
				while(g_vartbl[GlobalhashIndex].name[0]!=0){
					ip=(char *)name;
					tp=(char *)g_vartbl[GlobalhashIndex].name;
					if(g_vartbl[GlobalhashIndex].type==T_BLOCKED)tmp=GlobalhashIndex;
					if(*ip++ == *tp++) {                 // preliminary quick check
						j = namelen-1;
						while(j > 0 && *ip == *tp) {                              // compare each letter
							j--; ip++; tp++;
						}
						if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
							break; //matching global while not in a subroutine
						}
					}
					GlobalhashIndex++;
					if(GlobalhashIndex==MAXVARS)GlobalhashIndex=MAXVARS/2;
                    if(GlobalhashIndex==OriginalGlobalHash)error("Too many global variables");
				}
				if(g_vartbl[GlobalhashIndex].name[0]==0){ // not found
					globalifree=GlobalhashIndex;
					if(tmp!=-1){
						globalifree=tmp;
						g_vartbl[GlobalhashIndex].type=T_NOTYPE;
						g_vartbl[GlobalhashIndex].name[0]=0;
					}
				}
			}
		}
    } else {
    	localifree=9999; //set a marker that a local variable is irrelevant
		if(g_vartbl[GlobalhashIndex].type == T_NOTYPE){
			globalifree = GlobalhashIndex;
		} else {
			while(g_vartbl[GlobalhashIndex].name[0]!=0){
				ip=(char *)name;
				tp=(char *)g_vartbl[GlobalhashIndex].name;
				if(g_vartbl[GlobalhashIndex].type==T_BLOCKED)tmp=GlobalhashIndex;
				if(*ip++ == *tp++) {                 // preliminary quick check
					j = namelen-1;
					while(j > 0 && *ip == *tp) {                              // compare each letter
						j--; ip++; tp++;
					}
					if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
						break; //matching global while not in a subroutine
					}
				}
				GlobalhashIndex++;
				if(GlobalhashIndex==MAXVARS)GlobalhashIndex=MAXVARS/2;
			}
			if(g_vartbl[GlobalhashIndex].name[0]==0){ // not found
				globalifree=GlobalhashIndex;
				if(tmp!=-1){
					globalifree=tmp;
					g_vartbl[GlobalhashIndex].type=T_NOTYPE;
					g_vartbl[GlobalhashIndex].name[0]=0;
				}
			}
		}
    }
//	MMPrintString("search status : ");PInt(g_LocalIndex);PIntComma(localifree);PIntComma(LocalhashIndex);PIntComma(globalifree);PIntComma(GlobalhashIndex);
//	MMPrintString((action & V_LOCAL ? " LOCAL" : "      "));MMPrintString((action & V_LOCAL ? " DIM" : "    "));PRet();
	// At this point we know if a local variable has been found or if a global variable has been found
    if(action & V_LOCAL) {
        // if we declared the variable as LOCAL within a sub/fun and an existing local was found
        if(localifree==-1) error("$ Local variable already declared", name);
    } else if(action & V_DIM_VAR) {
        // if are using DIM to declare a global variable and an existing global variable was found
        if(globalifree==-1 ) error("$ Global variable already declared", name);
    }
	// we are not declaring the variable but it may need to be created
    if(action & V_LOCAL) {
    	ifree = i = localifree;
    } else if(localifree==-1){ // can only happen when a local variable has been found so we can ignore everything global
		ifree= -1;
		i = LocalhashIndex;
	} else if(globalifree==-1){ //A global variable has been found
		ifree= -1;
		i = GlobalhashIndex;
	} else { //nothing has been found so we are going to create a global unless EXPLICIT is set
		ifree = i = globalifree;
	}

//    MMPrintString(name);PIntComma(i);MMPrintString((ifree==-1 ? " - found" : " - not there"));PRet();

    // if we found an existing and matching variable
    // set the global g_VarIndex indicating the index in the table
    if(ifree==-1 && g_vartbl[i].name[0] != 0) {
        g_VarIndex = vindex = i;

        // check that the dimensions match
        for(i = 0; i < MAXDIM && g_vartbl[vindex].dims[i] != 0; i++);
        if(dnbr == -1) {
            if(i == 0) error("Array dimensions");
        } else {
            if(i != dnbr) error("Array dimensions");
        }

        if(vtype == 0) {
            if(!(g_vartbl[vindex].type & (DefaultType | T_IMPLIED))) error("$ Different type already declared", name);
        } else {
            if(!(g_vartbl[vindex].type & vtype)) error("$ Different type already declared", name);
        }

        // if it is a non arrayed variable or an empty array it is easy, just calculate and return a pointer to the value
        if(dnbr == -1 || g_vartbl[vindex].dims[0] == 0) {
            if(dnbr == -1 || g_vartbl[vindex].type & (T_PTR | T_STR))
                return g_vartbl[vindex].val.s;                        // if it is a string or pointer just return the pointer to the data
            else
                if(g_vartbl[vindex].type & (T_INT))
                    return &(g_vartbl[vindex].val.i);                 // must be an integer, point to its value
                else
                    return &(g_vartbl[vindex].val.f);                 // must be a straight number (float), point to its value
         }

        // if we reached this point it must be a reference to an existing array
        // check that we are not using DIM and that all parameters are within the dimensions
        if(action & V_DIM_VAR) error("Cannot re dimension array");
        for(i = 0; i < dnbr; i++) {
            if(dim[i] > g_vartbl[vindex].dims[i] || dim[i] < g_OptionBase)
                error("Index out of bounds");
        }

        // then calculate the index into the array.  Bug fix by Gerard Sexton.
        nbr = dim[0] - g_OptionBase;
        j = 1;
        for(i = 1; i < dnbr; i++) {
            j *= (g_vartbl[vindex].dims[i - 1] + 1 - g_OptionBase);
            nbr += (dim[i] - g_OptionBase) * j;
        }
        // finally return a pointer to the value
        if(g_vartbl[vindex].type & T_NBR)
            return g_vartbl[vindex].val.s + (nbr * sizeof(MMFLOAT));
        else
            if(g_vartbl[vindex].type & T_INT)
                return g_vartbl[vindex].val.s + (nbr * sizeof(long long int));
            else
                return g_vartbl[vindex].val.s + (nbr * (g_vartbl[vindex].size + 1));
    }

    // we reached this point if no existing variable has been found
    if(action & V_NOFIND_ERR) error("Cannot find $", name);
    if(action & V_NOFIND_NULL) return NULL;
    if((OptionExplicit || dnbr != 0) && !(action & V_DIM_VAR))
        error("$ is not declared", name);
    if(vtype == 0) {
        if(action & T_IMPLIED)
            vtype = (action & (T_NBR | T_INT | T_STR));
        else
            vtype = DefaultType;
    }
    // now scan the sub/fun table to make sure that there is not a sub/fun with the same name
#ifdef rp2350
    if(!(action & V_FUNCT) && (funtbl[funhash].name[0])) {                                       // don't do this if we are defining the local variable for a function name
		while(funtbl[funhash].name[0]!=0){
			ip=(char *)name;
			tp=funtbl[funhash].name;
			if(*ip++ == *tp++) {                 // preliminary quick check
				j = namelen-1;
				while(j > 0 && *ip == *tp) {                              // compare each letter
					j--; ip++; tp++;
				}
				if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
					if(funtbl[funhash].index<MAXSUBFUN)error("A sub/fun has the same name: $", name);
				}
			}
			funhash++;
			if(funhash==MAXSUBFUN)funhash=0;
        }
    }
#else
    if(!(action & V_FUNCT)) {                                       // don't do this if we are defining the local variable for a function name
        for(i = 0;  i < MAXSUBFUN && subfun[i] != NULL; i++) {
            x = subfun[i];                                          // point to the command token
            x++; skipspace(x);                                      // point to the identifier
            s = name;                                               // point to the new variable
            if(*s != toupper(*x)) continue;                         // quick first test
            while(1) {
                if(!isnamechar(*s) && !isnamechar(*x)) error("A sub/fun has the same name: $", name);
                if(*s != toupper(*x) || *s == 0 || !isnamechar(*x) || s - name >= MAXVARLEN) break;
                s++; x++;
            }
        }
    }
#endif
    // set a default string size
    size = MAXSTRLEN;

    // if it is an array we must be dimensioning it
    // if it is a string array we skip over the dimension values and look for the LENGTH keyword
    // and if found find the string size and change the g_vartbl entry
      if(action & V_DIM_VAR) {
          if(vtype & T_STR) {
            i = 0;
            if(*p == '(') {
                do {
                    if(*p == '(') i++;
                    if(tokentype(*p) & T_FUN) i++;
                    if(*p == ')') i--;
                    p++;
                } while(i);
            }
            skipspace(p);
            if((s = checkstring(p, (unsigned char *)"LENGTH")) != NULL)
                size = getint(s, 1, MAXSTRLEN) ;
            else
                if(!(*p == ',' || *p == 0 || tokenfunction(*p) == op_equal || tokenfunction(*p) == op_invalid)) error("Unexpected text: $", p);
        }
    }


    // at this point we need to create the variable
    // as a result of the previous search ifree is the index to the entry that we should use

 // if we are adding to the top, increment the number of vars
	if(ifree>=MAXVARS/2){
		g_Globalvarcnt++;
		if(g_Globalvarcnt>=MAXVARS/2)error("Not enough Global variable memory");
	} else {
		g_Localvarcnt++;
		if(g_Localvarcnt>=MAXVARS/2)error("Not enough Local variable memory");
	}
	g_varcnt=g_Globalvarcnt+g_Localvarcnt;
    g_VarIndex = vindex = ifree;

    // initialise it: save the name, set the initial value to zero and set the type
    s = name;  x = g_vartbl[ifree].name; j = namelen;
    while(j--) *x++ = *s++;
    if(namelen < MAXVARLEN)*x++ = 0;
    g_vartbl[ifree].type = vtype | (action & (T_IMPLIED | T_CONST));
    if(suffix)g_vartbl[ifree].type|=T_EXPLICIT;
    if(ifree<MAXVARS/2){
    	g_hashlist[g_hashlistpointer].level=g_LocalIndex;
    	g_hashlist[g_hashlistpointer++].hash=ifree;
        g_vartbl[ifree].level = g_LocalIndex;
    } else g_vartbl[ifree].level = 0;
//    cleardims(&g_vartbl[ifree].dims[0]);
    for(j = 0; j < MAXDIM; j++) g_vartbl[ifree].dims[j] = 0;
//    MMPrintString("Creating variable : ");MMPrintString(g_vartbl[ifree].name);MMPrintString(", at depth : ");PInt(g_vartbl[ifree].level);MMPrintString(", Type : ");PInt(vtype);MMPrintString(", hash key : ");PInt(ifree);PRet();
    // the easy request is for is a non array numeric variable, so just initialise to
    // zero and return the pointer
    if(dnbr == 0) {
        if(vtype & T_NBR) {
            g_vartbl[ifree].val.f = 0;
            return &(g_vartbl[ifree].val.f);
        } else if(vtype & T_INT) {
            g_vartbl[ifree].val.i = 0;
            return &(g_vartbl[ifree].val.i);
        }
    }

    // if this is a definition of an empty array (only used in the parameter list for a sub/function)
    if(dnbr == -1) {
        g_vartbl[vindex].dims[0] = -1;                                // let the caller know that this is an empty array and needs more work
        return g_vartbl[vindex].val.s;                                // just return a pointer to the data element as it will be replaced in the sub/fun with a pointer
    }

    // if this is an array copy the array dimensions and calculate the overall size
    // for a non array string this will leave nbr = 1 which is just what we want
    for(nbr = 1, i = 0; i < dnbr; i++) {
        if(dim[i] <= g_OptionBase) error("Dimensions");
        g_vartbl[vindex].dims[i] = dim[i];
        nbr *= (dim[i] + 1 - g_OptionBase);
    }

    // we now have a string, an array of strings or an array of numbers
    // all need some memory to be allocated (note: GetMemory() zeros the memory)

    // First, set the important characteristics of the variable to indicate that the
    // variable is not allocated.  Thus, if GetMemory() fails with "not enough memory",
    // the variable will remain not allocated
    g_vartbl[ifree].val.s = NULL;
    g_vartbl[ifree].type = T_BLOCKED;
    i = *g_vartbl[ifree].name;   *g_vartbl[ifree].name = 0;
	j = g_vartbl[ifree].dims[0]; g_vartbl[ifree].dims[0] = 0;


	// Now, grab the memory
    if(vtype & (T_NBR | T_INT)) {
    	tmp=(nbr * sizeof(MMFLOAT));
        if(tmp<=256)mptr = GetMemory(STRINGSIZE);
        else mptr = GetMemory(tmp);
    }  else {
    	tmp=(nbr * (size + 1));
    	if(tmp<=(MAXDIM-1)*sizeof(g_vartbl[ifree].dims[1]) && j==0)mptr = (void *)&g_vartbl[ifree].dims[1];
    	else if(tmp<=256)mptr = GetMemory(STRINGSIZE);
        else mptr = GetMemory(tmp);
    }


    // If we reached here the memory request was successful, so restore the details of
    // the variable that were saved previously and set the variables pointer to the
    // allocated memory
    g_vartbl[ifree].type = vtype | (action & (T_IMPLIED | T_CONST));
    if(suffix)g_vartbl[ifree].type|=T_EXPLICIT;
    *g_vartbl[ifree].name = i;
    g_vartbl[ifree].dims[0] = j;
    g_vartbl[ifree].size = size;
    g_vartbl[ifree].val.s = mptr;
    return mptr;
}




/********************************************************************************************************************************************
 utility routines
 these routines form a library of functions that any command or function can use when dealing with its arguments
 by centralising these routines it is hoped that bugs can be more easily found and corrected (unlike bwBasic !)
*********************************************************************************************************************************************/

// take a line of basic code and split it into arguments
// this function should always be called via the macro getargs
//
// a new argument is created by any of the chars in the string delim (not in brackets or quotes)
// with this function commands have much less work to do to evaluate the arguments
//
// The arguments are:
//   pointer to a pointer which points to the string to be broken into arguments.
//   the maximum number of arguments that are expected.  an error will be thrown if more than this are found.
//   buffer where the returned strings are to be stored
//   pointer to an array of strings that will contain (after the function has returned) the values of each argument
//   pointer to an integer that will contain (after the function has returned) the number of arguments found
//   pointer to a string that contains the characters to be used in spliting up the line.  If the first unsigned char of that
//       string is an opening bracket '(' this function will expect the arg list to be enclosed in brackets.
void MIPS16 __not_in_flash_func(makeargs)(unsigned char **p, int maxargs, unsigned char *argbuf, unsigned char *argv[], int *argc, unsigned char *delim) {
    unsigned char *op;
    int inarg, expect_cmd, expect_bracket, then_tkn, else_tkn;
    unsigned char *tp;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    tp = *p;
    op = argbuf;
    *argc = 0;
    inarg = false;
    expect_cmd = false;
    expect_bracket = false;
    then_tkn = tokenTHEN;
    else_tkn = tokenELSE;

    // skip leading spaces
    while(*tp == ' ') tp++;

    // check if we are processing a list enclosed in brackets and if so
    //  - skip the opening bracket
    //  - flag that a closing bracket should be found
    if(*delim == '(') {
        if(*tp != '(')
            error("Syntax");
        expect_bracket = true;
        delim++;
        tp++;
    }

    // the main processing loop
    while(*tp) {

        if(expect_bracket == true && *tp == ')') break;

        // comment char causes the rest of the line to be skipped
        if(*tp == '\'') {
            break;
        }

        // the special characters that cause the line to be split up are in the string delim
        // any other chars form part of the one argument
        if(strchr((char *)delim, (char)*tp) != NULL && !expect_cmd) {
            if(*tp == then_tkn || *tp == else_tkn) expect_cmd = true;
            if(inarg) {                                             // if we have been processing an argument
                while(op > argbuf && *(op - 1) == ' ') op--;        // trim trailing spaces
                *op++ = 0;                                          // terminate it
            } else if(*argc) {                                      // otherwise we have two delimiters in a row (except for the first argument)
                argv[(*argc)++] = op;                               // create a null argument to go between the two delimiters
                *op++ = 0;                                          // and terminate it
            }

            inarg = false;
            if(*argc >= maxargs) error("Syntax");
            argv[(*argc)++] = op;                                   // save the pointer for this delimiter
            *op++ = *tp++;                                          // copy the token or char (always one)
            *op++ = 0;                                              // terminate it
            continue;
        }

        // check if we have a THEN or ELSE token and if so flag that a command should be next
        if(*tp == then_tkn || *tp == else_tkn) expect_cmd = true;


        // remove all spaces (outside of quoted text and bracketed text)
        if(!inarg && *tp == ' ') {
            tp++;
            continue;
        }

        // not a special char so we must start a new argument
        if(!inarg) {
            if(*argc >= maxargs) error("Syntax");
            argv[(*argc)++] = op;                                   // save the pointer for this arg
            inarg = true;
        }

        // if an opening bracket '(' copy everything until we hit the matching closing bracket
        // this includes special characters such as , and ; and keeps track of any nested brackets
        if(*tp == '(' || ((tokentype(*tp) & T_FUN) && !expect_cmd)) {
            int x;
            x = (getclosebracket(tp) - tp) + 1;
            memcpy(op, tp, x);
            op += x; tp += x;
            continue;
        }

        // if quote mark (") copy everything until the closing quote
        // this includes special characters such as , and ;
        // the tokenise() function will have ensured that the closing quote is always there
        if(*tp == '"') {
            do {
                *op++ = *tp++;
                if(*tp == 0) error("Syntax");
            } while(*tp != '"');
            *op++ = *tp++;
            continue;
        }

        // anything else is just copied into the argument
        *op++ = *tp++;
        if(expect_cmd)*op++ = *tp++; //copy rest of command token
        expect_cmd = false;
    }
    if(expect_bracket && *tp != ')') error("Syntax");
    while(op - 1 > argbuf && *(op-1) == ' ') --op;                  // trim any trailing spaces on the last argument
    *op = 0;                                                        // terminate the last argument
}


static void MIPS16 display_string(const char *s, bool fill) {
    // Indent each line by one space.
    if (CurrentX == 0) DisplayPutC(' ');

    // Display characters one at a time,
    for (const char *p = s; *p; ++p) {
        if (CurrentX + gui_font_width >= HRes) {
            DisplayPutC(' '); // Leave one space at the end of each line and wrap to the next.
            DisplayPutC(' '); // Indent each new line by one space.
            if (*p == ' ') continue; // Skip first space on each new line.
        }
        DisplayPutC(*p);
    }

    // Fill to the end of the line with spaces.
    if (fill) {
        while (CurrentX + gui_font_width <= HRes) DisplayPutC(' ');
        CurrentX = 0;
        CurrentY += gui_font_height;
    }
}

/**
 * Displays an error message with context on the display.
 *
 * @param  line_num   The error line,
 *                    -1 for the LIBRARY,
 *                    -2 when error occurs at the prompt.
 * @param  line_txt   The text of the line that caused the error.
 * @param  error_msg  The error message.
 */
void MIPS16 LCD_error(int line_num, const char *line_txt, const char* error_msg) {
    if (HRes == 0) return; // No display configured.

    // Always write error to the actual display.
    restorepanel();

    // Store current property display values.
    const unsigned char old_console = Option.DISPLAY_CONSOLE;
    const int old_font = gui_font;
    const int old_fcolour = gui_fcolour;
    const int old_bcolour = gui_bcolour;

    // Override properties required by DisplayPutC.
    const int font = 1;
    Option.DISPLAY_CONSOLE = 1;
    SetFont(font);
    gui_fcolour = 0xEE4B2B; // Bright Red.
    gui_bcolour = 0x0;

    // Display the error message halfway down the display (approx.)
    const int chars_per_line = (HRes / gui_font_width) - 2;
    int num_lines = 2;
    num_lines += strlen(error_msg) / chars_per_line;
    if (strlen(error_msg) % chars_per_line > 0) num_lines++;
    num_lines += strlen(line_txt) / chars_per_line;
    if (strlen(line_txt) % chars_per_line > 0) num_lines++;
    CurrentX = 0;
    CurrentY = (VRes / 2) - (num_lines * gui_font_height / 2);

    display_string("", true);
    display_string("ERROR: ", false);
    display_string(error_msg, true);
    if (*line_txt) {
        char buf[32];
        if (line_num == -1) {
            sprintf(buf, "[LIBRARY] ");
        } else {
            sprintf(buf, "[%d] ", line_num);
        }
        display_string(buf, false);
        display_string(line_txt, true);
    }
    display_string("", true);

    // Restore display property values.
    SetFont(old_font);
    PromptFont=old_font;
    Option.DISPLAY_CONSOLE = old_console;
    gui_fcolour = old_fcolour;
    gui_bcolour = old_bcolour;
}
// throw an error
// displays the error message and aborts the program
// the message can contain variable text which is indicated by a special character in the message string
//  $ = insert a string at this place
//  @ = insert a character
//  % = insert a number
// the optional data to be inserted is the second argument to this function
// this uses longjump to skip back to the command input and cleanup the stack
void MIPS16 error(char *msg, ...) {
    char *p, *tp, tstr[STRINGSIZE * 2];
    va_list ap;
    ScrewUpTimer = 0;
    // first build the error message in the global string MMErrMsg
    if(MMerrno == 0) MMerrno = 16;                                  // indicate an error
    memset(tstr, 0, STRINGSIZE * 2);                                 // clear any previous string
    if(*msg) {
        va_start(ap, msg);
        while(*msg) {
            tp = &tstr[strlen(tstr)];                               // point to the end of the string
            if(*msg == '$')                                         // insert a string
                strcpy(tp, va_arg(ap, char *));
            else if(*msg == '@')                                    // insert a character
                *tp = (va_arg(ap, int));
            else if(*msg == '%')                                    // insert an integer
                IntToStr(tp, va_arg(ap, int), 10);
            else if(*msg == '~')                                    // insert a long long integer
                IntToStr(tp, va_arg(ap, int64_t), 10);
            else if(*msg == '|')                                    // insert an integer
                strcpy(tp,PinDef[va_arg(ap, int)].pinname);
            else
                *tp = *msg;
            msg++;
        }
    }
    
    // copy the error message into the global MMErrMsg truncating at any tokens or if the string is too long
    for(p = MMErrMsg, tp = tstr; *tp < 127 && (tp - tstr) < MAXERRMSG - 1; ) *p++ = *tp++;
    *p = 0;
    if(optionlogging){
        lfs_file_t lfs_file;
        char crlf[]="\r\n";
        lfs_file_open(&lfs, &lfs_file, "log.txt", LFS_O_APPEND | LFS_O_CREAT);
        lfs_file_write(&lfs, &lfs_file, MMErrMsg, sizeof(MMErrMsg));
        lfs_file_write(&lfs, &lfs_file, crlf, sizeof(crlf));
        lfs_file_close(&lfs, &lfs_file);	
    }
    if(OptionErrorSkip) longjmp(ErrNext, 1);                       // if OPTION ERROR SKIP/IGNORE is in force
#ifdef PICOMITE
        multicore_fifo_push_blocking(0xFF);
        busy_wait_ms(mergetimer+200);
        if(mergerunning){
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
#endif

    LoadOptions();                                                  // make sure that the option struct is in a clean state
    OptionConsole=1;
    if(Option.DISPLAY_CONSOLE) {
        OptionConsole=3;
        #ifdef PICOMITEVGA
            WriteBuf=(unsigned char *)FRAMEBUFFER;
            DisplayBuf=(unsigned char *)FRAMEBUFFER;
        #else
            restorepanel();            
        #endif       // we now have CurrentLinePtr pointing to the start of the line
        SetFont(PromptFont);
        gui_fcolour = PromptFC;
        gui_bcolour = PromptBC;
        if((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE4 || DISPLAY_TYPE==SCREENMODE5) && gui_font_width>6){
            SetFont((6<<4) | 1) ;
            PromptFont=(6<<4) | 1;
        } else {
#ifdef HDMI
            if(((FullColour) || DISPLAY_TYPE==SCREENMODE3) && gui_font_width>8){
                SetFont(1) ;
                PromptFont = 1;
            } else if(gui_font_width>16){
                SetFont((2<<4) | 1) ;
                PromptFont=(2<<4) | 1;
            }
#else
            if(gui_font_width>8){
                SetFont(1) ;
                PromptFont = 1;
            }
#endif
        }
        if(CurrentX != 0) MMPrintString("\r\n");                   // error message should be on a new line
    }
    if(MMCharPos > 1) MMPrintString("\r\n");
    int line_num = -2;
    if(CurrentLinePtr) {
        tp = p = (char *)ProgMemory;
        if (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE && CurrentLinePtr < LibMemory+MAX_PROG_SIZE)
           tp = p = (char *)LibMemory;
        //if(*CurrentLinePtr != T_NEWLINE && CurrentLinePtr < ProgMemory + MAX_PROG_SIZE) {
        if(*CurrentLinePtr != T_NEWLINE && ((CurrentLinePtr < ProgMemory + MAX_PROG_SIZE) || (Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE && CurrentLinePtr < LibMemory+MAX_PROG_SIZE))) {    
            // normally CurrentLinePtr points to a T_NEWLINE token but in this case it does not
            // so we have to search for the start of the line and set CurrentLinePtr to that
          while(*p != 0xff) {
              while(*p) p++;                                        // look for the zero marking the start of an element
              if(p >= (char *)CurrentLinePtr || p[1] == 0) {                // the previous line was the one that we wanted
                  CurrentLinePtr = (unsigned char *)tp;
                  break;
                }
              if(p[1] == T_NEWLINE) {
                  tp = ++p;                                         // save because it might be the line we want
              }
         		p++;                                                // step over the zero marking the start of the element
        		skipspace(p);
        		if(p[0] == T_LABEL) p += p[1] + 2;					// skip over the label
            }
        }

       // we now have CurrentLinePtr pointing to the start of the line
//        dump(CurrentLinePtr, 80);
        llist(tknbuf, CurrentLinePtr);
        p = (char *) tknbuf;
        skipspace(p);
        if(CurrentLinePtr >= ProgMemory && CurrentLinePtr < ProgMemory + MAX_PROG_SIZE){
            line_num = CountLines(CurrentLinePtr);
            StartEditPoint = CurrentLinePtr;
            StartEditChar = 0;
        } else {
            line_num = -1;
        }
    }

    // Print the line.
    if (line_num != -2) {
        if (line_num == -1) {
            sprintf(tstr, "[LIBRARY] %s\r\n", p);
        } else {
            sprintf(tstr, "[%d] %s\r\n", line_num, p);
        }
        MMPrintString(tstr);
    }

    // Print the error message.
    if (*MMErrMsg) {
        sprintf(tstr, "Error : %s\r\n", MMErrMsg);
    } else {
        sprintf(tstr, "Error");
    }
    MMPrintString(tstr);
    #ifndef PICOMITEVGA
    if (!Option.DISPLAY_CONSOLE && Option.DISPLAY_TYPE>I2C_PANEL) {
        int width=Option.Width;
        int height=Option.Height;
        LCD_error(line_num, p, MMErrMsg);
        Option.Width=width;
        Option.Height=height;
    }
    
    #endif
    cmdline=NULL;
	do_end(false);
	longjmp(mark, 1);												// jump back to the input prompt
}



/**********************************************************************************************
 Routines to convert floats and integers to formatted strings
 These replace the sprintf() libraries with much less flash usage
**********************************************************************************************/

#define IntToStrBufSize 65

// convert a integer to a string.
// sstr is a buffer where the chars are to be written to
// sum is the number to be converted
// base is the numbers base radix (10 = decimal, 16 = hex, etc)
// if base 10 the number will be signed otherwise it will be unsigned
void MIPS16 IntToStr(char *strr, long long int nbr, unsigned int base) {
    int i, negative;
    unsigned char digit;
    unsigned long long int sum;
    extern long long int llabs (long long int n);

    unsigned char str[IntToStrBufSize];

    if(nbr < 0 && base == 10) {                                     // we can have negative numbers in base 10 only
        nbr = llabs(nbr);
        negative = true;
    } else
        negative = false;

    // this generates the digits in reverse order
    sum = (unsigned long long int) nbr;
    i = 0;
    do {
        digit = sum % base;
        if (digit < 0xA)
            str[i++] = '0' + digit;
        else
            str[i++] = 'A' + digit - 0xA;
        sum /= base;
    } while (sum && i < IntToStrBufSize);

    if(negative) *strr++ = '-';

    // we now need to reverse the digits into their correct order
    for(i--; i >= 0; i--) *strr++ = str[i];
    *strr = 0;
}


// convert an integer to a string padded with a leading character
// p is a pointer to the destination
// nbr is the number to convert (can be signed in which case the number is preceeded by '-')
// padch is the leading padding char (usually a space)
// maxch is the desired width of the resultant string (incl padding chars)
// radix is the base of the number.  Base 10 is signed, all others are unsigned
// Special case (used by FloatToStr() only):
//     if padch is negative and nbr is zero prefix the number with the - sign
void MIPS16 IntToStrPad(char *p, long long int nbr, signed char padch, int maxch, int radix) {
    int j;
    char sign, buf[IntToStrBufSize];

    sign = 0;
    if ((nbr < 0 && radix == 10 && nbr!=0x8000000000000000) || padch < 0) {                      // if the number is negative or we are forced to use a - symbol
        sign = '-';                                                 // set the sign
        nbr *= -1;                                                  // convert to a positive nbr
        padch = abs(padch);
    } else {
        if(nbr >= 0 && maxch < 0 && radix == 10)                    // should we display the + sign?
            sign = '+';
    }

    IntToStr(buf, nbr, radix);
    j = abs(maxch) - strlen(buf);                                   // calc padding required
    if(j <= 0)j = 0;
    else memset(p, padch, abs(maxch));                               // fill the buffer with the padding char
	if(sign != 0) {                                                 // if we need a sign
		if(j == 0) j = 1;                                           // make space if necessary
	    if(padch == '0')
	        p[0] = sign;                                            // for 0 padding the sign is before the padding
	    else
	        p[j - 1] = sign;                                        // for anything else the padding is before the sign
	 }
    strcpy(&p[j], buf) ;
}


// convert a float to a string including scientific notation if necessary
// p is the buffer to store the string
// f is the number
// m is the nbr of chars before the decimal point (if negative print the + sign)
// n is the nbr chars after the point
//     if n == STR_AUTO_PRECISION we should automatically determine the precision
//     if n is negative always use exponential format
// ch is the leading pad char
void MIPS16 FloatToStr(char *p, MMFLOAT f, int m, int n, unsigned char ch) {
    int exp, trim = false, digit;
    MMFLOAT rounding;
    char *pp;
    if(f==INFINITY){
        strcpy(p,"INF");
        return;
    }
    ch &= 0x7f;                                                     // make sure that ch is an ASCII char
    if(f == 0)
        exp = 0;
    else
        exp = floor(log10(fabs(f)));                             // get the exponent part
    if(((fabs(f) < 0.0001 || fabs(f) >= 1000000) && f != 0 && (n == STR_AUTO_PRECISION || n==STR_FLOAT_PRECISION)) || n < 0) {
        // we must use scientific notation
        f /= pow(10, exp);                                         // scale the number to 1.2345
        if(f >= 10) { f /= 10; exp++; }
        if(n < 0) n = -n;                                           // negative indicates always use exponantial format
        FloatToStr(p, f, m, n, ch);                                 // recursively call ourself to convert that to a string
        p = p + strlen(p);
        *p++ = 'e';                                                 // add the exponent
        if(exp >= 0) {
            *p++ = '+';
            IntToStrPad(p, exp, '0', 2, 10);                        // add a positive exponent
        } else {
            *p++ = '-';
            IntToStrPad(p, exp * -1, '0', 2, 10);                   // add a negative exponent
        }
    } else {
        // we can treat it as a normal number

        // first figure out how many decimal places we want.
        // n == STR_AUTO_PRECISION means that we should automatically determine the precision
        if(n == STR_AUTO_PRECISION) {
            trim = true;
            n = STR_SIG_DIGITS - exp;
            if(n < 0) n = 0;
        }
        if(n == STR_FLOAT_PRECISION) {
            trim = true;
            n = STR_FLOAT_DIGITS - exp;
            if(n < 0) n = 0;
        }

 
        // calculate rounding to hide the vagaries of floating point
        if(n > 0)
            rounding = 0.5/pow(10, n);
        else
            rounding = 0.5;
        if(f > 0) f += rounding;                                    // add rounding for positive numbers
        if(f < 0) f -= rounding;                                    // add rounding for negative numbers

        // convert the digits before the decimal point
        if((int)f == 0 && f < 0)
            IntToStrPad(p, 0, -ch, m, 10);                          // convert -0 incl padding if necessary
        else
            IntToStrPad(p, f, ch, m, 10);                           // convert the integer incl padding if necessary
        p += strlen(p);                                             // point to the end of the integer
        pp = p;

        // convert the digits after the decimal point
        if(f < 0) f = -f;                                           // make the number positive
        if(n > 0) {                                                 // if we need to have a decimal point and following digits
            *pp++ = '.';                                            // add the decimal point
            f -= floor(f);                                         // get just the fractional part
            while(n--) {
                f *= 10;
                digit = floor(f);                                  // get the next digit for the string
                f -= digit;
                *pp++ = digit + '0';
            }

            // if we do not have a fixed number of decimal places step backwards removing trailing zeros and the decimal point if necessary
            while(trim && pp > p) {
                pp--;
                if(*pp == '.') break;
                if(*pp != '0') { pp++; break; }
            }
        }
        *pp = 0;
    }
}



/**********************************************************************************************
Various routines to clear memory or the interpreter's state
**********************************************************************************************/


// clear (or delete) variables
// if level is not zero it will only delete local variables at that level or greater
// if level is zero to will delete all variables and reset global settings
void MIPS16 __not_in_flash_func(ClearVars)(int level, bool all) {
   int i, newhashpointer,hashcurrent,hashnext;

    // first step through the variable table and delete local variables at that level or greater
	if(level){
		newhashpointer=g_hashlistpointer; //save the current number of stored values
		for(i=g_hashlistpointer-1;i>=0;i--){ //delete in reverse order of creation
			if(g_hashlist[i].level>= level){
				hashnext = hashcurrent = g_hashlist[i].hash;
				hashnext++;
				hashnext %= MAXVARS/2;
				if(((g_vartbl[hashcurrent].type & T_STR) || g_vartbl[hashcurrent].dims[0] != 0) && !(g_vartbl[hashcurrent].type & T_PTR) && ((uint32_t)g_vartbl[hashcurrent].val.s<(uint32_t)MMHeap + heap_memory_size)&& ((uint32_t)g_vartbl[hashcurrent].val.s>(uint32_t)MMHeap)) {
					FreeMemorySafe((void **)&g_vartbl[hashcurrent].val.s);
					// free any memory (if allocated)
				}
//				MMPrintString("Deleting ");MMPrintString(g_vartbl[g_hashlist[i].hash].name);PIntComma(g_hashlist[i].level);PIntComma(g_hashlist[i].hash);PRet();
				g_hashlist[i].level=-1;
				newhashpointer=i; //set the new highest index
				memset(&g_vartbl[hashcurrent],0,sizeof(struct s_vartbl));
				if(g_vartbl[hashnext].type){
					g_vartbl[hashcurrent].type = T_BLOCKED ;                                // block slot
					g_vartbl[hashcurrent].name[0] = '~';                                      // safety precaution
				}
				g_Localvarcnt--;
			}
		}
		g_hashlistpointer=newhashpointer;
	} else {
		for(i = 0; i < MAXVARS; i++) {
			if(((g_vartbl[i].type & T_STR) || g_vartbl[i].dims[0] != 0) && !(g_vartbl[i].type & T_PTR)) {
				if((uint32_t)g_vartbl[i].val.s>(uint32_t)MMHeap && (uint32_t)g_vartbl[i].val.s<(uint32_t)MMHeap + heap_memory_size){
                    FreeMemorySafe((void **)&g_vartbl[i].val.s);                        // free any memory (if allocated)
                }
            }
#ifdef rp2350
#ifndef PICOMITEWEB
            if(all){
                if(((g_vartbl[i].type & T_STR) || g_vartbl[i].dims[0] != 0) && !(g_vartbl[i].type & T_PTR)) {
                    if((uint32_t)g_vartbl[i].val.s>(uint32_t)PSRAMbase && (uint32_t)g_vartbl[i].val.s<(uint32_t)PSRAMbase + PSRAMsize){
                        FreeMemorySafe((void **)&g_vartbl[i].val.s);                        // free any memory (if allocated)
                    }
                }
            }
#endif
#endif
			memset(&g_vartbl[i],0,sizeof(struct s_vartbl));
		}
	}
   // then step through the for...next table and remove any loops at the level or greater
    for(i = 0; i < g_forindex; i++) {
        if(g_forstack[i].level >= level) {
            g_forindex = i;
            break;
        }
    }

    // also step through the do...loop table and remove any loops at the level or greater
    for(i = 0; i < g_doindex; i++) {
        if(g_dostack[i].level >= level) {
            g_doindex = i;
            break;
        }
    }

    if(level != 0) return;

    g_forindex = g_doindex = 0;
    g_LocalIndex = 0;                                                 // signal that all space is to be cleared
    ClearTempMemory();                                              // clear temp string space
    // we can now delete all variables by zeroing the counters
    g_Localvarcnt = 0;
    g_Globalvarcnt = 0;
    g_OptionBase = 0;
    g_DimUsed = false;
    g_hashlistpointer=0;
}


// clear all stack pointers (eg, FOR/NEXT stack, DO/LOOP stack, GOSUB stack, etc)
// this is done at the command prompt or at any break
void  MIPS16 ClearStack(void) {
    NextData = 0;
	NextDataLine = ProgMemory;
    g_forindex = 0;
    g_doindex = 0;
    gosubindex = 0;
    g_LocalIndex = 0;
    g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
    InterruptReturn = NULL;
}


// clear the runtime (eg, variables, external I/O, etc) includes ClearStack() and ClearVars()
// this is done before running a program
void MIPS16 ClearRuntime(bool all) {
    int i;
#ifdef PICOMITEWEB
    if(TCPstate){
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        for(int i=0 ; i<MaxPcb ; i++){
            if(state->client_pcb[i] && state->telnet_pcb_no!=i)tcp_server_close(state, i);
            if(state->buffer_recv[i])FreeMemorySafe((void **)&state->buffer_recv[i]);
            state->inttrig[i]=0;
            state->sent_len[i]=0;
            state->recv_len[i]=0;
            state->to_send[i]=0;
        }
    }
    optionsuppressstatus=0;
#endif
    CloseAllFiles();
    ClearExternalIO();                                              // this MUST come before InitHeap(true)
    ClearStack();
#ifdef USBKEYBOARD
	clearrepeat();
#endif	
    OptionExplicit = false;
    OptionEscape = false;
    OptionConsole=3;
    DefaultType = T_NBR;
    ds18b20Timers = NULL;                                           // InitHeap(true) will recover the memory allocated to this array
    findlabel(NULL);                                                // clear the label cache
    OptionErrorSkip = 0;
	optionangle=1.0;
    optionfulltime=false;
    optionfastaudio=0;
    optionlogging=false;
#ifndef PICOMITEVGA
    if(SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16 || SPI480)clear320();
#endif
    MMerrno = 0;                                                    // clear the error flags
   *MMErrMsg = 0;
    InitHeap(true);
    m_alloc(all? M_VAR : M_LIMITED);
    ClearVars(0,true);
    memset(datastore, 0, sizeof(struct sa_data) * MAXRESTORE);
    restorepointer = 0;
    g_varcnt = 0;
    CurrentLinePtr = ContinuePoint = NULL;
    for(i = 0;  i < MAXSUBFUN; i++)  subfun[i] = NULL;
#ifdef GUICONTROLS
    for(i = 1; i < Option.MaxCtrls; i++) {
        memset(&Ctrl[i],0,sizeof(struct s_ctrl));
        Ctrl[i].state = Ctrl[i].type = 0;
        Ctrl[i].s = NULL;
    }
#endif
}



// clear everything including program memory (includes ClearStack() and ClearRuntime(true))
// this is used before loading a program
void MIPS16 ClearProgram(void) {
//    InitHeap(true);
    initFonts();
    m_alloc(M_PROG);                                           // init the variables for program memory
    if(Option.DISPLAY_TYPE>=VIRTUAL && WriteBuf)FreeMemorySafe((void **)&WriteBuf);
    ClearRuntime(true);
//    ProgMemory[0] = ProgMemory[1] = ProgMemory[3] = ProgMemory[4] = 0;
    PSize = 0;
    StartEditPoint = NULL;
    StartEditChar= 0;
    ProgramChanged = false;
    TraceOn = false;
}




// round a float to an integer
#ifdef rp2350
int  __not_in_flash_func(FloatToInt32)(MMFLOAT x) {
#else
#ifdef PICOMITEVGA
int FloatToInt32(MMFLOAT x) {
#else
int  __not_in_flash_func(FloatToInt32)(MMFLOAT x) {
#endif
#endif
    if(x < LONG_MIN - 0.5 || x > LONG_MAX + 0.5)
        error("Number too large");
    return (x >= 0 ? (int)(x + 0.5) : (int)(x - 0.5)) ;
}


#ifdef rp2350
long long int  __not_in_flash_func(FloatToInt64)(MMFLOAT x) {
#else
#ifdef PICOMITEVGA
long long int FloatToInt64(MMFLOAT x) {
#else
long long int  __not_in_flash_func(FloatToInt64)(MMFLOAT x) {
#endif
#endif
    if(x < (-(0x7fffffffffffffffLL) -1) - 0.5 || x > 0x7fffffffffffffffLL + 0.5)
        error("Number too large");
    if ((x < -0xfffffffffffff) || (x > 0xfffffffffffff))
        return (long long int)(x);
    else
    return (x >= 0 ? (long long int )(x + 0.5) : (long long int )(x - 0.5)) ;
}



// make a string uppercase
void __not_in_flash_func(makeupper)(unsigned char *p) {
    while(*p) {
        *p = mytoupper(*p);
        p++;
    }
}


// find the value of a command token given its name
int GetCommandValue( unsigned char *n) {
    int i;
    for(i = 0; i < CommandTableSize - 1; i++)
        if(str_equal(n, commandtbl[i].name))
            return i;
    error("Invalid statement in Type definition");
    return 0;
}



// find the value of a token given its name
int GetTokenValue (unsigned char *n) {
    int i;
    for(i = 0; i < TokenTableSize - 1; i++)
        if(str_equal(n, tokentbl[i].name))
            return i + C_BASETOKEN;
    error("Internal fault 4(sorry)");
    return 0;
}



// skip to the end of a variable
unsigned char MIPS16 __not_in_flash_func(*skipvar)(unsigned char *p, int noerror) {
    unsigned char *pp, *tp;
    int i;
    int inquote = false;

    tp = p;
    // check the first char for a legal variable name
    skipspace(p);
    if(!isnamestart(*p)) return tp;

    do {
        p++;
    } while(isnamechar(*p));

    // check the terminating char.
    if(*p == '$' || *p == '%' || *p == '!') p++;

    if(p - tp > MAXVARLEN) {
        if(noerror) return p;
        error("Variable name too long");
    }

    pp = p; skipspace(pp); if(*pp == (unsigned char)'(') p = pp;
    if(*p == '(') {
        // this is an array

        p++;
        if(p - tp > MAXVARLEN) {
            if(noerror) return p;
            error("Variable name too long");
        }

        // step over the parameters keeping track of nested brackets
        i = 1;
        while(1) {
            if(*p == '\"') inquote = !inquote;
            if(*p == 0) {
                if(noerror) return p;
                error("Expected closing bracket");
            }
            if(!inquote) {
                if(*p == ')') if(--i == 0) break;
                if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
            }
            p++;
        }
        p++;        // step over the closing bracket
    }
    return p;
}



// skip to the end of an expression (terminates on null, comma, comment or unpaired ')'
unsigned char __not_in_flash_func(*skipexpression)(unsigned char *p) {
    int i, inquote;

    for(i = inquote = 0; *p; p++) {
        if(*p == '\"') inquote = !inquote;
        if(!inquote) {
            if(*p == ')') i--;
            if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
        }
        if(i < 0 || (i == 0 && (*p == ',' || *p == '\''))) break;
    }
    return p;
}


// find the next command in the program
// this contains the logic for stepping over a line number and label (if present)
// p is the current place in the program to start the search from
// CLine is a pointer to a char pointer which in turn points to the start of the current line for error reporting (if NULL it will be ignored)
// EOFMsg is the error message to use if the end of the program is reached
// returns a pointer to the next command
unsigned char __not_in_flash_func(*GetNextCommand)(unsigned char *p, unsigned char **CLine, unsigned char *EOFMsg) {
    do {
        if(*p != T_NEWLINE) {                                       // if we are not already at the start of a line
            while(*p) p++;                                          // look for the zero marking the start of an element
            p++;                                                    // step over the zero
        }
        if(*p == 0) {
            if(EOFMsg == NULL) return p;
            error((char *)EOFMsg);
        }
        if(*p == T_NEWLINE) {
            if(CLine) *CLine = p;                                   // and a pointer to the line also for error reporting
            p++;
        }
        if(*p == T_LINENBR) p += 3;

        skipspace(p);
        if(p[0] == T_LABEL) {                                       // got a label
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }
    } while(*p < C_BASETOKEN);
    return p;
}


// scans text looking for the matching closing bracket
// it will handle nested strings, brackets and functions
// it expects to be called pointing at the opening bracket or a function token
unsigned char __not_in_flash_func(*getclosebracket)(unsigned char *p) {
    int i = 0;
    int inquote = false;

    do {
        if(*p == 0) error("Expected closing bracket");
        if(*p == '\"') inquote = !inquote;
        if(!inquote) {
            if(*p == ')') i--;
            if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
        }
        p++;
    } while(i);
    return p - 1;
}


// check that there is no excess text following an element
// will skip spaces and abort if a zero char is not found
void __not_in_flash_func(checkend)(unsigned char *p) {
    skipspace(p);
    if(*p == '\'') return;
    if(*p)
        error("Unexpected text: $", p);
}


// check if the next text in an element (a basic statement) corresponds to an alpha string
// leading whitespace is skipped and the string must be terminated with a valid terminating
// character. Returns a pointer to the end of the string if found or NULL is not
unsigned char __not_in_flash_func(*checkstring)(unsigned char *p, unsigned char *tkn) {
    skipspace(p);                                           // skip leading spaces
    while(*tkn && (mytoupper(*tkn) == mytoupper(*p))) { tkn++; p++; }   // compare the strings
//    if(*tkn == 0 && (*p == (unsigned char)' ' || *p == (unsigned char)',' || *p == (unsigned char)'\'' || *p == 0 || *p == (unsigned char)'('  || *p == (unsigned char)'=')) {
    if(*tkn == 0 && !isnamechar(*p)){
        skipspace(p);
        return p;                                                   // if successful return a pointer to the next non space character after the matched string
    }
    return NULL;                                                    // or NULL if not
}

/********************************************************************************************************************************************
A couple of I/O routines that do not belong anywhere else
*********************************************************************************************************************************************/





/********************************************************************************************************************************************
 string routines
 these routines form a library of functions for manipulating MMBasic strings.  These strings differ from ordinary C strings in that the length
 of the string is stored in the first byte and the string is NOT terminated with a zero valued byte.  This type of string can store the full
 range of binary values (0x00 to 0xff) in each character.
*********************************************************************************************************************************************/

// convert a MMBasic string to a C style string
// if the MMstr contains a null byte that byte is skipped and not copied
unsigned char __not_in_flash_func(*MtoC)(unsigned char *p) {
    int i;
    unsigned char *p1, *p2;
    i = *p;
    p1 = p + 1; p2 = p;
    while(i) {
        if(p1) *p2++ = *p1;
        p1++;
        i--;
    }
    *p2 = 0;
    return p;
}


// convert a c style string to a MMBasic string
unsigned char __not_in_flash_func(*CtoM)(unsigned char *p) {
    int len, i;
    unsigned char *p1, *p2;
    len = i = strlen((char *)p);
    if(len > MAXSTRLEN) error("String too long");
    p1 = p + len; p2 = p + len - 1;
    while(i--) *p1-- = *p2--;
    *p = len;
    return p;
}


// copy a MMBasic string to a new location
#ifdef rp2350
void __not_in_flash_func(Mstrcpy)(unsigned char *dest, unsigned char *src) {
 #else
#ifdef PICOMITEVGA
void Mstrcpy(unsigned char *dest, unsigned char *src) {
 #else
void __not_in_flash_func(Mstrcpy)(unsigned char *dest, unsigned char *src) {
#endif
#endif
   int i;
    i = *src + 1;
    while(i--) *dest++ = *src++;
}



// concatenate two MMBasic strings
void Mstrcat(unsigned char *dest, unsigned char *src) {
    int i;
    i = *src;
    *dest += i;
    dest += *dest + 1 - i; src++;
    while(i--) *dest++ = *src++;
}



// compare two MMBasic style strings
// returns 1 if s1 > s2  or  0 if s1 = s2  or  -1 if s1 < s2
int Mstrcmp(unsigned char *s1, unsigned char *s2) {
    register int i;
    register unsigned char *p1, *p2;

    // get the smaller length
    i = *s1 < *s2 ? *s1 : *s2;

    // skip the length byte and point to the unsigned char array
    p1 = s1 + 1; p2 = s2 + 1;

    // compare each char
    while(i--) {
        if(*p1 > *p2) return 1;
        if(*p1 < *p2) return -1;
        p1++; p2++;
    }
    // up to this point the strings matched - make the decision based on which one is shorter
    if(*s1 > *s2) return 1;
    if(*s1 < *s2) return -1;
    return 0;
}



////////////////////////////////////////////////////////////////////////////////////////////////////
// these library functions went missing in the PIC32 C compiler ver 1.12 and later
////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * mystrncasecmp.c --
 *
 *  Source code for the "mystrncasecmp" library routine.
 *
 * Copyright (c) 1988-1993 The Regents of the University of California.
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: mystrncasecmp.c,v 1.3 2007/04/16 13:36:34 dkf Exp $
 */

/*
 * This array is designed for mapping upper and lower case letter together for
 * a case independent comparison. The mappings are based upon ASCII character
 * sequences.
 */

static unsigned char charmap[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xe1, 0xe2, 0xe3, 0xe4, 0xc5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};


/*
 *----------------------------------------------------------------------
 *
 * mystrncasecmp --
 *
 *  Compares two strings, ignoring case differences.
 *
 * Results:
 *  Compares up to length chars of s1 and s2, returning -1, 0, or 1 if s1
 *  is lexicographically less than, equal to, or greater than s2 over
 *  those characters.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */


int mystrncasecmp(
    const unsigned char *s1,         /* First string. */
    const unsigned char *s2,         /* Second string. */
    size_t  length)      /* Maximum number of characters to compare
                         * (stop earlier if the end of either string
                         * is reached). */
{
    register unsigned char u1, u2;

    for (; length != 0; length--, s1++, s2++) {
        u1 = (unsigned char) *s1;
        u2 = (unsigned char) *s2;
        if (charmap[u1] != charmap[u2]) {
            return charmap[u1] - charmap[u2];
        }
        if (u1 == '\0') {
            return 0;
        }
    }
    return 0;
}



// Compare two strings, ignoring case differences.
// Returns true if the strings are equal (ignoring case) otherwise returns false.
#if defined(__PIC32MX__)
inline
#endif
int __not_in_flash_func(str_equal)(const unsigned char *s1, const unsigned char *s2) {
    if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    for ( ; ; ) {
        if(*s2 == '\0') return 1;
        s1++; s2++;
        if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    }
    return 0;
}


// Compare two areas of memory, ignoring case differences.
// Returns true if they are equal (ignoring case) otherwise returns false.
int __not_in_flash_func(mem_equal)(unsigned char *s1, unsigned char *s2, int i) {
    if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    while (--i) {
        if(charmap[*(unsigned char *)++s1] != charmap[*(unsigned char *)++s2])
            return 0;
    }
    return 1;
}
/*  @endcond */
