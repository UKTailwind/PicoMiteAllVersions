/***********************************************************************************************************************
PicoMite MMBasic

* @file commands.c

<COPYRIGHT HOLDERS>  @author Geoff Graham, Peter Mather
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
* @file Commands.c
* @author Geoff Graham, Peter Mather
* @brief Source for standard MMBasic commands
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */


#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/flash.h"
#include "hardware/dma.h"
#include "hardware/structs/watchdog.h"
#ifdef PICOMITE
#include "pico/multicore.h"
#endif
#define overlap (VRes % (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) ? 0 : 1)
#include <math.h>
void flist(int, int, int);
//void clearprog(void);
char *KeyInterrupt=NULL;
unsigned char* SaveNextDataLine = NULL;
void execute_one_command(unsigned char *p);
void ListNewLine(int *ListCnt, int all);
int printWrappedText(const char *text, int screenWidth, int listcnt, int all) ;
char MMErrMsg[MAXERRMSG];                                           // the error message
volatile bool Keycomplete=false;
int keyselect=0;
extern volatile unsigned int ScrewUpTimer;
int SaveNextData = 0;
struct sa_data datastore[MAXRESTORE];
int restorepointer = 0;
uint64_t g_flag=0;
const uint8_t pinlist[]={ //this is a Basic program to print out the status of all the pins
	1,132,128,95,113,37,0,
	1,153,128,95,113,37,144,48,32,204,32,241,109,97,120,32,103,112,41,0,
	1,168,128,34,71,80,34,130,186,95,113,37,41,44,32,241,112,105,110,110,111,32,34,71,80,34,130,186,
	95,113,37,41,41,44,241,112,105,110,32,241,112,105,110,110,111,32,34,71,80,34,130,186,95,113,37,41,41,41,0,
	1,166,128,0,
    1,147,128,95,113,37,0,0
};
const uint8_t i2clist[]={ //this is a Basic program to print out the I2C devices connected to the SYSTEM I2C pins
  1, 132, 128, 105, 110, 116, 101, 103, 101, 114, 32, 95, 97, 100, 0,
  1, 132, 128, 105, 110, 116, 101, 103, 101, 114, 32, 120, 95, 44, 121, 95, 0, 
  1, 168, 128, 34, 32, 72, 69, 88, 32, 32, 48, 32, 32, 49, 32, 32, 50, 32, 32, 51, 32, 32, 52, 32, 32, 53, 32, 32, 54, 32, 32, 55, 32, 32, 
  56, 32, 32, 57, 32, 32, 65, 32, 32, 66, 32, 32, 67, 32, 32, 68, 32, 32, 69, 32, 32, 70, 34, 0, 
  1, 153, 128, 121, 95, 32, 144, 32, 48, 32, 204, 32, 55, 0, 
  1, 168, 128, 34, 32, 34, 59, 32, 164, 121, 95, 44, 32, 49, 41, 59, 32, 34, 48, 58, 32, 34, 59, 0, 
  1, 153, 128, 120, 95, 32, 144, 32, 48, 32, 204, 32, 49, 53, 0, 
  1, 161, 128, 95, 97, 100, 32, 144, 32, 121, 95, 32, 133, 32, 49, 54, 32, 130, 32, 120, 95, 0, 
  1, 158, 128, 241, 83, 89, 83, 84, 69, 77, 32, 73, 50, 67, 41, 144, 34, 73, 50, 67, 34, 32, 203, 32, 228, 128, 99, 104, 
  101, 99, 107, 32, 95, 97, 100, 32, 199, 32, 229, 128, 32, 99, 104, 101, 99, 107, 32, 95, 97, 100, 0, 
  1, 158, 128, 243, 68, 41, 32, 144, 32, 48, 32, 203, 0, 
  1, 158, 128, 95, 97, 100, 32, 144, 32, 48, 32, 203, 32, 168, 128, 34, 45, 45, 32, 34, 59, 0, 
  1, 158, 128, 95, 97, 100, 32, 143, 32, 48, 32, 203, 32, 168, 128, 164, 95, 97, 100, 44, 32, 50, 41, 59, 34, 32, 34, 59, 0, 
  1, 139, 128, 0, 1, 168, 128, 34, 45, 45, 32, 34, 59, 0, 1, 143, 128, 0, 1, 166, 128, 120, 95, 0, 
  1, 168, 128, 0, 1, 166, 128, 121, 95, 0, 1, 147, 128, 120, 95, 44, 121, 95, 0,
  1, 147, 128, 95, 97, 100, 0,0
 };
// stack to keep track of nested FOR/NEXT loops
struct s_forstack g_forstack[MAXFORLOOPS + 1];
int g_forindex;



// stack to keep track of nested DO/LOOP loops
struct s_dostack g_dostack[MAXDOLOOPS];
int g_doindex;                                // counts the number of nested DO/LOOP loops


// stack to keep track of GOSUBs, SUBs and FUNCTIONs
unsigned char *gosubstack[MAXGOSUB];
unsigned char *errorstack[MAXGOSUB];
int gosubindex;

unsigned char g_DimUsed = false;						// used to catch OPTION BASE after DIM has been used

int TraceOn;                                // used to track the state of TRON/TROFF
unsigned char *TraceBuff[TRACE_BUFF_SIZE];
int TraceBuffIndex;                       // used for listing the contents of the trace buffer
int OptionErrorSkip;                                               // how to handle an error
int MMerrno;                                                        // the error number
unsigned char cmdlinebuff[STRINGSIZE];
const unsigned int CaseOption = 0xffffffff;	// used to store the case of the listed output

static inline CommandToken commandtbl_decode(const unsigned char *p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}

void __not_in_flash_func(cmd_null)(void) {
	// do nothing (this is just a placeholder for commands that have no action)
}
/** @endcond */
/** 
 * This command increments an integer or a float or concatenates two strings
 * @param a the integer, float or string to be changed
 * @param b OPTIONAL for integers and floats - defaults to 1. Otherwise the amount to increment the number or the string to concatenate
 */
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_inc)(void){
#else
#ifdef PICOMITEVGA
void MIPS16 cmd_inc(void){
#else
void MIPS16 __not_in_flash_func(cmd_inc)(void){
#endif
#endif
	unsigned char *p, *q;
    int vtype;
	getargs(&cmdline,3,(unsigned char *)",");
	if(argc==1){
		p = findvar(argv[0], V_FIND);
		if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
        vtype = TypeMask(g_vartbl[g_VarIndex].type);
        if(vtype & T_STR) error("Invalid variable");                // sanity check
		if(vtype & T_NBR)
            (*(MMFLOAT *)p) = (*(MMFLOAT *)p) + 1.0;
		else if(vtype & T_INT)*(long long int *)p = *(long long int *)p + 1;
		else error("Syntax");
	} else {
		p = findvar(argv[0], V_FIND);
		if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
        vtype = TypeMask(g_vartbl[g_VarIndex].type);
        if(vtype & T_STR){
			int size=g_vartbl[g_VarIndex].size;
        	q=getstring(argv[2]);
    		if(*p + *q > size) error("String too long");
			Mstrcat(p, q);
        } else if(vtype & T_NBR){
        	 (*(MMFLOAT *)p) = (*(MMFLOAT *)p)+getnumber(argv[2]);
        } else if(vtype & T_INT){
        	*(long long int *)p = *(long long int *)p+getinteger(argv[2]);
        } else error("syntax");
 	}
}
// the PRINT command
void cmd_print(void) {
	unsigned char *s, *p;
    unsigned char *ss;
	MMFLOAT f;
    long long int  i64;
	int i, t, fnbr;
	int docrlf;														// this is used to suppress the cr/lf if needed

	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)";,");				// this is a macro and must be the first executable stmt

//    s = 0; *s = 56;											    // for testing the exception handler

	docrlf = true;

	if(argc > 0 && *argv[0] == '#') {								// check if the first arg is a file number
		argv[0]++;
         if((*argv[0] == 'G') || (*argv[0] == 'g')){
            argv[0]++;
            if(!((*argv[0] == 'P') || (*argv[0] == 'p')))error("Syntax");
            argv[0]++;
            if(!((*argv[0] == 'S') || (*argv[0] == 's')))error("Syntax");
            if(!GPSchannel) error("GPS not activated");
            if(argc!=3) error("Only a single string parameter allowed");
            p = argv[2];
			t = T_NOTYPE;
			p = evaluate(p, &f, &i64, &s, &t, true);			// get the value and type of the argument
            ss=(unsigned char *)s;
            if(!(t & T_STR)) error("Only a single string parameter allowed");
            int i,xsum=0;
            if(ss[1]!='$' || ss[ss[0]]!='*')error("GPS command must start with dollar and end with star");
            for(i=1;i<=ss[0];i++){
                SerialPutchar(GPSchannel, s[i]);
                if(s[i]=='$')xsum=0;
                if(s[i]!='*')xsum ^=s[i];
            }
            i=xsum/16;
            i=i+'0';
            if(i>'9')i=i-'0'+'A';
            SerialPutchar(GPSchannel, i);
            i=xsum % 16;
            i=i+'0';
            if(i>'9')i=i-'0'+'A';
            SerialPutchar(GPSchannel, i);
            SerialPutchar(GPSchannel, 13);
            SerialPutchar(GPSchannel, 10);
            return;
        } else {
			fnbr = getinteger(argv[0]);									// get the number
			i = 1;
			if(argc >= 2 && *argv[1] == ',') i = 2;						// and set the next argument to be looked at
		}
	} else {
		fnbr = 0;													// no file number so default to the standard output
		i = 0;
	}

	for(; i < argc; i++) {											// step through the arguments
		if(*argv[i] == ',') {
			MMfputc('\t', fnbr);									// print a tab for a comma
			docrlf = false;                                         // a trailing comma should suppress CR/LF
		}
		else if(*argv[i] == ';') {
			docrlf = false;											// other than suppress cr/lf do nothing for a semicolon
		}
		else {														// we have a normal expression
			p = argv[i];
			while(*p) {
				t = T_NOTYPE;
				p = evaluate(p, &f, &i64, &s, &t, true);			// get the value and type of the argument
                if(t & T_NBR) {
                    *inpbuf = ' ';                                  // preload a space
                    FloatToStr((char *)inpbuf + ((f >= 0) ? 1:0), f, 0, STR_AUTO_PRECISION, (unsigned char)' ');// if positive output a space instead of the sign
					MMfputs((unsigned char *)CtoM(inpbuf), fnbr);					// convert to a MMBasic string and output
				} else if(t & T_INT) {
                    *inpbuf = ' ';                                  // preload a space
                    IntToStr((char *)inpbuf + ((i64 >= 0) ? 1:0), i64, 10); // if positive output a space instead of the sign
					MMfputs((unsigned char *)CtoM(inpbuf), fnbr);					// convert to a MMBasic string and output
				} else if(t & T_STR) {
					MMfputs((unsigned char *)s, fnbr);								// print if a string (s is a MMBasic string)
				} else error("Attempt to print reserved word");	
			}
			docrlf = true;
		}
	}
	if(docrlf) MMfputs((unsigned char *)"\2\r\n", fnbr);								// print the terminating cr/lf unless it has been suppressed
	if(PrintPixelMode!=0)SSPrintString("\033[m");
	PrintPixelMode=0;
}
void cmd_arrayset(void){
	array_set(cmdline);
}
void array_set(unsigned char *tp){
    MMFLOAT f;
    long long int i64;
    unsigned char *s;
	#ifdef rp2350
	int dims[MAXDIM]={0};
	#else
	short dims[MAXDIM]={0};
	#endif
	int i,t,copy,card1=1;
	unsigned char size=0;
	MMFLOAT *a1float=NULL;
	int64_t *a1int=NULL;
	unsigned char *a1str=NULL;
	getargs(&tp, 3,(unsigned char *)",");
	if(!(argc == 3)) error("Argument count");
	findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t=g_vartbl[g_VarIndex].type;
	evaluate(argv[0], &f, &i64, &s, &t, false);
	if(t & T_STR){
		card1=parsestringarray(argv[2],&a1str,2,0, dims, true, &size);
		copy=(int)size+1;
		memset(a1str,0,copy*card1);
		if(*s){
			for(i=0; i< card1;i++){
				Mstrcpy(&a1str[i*copy],s);
			}
		}
	} else {
		card1=parsenumberarray(argv[2],&a1float,&a1int,2,0, dims, true);
		if(t & T_STR) error("Syntax");

		if(a1float!=NULL){
			for(i=0; i< card1;i++)*a1float++ = ((t & T_INT) ? (MMFLOAT)i64 : f);
		} else {
			for(i=0; i< card1;i++)*a1int++ = ((t & T_INT) ? i64 : FloatToInt64(f));
		}
	}
}
void cmd_add(void){
	array_add(cmdline);
}

void array_add(unsigned char *tp){
    MMFLOAT f;
    long long int i64;
    unsigned char *s;
	#ifdef rp2350
	int dims[MAXDIM]={0};
	#else
	short dims[MAXDIM]={0};
	#endif
	int i,t,card1=1, card2=1;
	MMFLOAT *a1float=NULL,*a2float=NULL, scale;
	int64_t *a1int=NULL, *a2int=NULL;
	unsigned char *a1str=NULL, *a2str=NULL;
	getargs(&tp, 5,(unsigned char *)",");
	if(!(argc == 5)) error("Argument count");
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t=g_vartbl[g_VarIndex].type;
	if(t & T_STR){
		unsigned char size=0,size2=0;
		unsigned char *toadd;
		card1=parsestringarray(argv[0], &a1str, 1, 0,dims, false, &size);
		evaluate(argv[2], &f, &i64, &s, &t, false);
		if(!(t & T_STR)) error("Syntax");
		toadd=getstring(argv[2]);
		card2=parsestringarray(argv[4], &a2str, 3, 0,dims, true, &size2);
		if(card1 != card2)error("Array size mismatch");
		unsigned char *buff = GetTempMemory(STRINGSIZE);								// this will last for the life of the command
		int copy=size+1;
		int copy2=size2+1;
		for(i=0; i< card1;i++){
			unsigned char *sarg1=a1str+i*copy;
			unsigned char *sarg2=a2str+i*copy2;
			if(*sarg1 + *toadd > size2) error("String too long");
			Mstrcpy(buff, sarg1);
			Mstrcat(buff, toadd);
			Mstrcpy(sarg2,buff);
		}
	} else {
		card1=parsenumberarray(argv[0], &a1float, &a1int, 1, 0,dims, false);
		evaluate(argv[2], &f, &i64, &s, &t, false);
		if(t & T_STR) error("Syntax");
		scale=getnumber(argv[2]);
		card2=parsenumberarray(argv[4], &a2float, &a2int, 3, 0,dims, true);
		if(card1 != card2)error("Array size mismatch");
		if(scale!=0.0){
			if(a2float!=NULL && a1float!=NULL){
				for(i=0; i< card1;i++)*a2float++ = ((t & T_INT) ? (MMFLOAT)i64 : f) + (*a1float++);
			} else if(a2float!=NULL && a1float==NULL){
				for(i=0; i< card1;i++)(*a2float++) = ((t & T_INT) ? (MMFLOAT)i64 : f) + ((MMFLOAT)*a1int++);
			} else if(a2float==NULL && a1float!=NULL){
				for(i=0; i< card1;i++)(*a2int++) = FloatToInt64(((t & T_INT) ? i64 : FloatToInt64(f)) + (*a1float++));
			} else {
				for(i=0; i< card1;i++)(*a2int++) = ((t & T_INT) ? i64 : FloatToInt64(f)) + (*a1int++);
			}
		} else {
			if(a2float!=NULL && a1float!=NULL){
				for(i=0; i< card1;i++)*a2float++ = *a1float++;
			} else if(a2float!=NULL && a1float==NULL){
				for(i=0; i< card1;i++)(*a2float++) = ((MMFLOAT)*a1int++);
			} else if(a2float==NULL && a1float!=NULL){
				for(i=0; i< card1;i++)(*a2int++) = FloatToInt64(*a1float++);
			} else {
				for(i=0; i< card1;i++)*a2int++ = *a1int++;
			}
		}
	}
}
void cmd_insert(void){
	array_insert(cmdline);
}
void array_insert(unsigned char *tp){
	int i, j, t, start, increment, dim[MAXDIM], pos[MAXDIM],off[MAXDIM], dimcount=0, target=-1;
	int64_t *a1int=NULL,*a2int=NULL;
	MMFLOAT *afloat=NULL;
	unsigned char *a1str=NULL, *a2str=NULL;
	unsigned char size=0,size2=0;
#ifdef rp2350
    int dims[MAXDIM]={0}; 
#else
    short dims[MAXDIM]={0};
#endif
	getargs(&tp, 15,(unsigned char *)",");
	if(argc<7)error("Argument count");
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t=g_vartbl[g_VarIndex].type;
	if(t & T_STR){
		parsestringarray(argv[0],&a1str,1,0,dims, false,&size);
	} else {
		parsenumberarray(argv[0],&afloat,&a1int,1,0,dims, false);
		if(!a1int)a1int=(int64_t *)afloat;
	}
	if(dims[1]<=0)error("Argument 1 must be a 2D or more array");
	for(i=0;i<MAXDIM;i++){
		if(dims[i]-g_OptionBase>0){
			dimcount++;
			dim[i]=dims[i]-g_OptionBase;
		} else dim[i]=0;
	}
	if(((argc-1)/2-1)!=dimcount)error("Argument count");
	for(i=0; i<dimcount;i++ ){
		if(*argv[i*2 +2]) pos[i]=getint(argv[i*2 +2],g_OptionBase,dim[i]+g_OptionBase)-g_OptionBase;
		else {
			if(target!=-1)error("Only one index can be omitted");
			target=i;
			pos[i]=1;
		}
	}
	if(t & T_STR){
		parsestringarray(argv[i*2 +2],&a2str,i+1,1,dims, true,&size2);
	} else {
		parsenumberarray(argv[i*2 +2],&afloat,&a2int,i+1,1,dims,true);
		if(!a2int)a2int=(int64_t *)afloat;
	}
	if(target==-1)return;
	if(dim[target]+g_OptionBase!=dims[0])error("Size mismatch between insert and target array");
	if(size!=size2)error("String arrays differ in string length");
	i=dimcount-1;
	while(i>=0){
		off[i]=1;
		for(j=0; j<i; j++)off[i]*=(dim[j]+1);
		i--;
	}
	start=1;
	for(i=0;i<dimcount;i++){
		start+= (pos[i]*off[i]);
	}
	start--;
	increment=off[target];
	start-=increment;
	if(t & T_STR){
		int copy=(int)size+1;
		for(i=0;i<=dim[target];i++) {
			unsigned char *p=a2str+i*copy;
			unsigned char *q=&a1str[(start+i*increment)*copy];
			memcpy(q,p,copy);
		}
	} else	{
		for(i=0;i<=dim[target];i++) a1int[start+i*increment]=*a2int++;
	}
	return;

}
void cmd_slice(void){
	array_slice(cmdline);
}
void array_slice(unsigned char *tp){
	int i, j, t, start, increment, dim[MAXDIM], pos[MAXDIM],off[MAXDIM], dimcount=0, target=-1, toarray=0;
	int64_t *a1int=NULL,*a2int=NULL;
	MMFLOAT *afloat=NULL;
	unsigned char *a1str=NULL,*a2str=NULL;
	unsigned char size=0,size2=0;
#ifdef rp2350
    int dims[MAXDIM]={0};
#else
    short dims[MAXDIM]={0};
#endif
	getargs(&tp, 15,(unsigned char *)",");
	if(argc<7)error("Argument count");
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t=g_vartbl[g_VarIndex].type;
	if(t & T_STR){
		parsestringarray(argv[0],&a1str,1,0,dims, false, &size);
	} else {
		parsenumberarray(argv[0],&afloat,&a1int,1,0,dims, false);
		if(!a1int)a1int=(int64_t *)afloat;
	}
	if(dims[1]<=0)error("Argument 1 must be a 2D or more array");
	for(i=0;i<MAXDIM;i++){
		if(dims[i]-g_OptionBase>0){
			dimcount++;
			dim[i]=dims[i]-g_OptionBase;
		} else dim[i]=0;
	}
	if(((argc-1)/2-1)!=dimcount)error("Argument count");
	for(i=0; i<dimcount;i++ ){
		if(*argv[i*2 +2]) pos[i]=getint(argv[i*2 +2],g_OptionBase,dim[i]+g_OptionBase)-g_OptionBase;
		else {
			if(target!=-1)error("Only one index can be omitted");
			target=i;
			pos[i]=1;
		}
	}
	if(t & T_STR){
		toarray=parsestringarray(argv[i*2 +2],&a2str,i+1,1,dims, true, &size2)-1;
	} else {
		toarray=parsenumberarray(argv[i*2 +2],&afloat,&a2int,i+1,1,dims, true)-1;
		if(!a2int)a2int=(int64_t *)afloat;
	}
	if(dim[target]!=toarray)error("Size mismatch between slice and target array");
	if(size!=size2)error("String arrays differ in string length");
	i=dimcount-1;
	while(i>=0){
		off[i]=1;
		for(j=0; j<i; j++)off[i]*=(dim[j]+1);
		i--;
	}
	start=1;
	for(i=0;i<dimcount;i++){
		start+= (pos[i]*off[i]);
	}
	start--;
	increment=off[target];
	start-=increment;
	if(t & T_STR){
		int copy=(int)size+1; //allow for the length character of the string
		for(i=0;i<=dim[target];i++){
			unsigned char *p=a2str+i*copy;
			unsigned char *q=&a1str[(start+i*increment)*copy];
			memcpy(p,q,copy);
		}
	} else {
		for(i=0;i<=dim[target];i++)*a2int++ = a1int[start+i*increment];
	}
	return;
}

