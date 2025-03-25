/***********************************************************************************************************************
PicoMite MMBasic

Functions.c

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
* @file Functions.c
* @author Geoff Graham, Peter Mather
* @brief Source for standard MMBasic commands
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <math.h>
#include "stdlib.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include <float.h>
#include "xregex.h"
#ifdef rp2350
#include "pico/rand.h"
#endif
extern long long int  llabs (long long int  n);
const char* overlaid_functions[]={
    "MM.HRES",
    "MM.VRES",
    "MM.VER",
	"MM.I2C",
	"MM.FONTHEIGHT",
	"MM.FONTWIDTH",
#ifndef USBKEYBOARD
	"MM.PS2",
#endif
	"MM.HPOS",
	"MM.VPOS",
	"MM.ONEWIRE",
	"MM.Errno",
	"MM.ErrMsg$",
	"MM.WATCHDOG",
	"MM.DEVICE$",
	"MM.CMDLINE$",
#ifdef PICOMITEWEB
	"MM.MESSAGE$",
	"MM.ADDRESS$",
	"MM.TOPIC$"
#endif
};
/********************************************************************************************************************************************
 basic functions
 each function is responsible for decoding a basic function
 all function names are in the form fun_xxxx() so, if you want to search for the function responsible for the ASC() function look for fun_asc

 There are 4 globals used by these functions:

 unsigned char *ep       This is a pointer to the argument of the function
                Eg, in the case of INT(35/7) ep would point to "35/7)"

 fret           Is the return value for a basic function that returns a float

 iret           Is the return value for a basic function that returns an integer

 sret           Is the return value for a basic function that returns a string

 tret           Is the type of the return value.  normally this is set by the caller and is not changed by the function

 ********************************************************************************************************************************************/
/*

DOCUMENTATION
=============
 FIELD$( str$, field)
 or
 FIELD$( str$, field, delim$)
 or
 FIELD$( str$, field, delim$, quote$)
 
 Extract a substring (ie, field) from 'str$'.  Each is separated by any one of 
 the characters in the string 'delim$' and the field number to return is specified 
 by 'field' (the first field is field number 1).  Any leading and trailing 
 spaces will be trimmed from the returned string.
 
 Note that 'delim$' can contain a number of characters and the fields 
 will then be separated by any one of these characters.  if delim$ is not
 provided it will default to a comma (,) character.
 
 'quote$' is the set of characters that might be used to quote text.  Typically
 it is the double quote character (") and any text that is surrounded by the quote
 character(s) will be treated as a block and any 'delim$' characters within that
 block will not be used as delimiters.
 
 This function is useful for splitting apart comma-separated-values (CSV) in data 
 streams produced by GPS modules and test equipment.  For example:
  PRINT FIELD$("aaa,bbb,ccc", 2, ",")
  Will print the string: bbb
 
  PRINT FIELD$("text1, 'quoted, text', text3", 2, ",", "'")
  will print the string: 'quoted, text'

*/

// return true if the char 'c' is contained in the string 'srch$'
// used only by scan_for_delimiter()  below
// Note: this operates on MMBasic strings
static int MInStr(char *srch, char c) {
    int i;
    for(i = 1; i <= *(unsigned char *)srch; i++)
        if(c == srch[i]) 
            return true;
    return false;
}
/*  @endcond */