// the LET command
// because the LET is implied (ie, line does not have a recognisable command)
// it ends up as the place where mistyped commands are discovered.  This is why
// the error message is "Unknown command"
void  MIPS16 __not_in_flash_func(cmd_let)(void) {
	int t, size;
	MMFLOAT f;
    long long int  i64;
	unsigned char *s;
	unsigned char *p1, *p2;

	p1 = cmdline;
	// search through the line looking for the equals sign
	while(*p1 && tokenfunction(*p1) != op_equal) p1++;
	if(!*p1) error("Unknown command");

	// check that we have a straight forward variable
	p2 = skipvar(cmdline, false);
	skipspace(p2);
	if(p1 != p2) error("Syntax");

	// create the variable and get the length if it is a string
	p2 = findvar(cmdline, V_FIND);
    size = g_vartbl[g_VarIndex].size;
    if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");

	// step over the equals sign, evaluate the rest of the command and save in the variable
	p1++;
	if(g_vartbl[g_VarIndex].type & T_STR) {
		t = T_STR;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if(*s > size) error("String too long");
		Mstrcpy(p2, s);
	}
	else if(g_vartbl[g_VarIndex].type & T_NBR) {
		t = T_NBR;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if(t & T_NBR)
            (*(MMFLOAT *)p2) = f;
        else
            (*(MMFLOAT *)p2) = (MMFLOAT)i64;
	} else {
		t = T_INT;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if(t & T_INT)
            (*(long long int  *)p2) = i64;
        else
            (*(long long int  *)p2) = FloatToInt64(f);
	}
	checkend(p1);
}
/**
 * @cond
 * The following section will be excluded from the documentation.
 */