void fun_bound(void){
	int which=1;
	getargs(&ep, 3,(unsigned char *)",");
	if(argc==3)which=getint(argv[2],0,MAXDIM);
    findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	if(which==0)iret=g_OptionBase;
	else iret=g_vartbl[g_VarIndex].dims[which-1];
	if(iret==-1)iret=0;
	targ=T_INT;
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

// scan through string p and return p if it points to any char in delims
// this will skip any quoted text (quote delimiters in quotes)
// used only by fun_field() below
// Note: this operates on MMBasic strings
static int scan_for_delimiter(int start, unsigned char *p, unsigned char *delims, unsigned char *quotes) {
    int i;
    unsigned char qidx;
    for(i = start; i <= *(unsigned char *)p && !MInStr((char *)delims, (char )p[i]); i++) {
        if(MInStr((char *)quotes, (char )p[i])) {                                  // if we have a quote
            qidx = p[i];
            i++;                                                    // step over the opening quote
            while(i < *(unsigned char *)p && p[i] != qidx) i++;    // skip the quoted text
        }
    }
    return i;
}
/*  @endcond */

void fun_call(void){
	int i;
    long long int i64 = 0;
    unsigned char *s = NULL;
    MMFLOAT f;
    unsigned char *q = ep; // store the value of 'ep' because calling getCstring() can change it.
    unsigned char *p=getCstring(ep); //get the command we want to call
    q = skipexpression(q);
	if(*q==',')q++;
	i = FindSubFun(p, true);                   // it could be a defined command
	strcat((char *)p," ");
	strcat((char *)p,(char *)q);
    targ= T_NOTYPE;
	if(i >= 0) {                                // >= 0 means it is a user defined command
		DefinedSubFun(true, p, i, &f, &i64, &s, &targ);
	} else error("Unknown user function");
    if(targ & T_STR) {
    	sret=GetTempMemory(STRINGSIZE);
        Mstrcpy(sret, s);                                             // if it is a string then save it
    }
    if(targ & T_INT)iret=i64;
    if(targ & T_NBR)fret=f;
}

// syntax:  str$ = FIELD$(string1, nbr, string2, string3)
//          find field nbr in string1 using the delimiters in string2 to separate the fields
//          if string3 is present any chars quoted by chars in string3 will not be searched for delimiters
// Note: this operates on MMBasic strings
void fun_field(void) {
	unsigned char *p, *delims = (unsigned char *)"\1,", *quotes = (unsigned char *)"\0";
    int fnbr, i, j, k;
	getargs(&ep, 7,(unsigned char *)",");
	if(!(argc == 3 || argc == 5 || argc == 7)) error("Syntax");
    p = getstring(argv[0]);                                         // the string containing the fields
    fnbr = getint(argv[2], 1, MAXSTRLEN);                           // field nbr to return
    if(argc > 3 && *argv[4]) delims = getstring(argv[4]);           // delimiters for fields
    if(argc == 7) quotes = getstring(argv[6]);                      // delimiters for quoted text
	sret = GetTempMemory(STRINGSIZE);                                      // this will last for the life of the command
    targ = T_STR;
    i = 1;
    while(--fnbr > 0) {
        i = scan_for_delimiter(i, p, delims, quotes);
        if(i > *p) return;
        i++;                                                        // step over the delimiter
    }
    while(p[i] == ' ') i++;                                         // trim leading spaces
    j = scan_for_delimiter(i, p, delims, quotes);                   // find the end of the field
    *sret = k = j - i;
    for(j = 1; j <= k; j++, i++) sret[j] = p[i];                    // copy to the return string
    for(k = *sret; k > 0 && sret[k] == ' '; k--);                   // trim trailing spaces
    *sret = k;
}

void fun_str2bin(void){
    union binmap{
    	int8_t c[8];
    	uint8_t uc[8];
    	float f;
    	double d;
    	int64_t l;
    	uint64_t ul;
    	int i;
    	uint32_t ui;
    	short s;
    	uint16_t us;
    }map;
    int j;
	getargs(&ep, 5,(unsigned char *)",");
	if(!(argc==3 || argc==5))error("Syntax");
	if(argc==5 && !checkstring(argv[4],(unsigned char *)"BIG"))error("Syntax");
	char *p;
	p = (char *)getstring(argv[2]);
	int len=p[0];
	map.l=0;
    for(j=0;j<len;j++)map.c[j]=p[j+1];
    if(argc==5){ // big endian so swap byte order
    	char k;
    	int m;
    	for(j=0;j<(len>>1);j++){
    		m=len-j-1;
    		k=map.c[j];
    		map.c[j]=map.c[m];
    		map.c[m]=k;
    	}
    }
    if(checkstring(argv[0],(unsigned char *)"DOUBLE")){
        if(len!=8)error("String length");
	targ=T_NBR;
	fret=(MMFLOAT)map.d;
    } else if(checkstring(argv[0],(unsigned char *)"SINGLE")){
        if(len!=4)error("String length");
	targ=T_NBR;
	fret=(MMFLOAT)map.f;
    } else if(checkstring(argv[0],(unsigned char *)"INT64")){
        if(len!=8)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.l;
    } else if( checkstring(argv[0],(unsigned char *)"INT32")){
        if(len!=4)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.i;
    } else if(checkstring(argv[0],(unsigned char *)"INT16")){
        if(len!=2)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.s;
    } else if(checkstring(argv[0],(unsigned char *)"INT8")){
        if(len!=1)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.c[0];
    } else if(checkstring(argv[0],(unsigned char *)"UINT64")){
        if(len!=8)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.ul;
    } else if(checkstring(argv[0],(unsigned char *)"UINT32")){
        if(len!=4)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.ui;
    } else if(checkstring(argv[0],(unsigned char *)"UINT16")){
        if(len!=2)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.us;
    } else if(checkstring(argv[0],(unsigned char *)"UINT8")){
        if(len!=1)error("String length");
    	targ=T_INT;
    	iret=(int64_t)map.uc[0];
    } else error("Syntax");

}


void fun_bin2str(void){
	int j, len=0;
    union binmap{
    	int8_t c[8];
    	uint8_t uc[8];
    	float f;
    	double d;
    	int64_t l;
    	uint64_t ul;
    	int i;
    	uint32_t ui;
    	short s;
    	uint16_t us;
    }map;
    int64_t i64;
	getargs(&ep, 5,(unsigned char *)",");
	if(!(argc==3 || argc==5))error("Syntax");
    if(argc==5 && !(checkstring(argv[4],(unsigned char *)"BIG")))error("Syntax");
	sret = GetTempMemory(STRINGSIZE);									// this will last for the life of the command
    if(checkstring(argv[0],(unsigned char *)"DOUBLE")){
		len=8;
		map.d=(double)getnumber(argv[2]);
    } else if(checkstring(argv[0],(unsigned char *)"SINGLE")){
		len=4;
    	map.f=(float)getnumber(argv[2]);
    } else {
    	i64=getinteger(argv[2]);
    	if(checkstring(argv[0],(unsigned char *)"INT64")){
    		len=8;
    		map.l=(int64_t)i64;
    	} else if( checkstring(argv[0],(unsigned char *)"INT32")){
    		len=4;
    		if(i64 > 2147483647 || i64 < -2147483648)error("Overflow");
    		map.i=(int32_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"INT16")){
    		len=2;
    		if(i64 > 32767 || i64 < -32768)error("Overflow");
    		map.s=(int16_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"INT8")){
    		len=1;
    		if(i64 > 127 || i64 < -128)error("Overflow");
    		map.c[0]=(int8_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"UINT64")){
    		len=8;
    		map.ul=(uint64_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"UINT32")){
    		len=4;
    		if(i64 > 4294967295 || i64 < 0)error("Overflow");
    		map.ui=(uint32_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"UINT16")){
    		len=2;
    		if(i64 > 65535 || i64 < 0)error("Overflow");
    		map.us=(uint16_t)i64;
    	} else if(checkstring(argv[0],(unsigned char *)"UINT8")){
    		len=1;
    		if(i64 > 255 || i64 < 0)error("Overflow");
    		map.uc[0]=(uint8_t)i64;
    	} else error("Syntax");
    }


	for(j=0;j<len;j++)sret[j]=map.c[j];

    if(argc==5){ // big endian so swap byte order
    	unsigned char k;
    	int m;
    	for(j=0;j<(len>>1);j++){
    		m=len-j-1;
    		k=sret[j];
    		sret[j]=sret[m];
    		sret[m]=k;
    	}
    }
    // convert from c type string but it can contain zeroes
    unsigned char *p1, *p2;
    j=len;
    p1 = sret + len; p2 = sret + len - 1;
    while(j--) *p1-- = *p2--;
    *sret = len;
	targ = T_STR;
}



// return the absolute value of a number (ie, without the sign)
// a = ABS(nbr)
void fun_abs(void) {
    unsigned char *s;
    MMFLOAT f;
    long long int  i64;

    targ = T_INT;
    evaluate(ep, &f, &i64, &s, &targ, false);                   // get the value and type of the argument
    if(targ & T_NBR)
        fret = fabs(f);
    else {
        iret = i64;
        if(iret < 0) iret = -iret;
    }
}




// return the ASCII value of the first character in a string (ie, its number value)
// a = ASC(str$)
void fun_asc(void) {
	unsigned char *s;

	s = getstring(ep);
	if(*s == 0)
	    iret = 0;
	else
    	iret = *(s + 1);
    targ = T_INT;
}
void fun_bit(void){
	uint64_t *s;
	uint64_t spos;
	getargs(&ep, 3, (unsigned char *)",");
	s=(uint64_t *)findvar(argv[0], V_NOFIND_ERR);
	if(!(g_vartbl[g_VarIndex].type & T_INT)) error("Not an integer");
	spos = getint(argv[2], 0,63);						    // the mid position
	iret = (int64_t)(*s&(1<<spos))>>spos;
	targ=T_INT;
}
void fun_byte(void){
	unsigned char *s;
	int spos;
	getargs(&ep, 3, (unsigned char *)",");
	s=(unsigned char *)findvar(argv[0], V_NOFIND_ERR);
	if(!(g_vartbl[g_VarIndex].type & T_STR)) error("Not a string");
	spos = getint(argv[2], 1, g_vartbl[g_VarIndex].size);						    // the mid position
	iret = s[spos];							// this will last for the life of the command
    targ = T_INT;
}
void fun_tilde(void){
	targ=T_INT;
/*
typedef enum {
    MMHRES,
    MMVRES,
    MMVER,
    MMI2C,
	MMFONTHEIGHT,
	MMFONTWIDTH,
#ifndef USBKEYBOARD
	MMPS2,
#endif
	MMHPOS,
	MMVPOS,
	MMONEWIRE,
    MMERRNO,
    MMERRMSG,
	MMWATCHDOG,
	MMDEVICE,
	MMCMDLINE,
#ifdef PICOMITEWEB
	MMMESSAGE,
    MMTOPIC,
    MMADDRESS,
#endif
    MMEND
} Operation;
*/
	switch(*ep-'A'){
		case MMHRES:
			iret=HRes;
			break;
		case MMVRES:
			iret=VRes;
			break;
		case MMVER:
			fun_version();
			break;
		case MMI2C:
			iret=mmI2Cvalue;
			break;
		case MMFONTHEIGHT:
			iret = FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
			break;
		case MMFONTWIDTH:
			iret = FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
			break;
#ifndef USBKEYBOARD
		case MMPS2:
			iret = (int64_t)(uint32_t)PS2code;
			break;
#endif
		case MMHPOS:
			iret = CurrentX;
			break;
		case MMVPOS:
			iret = CurrentY;
			break;
		case MMONEWIRE:
			iret = mmOWvalue;
			break;
		case MMERRNO:
		    iret = MMerrno;
			break;
		case MMERRMSG:
			fun_errmsg();
			break;
		case MMWATCHDOG:
			iret = WatchdogSet;
			break;
		case MMDEVICE:
			fun_device();
			break;
		case MMCMDLINE:
			sret = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
			Mstrcpy(sret,cmdlinebuff);
			targ=T_STR;
			break;
#ifdef PICOMITEWEB
		case MMMESSAGE:
			sret = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
			Mstrcpy(sret,messagebuff);
			targ=T_STR;
			break;
		case MMTOPIC:
			sret = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
			Mstrcpy(sret,topicbuff);
			targ=T_STR;
			break;
		case MMADDRESS:
			sret = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
			Mstrcpy(sret,addressbuff);
			targ=T_STR;
			break;
#endif
		default:
		iret=-1;
	}
}

// return the arctangent of a number in radians
void fun_atn(void) {
	fret = atan(getnumber(ep))*optionangle;
    targ = T_NBR;
}

void fun_atan2(void) {
    MMFLOAT y,x,z;
    getargs(&ep, 3, (unsigned char *)",");
    if(argc != 3)error("Syntax");
    y=getnumber(argv[0]);
    x=getnumber(argv[2]);
    z=atan2(y,x);
    fret=z*optionangle;
    targ = T_NBR;
}

// convert a number into a one character string
// s$ = CHR$(nbr)
void fun_chr(void) {
	int i;

	i = getint(ep, 0, 0xff);
	sret = GetTempMemory(STRINGSIZE);									// this will last for the life of the command
	sret[0] = 1;
	sret[1] = i;
    targ = T_STR;
}



// Round numbers with fractional portions up or down to the next whole number or integer.
void fun_cint(void) {
	iret = getinteger(ep);
    targ = T_INT;
}



// return the cosine of a number in radians
void fun_cos(void) {
	fret = cos(getnumber(ep)/optionangle);
    targ = T_NBR;
}


// convert radians to degrees.  Thanks to Alan Williams for the contribution
void fun_deg(void) {
	fret = (MMFLOAT)((MMFLOAT)getnumber(ep)*RADCONV);
    targ = T_NBR;
}



// Returns the exponential value of a number.
void fun_exp(void) {
	fret = exp(getnumber(ep));
    targ = T_NBR;
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

// utility function used by HEX$(), OCT$() and BIN$()
void DoHexOctBin(int base) {
	unsigned long long int  i;
    int j = 1;
	getargs(&ep, 3, (unsigned char *)",");
	i = (unsigned long long int )getinteger(argv[0]);                // get the number
    if(argc == 3) j = getint(argv[2], 0, MAXSTRLEN);                // get the optional number of chars to return
	if(j==0)j=1;
	sret = GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
    IntToStrPad((char *)sret, (signed long long int )i, '0', j, base);
	CtoM(sret);
    targ = T_STR;
}

/*  @endcond */


// return the hexadecimal representation of a number
// s$ = HEX$(nbr)
void fun_hex(void) {
    DoHexOctBin(16);
}



// return the octal representation of a number
// s$ = OCT$(nbr)
void fun_oct(void) {
    DoHexOctBin(8);
}



// return the binary representation of a number
// s$ = BIN$(nbr)
void fun_bin(void) {
    DoHexOctBin(2);
}

// syntax:  nbr = INSTR([start,] string1, string2)
//          find the position of string2 in string1 starting at start chars in string1
// returns an integer
void fun_instr(void) {
	unsigned char *s1 = NULL, *s2 = NULL;
	int t, start = 0, n = 0 ;
    unsigned char *ss;
    MMFLOAT f;
    long long int  i64;
	getargs(&ep, 7, (unsigned char *)",");
	if(!(argc==3 || argc==5 || argc==7))error("Syntax");
    t = T_NOTYPE;
    evaluate(argv[0], &f, &i64, &ss, &t, false);                   // get the value and type of the argument
    if(t & T_NBR){
        n=2;
		start=getint(argv[0],0,255)-1;
    } else if(t & T_INT){
		n=2;
		start=getint(argv[0],0,255)-1;
    } else if(t & T_STR){
		n=0;
	} else error("Syntax");
	if(argc < (n==2 ? 7 : 5)){
		s1 = getstring(argv[0+n]);
		s2 = getstring(argv[2+n]);
		targ = T_INT;
		if(start > *s1 - *s2 + 1 || *s2 == 0)
			iret = 0;
		else {
			// find s2 in s1 using MMBasic strings
			int i;
			for(i = start; i < *s1 - *s2 + 1; i++) {
				if(memcmp(s1 + i + 1, s2 + 1, *s2) == 0) {
					iret = i + 1;
					return;
				}
			}
		}
		iret = 0;
	} else {
		regex_t regex;
		int reti;
		regmatch_t pmatch;
		MMFLOAT *temp=NULL;
		char *s=GetTempMemory(STRINGSIZE), *p=GetTempMemory(STRINGSIZE);
		strcpy(s,(char *)getCstring(argv[0+n]));
		strcpy(p,(char *)getCstring(argv[2+n]));
		if(argc==5+n){
			temp = findvar(argv[4+n], V_FIND);
			if(!(g_vartbl[g_VarIndex].type & T_NBR)) error("Invalid variable");
		}
		reti = regcomp(&regex, p, 0);
		if( reti ){
			regfree(&regex);
			error("Could not compile regex");
		} 
		reti = regexec(&regex, &s[start], 1, &pmatch, 0);
		targ=T_INT;
		if( !reti ){
			iret=pmatch.rm_so+1+start;
			if(temp)*temp=(MMFLOAT)(pmatch.rm_eo-pmatch.rm_so);
		}
		else if( reti == REG_NOMATCH ){
			iret=0;
			if(temp)*temp=0.0;
		}
		else{
			regfree(&regex);
			error("Regex execution error");
		}
		regfree(&regex);
	}
	targ=T_INT;
}





// Truncate an expression to the next whole number less than or equal to the argument. 
void fun_int(void) {
	iret = floor(getnumber(ep));
    targ = T_INT;
}


// Truncate a number to a whole number by eliminating the decimal point and all characters 
// to the right of the decimal point.
void fun_fix(void) {
	iret = getnumber(ep);
    targ = T_INT;
}



// Return a substring offset by a number of characters from the left (beginning) of the string.
// s$ = LEFT$( string$, nbr )
void fun_left(void) {
	int i;
    unsigned char *s;
	getargs(&ep, 3, (unsigned char *)",");

	if(argc != 3) error("Argument count");
	s = GetTempMemory(STRINGSIZE);                                       // this will last for the life of the command
	Mstrcpy(s, getstring(argv[0]));
	i = getint(argv[2], 0, MAXSTRLEN);
	if(i < *s) *s = i;                                              // truncate if it is less than the current string length
    sret = s;
    targ = T_STR;
}



// Return a substring of ?string$? with ?number-of-chars? from the right (end) of the string.
// s$ = RIGHT$( string$, number-of-chars )
void fun_right(void) {
	int nbr;
	unsigned char *s, *p1, *p2;
	getargs(&ep, 3, (unsigned char *)",");

	if(argc != 3) error("Argument count");
	s = getstring(argv[0]);
	nbr = getint(argv[2], 0, MAXSTRLEN);
	if(nbr > *s) nbr = *s;											// get the number of chars to copy
	sret = GetTempMemory(STRINGSIZE);									// this will last for the life of the command
	p1 = sret; p2 = s + (*s - nbr) + 1;
	*p1++ = nbr;													// inset the length of the returned string
	while(nbr--) *p1++ = *p2++;										// and copy the characters
    targ = T_STR;
}



// return the length of a string
// nbr = LEN( string$ )
void fun_len(void) {
	iret = *(unsigned char *)getstring(ep);                         // first byte is the length
    targ = T_INT;
}



// Return the natural logarithm of the argument 'number'.
// n = LOG( number )
void fun_log(void) {
    MMFLOAT f;
	f = getnumber(ep);
    if(f == 0) error("Divide by zero");
	if(f < 0) error("Negative argument");
	fret = log(f);
    targ = T_NBR;
}



// Returns a substring of ?string$? beginning at ?start? and continuing for ?nbr? characters.
// S$ = MID$(s, spos [, nbr])
void fun_mid(void) {
	unsigned char *s, *p1, *p2;
	int spos, nbr = 0, i;
	getargs(&ep, 5, (unsigned char *)",");

	if(argc == 5) {													// we have MID$(s, n, m)
		nbr = getint(argv[4], 0, MAXSTRLEN);						// nbr of chars to return
	}
	else if(argc == 3) {											// we have MID$(s, n)
		nbr = MAXSTRLEN;											// default to all chars
	}
	else
		error("Argument count");

	s = getstring(argv[0]);											// the string
	spos = getint(argv[2], 1, MAXSTRLEN);						    // the mid position

	sret = GetTempMemory(STRINGSIZE);									// this will last for the life of the command
    targ = T_STR;
	if(spos > *s || nbr == 0)										// if the numeric args are not in the string
		return;														// return a null string
	else {
		i = *s - spos + 1;											// find how many chars remaining in the string
		if(i > nbr) i = nbr;										// reduce it if we don't need that many
		p1 = sret; p2 = s + spos;
		*p1++ = i;													// set the length of the MMBasic string
		while(i--) *p1++ = *p2++;									// copy the nbr chars required
	}
}



// Return the value of Pi.  Thanks to Alan Williams for the contribution
// n = PI
void fun_pi(void) {
	fret = M_PI;
    targ = T_NBR;
}



// convert degrees to radians.  Thanks to Alan Williams for the contribution
// r = RAD( degrees )
void fun_rad(void) {
	fret = (MMFLOAT)((MMFLOAT)getnumber(ep)/RADCONV);
    targ = T_NBR;
}


// generate a random number that is greater than or equal to 0 but less than 1
// n = RND()
void fun_rnd(void) {
#ifdef rp2350
	fret = (MMFLOAT)get_rand_32()/(MMFLOAT)0x100000000;
#else
	fret = (MMFLOAT)rand()/((MMFLOAT)RAND_MAX + (MMFLOAT)RAND_MAX/1000000);
#endif
    targ = T_NBR;
}



// Return the sign of the argument
// n = SGN( number )
void fun_sgn(void) {
	MMFLOAT f;
	f = getnumber(ep);
	if(f > 0)
		iret = +1;
	else if(f < 0)
		iret = -1;
	else
		iret = 0;
    targ = T_INT;
}



// Return the sine of the argument 'number' in radians.
// n = SIN( number )
void fun_sin(void) {
	fret = sin(getnumber(ep)/optionangle);
    targ = T_NBR;
}



// Return the square root of the argument 'number'.
// n = SQR( number )
void fun_sqr(void) {
	MMFLOAT f;
	f = getnumber(ep);
	if(f < 0) error("Negative argument");
	fret = sqrt(f);
    targ = T_NBR;
}



// Return the tangent of the argument 'number' in radians.
// n = TAN( number )
void fun_tan(void) {
	fret = tan(getnumber(ep)/optionangle);
    targ = T_NBR;
}



// Returns the numerical value of the ?string$?.
// n = VAL( string$ )
void fun_val(void) {
	unsigned char *p, *t1, *t2;
	p = getCstring(ep);
    targ = T_INT;
	if(*p == '&') {
                p++; iret = 0;
		switch(toupper(*p++)) {
			case 'H': while(isxdigit(*p)) {
                                      iret = (iret << 4) | ((toupper(*p) >= 'A') ? toupper(*p) - 'A' + 10 : *p - '0');
                                      p++;
                                  }
                                  break;
			case 'O': while(*p >= '0' && *p <= '7') {
                                      iret = (iret << 3) | (*p++ - '0');
                                  } 
                                  break;
			case 'B': while(*p == '0' || *p == '1') {
                                      iret = (iret << 1) | (*p++ - '0');
                                  } 
                                  break;
			default : iret = 0;
		}
	} else {
        fret = (MMFLOAT) strtod((char *)p, (char **)&t1);
        iret = strtoll((char *)p, (char **)&t2, 10);
        if (t1 > t2) targ = T_NBR;
    }
}

void fun_eval(void) {
    unsigned char *s, *st, *temp_tknbuf;
	int t;
    temp_tknbuf = GetTempMemory(STRINGSIZE);
    strcpy((char *)temp_tknbuf, (char *)tknbuf);                                    // first save the current token buffer in case we are in immediate mode
    // we have to fool the tokeniser into thinking that it is processing a program line entered at the console
    st = GetTempMemory(STRINGSIZE);
    strcpy((char *)st, (char *)getstring(ep));                                      // then copy the argument
    MtoC(st);                                                       // and convert to a C string
    inpbuf[0] = 'r'; inpbuf[1] = '=';                               // place a dummy assignment in the input buffer to keep the tokeniser happy
    strcpy((char *)inpbuf + 2, (char *)st);
	multi=false;
    tokenise(true);                                                 // and tokenise it (the result is in tknbuf)
  	strcpy((char *)st, (char *)(tknbuf + 2 + sizeof(CommandToken)));
    t = T_NOTYPE;
    evaluate(st, &fret, &iret, &s, &t, false);                   // get the value and type of the argument
    if(t & T_STR) {
        Mstrcpy(st, s);                                             // if it is a string then save it
        sret = st;
    }
	targ=t;
    strcpy((char *)tknbuf, (char *)temp_tknbuf);                                    // restore the saved token buffer
}


void fun_errno(void) {
    iret = MMerrno;
    targ = T_INT;
}


void fun_errmsg(void) {
    sret = GetTempMemory(STRINGSIZE);
    strcpy((char *)sret, MMErrMsg);
    CtoM(sret);
    targ = T_STR;
}



// Returns a string of blank spaces 'number' bytes long.
// s$ = SPACE$( number )
void fun_space(void) {
	int i;

	i = getint(ep, 0, MAXSTRLEN);
	sret = GetTempMemory(STRINGSIZE);									// this will last for the life of the command
	memset(sret + 1, ' ', i);
	*sret = i;
    targ = T_STR;
}



// Returns a string in the decimal (base 10) representation of  'number'.
// s$ = STR$( number, m, n, c$ )
void fun_str(void) {
	unsigned char *s;
	MMFLOAT f;
    long long int i64;
	int t;
    int m, n;
    unsigned char ch, *p;

    getargs(&ep, 7, (unsigned char *)",");
    if((argc & 1) != 1) error("Syntax");
    t = T_NOTYPE;
    p = evaluate(argv[0], &f, &i64, &s, &t, false);                 // get the value and type of the argument
    if(!(t & T_INT || t & T_NBR)) error("Expected a number");
    m = 0; n = STR_AUTO_PRECISION; ch = ' ';
    if(argc > 2) m = getint(argv[2], -128, 128);                    // get the number of digits before the point
    if(argc > 4) n = getint(argv[4], -20, 20);                      // get the number of digits after the point
	if(argc == 7) {
        p = getstring(argv[6]);
        if(*p == 0) error("Zero length argument");
        ch = ((unsigned char)p[1] & 0x7f);
	}

	sret = GetTempMemory(STRINGSIZE);									    // this will last for the life of the command
    if(t & T_NBR)
        FloatToStr((char *)sret, f, m, n, ch);                              // convert the float
    else {
        if(n < 0)
            FloatToStr((char *)sret, i64, m, n, ch);                        // convert as a float
        else {
            IntToStrPad((char *)sret, i64, ch, m, 10);                      // convert the integer
            if(n != STR_AUTO_PRECISION && n > 0) {
                strcat((char *)sret, ".");
                while(n--) strcat((char *)sret, "0");                       // and add on any zeros after the point
            }
        }
    }
	CtoM(sret);
    targ = T_STR;
}



// Returns a string 'nbr' bytes long
// s$ = STRING$( nbr,  string$ )
void fun_string(void) {
    int i, j, t = T_NOTYPE;
    void *p;

    getargs(&ep, 3, (unsigned char *)",");
    if(argc != 3) error("Syntax");

    i = getint(argv[0], 0, MAXSTRLEN);
    p = DoExpression(argv[2], &t);                                  // get the value and type of the argument
    if(t & T_STR) {
        if(!*(char *)p) error("Argument value: $", argv[2]);
        j = *((char *)p + 1);
    } else if(t & T_INT)
        j = *(long long int *)p;
    else
        j = FloatToInt32(*((MMFLOAT *)p));
    if(j < 0 || j > 255) error("Argument value: $", argv[2]);

    sret = GetTempMemory(STRINGSIZE);                                      // this will last for the life of the command
    memset(sret + 1, j, i);
    *sret = i;
    targ = T_STR;
}



// Returns string$ converted to uppercase characters.
// s$ = UCASE$( string$ )
void fun_ucase(void) {
	unsigned char *s, *p;
	int i;

	s = getstring(ep);
	p = sret = GetTempMemory(STRINGSIZE);								// this will last for the life of the command
	i = *p++ = *s++;												// get the length of the string and save in the destination
	while(i--) {
		*p = toupper(*s);
		p++; s++;
	}
    targ = T_STR;
}



// Returns string$ converted to lowercase characters.
// s$ = LCASE$( string$ )
void fun_lcase(void) {
	unsigned char *s, *p;
	int i;

	s = getstring(ep);
	p = sret = GetTempMemory(STRINGSIZE);								// this will last for the life of the command
	i = *p++ = *s++;												// get the length of the string and save in the destination
	while(i--) {
		*p = tolower(*s);
		p++; s++;
	}
    targ = T_STR;
}


// function (which looks like a pre defined variable) to return the version number
// it pulls apart the VERSION string to generate the number
void fun_version(void){
	char *p;
    fret = strtol(VERSION, &p, 10);
    fret += (MMFLOAT)strtol(p + 1, &p, 10) / 100;
    fret += (MMFLOAT)strtol(p + 1, &p, 10) / 10000;
    fret += (MMFLOAT)strtol(p + 1, &p, 10) / 1000000;
    targ = T_NBR;
}



// Returns the current cursor position in the line in characters.
// n = POS
void fun_pos(void){
	iret = MMCharPos;
    targ = T_INT;
}



// Outputs spaces until the column indicated by 'number' has been reached.
// PRINT TAB( number )
void fun_tab(void) {
	int i;
	unsigned char *p;

	i = getint(ep, 1, 255);
	sret = p = GetTempMemory(STRINGSIZE);							    // this will last for the life of the command
	if(MMCharPos > i) {
		i--;
		*p++ = '\r';
		*p++ = '\n';
	}
	else
		i -= MMCharPos;
	memset(p, ' ', i);
	p[i] = 0;
	CtoM(sret);
    targ = T_STR;
}



// get a character from the console input queue
// s$ = INKEY$
void __not_in_flash_func(fun_inkey)(void){
    int i;

	sret = GetTempMemory(STRINGSIZE);									// this buffer is automatically zeroed so the string is zero size

	i = MMInkey();
	if(i != -1) {
		sret[0] = 1;												// this is the length
		sret[1] = i;												// and this is the character
	}
    targ = T_STR;
}


/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

// used by ACos() and ASin() below
MMFLOAT arcsinus(MMFLOAT x) {
     return 2.0L * atan(x / (1.0L + sqrt(1.0L - x * x)));
}

/*  @endcond */

// Return the arcsine (in radians) of the argument 'number'.
// n = ASIN(number)
void fun_asin(void) {
     MMFLOAT f = getnumber(ep);
     if(f < -1.0 || f > 1.0) error("Number out of bounds");
     if (f == 1.0) {
          fret = M_PI_2;
     } else if (f == -1.0) {
          fret = -M_PI_2;
     } else {
          fret = arcsinus(f);
     }
	 fret *=optionangle;
     targ = T_NBR;
}


// Return the arccosine (in radians) of the argument 'number'.
// n = ACOS(number)
void fun_acos(void) {
     MMFLOAT f = getnumber(ep);
     if(f < -1.0L || f > 1.0L) error("Number out of bounds");
     if (f == 1.0L) {
          fret = 0.0L;
     } else if (f == -1.0L) {
          fret = M_PI;
     } else {
          fret = M_PI_2 - arcsinus(f);
     }
	 fret *=optionangle;
     targ = T_NBR;
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
// utility function to do the max/min comparison and return the value
// it is only called by fun_max() and fun_min() below.
void do_max_min(int cmp) {
    int i;
    MMFLOAT nbr, f;
    getargs(&ep, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
    if((argc & 1) != 1) error("Syntax");
    if(cmp) nbr = -FLT_MAX; else nbr = FLT_MAX;
    for(i = 0; i < argc; i += 2) {
        f = getnumber(argv[i]);
		if(cmp && f > nbr) nbr = f;
		if(!cmp && f < nbr) nbr = f;
    }
    fret = nbr;
    targ = T_NBR;
}
/*  @endcond */

void fun_max(void) {
    do_max_min(1);
}


void fun_min(void) {
    do_max_min(0);
}
#ifdef rp2350
void __not_in_flash_func(fun_ternary)(void){
#else
#ifdef PICOMITEVGA
void fun_ternary(void){
#else
void __not_in_flash_func(fun_ternary)(void){
#endif
#endif
    MMFLOAT f = 0;
    long long int i64 = 0;
    unsigned char *s = NULL;
    int t = T_NOTYPE;
	getargs(&ep,5,(unsigned char *)",");
	if(argc!=5)error("Syntax");
	int which=getnumber(argv[0]);
	if(which){
		evaluate(argv[2], &f, &i64, &s, &t, false);
	} else {
		evaluate(argv[4], &f, &i64, &s, &t, false);
	}
	if(t & T_INT){
		iret=i64;
		targ=T_INT;
		return;
	} else if (t & T_NBR){
		fret=f;
		targ=T_NBR;
		return;
	} else if (t & T_STR){
		sret=GetTempMemory(STRINGSIZE);
		Mstrcpy(sret, s);                                   // copy the string
		targ=T_STR;
		return;
	} else error("Syntax");
}