int MIPS16 as_strcmpi (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  unsigned char c1, c2;

  if (p1 == p2)
    return 0;

  do
    {
      c1 = tolower (*p1++);
      c2 = tolower (*p2++);
      if (c1 == '\0')
	break;
    }
  while (c1 == c2);

  return c1 - c2;
}
void MIPS16 sortStrings(char **arr, int n)
{
    char temp[STRINGSIZE];
    int i,j;
    // Sorting strings using bubble sort
    for (j=0; j<n-1; j++)
    {
        for (i=j+1; i<n; i++)
        {
            if (as_strcmpi(arr[j], arr[i]) > 0)
            {
                strcpy(temp, arr[j]);
                strcpy(arr[j], arr[i]);
                strcpy(arr[i], temp);
            }
        }
    }
}
void MIPS16 ListFile(char *pp, int all) {
	char buff[STRINGSIZE];
    int fnbr;
    int i,ListCnt = CurrentY/(FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
	fnbr = FindFreeFileNbr();
	if(!BasicFileOpen(pp, fnbr, FA_READ)) return;
	while(!FileEOF(fnbr)) {                                     // while waiting for the end of file
		memset(buff,0,256);
		MMgetline(fnbr, (char *)buff);									    // get the input line
		for(i=0;i<strlen(buff);i++)if(buff[i] == TAB) buff[i] = ' ';
		ListCnt=printWrappedText(buff,Option.Width,ListCnt,all);
	}
	FileClose(fnbr);
}

void MIPS16 ListNewLine(int *ListCnt, int all) {
	unsigned char noscroll=Option.NoScroll;
	if(!all && (void *)ReadBuffer!=(void *)DisplayNotSet)Option.NoScroll=0;
	MMPrintString("\r\n");
	(*ListCnt)++;
    if(!all && *ListCnt >= Option.Height-overlap) {
		#ifdef USBKEYBOARD
		clearrepeat();
		#endif
    	MMPrintString("PRESS ANY KEY ...");
    	MMgetchar();
    	MMPrintString("\r                 \r");
        if(Option.DISPLAY_CONSOLE){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
    	*ListCnt = 2;
    }
	Option.NoScroll=noscroll;
}


void MIPS16 ListProgram(unsigned char *p, int all) {
	char b[STRINGSIZE];
	char *pp;
    int ListCnt = CurrentY/(FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
	while(!(*p == 0 || *p == 0xff)) {                               // normally a LIST ends at the break so this is a safety precaution
        if(*p == T_NEWLINE) {
			p = llist((unsigned char *)b, p);                                        // otherwise expand the line
            if(!(b[0]=='\'' && b[1]=='#')){
				pp = b;
				if(Option.continuation){
					format_string(pp,Option.Width);
					while(*pp) {
						if(*pp=='\n'){
							ListNewLine(&ListCnt, all);
							pp++;
							continue;
						}
						MMputchar(*pp++,0);
					}
				} else {
					while(*pp) {
						if(MMCharPos > Option.Width) ListNewLine(&ListCnt, all);
						MMputchar(*pp++,0);
					}
				}
				fflush(stdout);
				ListNewLine(&ListCnt, all);
				if(p[0] == 0 && p[1] == 0) break;                       // end of the listing ?
			}
		}
	}
}


void MIPS16 do_run(unsigned char *cmdline, bool CMM2mode) {
    // RUN [ filename$ ] [, cmd_args$ ]
    unsigned char *filename = (unsigned char *)"", *cmd_args = (unsigned char *)"";
	unsigned char *cmdbuf=GetMemory(256);
	memcpy(cmdbuf,cmdline,STRINGSIZE);
    getargs(&cmdbuf, 3, (unsigned char *)",");
	    switch (argc) {
        case 0:
            break;
        case 1:
            filename = getCstring(argv[0]);
            break;
        case 2:
            cmd_args = getCstring(argv[1]);
            break;
        default:
            filename = getCstring(argv[0]);
            if(*argv[2])cmd_args = getCstring(argv[2]);
            break;
    }

    // The memory allocated by getCstring() is not preserved across
    // a call to FileLoadProgram() so we need to cache 'filename' and
    // 'cmd_args' on the stack.
    unsigned char buf[MAXSTRLEN + 1];
    if (snprintf((char *)buf, MAXSTRLEN + 1, "\"%s\",%s", filename, cmd_args) > MAXSTRLEN) {
        error("RUN command line too long");
    }
    unsigned char *pcmd_args = buf + strlen((char *)filename) + 3; // *** THW 16/4/23

#ifdef rp2350
    if(CMM2mode){
		if (*filename && !FileLoadCMM2Program((char *)buf,false)) return;
	} else {
#endif
		if (*filename && !FileLoadProgram(buf, false)) return;
#ifdef rp2350
	}
#endif
    ClearRuntime(true);
    PrepareProgram(true);
    if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
    // Create a global constant MM.CMDLINE$ containing 'cmd_args'.
//    void *ptr = findvar((unsigned char *)"MM.CMDLINE$", V_FIND | V_DIM_VAR | T_CONST);
    CtoM(pcmd_args);
//    memcpy(cmdlinebuff, pcmd_args, *pcmd_args + 1); // *** THW 16/4/23
	Mstrcpy(cmdlinebuff, pcmd_args);
    IgnorePIN = false;
	if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) ExecuteProgram(LibMemory );       // run anything that might be in the library
    if(*ProgMemory != T_NEWLINE) return;                             // no program to run
#ifdef PICOMITEWEB
	cleanserver();
#endif
#ifndef USBKEYBOARD
    if(mouse0==false && Option.MOUSE_CLOCK)initMouse0(0);  //see if there is a mouse to initialise 
#endif
	nextstmt = ProgMemory;
}
/** @endcond */
void MIPS16 cmd_list(void) {
	unsigned char *p;
	int i,j,k,m,step;
    if((p = checkstring(cmdline, (unsigned char *)"ALL"))) {
        if(!(*p == 0 || *p == '\'')) {
        	if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
        	getargs(&p,1,(unsigned char *)",");
        	char *buff=GetTempMemory(STRINGSIZE);
        	strcpy(buff,(char *)getCstring(argv[0]));
    		if(strchr(buff, '.') == NULL) strcat(buff, ".bas");
            ListFile(buff, true);
        } else {
        	if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
        	ListProgram(ProgMemory, true);
        	checkend(p);
        }
	} else if((p = checkstring(cmdline, (unsigned char *)"VARIABLES"))) {
		int count=0;
		int64_t *dest=NULL;
		char *buff=NULL;
		int j=0;
		getargs(&p,1,(unsigned char *)",");
		if(argc){
			j=(parseintegerarray(argv[0],&dest,1,1,NULL,true)-1)*8;
			dest[0] = 0;
		}
		for(int i=0;i<MAXVARS;i++){
			if(g_vartbl[i].type & (T_INT|T_STR|T_NBR)){
				count++;
			}
		}
		if(!count)return;
		char** c=GetTempMemory(count*sizeof(*c)+count*(MAXVARLEN+30));
		for(int i=0,j=0;i<MAXVARS;i++){
			char out[MAXVARLEN+30];
			if(g_vartbl[i].type & (T_INT|T_STR|T_NBR)){
				if(g_vartbl[i].level==0)strcpy(out,"DIM ");
				else strcpy(out,"LOCAL ");
				if(!(g_vartbl[i].type & T_EXPLICIT)){
					if(g_vartbl[i].type & T_INT){
						if(!(g_vartbl[i].type & T_EXPLICIT))strcat(out,"INTEGER ");
					}
					if(g_vartbl[i].type & T_STR){
						if(!(g_vartbl[i].type & T_EXPLICIT))strcat(out,"STRING ");
					}
					if(g_vartbl[i].type & T_NBR){
						if(!(g_vartbl[i].type & T_EXPLICIT))strcat(out,"FLOAT ");
					}
				}
				strcat(out,(char *)g_vartbl[i].name);
				if(g_vartbl[i].type & T_INT){
					if(g_vartbl[i].type & T_EXPLICIT)strcat(out,"%");
				}
				if(g_vartbl[i].type & T_STR){
					if(g_vartbl[i].type & T_EXPLICIT)strcat(out,"$");
				}
				if(g_vartbl[i].type & T_NBR){
					if(g_vartbl[i].type & T_EXPLICIT)strcat(out,"!");
				}
				if(g_vartbl[i].dims[0]>0){
					strcat(out,"(");
					for(int k=0;k<MAXDIM;k++){
						if(g_vartbl[i].dims[k]>0){
							char s[20];
							IntToStr(s, (int64_t)g_vartbl[i].dims[k], 10);
							strcat(out,s)					;
						}
						if(k<MAXDIM-1 && g_vartbl[i].dims[k+1]>0)strcat(out,",");
					}
					strcat(out,")");
				}
				c[j]= (char *)((int)c + sizeof(char *) * count + j*(MAXVARLEN+30));
				strcpy(c[j],out);
				j++;
			}
		}
		sortStrings(c,count);
    	int ListCnt = 2;
		if(dest==NULL){
			for(int i=0;i<count;i++){
				MMPrintString(c[i]);
				if(Option.DISPLAY_CONSOLE)ListNewLine(&ListCnt, 0);
				else MMPrintString("\r\n");
			}
		} else {
			int ol=0;
			buff=(char *)&dest[1];
			for(int i=0;i<count;i++){
				if(ol+strlen(c[i])+2 > j)error("Destination array too small");
				else ol+=strlen(c[i])+2;
				strcat(buff,c[i]);
				strcat(buff,"\r\n");
			}
			dest[0]=ol;
		}
		
   	} else if((p = checkstring(cmdline, (unsigned char *)"PINS"))) {
#if defined(PICOMITEWEB) && defined(rp2350)
		if(!rp2350a)error("Incompatible board, RP2350A only" );
#endif
		CallExecuteProgram((char *)pinlist);
		return;
   	} else if((p = checkstring(cmdline, (unsigned char *)"SYSTEM I2C"))) {
		if(I2C0locked || I2C1locked)CallExecuteProgram((char *)i2clist);
		else error("System I2c not defined");
		return;
   	} else if((p = checkstring(cmdline, (unsigned char *)"COMMANDS"))) {
    	int ListCnt = 2;
    	step=Option.DISPLAY_CONSOLE ? HRes/gui_font_width/20 : 5;
        if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
    	m=0;
		int x=0;
		char** c=GetTempMemory((CommandTableSize+x)*sizeof(*c)+(CommandTableSize+x)*18);
		for(i=0;i<CommandTableSize+x;i++){
				c[m]= (char *)((int)c + sizeof(char *) * (CommandTableSize+x) + m*18);
				if(m<CommandTableSize)strcpy(c[m],(char *)commandtbl[i].name);
				if(*c[m]=='_' && c[m][1]!='(')*c[m]='.';
    			m++;
		}
    	sortStrings(c,m);
    	for(i=1;i<m;i+=step){
    		for(k=0;k<step;k++){
        		if(i+k<m){
        			MMPrintString(c[i+k]);
        			if(k!=(step-1))for(j=strlen(c[i+k]);j<15;j++)MMputchar(' ',1);
        		}
    		}
			if(Option.DISPLAY_CONSOLE)ListNewLine(&ListCnt, 0);
    		else MMPrintString("\r\n");
    	}
		MMPrintString("Total of ");PInt(m-1);MMPrintString(" commands\r\n");
    } else if((p = checkstring(cmdline, (unsigned char *)"FUNCTIONS"))) {
    	m=0;
    	int ListCnt = 2;
    	step=Option.DISPLAY_CONSOLE ? HRes/gui_font_width/20 : 5;
        if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
		int x=3+MMEND;
		char** c=GetTempMemory((TokenTableSize+x)*sizeof(*c)+(TokenTableSize+x)*20);
		for(i=0;i<TokenTableSize+x;i++){
				c[m]= (char *)((int)c + sizeof(char *) * (TokenTableSize+x) + m*20);
				if(m<TokenTableSize)strcpy(c[m],(char *)tokentbl[i].name);
	   			else if(m<TokenTableSize+MMEND && m>=TokenTableSize)strcpy(c[m],overlaid_functions[i-TokenTableSize]);
    			else if(m==TokenTableSize+MMEND)strcpy(c[m],"=<");
    			else if(m==TokenTableSize+MMEND+1)strcpy(c[m],"=>");
    			else strcpy(c[m],"MM.Info$(");
				m++;
		}
    	sortStrings(c,m);
    	for(i=1;i<m-1;i+=step){
    		for(k=0;k<step;k++){
        		if(i+k<m-1){
        			MMPrintString(c[i+k]);
        			if(k!=(step-1))for(j=strlen(c[i+k]);j<15;j++)MMputchar(' ',1);
        		}
    		}
			if(Option.DISPLAY_CONSOLE)ListNewLine(&ListCnt, 0);
    		else MMPrintString("\r\n");
    	}
		MMPrintString("Total of ");PInt(m-1);MMPrintString(" functions and operators\r\n");
    } else {
        if(!(*cmdline == 0 || *cmdline == '\'')) {
        	getargs(&cmdline,1,(unsigned char *)",");
        	if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
        	char *buff=GetTempMemory(STRINGSIZE);
        	strcpy(buff,(char *)getCstring(argv[0]));
    		if(strchr(buff, '.') == NULL) {
				if(!ExistsFile(buff))strcat(buff, ".bas");
			}
			ListFile(buff, false);
        } else {
        	if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
			ListProgram(ProgMemory, false);
			checkend(cmdline);
		}
    }
}
#include <stdio.h>
#include <string.h>

int printWrappedText(const char *text, int screenWidth, int listcnt, int all) {
    int length = strlen(text);
    int start = 0; // Start index of the current line
	char buff[STRINGSIZE];
    while (start < length) {
        int end = start + screenWidth; // Calculate the end index for the current line
        if (end >= length) {
            // If end is beyond the text length, just print the remaining text
			memset(buff,0,STRINGSIZE);
            sprintf(buff,"%s", text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
            break;
        }

        // Find the last space within the current screen width
        int lastSpace = -1;
        for (int i = start; i < end; i++) {
            if (text[i] == ' ') {
                lastSpace = i;
            }
        }

        if (lastSpace != -1) {
            // If a space is found, break at the space
			memset(buff,0,STRINGSIZE);
            sprintf(buff, "%.*s", lastSpace - start, text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
            start = lastSpace + 1; // Skip the space
        } else {
            // If no space is found, truncate at screen width
			memset(buff,0,STRINGSIZE);
            sprintf(buff,"%.*s", screenWidth, text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
            start += screenWidth;
        }
    }
	return listcnt;
}

void cmd_help(void){
	getargs(&cmdline,1,(unsigned char *)",");
	if(!ExistsFile("A:/help.txt"))error("A:/help.txt not found");
	if(!argc){
		MMPrintString("Enter help and the name of the command or function\r\nUse * for multicharacter wildcard or ? for single character wildcard\r\n");
	} else {
		int fnbr = FindFreeFileNbr();
		char *buff=GetTempMemory(STRINGSIZE);
		BasicFileOpen("A:/help.txt",fnbr, FA_READ);
		int ListCnt = CurrentY/(FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
		char *p=(char *)getCstring(argv[0]);
		bool end=false;
		while (!FileEOF(fnbr))            { // while waiting for the end of file
			memset(buff,0,STRINGSIZE);
			char *in=buff;
			while(1){
				if(FileEOF(fnbr)){end=true;break;}
				char c = FileGetChar(fnbr);
				if(c=='\n')break;
				if(c=='\r')continue;
				*in++=c;
			}
			if(end)break;
			skipspace(p);
			if(buff[0]=='~'){
				if(pattern_matching(p,&buff[1],0,0)){
					while(1){ //loop through all lines for the command
						memset(buff,0,STRINGSIZE);
						char *in=buff;
							while(1){ //get this line
							if(FileEOF(fnbr)){end=true;break;}
							char c = FileGetChar(fnbr);
							if(c=='\n')break;
							if(c=='\r')continue;
							*in++=c;
						}
						if(end)break;
						if(buff[0]=='~'){ //now we need to rewind the file to check this line
							ListNewLine(&ListCnt, false);
							lfs_file_seek(&lfs, FileTable[fnbr].lfsptr, -(strlen(buff)+2), LFS_SEEK_CUR);
							break;
						} else {
							ListCnt=printWrappedText(buff,Option.Width-1,ListCnt,false);
						}
					}
				}
			} 
		}
	FileClose(fnbr);
	}
}
void MIPS16 cmd_run(void){
	do_run(cmdline,false);
}

void MIPS16 cmd_RunCMM2(void){
	do_run(cmdline,true);
}

void  MIPS16 cmd_continue(void) {
    if(*cmdline == tokenFOR) {
        if(g_forindex == 0) error("No FOR loop is in effect");
        nextstmt = g_forstack[g_forindex - 1].nextptr;
        return;
    }
    if(checkstring(cmdline, (unsigned char *)"DO")) {
        if(g_doindex == 0) error("No DO loop is in effect");
        nextstmt = g_dostack[g_doindex - 1].loopptr;
        return;
    }
    // must be a normal CONTINUE
	checkend(cmdline);
	if(CurrentLinePtr) error("Invalid in a program");
	if(ContinuePoint == NULL) error("Cannot continue");
//    IgnorePIN = false;
	nextstmt = ContinuePoint;
}

void MIPS16 cmd_new(void) {
	closeframebuffer('A');
	checkend(cmdline);
	ClearProgram(true);
	FlashLoad=0;
	uSec(250000);
    FlashWriteInit(PROGRAM_FLASH);
    flash_range_erase(realflashpointer, MAX_PROG_SIZE);
    FlashWriteByte(0); FlashWriteByte(0); FlashWriteByte(0);    // terminate the program in flash
    FlashWriteClose();
#ifdef PICOMITEVGA
	int mode = DISPLAY_TYPE-SCREENMODE1+1;
	setmode(mode, true);
#endif
    memset(inpbuf,0,STRINGSIZE);
	longjmp(mark, 1);							                    // jump back to the input prompt
}


void MIPS16 cmd_erase(void) {
	int i,j,k, len;
	char p[MAXVARLEN + 1], *s, *x;

	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");				// getargs macro must be the first executable stmt in a block
	if((argc & 0x01) == 0) error("Argument count");
	for(i = 0; i < argc; i += 2) {
		strcpy((char *)p, (char *)argv[i]);
		if(*argv[i] & 0x80)error("You can't erase an in-built function");
        while(!isnamechar(p[strlen(p) - 1])) p[strlen(p) - 1] = 0;
		makeupper((unsigned char *)p);  
		for(j = MAXVARS/2; j < MAXVARS; j++) {
            s = p;  x = (char *)g_vartbl[j].name; len = strlen(p);
            while(len > 0 && *s == *x) {                            // compare the variable to the name that we have
                len--; s++; x++;
            }
            if(!(len == 0 && (*x == 0 || strlen(p) == MAXVARLEN))) continue;
    		// found the variable
			if(((g_vartbl[j].type & T_STR) || g_vartbl[j].dims[0] != 0) && !(g_vartbl[j].type & T_PTR)) {
				FreeMemory(g_vartbl[j].val.s);                        // free any memory (if allocated)
				g_vartbl[j].val.s=NULL;
			}
			k=j+1;
			if(k==MAXVARS)k=MAXVARS/2;
			if(g_vartbl[k].type){
				g_vartbl[j].name[0]='~';
				g_vartbl[j].type=T_BLOCKED;
			} else {
				g_vartbl[j].name[0]=0;
				g_vartbl[j].type=T_NOTYPE;
			}
			g_vartbl[j].dims[0] = 0;                                    // and again
			g_vartbl[j].level = 0;
			g_Globalvarcnt--;
			break;
		}
		if(j == MAXVARS) error("Cannot find $", p);
	}
}
void MIPS16 cmd_clear(void) {
	checkend(cmdline);
	if(g_LocalIndex)error("Invalid in a subroutine");
	ClearVars(0,true);
}


void cmd_goto(void) {
	if(isnamestart(*cmdline))
		nextstmt = findlabel(cmdline);								// must be a label
	else
		nextstmt = findline(getinteger(cmdline), true);				// try for a line number
	CurrentLinePtr = nextstmt;
}



#ifdef PICOMITEWEB
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_if)(void) {
#else
void cmd_if(void) {
#endif
#else
#ifndef rp2350
#ifdef PICOMITEVGA
void cmd_if(void) {
#else
void MIPS16 __not_in_flash_func(cmd_if)(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_if)(void) {
#endif
#endif
 	int r, i, testgoto, testelseif;
	unsigned char ss[3];														// this will be used to split up the argument line
	unsigned char *p, *tp;
	unsigned char *rp = NULL;

	ss[0] = tokenTHEN;
	ss[1] = tokenELSE;
	ss[2] = 0;

	testgoto = false;
	testelseif = false;

retest_an_if:
	{																// start a new block
		getargs(&cmdline, 20, ss);									// getargs macro must be the first executable stmt in a block

		if(testelseif && argc > 2) error("Unexpected text");

		// if there is no THEN token retry the test with a GOTO.  If that fails flag an error
		if(argc < 2 || *argv[1] != ss[0]) {
			if(testgoto) error("IF without THEN");
			ss[0] = tokenGOTO;
			testgoto = true;
			goto retest_an_if;
		}


		// allow for IF statements embedded inside this IF
		if (argc >= 3 && commandtbl_decode(argv[2]) == cmdIF) argc = 3;  // this is IF xx=yy THEN IF ... so we want to evaluate only the first 3
		if (argc >= 5 && commandtbl_decode(argv[4]) == cmdIF) argc = 5;  // this is IF xx=yy THEN cmd ELSE IF ... so we want to evaluate only the first 5

		if(argc == 4 || (argc == 5 && *argv[3] != ss[1])) error("Syntax");

		r = (getnumber(argv[0]) != 0);								// evaluate the expression controlling the if statement

		if(r) {
			// the test returned TRUE
			// first check if it is a multiline IF (ie, only 2 args)
			if(argc == 2) {
				// if multiline do nothing, control will fall through to the next line (which is what we want to execute next)
				;
			}
			else {
				// This is a standard single line IF statement
				// Because the test was TRUE we are just interested in the THEN cmd stage.
				if(*argv[1] == tokenGOTO) {
					cmdline = argv[2];
					cmd_goto();
					return;
				} else if(isdigit(*argv[2])) {
					nextstmt = findline(getinteger(argv[2]), true);
				} else {
					if(argc == 5) {
						// this is a full IF THEN ELSE and the statement we want to execute is between the THEN & ELSE
						// this is handled by a special routine
						execute_one_command(argv[2]);
					} else {
						// easy - there is no ELSE clause so just point the next statement pointer to the byte after the THEN token
						for(p = cmdline; *p && *p != ss[0]; p++);	// search for the token
						nextstmt = p + 1;							// and point to the byte after
					}
				}
			}
		} else {
			// the test returned FALSE so we are just interested in the ELSE stage (if present)
			// first check if it is a multiline IF (ie, only 2 args)
			if(argc == 2) {
				// search for the next ELSE, or ENDIF and pass control to the following line
				// if an ELSEIF is found re execute this function to evaluate the condition following the ELSEIF
				i = 1; p = nextstmt;
				while(1) {
                    p = GetNextCommand(p, &rp, (unsigned char *)"No matching ENDIF");
        			CommandToken tkn=commandtbl_decode(p);
					if(tkn == cmdtoken) {
						// found a nested IF command, we now need to determine if it is a single or multiline IF
						// search for a THEN, then check if only white space follows.  If so, it is multiline.
						tp = p + sizeof(CommandToken);
						while(*tp && *tp != ss[0]) tp++;
						if(*tp) tp++;								// step over the THEN
						skipspace(tp);
						if(*tp == 0 || *tp == '\'')					// yes, only whitespace follows
							i++;									// count it as a nested IF
						else										// no, it is a single line IF
							skipelement(p);							// skip to the end so that we avoid an ELSE
						continue;
					}

					if(tkn == cmdELSE && i == 1) {
						// found an ELSE at the same level as this IF.  Step over it and continue with the statement after it
						skipelement(p);
						nextstmt = p;
						break;
					}

					if((tkn == cmdELSEIF || tkn==cmdELSE_IF) && i == 1) {
						// we have found an ELSEIF statement at the same level as our IF statement
						// setup the environment to make this function evaluate the test following ELSEIF and jump back
						// to the start of the function.  This is not very clean (it uses the dreaded goto for a start) but it works
						p+=sizeof(CommandToken);                                        // step over the token
						skipspace(p);
						CurrentLinePtr = rp;
						if(*p == 0) error("Syntax");        // there must be a test after the elseif
						cmdline = p;
						skipelement(p);
						nextstmt = p;
						testgoto = false;
						testelseif = true;
						goto retest_an_if;
					}

					if(tkn == cmdENDIF || tkn==cmdEND_IF) i--;						// found an ENDIF so decrement our nested counter
					if(i == 0) {
						// found our matching ENDIF stmt.  Step over it and continue with the statement after it
						skipelement(p);
						nextstmt = p;
						break;
					}
				}
			}
			else {
				// this must be a single line IF statement
				// check if there is an ELSE on the same line
				if(argc == 5) {
					// there is an ELSE command
					if(isdigit(*argv[4]))
						// and it is just a number, so get it and find the line
						nextstmt = findline(getinteger(argv[4]), true);
					else {
/*						// there is a statement after the ELSE clause  so just point to it (the byte after the ELSE token)
						for(p = cmdline; *p && *p != ss[1]; p++);	// search for the token
						nextstmt = p + 1;							// and point to the byte after
*/
		                // IF <condition> THEN <statement1> ELSE <statement2>
						// Find and read the THEN function token.
						for(p = cmdline; *p && *p != ss[0]; p++){}
						// Skip the command that <statement1> must start with.
						p++;
						skipspace(p);
						p += sizeof(CommandToken);
						// Find and read the ELSE function token.
						for(; *p && *p != ss[1]; p++);
		                nextstmt = p+1;  // The statement after the ELSE token.
					}
				} else {
					// no ELSE on a single line IF statement, so just continue with the next statement
					skipline(cmdline);
					nextstmt = cmdline;
				}
			}
		}
	}
}



#ifdef PICOMITEWEB
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_else)(void) {
#else
void cmd_else(void) {
#endif
#else
#ifndef rp2350
#ifdef PICOMITEVGA
void cmd_else(void) {
#else
void MIPS16 __not_in_flash_func(cmd_else)(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_else)(void) {
#endif
#endif
	int i;
	unsigned char *p, *tp;

	// search for the next ENDIF and pass control to the following line
	i = 1; p = nextstmt;

	if(cmdtoken ==  cmdELSE) checkend(cmdline);

	while(1) {
        p = GetNextCommand(p, NULL, (unsigned char *)"No matching ENDIF");
        CommandToken tkn=commandtbl_decode(p);
		if(tkn == cmdIF) { 
			// found a nested IF command, we now need to determine if it is a single or multiline IF
			// search for a THEN, then check if only white space follows.  If so, it is multiline.
			tp = p + sizeof(CommandToken);
			while(*tp && *tp != tokenTHEN) tp++;
			if(*tp) tp++;											// step over the THEN
			skipspace(tp);
			if(*tp == 0 || *tp == '\'')								// yes, only whitespace follows
				i++;												// count it as a nested IF
		}
		if(tkn == cmdENDIF || tkn==cmdEND_IF) i--;				    // found an ENDIF so decrement our nested counter
		if(i == 0) break;											// found our matching ENDIF stmt
	}
	// found a matching ENDIF.  Step over it and continue with the statement after it
	skipelement(p);
	nextstmt = p;
}



void do_end(bool ecmd) {
#ifdef PICOMITE
    if(mergerunning){
        multicore_fifo_push_blocking(0xFF);
        mergerunning=false;
        busy_wait_ms(100);
    }
#endif
if(Option.SerialConsole)while(ConsoleTxBufHead!=ConsoleTxBufTail)routinechecks();
	fflush(stdout);
	if(ecmd){
		getargs(&cmdline,1,(unsigned char *)",");
		if(argc==1){
			if(FindSubFun((unsigned char *)"MM.END", 0) >= 0 && checkstring(argv[0],(unsigned char *)"NOEND")==NULL) {
				ExecuteProgram((unsigned char *)"MM.END\0");
				if(Option.SerialConsole)while(ConsoleTxBufHead!=ConsoleTxBufTail)routinechecks();
				fflush(stdout);
				memset(inpbuf,0,STRINGSIZE);
			} else {
				unsigned char *cmd_args = (unsigned char *)"";
				cmd_args = getCstring(argv[0]);
				void *ptr = findvar((unsigned char *)"MM.ENDLINE$", T_STR| V_NOFIND_NULL);  
				if(ptr==NULL)ptr = findvar((unsigned char *)"MM.ENDLINE$", V_FIND |V_DIM_VAR);
				strcpy(ptr, (char *)cmd_args ); // *** THW 16/4/23
				CtoM(ptr);
			}
		} else if(FindSubFun((unsigned char *)"MM.END", 0) >= 0) {
			ExecuteProgram((unsigned char *)"MM.END\0");
			if(Option.SerialConsole)while(ConsoleTxBufHead!=ConsoleTxBufTail)routinechecks();
			fflush(stdout);
			memset(inpbuf,0,STRINGSIZE);
		}
	}
    if(!(MMerrno == 16))hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    irq_set_enabled(DMA_IRQ_1, false);
    dma_hw->abort = ((1u << dma_rx_chan2) | (1u << dma_rx_chan));
    if(dma_channel_is_busy(dma_rx_chan))dma_channel_abort(dma_rx_chan);
    if(dma_channel_is_busy(dma_rx_chan2))dma_channel_abort(dma_rx_chan2);
//    dma_channel_cleanup(dma_rx_chan);
//    dma_channel_cleanup(dma_rx_chan2);
    dma_hw->abort = ((1u << dma_tx_chan2) | (1u << dma_tx_chan));
    if(dma_channel_is_busy(dma_tx_chan))dma_channel_abort(dma_tx_chan);
    if(dma_channel_is_busy(dma_tx_chan2))dma_channel_abort(dma_tx_chan2);
//    dma_channel_cleanup(dma_tx_chan);
//    dma_channel_cleanup(dma_tx_chan2);
    dma_hw->abort = ((1u << ADC_dma_chan2) | (1u << ADC_dma_chan));
    if(dma_channel_is_busy(ADC_dma_chan))dma_channel_abort(ADC_dma_chan);
    if(dma_channel_is_busy(ADC_dma_chan2))dma_channel_abort(ADC_dma_chan2);
//    dma_channel_cleanup(ADC_dma_chan);
//    dma_channel_cleanup(ADC_dma_chan2);
	for(int i=0; i< NBRSETTICKS;i++){
		TickPeriod[i]=0;
		TickTimer[i]=0;
		TickInt[i]=NULL;
		TickActive[i]=0;
	}
	InterruptUsed=0;       
    InterruptReturn = NULL ; 
    memset(inpbuf,0,STRINGSIZE);
	CloseAudio(1);
	CloseAllFiles();
    ADCDualBuffering=0;
	WatchdogSet = false;
    WDTimer = 0;
	hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
	_excep_code=0;
	dmarunning = false;
    multi=false;
	WAVInterrupt = NULL;
	WAVcomplete = 0;
	if(g_myrand)FreeMemory((void *)g_myrand);
	g_myrand=NULL;
	OptionConsole=3;
#ifdef PICOMITEVGA
	int mode = DISPLAY_TYPE-SCREENMODE1+1;
	setmode(mode,false);
#endif
	SSPrintString("\033[?25h"); //in case application has turned the cursor off
	SSPrintString("\033[97;40m");
#ifdef PICOMITEWEB
	close_tcpclient();
#endif
#ifndef USBKEYBOARD
    if(mouse0==false && Option.MOUSE_CLOCK)initMouse0(0);  //see if there is a mouse to initialise 
#endif
}
void cmd_end(void) {
	do_end(true);
	longjmp(mark, 1);												// jump back to the input prompt
}
extern unsigned int mmap[HEAP_MEMORY_SIZE/ PAGESIZE / PAGESPERWORD];
extern unsigned int psmap[7*1024*1024/ PAGESIZE / PAGESPERWORD];
extern struct s_hash g_hashlist[MAXVARS/2];
extern int g_hashlistpointer;
extern short g_StrTmpIndex;
extern bool g_TempMemoryIsChanged;
extern volatile char *g_StrTmp[MAXTEMPSTRINGS];                                       // used to track temporary string space on the heap
extern volatile char g_StrTmpLocalIndex[MAXTEMPSTRINGS];                              // used to track the g_LocalIndex for each temporary string space on the heap
void SaveContext(void){
	CloseAudio(1);
	#if defined(rp2350) && !defined(PICOMITEWEB)
	if(PSRAMsize){
		ClearTempMemory();
		uint8_t *p=(uint8_t *)PSRAMbase+PSRAMsize;
		memcpy(p,  &g_StrTmpIndex, sizeof(g_StrTmpIndex));
		p+=sizeof(g_StrTmpIndex);
		memcpy(p,  &g_TempMemoryIsChanged, sizeof(g_TempMemoryIsChanged));
		p+=sizeof(g_TempMemoryIsChanged);
		memcpy(p,  (void *)g_StrTmp, sizeof(g_StrTmp));
		p+=sizeof(g_StrTmp);
		memcpy(p,  (void *)g_StrTmpLocalIndex, sizeof(g_StrTmpLocalIndex));
		p+=sizeof(g_StrTmpLocalIndex);
		memcpy(p,  &g_LocalIndex, sizeof(g_LocalIndex));
		p+=sizeof(g_LocalIndex);
		memcpy(p,  &g_OptionBase, sizeof(g_OptionBase));
		p+=sizeof(g_OptionBase);
		memcpy(p,  &g_DimUsed, sizeof(g_DimUsed));
		p+=sizeof(g_DimUsed);
		memcpy(p,  &g_varcnt, sizeof(g_varcnt));
		p+=sizeof(g_varcnt);
		memcpy(p,  &g_Globalvarcnt, sizeof(g_Globalvarcnt));
		p+=sizeof(g_Globalvarcnt);
		memcpy(p,  &g_Localvarcnt, sizeof(g_Localvarcnt));
		p+=sizeof(g_Localvarcnt);
		memcpy(p,  &g_hashlistpointer, sizeof(g_hashlistpointer));
		p+=sizeof(g_hashlistpointer);
		memcpy(p,  &g_forindex, sizeof(g_forindex));
		p+=sizeof(g_forindex);
		memcpy(p,  &g_doindex, sizeof(g_doindex));
		p+=sizeof(g_doindex);
		memcpy(p,  g_forstack, sizeof(struct s_forstack)*MAXFORLOOPS);
		p+=sizeof(struct s_forstack)*MAXFORLOOPS;
		memcpy(p,  g_dostack, sizeof(struct s_dostack)*MAXDOLOOPS);
		p+=sizeof(struct s_dostack)*MAXDOLOOPS;
		memcpy(p,  g_vartbl, sizeof(struct s_vartbl)*MAXVARS);
		p+=sizeof(struct s_vartbl)*MAXVARS;
		memcpy(p,  g_hashlist, sizeof(struct s_hash)*MAXVARS/2);
		p+=sizeof(struct s_hash)*MAXVARS/2;
		memcpy(p,  MMHeap, heap_memory_size+256);
		p+=heap_memory_size+256;
		memcpy(p,  mmap, sizeof(mmap));
		p+=sizeof(mmap);
		memcpy(p, psmap, sizeof(psmap));
		p+=sizeof(psmap);
	} else {
#endif
		lfs_file_t lfs_file;
		if(ExistsFile("A:/.vars")){
			lfs_remove(&lfs, "/.vars");
		}
		int sizeneeded= sizeof(g_StrTmpIndex)+ sizeof(g_TempMemoryIsChanged)+sizeof(g_StrTmp)+sizeof(g_StrTmpLocalIndex)+sizeof(g_Localvarcnt)+
		sizeof(g_LocalIndex)+sizeof(g_OptionBase)+sizeof(g_DimUsed)+sizeof(g_varcnt)+sizeof(g_Globalvarcnt)+
		sizeof(g_hashlistpointer)+sizeof(g_forindex)+sizeof(g_doindex)+sizeof(struct s_forstack)*MAXFORLOOPS+sizeof(struct s_dostack)*MAXDOLOOPS+
		sizeof(struct s_vartbl)*MAXVARS+sizeof(struct s_hash)*MAXVARS/2+heap_memory_size+256+sizeof(mmap);
		if(sizeneeded>=Option.FlashSize-(Option.modbuff ? 1024*Option.modbuffsize : 0)-RoundUpK4(TOP_OF_SYSTEM_FLASH)-lfs_fs_size(&lfs)*4096)error("Not enough free space on A: drive: % needed",sizeneeded);
		lfs_file_open(&lfs, &lfs_file, ".vars", LFS_O_RDWR | LFS_O_CREAT);;
		int dt=get_fattime();
		ClearTempMemory();
		lfs_setattr(&lfs, ".vars", 'A', &dt,   4);
		lfs_file_write(&lfs, &lfs_file, &g_StrTmpIndex, sizeof(g_StrTmpIndex));
		lfs_file_write(&lfs, &lfs_file, &g_TempMemoryIsChanged, sizeof(g_TempMemoryIsChanged));
		lfs_file_write(&lfs, &lfs_file, (void *)g_StrTmp, sizeof(g_StrTmp));
		lfs_file_write(&lfs, &lfs_file, (void *)g_StrTmpLocalIndex, sizeof(g_StrTmpLocalIndex));
		lfs_file_write(&lfs, &lfs_file, &g_LocalIndex, sizeof(g_LocalIndex));
		lfs_file_write(&lfs, &lfs_file, &g_OptionBase, sizeof(g_OptionBase));
		lfs_file_write(&lfs, &lfs_file, &g_DimUsed, sizeof(g_DimUsed));
		lfs_file_write(&lfs, &lfs_file, &g_varcnt, sizeof(g_varcnt));
		lfs_file_write(&lfs, &lfs_file, &g_Globalvarcnt, sizeof(g_Globalvarcnt));
		lfs_file_write(&lfs, &lfs_file, &g_Localvarcnt, sizeof(g_Globalvarcnt));
		lfs_file_write(&lfs, &lfs_file, &g_hashlistpointer, sizeof(g_hashlistpointer));
		lfs_file_write(&lfs, &lfs_file, &g_forindex, sizeof(g_forindex));
		lfs_file_write(&lfs, &lfs_file, &g_doindex, sizeof(g_doindex));
		lfs_file_write(&lfs, &lfs_file, g_forstack, sizeof(struct s_forstack)*MAXFORLOOPS);
		lfs_file_write(&lfs, &lfs_file, g_dostack, sizeof(struct s_dostack)*MAXDOLOOPS);
		lfs_file_write(&lfs, &lfs_file, g_vartbl, sizeof(struct s_vartbl)*MAXVARS);
		lfs_file_write(&lfs, &lfs_file, g_hashlist, sizeof(struct s_hash)*MAXVARS/2);
		lfs_file_write(&lfs, &lfs_file, MMHeap, heap_memory_size+256);
		lfs_file_write(&lfs, &lfs_file, mmap, sizeof(mmap));
		lfs_file_close(&lfs, &lfs_file);
#if defined(rp2350) && !defined(PICOMITEWEB)
	}
#endif
}
void RestoreContext(bool keep){
	CloseAudio(1);
	#if defined(rp2350) && !defined(PICOMITEWEB)
	if(PSRAMsize){
		uint8_t *p=(uint8_t *)PSRAMbase+PSRAMsize;
		memcpy(&g_StrTmpIndex, p, sizeof(g_StrTmpIndex));
		p+=sizeof(g_StrTmpIndex);
		memcpy(&g_TempMemoryIsChanged, p, sizeof(g_TempMemoryIsChanged));
		p+=sizeof(g_TempMemoryIsChanged);
		memcpy((void *)g_StrTmp, p, sizeof(g_StrTmp));
		p+=sizeof(g_StrTmp);
		memcpy((void *)g_StrTmpLocalIndex, p, sizeof(g_StrTmpLocalIndex));
		p+=sizeof(g_StrTmpLocalIndex);
		memcpy(&g_LocalIndex, p, sizeof(g_LocalIndex));
		p+=sizeof(g_LocalIndex);
		memcpy(&g_OptionBase, p, sizeof(g_OptionBase));
		p+=sizeof(g_OptionBase);
		memcpy(&g_DimUsed, p, sizeof(g_DimUsed));
		p+=sizeof(g_DimUsed);
		memcpy(&g_varcnt, p, sizeof(g_varcnt));
		p+=sizeof(g_varcnt);
		memcpy(&g_Globalvarcnt, p, sizeof(g_Globalvarcnt));
		p+=sizeof(g_Globalvarcnt);
		memcpy(&g_Localvarcnt, p, sizeof(g_Localvarcnt));
		p+=sizeof(g_Localvarcnt);
		memcpy(&g_hashlistpointer, p, sizeof(g_hashlistpointer));
		p+=sizeof(g_hashlistpointer);
		memcpy(&g_forindex, p, sizeof(g_forindex));
		p+=sizeof(g_forindex);
		memcpy(&g_doindex, p, sizeof(g_doindex));
		p+=sizeof(g_doindex);
		memcpy(g_forstack, p, sizeof(struct s_forstack)*MAXFORLOOPS);
		p+=sizeof(struct s_forstack)*MAXFORLOOPS;
		memcpy(g_dostack, p, sizeof(struct s_dostack)*MAXDOLOOPS);
		p+=sizeof(struct s_dostack)*MAXDOLOOPS;
		memcpy(g_vartbl, p, sizeof(struct s_vartbl)*MAXVARS);
		p+=sizeof(struct s_vartbl)*MAXVARS;
		memcpy(g_hashlist, p, sizeof(struct s_hash)*MAXVARS/2);
		p+=sizeof(struct s_hash)*MAXVARS/2;
		memcpy(MMHeap, p, heap_memory_size+256);
		p+=heap_memory_size+256;
		memcpy(mmap, p, sizeof(mmap));
		p+=sizeof(mmap);
		memcpy(psmap, p, sizeof(psmap));
		p+=sizeof(psmap);
	} else {
#endif
		lfs_file_t lfs_file;
		if(!ExistsFile("A:/.vars"))error("Internal error");
		lfs_file_open(&lfs, &lfs_file, "/.vars", LFS_O_RDONLY);
		lfs_file_read(&lfs, &lfs_file, &g_StrTmpIndex, sizeof(g_StrTmpIndex));
		lfs_file_read(&lfs, &lfs_file, &g_TempMemoryIsChanged, sizeof(g_TempMemoryIsChanged));
		lfs_file_read(&lfs, &lfs_file, (void *)g_StrTmp, sizeof(g_StrTmp));
		lfs_file_read(&lfs, &lfs_file, (void *)g_StrTmpLocalIndex, sizeof(g_StrTmpLocalIndex));
		lfs_file_read(&lfs, &lfs_file, &g_LocalIndex, sizeof(g_LocalIndex));
		lfs_file_read(&lfs, &lfs_file, &g_OptionBase, sizeof(g_OptionBase));
		lfs_file_read(&lfs, &lfs_file, &g_DimUsed, sizeof(g_DimUsed));
		lfs_file_read(&lfs, &lfs_file, &g_varcnt, sizeof(g_varcnt));
		lfs_file_read(&lfs, &lfs_file, &g_Globalvarcnt, sizeof(g_Globalvarcnt));
		lfs_file_read(&lfs, &lfs_file, &g_Localvarcnt, sizeof(g_Globalvarcnt));
		lfs_file_read(&lfs, &lfs_file, &g_hashlistpointer, sizeof(g_hashlistpointer));
		lfs_file_read(&lfs, &lfs_file, &g_forindex, sizeof(g_forindex));
		lfs_file_read(&lfs, &lfs_file, &g_doindex, sizeof(g_doindex));
		lfs_file_read(&lfs, &lfs_file, g_forstack, sizeof(struct s_forstack)*MAXFORLOOPS);
		lfs_file_read(&lfs, &lfs_file, g_dostack, sizeof(struct s_dostack)*MAXDOLOOPS);
		lfs_file_read(&lfs, &lfs_file, g_vartbl, sizeof(struct s_vartbl)*MAXVARS);
		lfs_file_read(&lfs, &lfs_file, g_hashlist, sizeof(struct s_hash)*MAXVARS/2);
		lfs_file_read(&lfs, &lfs_file, MMHeap, heap_memory_size+256);
		lfs_file_read(&lfs, &lfs_file, mmap, sizeof(mmap));
		lfs_file_close(&lfs, &lfs_file);
		if(!keep)lfs_remove(&lfs, "/.vars");
#if defined(rp2350) && !defined(PICOMITEWEB)
	}
#endif
}
extern void chdir(char *p);
void MIPS16 do_chain(unsigned char *cmdline){
    unsigned char *filename = (unsigned char *)"", *cmd_args = (unsigned char *)"";
	unsigned char *cmdbuf=GetMemory(256);
	memcpy(cmdbuf,cmdline,STRINGSIZE);
    getargs(&cmdbuf, 3, (unsigned char *)",");
	    switch (argc) {
        case 0:
            break;
        case 1:
            filename = getCstring(argv[0]);
            break;
        case 2:
            cmd_args = getCstring(argv[1]);
            break;
        default:
            filename = getCstring(argv[0]);
            if(*argv[2])cmd_args = getCstring(argv[2]);
            break;
    }

    // The memory allocated by getCstring() is not preserved across
    // a call to FileLoadProgram() so we need to cache 'filename' and
    // 'cmd_args' on the stack.
    unsigned char buf[MAXSTRLEN + 1];
    if (snprintf((char *)buf, MAXSTRLEN + 1, "\"%s\",%s", filename, cmd_args) > MAXSTRLEN) {
        error("RUN command line too long");
    }
	FreeMemory(cmdbuf);
    unsigned char *pcmd_args = buf + strlen((char *)filename) + 3; // *** THW 16/4/23
    *cmdline=0;
	do_end(false);
	SaveContext();
	ClearVars(0,false);
	InitHeap(false);
	if (*buf && !FileLoadProgram(buf, true)) return;
    ClearRuntime(false);
    PrepareProgram(true);
	RestoreContext(false);
    if(Option.DISPLAY_CONSOLE && (SPIREAD  || Option.NoScroll)){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
    // Create a global constant MM.CMDLINE$ containing 'cmd_args'.
//    void *ptr = findvar((unsigned char *)"MM.CMDLINE$", V_NOFIND_ERR);
    CtoM(pcmd_args);
//    memcpy(cmdlinebuff, pcmd_args, *pcmd_args + 1); // *** THW 16/4/23
	Mstrcpy(cmdlinebuff, pcmd_args);
    IgnorePIN = false;
	if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) ExecuteProgram(LibMemory );       // run anything that might be in the library
    if(*ProgMemory != T_NEWLINE) return;                             // no program to run
#ifdef PICOMITEWEB
	cleanserver();
#endif
#ifndef USBKEYBOARD
    if(mouse0==false && Option.MOUSE_CLOCK)initMouse0(0);  //see if there is a mouse to initialise 
#endif
	nextstmt = ProgMemory;
}
void cmd_chain(void){
	do_chain(cmdline);
}

void cmd_select(void) {
    int i, type;
    unsigned char *p, *rp = NULL, *SaveCurrentLinePtr;
    void *v;
    MMFLOAT f = 0;
    long long int  i64 = 0;
    unsigned char s[STRINGSIZE];

    // these are the tokens that we will be searching for
    // they are cached the first time this command is called

    type = T_NOTYPE;
    v = DoExpression(cmdline, &type);                               // evaluate the select case value
    type = TypeMask(type);
    if(type & T_NBR) f = *(MMFLOAT *)v;
    if(type & T_INT) i64 = *(long long int  *)v;
    if(type & T_STR) Mstrcpy((unsigned char *)s, (unsigned char *)v);

    // now search through the program looking for a matching CASE statement
    // i tracks the nesting level of any nested SELECT CASE commands
    SaveCurrentLinePtr = CurrentLinePtr;                            // save where we are because we will have to fake CurrentLinePtr to get errors reported correctly
    i = 1; p = nextstmt;
    while(1) {
        p = GetNextCommand(p, &rp, (unsigned char *)"No matching END SELECT");
        CommandToken tkn=commandtbl_decode(p);

        if(tkn == cmdSELECT_CASE) i++;                                  // found a nested SELECT CASE command, increase the nested count and carry on searching
        // is this a CASE stmt at the same level as this SELECT CASE.
        if(tkn == cmdCASE && i == 1) {
            int t;
            MMFLOAT ft, ftt;
            long long int  i64t, i64tt;
            unsigned char *st, *stt;

            CurrentLinePtr = rp;                                    // and report errors at the line we are on
			p++; //step past rest of command token
            // loop through the comparison elements on the CASE line.  Each element is separated by a comma
            do {
                p++;
                skipspace(p);
                t = type;
                // check for CASE IS,  eg  CASE IS > 5  -or-  CASE > 5  and process it if it is
                // an operator can be >, <>, etc but it can also be a prefix + or - so we must not catch them
                if((SaveCurrentLinePtr = checkstring(p, (unsigned char *)"IS")) || ((tokentype(*p) & T_OPER) && !(*p == GetTokenValue((unsigned char *)"+") || *p == GetTokenValue((unsigned char *)"-")))) {
                    int o;
                    if(SaveCurrentLinePtr) p += 2;
                    skipspace(p);
                    if(tokentype(*p) & T_OPER)
                        o = *p++ - C_BASETOKEN;                     // get the operator
                    else
                        error("Syntax");
                    if(type & T_NBR) ft = f;
                    if(type & T_INT) i64t = i64;
                    if(type & T_STR) st = s;
                    while(o != E_END) p = doexpr(p, &ft, &i64t, &st, &o, &t); // get the right hand side of the expression and evaluate the operator in o
                    if(!(t & T_INT)) error("Syntax");     // comparisons must always return an integer
                    if(i64t) {                                      // evaluates to true
                        skipelement(p);
                        nextstmt = p;
                        CurrentLinePtr = SaveCurrentLinePtr;
                        return;                                     // if we have a match just return to the interpreter and let it execute the code
                    } else {                                        // evaluates to false
                        skipspace(p);
                        continue;
                    }
                }

                // it must be either a single value (eg, "foo") or a range (eg, "foo" TO "zoo")
                // evaluate the first value
                p = evaluate(p, &ft, &i64t, &st, &t, true);
                skipspace(p);
                if(*p == tokenTO) {                      // is there is a TO keyword?
                    p++;
                    t = type;
                    p = evaluate(p, &ftt, &i64tt, &stt, &t, false); // evaluate the right hand side of the TO expression
                    if(((type & T_NBR) && f >= ft && f <= ftt) || ((type & T_INT) && i64 >= i64t && i64 <= i64tt) || (((type & T_STR) && Mstrcmp(s, st) >= 0) && (Mstrcmp(s, stt) <= 0))) {
                        skipelement(p);
                        nextstmt = p;
                        CurrentLinePtr = SaveCurrentLinePtr;
                        return;                                     // if we have a match just return to the interpreter and let it execute the code
                    } else {
                        skipspace(p);
                        continue;                                   // otherwise continue searching
                    }
                }

                // if we got to here the element must be just a single match.  So make the test
                if(((type & T_NBR) && f == ft) ||  ((type & T_INT) && i64 == i64t) ||  ((type & T_STR) && Mstrcmp(s, st) == 0)) {
                    skipelement(p);
                    nextstmt = p;
                    CurrentLinePtr = SaveCurrentLinePtr;
                    return;                                         // if we have a match just return to the interpreter and let it execute the code
                }
                skipspace(p);
            } while(*p == ',');                                     // keep looping through the elements on the CASE line
            checkend(p);
            CurrentLinePtr = SaveCurrentLinePtr;
        }

        // test if we have found a CASE ELSE statement at the same level as this SELECT CASE
        // if true it means that we did not find a matching CASE - so execute this code
        if(tkn == cmdCASE_ELSE && i == 1) {
            p+=sizeof(CommandToken);                                                    // step over the token
            checkend(p);
            skipelement(p);
            nextstmt = p;
            CurrentLinePtr = SaveCurrentLinePtr;
            return;
        }

        if(tkn == cmdEND_SELECT) {i--;  p++;}                             // found an END SELECT so decrement our nested counter

        if(i == 0) {
            // found our matching END SELECT stmt.  Step over it and continue with the statement after it
            skipelement(p);
            nextstmt = p;
            CurrentLinePtr = SaveCurrentLinePtr;
            return;
        }
    }
}


// if we have hit a CASE or CASE ELSE we must search for a END SELECT at this level and resume at that point
void cmd_case(void) {
    int i;
    unsigned char *p;

    // search through the program looking for a END SELECT statement
    // i tracks the nesting level of any nested SELECT CASE commands
    i = 1; p = nextstmt;
    while(1) {
        p = GetNextCommand(p, NULL, (unsigned char *)"No matching END SELECT");
        CommandToken tkn=commandtbl_decode(p);
        if(tkn == cmdSELECT_CASE) i++;                               // found a nested SELECT CASE command, we now need to search for its END CASE
        if(tkn == cmdEND_SELECT) i--;                                // found an END SELECT so decrement our nested counter
        if(i == 0) {
            // found our matching END SELECT stmt.  Step over it and continue with the statement after it
            skipelement(p);
            nextstmt = p;
            break;
        }
    }
}


void cmd_input(void) {
	unsigned char s[STRINGSIZE];
	unsigned char *p, *sp, *tp;
	int i, fnbr;
	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",;");				// this is a macro and must be the first executable stmt

	// is the first argument a file number specifier?  If so, get it
	if(argc >= 3 && *argv[0] == '#') {
		argv[0]++;
		fnbr = getinteger(argv[0]);
		i = 2;
	}
	else {
		fnbr = 0;
		// is the first argument a prompt?
		// if so, print it followed by an optional question mark
		if(argc >= 3 && *argv[0] == '"' && (*argv[1] == ',' || *argv[1] == ';')) {
			*(argv[0] + strlen((char *)argv[0]) - 1) = 0;
			argv[0]++;
			MMPrintString((char *)argv[0]);
			if(*argv[1] == ';') MMPrintString((char *)"? ");
			i = 2;
		} else {
			MMPrintString((char *)"? ");									// no prompt?  then just print the question mark
			i = 0;
		}
	}

	if(argc - i < 1) error("Syntax");						// no variable to input to
	*inpbuf = 0;													// start with an empty buffer
	MMgetline(fnbr, (char *)inpbuf);									    // get the line
	p = inpbuf;

	// step through the variables listed for the input statement
	// and find the next item on the line and assign it to the variable
	for(; i < argc; i++) {
		sp = s;														// sp is a temp pointer into s[]
		if(*argv[i] == ',' || *argv[i] == ';') continue;
		skipspace(p);
		if(*p != 0) {
			if(*p == '"') {											// if it is a quoted string
				p++;												// step over the quote
				while(*p && *p != '"')  *sp++ = *p++;				// and copy everything upto the next quote
				while(*p && *p != ',') p++;							// then find the next comma
			} else {												// otherwise it is a normal string of characters
				while(*p && *p != ',') *sp++ = *p++;				// copy up to the comma
				while(sp > s && sp[-1] == ' ') sp--;				// and trim trailing whitespace
			}
		}
		*sp = 0;													// terminate the string
		tp = findvar(argv[i], V_FIND);								// get the variable and save its new value
        if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
		if(g_vartbl[g_VarIndex].type & T_STR) {
    		if(strlen((char *)s) > g_vartbl[g_VarIndex].size) error("String too long");
			strcpy((char *)tp, (char *)s);
			CtoM(tp);												// convert to a MMBasic string
		} else
		if(g_vartbl[g_VarIndex].type & T_INT) {
    		*((long long int  *)tp) = strtoll((char *)s, (char **)&sp, 10);			// convert to an integer
		}
		else
			*((MMFLOAT *)tp) = (MMFLOAT)atof((char *)s);
		if(*p == ',') p++;
	}
}


void MIPS16 cmd_trace(void) {
    if(checkstring(cmdline, (unsigned char *)"ON"))
    	TraceOn = true;
    else if(checkstring(cmdline, (unsigned char *)"OFF"))
        TraceOn = false;
    else if(checkstring(cmdline, (unsigned char *)"LIST")) {
        int i;
        cmdline += 4;
        skipspace(cmdline);
        if(*cmdline == 0 || *cmdline ==(unsigned char)'\'')  //'
        	i = TRACE_BUFF_SIZE - 1;
        else
        	i = getint(cmdline, 0, TRACE_BUFF_SIZE - 1);
        i = TraceBuffIndex - i;
        if(i < 0) i += TRACE_BUFF_SIZE;
        while(i != TraceBuffIndex) {
			if(TraceBuff[i] >= ProgMemory && TraceBuff[i] <= ProgMemory+MAX_PROG_SIZE){
           		 inpbuf[0] = '[';
            	IntToStr((char *)inpbuf + 1, CountLines(TraceBuff[i]), 10);
            	strcat((char *)inpbuf, "]");
			}else if(TraceBuff[i]){
                strcpy((char *)inpbuf, "[Lib]");	
			}else{
			    inpbuf[0] = 0;
			}	
            MMPrintString((char *)inpbuf);
            if(++i >= TRACE_BUFF_SIZE) i = 0;
        }
    }
    else error("Unknown command");
}



// FOR command
#ifndef PICOMITE
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_for)(void) {
#else
void cmd_for(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_for)(void) {
#endif
	int i, t, vlen, test;
	unsigned char ss[4];														// this will be used to split up the argument line
	unsigned char *p, *tp, *xp;
	void *vptr;
	unsigned char *vname, vtype;
//	static unsigned char fortoken, nexttoken;

    // cache these tokens for speed
//	if(!fortoken) fortoken = GetCommandValue((unsigned char *)"For");
//	if(!nexttoken) nexttoken = GetCommandValue((unsigned char *)"Next");

	ss[0] = tokenEQUAL;
	ss[1] = tokenTO;
	ss[2] = tokenSTEP;
	ss[3] = 0;
	{																// start a new block
		getargs(&cmdline, 7, ss);									// getargs macro must be the first executable stmt in a block
		if(argc < 5 || argc == 6 || *argv[1] != ss[0] || *argv[3] != ss[1]) error("FOR with misplaced = or TO");
		if(argc == 6 || (argc == 7 && *argv[5] != ss[2])) error("Syntax");

		// get the variable name and trim any spaces
		vname = argv[0];
		if(*vname && *vname == ' ') vname++;
		while(*vname && vname[strlen((char *)vname) - 1] == ' ') vname[strlen((char *)vname) - 1] = 0;
		vlen = strlen((char *)vname);
		vptr = findvar(argv[0], V_FIND);					        // create the variable
        if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
        vtype = TypeMask(g_vartbl[g_VarIndex].type);
		if(vtype & T_STR) error("Invalid variable");                // sanity check

		// check if the FOR variable is already in the stack and remove it if it is
		// this is necessary as the program can jump out of the loop without hitting
		// the NEXT statement and this will eventually result in a stack overflow
		for(i = 0; i < g_forindex ;i++) {
			if(g_forstack[i].var == vptr && g_forstack[i].level == g_LocalIndex) {
				while(i < g_forindex - 1) {
					g_forstack[i].forptr = g_forstack[i+1].forptr;
					g_forstack[i].nextptr = g_forstack[i+1].nextptr;
					g_forstack[i].var = g_forstack[i+1].var;
					g_forstack[i].vartype = g_forstack[i+1].vartype;
					g_forstack[i].level = g_forstack[i+1].level;
					g_forstack[i].tovalue.i = g_forstack[i+1].tovalue.i;
					g_forstack[i].stepvalue.i = g_forstack[i+1].stepvalue.i;
					i++;
				}
				g_forindex--;
				break;
			}
		}

        if(g_forindex == MAXFORLOOPS) error("Too many nested FOR loops");

		g_forstack[g_forindex].var = vptr;								// save the variable index
		g_forstack[g_forindex].vartype = vtype;							// save the type of the variable
		g_forstack[g_forindex].level = g_LocalIndex;						// save the level of the variable in terms of sub/funs
        g_forindex++;                                                 // incase functions use for loops
        if(vtype & T_NBR) {
            *(MMFLOAT *)vptr = getnumber(argv[2]);					// get the starting value for a float and save
            g_forstack[g_forindex - 1].tovalue.f = getnumber(argv[4]);		// get the to value and save
            if(argc == 7)
                g_forstack[g_forindex - 1].stepvalue.f = getnumber(argv[6]);// get the step value for a float and save
            else
                g_forstack[g_forindex - 1].stepvalue.f = 1.0;				// default is +1
        } else {
            *(long long int  *)vptr = getinteger(argv[2]);			// get the starting value for an integer and save
            g_forstack[g_forindex - 1].tovalue.i = getinteger(argv[4]);		// get the to value and save
            if(argc == 7)
                g_forstack[g_forindex - 1].stepvalue.i = getinteger(argv[6]);// get the step value for an integer and save
            else
                g_forstack[g_forindex - 1].stepvalue.i = 1;					// default is +1
        }
        g_forindex--;

		g_forstack[g_forindex].forptr = nextstmt + 1;					// return to here when looping

		// now find the matching NEXT command
        t = 1; p = nextstmt;
        while(1) {
              p = GetNextCommand(p, &tp, (unsigned char *)"No matching NEXT");
//            if(*p == fortoken) t++;                                 // count the FOR
//            if(*p == nexttoken) {                                   // is it NEXT
        	CommandToken tkn=commandtbl_decode(p);

            if(tkn == cmdFOR) t++;                                 // count the FOR

            if(tkn == cmdNEXT) {                                   // is it NEXT
				xp = p + sizeof(CommandToken);											// point to after the NEXT token
				while(*xp && mystrncasecmp(xp, vname, vlen)) xp++;	// step through looking for our variable
				if(*xp && !isnamechar(xp[vlen]))					// is it terminated correctly?
					t = 0;											// yes, found the matching NEXT
				else
					t--;											// no luck, just decrement our stack counter
			}
			if(t == 0) {											// found the matching NEXT
				g_forstack[g_forindex].nextptr = p;					// pointer to the start of the NEXT command
				break;
			}
		}

        // test the loop value at the start
        if(g_forstack[g_forindex].vartype & T_INT)
            test = (g_forstack[g_forindex].stepvalue.i >= 0 && *(long long int  *)vptr > g_forstack[g_forindex].tovalue.i) || (g_forstack[g_forindex].stepvalue.i < 0 && *(long long int  *)vptr < g_forstack[g_forindex].tovalue.i) ;
        else
            test = (g_forstack[g_forindex].stepvalue.f >= 0 && *(MMFLOAT *)vptr > g_forstack[g_forindex].tovalue.f) || (g_forstack[g_forindex].stepvalue.f < 0 && *(MMFLOAT *)vptr < g_forstack[g_forindex].tovalue.f) ;

        if(test) {
			// loop is invalid at the start, so go to the end of the NEXT command
			skipelement(p);            // find the command after the NEXT command
			nextstmt = p;              // this is where we will continue
		} else {
			g_forindex++;					// save the loop data and continue on with the command after the FOR statement
        }
	}
}



#ifndef PICOMITE
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_next)(void) {
#else
void cmd_next(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_next)(void) {
#endif
	int i, vindex, test;
	void *vtbl[MAXFORLOOPS];
	int vcnt;
	unsigned char *p;
	getargs(&cmdline, MAXFORLOOPS * 2, (unsigned char *)",");						// getargs macro must be the first executable stmt in a block

	vindex = 0;														// keep lint happy

	for(vcnt = i = 0; i < argc; i++) {
		if(i & 0x01) {
			if(*argv[i] != ',') error("Syntax");
		} else
			vtbl[vcnt++] = findvar(argv[i], V_FIND | V_NOFIND_ERR); // find the variable and error if not found
	}

	loopback:
	// first search the for stack for a loop with the same variable specified on the NEXT's line
	if(vcnt) {
		for(i = g_forindex - 1; i >= 0; i--)
			for(vindex = vcnt - 1; vindex >= 0 ; vindex--)
				if(g_forstack[i].var == vtbl[vindex])
					goto breakout;
	} else {
		// if no variables specified search the for stack looking for an entry with the same program position as
		// this NEXT statement. This cheats by using the cmdline as an identifier and may not work inside an IF THEN ELSE
        for(i = 0; i < g_forindex; i++) {
            p = g_forstack[i].nextptr + sizeof(CommandToken);
            skipspace(p);
            if(p == cmdline) goto breakout;
        }
	}

	error("Cannot find a matching FOR");

	breakout:

	// found a match
	// apply the STEP value to the variable and test against the TO value
    if(g_forstack[i].vartype & T_INT) {
        *(long long int  *)g_forstack[i].var += g_forstack[i].stepvalue.i;
	    test = (g_forstack[i].stepvalue.i >= 0 && *(long long int  *)g_forstack[i].var > g_forstack[i].tovalue.i) || (g_forstack[i].stepvalue.i < 0 && *(long long int  *)g_forstack[i].var < g_forstack[i].tovalue.i) ;
    } else {
        *(MMFLOAT *)g_forstack[i].var += g_forstack[i].stepvalue.f;
	    test = (g_forstack[i].stepvalue.f >= 0 && *(MMFLOAT *)g_forstack[i].var > g_forstack[i].tovalue.f) || (g_forstack[i].stepvalue.f < 0 && *(MMFLOAT *)g_forstack[i].var < g_forstack[i].tovalue.f) ;
    }

    if(test) {
		// the loop has terminated
		// remove the entry in the table, then skip forward to the next element and continue on from there
		while(i < g_forindex - 1) {
			g_forstack[i].forptr = g_forstack[i+1].forptr;
			g_forstack[i].nextptr = g_forstack[i+1].nextptr;
			g_forstack[i].var = g_forstack[i+1].var;
			g_forstack[i].vartype = g_forstack[i+1].vartype;
			g_forstack[i].level = g_forstack[i+1].level;
			g_forstack[i].tovalue.i = g_forstack[i+1].tovalue.i;
			g_forstack[i].stepvalue.i = g_forstack[i+1].stepvalue.i;
			i++;
		}
		g_forindex--;
		if(vcnt > 0) {
			// remove that entry from our FOR stack
			for(; vindex < vcnt - 1; vindex++) vtbl[vindex] = vtbl[vindex + 1];
			vcnt--;
			if(vcnt > 0)
				goto loopback;
			else
				return;
		}

	} else {
		// we have not reached the terminal value yet, so go back and loop again
		nextstmt = g_forstack[i].forptr;
	}
}




#ifndef PICOMITE
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_do)(void) {
#else
void cmd_do(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_do)(void) {
#endif
	int i;
	unsigned char *p, *tp, *evalp;
    if(cmdtoken==cmdWHILE)error("Unknown command");
	// if it is a DO loop find the WHILE token and (if found) get a pointer to its expression
	while(*cmdline && *cmdline != tokenWHILE && *cmdline != tokenUNTIL) cmdline++;
	if(*cmdline == tokenUNTIL)error("Syntax");
	if(*cmdline == tokenWHILE) {
			evalp = ++cmdline;
		}
		else
			evalp = NULL;
	// check if this loop is already in the stack and remove it if it is
	// this is necessary as the program can jump out of the loop without hitting
	// the LOOP or WEND stmt and this will eventually result in a stack overflow
	for(i = 0; i < g_doindex ;i++) {
		if(g_dostack[i].doptr == nextstmt) {
			while(i < g_doindex - 1) {
				g_dostack[i].evalptr = g_dostack[i+1].evalptr;
				g_dostack[i].loopptr = g_dostack[i+1].loopptr;
				g_dostack[i].doptr = g_dostack[i+1].doptr;
				g_dostack[i].level = g_dostack[i+1].level;
				i++;
			}
			g_doindex--;
			break;
		}
	}

	// add our pointers to the top of the stack
	if(g_doindex == MAXDOLOOPS) error("Too many nested DO or WHILE loops");
	g_dostack[g_doindex].evalptr = evalp;
	g_dostack[g_doindex].doptr = nextstmt;
	g_dostack[g_doindex].level = g_LocalIndex;

	// now find the matching LOOP command
	i = 1; p = nextstmt;
	while(1) {
        p = GetNextCommand(p, &tp, (unsigned char *)"No matching LOOP");
        CommandToken tkn=commandtbl_decode(p);
		if(tkn == cmdtoken) i++;                                     // entered a nested DO or WHILE loop
		if(tkn == cmdLOOP) i--;									// exited a nested loop

		if(i == 0) {												// found our matching LOOP or WEND stmt
			g_dostack[g_doindex].loopptr = p;
			break;
		}
	}

    if(g_dostack[g_doindex].evalptr != NULL) {
		// if this is a DO WHILE ... LOOP statement
		// search the LOOP statement for a WHILE or UNTIL token (p is pointing to the matching LOOP statement)
		p+=sizeof(CommandToken);
		while(*p && *p < 0x80) p++;
		if(*p == tokenWHILE) error("LOOP has a WHILE test");
		if(*p == tokenUNTIL) error("LOOP has an UNTIL test");
	}

	g_doindex++;

    // do the evaluation (if there is something to evaluate) and if false go straight to the command after the LOOP or WEND statement
    if(g_dostack[g_doindex - 1].evalptr != NULL && getnumber(g_dostack[g_doindex - 1].evalptr) == 0) {
        g_doindex--;                                                  // remove the entry in the table
        nextstmt = g_dostack[g_doindex].loopptr;                        // point to the LOOP or WEND statement
        skipelement(nextstmt);                                      // skip to the next command
    }

}




#ifdef PICOMITEWEB
#ifdef rp2350
void MIPS16 __not_in_flash_func(cmd_loop)(void) {
#else
void cmd_loop(void) {
#endif
#else
void MIPS16 __not_in_flash_func(cmd_loop)(void) {
#endif
    unsigned char *p;
	int tst = 0;                                                    // initialise tst to stop the compiler from complaining
	int i;

	// search the do table looking for an entry with the same program position as this LOOP statement
	for(i = 0; i < g_doindex ;i++) {
        p = g_dostack[i].loopptr + sizeof(CommandToken);
        skipspace(p);
        if(p == cmdline) {
			// found a match
			// first check if the DO statement had a WHILE component
			// if not find the WHILE statement here and evaluate it
			if(g_dostack[i].evalptr == NULL) {						// if it was a DO without a WHILE
				if(*cmdline >= 0x80) {								// if there is something
					if(*cmdline == tokenWHILE)
						tst = (getnumber(++cmdline) != 0);			// evaluate the expression
					else if(*cmdline == tokenUNTIL)
						tst = (getnumber(++cmdline) == 0);			// evaluate the expression
					else
						error("Syntax");
				}
				else {
					tst = 1;										// and loop forever
					checkend(cmdline);								// make sure that there is nothing else
				}
			}
			else {													// if was DO WHILE
				tst = (getnumber(g_dostack[i].evalptr) != 0);			// evaluate its expression
				checkend(cmdline);									// make sure that there is nothing else
			}

			// test the expression value and reset the program pointer if we are still looping
			// otherwise remove this entry from the do stack
			if(tst)
				nextstmt = g_dostack[i].doptr;						// loop again
			else {
				// the loop has terminated
				// remove the entry in the table, then just let the default nextstmt run and continue on from there
                g_doindex = i;
				// just let the default nextstmt run
			}
			return;
		}
	}
	error("LOOP without a matching DO");
}



void cmd_exitfor(void) {
	if(g_forindex == 0) error("No FOR loop is in effect");
	nextstmt = g_forstack[--g_forindex].nextptr;
	checkend(cmdline);
	skipelement(nextstmt);
}



void cmd_exit(void) {
	if(g_doindex == 0) error("No DO loop is in effect");
	nextstmt = g_dostack[--g_doindex].loopptr;
	checkend(cmdline);
	skipelement(nextstmt);
}
 


/*void cmd_error(void) {
	unsigned char *s;
	if(*cmdline && *cmdline != '\'') {
		s = getCstring(cmdline);
		char *p=GetTempMemory(STRINGSIZE);
		strcpy(p,"[");
		int ln=CountLines(CurrentLinePtr);
		IntToStr(&p[1],ln,10);
		SaveCurrentLinePtr=CurrentLinePtr;
		CurrentLinePtr = NULL;                                      // suppress printing the line that caused the issue
		strcat((char *)p,"] ");
		strcat((char *)p,(char *)s);
		error(p);
	}
	else
		error("");
}*/
void cmd_error(void) {
	unsigned char *s;
	if(*cmdline && *cmdline != '\'') {
		s = getCstring(cmdline);
		// CurrentLinePtr = NULL;                                      // suppress printing the line that caused the issue
		error((char *) s);
	}
	else
		error("");
}


#ifndef rp2350
	void cmd_randomize(void) {
	int i;
	getargs(&cmdline,1,(unsigned char *)",");
	if(argc==1)i = getinteger(argv[0]);
	else i=time_us_32();
	if(i < 0) error("Number out of bounds");
	srand(i);
}
#endif

// this is the Sub or Fun command
// it simply skips over text until it finds the end of it
void cmd_subfun(void) {
	unsigned char *p;
	unsigned short returntoken, errtoken;

    if(gosubindex != 0) error("No matching END declaration");       // we have hit a SUB/FUN while in another SUB or FUN
	if(cmdtoken == cmdSUB) {
	    returntoken = cmdENDSUB;
	    errtoken = cmdENDFUNCTION;
	} else {
	    returntoken = cmdENDFUNCTION;
	    errtoken = cmdENDSUB;
    }
	p = nextstmt;
	while(1) {
        p = GetNextCommand(p, NULL, (unsigned char *)"No matching END declaration");
        CommandToken tkn=commandtbl_decode(p);
        if(tkn == cmdSUB || tkn == cmdFUN || tkn == errtoken) error("No matching END declaration");
		if(tkn == returntoken) {                                     // found the next return
    		skipelement(p);
    		nextstmt = p;                                           // point to the next command
    		break;
        }
    }
}
// this is the Sub or Fun command
// it simply skips over text until it finds the end of it
void cmd_comment(void) {
	unsigned char *p;
	unsigned short returntoken;

	returntoken = GetCommandValue((unsigned char *)"*/");
//	errtoken = cmdENDSUB;
	p = nextstmt;
	while(1) {
        p = GetNextCommand(p, NULL, (unsigned char *)"No matching END declaration");
        CommandToken tkn=commandtbl_decode(p);
        if(tkn == cmdComment) error("No matching END declaration");
		if(tkn == returntoken) {                                     // found the next return
    		skipelement(p);
    		nextstmt = p;                                           // point to the next command
    		break;
        }
    }
}
void cmd_endcomment(void){

}


void cmd_gosub(void) {
   if(gosubindex >= MAXGOSUB) error("Too many nested GOSUB");
   char *return_to = (char *)nextstmt;
   if(isnamestart(*cmdline))
       nextstmt = findlabel(cmdline);
   else
       nextstmt = findline(getinteger(cmdline), true);
   IgnorePIN = false;

   errorstack[gosubindex] = CurrentLinePtr;
   gosubstack[gosubindex++] = (unsigned char *)return_to;
   g_LocalIndex++;
   CurrentLinePtr = nextstmt;
}

void cmd_mid(void){
	unsigned char *p;
	getargs(&cmdline,5,(unsigned char *)",");
	findvar(argv[0], V_NOFIND_ERR);
    if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
	if(!(g_vartbl[g_VarIndex].type & T_STR)) error("Not a string");
	int size=g_vartbl[g_VarIndex].size;
	char *sourcestring=(char *)getstring(argv[0]);
	int start=getint(argv[2],1,sourcestring[0]);
	int num=0;
	if(argc==5)num=getint(argv[4],1,sourcestring[0]);
	if(start+num-1>sourcestring[0])error("Selection exceeds length of string");
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
	if(!*cmdline) error("Syntax");
	char *value = (char *)getstring(cmdline);
	if(num==0)num=value[0];
	p=(unsigned char *)&value[1];
	if(num==value[0]) memcpy(&sourcestring[start],p,num);
	else {
		int change=value[0]-num;
		if(sourcestring[0]+change>size)error("String too long");
		memmove(&sourcestring[start+value[0]],&sourcestring[start+num],sourcestring[0]-(start+num-1));
		sourcestring[0]+=change;
		memcpy(&sourcestring[start],p,value[0]);
	}
}
void cmd_byte(void){
	getargs(&cmdline,3,(unsigned char *)",");
	findvar(argv[0], V_NOFIND_ERR);
    if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
	if(!(g_vartbl[g_VarIndex].type & T_STR)) error("Not a string");
	unsigned char *sourcestring=(unsigned char *)getstring(argv[0]);
	int start=getint(argv[2],1,sourcestring[0]);
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
	if(!*cmdline) error("Syntax");
	int value = getint(cmdline,0,255);
	sourcestring[start]=value;
}
void cmd_bit(void){
	getargs(&cmdline,3,(unsigned char *)",");
	uint64_t *source=(uint64_t *)findvar(argv[0], V_NOFIND_ERR);
    if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
	if(!(g_vartbl[g_VarIndex].type & T_INT)) error("Not an integer");
	uint64_t bit=(uint64_t)1<<(uint64_t)getint(argv[2],0,63);
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
	if(!*cmdline) error("Syntax");
	int value = getint(cmdline,0,1);
	if(value)*source|=bit;
	else *source&=(~bit);
}
void cmd_flags(void) {
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	g_flag=getinteger(++cmdline);
}
  
void cmd_flag(void){
	getargs(&cmdline,1,(unsigned char *)",");
	uint64_t bit=(uint64_t)1<<(uint64_t)getint(argv[0],0,63);
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
	if(!*cmdline) error("Syntax");
	int value = getint(cmdline,0,1);
	if(value)g_flag |=bit;
	else g_flag &=~bit;
}

void MIPS16 __not_in_flash_func(cmd_return)(void) {
 	checkend(cmdline);
	if(gosubindex == 0 || gosubstack[gosubindex - 1] == NULL) error("Nothing to return to");
    ClearVars(g_LocalIndex--, true);                                        // delete any local variables
    g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
	nextstmt = gosubstack[--gosubindex];                            // return to the caller
    CurrentLinePtr = errorstack[gosubindex];
}
/*frame
#define c_topleft  218
#define c_topright 191
#define c_bottomleft 192
#define c_bottomright 217
#define c_horizontal 196
#define c_vertical 179
#define c_cross 197
#define c_tup 193
#define c_tdown 194
#define c_tleft 195
#define c_tright 180
#define c_d_topleft  201
#define c_d_topright 187
#define c_d_bottomleft 200
#define c_d_bottomright 188
#define c_d_horizontal 205
#define c_d_vertical 186
#define c_d_cross 206
#define c_d_tup 202
#define c_d_tdown 203
#define c_d_tleft 204
#define c_d_tright 185


extern const int colours[16];
extern void setterminal(void);
unsigned short *frame=NULL, *outframe=NULL;
bool framecursor=true;
static int framex=0,framey=0;
static inline void framewritechar(int x, int y, uint8_t ascii, uint8_t fcolour, uint8_t attributes){
	if(x>=framex || y>=framey)return;
	frame[(y*framex)+x]=ascii | (fcolour<<8) | (attributes<<12);
}
static inline uint16_t framegetchar(int x, int y ){
	return frame[(y*framex)+x];
}
static void SCursor(int x, int y) {
    char s[30];
	ShowCursor(0);
	CurrentX=x*gui_font_width;
	CurrentY=y*gui_font_height;
	sprintf(s,"\033[%d;%dH",y+1,x+1);
	SSPrintString(s);
	ShowCursor(framecursor);
}
static void SColour(int colour, int fore){
    char s[24]={0};
	int r=colour>>16;
	int g=(colour>>8) & 0xFF;
	int b=colour & 0xff;
	if(fore){
		strcpy(s,"\033[38;2;");
		gui_fcolour=colour;
	} else {
		strcpy(s,"\033[48;2;");
		gui_bcolour=colour;
	}
	sprintf(&s[7],"%d;%d;%dm",r,g,b);
	SSPrintString(s);
}

void cmd_frame(void){
	unsigned char *p=NULL;
	if((p=checkstring(cmdline,(unsigned char *)"CREATE"))){
		if(frame)error("Frame already exists");
		framex=HRes/gui_font_width;
		framey=VRes/gui_font_height;
		frame=(uint16_t *)GetMemory(framex*framey*sizeof(uint16_t));
		outframe=(uint16_t *)GetMemory(framex*framey*sizeof(uint16_t));
		for(int i=0;i<framex*framey;i++)outframe[i]=0xFFFF;
		Option.Width=framex;
		Option.Height=framey;
		char sp[20]={0};
		strcpy(sp,"\033[8;");
		IntToStr(&sp[strlen(sp)],framey,10);
		strcat(sp,";");
		IntToStr(&sp[strlen(sp)],framex+1,10);
		strcat(sp,"t");
		SSPrintString(sp);	
		SSPrintString("\0337\033[2J\033[H");                            // vt100 clear screen and home cursor
		ClearScreen(gui_bcolour);					//
		return;
	} 
	if(!frame)error("Frame not created");
	if((p=checkstring(cmdline,(unsigned char *)"CURSOR"))){
		if(checkstring(p,(unsigned char *)"ON")){
			framecursor=true;
			SSPrintString("\033[?25h");
		} else if(checkstring(p,(unsigned char *)"OFF")){
			ShowCursor(0);
			framecursor=false;
			SSPrintString("\033[?25l");
		} else {
			getargs(&p,3,(unsigned char *)",");
			if(argc<3)error("Syntax");
			int x=getint(argv[0],0,framex-1);
			int y=getint(argv[2],0,framey-1);
			if(DISPLAY_TYPE==SCREENMODE1)tilefcols[y*X_TILE+x]=RGB121pack(gui_fcolour);
			SCursor(x,y);
		}
	} else if((p=checkstring(cmdline,(unsigned char *)"BOX"))){
		int fc=gui_fcolour;
		bool dual=false;
		getargs(&p,11,(unsigned char *)",");
		if(argc<7)error("Syntax");
		int x=getint(argv[0],0,framex-1);
		int y=getint(argv[2],0,framey-1);
		int w=getint(argv[4],1,framex-1-x);
		int h=getint(argv[6],1,framey-1-y);
		if(argc>=9 && *argv[8])fc=getint(argv[8],0,WHITE);
		if(argc==11){
			if(checkstring(argv[10],(unsigned char *)"DOUBLE"))dual=true;
		}
		fc=RGB121(fc);
		framewritechar(x,y,dual ? c_d_topleft : c_topleft,fc,0);
		framewritechar(x+w,y,dual ? c_d_topright : c_topright,fc,0);
		framewritechar(x+w,y+h,dual ? c_d_bottomright : c_bottomright,fc,0);
		framewritechar(x,y+h,dual ? c_d_bottomleft : c_bottomleft,fc,0);
		for(int i=x+1;i<x+w;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_horizontal,fc,0);
		}
		for(int i=y+1;i<y+h;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_vertical,fc,0);
		}
		SCursor(1,1);
	} else if((p=checkstring(cmdline,(unsigned char *)"QBOX"))){
		int fc=gui_fcolour;
		bool dual=false;
		getargs(&p,15,(unsigned char *)",");
		if(argc<9)error("Syntax");
		int x=getint(argv[0],0,framex-1);
		int y=getint(argv[2],0,framey-1);
		int w=getint(argv[4],1,framex-1-x);
		int w2=getint(argv[6],1,framex-1-x-w);
		int h=getint(argv[8],1,framey-1-y);
		int h2=getint(argv[10],1,framey-1-y-h);
		if(argc>=13 && *argv[12])fc=getint(argv[12],0,WHITE);
		if(argc==15){
			if(checkstring(argv[14],(unsigned char *)"DOUBLE"))dual=true;
		}
		fc=RGB121(fc);
		framewritechar(x,y,dual ? c_d_topleft : c_topleft,fc,0);
		framewritechar(x+w,y,dual ? c_d_tdown : c_tdown,fc,0);
		framewritechar(x+w+w2,y,dual ? c_d_topright : c_topright,fc,0);
		framewritechar(x,y+h,dual ? c_d_tleft : c_tleft,fc,0);
		framewritechar(x+w,y+h,dual ? c_d_cross : c_cross,fc,0);
		framewritechar(x+w+w2,y+h,dual ? c_d_tright : c_tright,fc,0);
		framewritechar(x,y+h+h2,dual ? c_d_bottomleft : c_bottomleft,fc,0);
		framewritechar(x+w,y+h+h2,dual ? c_d_tup : c_tup,fc,0);
		framewritechar(x+w+w2,y+h+h2,dual ? c_d_bottomright : c_bottomright,fc,0);
		for(int i=x+1;i<x+w;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h+h2,dual ? c_d_horizontal : c_horizontal,fc,0);
		}
		for(int i=x+1+w;i<x+w+w2;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h+h2,dual ? c_d_horizontal : c_horizontal,fc,0);
		}
		for(int i=y+1;i<y+h;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w+w2,i,dual ? c_d_vertical : c_vertical,fc,0);
		}
		for(int i=y+1+h;i<y+h+h2;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w+w2,i,dual ? c_d_vertical : c_vertical,fc,0);
		}
		SCursor(1,1);
	} else if((p=checkstring(cmdline,(unsigned char *)"VBOX"))){
		int fc=gui_fcolour;
		bool dual=false;
		getargs(&p,13,(unsigned char *)",");
		if(argc<9)error("Syntax");
		int x=getint(argv[0],0,framex-1);
		int y=getint(argv[2],0,framey-1);
		int w=getint(argv[4],1,framex-1-x);
		int h=getint(argv[6],1,framey-1-y);
		int h2=getint(argv[8],1,framey-1-y-h);
		if(argc>=11 && *argv[10])fc=getint(argv[10],0,WHITE);
		if(argc==13){
			if(checkstring(argv[12],(unsigned char *)"DOUBLE"))dual=true;
		}
		fc=RGB121(fc);
		framewritechar(x,y,dual ? c_d_topleft : c_d_topleft,fc,0);
		framewritechar(x+w,y,dual ? c_d_topright : c_d_topright,fc,0);
		framewritechar(x+w,y+h,dual ? c_d_tright : c_d_tright,fc,0);
		framewritechar(x+w,y+h+h2,dual ? c_d_bottomright : c_d_bottomright,fc,0);
		framewritechar(x,y+h+h2,dual ? c_d_bottomleft : c_d_bottomleft,fc,0);
		framewritechar(x,y+h,dual ? c_d_tleft : c_d_tleft,fc,0);
		for(int i=x+1;i<x+w;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_d_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_d_horizontal,fc,0);
			framewritechar(i,y+h+h2,dual ? c_d_horizontal : c_d_horizontal,fc,0);
		}
		for(int i=y+1;i<y+h;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_d_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_d_vertical,fc,0);
		}
		for(int i=y+1+h;i<y+h+h2;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_d_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_d_vertical,fc,0);
		}
		SCursor(1,1);
	} else if((p=checkstring(cmdline,(unsigned char *)"HBOX"))){
		int fc=gui_fcolour;
		bool dual=false;
		getargs(&p,13,(unsigned char *)",");
		if(argc<9)error("Syntax");
		int x=getint(argv[0],0,framex-1);
		int y=getint(argv[2],0,framey-1);
		int w=getint(argv[4],1,framex-1-x);
		int w2=getint(argv[6],1,framex-1-x-w);
		int h=getint(argv[8],1,framey-1-y);
		if(argc>=11 && *argv[10])fc=getint(argv[10],0,WHITE);
		if(argc==13){
			if(checkstring(argv[12],(unsigned char *)"DOUBLE"))dual=true;
		}
		fc=RGB121(fc);
		framewritechar(x,y,dual ? c_d_topleft : c_topleft,fc,0);
		framewritechar(x+w,y,dual ? c_d_tdown : c_tdown,fc,0);
		framewritechar(x+w,y+h,dual ? c_d_tup : c_tup,fc,0);
		framewritechar(x+w+w2,y,dual ? c_d_topright : c_topright,fc,0);
		framewritechar(x+w+w2,y+h,dual ? c_d_bottomright : c_bottomright,fc,0);
		framewritechar(x,y+h,dual ? c_d_bottomleft : c_bottomleft,fc,0);
		for(int i=x+1;i<x+w;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_horizontal,fc,0);
		}
		for(int i=x+1+w;i<x+w+w2;i++){
			framewritechar(i,y,dual ? c_d_horizontal : c_horizontal,fc,0);
			framewritechar(i,y+h,dual ? c_d_horizontal : c_horizontal,fc,0);
		}
		for(int i=y+1;i<y+h;i++){
			framewritechar(x,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w,i,dual ? c_d_vertical : c_vertical,fc,0);
			framewritechar(x+w+w2,i,dual ? c_d_vertical : c_vertical,fc,0);
		}
		SCursor(1,1);
	} else if((p=checkstring(cmdline,(unsigned char *)"CLEAR"))){
		memset(frame,0,framex*framey*sizeof(uint16_t));
	} else if((p=checkstring(cmdline,(unsigned char *)"CLOSE"))){
		if(!frame)error("Frame does not exist");
		FreeMemorySafe((void **)&frame);
		FreeMemorySafe((void **)&outframe);
	} else if((p=checkstring(cmdline,(unsigned char *)"SCROLL"))){
		int xstart=0,ystart=0,xend=framex-1,yend=framey-1, xmax=framex-1, ymax=framey-1;
		getargs(&p,11,(unsigned char *)",");
//		if(argc<7)error("Syntax");
		int xs=getint(argv[0],-(xmax),xmax);
		int ys=getint(argv[2],-(ymax),ymax);
		if(argc>=5 && *argv[4])xstart=getint(argv[4],0,xmax);
		if(argc>=7 && *argv[6])ystart=getint(argv[6],0,ymax);
		if(argc>=9 && *argv[8])xend=getint(argv[8],1,xmax);
		if(argc==11)yend=getint(argv[10],1,ymax);
		SCursor(xstart,ystart);
		if(DISPLAY_TYPE==SCREENMODE1)tilefcols[ystart*X_TILE+xstart]=RGB;
		if(abs(xs)>=0){
			for(int y=ystart;y<=yend;y++){
				uint16_t *line=&frame[y*framex];
				if(xs>0){
					memmove((uint8_t*)&line[xstart+xs],(uint8_t*)&line[xstart],(xend-xstart-xs+1)*sizeof(uint16_t));
					memset((uint8_t*)&line[xstart],0,xs*sizeof(uint16_t));
				} else {
					memmove((uint8_t*)&line[xstart],(uint8_t*)&line[xstart-xs],(xend-xstart+xs+1)*sizeof(uint16_t));
					memset((uint8_t*)&line[xend+xs+1],0,abs(xs)*sizeof(uint16_t));
				}
			}
		}
		if(ys>0){
			uint16_t *line1=&frame[(yend)*framex+xstart];
			uint16_t *line2=&frame[(yend-ys)*framex+xstart];
			for(int y=yend;y>ystart-ys+1;y--){
				memcpy((uint8_t*)line1,(uint8_t*)line2,(xend-xstart+1)*sizeof(uint16_t));
				line1-=framex;
				line2-=framex;
			}
			line1=&frame[ystart*framex+xstart];
			for(int y=ystart;y<ystart+ys;y++){
				memset((uint8_t*)line1,0,(xend-xstart+1)*sizeof(uint16_t));
				line1+=framex;
			}
		} else if(ys<0){
			ys=-ys;
			uint16_t *line1=&frame[ystart*framex+xstart];
			uint16_t *line2=&frame[(ystart+ys)*framex+xstart];
			for(int y=ystart;y<=yend-ys;y++){
				memcpy((uint8_t*)line1,(uint8_t*)line2,(xend-xstart+1)*sizeof(uint16_t));
				line1+=framex;
				line2+=framex;
			}
			line1=&frame[(yend-ys+1)*framex+xstart];
			for(int y=yend-ys+1;y<=yend;y++){
				memset((uint8_t*)line1,0,(xend-xstart+1)*sizeof(uint16_t));
				line1+=framex;
			}
		}
	} else if((p=checkstring(cmdline,(unsigned char *)"WRITE"))){
		int savefcol=gui_fcolour;
		int lasty=-1,lastx=-1,lastc=-1;
		int sx=CurrentX/gui_font_width,sy=CurrentY/gui_font_height;
		int ccursor=framecursor;
		framecursor=0;
		ShowCursor(0);
		SColour(gui_bcolour,0);
		for(int y=0;y<framey;y++){
			for(int x=0;x<framex;x++){
				uint16_t c=framegetchar(x,y);
				if(c!=outframe[(y*framex)+x]){
					outframe[(y*framex)+x]=c;
					if(y!=lasty || x!=lastx){
						SCursor(x,y);
						lastx=x+1;
						lasty=y;
					} else lastx=x+1;
					int outc=colours[(c>>8) & 0xF];
					if(outc!=lastc){
						lastc=outc;
						SColour(outc,1);
					}
					if(c==0)c=' ';
					DisplayPutC(c&0xFF);
					SerialConsolePutC(c&0xFF,0);
				}
			}
		}
		fflush(stdout);
		gui_fcolour=savefcol;
		SCursor(sx,sy);
		framecursor=ccursor;
		ShowCursor(framecursor);
		if(DISPLAY_TYPE==SCREENMODE1)tilefcols[sy*X_TILE+sx]=Option.VGAFC;
		SColour(gui_fcolour,1);
	} else {
		int attributes=0, fc=gui_fcolour;
		getargs(&cmdline,9,(unsigned char *)",");
		if(argc<5)error("Syntax");
		int x=getint(argv[0],0,framex-1);
		int y=getint(argv[2],0,framey-1);
		p=getCstring(argv[4]);
		if(argc>=7 && *argv[6])fc=getint(argv[6],0,WHITE);
		if(argc==9)attributes=getint(argv[8],0,15);
		int l=strlen((char *)p);
		fc=RGB121(fc);
		while(l--){
			if(x==framex){y++;x=0;}
			if(y==framey)return;
			framewritechar(x++,y,*p++,fc,attributes);
		}
	}
}*/


void cmd_endfun(void) {
 	checkend(cmdline);
	if(gosubindex == 0 || gosubstack[gosubindex - 1] != NULL) error("Nothing to return to");
	nextstmt = (unsigned char *)"\0\0\0";                                            // now terminate this run of ExecuteProgram()
}



void MIPS16 cmd_read(void) {
    int i, j, k, len, card;
    unsigned char *p, *lineptr = NULL, *ptr;
	unsigned short  datatoken;
    int vcnt, vidx, num_to_read=0;
	if (checkstring(cmdline, (unsigned char*)"SAVE")) {
		if(restorepointer== MAXRESTORE - 1)error((char*)"Too many saves");
		datastore[restorepointer].SaveNextDataLine = NextDataLine;
		datastore[restorepointer].SaveNextData = NextData;
		restorepointer++;
		return;
	}
	if (checkstring(cmdline, (unsigned char*)"RESTORE")) {
		if (!restorepointer)error((char*)"Nothing to restore");
		restorepointer--;
		NextDataLine = datastore[restorepointer].SaveNextDataLine;
		NextData = datastore[restorepointer].SaveNextData;
		return;
	}
    getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");                // getargs macro must be the first executable stmt in a block
    if(argc == 0) error("Syntax");
	// first count the elements and do the syntax checking
    for(vcnt = i = 0; i < argc; i++) {
        if(i & 0x01) {
            if(*argv[i] != ',') error("Syntax");
        } else {
			findvar(argv[i], V_FIND | V_EMPTY_OK);
			if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
			card=1;
			if(emptyarray){ //empty array
				for(k=0;k<MAXDIM;k++){
					j=(g_vartbl[g_VarIndex].dims[k] - g_OptionBase + 1);
					if(j)card *= j;
				}
			}
			num_to_read+=card;
		}
	}
    char **vtbl=GetTempMemory(num_to_read * sizeof (char *));
    int *vtype=GetTempMemory(num_to_read * sizeof (int));
    int *vsize=GetTempMemory(num_to_read * sizeof (int));
    // step through the arguments and save the pointer and type
    for(vcnt = i = 0; i < argc; i+=2) {
		ptr = findvar(argv[i], V_FIND | V_EMPTY_OK);
		vtbl[vcnt] = (char *)ptr;
		card=1;
		if(emptyarray){ //empty array
			for(k=0;k<MAXDIM;k++){
				j=(g_vartbl[g_VarIndex].dims[k] - g_OptionBase + 1);
				if(j)card *= j;
			}
		}
		for(k=0;k<card;k++){
			if(k){
				if(g_vartbl[g_VarIndex].type & (T_INT | T_NBR))ptr+=8;
				else ptr+=g_vartbl[g_VarIndex].size+1;
				vtbl[vcnt]=(char *)ptr;
			}
			vtype[vcnt] = TypeMask(g_vartbl[g_VarIndex].type);
			vsize[vcnt] = g_vartbl[g_VarIndex].size;
			vcnt++;
		}
    }

    // setup for a search through the whole memory
    vidx = 0;
    datatoken = GetCommandValue((unsigned char *)"Data");
    p = lineptr = NextDataLine;
    if(*p == 0xff) error("No DATA to read");                        // error if there is no program

  // search looking for a DATA statement.  We keep returning to this point until all the data is found
search_again:
    while(1) {
        if(*p == 0) p++;                                            // if it is at the end of an element skip the zero marker
        if(*p == 0/* || *p == 0xff*/) error("No DATA to read");         // end of the program and we still need more data
        if(*p == T_NEWLINE) lineptr = p++;
        if(*p == T_LINENBR) p += 3;
        skipspace(p);
        if(*p == T_LABEL) {                                         // if there is a label here
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }
        CommandToken tkn=commandtbl_decode(p);
        if(tkn == datatoken) break;                                  // found a DATA statement
        while(*p) p++;                                              // look for the zero marking the start of the next element
    }
    NextDataLine = lineptr;
    p+=sizeof(CommandToken);                                                            // step over the token
    skipspace(p);
    if(!*p || *p == '\'') { CurrentLinePtr = lineptr; error("No DATA to read"); }

        // we have a DATA statement, first split the line into arguments
        {                                                           // new block, the getargs macro must be the first executable stmt in a block
        getargs(&p, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
        if((argc & 1) == 0) { CurrentLinePtr = lineptr; error("Syntax"); }
        // now step through the variables on the READ line and get their new values from the argument list
        // we set the line number to the number of the DATA stmt so that any errors are reported correctly
        while(vidx < vcnt) {
            // check that there is some data to read if not look for another DATA stmt
            if(NextData > argc) {
                skipline(p);
                NextData = 0;
                goto search_again;
            }
            CurrentLinePtr = lineptr;
            if(vtype[vidx] & T_STR) {
                char *p1, *p2;
                if(*argv[NextData] == '"') {                               // if quoted string
                  	int toggle=0;
                    for(len = 0, p1 = vtbl[vidx], p2 = (char *)argv[NextData] + 1; *p2 && *p2 != '"'; len++) {
                    	if(*p2=='\\' && p2[1]!='"' && OptionEscape)toggle^=1;
	                    if(toggle){
	                        if(*p2=='\\' && isdigit((unsigned char)p2[1]) && isdigit((unsigned char)p2[2]) && isdigit((unsigned char)p2[3])){
	                            p2++;
	                            i=(*p2++)-48;
	                            i*=10;
	                            i+=(*p2++)-48;
	                            i*=10;
	                            i+=(*p2++)-48;
                                if(i==0)error("Null character \\000 in escape sequence - use CHR$(0)","$");
	                            *p1++=i;
	                        } else {
	                            p2++;
	                            switch(*p2){
	                                case '\\':
	                                    *p1++='\\';
	                                    p2++;
	                                    break;
	                                case 'a':
	                                    *p1++='\a';
	                                    p2++;
	                                    break;
	                                case 'b':
	                                    *p1++='\b';
	                                    p2++;
	                                    break;
	                                case 'e':
	                                    *p1++='\e';
	                                    p2++;
	                                    break;
	                                case 'f':
	                                    *p1++='\f';
	                                    p2++;
	                                    break;
	                                case 'n':
	                                    *p1++='\n';
	                                    p2++;
	                                    break;
	                                case 'q':
	                                    *p1++='\"';
	                                    p2++;
	                                    break;
	                                case 'r':
	                                    *p1++='\r';
	                                    p2++;
	                                    break;
	                                case 't':
	                                    *p1++='\t';
	                                    p2++;
	                                    break;
	                                case 'v':
	                                    *p1++='\v';
	                                    p2++;
	                                    break;
	                                case '&':
	                                    p2++;
	                                    if(isxdigit((unsigned char)*p2) && isxdigit((unsigned char)p2[1])){
	                                        i=0;
	                                        i = (i << 4) | ((mytoupper(*p2) >= 'A') ? mytoupper(*p2) - 'A' + 10 : *p2 - '0');
	                                        p++;
	                                        i = (i << 4) | ((mytoupper(*p2) >= 'A') ? mytoupper(*p2) - 'A' + 10 : *p2 - '0');
                                			if(i==0)error("Null character \\&00 in escape sequence - use CHR$(0)","$");
	                                        p2++;
	                                        *p1++=i;
	                                    } else *p1++='x';
	                                    break;
	                                default:
	                                    *p1++=*p2++;
	                            }
	                        }
	                        toggle=0;
	                    } else *p1++ = *p2++;
                    }
                } else {                                            // else if not quoted
                    for(len = 0, p1 = vtbl[vidx], p2 = (char *)argv[NextData]; *p2 && *p2 != '\'' ; len++, p1++, p2++) {
                        if(*p2 < 0x20 || *p2 >= 0x7f) error("Invalid character");
                        *p1 = *p2;                                  // copy up to the comma
                    }
                }
                if(len > vsize[vidx]) error("String too long");
                *p1 = 0;                                            // terminate the string
                CtoM((unsigned char *)vtbl[vidx]);                                   // convert to a MMBasic string
            }
            else if(vtype[vidx] & T_INT)
                *((long long int *)vtbl[vidx]) = getinteger(argv[NextData]); // much easier if integer variable
            else
                *((MMFLOAT *)vtbl[vidx]) = getnumber(argv[NextData]);      // same for numeric variable

            vidx++;
            NextData += 2;
        }
    }
}

void cmd_call(void){
	int i;
	unsigned char *p=getCstring(cmdline); //get the command we want to call
    unsigned char *q = skipexpression(cmdline);
	if(*q==',')q++;
	i = FindSubFun(p, false);                   // it could be a defined command
	strcat((char *)p," ");
	strcat((char *)p,(char *)q);
	if(i >= 0) {                                // >= 0 means it is a user defined command
		DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
	}
	else
		error("Unknown user subroutine");
}



void MIPS16 cmd_restore(void) {
   if(*cmdline == 0 || *cmdline == '\'') {
       if(CurrentLinePtr >= ProgMemory && CurrentLinePtr < ProgMemory + MAX_PROG_SIZE )
           NextDataLine = ProgMemory;
       else
           NextDataLine = LibMemory;
       NextData = 0;
	} else {
		skipspace(cmdline);
		if(*cmdline=='"') {
			NextDataLine = findlabel(getCstring(cmdline));
			NextData = 0;
		}
		else if(isdigit(*cmdline) || *cmdline==GetTokenValue((unsigned char *)"+") || *cmdline==GetTokenValue((unsigned char *)"-")  || *cmdline=='.'){
			NextDataLine = findline(getinteger(cmdline), true); // try for a line number
			NextData = 0;
		} else {
			void *ptr=findvar(cmdline,V_NOFIND_NULL);
			if(ptr){
				if(g_vartbl[g_VarIndex].type & T_NBR) {
					if(g_vartbl[g_VarIndex].dims[0] > 0) { // Not an array
						error("Syntax");
					}
					NextDataLine = findline(getinteger(cmdline), true);
				} else if(g_vartbl[g_VarIndex].type & T_INT) {
					if(g_vartbl[g_VarIndex].dims[0] > 0) { // Not an array
						error("Syntax");
					}
					NextDataLine = findline(getinteger(cmdline), true);
				} else {
					NextDataLine = findlabel(getCstring(cmdline));    // must be a label
				}
			} else if(isnamestart(*cmdline)) {
				NextDataLine = findlabel(cmdline);    // must be a label
			}
			NextData = 0;
		}
	}
}



void cmd_lineinput(void) {
	unsigned char *vp;
	int i, fnbr;
	getargs(&cmdline, 3, (unsigned char *)",;");										// this is a macro and must be the first executable stmt
	if(argc == 0 || argc == 2) error("Syntax");

	i = 0;
	fnbr = 0;
	if(argc == 3) {
		// is the first argument a file number specifier?  If so, get it
		if(*argv[0] == '#' && *argv[1] == ',') {
			argv[0]++;
			fnbr = getinteger(argv[0]);
		}
		else {
			// is the first argument a prompt?  if so, print it otherwise there are too many arguments
			if(*argv[1] != ',' && *argv[1] != ';') error("Syntax");
			MMfputs((unsigned char *)getstring(argv[0]), 0);
		}
	i = 2;
	}

	if(argc - i != 1) error("Syntax");
	vp = findvar(argv[i], V_FIND);
    if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
	if(!(g_vartbl[g_VarIndex].type & T_STR)) error("Invalid variable");
	MMgetline(fnbr, (char *)inpbuf);									    // get the input line
	if(strlen((char *)inpbuf) > g_vartbl[g_VarIndex].size) error("String too long");
	strcpy((char *)vp, (char *)inpbuf);
	CtoM(vp);														// convert to a MMBasic string
}


void cmd_on(void) {
	int r;
	unsigned char ss[4];													    // this will be used to split up the argument line
    unsigned char *p;
	// first check if this is:   ON KEY location
	p = checkstring(cmdline, (unsigned char *)"PS2");
	if(p){
		getargs(&p,1,(unsigned char *)",");
		if(*argv[0] == '0' && !isdigit(*(argv[0]+1))){
			OnPS2GOSUB = NULL;                                      // the program wants to turn the interrupt off
		} else {
			OnPS2GOSUB = GetIntAddress(argv[0]);						    // get a pointer to the interrupt routine
			InterruptUsed = true;
		}
		return;
	}
	p = checkstring(cmdline, (unsigned char *)"KEY");
	if(p) {
		getargs(&p,3,(unsigned char *)",");
		if(argc==1){
			if(*argv[0] == '0' && !isdigit(*(argv[0]+1))){
				OnKeyGOSUB = NULL;                                      // the program wants to turn the interrupt off
			} else {
				OnKeyGOSUB = GetIntAddress(argv[0]);						    // get a pointer to the interrupt routine
				InterruptUsed = true;
			}
			return;
		} else {
			keyselect=getint(argv[0],0,255);
			if(keyselect==0){
				KeyInterrupt = NULL;                                      // the program wants to turn the interrupt off
			} else {
				if(*argv[2] == '0' && !isdigit(*(argv[2]+1))){
					KeyInterrupt = NULL;                                      // the program wants to turn the interrupt off
				} else {
					KeyInterrupt = (char *)GetIntAddress(argv[2]);						    // get a pointer to the interrupt routine
					InterruptUsed = true;
				}
			}
			return;
		}
	}
    p = checkstring(cmdline, (unsigned char *)"ERROR");
	if(p) {
		if(checkstring(p, (unsigned char *)"ABORT")) {
            OptionErrorSkip = 0;
            return;
        }
        MMerrno = 0;                                                // clear the error flags
        *MMErrMsg = 0;
        if(checkstring(p, (unsigned char *)"CLEAR")) return;
		if(checkstring(p, (unsigned char *)"IGNORE")) {
            OptionErrorSkip = -1;
            return;
        }
		if((p = checkstring(p, (unsigned char *)"SKIP"))) {
            if(*p == 0 || *p == (unsigned char)'\'')
                OptionErrorSkip = 2;
            else
                OptionErrorSkip = getint(p, 1, 10000) + 1;
            return;
        }
        error("Syntax");
	}

	// if we got here the command must be the traditional:  ON nbr GOTO|GOSUB line1, line2,... etc

	ss[0] = tokenGOTO;
	ss[1] = tokenGOSUB;
	ss[2] = ',';
	ss[3] = 0;
	{																// start a new block
		getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, ss);				// getargs macro must be the first executable stmt in a block
		if(argc < 3 || !(*argv[1] == ss[0] || *argv[1] == ss[1])) error("Syntax");
		if(argc%2 == 0) error("Syntax");

		r = getint(argv[0], 0, 255);									// evaluate the expression controlling the statement
		if(r == 0 || r > argc/2) return;							// microsoft say that we just go on to the next line

		if(*argv[1] == ss[1]) {
			// this is a GOSUB, same as a GOTO but we need to first push the return pointer
			if(gosubindex >= MAXGOSUB) error("Too many nested GOSUB");
            errorstack[gosubindex] = CurrentLinePtr;
			gosubstack[gosubindex++] = nextstmt;
        	g_LocalIndex++;
		}

		if(isnamestart(*argv[r*2]))
			nextstmt = findlabel(argv[r*2]);						// must be a label
		else
			nextstmt = findline(getinteger(argv[r*2]), true);		// try for a line number
	}
//    IgnorePIN = false;
}

/**
 * @cond
 * The following section will be excluded from the documentation.
 */
// utility routine used by DoDim() below and other places in the interpreter
// checks if the type has been explicitly specified as in DIM FLOAT A, B, ... etc
unsigned char *CheckIfTypeSpecified(unsigned char *p, int *type, int AllowDefaultType) {
    unsigned char *tp;

    if((tp = checkstring(p, (unsigned char *)"INTEGER")) != NULL)
        *type = T_INT | T_IMPLIED;
    else if((tp = checkstring(p, (unsigned char *)"STRING")) != NULL)
        *type = T_STR | T_IMPLIED;
    else if((tp = checkstring(p, (unsigned char *)"FLOAT")) != NULL)
        *type = T_NBR | T_IMPLIED;
    else {
        if(!AllowDefaultType) error("Variable type");
        tp = p;
        *type = DefaultType;                                        // if the type is not specified use the default
    }
    return tp;
}



unsigned char *SetValue(unsigned char *p, int t, void *v) {
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;
    char TempCurrentSubFunName[MAXVARLEN + 1];
    strcpy(TempCurrentSubFunName, (char *)CurrentSubFunName);			    // save the current sub/fun name
	if(t & T_STR) {
		p = evaluate(p, &f, &i64, &s, &t, true);
		Mstrcpy(v, s);
	}
	else if(t & T_NBR) {
		p = evaluate(p, &f, &i64, &s, &t, false);
		if(t & T_NBR)
            (*(MMFLOAT *)v) = f;
        else
            (*(MMFLOAT *)v) = (MMFLOAT)i64;
	} else {
		p = evaluate(p, &f, &i64, &s, &t, false);
		if(t & T_INT)
            (*(long long int  *)v) = i64;
        else
            (*(long long int  *)v) = FloatToInt64(f);
	}
	strcpy((char *)CurrentSubFunName, TempCurrentSubFunName);			    // restore the current sub/fun name
    return p;
}

/** @endcond */


// define a variable
// DIM [AS INTEGER|FLOAT|STRING] var[(d1 [,d2,...]] [AS INTEGER|FLOAT|STRING] [, ..., ...]
// LOCAL also uses this function the routines only differ in that LOCAL can only be used in a sub/fun
void MIPS16 cmd_dim(void) {
	int i, j, k, type, typeSave, ImpliedType = 0, VIndexSave, StaticVar = false;
    unsigned char *p, chSave, *chPosit;
    unsigned char VarName[(MAXVARLEN * 2) + 1];
    void *v, *tv;

    if(*cmdline == tokenAS) cmdline++;                              // this means that we can use DIM AS INTEGER a, b, etc
    p = CheckIfTypeSpecified(cmdline, &type, true);                 // check for DIM FLOAT A, B, ...
    ImpliedType = type;
    {                                                               // getargs macro must be the first executable stmt in a block
        getargs(&p, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
        if((argc & 0x01) == 0) error("Syntax");

        for(i = 0; i < argc; i += 2) {
            p = skipvar(argv[i], false);                            // point to after the variable
            while(!(*p == 0 || *p == tokenAS || *p == (unsigned char)'\'' || *p == tokenEQUAL))
                p++;                                                // skip over a LENGTH keyword if there and see if we can find "AS"
            chSave = *p; chPosit = p; *p = 0;                       // save the char then terminate the string so that LENGTH is evaluated correctly
            if(chSave == tokenAS) {                                 // are we using Microsoft syntax (eg, AS INTEGER)?
                if(ImpliedType & T_IMPLIED) error("Type specified twice");
                p++;                                                // step over the AS token
                p = CheckIfTypeSpecified(p, &type, true);           // and get the type
                if(!(type & T_IMPLIED)) error("Variable type");
            }

            if(cmdtoken == cmdLOCAL) {
                if(g_LocalIndex == 0) error("Invalid here");
                type |= V_LOCAL;                                    // local if defined in a sub/fun
            }

            if(cmdtoken == cmdSTATIC) {
                if(g_LocalIndex == 0) error("Invalid here");
                // create a unique global name
                if(*CurrentInterruptName)
                    strcpy((char *)VarName, CurrentInterruptName);          // we must be in an interrupt sub
                else
                    strcpy((char *)VarName, CurrentSubFunName);             // normal sub/fun
                for(k = 1; k <= MAXVARLEN; k++) if(!isnamechar(VarName[k])) {
                    VarName[k] = 0;                                 // terminate the string on a non valid char
                    break;
                }
				strcat((char *)VarName,".");
                strcat((char *)VarName, (char *)argv[i]);					        // by prefixing the var name with the sub/fun name
            	StaticVar = true;
            } else
            	strcpy((char *)VarName, (char *)argv[i]);

            v = findvar(VarName, type | V_NOFIND_NULL);             // check if the variable exists
            typeSave = type;
            VIndexSave = g_VarIndex;
			if(v == NULL) {											// if not found
                v = findvar(VarName, type | V_FIND | V_DIM_VAR);    // create the variable
                type = TypeMask(g_vartbl[g_VarIndex].type);
                VIndexSave = g_VarIndex;
                *chPosit = chSave;                                  // restore the char previously removed
                if(g_vartbl[g_VarIndex].dims[0] == -1) error("Array dimensions");
                if(g_vartbl[g_VarIndex].dims[0] > 0) {
                    g_DimUsed = true;                                 // prevent OPTION BASE from being used
                    v = g_vartbl[g_VarIndex].val.s;
                }
                while(*p && *p != '\'' && tokenfunction(*p) != op_equal) p++;	// search through the line looking for the equals sign
            	if(tokenfunction(*p) == op_equal) {
                    p++;                                            // step over the equals sign
                    skipspace(p);
                    if(g_vartbl[g_VarIndex].dims[0] > 0 && *p == '(') {
                        // calculate the overall size of the array
                        for(j = 1, k = 0; k < MAXDIM && g_vartbl[VIndexSave].dims[k]; k++) {
                            j *= (g_vartbl[VIndexSave].dims[k] + 1 - g_OptionBase);
                        }
                        do {
                            p++;                                    // step over the opening bracket or terminating comma
                            p = SetValue(p, type, v);
                            if(type & T_STR) v = (char *)v + g_vartbl[VIndexSave].size + 1;
                            if(type & T_NBR) v = (char *)v + sizeof(MMFLOAT);
                            if(type & T_INT) v = (char *)v + sizeof(long long int);
                            skipspace(p); j--;
                        } while(j > 0 && *p == ',');
                        if(*p != ')') error("Number of initialising values");
                        if(j != 0) error("Number of initialising values");
                    } else
                        SetValue(p, type, v);
                }
                type = ImpliedType;
             } else {
             	if(!StaticVar) error("$ already declared", VarName);
             }


			 // if it is a STATIC var create a local var pointing to the global var
             if(StaticVar) {
                tv = findvar(argv[i], typeSave | V_LOCAL | V_NOFIND_NULL);    						// check if the local variable exists
                if(tv != NULL) error("$ already declared", argv[i]);
                tv = findvar(argv[i], typeSave | V_LOCAL | V_FIND | V_DIM_VAR);         			// create the variable
                if(g_vartbl[VIndexSave].dims[0] > 0 || (g_vartbl[VIndexSave].type & T_STR)) {
                    FreeMemory(tv);                                                                 // we don't need the memory allocated to the local
                    g_vartbl[g_VarIndex].val.s = g_vartbl[VIndexSave].val.s;                              // point to the memory of the global variable
                } else
                    g_vartbl[g_VarIndex].val.ia = &(g_vartbl[VIndexSave].val.i);                    		// point to the data of the variable
                g_vartbl[g_VarIndex].type = g_vartbl[VIndexSave].type | T_PTR;           					// set the type to a pointer
                g_vartbl[g_VarIndex].size = g_vartbl[VIndexSave].size;                   					// just in case it is a string copy the size
                for(j = 0; j < MAXDIM; j++) g_vartbl[g_VarIndex].dims[j] = g_vartbl[VIndexSave].dims[j];  // just in case it is an array copy the dimensions
			}
        }
    }
}




void  cmd_const(void) {
    unsigned char *p;
    void *v;
    int i, type;

	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");				// getargs macro must be the first executable stmt in a block
	if((argc & 0x01) == 0) error("Syntax");

    for(i = 0; i < argc; i += 2) {
        p = skipvar(argv[i], false);                                // point to after the variable
        skipspace(p);
        if(tokenfunction(*p) != op_equal) error("Syntax");  // must be followed by an equals sign
        p++;                                                        // step over the equals sign
        type = T_NOTYPE;
        v = DoExpression(p, &type);                                 // evaluate the constant's value
        type = TypeMask(type);
        type |= V_FIND | V_DIM_VAR | T_CONST | T_IMPLIED;
        if(g_LocalIndex != 0) type |= V_LOCAL;                        // local if defined in a sub/fun
        findvar(argv[i], type);                                     // create the variable
        if(g_vartbl[g_VarIndex].dims[0] != 0) error("Invalid constant");
        if(TypeMask(g_vartbl[g_VarIndex].type) != TypeMask(type)) error("Invalid constant");
        else {
            if(type & T_NBR) g_vartbl[g_VarIndex].val.f = *(MMFLOAT *)v;           // and set its value
            if(type & T_INT) g_vartbl[g_VarIndex].val.i = *(long long int  *)v;
            if(type & T_STR) {
				if((unsigned char)*(unsigned char *)v<(MAXDIM-1)*sizeof(g_vartbl[g_VarIndex].dims[1])){
					FreeMemorySafe((void **)&g_vartbl[g_VarIndex].val.s);
					g_vartbl[g_VarIndex].val.s=(void *)&g_vartbl[g_VarIndex].dims[1];
				}
				Mstrcpy((unsigned char *)g_vartbl[g_VarIndex].val.s, (unsigned char *)v);
			}
        }
    }
}

/**
 * @cond
 * The following section will be excluded from the documentation.
 */

// utility function used by llist() below
// it copys a command or function honouring the case selected by the user
void strCopyWithCase(char *d, char *s) {
    if(Option.Listcase == CONFIG_LOWER) {
        while(*s) *d++ = tolower(*s++);
    } else if(Option.Listcase == CONFIG_UPPER) {
        while(*s) *d++ = mytoupper(*s++);
    } else {
        while(*s) *d++ = *s++;
    }
    *d = 0;
}

void replaceAlpha(char *str, const char *replacements[MMEND]){
    char buffer[STRINGSIZE]; // Buffer to store the modified string
    int bufferIndex = 0;
    int len = strlen(str);
    int i = 0;

    while (i < len) {
        // Check for the pattern "~(X)" where X is an uppercase letter
        if (i<len-3 && str[i] == '~' && str[i + 1] == '(' && isupper((int)str[i + 2]) && str[i + 3] == ')') {
            char alpha = str[i + 2]; // Extract the letter 'alpha'
            const char *replacement = replacements[alpha - 'A']; // Get the replacement string

            // Copy the replacement string into the buffer
            strcpy(&buffer[bufferIndex], replacement);
            bufferIndex += strlen(replacement);

            i += 4; // Move past "~(X)"
        } else {
            // Copy the current character to the buffer
            buffer[bufferIndex++] = str[i++];
        }
    }

    buffer[bufferIndex] = '\0'; // Null-terminate the buffer
    strcpy(str,  buffer); // Copy the buffer back into the original string
}
int format_string(char *c, int n) {
	int count=0;
	n-=2;
    int len = strlen(c);
	if(*c==0)return 0;
    char *result = GetMemory(len * 2); // Allocate enough space for the modified string
    int pos = 0, start = 0;

    while (start < len) {
        int split_pos = start + n - 1;

        if (split_pos >= len) { // If remaining text fits in one line
            strcpy(result + pos, c + start);
            pos += strlen(c + start);
            break;
        }

        while (split_pos > start && !(c[split_pos] == ' ' || c[split_pos] == ',')) {
            split_pos--; // Try to find a space to break at
        }

        if (split_pos == start) {
            split_pos = start + n - 1; // No space found, force a split
        }

        strncpy(result + pos, c + start, split_pos - start + 1);
        pos += (split_pos - start + 1);

        start = split_pos + 1;

        if (start < len) { // Only add underscore if not the last substring
            result[pos++] = ' ';
            result[pos++] = Option.continuation;
            result[pos++] = '\n';
			count++;
        }
    }

    result[pos] = '\0';
	strcpy(c,result);
	FreeMemory((void *)result);
	return count;
}
// list a line into a buffer (b) given a pointer to the beginning of the line (p).
// the returned string is a C style string (terminated with a zero)
// this is used by cmd_list(), cmd_edit() and cmd_xmodem()
unsigned char  *llist(unsigned char *b, unsigned char *p) {
	int i, firstnonwhite = true;
    unsigned char *b_start = b;

	while(1) {
        if(*p == T_NEWLINE) {
            p++;
            firstnonwhite = true;
            continue;
        }

		if(*p == T_LINENBR) {
			i = (((p[1]) << 8) | (p[2]));							// get the line number
			p += 3;													// and step over the number
                IntToStr((char *)b, i, 10);
                b += strlen((char *)b);
				if(*p != ' ') *b++ = ' ';
			}

		if(*p == T_LABEL) {											// got a label
			for(i = p[1], p += 2; i > 0; i--)
				*b++ = *p++;										// copy to the buffer
			*b++ = ':';												// terminate with a colon
			if(*p && *p != ' ') *b++ = ' ';							// and a space if necessary
			firstnonwhite = true;
			}														// this deliberately drops through in case the label is the only thing on the line

		if(*p >= C_BASETOKEN) {
			if(firstnonwhite) {
        		CommandToken tkn=commandtbl_decode(p);
				if(tkn == GetCommandValue( (unsigned char *)"Let"))
					*b = 0;											// use nothing if it LET
				else {
					strCopyWithCase((char *)b, (char *)commandname(tkn));			// expand the command (if it is not LET)
					if(*b=='_'){
						if(!strncasecmp((char *)&b[1],"SIDE SET",8) ||
							!strncasecmp((char *)&b[1],"END PROGRAM",11) ||
							!strncasecmp((char *)&b[1],"WRAP",4) ||
							!strncasecmp((char *)&b[1],"LINE",4) ||
							!strncasecmp((char *)&b[1],"PROGRAM",7) ||
							!strncasecmp((char *)&b[1],"LABEL",5)
						) *b='.';
						else if(b[1]=='(')*b='&';
					} 
						b += strlen((char *)b);                                 // update pointer to the end of the buffer
                    if(isalpha(*(b - 1))) *b++ = ' ';               // add a space to the end of the command name
                }
				firstnonwhite = false;
				p+=sizeof(CommandToken);
			} else {												// not a command so must be a token
				strCopyWithCase((char *)b, (char *)tokenname(*p));					// expand the token
                    b += strlen((char *)b);                                 // update pointer to the end of the buffer
				if(*p == tokenTHEN || *p == tokenELSE)
					firstnonwhite = true;
				else
					firstnonwhite = false;
				p++;
			}
			continue;
		}

		// hey, an ordinary char, just copy it to the output
		if(*p) {
			*b = *p;												// place the char in the buffer
			if(*p != ' ') firstnonwhite = false;
			p++;  b++;												// move the pointers
			continue;
		}

        // at this point the char must be a zero
        // zero char can mean both a separator or end of line
		if(!(p[1] == T_NEWLINE || p[1] == 0)) {
			*b++ = ':';												// just a separator
			firstnonwhite = true;
			p++;
			continue;
		}

		// must be the end of a line - so return to the caller
        while(*(b-1) == ' ' && b > b_start) --b;                    // eat any spaces on the end of the line
		*b = 0;	
		replaceAlpha((char *)b_start, overlaid_functions) ;  //replace the user version of all the MM. functions
		STR_REPLACE((char *)b_start, "PEEK(INT8", "PEEK(BYTE",0);
		return ++p;
	} // end while
}



void execute_one_command(unsigned char *p) {
    int i;

	CheckAbort();
	targ = T_CMD;
	skipspace(p);													// skip any whitespace
	if(p[0]>= C_BASETOKEN && p[1]>=C_BASETOKEN){
//                    if(*(char*)p >= C_BASETOKEN && *(char*)p - C_BASETOKEN < CommandTableSize - 1 && (commandtbl[*(char*)p - C_BASETOKEN].type & T_CMD)) {
        CommandToken cmd=commandtbl_decode(p);
        if(cmd == cmdWHILE || cmd == cmdDO || cmd == cmdFOR) error("Invalid inside THEN ... ELSE") ;
		cmdtoken=cmd;
		cmdline = p + sizeof(CommandToken);
        skipspace(cmdline);
		commandtbl[cmd].fptr(); // execute the command
	} else {
	    if(!isnamestart(*p)) error("Invalid character");
        i = FindSubFun(p, false);                                   // it could be a defined command
        if(i >= 0)                                                  // >= 0 means it is a user defined command
            DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
        else
            error("Unknown command");
	}
	ClearTempMemory();											    // at the end of each command we need to clear any temporary string vars
}

void execute(char* mycmd) {
	//    char *temp_tknbuf;
	unsigned char* ttp=NULL;
	int i = 0, toggle = 0;
	//    temp_tknbuf = GetTempStrMemory();
	//    strcpy(temp_tknbuf, tknbuf);
		// first save the current token buffer in case we are in immediate mode
		// we have to fool the tokeniser into thinking that it is processing a program line entered at the console
	skipspace(mycmd);
	strcpy((char *)inpbuf, (const char *)getCstring((unsigned char *)mycmd));                                      // then copy the argument
	if (!(toupper(inpbuf[0]) == 'R' && toupper(inpbuf[1]) == 'U' && toupper(inpbuf[2]) == 'N')) { //convert the string to upper case
		while (inpbuf[i]) {
			if (inpbuf[i] == 34) {
				if (toggle == 0)toggle = 1;
				else toggle = 0;
			}
			if (!toggle) {
				if (inpbuf[i] == ':')error((char *)"Only single statements allowed");
				inpbuf[i] = toupper(inpbuf[i]);
			}
			i++;
		}
		multi=false;
		tokenise(true);                                                 // and tokenise it (the result is in tknbuf)
		memset(inpbuf, 0, STRINGSIZE);
		tknbuf[strlen((char *)tknbuf)] = 0;
		tknbuf[strlen((char*)tknbuf) + 1] = 0;
		if(CurrentLinePtr)ttp = nextstmt;                                                 // save the globals used by commands
		ScrewUpTimer = 1000;
		ExecuteProgram(tknbuf);                                              // execute the function's code
		ScrewUpTimer = 0;
		// g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
		if(CurrentLinePtr)nextstmt = ttp;
		return;
	}
	else {
		unsigned char* p = inpbuf;
//		char* q;
//		char fn[STRINGSIZE] = { 0 };
        unsigned short tkn=GetCommandValue((unsigned char *)"RUN");
        tknbuf[0] = (tkn & 0x7f ) + C_BASETOKEN;
        tknbuf[1] = (tkn >> 7) + C_BASETOKEN; //tokens can be 14-bit
		p[0] = (tkn & 0x7f ) + C_BASETOKEN;
		p[1] = (tkn >> 7) + C_BASETOKEN; //tokens can be 14-bit
		memmove(&p[2], &p[4], strlen((char *)p) - 4);
/*		if ((q = strchr((char *)p, ':'))) {
			q--;
			*q = '0';
		}*/
		p[strlen((char*)p) - 2] = 0;
//		MMPrintString(fn); PRet();
//		CloseAudio(1);
		strcpy((char *)tknbuf, (char*)inpbuf);
		if (CurrentlyPlaying != P_NOTHING)CloseAudio(1);
		longjmp(jmprun, 1);
	}
}
/** @endcond */

void cmd_execute(void) {
	execute((char*)cmdline);
}


