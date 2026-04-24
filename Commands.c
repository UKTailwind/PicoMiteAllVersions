/***********************************************************************************************************************
PicoMite MMBasic

* @file commands.c

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
#ifndef PICOMITEMIN
#include "re.h"
#endif
#ifndef USBKEYBOARD
#include "class/cdc/cdc_device.h"
#endif
#ifdef PICOMITE
#include "pico/multicore.h"
#endif
#ifndef PICOMITEWEB
#include "Turtle.h"
#endif
#define overlap (VRes % (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) ? 0 : 1)

/* Stride-aware array access macros for struct member arrays */
#define STRIDE_FLOAT(ptr, idx, stride) (*(MMFLOAT *)((char *)(ptr) + (idx) * (stride)))
#define STRIDE_INT(ptr, idx, stride) (*(long long int *)((char *)(ptr) + (idx) * (stride)))
#include <math.h>
void flist(int, int, int);
// void clearprog(void);
char *KeyInterrupt = NULL;
unsigned char *SaveNextDataLine = NULL;
#ifdef MMBASIC_FM
int fm_program_launched_from_fm = 0;
char fm_relaunch_status[STRINGSIZE * 2] = {0};
int fm_relaunch_status_valid = 0;
int fm_suppress_error_output = 0;
char fm_last_launched_bas[FF_MAX_LFN] = {0};
char fm_error_file[FF_MAX_LFN] = {0};
int fm_error_line = 0;
int fm_error_char = 0;
int fm_error_location_valid = 0;
int fm_pending_edit_seek_valid = 0;
int fm_pending_edit_seek_line = 0;
int fm_pending_edit_seek_char = 0;
int fm_sanitize_next_console_input = 0;
#endif
void execute_one_command(unsigned char *p);
void ListNewLine(int *ListCnt, int all);
int printWrappedText(const char *text, int screenWidth, int listcnt, int all);
static void perf_print(const char *s, int *cnt);

static int VarNameLength(const unsigned char *name)
{
	int len = 0;
	while (len < MAXVARLEN && name[len] != 0)
		len++;
	return len;
}

static uint32_t VarNameHash(const unsigned char *name)
{
	uint32_t hash = FNV_offset_basis;
	for (int i = 0; i < MAXVARLEN && name[i] != 0; i++)
	{
		hash ^= name[i];
		hash *= FNV_prime;
	}
	return hash;
}

static int VarNamesEqual(const unsigned char *a, const unsigned char *b)
{
	int la = VarNameLength(a);
	int lb = VarNameLength(b);
	if (la != lb)
		return 0;
	return memcmp(a, b, la) == 0;
}

static void VarNameToC(char *dest, const unsigned char *name)
{
	int len = VarNameLength(name);
	if (len > MAXVARLEN)
		len = MAXVARLEN;
	memcpy(dest, name, len);
	dest[len] = 0;
}

static int IsActiveVariable(const struct s_vartbl *v)
{
#ifdef STRUCTENABLED
	return (v->type & (T_INT | T_STR | T_NBR | T_STRUCT)) != 0;
#else
	return (v->type & (T_INT | T_STR | T_NBR)) != 0;
#endif
}

static int PrintCollisionDomain(const char *domain_name, int start, int end, int slots, int local_domain, int *list_cnt)
{
	int groups = 0;
	char line[80];
	char namebuf[MAXVARLEN + 1];
	unsigned char *processed = (unsigned char *)GetTempMemory(slots);
	unsigned char **group_names = (unsigned char **)GetTempMemory((end - start) * sizeof(*group_names));

	memset(processed, 0, slots);

	for (int i = start; i < end; i++)
	{
		if (!IsActiveVariable(&g_vartbl[i]))
			continue;
		if (local_domain)
		{
			if (g_vartbl[i].level == 0)
				continue;
		}
		else
		{
			if (g_vartbl[i].level != 0)
				continue;
		}

		int bucket = (int)(VarNameHash(g_vartbl[i].name) % (uint32_t)slots);
		if (processed[bucket])
			continue;
		processed[bucket] = 1;

		int count = 0;
		for (int j = start; j < end; j++)
		{
			if (!IsActiveVariable(&g_vartbl[j]))
				continue;
			if (local_domain)
			{
				if (g_vartbl[j].level == 0)
					continue;
			}
			else
			{
				if (g_vartbl[j].level != 0)
					continue;
			}

			if ((int)(VarNameHash(g_vartbl[j].name) % (uint32_t)slots) != bucket)
				continue;

			int duplicate = 0;
			for (int k = 0; k < count; k++)
			{
				if (VarNamesEqual(group_names[k], g_vartbl[j].name))
				{
					duplicate = 1;
					break;
				}
			}
			if (!duplicate)
				group_names[count++] = g_vartbl[j].name;
		}

		if (count > 1)
		{
			groups++;
			snprintf(line, sizeof(line), "%s bucket %d:\r\n", domain_name, bucket);
			perf_print(line, list_cnt);
			for (int k = 0; k < count; k++)
			{
				VarNameToC(namebuf, group_names[k]);
				snprintf(line, sizeof(line), "  %s\r\n", namebuf);
				perf_print(line, list_cnt);
			}
		}
	}

	if (groups == 0)
	{
		snprintf(line, sizeof(line), "%s: none\r\n", domain_name);
		perf_print(line, list_cnt);
	}

	return groups;
}

char MMErrMsg[MAXERRMSG]; // the error message
volatile bool Keycomplete = false;
int keyselect = 0;
extern volatile unsigned int ScrewUpTimer;
int SaveNextData = 0;
struct sa_data datastore[MAXRESTORE];
int restorepointer = 0;
uint64_t g_flag = 0;
const uint8_t pinlist[] = { // this is a Basic program to print out the status of all the pins
	1, 132, 128, 95, 113, 37, 0,
	1, 153, 128, 95, 113, 37, 144, 48, 32, 204, 32, 241, 109, 97, 120, 32, 103, 112, 41, 0,
	1, 168, 128, 34, 71, 80, 34, 130, 186, 95, 113, 37, 41, 44, 32, 241, 112, 105, 110, 110, 111, 32, 34, 71, 80, 34, 130, 186,
	95, 113, 37, 41, 41, 44, 241, 112, 105, 110, 32, 241, 112, 105, 110, 110, 111, 32, 34, 71, 80, 34, 130, 186, 95, 113, 37, 41, 41, 41, 0,
	1, 166, 128, 0,
	1, 147, 128, 95, 113, 37, 0, 0};
const uint8_t i2clist[] = { // this is a Basic program to print out the I2C devices connected to the SYSTEM I2C pins
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
	1, 147, 128, 95, 97, 100, 0, 0};
// stack to keep track of nested FOR/NEXT loops
struct s_forstack g_forstack[MAXFORLOOPS + 1];
int g_forindex;

// stack to keep track of nested DO/LOOP loops
struct s_dostack g_dostack[MAXDOLOOPS];
int g_doindex; // counts the number of nested DO/LOOP loops

// stack to keep track of GOSUBs, SUBs and FUNCTIONs
unsigned char *gosubstack[MAXGOSUB];
unsigned char *errorstack[MAXGOSUB];
int gosubindex;

unsigned char g_DimUsed = false; // used to catch OPTION BASE after DIM has been used

#ifdef STRUCTENABLED
// Structure type definition table
struct s_structdef *g_structtbl[MAX_STRUCT_TYPES]; // Array of pointers, allocated per-type
int g_structcnt = 0;							   // Number of defined structure types
int g_StructArg = -1;							   // Struct index for pending DIM AS structtype (-1 if none)
int g_StructMemberType = 0;						   // Type of struct member being accessed (0 if not a member access)
int g_StructMemberOffset = 0;					   // Offset of member within struct (for EXTRACT/INSERT/SORT)
int g_StructMemberSize = 0;						   // Size of the member (for EXTRACT/INSERT/SORT)
int g_ExprStructType = -1;						   // Struct type index from expression evaluation (-1 if not a struct)
#endif

int TraceOn; // used to track the state of TRON/TROFF
unsigned char *TraceBuff[TRACE_BUFF_SIZE];
int TraceBuffIndex;	 // used for listing the contents of the trace buffer
int OptionErrorSkip; // how to handle an error
int MMerrno;		 // the error number
unsigned char cmdlinebuff[STRINGSIZE];
const unsigned int CaseOption = 0xffffffff; // used to store the case of the listed output

void __not_in_flash_func(cmd_null)(void)
{
	// do nothing (this is just a placeholder for commands that have no action)
}
/** @endcond */
/**
 * This command increments an integer or a float or concatenates two strings
 * @param a the integer, float or string to be changed
 * @param b OPTIONAL for integers and floats - defaults to 1. Otherwise the amount to increment the number or the string to concatenate
 */
#if LOWRAM
void MIPS16 cmd_inc(void)
{
#else
void MIPS16 __not_in_flash_func(cmd_inc)(void)
{
#endif
	unsigned char *p, *q;
	int vtype;
	getcsargs(&cmdline, 3);
	if (argc == 1)
	{
		p = findvar(argv[0], V_FIND);
		if (g_vartbl[g_VarIndex].type & T_CONST)
			StandardError(22);
		vtype = TypeMask(g_vartbl[g_VarIndex].type);
#ifdef STRUCTENABLED
		if (g_StructMemberType != 0)
			vtype = TypeMask(g_StructMemberType);
#endif
		if (vtype & T_STR)
			StandardError(6); // sanity check
		if (vtype & T_NBR)
			(*(MMFLOAT *)p) = (*(MMFLOAT *)p) + 1.0;
		else if (vtype & T_INT)
			*(long long int *)p = *(long long int *)p + 1;
		else
			SyntaxError();
		;
	}
	else
	{
		p = findvar(argv[0], V_FIND);
		if (g_vartbl[g_VarIndex].type & T_CONST)
			StandardError(22);
		vtype = TypeMask(g_vartbl[g_VarIndex].type);
#ifdef STRUCTENABLED
		if (g_StructMemberType != 0)
			vtype = TypeMask(g_StructMemberType);
#endif
		if (vtype & T_STR)
		{
			int size = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
			if (g_StructMemberType & T_STR)
				size = g_StructMemberSize;
#endif
			q = getstring(argv[2]);
			if (*p + *q > size)
				error("String too long");
			Mstrcat(p, q);
		}
		else if (vtype & T_NBR)
		{
			(*(MMFLOAT *)p) = (*(MMFLOAT *)p) + getnumber(argv[2]);
		}
		else if (vtype & T_INT)
		{
			*(long long int *)p = *(long long int *)p + getinteger(argv[2]);
		}
		else
			SyntaxError();
	}
}
// the PRINT command
void MIPS16 __not_in_flash_func(cmd_print)(void)
{
	unsigned char *s, *p;
	unsigned char *ss;
	MMFLOAT f;
	long long int i64;
	int i, t, fnbr;
	int docrlf; // this is used to suppress the cr/lf if needed

#define ADV_CHARPOS(ch)                                        \
	do                                                         \
	{                                                          \
		if ((ch) == '\r' || (ch) == '\n')                      \
		{                                                      \
			charpos = 1;                                       \
		}                                                      \
		else if ((ch) == '\t')                                 \
		{                                                      \
			int nexttab = (((charpos - 1) / 14) + 1) * 14 + 1; \
			charpos = nexttab;                                 \
		}                                                      \
		else                                                   \
		{                                                      \
			charpos++;                                         \
		}                                                      \
	} while (0)

#define ADV_CHUNK(ptr, len)                         \
	do                                              \
	{                                               \
		for (int adv_i = 0; adv_i < (len); adv_i++) \
			ADV_CHARPOS((ptr)[adv_i]);              \
	} while (0)

	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)";,"); // this is a macro and must be the first executable stmt

	//    s = 0; *s = 56;											    // for testing the exception handler

	docrlf = true;
	int charpos = MMCharPos; // Track current column for TAB() function

	if (argc > 0 && *argv[0] == '#')
	{ // check if the first arg is a file number
		argv[0]++;
		if ((*argv[0] == 'G') || (*argv[0] == 'g'))
		{
			argv[0]++;
			if (!((*argv[0] == 'P') || (*argv[0] == 'p')))
				SyntaxError();
			;
			argv[0]++;
			if (!((*argv[0] == 'S') || (*argv[0] == 's')))
				SyntaxError();
			;
			if (!GPSchannel)
				error("GPS not activated");
			if (argc != 3)
				error("Only a single string parameter allowed");
			p = argv[2];
			t = T_NOTYPE;
			p = evaluate(p, &f, &i64, &s, &t, true); // get the value and type of the argument
			ss = (unsigned char *)s;
			if (!(t & T_STR))
				error("Only a single string parameter allowed");
			int i, xsum = 0;
			if (ss[1] != '$' || ss[ss[0]] != '*')
				error("GPS command must start with dollar and end with star");
			for (i = 1; i <= ss[0]; i++)
			{
				SerialPutchar(GPSchannel, s[i]);
				if (s[i] == '$')
					xsum = 0;
				if (s[i] != '*')
					xsum ^= s[i];
			}
			i = xsum / 16;
			i = i + '0';
			if (i > '9')
				i = i - '0' + 'A';
			SerialPutchar(GPSchannel, i);
			i = xsum % 16;
			i = i + '0';
			if (i > '9')
				i = i - '0' + 'A';
			SerialPutchar(GPSchannel, i);
			SerialPutchar(GPSchannel, 13);
			SerialPutchar(GPSchannel, 10);
			return;
		}
		else
		{
			fnbr = getinteger(argv[0]); // get the number
			i = 1;
			if (argc >= 2 && *argv[1] == ',')
				i = 2; // and set the next argument to be looked at
		}
	}
	else
	{
		fnbr = 0; // no file number so default to the standard output
		i = 0;
	}

	// Get initial buffer for accumulating output
	unsigned char *outbuf = GetTempMemory(STRINGSIZE);
	unsigned char *bufptr = outbuf;
	int bufsize = STRINGSIZE;
	int used = 0;

	for (; i < argc; i++)
	{ // step through the arguments
		if (*argv[i] == ',')
		{
			// Check if we need more space for a tab
			if (used + 1 >= bufsize)
			{
				// Need to expand buffer
				unsigned char *newbuf = GetTempMemory(bufsize + STRINGSIZE);
				memcpy(newbuf, outbuf, used);
				ClearSpecificTempMemory(outbuf);
				outbuf = newbuf;
				bufptr = outbuf + used;
				bufsize += STRINGSIZE;
			}
			*bufptr++ = '\t';
			used++;
			ADV_CHARPOS('\t');
			docrlf = false; // a trailing comma should suppress CR/LF
		}
		else if (*argv[i] == ';')
		{
			docrlf = false; // other than suppress cr/lf do nothing for a semicolon
		}
		else
		{ // we have a normal expression
			p = argv[i];
			while (*p)
			{
				t = T_NOTYPE;
				MMCharPos = charpos;					 // Ensure TAB() sees the current column before evaluation
				p = evaluate(p, &f, &i64, &s, &t, true); // get the value and type of the argument
				if (t & T_NBR)
				{
					*inpbuf = ' ';																				   // preload a space
					FloatToStr((char *)inpbuf + ((f >= 0) ? 1 : 0), f, 0, STR_AUTO_PRECISION, (unsigned char)' '); // if positive output a space instead of the sign
					int len = strlen((char *)inpbuf);

					// Check if we need more space
					while (used + len >= bufsize)
					{
						unsigned char *newbuf = GetTempMemory(bufsize + STRINGSIZE);
						memcpy(newbuf, outbuf, used);
						ClearSpecificTempMemory(outbuf);
						outbuf = newbuf;
						bufptr = outbuf + used;
						bufsize += STRINGSIZE;
					}

					strcpy((char *)bufptr, (char *)inpbuf);
					bufptr += len;
					used += len;
					ADV_CHUNK(inpbuf, len);
				}
				else if (t & T_INT)
				{
					*inpbuf = ' ';											  // preload a space
					IntToStr((char *)inpbuf + ((i64 >= 0) ? 1 : 0), i64, 10); // if positive output a space instead of the sign
					int len = strlen((char *)inpbuf);

					// Check if we need more space
					while (used + len >= bufsize)
					{
						unsigned char *newbuf = GetTempMemory(bufsize + STRINGSIZE);
						memcpy(newbuf, outbuf, used);
						ClearSpecificTempMemory(outbuf);
						outbuf = newbuf;
						bufptr = outbuf + used;
						bufsize += STRINGSIZE;
					}

					strcpy((char *)bufptr, (char *)inpbuf);
					bufptr += len;
					used += len;
					ADV_CHUNK(inpbuf, len);
				}
				else if (t & T_STR)
				{
					// s is already a MMBasic string, need to extract the C string part
					unsigned char *cstr = s + 1; // Skip length byte
					int len = *s;				 // Get length from MMBasic string

					// Check if we need more space
					while (used + len >= bufsize)
					{
						unsigned char *newbuf = GetTempMemory(bufsize + STRINGSIZE);
						memcpy(newbuf, outbuf, used);
						ClearSpecificTempMemory(outbuf);
						outbuf = newbuf;
						bufptr = outbuf + used;
						bufsize += STRINGSIZE;
					}

					memcpy(bufptr, cstr, len);
					bufptr += len;
					used += len;
					ADV_CHUNK(cstr, len);
				}
				else
					error("Attempt to print reserved word");
			}
			docrlf = true;
		}
	}

	if (docrlf)
	{
		// Check if we need more space for cr/lf
		if (used + 2 >= bufsize)
		{
			unsigned char *newbuf = GetTempMemory(bufsize + STRINGSIZE);
			memcpy(newbuf, outbuf, used);
			ClearSpecificTempMemory(outbuf);
			outbuf = newbuf;
			bufptr = outbuf + used;
			bufsize += STRINGSIZE;
		}
		*bufptr++ = '\r';
		used++;
		ADV_CHARPOS('\r');
#ifdef USBKEYBOARD
		// For USB CDC host ports (COM3-COM6), send only CR, not CRLF
		if (!(fnbr >= 1 && fnbr <= MAXOPENFILES && FileTable[fnbr].com >= 3 && FileTable[fnbr].com <= 6))
#endif
		{
			*bufptr++ = '\n';
			used++;
			ADV_CHARPOS('\n');
		}
	}

	// Null terminate the C string
	*bufptr = '\0';

	// Output the buffer - don't null terminate, use explicit lengths
	if (used <= 255)
	{
		// Simple case: fits in a single MMBasic string
		unsigned char *mmstr = GetTempMemory(256);
		mmstr[0] = used;
		memcpy(mmstr + 1, outbuf, used);
		MMfputs(mmstr, fnbr);
	}
	else
	{
		// Need to output in chunks of maximum 255 bytes
		int offset = 0;
		unsigned char *mmstr = GetTempMemory(256);
		while (offset < used)
		{
			int chunklen = (used - offset > 255) ? 255 : (used - offset);
			mmstr[0] = chunklen;
			memcpy(mmstr + 1, outbuf + offset, chunklen);
			MMfputs(mmstr, fnbr);
			offset += chunklen;
		}
	}
	if (PrintPixelMode != 0)
		SSPrintString("\033[m");
	PrintPixelMode = 0;
	MMCharPos = charpos; // Final column state after buffered output

#undef ADV_CHUNK
#undef ADV_CHARPOS
}
void cmd_arrayset(void)
{
	array_set(cmdline);
}
void array_set(unsigned char *tp)
{
	MMFLOAT f;
	long long int i64;
	unsigned char *s;
#ifdef rp2350
	int dims[MAXDIM] = {0};
#else
	short dims[MAXDIM] = {0};
#endif
	int i, t, copy, card1 = 1;
	unsigned char size = 0;
	MMFLOAT *a1float = NULL;
	int64_t *a1int = NULL;
	unsigned char *a1str = NULL;
	int s1; // stride for numeric arrays
	getcsargs(&tp, 3);
	if (!(argc == 3))
		StandardError(2);
	findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t = g_vartbl[g_VarIndex].type;
	evaluate(argv[0], &f, &i64, &s, &t, false);
	if (t & T_STR)
	{
		card1 = parsestringarray(argv[2], &a1str, 2, 0, dims, true, &size);
		copy = (int)size + 1;
		memset(a1str, 0, copy * card1);
		if (*s)
		{
			for (i = 0; i < card1; i++)
			{
				Mstrcpy(&a1str[i * copy], s);
			}
		}
	}
	else
	{
		card1 = parsenumberarray(argv[2], &a1float, &a1int, 2, 0, dims, true, &s1);
		if (t & T_STR)
			SyntaxError();
		;

		if (a1float != NULL)
		{
			for (i = 0; i < card1; i++)
				STRIDE_FLOAT(a1float, i, s1) = ((t & T_INT) ? (MMFLOAT)i64 : f);
		}
		else
		{
			for (i = 0; i < card1; i++)
				STRIDE_INT(a1int, i, s1) = ((t & T_INT) ? i64 : FloatToInt64(f));
		}
	}
}
void cmd_add(void)
{
	array_add(cmdline);
}

void array_add(unsigned char *tp)
{
	MMFLOAT f;
	long long int i64;
	unsigned char *s;
#ifdef rp2350
	int dims[MAXDIM] = {0};
#else
	short dims[MAXDIM] = {0};
#endif
	int i, t, card1 = 1, card2 = 1;
	MMFLOAT *a1float = NULL, *a2float = NULL, scale;
	int64_t *a1int = NULL, *a2int = NULL;
	unsigned char *a1str = NULL, *a2str = NULL;
	int s1, s2;
	getcsargs(&tp, 5);
	if (!(argc == 5))
		StandardError(2);
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t = g_vartbl[g_VarIndex].type;
	if (t & T_STR)
	{
		unsigned char size = 0, size2 = 0;
		unsigned char *toadd;
		card1 = parsestringarray(argv[0], &a1str, 1, 0, dims, false, &size);
		evaluate(argv[2], &f, &i64, &s, &t, false);
		if (!(t & T_STR))
			SyntaxError();
		;
		toadd = getstring(argv[2]);
		card2 = parsestringarray(argv[4], &a2str, 3, 0, dims, true, &size2);
		if (card1 != card2)
			StandardError(16);
		unsigned char *buff = GetTempStrMemory(); // this will last for the life of the command
		int copy = size + 1;
		int copy2 = size2 + 1;
		for (i = 0; i < card1; i++)
		{
			unsigned char *sarg1 = a1str + i * copy;
			unsigned char *sarg2 = a2str + i * copy2;
			if (*sarg1 + *toadd > size2)
				error("String too long");
			Mstrcpy(buff, sarg1);
			Mstrcat(buff, toadd);
			Mstrcpy(sarg2, buff);
		}
	}
	else
	{
		card1 = parsenumberarray(argv[0], &a1float, &a1int, 1, 0, dims, false, &s1);
		evaluate(argv[2], &f, &i64, &s, &t, false);
		if (t & T_STR)
			SyntaxError();
		;
		scale = getnumber(argv[2]);
		card2 = parsenumberarray(argv[4], &a2float, &a2int, 3, 0, dims, true, &s2);
		if (card1 != card2)
			StandardError(16);
		if (scale != 0.0)
		{
			if (a2float != NULL && a1float != NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_FLOAT(a2float, i, s2) = ((t & T_INT) ? (MMFLOAT)i64 : f) + STRIDE_FLOAT(a1float, i, s1);
			}
			else if (a2float != NULL && a1float == NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_FLOAT(a2float, i, s2) = ((t & T_INT) ? (MMFLOAT)i64 : f) + ((MMFLOAT)STRIDE_INT(a1int, i, s1));
			}
			else if (a2float == NULL && a1float != NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_INT(a2int, i, s2) = FloatToInt64(((t & T_INT) ? i64 : FloatToInt64(f)) + STRIDE_FLOAT(a1float, i, s1));
			}
			else
			{
				for (i = 0; i < card1; i++)
					STRIDE_INT(a2int, i, s2) = ((t & T_INT) ? i64 : FloatToInt64(f)) + STRIDE_INT(a1int, i, s1);
			}
		}
		else
		{
			if (a2float != NULL && a1float != NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_FLOAT(a2float, i, s2) = STRIDE_FLOAT(a1float, i, s1);
			}
			else if (a2float != NULL && a1float == NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_FLOAT(a2float, i, s2) = ((MMFLOAT)STRIDE_INT(a1int, i, s1));
			}
			else if (a2float == NULL && a1float != NULL)
			{
				for (i = 0; i < card1; i++)
					STRIDE_INT(a2int, i, s2) = FloatToInt64(STRIDE_FLOAT(a1float, i, s1));
			}
			else
			{
				for (i = 0; i < card1; i++)
					STRIDE_INT(a2int, i, s2) = STRIDE_INT(a1int, i, s1);
			}
		}
	}
}
void cmd_insert(void)
{
	array_insert(cmdline);
}
void array_insert(unsigned char *tp)
{
	int i, j, t, start, increment, dim[MAXDIM], pos[MAXDIM], off[MAXDIM], dimcount = 0, target = -1;
	int64_t *a1int = NULL, *a2int = NULL;
	MMFLOAT *afloat = NULL;
	unsigned char *a1str = NULL, *a2str = NULL;
	unsigned char size = 0, size2 = 0;
#ifdef rp2350
	int dims[MAXDIM] = {0};
#else
	short dims[MAXDIM] = {0};
#endif
	getcsargs(&tp, 15);
	if (argc < 7)
		StandardError(2);
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t = g_vartbl[g_VarIndex].type;
	if (t & T_STR)
	{
		parsestringarray(argv[0], &a1str, 1, 0, dims, false, &size);
	}
	else
	{
		parsenumberarray(argv[0], &afloat, &a1int, 1, 0, dims, false, NULL);
		if (!a1int)
			a1int = (int64_t *)afloat;
	}
	if (dims[1] <= 0)
		error("Argument 1 must be a 2D or more array");
	for (i = 0; i < MAXDIM; i++)
	{
		if (dims[i] - g_OptionBase > 0)
		{
			dimcount++;
			dim[i] = dims[i] - g_OptionBase;
		}
		else
			dim[i] = 0;
	}
	if (((argc - 1) / 2 - 1) != dimcount)
		StandardError(2);
	for (i = 0; i < dimcount; i++)
	{
		if (*argv[i * 2 + 2])
			pos[i] = getint(argv[i * 2 + 2], g_OptionBase, dim[i] + g_OptionBase) - g_OptionBase;
		else
		{
			if (target != -1)
				error("Only one index can be omitted");
			target = i;
			pos[i] = 1;
		}
	}
	if (t & T_STR)
	{
		parsestringarray(argv[i * 2 + 2], &a2str, i + 1, 1, dims, true, &size2);
	}
	else
	{
		parsenumberarray(argv[i * 2 + 2], &afloat, &a2int, i + 1, 1, dims, true, NULL);
		if (!a2int)
			a2int = (int64_t *)afloat;
	}
	if (target == -1)
		return;
	if (dim[target] + g_OptionBase != dims[0])
		error("Size mismatch between insert and target array");
	if (size != size2)
		error("String arrays differ in string length");
	i = dimcount - 1;
	while (i >= 0)
	{
		off[i] = 1;
		for (j = 0; j < i; j++)
			off[i] *= (dim[j] + 1);
		i--;
	}
	start = 1;
	for (i = 0; i < dimcount; i++)
	{
		start += (pos[i] * off[i]);
	}
	start--;
	increment = off[target];
	start -= increment;
	if (t & T_STR)
	{
		int copy = (int)size + 1;
		for (i = 0; i <= dim[target]; i++)
		{
			unsigned char *p = a2str + i * copy;
			unsigned char *q = &a1str[(start + i * increment) * copy];
			memcpy(q, p, copy);
		}
	}
	else
	{
		for (i = 0; i <= dim[target]; i++)
			a1int[start + i * increment] = *a2int++;
	}
	return;
}
void cmd_slice(void)
{
	array_slice(cmdline);
}
void array_slice(unsigned char *tp)
{
	int i, j, t, start, increment, dim[MAXDIM], pos[MAXDIM], off[MAXDIM], dimcount = 0, target = -1, toarray = 0;
	int64_t *a1int = NULL, *a2int = NULL;
	MMFLOAT *afloat = NULL;
	unsigned char *a1str = NULL, *a2str = NULL;
	unsigned char size = 0, size2 = 0;
#ifdef rp2350
	int dims[MAXDIM] = {0};
#else
	short dims[MAXDIM] = {0};
#endif
	getcsargs(&tp, 15);
	if (argc < 7)
		StandardError(2);
	findvar(argv[0], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
	t = g_vartbl[g_VarIndex].type;
	if (t & T_STR)
	{
		parsestringarray(argv[0], &a1str, 1, 0, dims, false, &size);
	}
	else
	{
		parsenumberarray(argv[0], &afloat, &a1int, 1, 0, dims, false, NULL);
		if (!a1int)
			a1int = (int64_t *)afloat;
	}
	if (dims[1] <= 0)
		error("Argument 1 must be a 2D or more array");
	for (i = 0; i < MAXDIM; i++)
	{
		if (dims[i] - g_OptionBase > 0)
		{
			dimcount++;
			dim[i] = dims[i] - g_OptionBase;
		}
		else
			dim[i] = 0;
	}
	if (((argc - 1) / 2 - 1) != dimcount)
		StandardError(2);
	for (i = 0; i < dimcount; i++)
	{
		if (*argv[i * 2 + 2])
			pos[i] = getint(argv[i * 2 + 2], g_OptionBase, dim[i] + g_OptionBase) - g_OptionBase;
		else
		{
			if (target != -1)
				error("Only one index can be omitted");
			target = i;
			pos[i] = 1;
		}
	}
	if (t & T_STR)
	{
		toarray = parsestringarray(argv[i * 2 + 2], &a2str, i + 1, 1, dims, true, &size2) - 1;
	}
	else
	{
		toarray = parsenumberarray(argv[i * 2 + 2], &afloat, &a2int, i + 1, 1, dims, true, NULL) - 1;
		if (!a2int)
			a2int = (int64_t *)afloat;
	}
	if (dim[target] != toarray)
		error("Size mismatch between slice and target array");
	if (size != size2)
		error("String arrays differ in string length");
	i = dimcount - 1;
	while (i >= 0)
	{
		off[i] = 1;
		for (j = 0; j < i; j++)
			off[i] *= (dim[j] + 1);
		i--;
	}
	start = 1;
	for (i = 0; i < dimcount; i++)
	{
		start += (pos[i] * off[i]);
	}
	start--;
	increment = off[target];
	start -= increment;
	if (t & T_STR)
	{
		int copy = (int)size + 1; // allow for the length character of the string
		for (i = 0; i <= dim[target]; i++)
		{
			unsigned char *p = a2str + i * copy;
			unsigned char *q = &a1str[(start + i * increment) * copy];
			memcpy(p, q, copy);
		}
	}
	else
	{
		for (i = 0; i <= dim[target]; i++)
			*a2int++ = a1int[start + i * increment];
	}
	return;
}

// the LET command
// because the LET is implied (ie, line does not have a recognisable command)
// it ends up as the place where mistyped commands are discovered.  This is why
// the error message is "Unknown command"
#if defined(rp2350) || defined(PICOMITEMIN)
void __not_in_flash_func(cmd_let)(void)
#else
void MIPS16 __not_in_flash_func(cmd_let)(void)
#endif
{
	int t, size;
	MMFLOAT f;
	long long int i64;
	unsigned char *s;
	unsigned char *p1, *p2;
	int vartype; // effective type (may differ for struct members)

#ifdef CACHE
	/* Trace-cache fast path (Phase 1.1: top-level global scalar LET only).
	 * Gate inline so the OFF case is one load + one branch (no call).        */
	if ((g_trace_cache_flags & (TCF_LET_NUM | TCF_LET_STR)) && TraceCacheTryLet(cmdline))
		return;
#endif
	p1 = cmdline;
	// search through the line looking for the equals sign
	while (*p1 && tokenfunction(*p1) != op_equal)
		p1++;
	if (!*p1)
		StandardError(36);

	// check that we have a straight forward variable
	p2 = skipvar(cmdline, false);
	skipspace(p2);
	if (p1 != p2)
		SyntaxError();
	;

	// create the variable and get the length if it is a string
	p2 = findvar(cmdline, V_FIND);
	size = g_vartbl[g_VarIndex].size;
	if (g_vartbl[g_VarIndex].type & T_CONST)
		StandardError(22);

#ifdef STRUCTENABLED
	// For struct member access, use the member type instead of the base variable type
	if (g_StructMemberType != 0)
	{
		vartype = g_StructMemberType;
		// For string members, use the size set by ResolveStructMember
		if (vartype & T_STR)
		{
			size = g_StructMemberSize;
		}
	}
	else
	{
		vartype = g_vartbl[g_VarIndex].type;
	}
#else
	vartype = g_vartbl[g_VarIndex].type;
#endif

	// step over the equals sign, evaluate the rest of the command and save in the variable
	p1++;
	if (vartype & T_STR)
	{
		t = T_STR;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if (*s > size)
			error("String too long");
		Mstrcpy(p2, s);
	}
	else if (vartype & T_NBR)
	{
		t = T_NBR;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if (t & T_NBR)
			(*(MMFLOAT *)p2) = f;
		else
			(*(MMFLOAT *)p2) = (MMFLOAT)i64;
	}
#ifdef STRUCTENABLED
	else if (vartype & T_STRUCT)
	{
		// Struct assignment - evaluate the right side and copy struct data
		// Save destination info BEFORE evaluate changes g_VarIndex
		int dest_struct_idx = (int)g_vartbl[g_VarIndex].size;
		int dest_struct_size = g_structtbl[dest_struct_idx]->total_size;

		t = T_NOTYPE; // Let evaluate determine the actual type
		p1 = evaluate(p1, &f, &i64, &s, &t, false);

		// Check that RHS is a struct
		if (!(t & T_STRUCT))
		{
			error("Expected a structure value");
		}

		// Validate struct types match (g_ExprStructType set by getvalue or function return)
		if (g_ExprStructType >= 0 && g_ExprStructType != dest_struct_idx)
		{
			error("Structure types must match");
		}

		if (s != NULL)
		{
			if (dest_struct_idx >= 0 && dest_struct_idx < g_structcnt)
			{
				memcpy(p2, s, dest_struct_size);
			}
			else
			{
				error("Invalid struct type");
			}
		}
		else
		{
			error("No struct value");
		}
	}
#endif
	else
	{
		t = T_INT;
		p1 = evaluate(p1, &f, &i64, &s, &t, false);
		if (t & T_INT)
			(*(long long int *)p2) = i64;
		else
			(*(long long int *)p2) = FloatToInt64(f);
	}
	checkend(p1);
}
/**
 * @cond
 * The following section will be excluded from the documentation.
 */
int MIPS16 as_strcmpi(const char *s1, const char *s2)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	unsigned char c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = tolower(*p1++);
		c2 = tolower(*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return c1 - c2;
}
void MIPS16 sortStrings(char **arr, int n)
{
	char temp[STRINGSIZE];
	int i, j;
	// Sorting strings using bubble sort
	for (j = 0; j < n - 1; j++)
	{
		for (i = j + 1; i < n; i++)
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
// Read one file line character-by-character and display it with word-wrap.
// Handles lines of any length without a large buffer or error() calls.
// Tab characters are expanded to spaces. Empty lines produce no output,
// matching the behaviour of printWrappedText("", ...).
// Returns updated listcnt.
static int file_display_line(int fnbr, int screenWidth, int listcnt, int all)
{
	char seg[STRINGSIZE]; // holds at most screenWidth (<=255) chars + NUL
	int seg_len = 0;

	for (;;)
	{
		// Fill the segment buffer up to screenWidth chars or end of line.
		bool eol = false;
		while (seg_len < screenWidth)
		{
			if (FileEOF(fnbr)) { eol = true; break; }
			int c = MMfgetc(fnbr);
			if (c == '\r') continue;
			if (c == '\n') { eol = true; break; }
			if (c <= 0) continue;
			if (c == '\t') c = ' ';
			seg[seg_len++] = (char)c;
		}

		if (eol)
		{
			// End of line: print remaining segment (if any) and newline.
			// seg_len==0 means an empty line; produce no output to match
			// the existing printWrappedText("") behaviour.
			if (seg_len > 0)
			{
				seg[seg_len] = 0;
				MMPrintString(seg);
				ListNewLine(&listcnt, all);
			}
			break;
		}

		// Segment is full and the line continues — need to wrap.
		// Find the last space in the segment (word-wrap break point).
		int last_space = -1;
		for (int i = 0; i < seg_len; i++)
			if (seg[i] == ' ') last_space = i;

		if (last_space >= 0)
		{
			// Word-wrap: print up to (not including) the space.
			seg[last_space] = 0;
			MMPrintString(seg);
			ListNewLine(&listcnt, all);
			// Carry chars after the space forward into the next segment.
			int carry = seg_len - last_space - 1;
			memmove(seg, seg + last_space + 1, carry);
			seg_len = carry;
		}
		else
		{
			// No space found: hard-break at screen width.
			seg[seg_len] = 0;
			MMPrintString(seg);
			ListNewLine(&listcnt, all);
			seg_len = 0;
		}
	}

	return listcnt;
}

// Count the number of screen rows that file_display_line would produce for
// the next file line, using the same word-wrap algorithm.  Seeks back to the
// start of the line afterwards so the caller can re-read and display it.
// Returns 0 for empty lines (matching countWrappedLines("") == 0).
static int file_count_line_rows(int fnbr, int screenWidth)
{
	int saved_pos = filegetpos(fnbr);
	char seg[STRINGSIZE];
	int seg_len = 0;
	int rows = 0;

	for (;;)
	{
		bool eol = false;
		while (seg_len < screenWidth)
		{
			if (FileEOF(fnbr)) { eol = true; break; }
			int c = MMfgetc(fnbr);
			if (c == '\r') continue;
			if (c == '\n') { eol = true; break; }
			if (c <= 0) continue;
			if (c == '\t') c = ' ';
			seg[seg_len++] = (char)c;
		}

		if (eol)
		{
			if (seg_len > 0) rows++;
			break;
		}

		// Segment full — wrap and continue counting.
		int last_space = -1;
		for (int i = 0; i < seg_len; i++)
			if (seg[i] == ' ') last_space = i;

		rows++;
		if (last_space >= 0)
		{
			int carry = seg_len - last_space - 1;
			memmove(seg, seg + last_space + 1, carry);
			seg_len = carry;
		}
		else
		{
			seg_len = 0;
		}
	}

	positionfile(fnbr, saved_pos, false);
	return rows;
}

void MIPS16 ListFile(char *pp, int all)
{
	int fnbr;
	int ListCnt = CurrentY / (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
	fnbr = FindFreeFileNbr();
	if (!BasicFileOpen(pp, fnbr, FA_READ))
		return;
	while (!FileEOF(fnbr))
	{
		ListCnt = file_display_line(fnbr, Option.Width, ListCnt, all);
		routinechecks();
		CheckAbort();
	}
	FileClose(fnbr);
}

#define LIST_MAX_PAGES 64

static int countWrappedLines(const char *text, int screenWidth)
{
	int len = strlen(text);
	int start = 0, count = 0;
	while (start < len)
	{
		int end = start + screenWidth;
		if (end >= len) { count++; break; }
		int lastSpace = -1;
		for (int i = start; i < end; i++)
			if (text[i] == ' ') lastSpace = i;
		if (lastSpace != -1) { count++; start = lastSpace + 1; }
		else                  { count++; start += screenWidth; }
	}
	return count;
}

void ListFilePaged(char *pp)
{
	int fnbr;
	int page_starts[LIST_MAX_PAGES];
	int nstarts, cur_page, list_cnt;
	int page_threshold = Option.Height - overlap;
	bool need_clear = false;

	fnbr = FindFreeFileNbr();
	if (!BasicFileOpen(pp, fnbr, FA_READ))
		return;

	page_starts[0] = 0;
	nstarts = 1;
	cur_page = 0;

	for (;;)
	{
		if (need_clear && Option.DISPLAY_CONSOLE)
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		need_clear = true;

		positionfile(fnbr, page_starts[cur_page], false);
		list_cnt = 2;
		bool overflowed = false;

		while (!FileEOF(fnbr))
		{
			int line_pos = filegetpos(fnbr);
			// Count rows this line will occupy without consuming it, then
			// display it.  file_count_line_rows seeks back to line_pos.
			int rows = file_count_line_rows(fnbr, Option.Width);
			if (list_cnt > 2 && list_cnt + rows >= page_threshold)
			{
				if (cur_page + 1 >= nstarts && nstarts < LIST_MAX_PAGES)
					page_starts[nstarts++] = line_pos;
				overflowed = true;
				break;
			}
			list_cnt = file_display_line(fnbr, Option.Width, list_cnt, true);
			routinechecks();
			CheckAbort();
		}

		clearrepeat();
		if (cur_page > 0)
			MMPrintString(overflowed ? "UP=prev  ANY KEY=next ..." :
			                           "UP=prev  ANY KEY=quit ...");
		else
			MMPrintString("PRESS ANY KEY ...");

		int c = -1;
		while (c == -1) { routinechecks(); c = MMInkey(); }
		MMPrintString("\r                              \r");

		if (c == UP) {
			if (cur_page > 0)
				cur_page--;
			/* else UP on page 0: no-op, loop re-displays page 0 */
		} else if (!overflowed)
			break;
		else if (cur_page + 1 < nstarts)
			cur_page++;
		else
			break;
	}

	FileClose(fnbr);
}

void MIPS16 ListNewLine(int *ListCnt, int all)
{
	unsigned char noscroll = Option.NoScroll;
	if (!all && (void *)ReadBuffer != (void *)DisplayNotSet)
		Option.NoScroll = 0;
	MMPrintString("\r\n");
	(*ListCnt)++;
	if (!all && *ListCnt >= Option.Height - overlap)
	{
		clearrepeat();
		MMPrintString("PRESS ANY KEY ...");
		MMgetchar();
		MMPrintString("\r                 \r");
		if (Option.DISPLAY_CONSOLE)
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		*ListCnt = 2;
	}
	Option.NoScroll = noscroll;
}

void MIPS16 ListProgram(unsigned char *p, int all)
{
	char b[STRINGSIZE];
	char *pp;
	int ListCnt = CurrentY / (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
	while (!(*p == 0 || *p == 0xff))
	{ // normally a LIST ends at the break so this is a safety precaution
		if (*p == T_NEWLINE)
		{
			p = llist((unsigned char *)b, p); // otherwise expand the line
			if (!(b[0] == '\'' && b[1] == '#'))
			{
				pp = b;
				if (Option.continuation)
				{
					format_string(pp, Option.Width);
					while (*pp)
					{
						if (*pp == '\n')
						{
							ListNewLine(&ListCnt, all);
							pp++;
							continue;
						}
						MMputchar(*pp++, 0);
					}
				}
				else
				{
					while (*pp)
					{
						if (MMCharPos > Option.Width)
							ListNewLine(&ListCnt, all);
						MMputchar(*pp++, 0);
					}
				}
#ifndef USBKEYBOARD
				// fflush(stdout);
				tud_cdc_write_flush();
#endif
				ListNewLine(&ListCnt, all);
				routinechecks();
				CheckAbort();
				if (p[0] == 0 && p[1] == 0)
					break; // end of the listing ?
			}
		}
	}
}

void ListProgramPaged(unsigned char *prog)
{
	char b[STRINGSIZE];
	char *pp;
	unsigned char *page_starts[LIST_MAX_PAGES];
	int nstarts = 1, cur_page = 0;
	int page_threshold = Option.Height - overlap;
	bool need_clear = false;

	page_starts[0] = prog;

	for (;;)
	{
		if (need_clear && Option.DISPLAY_CONSOLE)
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		need_clear = true;

		unsigned char *p = page_starts[cur_page];
		int list_cnt = 2;
		bool overflowed = false;

		while (!(*p == 0 || *p == 0xff))
		{
			if (*p == T_NEWLINE)
			{
				unsigned char *line_start = p;
				p = llist((unsigned char *)b, p);
				if (b[0] == '\'' && b[1] == '#')
					continue;

				pp = b;
				int rows = countWrappedLines(pp, Option.Width);
				if (list_cnt > 2 && list_cnt + rows >= page_threshold)
				{
					if (cur_page + 1 >= nstarts && nstarts < LIST_MAX_PAGES)
						page_starts[nstarts++] = line_start;
					overflowed = true;
					break;
				}

				if (Option.continuation)
				{
					format_string(pp, Option.Width);
					while (*pp)
					{
						if (*pp == '\n')
						{
							ListNewLine(&list_cnt, true);
							pp++;
							continue;
						}
						MMputchar(*pp++, 0);
					}
				}
				else
				{
					while (*pp)
					{
						if (MMCharPos > Option.Width)
							ListNewLine(&list_cnt, true);
						MMputchar(*pp++, 0);
					}
				}
#ifndef USBKEYBOARD
				tud_cdc_write_flush();
#endif
				ListNewLine(&list_cnt, true);
				routinechecks();
				CheckAbort();
				if (p[0] == 0 && p[1] == 0)
					break;
			}
		}

		clearrepeat();
		if (cur_page > 0)
			MMPrintString(overflowed ? "UP=prev  ANY KEY=next ..." :
			                           "UP=prev  ANY KEY=quit ...");
		else
			MMPrintString("PRESS ANY KEY ...");

		int c = -1;
		while (c == -1) { routinechecks(); c = MMInkey(); }
		MMPrintString("\r                              \r");

		if (c == UP)
		{
			if (cur_page > 0)
				cur_page--;
		}
		else if (!overflowed)
			break;
		else if (cur_page + 1 < nstarts)
			cur_page++;
		else
			break;
	}
}

void MIPS16 do_run(unsigned char *cmdline, bool CMM2mode)
{
	extern void ResetPerfCounters(void);
	ResetPerfCounters();
	// RUN [ filename$ ] [, cmd_args$ ]
	unsigned char *filename = (unsigned char *)"", *cmd_args = (unsigned char *)"";
	unsigned char *cmdbuf = GetTempMemory(256);
	memcpy(cmdbuf, cmdline, STRINGSIZE);
	getcsargs(&cmdbuf, 3);
	switch (argc)
	{
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
		if (*argv[2])
			cmd_args = getCstring(argv[2]);
		break;
	}

	// The memory allocated by getCstring() is not preserved across
	// a call to FileLoadProgram() so we need to cache 'filename' and
	// 'cmd_args' on the stack.
	unsigned char buf[MAXSTRLEN + 1];
	if (snprintf((char *)buf, MAXSTRLEN + 1, "\"%s\",%s", filename, cmd_args) > MAXSTRLEN)
	{
		error("RUN command line too long");
	}
	unsigned char *pcmd_args = buf + strlen((char *)filename) + 3; // *** THW 16/4/23

#ifdef rp2350
	if (CMM2mode)
	{
		if (*filename && !FileLoadCMM2Program((char *)buf, false))
			return;
	}
	else
	{
#endif
		if (*filename && !FileLoadProgram(buf, false))
			return;
#ifdef rp2350
	}
#endif
	ClearRuntime(true);
	if (PrepareProgram(true))
	{
		// Error in program - print message and don't run
		PrintPreprogramError();
		return;
	}
	if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
	{
		ClearScreen(gui_bcolour);
		CurrentX = 0;
		CurrentY = 0;
	}
	// Create a global constant MM.CMDLINE$ containing 'cmd_args'.
	//    void *ptr = findvar((unsigned char *)"MM.CMDLINE$", V_FIND | V_DIM_VAR | T_CONST);
	CtoM(pcmd_args);
	//    memcpy(cmdlinebuff, pcmd_args, *pcmd_args + 1); // *** THW 16/4/23
	Mstrcpy(cmdlinebuff, pcmd_args);
	IgnorePIN = false;
	//	uint8_t *dummy __attribute((unused))=GetMemory(STRINGSIZE);
	if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
		ExecuteProgram(LibMemory); // run anything that might be in the library
	if (*ProgMemory != T_NEWLINE)
		return; // no program to run
#ifdef PICOMITEWEB
	cleanserver();
#endif
#ifndef USBKEYBOARD
	if (mouse0 == false && Option.MOUSE_CLOCK)
		initMouse0(0); // see if there is a mouse to initialise
#endif
	nextstmt = ProgMemory;
}
/** @endcond */
void MIPS16 cmd_list(void)
{
	unsigned char *p;
	int i, j, k, m, step;
	if ((p = checkstring(cmdline, (unsigned char *)"ALL")))
	{
		if (!(*p == 0 || *p == '\''))
		{
			if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
			{
				ClearScreen(gui_bcolour);
				CurrentX = 0;
				CurrentY = 0;
			}
			getcsargs(&p, 1);
			char *buff = GetTempStrMemory();
			strcpy(buff, (char *)getCstring(argv[0]));
			if (strchr(buff, '.') == NULL)
				strcat(buff, ".bas");
			ListFile(buff, true);
		}
		else
		{
			if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
			{
				ClearScreen(gui_bcolour);
				CurrentX = 0;
				CurrentY = 0;
			}
			ListProgram(ProgMemory, true);
			checkend(p);
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"OPTIONS")))
	{
		printoptions();
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"VARIABLES")))
	{
		int count = 0;
		int64_t *dest = NULL;
		char *buff = NULL;
		int j = 0;
		getcsargs(&p, 1);
		if (argc)
		{
			j = (parseintegerarray(argv[0], &dest, 1, 1, NULL, true, NULL) - 1) * 8;
			dest[0] = 0;
		}
		for (int i = 0; i < MAXVARS; i++)
		{
#ifdef STRUCTENABLED
			if (g_vartbl[i].type & (T_INT | T_STR | T_NBR | T_STRUCT))
#else
			if (g_vartbl[i].type & (T_INT | T_STR | T_NBR))
#endif
			{
				count++;
			}
		}
		if (!count)
			return;
		char **c = GetTempMainMemory(count * sizeof(*c) + count * (MAXVARLEN + 30));
		for (int i = 0, j = 0; i < MAXVARS; i++)
		{
			char out[MAXVARLEN + 30];
#ifdef STRUCTENABLED
			if (g_vartbl[i].type & (T_INT | T_STR | T_NBR | T_STRUCT))
#else
			if (g_vartbl[i].type & (T_INT | T_STR | T_NBR))
#endif
			{
				if (g_vartbl[i].level == 0)
					strcpy(out, "DIM ");
				else
					strcpy(out, "LOCAL ");
#ifdef STRUCTENABLED
				// Handle structure types
				if (g_vartbl[i].type & T_STRUCT)
				{
					// size field holds struct index
					int struct_idx = (int)g_vartbl[i].size;
					if (struct_idx >= 0 && struct_idx < g_structcnt)
					{
						strcat(out, (char *)g_vartbl[i].name);
						// Array dimensions for structs
						if (g_vartbl[i].dims[0] > 0)
						{
							strcat(out, "(");
							for (int k = 0; k < MAXDIM; k++)
							{
								if (g_vartbl[i].dims[k] > 0)
								{
									char s[20];
									IntToStr(s, (int64_t)g_vartbl[i].dims[k], 10);
									strcat(out, s);
								}
								if (k < MAXDIM - 1 && g_vartbl[i].dims[k + 1] > 0)
									strcat(out, ",");
							}
							strcat(out, ")");
						}
						strcat(out, " AS ");
						strcat(out, (char *)g_structtbl[struct_idx]->name);
					}
					else
					{
						strcat(out, (char *)g_vartbl[i].name);
						strcat(out, " AS <invalid>");
					}
					// Skip to c[j] assignment for struct types
					c[j] = (char *)((int)c + sizeof(char *) * count + j * (MAXVARLEN + 30));
					strcpy(c[j], out);
					j++;
					continue;
				}
#endif
				if (!(g_vartbl[i].namelen & NAMELEN_EXPLICIT))
				{
					if (g_vartbl[i].type & T_INT)
					{
						if (!(g_vartbl[i].namelen & NAMELEN_EXPLICIT))
							strcat(out, "INTEGER ");
					}
					if (g_vartbl[i].type & T_STR)
					{
						if (!(g_vartbl[i].namelen & NAMELEN_EXPLICIT))
							strcat(out, "STRING ");
					}
					if (g_vartbl[i].type & T_NBR)
					{
						if (!(g_vartbl[i].namelen & NAMELEN_EXPLICIT))
							strcat(out, "FLOAT ");
					}
				}
				strcat(out, (char *)g_vartbl[i].name);
				if (g_vartbl[i].type & T_INT)
				{
					if (g_vartbl[i].namelen & NAMELEN_EXPLICIT)
						strcat(out, "%");
				}
				if (g_vartbl[i].type & T_STR)
				{
					if (g_vartbl[i].namelen & NAMELEN_EXPLICIT)
						strcat(out, "$");
				}
				if (g_vartbl[i].type & T_NBR)
				{
					if (g_vartbl[i].namelen & NAMELEN_EXPLICIT)
						strcat(out, "!");
				}
				if (g_vartbl[i].dims[0] > 0)
				{
					strcat(out, "(");
					for (int k = 0; k < MAXDIM; k++)
					{
						if (g_vartbl[i].dims[k] > 0)
						{
							char s[20];
							IntToStr(s, (int64_t)g_vartbl[i].dims[k], 10);
							strcat(out, s);
						}
						if (k < MAXDIM - 1 && g_vartbl[i].dims[k + 1] > 0)
							strcat(out, ",");
					}
					strcat(out, ")");
				}
				c[j] = (char *)((int)c + sizeof(char *) * count + j * (MAXVARLEN + 30));
				strcpy(c[j], out);
				j++;
			}
		}
		sortStrings(c, count);
		int ListCnt = 2;
		if (dest == NULL)
		{
			for (int i = 0; i < count; i++)
			{
				MMPrintString(c[i]);
				if (Option.DISPLAY_CONSOLE)
					ListNewLine(&ListCnt, 0);
				else
					MMPrintString("\r\n");
			}
		}
		else
		{
			int ol = 0;
			buff = (char *)&dest[1];
			for (int i = 0; i < count; i++)
			{
				if (ol + strlen(c[i]) + 2 > j)
					StandardError(23);
				else
					ol += strlen(c[i]) + 2;
				strcat(buff, c[i]);
				strcat(buff, "\r\n");
			}
			dest[0] = ol;
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"COLLISIONS")))
	{
		checkend(p);
		int list_cnt = 2;
		int local_slots = GetLocalVarHashSize();
		int global_slots = GetGlobalVarHashSize();
		if (local_slots < 1 || local_slots >= MAXVARS)
			local_slots = MAXLOCALVARS;
		if (global_slots < 1 || local_slots + global_slots != MAXVARS)
			global_slots = MAXVARS - local_slots;

		int groups = 0;
		groups += PrintCollisionDomain("LOCAL", 0, local_slots, local_slots, 1, &list_cnt);
		groups += PrintCollisionDomain("GLOBAL", local_slots, MAXVARS, global_slots, 0, &list_cnt);
		if (groups == 0)
			perf_print("No hash collisions found\r\n", &list_cnt);
	}
#ifdef STRUCTENABLED
	else if ((p = checkstring(cmdline, (unsigned char *)"TYPE")))
	{
		// LIST TYPE [typename] - Display structure type definitions
		int i, j;
		unsigned char typename[MAXVARLEN + 1];
		int found = -1;

		skipspace(p);

		if (*p && *p != '\'')
		{
			// Specific type name provided
			int namelen = 0;
			while (isnamechar(*p) && namelen < MAXVARLEN)
			{
				typename[namelen++] = mytoupper(*p++);
			}
			typename[namelen] = 0;

			// Find the type
			for (i = 0; i < g_structcnt; i++)
			{
				if (strcmp((char *)typename, (char *)g_structtbl[i]->name) == 0)
				{
					found = i;
					break;
				}
			}
			if (found < 0)
				error("TYPE not found");
		}

		if (g_structcnt == 0)
		{
			MMPrintString("No structure types defined\r\n");
			return;
		}

		// Print structure(s)
		for (i = 0; i < g_structcnt; i++)
		{
			if (found >= 0 && i != found)
				continue;

			struct s_structdef *sd = g_structtbl[i];
			char buf[128];

			MMPrintString("TYPE ");
			MMPrintString((char *)sd->name);
			MMPrintString("\r\n");

			for (j = 0; j < sd->num_members; j++)
			{
				struct s_structmember *sm = &sd->members[j];
				char typestr[48];
				char dimstr[64] = "";

				// Build dimension string if array
				if (sm->dims[0] != 0)
				{
					strcpy(dimstr, "(");
					for (int d = 0; d < MAXDIM && sm->dims[d] != 0; d++)
					{
						if (d > 0)
							strcat(dimstr, ",");
						char numbuf[16];
						sprintf(numbuf, "%d", sm->dims[d]);
						strcat(dimstr, numbuf);
					}
					strcat(dimstr, ")");
				}

				if (sm->type & T_INT)
					strcpy(typestr, "INTEGER");
				else if (sm->type & T_NBR)
					strcpy(typestr, "FLOAT");
				else if (sm->type & T_STR)
				{
					sprintf(typestr, "STRING LENGTH %d", sm->size);
				}
				else if (sm->type & T_STRUCT)
				{
					// Show nested structure type name
					if (sm->size >= 0 && sm->size < g_structcnt)
						strcpy(typestr, (char *)g_structtbl[sm->size]->name);
					else
						strcpy(typestr, "(INVALID STRUCT)");
				}
				else
					strcpy(typestr, "?");

				MMPrintString("    ");
				MMPrintString((char *)sm->name);
				MMPrintString(dimstr);
				MMPrintString(" AS ");
				MMPrintString(typestr);
				sprintf(buf, "  ' offset=%d\r\n", sm->offset);
				MMPrintString(buf);
			}

			sprintf(buf, "END TYPE  ' size=%d bytes\r\n\r\n", sd->total_size);
			MMPrintString(buf);
		}
	}
#endif
	else if ((p = checkstring(cmdline, (unsigned char *)"PINS")))
	{
		CallExecuteProgram((char *)pinlist);
		return;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SYSTEM I2C")))
	{
		if (I2C0locked || I2C1locked)
			CallExecuteProgram((char *)i2clist);
		else
			error("System I2c not defined");
		return;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"COMMANDS")))
	{
		int ListCnt = 2;
		step = Option.DISPLAY_CONSOLE ? HRes / gui_font_width / 20 : 5;
		if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		m = 0;
		int x = 0;
		char **c = GetTempMainMemory((CommandTableSize + x) * sizeof(*c) + (CommandTableSize + x) * 18);
		for (i = 0; i < CommandTableSize + x; i++)
		{
			c[m] = (char *)((int)c + sizeof(char *) * (CommandTableSize + x) + m * 18);
			if (m < CommandTableSize)
				strcpy(c[m], (char *)commandtbl[i].name);
			if (*c[m] == '_' && c[m][1] != '(')
				*c[m] = '.';
			m++;
		}
		sortStrings(c, m);
		for (i = 1; i < m; i += step)
		{
			for (k = 0; k < step; k++)
			{
				if (i + k < m)
				{
					MMPrintString(c[i + k]);
					if (k != (step - 1))
						for (j = strlen(c[i + k]); j < 15; j++)
							MMputchar(' ', 1);
				}
			}
			if (Option.DISPLAY_CONSOLE)
				ListNewLine(&ListCnt, 0);
			else
				MMPrintString("\r\n");
		}
		MMPrintString("Total of ");
		PInt(m - 1);
		MMPrintString(" commands\r\n");
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"FUNCTIONS")))
	{
		m = 0;
		int ListCnt = 2;
		step = Option.DISPLAY_CONSOLE ? HRes / gui_font_width / 20 : 5;
		if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		int x = 3 + MMEND;
		char **c = GetTempMainMemory((TokenTableSize + x) * sizeof(*c) + (TokenTableSize + x) * 20);
		for (i = 0; i < TokenTableSize + x; i++)
		{
			c[m] = (char *)((int)c + sizeof(char *) * (TokenTableSize + x) + m * 20);
			if (m < TokenTableSize)
				strcpy(c[m], (char *)tokentbl[i].name);
			else if (m < TokenTableSize + MMEND && m >= TokenTableSize)
				strcpy(c[m], overlaid_functions[i - TokenTableSize]);
			else if (m == TokenTableSize + MMEND)
				strcpy(c[m], "=<");
			else if (m == TokenTableSize + MMEND + 1)
				strcpy(c[m], "=>");
			else
				strcpy(c[m], "MM.Info$(");
			m++;
		}
		sortStrings(c, m);
		for (i = 1; i < m - 1; i += step)
		{
			for (k = 0; k < step; k++)
			{
				if (i + k < m - 1)
				{
					MMPrintString(c[i + k]);
					if (k != (step - 1))
						for (j = strlen(c[i + k]); j < 15; j++)
							MMputchar(' ', 1);
				}
			}
			if (Option.DISPLAY_CONSOLE)
				ListNewLine(&ListCnt, 0);
			else
				MMPrintString("\r\n");
		}
		MMPrintString("Total of ");
		PInt(m - 1);
		MMPrintString(" functions and operators\r\n");
	}
	else
	{
		if (!(*cmdline == 0 || *cmdline == '\''))
		{
			getcsargs(&cmdline, 1);
			if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
			{
				ClearScreen(gui_bcolour);
				CurrentX = 0;
				CurrentY = 0;
			}
			char *buff = GetTempStrMemory();
			strcpy(buff, (char *)getCstring(argv[0]));
			if (strchr(buff, '.') == NULL)
			{
				if (!ExistsFile(buff))
					strcat(buff, ".bas");
			}
			ListFilePaged(buff);
		}
		else
		{
			ListProgramPaged(ProgMemory);
			checkend(cmdline);
		}
	}
}
#include <stdio.h>
#include <string.h>

int printWrappedText(const char *text, int screenWidth, int listcnt, int all)
{
	int length = strlen(text);
	int start = 0; // Start index of the current line
	char buff[STRINGSIZE];
	while (start < length)
	{
		int end = start + screenWidth; // Calculate the end index for the current line
		if (end >= length)
		{
			// If end is beyond the text length, just print the remaining text
			memset(buff, 0, STRINGSIZE);
			sprintf(buff, "%s", text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
			break;
		}

		// Find the last space within the current screen width
		int lastSpace = -1;
		for (int i = start; i < end; i++)
		{
			if (text[i] == ' ')
			{
				lastSpace = i;
			}
		}

		if (lastSpace != -1)
		{
			// If a space is found, break at the space
			memset(buff, 0, STRINGSIZE);
			sprintf(buff, "%.*s", lastSpace - start, text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
			start = lastSpace + 1; // Skip the space
		}
		else
		{
			// If no space is found, truncate at screen width
			memset(buff, 0, STRINGSIZE);
			sprintf(buff, "%.*s", screenWidth, text + start);
			MMPrintString(buff);
			ListNewLine(&listcnt, all);
			start += screenWidth;
		}
	}
	return listcnt;
}

void cmd_help(void)
{
	getcsargs(&cmdline, 1);
	if (!ExistsFile("A:/help.txt"))
		error("A:/help.txt not found");
	if (!argc)
	{
		MMPrintString("Enter help and the name of the command or function\r\nUse * for multicharacter wildcard or ? for single character wildcard\r\n");
	}
	else
	{
		int fnbr = FindFreeFileNbr();
		char *buff = GetTempStrMemory();
		BasicFileOpen("A:/help.txt", fnbr, FA_READ);
		int ListCnt = CurrentY / (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) + 2;
		char *p = (char *)getCstring(argv[0]);
		bool end = false;
		while (!FileEOF(fnbr))
		{ // while waiting for the end of file
			memset(buff, 0, STRINGSIZE);
			char *in = buff;
			while (1)
			{
				if (FileEOF(fnbr))
				{
					end = true;
					break;
				}
				char c = FileGetChar(fnbr);
				if (c == '\n')
					break;
				if (c == '\r')
					continue;
				*in++ = c;
			}
			if (end)
				break;
			skipspace(p);
			if (buff[0] == '~')
			{
				if (pattern_matching(p, &buff[1], 0, 0))
				{
					while (1)
					{ // loop through all lines for the command
						memset(buff, 0, STRINGSIZE);
						char *in = buff;
						while (1)
						{ // get this line
							if (FileEOF(fnbr))
							{
								end = true;
								break;
							}
							char c = FileGetChar(fnbr);
							if (c == '\n')
								break;
							if (c == '\r')
								continue;
							*in++ = c;
						}
						if (end)
							break;
						if (buff[0] == '~')
						{ // now we need to rewind the file to check this line
							ListNewLine(&ListCnt, false);
							lfs_file_seek(&lfs, FileTable[fnbr].lfsptr, -(strlen(buff) + 2), LFS_SEEK_CUR);
							break;
						}
						else
						{
							ListCnt = printWrappedText(buff, Option.Width - 1, ListCnt, false);
						}
					}
				}
			}
		}
		FileClose(fnbr);
	}
}
void MIPS16 cmd_run(void)
{
	do_run(cmdline, false);
}

void MIPS16 cmd_RunCMM2(void)
{
	do_run(cmdline, true);
}

void MIPS16 cmd_continue(void)
{
	if (*cmdline == tokenFOR)
	{
		if (g_forindex == 0)
			error("No FOR loop is in effect");
		nextstmt = g_forstack[g_forindex - 1].nextptr;
		return;
	}
	if (checkstring(cmdline, (unsigned char *)"DO"))
	{
		if (g_doindex == 0)
			error("No DO loop is in effect");
		nextstmt = g_dostack[g_doindex - 1].loopptr;
		return;
	}
	// must be a normal CONTINUE
	checkend(cmdline);
	if (CurrentLinePtr)
		StandardError(10);
	if (ContinuePoint == NULL)
		error("Cannot continue");
	//    IgnorePIN = false;
	nextstmt = ContinuePoint;
}

void MIPS16 cmd_new(void)
{
	closeframebuffer('A');
	checkend(cmdline);
	ClearProgram(true);
	FlashLoad = 0;
	uSec(250000);
	FlashWriteInit(PROGRAM_FLASH);
	safe_flash_range_erase(realflashpointer, MAX_PROG_SIZE);
	FlashWriteByte(0);
	FlashWriteByte(0);
	FlashWriteByte(0); // terminate the program in flash
	FlashWriteClose();
#ifdef PICOMITEVGA
	int mode = DISPLAY_TYPE - SCREENMODE1 + 1;
	setmode(mode, true);
#endif
	memset(inpbuf, 0, STRINGSIZE);
	longjmp(mark, 1); // jump back to the input prompt
}

void MIPS16 cmd_clear(void)
{
	checkend(cmdline);
	if (g_LocalIndex)
		error("Invalid in a subroutine");
	ClearVars(0, true);
}

void cmd_goto(void)
{
#ifdef CACHE
	/* Guard: only cache when cmdline is a stable ProgMemory pointer.
	 * When called from cmd_if's testgoto path the fix above ensures this,
	 * but the execute_one_command path (IF..THEN cmd ELSE) still passes an
	 * argbuf pointer — skip caching for any non-ProgMemory cmdline.       */
	int _goto_cacheable = (g_trace_cache_flags & TCF_JUMP) &&
		cmdline >= ProgMemory && cmdline < ProgMemory + MAX_PROG_SIZE;
	if (_goto_cacheable)
	{
		unsigned char *cached_tgt;
		if (TraceCacheTryJump(cmdline, &cached_tgt))
		{
			nextstmt = CurrentLinePtr = cached_tgt;
			return;
		}
	}
#endif
	if (isnamestart(*cmdline))
		nextstmt = findlabel(cmdline); // must be a label
	else
		nextstmt = findline(getinteger(cmdline), true); // try for a line number
#ifdef CACHE
	if (_goto_cacheable)
		TraceCacheStoreJump(cmdline, nextstmt);
#endif
	CurrentLinePtr = nextstmt;
}

#if LOWRAM
void cmd_if(void)
{
#else
#ifdef rp2350
void __not_in_flash_func(cmd_if)(void)
#else
void MIPS16 __not_in_flash_func(cmd_if)(void)
#endif
{
#endif
	int r, i, testgoto, testelseif;
	unsigned char ss[3]; // this will be used to split up the argument line
	unsigned char *p, *tp;
	unsigned char *rp = NULL;
#ifdef CACHE
	/* Save the cmdline pointer BEFORE getargs (makeargs) can advance it.
	 * This stable ProgMemory pointer is the cache key for the IF condition. */
	unsigned char *if_cond_key = cmdline;
#endif

	ss[0] = tokenTHEN;
	ss[1] = tokenELSE;
	ss[2] = 0;

	testgoto = false;
	testelseif = false;

retest_an_if:
{							   // start a new block
	getargs(&cmdline, 20, ss); // getargs macro must be the first executable stmt in a block

	if (testelseif && argc > 2)
		error("Unexpected text");

	// if there is no THEN token retry the test with a GOTO.  If that fails flag an error
	if (argc < 2 || *argv[1] != ss[0])
	{
		if (testgoto)
			error("IF without THEN");
		ss[0] = tokenGOTO;
		testgoto = true;
		goto retest_an_if;
	}

	// allow for IF statements embedded inside this IF
	if (argc >= 3 && commandtbl_decode(argv[2]) == cmdIF)
		argc = 3; // this is IF xx=yy THEN IF ... so we want to evaluate only the first 3
	if (argc >= 5 && commandtbl_decode(argv[4]) == cmdIF)
		argc = 5; // this is IF xx=yy THEN cmd ELSE IF ... so we want to evaluate only the first 5

	if (argc == 4 || (argc == 5 && *argv[3] != ss[1]))
		SyntaxError();
	;

#ifdef CACHE
	{
		int _cached_r;
		if ((g_trace_cache_flags & TCF_IF) && TraceCacheTryIf(if_cond_key, &_cached_r))
		{
			r = _cached_r;
			goto if_condition_done;
		}
	}
#endif
	r = (getnumber(argv[0]) != 0); // evaluate the expression controlling the if statement
#ifdef CACHE
if_condition_done:;
#endif

	if (r)
	{
		// the test returned TRUE
		// first check if it is a multiline IF (ie, only 2 args)
		if (argc == 2)
		{
			// if multiline do nothing, control will fall through to the next line (which is what we want to execute next)
			;
		}
		else
		{
			// This is a standard single line IF statement
			// Because the test was TRUE we are just interested in the THEN cmd stage.
			if (*argv[1] == tokenGOTO)
			{
#ifdef CACHE
				/* argv[2] points into the local argbuf stack buffer, not ProgMemory.
				 * Every IF...GoTo statement with the same-length condition shares the
				 * same argbuf address, so the jump cache would return the first label
				 * cached for all of them.  Scan from if_cond_key (a stable ProgMemory
				 * pointer saved before getargs) to find tokenGOTO, then skip past it
				 * and any spaces (mirroring what makeargs does) to give cmd_goto a
				 * unique, ProgMemory-anchored cache key and correct argument.        */
				{
					unsigned char *goto_arg = if_cond_key;
					while (*goto_arg && *goto_arg != tokenGOTO)
						goto_arg++;
					if (*goto_arg == tokenGOTO)
					{
						goto_arg++;                          /* skip tokenGOTO byte  */
						while (*goto_arg == ' ') goto_arg++; /* skip spaces          */
						cmdline = goto_arg;
					}
					else
						cmdline = argv[2]; /* fallback: should never happen */
				}
#else
				cmdline = argv[2];
#endif
				cmd_goto();
				return;
			}
			else if (isdigit(*argv[2]))
			{
				nextstmt = findline(getinteger(argv[2]), true);
			}
			else
			{
				if (argc == 5)
				{
					// this is a full IF THEN ELSE and the statement we want to execute is between the THEN & ELSE
					// this is handled by a special routine
					execute_one_command(argv[2]);
				}
				else
				{
					// easy - there is no ELSE clause so just point the next statement pointer to the byte after the THEN token
					for (p = cmdline; *p && *p != ss[0]; p++)
						;			  // search for the token
					nextstmt = p + 1; // and point to the byte after
				}
			}
		}
	}
	else
	{
		// the test returned FALSE so we are just interested in the ELSE stage (if present)
		// first check if it is a multiline IF (ie, only 2 args)
		if (argc == 2)
		{
			// search for the next ELSE, or ENDIF and pass control to the following line
			// if an ELSEIF is found re execute this function to evaluate the condition following the ELSEIF
			i = 1;
			p = nextstmt;
			while (1)
			{
				p = GetNextCommand(p, &rp, (unsigned char *)"No matching ENDIF");
				CommandToken tkn = commandtbl_decode(p);
				if (tkn == cmdtoken)
				{
					// found a nested IF command, we now need to determine if it is a single or multiline IF
					// search for a THEN, then check if only white space follows.  If so, it is multiline.
					tp = p + sizeof(CommandToken);
					while (*tp && *tp != ss[0])
						tp++;
					if (*tp)
						tp++; // step over the THEN
					skipspace(tp);
					if (*tp == 0 || *tp == '\'') // yes, only whitespace follows
						i++;					 // count it as a nested IF
					else						 // no, it is a single line IF
						skipelement(p);			 // skip to the end so that we avoid an ELSE
					continue;
				}

				if (tkn == cmdELSE && i == 1)
				{
					// found an ELSE at the same level as this IF.  Step over it and continue with the statement after it
					skipelement(p);
					nextstmt = p;
					break;
				}

				if ((tkn == cmdELSEIF || tkn == cmdELSE_IF) && i == 1)
				{
					// we have found an ELSEIF statement at the same level as our IF statement
					// setup the environment to make this function evaluate the test following ELSEIF and jump back
					// to the start of the function.  This is not very clean (it uses the dreaded goto for a start) but it works
					p += sizeof(CommandToken); // step over the token
					skipspace(p);
					CurrentLinePtr = rp;
					if (*p == 0)
						SyntaxError();
					; // there must be a test after the elseif
					cmdline = p;
					skipelement(p);
					nextstmt = p;
					testgoto = false;
					testelseif = true;
#ifdef CACHE
					if_cond_key = cmdline; /* each ELSEIF has its own cache entry */
#endif
					goto retest_an_if;
				}

				if (tkn == cmdENDIF || tkn == cmdEND_IF)
					i--; // found an ENDIF so decrement our nested counter
				if (i == 0)
				{
					// found our matching ENDIF stmt.  Step over it and continue with the statement after it
					skipelement(p);
					nextstmt = p;
					break;
				}
			}
		}
		else
		{
			// this must be a single line IF statement
			// check if there is an ELSE on the same line
			if (argc == 5)
			{
				// there is an ELSE command
				if (isdigit(*argv[4]))
					// and it is just a number, so get it and find the line
					nextstmt = findline(getinteger(argv[4]), true);
				else
				{
					/*						// there is a statement after the ELSE clause  so just point to it (the byte after the ELSE token)
											for(p = cmdline; *p && *p != ss[1]; p++);	// search for the token
											nextstmt = p + 1;							// and point to the byte after
					*/
					// IF <condition> THEN <statement1> ELSE <statement2>
					// Find and read the THEN function token.
					for (p = cmdline; *p && *p != ss[0]; p++)
					{
					}
					// Skip the command that <statement1> must start with.
					p++;
					skipspace(p);
					p += sizeof(CommandToken);
					// Find and read the ELSE function token.
					for (; *p && *p != ss[1]; p++)
						;
					nextstmt = p + 1; // The statement after the ELSE token.
				}
			}
			else
			{
				// no ELSE on a single line IF statement, so just continue with the next statement
				skipline(cmdline);
				nextstmt = cmdline;
			}
		}
	}
}
}

#if LOWRAM
void cmd_else(void)
{
#else
void __not_in_flash_func(cmd_else)(void)
{
#endif
	int i;
	unsigned char *p, *tp;

	// search for the next ENDIF and pass control to the following line
	i = 1;
	p = nextstmt;

	if (cmdtoken == cmdELSE)
		checkend(cmdline);

	while (1)
	{
		p = GetNextCommand(p, NULL, (unsigned char *)"No matching ENDIF");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdIF)
		{
			// found a nested IF command, we now need to determine if it is a single or multiline IF
			// search for a THEN, then check if only white space follows.  If so, it is multiline.
			tp = p + sizeof(CommandToken);
			while (*tp && *tp != tokenTHEN)
				tp++;
			if (*tp)
				tp++; // step over the THEN
			skipspace(tp);
			if (*tp == 0 || *tp == '\'') // yes, only whitespace follows
				i++;					 // count it as a nested IF
		}
		if (tkn == cmdENDIF || tkn == cmdEND_IF)
			i--; // found an ENDIF so decrement our nested counter
		if (i == 0)
			break; // found our matching ENDIF stmt
	}
	// found a matching ENDIF.  Step over it and continue with the statement after it
	skipelement(p);
	nextstmt = p;
}
void do_end(bool ecmd)
{
#ifdef rp2350
	// On runtime error termination, abort steppers to a safe state.
	// Keeps the 100kHz IRQ running and preserves axis configuration.
	// Leaves the system in TEST mode, drivers disabled, spindle off, buffer cleared,
	// and position unknown (requires G28 or STEPPER POSITION + STEPPER RUN).
	void stepper_abort_to_safe_state_on_error(void);
	if (MMerrno != 0)
		stepper_abort_to_safe_state_on_error();
#endif

	dma_hw->abort = ((1u << dma_rx_chan2) | (1u << dma_rx_chan));
	if (dma_channel_is_busy(dma_rx_chan))
		dma_channel_abort(dma_rx_chan);
	if (dma_channel_is_busy(dma_rx_chan2))
		dma_channel_abort(dma_rx_chan2);
	dma_hw->abort = ((1u << dma_tx_chan2) | (1u << dma_tx_chan));
	if (dma_channel_is_busy(dma_tx_chan))
		dma_channel_abort(dma_tx_chan);
	if (dma_channel_is_busy(dma_tx_chan2))
		dma_channel_abort(dma_tx_chan2);
	dma_hw->abort = ((1u << ADC_dma_chan2) | (1u << ADC_dma_chan));
	if (dma_channel_is_busy(ADC_dma_chan))
		dma_channel_abort(ADC_dma_chan);
	if (dma_channel_is_busy(ADC_dma_chan2))
		dma_channel_abort(ADC_dma_chan2);
#ifdef PICOMITE
	if (mergerunning)
	{
		multicore_fifo_push_blocking(0xFF);
		mergerunning = false;
		busy_wait_ms(100);
	}
#endif
	if (Option.SerialConsole)
		while (ConsoleTxBufHead != ConsoleTxBufTail)
			routinechecks();
#ifndef USBKEYBOARD
	// fflush(stdout);
	tud_cdc_write_flush();
#endif
	if (ecmd)
	{
		getcsargs(&cmdline, 1);
		if (argc == 1)
		{
			if (FindSubFun((unsigned char *)"MM.END", 0) >= 0 && checkstring(argv[0], (unsigned char *)"NOEND") == NULL)
			{
				ExecuteProgram((unsigned char *)"MM.END\0");
				if (Option.SerialConsole)
					while (ConsoleTxBufHead != ConsoleTxBufTail)
						routinechecks();
#ifndef USBKEYBOARD
				// fflush(stdout);
				tud_cdc_write_flush();
#endif
				memset(inpbuf, 0, STRINGSIZE);
			}
			else
			{
				unsigned char *cmd_args = (unsigned char *)"";
				cmd_args = getCstring(argv[0]);
				void *ptr = findvar((unsigned char *)"MM.ENDLINE$", T_STR | V_NOFIND_NULL);
				if (ptr == NULL)
					ptr = findvar((unsigned char *)"MM.ENDLINE$", V_FIND | V_DIM_VAR);
				strcpy(ptr, (char *)cmd_args); // *** THW 16/4/23
				CtoM(ptr);
			}
		}
		else if (FindSubFun((unsigned char *)"MM.END", 0) >= 0)
		{
			ExecuteProgram((unsigned char *)"MM.END\0");
			if (Option.SerialConsole)
				while (ConsoleTxBufHead != ConsoleTxBufTail)
					routinechecks();
#ifndef USBKEYBOARD
			// fflush(stdout);
			tud_cdc_write_flush();
#endif
			memset(inpbuf, 0, STRINGSIZE);
		}
	}
	if (!(MMerrno == 16))
		hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
	irq_set_enabled(DMA_IRQ_1, false);
	for (int i = 0; i < NBRSETTICKS; i++)
	{
		TickPeriod[i] = 0;
		TickTimer[i] = 0;
		TickInt[i] = NULL;
		TickActive[i] = 0;
	}
	InterruptUsed = 0;
	InterruptReturn = NULL;
	memset(inpbuf, 0, STRINGSIZE);
	CloseAudio(1);
	CloseAllFiles();
	ADCDualBuffering = 0;
	WatchdogSet = false;
	WDTimer = 0;
	hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
	_excep_code = 0;
	dmarunning = false;
	multi = false;
	WAVInterrupt = NULL;
	WAVcomplete = 0;
	if (g_myrand)
		FreeMemory((void *)g_myrand);
	g_myrand = NULL;
	OptionConsole = 3;
#ifdef PICOMITEVGA
	int mode = DISPLAY_TYPE - SCREENMODE1 + 1;
	setmode(mode, false);
#endif
	SSPrintString("\033[?25h"); // in case application has turned the cursor off
	SSPrintString("\033[97;40m");
#ifdef PICOMITEWEB
	close_tcpclient();
#endif
#ifndef USBKEYBOARD
	if (mouse0 == false && Option.MOUSE_CLOCK)
		initMouse0(0); // see if there is a mouse to initialise
#endif
#if PICOMITERP2350
	if (Option.DISPLAY_TYPE >= NEXTGEN)
		Option.Refresh = 1;
#endif
}

static void perf_print(const char *s, int *cnt)
{
	MMPrintString((char *)s);
	for (const char *p = s; *p; p++)
		if (*p == '\n')
			(*cnt)++;
	if (*cnt >= Option.Height - overlap)
	{
		clearrepeat();
		MMPrintString("PRESS ANY KEY ...");
		MMgetchar();
		MMPrintString("\r                 \r");
		if (Option.DISPLAY_CONSOLE)
		{
			ClearScreen(gui_bcolour);
			CurrentX = 0;
			CurrentY = 0;
		}
		*cnt = 2;
	}
}

void cmd_end(void)
{
	// ---- Performance counter report (gated by OPTION PROFILING ON) -----
	if (g_option_profiling)
	{
		int list_cnt = 2;
		extern uint32_t *g_perf_cmdcount;
		extern uint32_t g_perf_usercmd_count;
		extern uint32_t g_perf_findvar_calls;
		extern uint32_t g_perf_findvar_locals;
		extern uint32_t g_perf_findvar_globals;
		extern uint64_t g_perf_start_us;
#define PERF_CMDTOKEN_MAX 1024
		uint64_t elapsed_us = time_us_64() - g_perf_start_us;
		uint64_t total_cmds = g_perf_usercmd_count;
		for (int k = 0; k < PERF_CMDTOKEN_MAX; k++)
			total_cmds += g_perf_cmdcount[k];
		char buf[200];
		unsigned resolved = g_perf_findvar_locals + g_perf_findvar_globals;
		unsigned local_pct = resolved ? (unsigned)((uint64_t)g_perf_findvar_locals * 100 / resolved) : 0;
		unsigned global_pct = resolved ? (unsigned)((uint64_t)g_perf_findvar_globals * 100 / resolved) : 0;
		snprintf(buf, sizeof(buf),
				 "\r\n[PERF] elapsed=%llu us  statements=%llu  findvar=%u (locals=%u [%u%%] globals=%u [%u%%])  user_subs=%u\r\n",
				 (unsigned long long)elapsed_us,
				 (unsigned long long)total_cmds,
				 (unsigned)g_perf_findvar_calls,
				 (unsigned)g_perf_findvar_locals, local_pct,
				 (unsigned)g_perf_findvar_globals, global_pct,
				 (unsigned)g_perf_usercmd_count);
		perf_print(buf, &list_cnt);
#ifdef CACHE
		snprintf(buf, sizeof(buf),
				 "[PERF] tracecache: flags=0x%02x size=%d replays=%u compiles_ok=%u compiles_bad=%u\r\n",
				 (unsigned)g_trace_cache_flags,
				 TraceCacheGetSize(),
				 (unsigned)g_trace_replays,
				 (unsigned)g_trace_compiles_ok,
				 (unsigned)g_trace_compiles_bad);
		if (g_trace_cache_flags)
			perf_print(buf, &list_cnt);
		snprintf(buf, sizeof(buf),
				 "[PERF] tracecache: lookup_null=%u alloc_fail=%u optin_skip=%u jump_hits=%u\r\n",
				 (unsigned)g_trace_lookup_null,
				 (unsigned)g_trace_alloc_fail,
				 (unsigned)g_trace_optin_skip,
				 (unsigned)g_trace_jump_hits);
		if (g_trace_cache_flags)
			perf_print(buf, &list_cnt);
		// Per-sub cache hit table (top-20 by total LET+IF replays).
		if (g_trace_cache_flags)
		{
			extern uint32_t *g_tc_sub_let_hits;
			extern uint32_t *g_tc_sub_if_hits;
			extern unsigned char *subfun[];
			// Check whether any sub has hits, or top-level has hits.
			int any = 0;
			if (g_tc_sub_let_hits && g_tc_sub_if_hits)
				for (int k = 0; k <= MAXSUBFUN; k++)
					if (g_tc_sub_let_hits[k] || g_tc_sub_if_hits[k]) { any = 1; break; }
			if (any)
			{
				uint32_t *scratch = (uint32_t *)GetMemory((MAXSUBFUN + 1) * sizeof(uint32_t));
				for (int k = 0; k <= MAXSUBFUN; k++)
					scratch[k] = g_tc_sub_let_hits[k] + g_tc_sub_if_hits[k];
				perf_print("[PERF] tracecache hits by SUB (top 20):\r\n", &list_cnt);
				perf_print("        let_hits   if_hits     total  name\r\n", &list_cnt);
				for (int rank = 0; rank < 20; rank++)
				{
					uint32_t best = 0;
					int best_idx = -1;
					for (int k = 0; k <= MAXSUBFUN; k++)
						if (scratch[k] > best) { best = scratch[k]; best_idx = k; }
					if (best_idx < 0 || best == 0)
						break;
					char nm[MAXVARLEN + 1];
					nm[0] = 0;
					if (best_idx < MAXSUBFUN && subfun[best_idx] != NULL)
					{
						unsigned char *sp = subfun[best_idx] + sizeof(CommandToken);
						skipspace(sp);
						int j = 0;
						while (j < MAXVARLEN && isnamechar(*sp))
							nm[j++] = *sp++;
						nm[j] = 0;
					}
					snprintf(buf, sizeof(buf), "  %10u  %8u  %8u  %s\r\n",
							 (unsigned)g_tc_sub_let_hits[best_idx],
							 (unsigned)g_tc_sub_if_hits[best_idx],
							 (unsigned)best,
							 nm[0] ? nm : "(top-level)");
					perf_print(buf, &list_cnt);
					scratch[best_idx] = 0;
				}
				FreeMemorySafe((void **)&scratch);
			}
		}
#endif
		// Find and print the top-20 most-executed builtin commands.
		perf_print("[PERF] top commands by dispatch count:\r\n", &list_cnt);
		for (int rank = 0; rank < 20; rank++)
		{
			uint32_t best = 0;
			int best_idx = -1;
			for (int k = 0; k < PERF_CMDTOKEN_MAX; k++)
			{
				if (g_perf_cmdcount[k] > best)
				{
					best = g_perf_cmdcount[k];
					best_idx = k;
				}
			}
			if (best_idx < 0 || best == 0)
				break;
			snprintf(buf, sizeof(buf), "  %10u  %s\r\n",
					 (unsigned)best, (const char *)commandname(best_idx));
			perf_print(buf, &list_cnt);
			g_perf_cmdcount[best_idx] = 0; // consume so next iteration finds the next
		}

		// Top-20 user SUB/FUNCTIONs by inclusive wall-clock time.
		{
			extern uint32_t *g_perf_subcall_count;
			extern unsigned char *subfun[];
			int any = 0;
			for (int k = 0; k < MAXSUBFUN; k++)
				if (g_perf_subcall_count[k])
				{
					any = 1;
					break;
				}
			if (any)
			{
				extern uint64_t *g_perf_subexcl_us;
				/* Snapshot exclusive (self) time into a scratch array so we
				 * can zero-out the "best" each iteration without losing the
				 * inclusive time / call counts used for the per-row report. */
				uint64_t *scratch_us = (uint64_t *)GetMemory(MAXSUBFUN * sizeof(uint64_t));
				memcpy(scratch_us, g_perf_subexcl_us, MAXSUBFUN * sizeof(uint64_t));

				perf_print("[PERF] top SUBs by exclusive (self) time:\r\n", &list_cnt);
				perf_print("       self_us    incl_us     calls   self_us/call  name\r\n", &list_cnt);
				for (int rank = 0; rank < 20; rank++)
				{
					uint64_t best = 0;
					int best_idx = -1;
					for (int k = 0; k < MAXSUBFUN; k++)
					{
						if (scratch_us[k] > best)
						{
							best = scratch_us[k];
							best_idx = k;
						}
					}
					if (best_idx < 0 || best == 0)
						break;
					char nm[MAXVARLEN + 1];
					nm[0] = 0;
					if (subfun[best_idx] != NULL)
					{
						unsigned char *sp = subfun[best_idx] + sizeof(CommandToken);
						skipspace(sp);
						int j = 0;
						while (j < MAXVARLEN && isnamechar(*sp))
							nm[j++] = *sp++;
						nm[j] = 0;
					}
					uint32_t calls = g_perf_subcall_count[best_idx];
					unsigned long long incl = (unsigned long long)g_perf_subtime_us[best_idx];
					unsigned long long per_call = calls ? (best / calls) : 0;
					snprintf(buf, sizeof(buf),
							 "  %12llu  %10llu  %8u  %12llu  %s\r\n",
							 (unsigned long long)best, incl, (unsigned)calls,
							 per_call, nm[0] ? nm : "(unknown)");
					perf_print(buf, &list_cnt);
					scratch_us[best_idx] = 0;
				}

				perf_print("[PERF] top SUBs by call count:\r\n", &list_cnt);
				for (int rank = 0; rank < 20; rank++)
				{
					uint32_t best = 0;
					int best_idx = -1;
					for (int k = 0; k < MAXSUBFUN; k++)
					{
						if (g_perf_subcall_count[k] > best)
						{
							best = g_perf_subcall_count[k];
							best_idx = k;
						}
					}
					if (best_idx < 0 || best == 0)
						break;
					char nm[MAXVARLEN + 1];
					nm[0] = 0;
					if (subfun[best_idx] != NULL)
					{
						unsigned char *sp = subfun[best_idx] + sizeof(CommandToken);
						skipspace(sp);
						int j = 0;
						while (j < MAXVARLEN && isnamechar(*sp))
							nm[j++] = *sp++;
						nm[j] = 0;
					}
					snprintf(buf, sizeof(buf), "  %10u  %s\r\n",
							 (unsigned)best, nm[0] ? nm : "(unknown)");
					perf_print(buf, &list_cnt);
					g_perf_subcall_count[best_idx] = 0;
				}
				FreeMemorySafe((void **)&scratch_us);
			}
		}
	}
#ifdef MMBASIC_FM
	int relaunch_fm = fm_program_launched_from_fm;
	fm_program_launched_from_fm = 0;
	do_end(true);
	if (relaunch_fm)
	{
		fm_program_launched_from_fm = 1;
		CurrentLinePtr = NULL;
		cmdline = (unsigned char *)"";
	}
#else
	do_end(true);
#endif
	longjmp(mark, 1); // jump back to the input prompt
}
extern unsigned int mmap[HEAP_MEMORY_SIZE / PAGESIZE / PAGESPERWORD];
extern unsigned int psmap[7 * 1024 * 1024 / PAGESIZE / PAGESPERWORD];
extern struct s_hash g_hashlist[MAXLOCALVARS];
extern int g_hashlistpointer;
extern int g_StrTmpIndex;
extern bool g_TempMemoryIsChanged;
extern char *g_StrTmp[MAXTEMPSTRINGS];			// used to track temporary string space on the heap
extern char g_StrTmpLocalIndex[MAXTEMPSTRINGS]; // used to track the g_LocalIndex for each temporary string space on the heap
void SaveContext(void)
{
	CloseAudio(1);
#if defined(rp2350)
	if (PSRAMsize)
	{
		ClearTempMemory();
		uint8_t *p = (uint8_t *)PSRAMbase + PSRAMsize;
		memcpy(p, &g_StrTmpIndex, sizeof(g_StrTmpIndex));
		p += sizeof(g_StrTmpIndex);
		memcpy(p, &g_TempMemoryIsChanged, sizeof(g_TempMemoryIsChanged));
		p += sizeof(g_TempMemoryIsChanged);
		memcpy(p, (void *)g_StrTmp, sizeof(g_StrTmp));
		p += sizeof(g_StrTmp);
		memcpy(p, (void *)g_StrTmpLocalIndex, sizeof(g_StrTmpLocalIndex));
		p += sizeof(g_StrTmpLocalIndex);
		memcpy(p, &g_LocalIndex, sizeof(g_LocalIndex));
		p += sizeof(g_LocalIndex);
		memcpy(p, &g_OptionBase, sizeof(g_OptionBase));
		p += sizeof(g_OptionBase);
		memcpy(p, &g_DimUsed, sizeof(g_DimUsed));
		p += sizeof(g_DimUsed);
		memcpy(p, &g_varcnt, sizeof(g_varcnt));
		p += sizeof(g_varcnt);
		memcpy(p, &g_Globalvarcnt, sizeof(g_Globalvarcnt));
		p += sizeof(g_Globalvarcnt);
		memcpy(p, &g_Localvarcnt, sizeof(g_Localvarcnt));
		p += sizeof(g_Localvarcnt);
		memcpy(p, &g_hashlistpointer, sizeof(g_hashlistpointer));
		p += sizeof(g_hashlistpointer);
		memcpy(p, &g_forindex, sizeof(g_forindex));
		p += sizeof(g_forindex);
		memcpy(p, &g_doindex, sizeof(g_doindex));
		p += sizeof(g_doindex);
		memcpy(p, g_forstack, sizeof(struct s_forstack) * MAXFORLOOPS);
		p += sizeof(struct s_forstack) * MAXFORLOOPS;
		memcpy(p, g_dostack, sizeof(struct s_dostack) * MAXDOLOOPS);
		p += sizeof(struct s_dostack) * MAXDOLOOPS;
		memcpy(p, g_vartbl, sizeof(struct s_vartbl) * MAXVARS);
		p += sizeof(struct s_vartbl) * MAXVARS;
		memcpy(p, g_hashlist, sizeof(struct s_hash) * MAXLOCALVARS);
		p += sizeof(struct s_hash) * MAXLOCALVARS;
		memcpy(p, MMHeap, heap_memory_size + 256);
		p += heap_memory_size + 256;
		memcpy(p, mmap, sizeof(mmap));
		p += sizeof(mmap);
		memcpy(p, psmap, sizeof(psmap));
		p += sizeof(psmap);
	}
	else
	{
#endif
		lfs_file_t lfs_file;
		struct lfs_info lfsinfo = {0};
		FSerror = lfs_stat(&lfs, "/.vars", &lfsinfo);
		if (lfsinfo.type == LFS_TYPE_REG)
			lfs_remove(&lfs, "/.vars");
		int sizeneeded = sizeof(g_StrTmpIndex) + sizeof(g_TempMemoryIsChanged) + sizeof(g_StrTmp) + sizeof(g_StrTmpLocalIndex) +
						 sizeof(g_LocalIndex) + sizeof(g_OptionBase) + sizeof(g_DimUsed) + sizeof(g_varcnt) + sizeof(g_Globalvarcnt) + sizeof(g_Localvarcnt) +
						 sizeof(g_hashlistpointer) + sizeof(g_forindex) + sizeof(g_doindex) + sizeof(struct s_forstack) * MAXFORLOOPS + sizeof(struct s_dostack) * MAXDOLOOPS +
						 sizeof(struct s_vartbl) * MAXVARS + sizeof(struct s_hash) * MAXLOCALVARS + heap_memory_size + 256 + sizeof(mmap);
		if (sizeneeded >= Option.FlashSize - (Option.modbuff ? 1024 * Option.modbuffsize : 0) - RoundUpK4(TOP_OF_SYSTEM_FLASH) - lfs_fs_size(&lfs) * 4096)
			error("Not enough free space on A: drive: % needed", sizeneeded);
		lfs_file_open(&lfs, &lfs_file, ".vars", LFS_O_RDWR | LFS_O_CREAT);
		;
		//		int dt=get_fattime();
		ClearTempMemory();
		//		lfs_setattr(&lfs, ".vars", 'A', &dt,   4);
		lfs_file_write(&lfs, &lfs_file, &g_StrTmpIndex, sizeof(g_StrTmpIndex));
		lfs_file_write(&lfs, &lfs_file, &g_TempMemoryIsChanged, sizeof(g_TempMemoryIsChanged));
		lfs_file_write(&lfs, &lfs_file, (void *)g_StrTmp, sizeof(g_StrTmp));
		lfs_file_write(&lfs, &lfs_file, (void *)g_StrTmpLocalIndex, sizeof(g_StrTmpLocalIndex));
		lfs_file_write(&lfs, &lfs_file, &g_LocalIndex, sizeof(g_LocalIndex));
		lfs_file_write(&lfs, &lfs_file, &g_OptionBase, sizeof(g_OptionBase));
		lfs_file_write(&lfs, &lfs_file, &g_DimUsed, sizeof(g_DimUsed));
		lfs_file_write(&lfs, &lfs_file, &g_varcnt, sizeof(g_varcnt));
		lfs_file_write(&lfs, &lfs_file, &g_Globalvarcnt, sizeof(g_Globalvarcnt));
		lfs_file_write(&lfs, &lfs_file, &g_Localvarcnt, sizeof(g_Localvarcnt));
		lfs_file_write(&lfs, &lfs_file, &g_hashlistpointer, sizeof(g_hashlistpointer));
		lfs_file_write(&lfs, &lfs_file, &g_forindex, sizeof(g_forindex));
		lfs_file_write(&lfs, &lfs_file, &g_doindex, sizeof(g_doindex));
		lfs_file_write(&lfs, &lfs_file, g_forstack, sizeof(struct s_forstack) * MAXFORLOOPS);
		lfs_file_write(&lfs, &lfs_file, g_dostack, sizeof(struct s_dostack) * MAXDOLOOPS);
		lfs_file_write(&lfs, &lfs_file, g_vartbl, sizeof(struct s_vartbl) * MAXVARS);
		lfs_file_write(&lfs, &lfs_file, g_hashlist, sizeof(struct s_hash) * MAXLOCALVARS);
		lfs_file_write(&lfs, &lfs_file, MMHeap, heap_memory_size + 256);
		lfs_file_write(&lfs, &lfs_file, mmap, sizeof(mmap));
		lfs_file_close(&lfs, &lfs_file);
#if defined(rp2350)
	}
#endif
}
void RestoreContext(bool keep)
{
	CloseAudio(1);
#if defined(rp2350)
	if (PSRAMsize)
	{
		uint8_t *p = (uint8_t *)PSRAMbase + PSRAMsize;
		memcpy(&g_StrTmpIndex, p, sizeof(g_StrTmpIndex));
		p += sizeof(g_StrTmpIndex);
		memcpy(&g_TempMemoryIsChanged, p, sizeof(g_TempMemoryIsChanged));
		p += sizeof(g_TempMemoryIsChanged);
		memcpy((void *)g_StrTmp, p, sizeof(g_StrTmp));
		p += sizeof(g_StrTmp);
		memcpy((void *)g_StrTmpLocalIndex, p, sizeof(g_StrTmpLocalIndex));
		p += sizeof(g_StrTmpLocalIndex);
		memcpy(&g_LocalIndex, p, sizeof(g_LocalIndex));
		p += sizeof(g_LocalIndex);
		memcpy(&g_OptionBase, p, sizeof(g_OptionBase));
		p += sizeof(g_OptionBase);
		memcpy(&g_DimUsed, p, sizeof(g_DimUsed));
		p += sizeof(g_DimUsed);
		memcpy(&g_varcnt, p, sizeof(g_varcnt));
		p += sizeof(g_varcnt);
		memcpy(&g_Globalvarcnt, p, sizeof(g_Globalvarcnt));
		p += sizeof(g_Globalvarcnt);
		memcpy(&g_Localvarcnt, p, sizeof(g_Localvarcnt));
		p += sizeof(g_Localvarcnt);
		memcpy(&g_hashlistpointer, p, sizeof(g_hashlistpointer));
		p += sizeof(g_hashlistpointer);
		memcpy(&g_forindex, p, sizeof(g_forindex));
		p += sizeof(g_forindex);
		memcpy(&g_doindex, p, sizeof(g_doindex));
		p += sizeof(g_doindex);
		memcpy(g_forstack, p, sizeof(struct s_forstack) * MAXFORLOOPS);
		p += sizeof(struct s_forstack) * MAXFORLOOPS;
		memcpy(g_dostack, p, sizeof(struct s_dostack) * MAXDOLOOPS);
		p += sizeof(struct s_dostack) * MAXDOLOOPS;
		memcpy(g_vartbl, p, sizeof(struct s_vartbl) * MAXVARS);
		p += sizeof(struct s_vartbl) * MAXVARS;
		memcpy(g_hashlist, p, sizeof(struct s_hash) * MAXLOCALVARS);
		p += sizeof(struct s_hash) * MAXLOCALVARS;
		memcpy(MMHeap, p, heap_memory_size + 256);
		p += heap_memory_size + 256;
		memcpy(mmap, p, sizeof(mmap));
		p += sizeof(mmap);
		memcpy(psmap, p, sizeof(psmap));
		p += sizeof(psmap);
	}
	else
	{
#endif
		lfs_file_t lfs_file;
		struct lfs_info lfsinfo = {0};
		FSerror = lfs_stat(&lfs, "/.vars", &lfsinfo);
		if (lfsinfo.type != LFS_TYPE_REG)
			error("Internal error");
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
		lfs_file_read(&lfs, &lfs_file, g_forstack, sizeof(struct s_forstack) * MAXFORLOOPS);
		lfs_file_read(&lfs, &lfs_file, g_dostack, sizeof(struct s_dostack) * MAXDOLOOPS);
		lfs_file_read(&lfs, &lfs_file, g_vartbl, sizeof(struct s_vartbl) * MAXVARS);
		lfs_file_read(&lfs, &lfs_file, g_hashlist, sizeof(struct s_hash) * MAXLOCALVARS);
		lfs_file_read(&lfs, &lfs_file, MMHeap, heap_memory_size + 256);
		lfs_file_read(&lfs, &lfs_file, mmap, sizeof(mmap));
		lfs_file_close(&lfs, &lfs_file);
		if (!keep)
			lfs_remove(&lfs, "/.vars");
#if defined(rp2350)
	}
#endif
}
extern void chdir(char *p);
void MIPS16 do_chain(unsigned char *cmdline)
{
	unsigned char *filename = (unsigned char *)"", *cmd_args = (unsigned char *)"";
	unsigned char *cmdbuf = GetMemory(256);
	memcpy(cmdbuf, cmdline, STRINGSIZE);
	getcsargs(&cmdbuf, 3);
	switch (argc)
	{
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
		if (*argv[2])
			cmd_args = getCstring(argv[2]);
		break;
	}

	// The memory allocated by getCstring() is not preserved across
	// a call to FileLoadProgram() so we need to cache 'filename' and
	// 'cmd_args' on the stack.
	unsigned char buf[MAXSTRLEN + 1];
	if (snprintf((char *)buf, MAXSTRLEN + 1, "\"%s\",%s", filename, cmd_args) > MAXSTRLEN)
	{
		error("RUN command line too long");
	}
	FreeMemory(cmdbuf);
	unsigned char *pcmd_args = buf + strlen((char *)filename) + 3; // *** THW 16/4/23
	*cmdline = 0;
	do_end(false);
	SaveContext();
	ClearVars(0, false);
	InitHeap(false);
	if (*buf && !FileLoadProgram(buf, true))
		return;
	ClearRuntime(false);
	if (PrepareProgram(true))
	{
		// Error in program - print message and don't run
		RestoreContext(false);
		PrintPreprogramError();
		return;
	}
	RestoreContext(false);
	if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
	{
		ClearScreen(gui_bcolour);
		CurrentX = 0;
		CurrentY = 0;
	}
	// Create a global constant MM.CMDLINE$ containing 'cmd_args'.
	//    void *ptr = findvar((unsigned char *)"MM.CMDLINE$", V_NOFIND_ERR);
	CtoM(pcmd_args);
	//    memcpy(cmdlinebuff, pcmd_args, *pcmd_args + 1); // *** THW 16/4/23
	Mstrcpy(cmdlinebuff, pcmd_args);
	IgnorePIN = false;
	if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
		ExecuteProgram(LibMemory); // run anything that might be in the library
	if (*ProgMemory != T_NEWLINE)
		return; // no program to run
#ifdef PICOMITEWEB
	cleanserver();
#endif
#ifndef USBKEYBOARD
	if (mouse0 == false && Option.MOUSE_CLOCK)
		initMouse0(0); // see if there is a mouse to initialise
#endif
	nextstmt = ProgMemory;
}
void cmd_chain(void)
{
	do_chain(cmdline);
}

void cmd_select(void)
{
	int i, type;
	unsigned char *p, *rp = NULL, *SaveCurrentLinePtr;
	void *v;
	MMFLOAT f = 0;
	long long int i64 = 0;
	unsigned char s[STRINGSIZE];

	// these are the tokens that we will be searching for
	// they are cached the first time this command is called

	type = T_NOTYPE;
	v = DoExpression(cmdline, &type); // evaluate the select case value
	type = TypeMask(type);
	if (type & T_NBR)
		f = *(MMFLOAT *)v;
	if (type & T_INT)
		i64 = *(long long int *)v;
	if (type & T_STR)
	{
		Mstrcpy((unsigned char *)s, (unsigned char *)v);
		ClearSpecificTempMemory(v); // free temp memory now that value is copied
	}

	// now search through the program looking for a matching CASE statement
	// i tracks the nesting level of any nested SELECT CASE commands
	SaveCurrentLinePtr = CurrentLinePtr; // save where we are because we will have to fake CurrentLinePtr to get errors reported correctly
	i = 1;
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, &rp, (unsigned char *)"No matching END SELECT");
		CommandToken tkn = commandtbl_decode(p);

		if (tkn == cmdSELECT_CASE)
			i++; // found a nested SELECT CASE command, increase the nested count and carry on searching
		// is this a CASE stmt at the same level as this SELECT CASE.
		if (tkn == cmdCASE && i == 1)
		{
			int t;
			MMFLOAT ft, ftt;
			long long int i64t, i64tt;
			unsigned char *st, *stt;

			CurrentLinePtr = rp; // and report errors at the line we are on
			p++;				 // step past rest of command token
			// loop through the comparison elements on the CASE line.  Each element is separated by a comma
			do
			{
				p++;
				skipspace(p);
				t = type;
				// check for CASE IS,  eg  CASE IS > 5  -or-  CASE > 5  and process it if it is
				// an operator can be >, <>, etc but it can also be a prefix + or - so we must not catch them
				if ((SaveCurrentLinePtr = checkstring(p, (unsigned char *)"IS")) || ((tokentype(*p) & T_OPER) && !(*p == GetTokenValue((unsigned char *)"+") || *p == GetTokenValue((unsigned char *)"-"))))
				{
					int o;
					if (SaveCurrentLinePtr)
						p += 2;
					skipspace(p);
					if (tokentype(*p) & T_OPER)
						o = *p++ - C_BASETOKEN; // get the operator
					else
						SyntaxError();
					;
					if (type & T_NBR)
						ft = f;
					if (type & T_INT)
						i64t = i64;
					if (type & T_STR)
						st = s;
					while (o != E_END)
						p = doexpr(p, &ft, &i64t, &st, &o, &t); // get the right hand side of the expression and evaluate the operator in o
					if (!(t & T_INT))
						SyntaxError();
					; // comparisons must always return an integer
					if (i64t)
					{ // evaluates to true
						skipelement(p);
						nextstmt = p;
						CurrentLinePtr = SaveCurrentLinePtr;
						return; // if we have a match just return to the interpreter and let it execute the code
					}
					else
					{ // evaluates to false
						skipspace(p);
						continue;
					}
				}

				// it must be either a single value (eg, "foo") or a range (eg, "foo" TO "zoo")
				// evaluate the first value
				p = evaluate(p, &ft, &i64t, &st, &t, true);
				skipspace(p);
				if (*p == tokenTO)
				{ // is there is a TO keyword?
					p++;
					t = type;
					p = evaluate(p, &ftt, &i64tt, &stt, &t, false); // evaluate the right hand side of the TO expression
					if (((type & T_NBR) && f >= ft && f <= ftt) || ((type & T_INT) && i64 >= i64t && i64 <= i64tt) || (((type & T_STR) && Mstrcmp(s, st) >= 0) && (Mstrcmp(s, stt) <= 0)))
					{
						if (type & T_STR)
						{
							ClearSpecificTempMemory(st);
							ClearSpecificTempMemory(stt);
						}
						skipelement(p);
						nextstmt = p;
						CurrentLinePtr = SaveCurrentLinePtr;
						return; // if we have a match just return to the interpreter and let it execute the code
					}
					else
					{
						skipspace(p);
						if (type & T_STR)
						{
							ClearSpecificTempMemory(st);
							ClearSpecificTempMemory(stt);
						}
						continue; // otherwise continue searching
					}
				}

				// if we got to here the element must be just a single match.  So make the test
				if (((type & T_NBR) && f == ft) || ((type & T_INT) && i64 == i64t) || ((type & T_STR) && Mstrcmp(s, st) == 0))
				{
					if (type & T_STR)
						ClearSpecificTempMemory(st);
					skipelement(p);
					nextstmt = p;
					CurrentLinePtr = SaveCurrentLinePtr;
					return; // if we have a match just return to the interpreter and let it execute the code
				}
				if (type & T_STR)
					ClearSpecificTempMemory(st);
				skipspace(p);
			} while (*p == ','); // keep looping through the elements on the CASE line
			checkend(p);
			CurrentLinePtr = SaveCurrentLinePtr;
		}

		// test if we have found a CASE ELSE statement at the same level as this SELECT CASE
		// if true it means that we did not find a matching CASE - so execute this code
		if (tkn == cmdCASE_ELSE && i == 1)
		{
			p += sizeof(CommandToken); // step over the token
			checkend(p);
			skipelement(p);
			nextstmt = p;
			CurrentLinePtr = SaveCurrentLinePtr;
			return;
		}

		if (tkn == cmdEND_SELECT)
		{
			i--;
			p++;
		} // found an END SELECT so decrement our nested counter

		if (i == 0)
		{
			// found our matching END SELECT stmt.  Step over it and continue with the statement after it
			skipelement(p);
			nextstmt = p;
			CurrentLinePtr = SaveCurrentLinePtr;
			return;
		}
	}
}

// if we have hit a CASE or CASE ELSE we must search for a END SELECT at this level and resume at that point
void cmd_case(void)
{
	int i;
	unsigned char *p;

	// search through the program looking for a END SELECT statement
	// i tracks the nesting level of any nested SELECT CASE commands
	i = 1;
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, NULL, (unsigned char *)"No matching END SELECT");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdSELECT_CASE)
			i++; // found a nested SELECT CASE command, we now need to search for its END CASE
		if (tkn == cmdEND_SELECT)
			i--; // found an END SELECT so decrement our nested counter
		if (i == 0)
		{
			// found our matching END SELECT stmt.  Step over it and continue with the statement after it
			skipelement(p);
			nextstmt = p;
			break;
		}
	}
}

void cmd_input(void)
{
	unsigned char s[STRINGSIZE];
	unsigned char *p, *sp, *tp;
	int i, fnbr;
	getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",;"); // this is a macro and must be the first executable stmt

	// is the first argument a file number specifier?  If so, get it
	if (argc >= 3 && *argv[0] == '#')
	{
		argv[0]++;
		fnbr = getinteger(argv[0]);
		i = 2;
	}
	else
	{
		fnbr = 0;
		// is the first argument a prompt?
		// if so, print it followed by an optional question mark
		if (argc >= 3 && *argv[0] == '"' && (*argv[1] == ',' || *argv[1] == ';'))
		{
			MMPrintString((char *)getCstring(argv[0]));
			if (*argv[1] == ';')
				MMPrintString((char *)"? ");
			i = 2;
		}
		else
		{
			MMPrintString((char *)"? "); // no prompt?  then just print the question mark
			i = 0;
		}
	}

	if (argc - i < 1)
		SyntaxError();
	;								 // no variable to input to
	*inpbuf = 0;					 // start with an empty buffer
	MMgetline(fnbr, (char *)inpbuf); // get the line
	p = inpbuf;

	// step through the variables listed for the input statement
	// and find the next item on the line and assign it to the variable
	for (; i < argc; i++)
	{
		sp = s; // sp is a temp pointer into s[]
		if (*argv[i] == ',' || *argv[i] == ';')
			continue;
		skipspace(p);
		if (*p != 0)
		{
			if (*p == '"')
			{		 // if it is a quoted string
				p++; // step over the quote
				while (*p && *p != '"')
					*sp++ = *p++; // and copy everything upto the next quote
				while (*p && *p != ',')
					p++; // then find the next comma
			}
			else
			{ // otherwise it is a normal string of characters
				while (*p && *p != ',')
					*sp++ = *p++; // copy up to the comma
				while (sp > s && sp[-1] == ' ')
					sp--; // and trim trailing whitespace
			}
		}
		*sp = 0;					   // terminate the string
		tp = findvar(argv[i], V_FIND); // get the variable and save its new value
		if (g_vartbl[g_VarIndex].type & T_CONST)
			StandardError(22);
		int inp_vtype = g_vartbl[g_VarIndex].type;
		int inp_size = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
		if (g_StructMemberType != 0)
		{
			inp_vtype = g_StructMemberType;
			if (g_StructMemberType & T_STR)
				inp_size = g_StructMemberSize;
		}
#endif
		if (inp_vtype & T_STR)
		{
			if (strlen((char *)s) > inp_size)
				error("String too long");
			strcpy((char *)tp, (char *)s);
			CtoM(tp); // convert to a MMBasic string
		}
		else if (inp_vtype & T_INT)
		{
			*((long long int *)tp) = strtoll((char *)s, (char **)&sp, 10); // convert to an integer
		}
		else
			*((MMFLOAT *)tp) = (MMFLOAT)atof((char *)s);
		if (*p == ',')
			p++;
	}
}

void MIPS16 cmd_trace(void)
{
	if (checkstring(cmdline, (unsigned char *)"ON"))
		TraceOn = true;
	else if (checkstring(cmdline, (unsigned char *)"OFF"))
		TraceOn = false;
	else if (checkstring(cmdline, (unsigned char *)"LIST"))
	{
		int i;
		cmdline += 4;
		skipspace(cmdline);
		if (*cmdline == 0 || *cmdline == (unsigned char)'\'') //'
			i = TRACE_BUFF_SIZE - 1;
		else
			i = getint(cmdline, 0, TRACE_BUFF_SIZE - 1);
		i = TraceBuffIndex - i;
		if (i < 0)
			i += TRACE_BUFF_SIZE;
		while (i != TraceBuffIndex)
		{
			if (TraceBuff[i] >= ProgMemory && TraceBuff[i] <= ProgMemory + MAX_PROG_SIZE)
			{
				inpbuf[0] = '[';
				IntToStr((char *)inpbuf + 1, CountLines(TraceBuff[i]), 10);
				strcat((char *)inpbuf, "]");
			}
			else if (TraceBuff[i])
			{
				strcpy((char *)inpbuf, "[Lib]");
			}
			else
			{
				inpbuf[0] = 0;
			}
			MMPrintString((char *)inpbuf);
			if (++i >= TRACE_BUFF_SIZE)
				i = 0;
		}
	}
	else
		StandardError(36);
}
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
static inline int mystrncasecmp(
	const unsigned char *s1, /* First string. */
	const unsigned char *s2, /* Second string. */
	size_t length)			 /* Maximum number of characters to compare
							  * (stop earlier if the end of either string
							  * is reached). */
{
	register unsigned char u1, u2;

	for (; length != 0; length--, s1++, s2++)
	{
		u1 = (unsigned char)*s1;
		u2 = (unsigned char)*s2;
		if (mytoupper(u1) != mytoupper(u2))
		{
			return mytoupper(u1) - mytoupper(u2);
		}
		if (u1 == '\0')
		{
			return 0;
		}
	}
	return 0;
}

// FOR command
#if LOWRAM
void cmd_for(void)
{
#else
void __not_in_flash_func(cmd_for)(void)
{
#endif

	int i, t, vlen, test;
	unsigned char ss[4]; // this will be used to split up the argument line
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
	{							  // start a new block
		getargs(&cmdline, 7, ss); // getargs macro must be the first executable stmt in a block
		if (argc < 5 || argc == 6 || *argv[1] != ss[0] || *argv[3] != ss[1])
			error("FOR with misplaced = or TO");
		if (argc == 6 || (argc == 7 && *argv[5] != ss[2]))
			SyntaxError();
		;

		// get the variable name and trim any spaces
		vname = argv[0];
		if (*vname && *vname == ' ')
			vname++;
		while (*vname && vname[strlen((char *)vname) - 1] == ' ')
			vname[strlen((char *)vname) - 1] = 0;
		vlen = strlen((char *)vname);
		vptr = findvar(argv[0], V_FIND); // create the variable
		if (g_vartbl[g_VarIndex].type & T_CONST)
			StandardError(22);
		vtype = TypeMask(g_vartbl[g_VarIndex].type);
#ifdef STRUCTENABLED
		if (g_StructMemberType != 0)
			vtype = TypeMask(g_StructMemberType);
#endif
		if (vtype & T_STR)
			StandardError(6); // sanity check

		// check if the FOR variable is already in the stack and remove it if it is
		// this is necessary as the program can jump out of the loop without hitting
		// the NEXT statement and this will eventually result in a stack overflow
		for (i = 0; i < g_forindex; i++)
		{
			if (g_forstack[i].var == vptr && g_forstack[i].level == g_LocalIndex)
			{
				while (i < g_forindex - 1)
				{
					g_forstack[i].forptr = g_forstack[i + 1].forptr;
					g_forstack[i].nextptr = g_forstack[i + 1].nextptr;
					g_forstack[i].var = g_forstack[i + 1].var;
					g_forstack[i].vartype = g_forstack[i + 1].vartype;
					g_forstack[i].level = g_forstack[i + 1].level;
					g_forstack[i].tovalue.i = g_forstack[i + 1].tovalue.i;
					g_forstack[i].stepvalue.i = g_forstack[i + 1].stepvalue.i;
					i++;
				}
				g_forindex--;
				break;
			}
		}

		if (g_forindex == MAXFORLOOPS)
			error("Too many nested FOR loops");

		g_forstack[g_forindex].var = vptr;			 // save the variable index
		g_forstack[g_forindex].vartype = vtype;		 // save the type of the variable
		g_forstack[g_forindex].level = g_LocalIndex; // save the level of the variable in terms of sub/funs
		g_forindex++;								 // incase functions use for loops
		if (vtype & T_NBR)
		{
			*(MMFLOAT *)vptr = getnumber(argv[2]);					   // get the starting value for a float and save
			g_forstack[g_forindex - 1].tovalue.f = getnumber(argv[4]); // get the to value and save
			if (argc == 7)
				g_forstack[g_forindex - 1].stepvalue.f = getnumber(argv[6]); // get the step value for a float and save
			else
				g_forstack[g_forindex - 1].stepvalue.f = 1.0; // default is +1
		}
		else
		{
			*(long long int *)vptr = getinteger(argv[2]);				// get the starting value for an integer and save
			g_forstack[g_forindex - 1].tovalue.i = getinteger(argv[4]); // get the to value and save
			if (argc == 7)
				g_forstack[g_forindex - 1].stepvalue.i = getinteger(argv[6]); // get the step value for an integer and save
			else
				g_forstack[g_forindex - 1].stepvalue.i = 1; // default is +1
		}
		g_forindex--;

		g_forstack[g_forindex].forptr = nextstmt + 1; // return to here when looping

		// now find the matching NEXT command
		t = 1;
		p = nextstmt;
		while (1)
		{
			p = GetNextCommand(p, &tp, (unsigned char *)"No matching NEXT");
			//            if(*p == fortoken) t++;                                 // count the FOR
			//            if(*p == nexttoken) {                                   // is it NEXT
			CommandToken tkn = commandtbl_decode(p);

			if (tkn == cmdFOR)
				t++; // count the FOR

			if (tkn == cmdNEXT)
			{								   // is it NEXT
				xp = p + sizeof(CommandToken); // point to after the NEXT token
				while (*xp && mystrncasecmp(xp, vname, vlen))
					xp++;						  // step through looking for our variable
				if (*xp && !isnamechar(xp[vlen])) // is it terminated correctly?
					t = 0;						  // yes, found the matching NEXT
				else
					t--; // no luck, just decrement our stack counter
			}
			if (t == 0)
			{										// found the matching NEXT
				g_forstack[g_forindex].nextptr = p; // pointer to the start of the NEXT command
				break;
			}
		}

		// test the loop value at the start
		if (g_forstack[g_forindex].vartype & T_INT)
			test = (g_forstack[g_forindex].stepvalue.i >= 0 && *(long long int *)vptr > g_forstack[g_forindex].tovalue.i) || (g_forstack[g_forindex].stepvalue.i < 0 && *(long long int *)vptr < g_forstack[g_forindex].tovalue.i);
		else
			test = (g_forstack[g_forindex].stepvalue.f >= 0 && *(MMFLOAT *)vptr > g_forstack[g_forindex].tovalue.f) || (g_forstack[g_forindex].stepvalue.f < 0 && *(MMFLOAT *)vptr < g_forstack[g_forindex].tovalue.f);

		if (test)
		{
			// loop is invalid at the start, so go to the end of the NEXT command
			skipelement(p); // find the command after the NEXT command
			nextstmt = p;	// this is where we will continue
		}
		else
		{
			g_forindex++; // save the loop data and continue on with the command after the FOR statement
		}
	}
}

#if LOWRAM
void cmd_next(void)
{
#else
#ifdef rp2350
void __not_in_flash_func(cmd_next)(void)
#else
void MIPS16 __not_in_flash_func(cmd_next)(void)
#endif
{
#endif
	int i, vindex, test;
	void *vtbl[MAXFORLOOPS];
	int vcnt;
	unsigned char *p;
	getargs(&cmdline, MAXFORLOOPS * 2, (unsigned char *)","); // getargs macro must be the first executable stmt in a block

	vindex = 0; // keep lint happy

	for (vcnt = i = 0; i < argc; i++)
	{
		if (i & 0x01)
		{
			if (*argv[i] != ',')
				SyntaxError();
			;
		}
		else
			vtbl[vcnt++] = findvar(argv[i], V_FIND | V_NOFIND_ERR); // find the variable and error if not found
	}

loopback:
	// first search the for stack for a loop with the same variable specified on the NEXT's line
	if (vcnt)
	{
		for (i = g_forindex - 1; i >= 0; i--)
			for (vindex = vcnt - 1; vindex >= 0; vindex--)
				if (g_forstack[i].var == vtbl[vindex])
					goto breakout;
	}
	else
	{
		// if no variables specified search the for stack looking for an entry with the same program position as
		// this NEXT statement. This cheats by using the cmdline as an identifier and may not work inside an IF THEN ELSE
		for (i = 0; i < g_forindex; i++)
		{
			p = g_forstack[i].nextptr + sizeof(CommandToken);
			skipspace(p);
			if (p == cmdline)
				goto breakout;
		}
	}

	error("Cannot find a matching FOR");

breakout:

	// found a match
	// apply the STEP value to the variable and test against the TO value
	if (g_forstack[i].vartype & T_INT)
	{
		*(long long int *)g_forstack[i].var += g_forstack[i].stepvalue.i;
		test = (g_forstack[i].stepvalue.i >= 0 && *(long long int *)g_forstack[i].var > g_forstack[i].tovalue.i) || (g_forstack[i].stepvalue.i < 0 && *(long long int *)g_forstack[i].var < g_forstack[i].tovalue.i);
	}
	else
	{
		*(MMFLOAT *)g_forstack[i].var += g_forstack[i].stepvalue.f;
		test = (g_forstack[i].stepvalue.f >= 0 && *(MMFLOAT *)g_forstack[i].var > g_forstack[i].tovalue.f) || (g_forstack[i].stepvalue.f < 0 && *(MMFLOAT *)g_forstack[i].var < g_forstack[i].tovalue.f);
	}

	if (test)
	{
		// the loop has terminated
		// remove the entry in the table, then skip forward to the next element and continue on from there
		while (i < g_forindex - 1)
		{
			g_forstack[i].forptr = g_forstack[i + 1].forptr;
			g_forstack[i].nextptr = g_forstack[i + 1].nextptr;
			g_forstack[i].var = g_forstack[i + 1].var;
			g_forstack[i].vartype = g_forstack[i + 1].vartype;
			g_forstack[i].level = g_forstack[i + 1].level;
			g_forstack[i].tovalue.i = g_forstack[i + 1].tovalue.i;
			g_forstack[i].stepvalue.i = g_forstack[i + 1].stepvalue.i;
			i++;
		}
		g_forindex--;
		if (vcnt > 0)
		{
			// remove that entry from our FOR stack
			for (; vindex < vcnt - 1; vindex++)
				vtbl[vindex] = vtbl[vindex + 1];
			vcnt--;
			if (vcnt > 0)
				goto loopback;
			else
				return;
		}
	}
	else
	{
		// we have not reached the terminal value yet, so go back and loop again
		nextstmt = g_forstack[i].forptr;
	}
}

#if LOWRAM
void cmd_do(void)
{
#else
void MIPS16 __not_in_flash_func(cmd_do)(void)
{
#endif
	int i, doUntil;
	unsigned char *p, *tp, *evalp;
	if (cmdtoken == cmdWHILE)
		StandardError(36);
	// if it is a DO loop find the WHILE/UNTIL token and (if found) get a pointer to its expression
	while (*cmdline && *cmdline != tokenWHILE && *cmdline != tokenUNTIL)
		cmdline++;
	if (*cmdline == tokenWHILE || *cmdline == tokenUNTIL)
	{
		doUntil = (*cmdline == tokenUNTIL);
		evalp = ++cmdline;
	}
	else
	{
		doUntil = 0;
		evalp = NULL;
	}
	// check if this loop is already in the stack and remove it if it is
	// this is necessary as the program can jump out of the loop without hitting
	// the LOOP or WEND stmt and this will eventually result in a stack overflow
	for (i = 0; i < g_doindex; i++)
	{
		if (g_dostack[i].doptr == nextstmt)
		{
			while (i < g_doindex - 1)
			{
				g_dostack[i].evalptr = g_dostack[i + 1].evalptr;
				g_dostack[i].loopptr = g_dostack[i + 1].loopptr;
				g_dostack[i].doptr = g_dostack[i + 1].doptr;
				g_dostack[i].level = g_dostack[i + 1].level;
				g_dostack[i].untiltest = g_dostack[i + 1].untiltest;
				i++;
			}
			g_doindex--;
			break;
		}
	}

	// add our pointers to the top of the stack
	if (g_doindex == MAXDOLOOPS)
		error("Too many nested DO or WHILE loops");
	g_dostack[g_doindex].evalptr = evalp;
	g_dostack[g_doindex].doptr = nextstmt;
	g_dostack[g_doindex].level = g_LocalIndex;
	g_dostack[g_doindex].untiltest = doUntil;

	// now find the matching LOOP command
	i = 1;
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, &tp, (unsigned char *)"No matching LOOP");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdtoken)
			i++; // entered a nested DO or WHILE loop
		if (tkn == cmdLOOP)
			i--; // exited a nested loop

		if (i == 0)
		{ // found our matching LOOP or WEND stmt
			g_dostack[g_doindex].loopptr = p;
			break;
		}
	}

	if (g_dostack[g_doindex].evalptr != NULL)
	{
		// if this is a DO WHILE ... LOOP statement
		// search the LOOP statement for a WHILE or UNTIL token (p is pointing to the matching LOOP statement)
		p += sizeof(CommandToken);
		while (*p && *p < 0x80)
			p++;
		if (*p == tokenWHILE)
			error("LOOP has a WHILE test");
		if (*p == tokenUNTIL)
			error("LOOP has an UNTIL test");
	}

	g_doindex++;

	// do the evaluation (if there is something to evaluate) and if false go straight to the command after the LOOP or WEND statement
	if (g_dostack[g_doindex - 1].evalptr != NULL)
	{
		int condval;
#ifdef CACHE
		{
			int _cached_r;
			if ((g_trace_cache_flags & TCF_LOOP) && TraceCacheTryIf(g_dostack[g_doindex - 1].evalptr, &_cached_r))
				condval = _cached_r;
			else
				condval = (getnumber(g_dostack[g_doindex - 1].evalptr) != 0);
		}
#else
		condval = (getnumber(g_dostack[g_doindex - 1].evalptr) != 0);
#endif
		if (g_dostack[g_doindex - 1].untiltest) condval = !condval; // DO UNTIL: skip body if condition is already true
		if (!condval)
		{
			g_doindex--;							 // remove the entry in the table
			nextstmt = g_dostack[g_doindex].loopptr; // point to the LOOP or WEND statement
			skipelement(nextstmt);					 // skip to the next command
		}
	}
}

#if WEBRP2350
void cmd_loop(void)
{
#else
void MIPS16 __not_in_flash_func(cmd_loop)(void)
{
#endif
	unsigned char *p;
	int tst = 0; // initialise tst to stop the compiler from complaining
	int i;

	// search the do table looking for an entry with the same program position as this LOOP statement
	for (i = 0; i < g_doindex; i++)
	{
		p = g_dostack[i].loopptr + sizeof(CommandToken);
		skipspace(p);
		if (p == cmdline)
		{
			// found a match
			// first check if the DO statement had a WHILE component
			// if not find the WHILE statement here and evaluate it
			if (g_dostack[i].evalptr == NULL)
			{ // if it was a DO without a WHILE/UNTIL
				if (*cmdline >= 0x80)
				{ // if there is something
					if (*cmdline != tokenWHILE && *cmdline != tokenUNTIL)
						SyntaxError();
					int isUntil = (*cmdline == tokenUNTIL);
					unsigned char *condkey = ++cmdline; // advance past the keyword token
					int condval;
#ifdef CACHE
					{
						int _cached_r;
						if ((g_trace_cache_flags & TCF_LOOP) && TraceCacheTryIf(condkey, &_cached_r))
							condval = _cached_r;
						else
							condval = (getnumber(condkey) != 0);
					}
#else
					condval = (getnumber(condkey) != 0);
#endif
					tst = isUntil ? !condval : condval;
				}
				else
				{
					tst = 1;		   // and loop forever
					checkend(cmdline); // make sure that there is nothing else
				}
			}
			else
			{												  // if was DO WHILE or DO UNTIL
				int condval;
#ifdef CACHE
				{
					int _cached_r;
					if ((g_trace_cache_flags & TCF_LOOP) && TraceCacheTryIf(g_dostack[i].evalptr, &_cached_r))
						condval = _cached_r;
					else
						condval = (getnumber(g_dostack[i].evalptr) != 0);
				}
#else
				condval = (getnumber(g_dostack[i].evalptr) != 0);
#endif
				tst = g_dostack[i].untiltest ? !condval : condval; // UNTIL inverts: loop while condition is false
				checkend(cmdline);							  // make sure that there is nothing else
			}

			// test the expression value and reset the program pointer if we are still looping
			// otherwise remove this entry from the do stack
			if (tst)
				nextstmt = g_dostack[i].doptr; // loop again
			else
			{
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

void cmd_exitfor(void)
{
	if (g_forindex == 0)
		error("No FOR loop is in effect");
	nextstmt = g_forstack[--g_forindex].nextptr;
	checkend(cmdline);
	skipelement(nextstmt);
}

void cmd_exit(void)
{
	if (g_doindex == 0)
		error("No DO loop is in effect");
	nextstmt = g_dostack[--g_doindex].loopptr;
	checkend(cmdline);
	skipelement(nextstmt);
}

void cmd_error(void)
{
	unsigned char *s;
	if (*cmdline && *cmdline != '\'')
	{
		s = getCstring(cmdline);
		// CurrentLinePtr = NULL;                                      // suppress printing the line that caused the issue
		error((char *)s);
	}
	else
		error("");
}

#ifndef rp2350
void cmd_randomize(void)
{
	int i;
	getcsargs(&cmdline, 1);
	if (argc == 1)
		i = getinteger(argv[0]);
	else
		i = time_us_32();
	if (i < 0)
		StandardError(21);
	srand(i);
}
#endif

// this is the Sub or Fun command
// it simply skips over text until it finds the end of it
void cmd_subfun(void)
{
	unsigned char *p;
	unsigned short returntoken, errtoken;

	if (gosubindex != 0)
		error("No matching END declaration"); // we have hit a SUB/FUN while in another SUB or FUN
	if (cmdtoken == cmdSUB)
	{
		returntoken = cmdENDSUB;
		errtoken = cmdENDFUNCTION;
	}
	else
	{
		returntoken = cmdENDFUNCTION;
		errtoken = cmdENDSUB;
	}
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, NULL, (unsigned char *)"No matching END declaration");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdSUB || tkn == cmdFUN || tkn == errtoken)
			error("No matching END declaration");
		if (tkn == returntoken)
		{ // found the next return
			skipelement(p);
			nextstmt = p; // point to the next command
			break;
		}
	}
}
// this is the Sub or Fun command
// it simply skips over text until it finds the end of it
void cmd_comment(void)
{
	unsigned char *p;
	unsigned short returntoken;

	returntoken = GetCommandValue((unsigned char *)"*/");
	//	errtoken = cmdENDSUB;
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, NULL, (unsigned char *)"No matching END declaration");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdComment)
			error("No matching END declaration");
		if (tkn == returntoken)
		{ // found the next return
			skipelement(p);
			nextstmt = p; // point to the next command
			break;
		}
	}
}
void cmd_endcomment(void)
{
}

void cmd_gosub(void)
{
	if (gosubindex >= MAXGOSUB)
		error("Too many nested GOSUB");
	char *return_to = (char *)nextstmt;
#ifdef CACHE
	int _gosub_cacheable = (g_trace_cache_flags & TCF_JUMP) &&
		cmdline >= ProgMemory && cmdline < ProgMemory + MAX_PROG_SIZE;
	if (_gosub_cacheable)
	{
		unsigned char *cached_tgt;
		if (TraceCacheTryJump(cmdline, &cached_tgt))
		{
			nextstmt = cached_tgt;
			goto gosub_resolved;
		}
	}
#endif
	if (isnamestart(*cmdline))
		nextstmt = findlabel(cmdline);
	else
		nextstmt = findline(getinteger(cmdline), true);
#ifdef CACHE
	if (_gosub_cacheable)
		TraceCacheStoreJump(cmdline, nextstmt);
gosub_resolved:;
#endif
	IgnorePIN = false;

	errorstack[gosubindex] = CurrentLinePtr;
	gosubstack[gosubindex++] = (unsigned char *)return_to;
	g_LocalIndex++;
#ifdef CACHE
	EnterLocalFrame(); // open a new local-variable frame for this GOSUB
#endif
	CurrentLinePtr = nextstmt;
}

void cmd_mid(void)
{
	unsigned char *p;
	int mid_vtype;
	getcsargs(&cmdline, 5);
	findvar(argv[0], V_NOFIND_ERR);
	if (g_vartbl[g_VarIndex].type & T_CONST)
		StandardError(22);
	mid_vtype = g_vartbl[g_VarIndex].type;
	int size = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
	if (g_StructMemberType != 0)
	{
		mid_vtype = g_StructMemberType;
		if (g_StructMemberType & T_STR)
			size = g_StructMemberSize;
	}
#endif
	if (!(mid_vtype & T_STR))
		error("Not a string");
	char *sourcestring = (char *)getstring(argv[0]);
	int start = getint(argv[2], 1, sourcestring[0]);
	int num = -1;
	if (argc == 5)
		num = getint(argv[4], 0, sourcestring[0]);
	if (start + (num < 0 ? 0 : num - 1) > sourcestring[0])
		error("Selection exceeds length of string");
	while (*cmdline && tokenfunction(*cmdline) != op_equal)
		cmdline++;
	if (!*cmdline)
		SyntaxError();
	;
	++cmdline;
	if (!*cmdline)
		SyntaxError();
	;
	char *value = (char *)getstring(cmdline);
	if (num == -1)
		num = value[0];
	p = (unsigned char *)&value[1];
	if (num == value[0])
		memcpy(&sourcestring[start], p, num);
	else
	{
		int change = value[0] - num;
		if (sourcestring[0] + change > size)
			error("String too long");
		memmove(&sourcestring[start + value[0]], &sourcestring[start + num], sourcestring[0] - (start + num - 1));
		sourcestring[0] += change;
		memcpy(&sourcestring[start], p, value[0]);
	}
}
void cmd_byte(void)
{
	int byte_vtype;
	getcsargs(&cmdline, 3);
	findvar(argv[0], V_NOFIND_ERR);
	if (g_vartbl[g_VarIndex].type & T_CONST)
		StandardError(22);
	byte_vtype = g_vartbl[g_VarIndex].type;
#ifdef STRUCTENABLED
	if (g_StructMemberType != 0)
		byte_vtype = g_StructMemberType;
#endif
	if (!(byte_vtype & T_STR))
		error("Not a string");
	unsigned char *sourcestring = (unsigned char *)getstring(argv[0]);
	int start = getint(argv[2], 1, sourcestring[0]);
	while (*cmdline && tokenfunction(*cmdline) != op_equal)
		cmdline++;
	if (!*cmdline)
		SyntaxError();
	;
	++cmdline;
	if (!*cmdline)
		SyntaxError();
	;
	int value = getint(cmdline, 0, 255);
	sourcestring[start] = value;
}
void cmd_bit(void)
{
	int bit_vtype;
	getcsargs(&cmdline, 3);
	uint64_t *source = (uint64_t *)findvar(argv[0], V_NOFIND_ERR);
	if (g_vartbl[g_VarIndex].type & T_CONST)
		StandardError(22);
	bit_vtype = g_vartbl[g_VarIndex].type;
#ifdef STRUCTENABLED
	if (g_StructMemberType != 0)
		bit_vtype = g_StructMemberType;
#endif
	if (!(bit_vtype & T_INT))
		error("Not an integer");
	uint64_t bit = (uint64_t)1 << (uint64_t)getint(argv[2], 0, 63);
	while (*cmdline && tokenfunction(*cmdline) != op_equal)
		cmdline++;
	if (!*cmdline)
		SyntaxError();
	;
	++cmdline;
	if (!*cmdline)
		SyntaxError();
	;
	int value = getint(cmdline, 0, 1);
	if (value)
		*source |= bit;
	else
		*source &= (~bit);
}
void cmd_flags(void)
{
	while (*cmdline && tokenfunction(*cmdline) != op_equal)
		cmdline++;
	if (!*cmdline)
		SyntaxError();
	;
	g_flag = getinteger(++cmdline);
}

void cmd_flag(void)
{
	getcsargs(&cmdline, 1);
	uint64_t bit = (uint64_t)1 << (uint64_t)getint(argv[0], 0, 63);
	while (*cmdline && tokenfunction(*cmdline) != op_equal)
		cmdline++;
	if (!*cmdline)
		SyntaxError();
	;
	++cmdline;
	if (!*cmdline)
		SyntaxError();
	;
	int value = getint(cmdline, 0, 1);
	if (value)
		g_flag |= bit;
	else
		g_flag &= ~bit;
}

void MIPS16 __not_in_flash_func(cmd_return)(void)
{
	checkend(cmdline);
	if (gosubindex == 0 || gosubstack[gosubindex - 1] == NULL)
		error("Nothing to return to");
	ClearVars(g_LocalIndex--, true); // delete any local variables
#ifdef CACHE
	LeaveLocalFrame(); // pop this GOSUB's local frame
#endif
	g_TempMemoryIsChanged = true;		 // signal that temporary memory should be checked
	nextstmt = gosubstack[--gosubindex]; // return to the caller
	CurrentLinePtr = errorstack[gosubindex];
}
#ifdef rp2350
// === Frame command implementation ===
// Generalised frame/panel system with grid-based box drawing,
// automatic panel registration, and panel-aware text output.

#define c_topleft 218
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
#define c_d_topleft 201
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
#define c_ds_tleft 198	// single vertical, double horizontal right
#define c_ds_tright 181 // single vertical, double horizontal left
#define c_sd_tleft 199	// double vertical, single horizontal right
#define c_sd_tright 182 // double vertical, single horizontal left

typedef struct
{
	int x1, y1; // top-left of panel interior (panel-local coords)
	int x2, y2; // bottom-right of panel interior
	int cx, cy; // cursor within panel (relative to x1/y1)
	bool active;
	uint16_t *buffer;	// buffer to write to (NULL = main frame)
	int buf_stride;		// row stride of buffer (0 = use framex)
	uint8_t default_fc; // default foreground colour (RGB121 index)
	uint8_t default_bc; // default background colour (RGB121 index)
	uint16_t *vbuf;		// virtual buffer (NULL = no vbuf, write direct)
	int vw, vh;			// virtual buffer dimensions
	int sx, sy;			// scroll/viewport offset into virtual buffer
} FramePanel;

typedef struct
{
	uint16_t *buffer;  // overlay's own frame buffer
	int width, height; // dimensions in characters
	int panel_id;	   // which panel ID this overlay is
	int ox, oy;		   // display position on main frame
	bool visible;	   // shown or hidden
	int zorder;		   // z-order (higher = on top)
} FrameOverlay;

unsigned short *frame = NULL, *outframe = NULL;
static bool framecursor_on = false; // FRAME CURSOR ON/OFF â€” serial cursor visibility
static int frame_last_panel = 0;	// last panel_id written to by FRAME PRINT
static int framex = 0, framey = 0;
static int lcd_cursor_x = -1, lcd_cursor_y = -1; // LCD cursor screen position
static bool lcd_cursor_active = false;			 // LCD inverted cursor is drawn
static uint64_t lcd_cursor_blink_time = 0;		 // last blink toggle (microseconds)
static bool lcd_cursor_blink_on = false;		 // true = cursor visible (inverted), false = normal
#define CURSOR_BLINK_US 500000					 // cursor blink interval (500ms)
static FramePanel *framepanels = NULL;
static int num_panels = 0;
static int max_panels = 0;
static FrameOverlay *frame_overlays = NULL;
static int num_overlays = 0;
static int max_overlays = 0;
static int overlay_zcounter = 0;

// Ensure the framepanels array can hold at least 'needed' panels.
// Grows the allocation in chunks of 8 when required.
static void ensure_panels(int needed)
{
	if (needed <= max_panels)
		return;
	int newmax = (needed + 7) & ~7; // round up to next multiple of 8
	FramePanel *np = (FramePanel *)GetMemory(newmax * sizeof(FramePanel));
	if (framepanels)
	{
		memcpy(np, framepanels, num_panels * sizeof(FramePanel));
		FreeMemorySafe((void **)&framepanels);
	}
	memset(&np[num_panels], 0, (newmax - num_panels) * sizeof(FramePanel));
	framepanels = np;
	max_panels = newmax;
}

static void MIPS16 free_overlays(void)
{
	for (int i = 0; i < num_overlays; i++)
	{
		FreeMemorySafe((void **)&frame_overlays[i].buffer);
	}
	FreeMemorySafe((void **)&frame_overlays);
	num_overlays = 0;
	max_overlays = 0;
	overlay_zcounter = 0;
}

static FrameOverlay MIPS16 *find_overlay_by_panel(int panel_id)
{
	for (int i = 0; i < num_overlays; i++)
	{
		if (frame_overlays[i].panel_id == panel_id)
			return &frame_overlays[i];
	}
	return NULL;
}

void MIPS16 closeframe(void)
{
	free_overlays();
	// Free any vbufs
	for (int i = 0; i < num_panels; i++)
	{
		if (framepanels[i].vbuf)
			FreeMemorySafe((void **)&framepanels[i].vbuf);
	}
	if (frame)
	{
		FreeMemorySafe((void **)&frame);
		FreeMemorySafe((void **)&outframe);
	}
	FreeMemorySafe((void **)&framepanels);
	num_panels = 0;
	max_panels = 0;
	framex = 0;
	framey = 0;
	framecursor_on = false;
	frame_last_panel = 0;
	lcd_cursor_active = false;
	lcd_cursor_x = -1;
	lcd_cursor_y = -1;
	lcd_cursor_blink_time = 0;
	lcd_cursor_blink_on = false;
}

static inline void framewritechar_to(uint16_t *buf, int stride, int x, int y, uint8_t ascii, uint8_t fcolour, uint8_t bcolour)
{
	buf[(y * stride) + x] = ascii | (fcolour << 8) | (bcolour << 12);
}

static inline void framewritechar(int x, int y, uint8_t ascii, uint8_t fcolour, uint8_t bcolour)
{
	if (x < 0 || x >= framex || y < 0 || y >= framey)
		return;
	frame[(y * framex) + x] = ascii | (fcolour << 8) | (bcolour << 12);
}

static inline uint16_t framegetchar(int x, int y)
{
	return frame[(y * framex) + x];
}

// Draw a single cell to LCD ONLY at screen position (x,y) with given cell value.
// Does NOT touch serial output at all.
static void MIPS16 frame_draw_cell_at(int x, int y, uint16_t cell)
{
	int ch = cell & 0xFF;
	if (ch == 0)
		ch = ' ';
	// Save and restore gui colours so we don't corrupt serial state
	int save_fc = gui_fcolour, save_bc = gui_bcolour;
	gui_fcolour = colours[(cell >> 8) & 0xF];
	gui_bcolour = colours[(cell >> 12) & 0xF];
	CurrentX = x * gui_font_width;
	CurrentY = y * gui_font_height;
	DisplayPutC(ch);
	gui_fcolour = save_fc;
	gui_bcolour = save_bc;
}

// Restore the cell under the LCD cursor to its normal (non-inverted) state.
static void MIPS16 frame_restore_lcd_cursor(void)
{
	if (!lcd_cursor_active || !outframe)
		return;
	if (lcd_cursor_x < 0 || lcd_cursor_x >= framex ||
		lcd_cursor_y < 0 || lcd_cursor_y >= framey)
	{
		lcd_cursor_active = false;
		return;
	}
	uint16_t cell = outframe[lcd_cursor_y * framex + lcd_cursor_x];
	frame_draw_cell_at(lcd_cursor_x, lcd_cursor_y, cell);
	lcd_cursor_active = false;
}

// Draw the LCD cursor by inverting fg/bg at the given screen position.
static void MIPS16 frame_draw_lcd_cursor(int x, int y)
{
	if (!outframe || x < 0 || x >= framex || y < 0 || y >= framey)
		return;
	uint16_t cell = outframe[y * framex + x];
	// Swap fg (bits 8-11) and bg (bits 12-15)
	int fg = (cell >> 8) & 0xF;
	int bg = (cell >> 12) & 0xF;
	// If fg == bg, inversion would be invisible; force a visible contrast
	if (fg == bg)
	{
		fg = (bg == 0xF) ? 0 : 0xF; // white on black, or black on white
	}
	int ch = cell & 0xFF;
	if (ch == 0)
		ch = ' ';
	uint16_t inverted = ch | (bg << 8) | (fg << 12);
	frame_draw_cell_at(x, y, inverted);
	lcd_cursor_x = x;
	lcd_cursor_y = y;
	lcd_cursor_active = true;
}

// Toggle cursor blink at screen position (x,y) using time_us_64().
// Handles both LCD (invert/restore) and serial (show/hide) cursors.
// Call repeatedly from wait loops; it returns immediately if not time to toggle yet.
static void MIPS16 frame_blink_cursor(int screen_x, int screen_y)
{
	if (!framecursor_on)
		return;
	uint64_t now = time_us_64();
	if (now - lcd_cursor_blink_time < CURSOR_BLINK_US)
		return;
	lcd_cursor_blink_time = now;
	lcd_cursor_blink_on = !lcd_cursor_blink_on;
	if (lcd_cursor_blink_on)
	{
		// Show cursor
		if (Option.DISPLAY_TYPE)
			frame_draw_lcd_cursor(screen_x, screen_y);
		SSPrintString("\033[?25h");
	}
	else
	{
		// Hide cursor
		if (Option.DISPLAY_TYPE && lcd_cursor_active)
			frame_restore_lcd_cursor();
		SSPrintString("\033[?25l");
	}
}

// Get buffer and stride for a panel (overlay or main frame)
static inline void MIPS16 panel_buf(FramePanel *pnl, uint16_t **buf, int *stride)
{
	if (pnl->vbuf)
	{
		// Virtual buffer: all writes go here
		*buf = pnl->vbuf;
		*stride = pnl->vw;
	}
	else if (pnl->buffer)
	{
		*buf = pnl->buffer;
		*stride = pnl->buf_stride;
	}
	else
	{
		*buf = frame;
		*stride = framex;
	}
}

// Get the write origin and dimensions for a panel.
// For vbuf panels, writes target the full virtual buffer at (0,0).
// For normal panels, writes target the panel interior in the frame/overlay buffer.
static inline void panel_write_geometry(FramePanel *pnl, int *ox, int *oy, int *pw, int *ph)
{
	if (pnl->vbuf)
	{
		*ox = 0;
		*oy = 0;
		*pw = pnl->vw;
		*ph = pnl->vh;
	}
	else
	{
		*ox = pnl->x1;
		*oy = pnl->y1;
		*pw = pnl->x2 - pnl->x1 + 1;
		*ph = pnl->y2 - pnl->y1 + 1;
	}
}

// Get the border/frame buffer (overlay buffer or main frame), bypassing vbuf.
// Used for operations that modify the border/decoration area (TITLE, HLINE).
static inline void MIPS16 panel_border_buf(FramePanel *pnl, uint16_t **buf, int *stride)
{
	if (pnl->buffer)
	{
		*buf = pnl->buffer;
		*stride = pnl->buf_stride;
	}
	else
	{
		*buf = frame;
		*stride = framex;
	}
}

// Flush the visible viewport of a vbuf panel into the main frame or overlay buffer.
static void MIPS16 panel_flush_vbuf(FramePanel *pnl)
{
	if (!pnl->vbuf)
		return;
	int pw = pnl->x2 - pnl->x1 + 1; // visible width
	int ph = pnl->y2 - pnl->y1 + 1; // visible height
	// Determine destination buffer (main frame or overlay)
	uint16_t *dst;
	int dstride;
	if (pnl->buffer)
	{
		dst = pnl->buffer;
		dstride = pnl->buf_stride;
	}
	else
	{
		dst = frame;
		dstride = framex;
	}
	for (int vy = 0; vy < ph; vy++)
	{
		int src_y = pnl->sy + vy;
		int dst_y = pnl->y1 + vy;
		for (int vx = 0; vx < pw; vx++)
		{
			int src_x = pnl->sx + vx;
			uint16_t cell;
			if (src_x >= 0 && src_x < pnl->vw && src_y >= 0 && src_y < pnl->vh)
				cell = pnl->vbuf[src_y * pnl->vw + src_x];
			else
				cell = ' ' | ((uint16_t)pnl->default_fc << 8) | ((uint16_t)pnl->default_bc << 12);
			dst[dst_y * dstride + (pnl->x1 + vx)] = cell;
		}
	}
}

// Lightweight cursor positioning â€” updates CurrentX/Y and sends VT100 position.
// Does NOT toggle the VGA or serial cursor visibility, so safe to use during rendering.
static void MIPS16 SMoveCursor(int x, int y)
{
	char s[30];
	CurrentX = x * gui_font_width;
	CurrentY = y * gui_font_height;
	sprintf(s, "\033[%d;%dH", y + 1, x + 1);
	SSPrintString(s);
}

// Map RGB121 4-bit colour index to ANSI foreground SGR code (30-37, 90-97).
// Background codes are foreground + 10 (40-47, 100-107).
static const uint8_t rgb121_to_ansi[16] = {
	30, // 0:  BLACK
	34, // 1:  BLUE
	32, // 2:  MYRTLE        -> green
	36, // 3:  COBALT        -> cyan
	32, // 4:  MIDGREEN      -> green
	94, // 5:  CERULEAN      -> bright blue
	92, // 6:  GREEN         -> bright green
	96, // 7:  CYAN          -> bright cyan
	31, // 8:  RED
	35, // 9:  MAGENTA
	33, // 10: RUST          -> yellow
	95, // 11: FUCHSIA       -> bright magenta
	93, // 12: BROWN         -> bright yellow
	95, // 13: LILAC         -> bright magenta
	93, // 14: YELLOW        -> bright yellow
	97	// 15: WHITE         -> bright white
};

static void MIPS16 SColour(int colour, int fore)
{
	char s[12];
	int code = rgb121_to_ansi[RGB121(colour) & 0xF];
	if (!fore)
		code += 10; // background offset: 40-47, 100-107
	if (fore)
		gui_fcolour = colour;
	else
		gui_bcolour = colour;
	sprintf(s, "\033[%dm", code);
	SSPrintString(s);
}

// Draw a box grid with cols x rows subdivisions and register each cell as a panel.
// x, y: top-left corner of outer box (char coords)
// w, h: outer box span â€” right border at x+w, bottom at y+h
// Interior per column = (w - cols) / cols chars (remainder spread across first columns)
// Interior per row    = (h - rows) / rows chars
static void MIPS16 draw_grid_box(int x, int y, int w, int h, int cols, int rows, int fc, bool dual)
{
	int *col_pos = (int *)GetMemory((cols + 2) * sizeof(int));
	int *row_pos = (int *)GetMemory((rows + 2) * sizeof(int));
	int base_cw = (w - cols) / cols;
	int col_rem = (w - cols) % cols;
	col_pos[0] = 0;
	for (int i = 0; i < cols; i++)
	{
		int cw = base_cw + (i < col_rem ? 1 : 0);
		col_pos[i + 1] = col_pos[i] + cw + 1;
	}
	int base_rh = (h - rows) / rows;
	int row_rem = (h - rows) % rows;
	row_pos[0] = 0;
	for (int i = 0; i < rows; i++)
	{
		int rh = base_rh + (i < row_rem ? 1 : 0);
		row_pos[i + 1] = row_pos[i] + rh + 1;
	}
	// Draw horizontal lines and junction characters
	for (int r = 0; r <= rows; r++)
	{
		int yy = y + row_pos[r];
		for (int c = 0; c <= cols; c++)
		{
			int xx = x + col_pos[c];
			int ch;
			if (r == 0 && c == 0)
				ch = dual ? c_d_topleft : c_topleft;
			else if (r == 0 && c == cols)
				ch = dual ? c_d_topright : c_topright;
			else if (r == rows && c == 0)
				ch = dual ? c_d_bottomleft : c_bottomleft;
			else if (r == rows && c == cols)
				ch = dual ? c_d_bottomright : c_bottomright;
			else if (r == 0)
				ch = dual ? c_d_tdown : c_tdown;
			else if (r == rows)
				ch = dual ? c_d_tup : c_tup;
			else if (c == 0)
				ch = dual ? c_d_tleft : c_tleft;
			else if (c == cols)
				ch = dual ? c_d_tright : c_tright;
			else
				ch = dual ? c_d_cross : c_cross;
			framewritechar(xx, yy, ch, fc, 0);
		}
		for (int c = 0; c < cols; c++)
		{
			for (int xx = x + col_pos[c] + 1; xx < x + col_pos[c + 1]; xx++)
			{
				framewritechar(xx, yy, dual ? c_d_horizontal : c_horizontal, fc, 0);
			}
		}
	}
	// Draw vertical lines between rows
	for (int r = 0; r < rows; r++)
	{
		for (int yy = y + row_pos[r] + 1; yy < y + row_pos[r + 1]; yy++)
		{
			for (int c = 0; c <= cols; c++)
			{
				framewritechar(x + col_pos[c], yy, dual ? c_d_vertical : c_vertical, fc, 0);
			}
		}
	}
	// Register panels (row-major order: left-to-right, top-to-bottom)
	ensure_panels(num_panels + cols * rows);
	for (int r = 0; r < rows; r++)
	{
		for (int c = 0; c < cols; c++)
		{
			framepanels[num_panels].x1 = x + col_pos[c] + 1;
			framepanels[num_panels].y1 = y + row_pos[r] + 1;
			framepanels[num_panels].x2 = x + col_pos[c + 1] - 1;
			framepanels[num_panels].y2 = y + row_pos[r + 1] - 1;
			framepanels[num_panels].cx = 0;
			framepanels[num_panels].cy = 0;
			framepanels[num_panels].active = true;
			framepanels[num_panels].default_fc = RGB121(gui_fcolour);
			framepanels[num_panels].default_bc = 0;
			num_panels++;
		}
	}
	FreeMemorySafe((void **)&col_pos);
	FreeMemorySafe((void **)&row_pos);
}

// Render frame buffer to screen with overlay compositing (differential update).
// Hides serial cursor only if cells change. Does NOT touch VGA cursor.
// Returns 1 if any cells were updated, 0 if not.
// Caller must ensure Option.DISPLAY_CONSOLE is set before calling if VGA output is needed.
static int MIPS16 frame_render(void)
{
	// Flush vbuf panels: copy the visible viewport into the main frame/overlay buffer
	for (int pi = 0; pi < num_panels; pi++)
	{
		if (framepanels[pi].active && framepanels[pi].vbuf)
			panel_flush_vbuf(&framepanels[pi]);
	}

	int savefcol = gui_fcolour;
	int lasty = -1, lastx = -1, lastc = -1, lastbc = -1;
	int changed = 0;
	for (int y = 0; y < framey; y++)
	{
		for (int x = 0; x < framex; x++)
		{
			// Start with main frame content
			uint16_t c = framegetchar(x, y);
			// Composite visible overlays (highest z-order wins)
			int best_z = -1;
			for (int oi = 0; oi < num_overlays; oi++)
			{
				FrameOverlay *ov = &frame_overlays[oi];
				if (!ov->visible)
					continue;
				if (x >= ov->ox && x < ov->ox + ov->width &&
					y >= ov->oy && y < ov->oy + ov->height)
				{
					if (ov->zorder > best_z)
					{
						best_z = ov->zorder;
						int lx = x - ov->ox, ly = y - ov->oy;
						c = ov->buffer[ly * ov->width + lx];
					}
				}
			}
			if (c != outframe[(y * framex) + x])
			{
				if (!changed)
				{
					// First change: hide serial cursor during render
					SSPrintString("\033[?25l");
					changed = 1;
				}
				outframe[(y * framex) + x] = c;
				if (y != lasty || x != lastx)
				{
					SMoveCursor(x, y);
					lastx = x + 1;
					lasty = y;
				}
				else
					lastx = x + 1;
				int outc = colours[(c >> 8) & 0xF];
				if (outc != lastc)
				{
					lastc = outc;
					SColour(outc, 1);
				}
				int outbc = colours[(c >> 12) & 0xF];
				if (outbc != lastbc)
				{
					lastbc = outbc;
					SColour(outbc, 0);
				}
				{
					int ch = c & 0xFF;
					if (ch == 0)
						ch = ' ';
					DisplayPutC(ch);
					SerialConsolePutC(ch, 0);
				}
			}
		}
	}
	if (changed)
	{
		gui_fcolour = savefcol;
		SColour(gui_fcolour, 1);
		SColour(gui_bcolour, 0);
	}
	return changed;
}

void MIPS16 cmd_frame(void)
{
	unsigned char *p = NULL;
	if ((p = checkstring(cmdline, (unsigned char *)"CREATE")))
	{
		if (frame)
			error("Frame already exists");
		framex = HRes / gui_font_width;
		framey = VRes / gui_font_height;
		frame = (uint16_t *)GetMemory(framex * framey * sizeof(uint16_t));
		outframe = (uint16_t *)GetMemory(framex * framey * sizeof(uint16_t));
		for (int i = 0; i < framex * framey; i++)
			outframe[i] = 0xFFFF;
		num_panels = 0;
		max_panels = 0;
		FreeMemorySafe((void **)&framepanels);
		Option.Width = framex;
		Option.Height = framey;
		char sp[20] = {0};
		strcpy(sp, "\033[8;");
		IntToStr(&sp[strlen(sp)], framey, 10);
		strcat(sp, ";");
		IntToStr(&sp[strlen(sp)], framex + 1, 10);
		strcat(sp, "t");
		SSPrintString(sp);
		SSPrintString("\0337\033[2J\033[H");
		ClearScreen(gui_bcolour);
		return;
	}
	if (!frame)
		error("Frame not created");
	if ((p = checkstring(cmdline, (unsigned char *)"CURSOR")))
	{
		// FRAME CURSOR ON | OFF | panel_id, x, y
		if (checkstring(p, (unsigned char *)"ON"))
		{
			framecursor_on = true;
		}
		else if (checkstring(p, (unsigned char *)"OFF"))
		{
			framecursor_on = false;
			SSPrintString("\033[?25l"); // hide serial cursor
			if (Option.DISPLAY_TYPE && lcd_cursor_active)
			{
				unsigned char sc = Option.DISPLAY_CONSOLE;
				Option.DISPLAY_CONSOLE = 1;
				frame_restore_lcd_cursor();
				SColour(gui_fcolour, 1);
				SColour(gui_bcolour, 0);
				Option.DISPLAY_CONSOLE = sc;
			}
		}
		else
		{
			// FRAME CURSOR panel_id, x, y â€” set the logical cursor position in a panel
			getcsargs(&p, 5);
			volatile int nargs = argc;
			if (nargs < 5)
				SyntaxError();
			int panel_id = getint(argv[0], 1, num_panels);
			if (!framepanels[panel_id - 1].active)
				error("Panel not active");
			FramePanel *pnl = &framepanels[panel_id - 1];
			int pw = pnl->vbuf ? pnl->vw : (pnl->x2 - pnl->x1 + 1);
			int ph = pnl->vbuf ? pnl->vh : (pnl->y2 - pnl->y1 + 1);
			int cx = getint(argv[2], 0, pw - 1);
			int cy = getint(argv[4], 0, ph - 1);
			pnl->cx = cx;
			pnl->cy = cy;
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"BOX")))
	{
		// FRAME BOX x, y, w, h [, cols, rows [, fc [, DOUBLE]]]
		// Draws a grid box with cols x rows panels and registers each panel.
		// cols and rows default to 1 (simple box).
		// Panel IDs are assigned sequentially starting from the next available ID.
		int fc = gui_fcolour;
		int cols = 1, rows = 1;
		bool dual = false;
		getcsargs(&p, 17);
		if (argc < 7)
			SyntaxError();
		int x = getint(argv[0], 0, framex - 1);
		int y = getint(argv[2], 0, framey - 1);
		int w = getint(argv[4], 2, framex - 1 - x);
		int h = getint(argv[6], 2, framey - 1 - y);
		if (argc >= 9 && *argv[8])
			cols = getint(argv[8], 1, framex);
		if (argc >= 11 && *argv[10])
			rows = getint(argv[10], 1, framey);
		if (argc >= 13 && *argv[12])
			fc = getint(argv[12], 0, WHITE);
		if (argc >= 15 && *argv[14])
		{
			if (checkstring(argv[14], (unsigned char *)"DOUBLE"))
				dual = true;
		}
		if (w < 2 * cols)
			error("Box too narrow");
		if (h < 2 * rows)
			error("Box too short");

		fc = RGB121(fc);
		draw_grid_box(x, y, w, h, cols, rows, fc, dual);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"TITLE")))
	{
		// FRAME TITLE panel_id, text$ [, colour]
		// Centres text in the top border of the panel or overlay.
		getcsargs(&p, 5);
		if (argc < 3)
			SyntaxError();
		int panel_id = getint(argv[0], 1, num_panels);
		if (!framepanels[panel_id - 1].active)
			error("Panel not active");
		FramePanel *pnl = &framepanels[panel_id - 1];
		unsigned char *text = getCstring(argv[2]);
		int tfc;
		if (argc >= 5 && *argv[4])
			tfc = RGB121(getint(argv[4], 0, WHITE));
		else
			tfc = pnl->default_fc;
		int tbc = pnl->default_bc;
		int tlen = strlen((char *)text);
		int pw = pnl->x2 - pnl->x1 + 1;
		if (tlen > pw - 2)
			tlen = pw - 2;
		int start = (pw - tlen) / 2;
		uint16_t *tbuf;
		int tstride;
		panel_border_buf(pnl, &tbuf, &tstride);
		int border_y = pnl->y1 - 1;
		// Detect border style from top-left corner
		uint16_t corner = tbuf[border_y * tstride + (pnl->x1 - 1)];
		bool is_double = ((corner & 0xFF) == c_d_topleft);
		// Write bracket-text-bracket into the border row
		int bx = pnl->x1 + start - 1;
		if (bx >= pnl->x1)
			framewritechar_to(tbuf, tstride, bx, border_y,
							  is_double ? c_ds_tright : c_tright, tfc, tbc);
		for (int i = 0; i < tlen; i++)
			framewritechar_to(tbuf, tstride, pnl->x1 + start + i, border_y,
							  text[i], tfc, tbc);
		bx = pnl->x1 + start + tlen;
		if (bx <= pnl->x2)
			framewritechar_to(tbuf, tstride, bx, border_y,
							  is_double ? c_ds_tleft : c_tleft, tfc, tbc);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"HLINE")))
	{
		// FRAME HLINE panel_id, row [, colour [, DOUBLE]]
		// Draws a horizontal divider across the panel at the given row,
		// with T-junction characters connecting to the left and right borders.
		int hfc = gui_fcolour;
		bool hdual = false;
		getcsargs(&p, 7);
		if (argc < 3)
			SyntaxError();
		int panel_id = getint(argv[0], 1, num_panels);
		if (!framepanels[panel_id - 1].active)
			error("Panel not active");
		FramePanel *pnl = &framepanels[panel_id - 1];
		int ph = pnl->y2 - pnl->y1 + 1;
		int row = getint(argv[2], 0, ph - 1);
		if (argc >= 5 && *argv[4])
			hfc = getint(argv[4], 0, WHITE);
		if (argc >= 7 && *argv[6])
		{
			if (checkstring(argv[6], (unsigned char *)"DOUBLE"))
				hdual = true;
		}
		hfc = RGB121(hfc);
		uint16_t *hbuf;
		int hstride;
		panel_border_buf(pnl, &hbuf, &hstride);
		int hy = pnl->y1 + row;
		int hbc = pnl->default_bc;
		// Detect border style from top-left corner
		uint16_t corner = hbuf[(pnl->y1 - 1) * hstride + (pnl->x1 - 1)];
		bool border_double = ((corner & 0xFF) == c_d_topleft);
		// Left junction
		int lch, rch;
		if (border_double && hdual)
		{
			lch = c_d_tleft;
			rch = c_d_tright;
		}
		else if (border_double && !hdual)
		{
			lch = c_sd_tleft;
			rch = c_sd_tright;
		}
		else if (!border_double && hdual)
		{
			lch = c_ds_tleft;
			rch = c_ds_tright;
		}
		else
		{
			lch = c_tleft;
			rch = c_tright;
		}
		framewritechar_to(hbuf, hstride, pnl->x1 - 1, hy, lch, hfc, hbc);
		framewritechar_to(hbuf, hstride, pnl->x2 + 1, hy, rch, hfc, hbc);
		for (int hx = pnl->x1; hx <= pnl->x2; hx++)
			framewritechar_to(hbuf, hstride, hx, hy, hdual ? c_d_horizontal : c_horizontal, hfc, hbc);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"PRINT")))
	{
		// FRAME PRINT panel_id, text$ [, fc [, WRAP]]
		// Writes text into the specified panel, advancing the panel's cursor.
		// Without WRAP: text that exceeds the panel width is clipped (discarded).
		// With WRAP:    text wraps to the next line within the panel.
		// Newline characters (\n) move to the start of the next line within the panel.
		bool wrap = false;
		getcsargs(&p, 7);
		if (argc < 3)
			SyntaxError();
		int panel_id = getint(argv[0], 1, num_panels);
		if (!framepanels[panel_id - 1].active)
			error("Panel not active");
		frame_last_panel = panel_id; // track for FRAME CURSOR ON
		FramePanel *pnl = &framepanels[panel_id - 1];
		p = getCstring(argv[2]);
		int fc;
		if (argc >= 5 && *argv[4])
			fc = RGB121(getint(argv[4], 0, WHITE));
		else
			fc = pnl->default_fc;
		int bc = pnl->default_bc;
		if (argc >= 7 && *argv[6])
		{
			if (checkstring(argv[6], (unsigned char *)"WRAP"))
				wrap = true;
		}
		uint16_t *pbuf;
		int pstride;
		panel_buf(pnl, &pbuf, &pstride);
		int ox, oy, pw, ph;
		panel_write_geometry(pnl, &ox, &oy, &pw, &ph);
		uint16_t bgfill = ' ' | ((uint16_t)fc << 8) | ((uint16_t)bc << 12);
		while (*p)
		{
			if (pnl->cy >= ph)
			{
				if (wrap)
				{
					// Auto-scroll: shift panel contents up one line
					for (int sy = 1; sy < ph; sy++)
					{
						memcpy((uint8_t *)&pbuf[(oy + sy - 1) * pstride + ox],
							   (uint8_t *)&pbuf[(oy + sy) * pstride + ox],
							   pw * sizeof(uint16_t));
					}
					// Clear the bottom line with background colour
					for (int fx = 0; fx < pw; fx++)
						pbuf[(oy + ph - 1) * pstride + ox + fx] = bgfill;
					pnl->cy = ph - 1;
					pnl->cx = 0;
				}
				else
					break;
			}
			if (*p == '\n')
			{
				pnl->cx = 0;
				pnl->cy++;
				p++;
				continue;
			}
			if (*p == '\r')
			{
				pnl->cx = 0;
				p++;
				continue;
			}
			if (pnl->cx >= pw)
			{
				if (wrap)
				{
					pnl->cx = 0;
					pnl->cy++;
					if (pnl->cy >= ph)
					{
						// Auto-scroll: shift panel contents up one line
						for (int sy = 1; sy < ph; sy++)
						{
							memcpy((uint8_t *)&pbuf[(oy + sy - 1) * pstride + ox],
								   (uint8_t *)&pbuf[(oy + sy) * pstride + ox],
								   pw * sizeof(uint16_t));
						}
						for (int fx = 0; fx < pw; fx++)
							pbuf[(oy + ph - 1) * pstride + ox + fx] = bgfill;
						pnl->cy = ph - 1;
					}
				}
				else
				{
					p++;
					continue;
				}
			}
			framewritechar_to(pbuf, pstride, ox + pnl->cx, oy + pnl->cy, *p, fc, bc);
			pnl->cx++;
			p++;
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"INPUT")))
	{
		// FRAME INPUT panel_id, variable [, prompt$ [, fc]]
		// Panel-aware line input. Displays optional prompt in the panel,
		// reads characters from the console echoing them into the panel buffer.
		// Handles backspace and wrapping. Enter terminates input.
		// Result is stored in the specified variable (string, integer, or float).
		// Works with both main-frame panels and visible overlays.
		getcsargs(&p, 7);
		if (argc < 3)
			SyntaxError();
		int panel_id = getint(argv[0], 1, num_panels);
		if (!framepanels[panel_id - 1].active)
			error("Panel not active");
		FramePanel *pnl = &framepanels[panel_id - 1];

		int fc = pnl->default_fc;
		int bc = pnl->default_bc;

		// Evaluate optional prompt before findvar
		unsigned char *prompt = NULL;
		if (argc >= 5 && *argv[4])
			prompt = getCstring(argv[4]);
		if (argc >= 7 && *argv[6])
			fc = RGB121(getint(argv[6], 0, WHITE));

		// Find the target variable
		unsigned char *vp = findvar(argv[2], V_FIND);
		if (g_vartbl[g_VarIndex].type & T_CONST)
			error("Cannot change a constant");
		int var_type = g_vartbl[g_VarIndex].type;
		int var_size = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
		if (g_StructMemberType != 0)
		{
			var_type = g_StructMemberType;
			if (g_StructMemberType & T_STR)
				var_size = g_StructMemberSize;
		}
#endif

		// Get panel buffer info
		uint16_t *pbuf;
		int pstride;
		panel_buf(pnl, &pbuf, &pstride);
		int ox, oy, pw, ph;
		panel_write_geometry(pnl, &ox, &oy, &pw, &ph);
		uint16_t bgfill = ' ' | ((uint16_t)fc << 8) | ((uint16_t)bc << 12);

		// Write prompt into panel if given
		if (prompt)
		{
			while (*prompt)
			{
				if (*prompt == '\n')
				{
					pnl->cx = 0;
					pnl->cy++;
					prompt++;
					continue;
				}
				if (*prompt == '\r')
				{
					pnl->cx = 0;
					prompt++;
					continue;
				}
				if (pnl->cx >= pw)
				{
					pnl->cx = 0;
					pnl->cy++;
				}
				if (pnl->cy >= ph)
				{
					// Auto-scroll for prompt
					for (int sy = 1; sy < ph; sy++)
						memcpy(&pbuf[(oy + sy - 1) * pstride + ox],
							   &pbuf[(oy + sy) * pstride + ox],
							   pw * sizeof(uint16_t));
					for (int fx = 0; fx < pw; fx++)
						pbuf[(oy + ph - 1) * pstride + ox + fx] = bgfill;
					pnl->cy = ph - 1;
					pnl->cx = 0;
				}
				framewritechar_to(pbuf, pstride, ox + pnl->cx, oy + pnl->cy, *prompt, fc, bc);
				pnl->cx++;
				prompt++;
			}
		}

		// Determine overlay (if any) for cursor positioning
		FrameOverlay *input_ov = find_overlay_by_panel(panel_id);

		// Input loop
		unsigned char ibuf[STRINGSIZE];
		int nchars = 0;

		unsigned char save_console = Option.DISPLAY_CONSOLE;
		if (Option.DISPLAY_TYPE)
			Option.DISPLAY_CONSOLE = 1;

		while (1)
		{
			// Restore old LCD cursor before render so outframe comparison is clean
			if (lcd_cursor_active && Option.DISPLAY_TYPE)
				frame_restore_lcd_cursor();

			// Render frame to screen
			int changed = frame_render();

			// Position cursor at current input position in the panel
			int screen_x = pnl->x1 + pnl->cx - (pnl->vbuf ? pnl->sx : 0);
			int screen_y = pnl->y1 + pnl->cy - (pnl->vbuf ? pnl->sy : 0);
			if (input_ov && input_ov->visible)
			{
				screen_x += input_ov->ox;
				screen_y += input_ov->oy;
			}
			SMoveCursor(screen_x, screen_y);
			if (changed)
				SSPrintString("\033[?25h");

			// Draw LCD inverted cursor at input position and start blink cycle
			if (framecursor_on)
			{
				if (Option.DISPLAY_TYPE)
					frame_draw_lcd_cursor(screen_x, screen_y);
				lcd_cursor_blink_on = true;
				lcd_cursor_blink_time = time_us_64();
			}

			// Wait for a key, animating cursor blink
			int c;
			do
			{
				CheckAbort();
				frame_blink_cursor(screen_x, screen_y);
				c = MMInkey();
			} while (c == -1);

			// Restore cursor to normal before processing key
			if (lcd_cursor_active && Option.DISPLAY_TYPE)
				frame_restore_lcd_cursor();
			if (framecursor_on)
				SSPrintString("\033[?25l");

			if (c == '\r' || c == '\n')
				break;

			if (c == '\b' || c == 127)
			{
				// Backspace
				if (nchars > 0)
				{
					nchars--;
					// Move cursor back
					if (pnl->cx > 0)
					{
						pnl->cx--;
					}
					else if (pnl->cy > 0)
					{
						pnl->cy--;
						pnl->cx = pw - 1;
					}
					// Clear the character at cursor position
					framewritechar_to(pbuf, pstride, ox + pnl->cx, oy + pnl->cy, ' ', fc, bc);
				}
				continue;
			}

			if (c < ' ' || c > 126)
				continue; // ignore non-printable / escape sequences

			if (nchars >= MAXSTRLEN)
				continue; // buffer full

			// Auto-scroll if cursor is past the panel bottom
			if (pnl->cy >= ph)
			{
				for (int sy = 1; sy < ph; sy++)
					memcpy(&pbuf[(oy + sy - 1) * pstride + ox],
						   &pbuf[(oy + sy) * pstride + ox],
						   pw * sizeof(uint16_t));
				for (int fx = 0; fx < pw; fx++)
					pbuf[(oy + ph - 1) * pstride + ox + fx] = bgfill;
				pnl->cy = ph - 1;
				pnl->cx = 0;
			}

			// Store character in buffer
			ibuf[nchars++] = c;

			// Write character to panel at cursor position
			framewritechar_to(pbuf, pstride, ox + pnl->cx, oy + pnl->cy, c, fc, bc);
			pnl->cx++;
			if (pnl->cx >= pw)
			{
				pnl->cx = 0;
				pnl->cy++;
			}
		}
		ibuf[nchars] = 0;

		// Advance cursor to next line after input
		pnl->cx = 0;
		pnl->cy++;

		// Restore LCD cursor before final render
		if (lcd_cursor_active && Option.DISPLAY_TYPE)
			frame_restore_lcd_cursor();

		// Final render
		int fchanged = frame_render();
		int screen_x = pnl->x1 + pnl->cx - (pnl->vbuf ? pnl->sx : 0);
		int screen_y = pnl->y1 + pnl->cy - (pnl->vbuf ? pnl->sy : 0);
		if (input_ov && input_ov->visible)
		{
			screen_x += input_ov->ox;
			screen_y += input_ov->oy;
		}
		SMoveCursor(screen_x, screen_y);
		if (fchanged)
			SSPrintString("\033[?25h");

		// Draw LCD cursor at final position and start blink cycle
		if (framecursor_on)
		{
			if (Option.DISPLAY_TYPE)
				frame_draw_lcd_cursor(screen_x, screen_y);
			lcd_cursor_blink_on = true;
			lcd_cursor_blink_time = time_us_64();
		}

		Option.DISPLAY_CONSOLE = save_console;

		// Assign to variable
		if (var_type & T_STR)
		{
			if (nchars > var_size)
				error("String too long");
			strcpy((char *)vp, (char *)ibuf);
			CtoM(vp);
		}
		else if (var_type & T_INT)
		{
			*((long long int *)vp) = strtoll((char *)ibuf, NULL, 10);
		}
		else
		{
			*((MMFLOAT *)vp) = (MMFLOAT)atof((char *)ibuf);
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"CLS")))
	{
		// FRAME CLS [panel_id]
		// Without panel_id: clears entire frame buffer, resets all panel cursors.
		// With panel_id:    clears only that panel's interior and resets its cursor.
		//                   If the panel has a vbuf, clears the entire virtual buffer.
		if (*p)
		{
			int panel_id = getint(p, 1, num_panels);
			if (!framepanels[panel_id - 1].active)
				error("Panel not active");
			FramePanel *pnl = &framepanels[panel_id - 1];
			uint16_t fill = ' ' | ((uint16_t)pnl->default_fc << 8) | ((uint16_t)pnl->default_bc << 12);
			if (pnl->vbuf)
			{
				// Clear entire virtual buffer
				for (int i = 0; i < pnl->vw * pnl->vh; i++)
					pnl->vbuf[i] = fill;
			}
			else
			{
				uint16_t *pbuf;
				int pstride;
				panel_buf(pnl, &pbuf, &pstride);
				for (int y = pnl->y1; y <= pnl->y2; y++)
				{
					for (int x = pnl->x1; x <= pnl->x2; x++)
					{
						pbuf[(y * pstride) + x] = fill;
					}
				}
			}
			pnl->cx = 0;
			pnl->cy = 0;
		}
		else
		{
			memset(frame, 0, framex * framey * sizeof(uint16_t));
			for (int i = 0; i < num_panels; i++)
			{
				framepanels[i].cx = 0;
				framepanels[i].cy = 0;
				// Also clear any vbufs
				if (framepanels[i].vbuf)
				{
					uint16_t fill = ' ' | ((uint16_t)framepanels[i].default_fc << 8) | ((uint16_t)framepanels[i].default_bc << 12);
					for (int j = 0; j < framepanels[i].vw * framepanels[i].vh; j++)
						framepanels[i].vbuf[j] = fill;
				}
			}
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"COLOUR")))
	{
		// FRAME COLOUR panel_id, fg [, bg]
		// Sets default foreground and optional background colour for a panel.
		getcsargs(&p, 5);
		if (argc < 3)
			SyntaxError();
		int panel_id = getint(argv[0], 1, num_panels);
		if (!framepanels[panel_id - 1].active)
			error("Panel not active");
		FramePanel *pnl = &framepanels[panel_id - 1];
		pnl->default_fc = RGB121(getint(argv[2], 0, WHITE));
		if (argc >= 5 && *argv[4])
			pnl->default_bc = RGB121(getint(argv[4], 0, WHITE));
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"CLEAR")))
	{
		memset(frame, 0, framex * framey * sizeof(uint16_t));
		free_overlays();
		// Free any vbufs
		for (int i = 0; i < num_panels; i++)
		{
			if (framepanels[i].vbuf)
				FreeMemorySafe((void **)&framepanels[i].vbuf);
		}
		FreeMemorySafe((void **)&framepanels);
		num_panels = 0;
		max_panels = 0;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
	{
		if (!frame)
			error("Frame does not exist");
		closeframe();
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
	{
		// Render changed cells to screen.
		unsigned char save_console = Option.DISPLAY_CONSOLE;
		if (Option.DISPLAY_TYPE)
			Option.DISPLAY_CONSOLE = 1;
		int sx = CurrentX / gui_font_width, sy = CurrentY / gui_font_height;
		// Restore old LCD cursor cell before render so outframe comparison is clean
		if (lcd_cursor_active && Option.DISPLAY_TYPE)
		{
			frame_restore_lcd_cursor();
			// Also fix outframe so frame_render sees a clean cell
			// (frame_draw_cell_at updated the LCD but outframe still has the true value)
		}
		int changed = frame_render();
		int cursor_x = -1, cursor_y = -1; // screen coords for cursor
		if (framecursor_on && frame_last_panel > 0 && frame_last_panel <= num_panels)
		{
			// Position serial cursor at the last-updated panel's logical cursor
			FramePanel *pnl = &framepanels[frame_last_panel - 1];
			if (pnl->active)
			{
				int cx_abs = pnl->x1 + pnl->cx - (pnl->vbuf ? pnl->sx : 0);
				int cy_abs = pnl->y1 + pnl->cy - (pnl->vbuf ? pnl->sy : 0);
				// If this panel belongs to a visible overlay, offset by overlay screen position
				FrameOverlay *ov = find_overlay_by_panel(frame_last_panel);
				if (ov && ov->visible)
				{
					cx_abs = ov->ox + pnl->x1 + pnl->cx - (pnl->vbuf ? pnl->sx : 0);
					cy_abs = ov->oy + pnl->y1 + pnl->cy - (pnl->vbuf ? pnl->sy : 0);
					// Check if a higher-z overlay obscures the cursor position
					bool obscured = false;
					for (int oi = 0; oi < num_overlays; oi++)
					{
						FrameOverlay *other = &frame_overlays[oi];
						if (other == ov || !other->visible || other->zorder <= ov->zorder)
							continue;
						if (cx_abs >= other->ox && cx_abs < other->ox + other->width &&
							cy_abs >= other->oy && cy_abs < other->oy + other->height)
						{
							obscured = true;
							break;
						}
					}
					if (!obscured)
					{
						SMoveCursor(cx_abs, cy_abs);
						SSPrintString("\033[?25h"); // show serial cursor
						cursor_x = cx_abs;
						cursor_y = cy_abs;
					}
					else
						SSPrintString("\033[?25l"); // hide cursor when obscured
				}
				else if (!ov)
				{
					// Regular panel (not an overlay)
					SMoveCursor(cx_abs, cy_abs);
					SSPrintString("\033[?25h");
					cursor_x = cx_abs;
					cursor_y = cy_abs;
				}
			}
			else if (changed)
				SMoveCursor(sx, sy);
		}
		else if (changed)
		{
			SMoveCursor(sx, sy);
			if (framecursor_on)
				SSPrintString("\033[?25h");
		}
		// Draw LCD inverted cursor at the computed position and start blink cycle
		if (framecursor_on && cursor_x >= 0)
		{
			if (Option.DISPLAY_TYPE)
				frame_draw_lcd_cursor(cursor_x, cursor_y);
			lcd_cursor_blink_on = true;
			lcd_cursor_blink_time = time_us_64();
		}
		// Restore colours after render
		SColour(gui_fcolour, 1);
		SColour(gui_bcolour, 0);
		Option.DISPLAY_CONSOLE = save_console;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"PANEL")))
	{
		// FRAME PANEL id, x, y, w, h
		// Manually define a panel at arbitrary frame buffer coordinates.
		getcsargs(&p, 9);
		if (argc < 9)
			SyntaxError();
		int id = getint(argv[0], 1, framex * framey);
		int x = getint(argv[2], 0, framex - 1);
		int y = getint(argv[4], 0, framey - 1);
		int w = getint(argv[6], 1, framex - x);
		int h = getint(argv[8], 1, framey - y);
		ensure_panels(id);
		// Free any existing vbuf on this panel
		if (framepanels[id - 1].vbuf)
			FreeMemorySafe((void **)&framepanels[id - 1].vbuf);
		framepanels[id - 1].x1 = x;
		framepanels[id - 1].y1 = y;
		framepanels[id - 1].x2 = x + w - 1;
		framepanels[id - 1].y2 = y + h - 1;
		framepanels[id - 1].cx = 0;
		framepanels[id - 1].cy = 0;
		framepanels[id - 1].active = true;
		framepanels[id - 1].buffer = NULL;
		framepanels[id - 1].buf_stride = 0;
		framepanels[id - 1].vw = 0;
		framepanels[id - 1].vh = 0;
		framepanels[id - 1].sx = 0;
		framepanels[id - 1].sy = 0;
		framepanels[id - 1].default_fc = RGB121(gui_fcolour);
		framepanels[id - 1].default_bc = 0;
		if (id > num_panels)
			num_panels = id;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"OVERLAY")))
	{
		// FRAME OVERLAY panel_id, width, height [, colour [, DOUBLE]]
		// Creates an overlay with its own buffer and a border, registered as the given panel ID.
		// Width and height include the border. Panel interior is (w-2) x (h-2).
		int fc = gui_fcolour;
		bool dual = false;
		getcsargs(&p, 9);
		if (argc < 5)
			SyntaxError();
		int id = getint(argv[0], 1, framex * framey);
		int w = getint(argv[2], 3, framex);
		int h = getint(argv[4], 3, framey);
		if (argc >= 7 && *argv[6])
			fc = getint(argv[6], 0, WHITE);
		if (argc >= 9 && *argv[8])
		{
			if (checkstring(argv[8], (unsigned char *)"DOUBLE"))
				dual = true;
		}
		fc = RGB121(fc);
		// Check panel ID not already in use
		ensure_panels(id);
		if (framepanels[id - 1].active)
			error("Panel ID already in use");
		// Check overlay doesn't already exist for this ID
		if (find_overlay_by_panel(id))
			error("Overlay already exists");
		// Grow overlay array if needed
		if (num_overlays >= max_overlays)
		{
			int newmax = max_overlays + 8;
			FrameOverlay *nov = (FrameOverlay *)GetMemory(newmax * sizeof(FrameOverlay));
			if (frame_overlays)
			{
				memcpy(nov, frame_overlays, num_overlays * sizeof(FrameOverlay));
				FreeMemorySafe((void **)&frame_overlays);
			}
			memset(&nov[num_overlays], 0, (newmax - num_overlays) * sizeof(FrameOverlay));
			frame_overlays = nov;
			max_overlays = newmax;
		}
		// Allocate overlay buffer
		FrameOverlay *ov = &frame_overlays[num_overlays];
		ov->buffer = (uint16_t *)GetMemory(w * h * sizeof(uint16_t));
		memset(ov->buffer, 0, w * h * sizeof(uint16_t));
		ov->width = w;
		ov->height = h;
		ov->panel_id = id;
		ov->ox = 0;
		ov->oy = 0;
		ov->visible = false;
		ov->zorder = 0;
		num_overlays++;
		// Draw border into the overlay buffer
		// Top-left corner
		framewritechar_to(ov->buffer, w, 0, 0, dual ? c_d_topleft : c_topleft, fc, 0);
		// Top-right corner
		framewritechar_to(ov->buffer, w, w - 1, 0, dual ? c_d_topright : c_topright, fc, 0);
		// Bottom-left corner
		framewritechar_to(ov->buffer, w, 0, h - 1, dual ? c_d_bottomleft : c_bottomleft, fc, 0);
		// Bottom-right corner
		framewritechar_to(ov->buffer, w, w - 1, h - 1, dual ? c_d_bottomright : c_bottomright, fc, 0);
		// Top and bottom horizontal lines
		for (int bx = 1; bx < w - 1; bx++)
		{
			framewritechar_to(ov->buffer, w, bx, 0, dual ? c_d_horizontal : c_horizontal, fc, 0);
			framewritechar_to(ov->buffer, w, bx, h - 1, dual ? c_d_horizontal : c_horizontal, fc, 0);
		}
		// Left and right vertical lines
		for (int by = 1; by < h - 1; by++)
		{
			framewritechar_to(ov->buffer, w, 0, by, dual ? c_d_vertical : c_vertical, fc, 0);
			framewritechar_to(ov->buffer, w, w - 1, by, dual ? c_d_vertical : c_vertical, fc, 0);
		}
		// Register as a panel â€” interior is inside the border
		framepanels[id - 1].x1 = 1;
		framepanels[id - 1].y1 = 1;
		framepanels[id - 1].x2 = w - 2;
		framepanels[id - 1].y2 = h - 2;
		framepanels[id - 1].cx = 0;
		framepanels[id - 1].cy = 0;
		framepanels[id - 1].active = true;
		framepanels[id - 1].buffer = ov->buffer;
		framepanels[id - 1].buf_stride = w;
		framepanels[id - 1].default_fc = RGB121(gui_fcolour);
		framepanels[id - 1].default_bc = 0;
		// Fill interior with opaque spaces
		for (int fy = 1; fy < h - 1; fy++)
			for (int fx = 1; fx < w - 1; fx++)
				ov->buffer[fy * w + fx] = ' ';
		if (id > num_panels)
			num_panels = id;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SHOW")))
	{
		// FRAME SHOW panel_id, x, y
		// Shows an overlay at position (x, y) on the main frame.
		getcsargs(&p, 5);
		if (argc < 5)
			SyntaxError();
		int id = getint(argv[0], 1, num_panels);
		int x = getint(argv[2], 0, framex - 1);
		int y = getint(argv[4], 0, framey - 1);
		FrameOverlay *ov = find_overlay_by_panel(id);
		if (!ov)
			error("Not an overlay panel");
		ov->ox = x;
		ov->oy = y;
		ov->visible = true;
		ov->zorder = ++overlay_zcounter;
		frame_last_panel = id; // bring cursor focus to shown overlay
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"HIDE")))
	{
		// FRAME HIDE panel_id
		// Hides an overlay.
		int id = getint(p, 1, num_panels);
		FrameOverlay *ov = find_overlay_by_panel(id);
		if (!ov)
			error("Not an overlay panel");
		ov->visible = false;
		if (frame_last_panel == id)
		{
			frame_last_panel = 0; // cursor was on this overlay; clear it
			if (Option.DISPLAY_TYPE && lcd_cursor_active)
			{
				unsigned char sc = Option.DISPLAY_CONSOLE;
				Option.DISPLAY_CONSOLE = 1;
				frame_restore_lcd_cursor();
				SColour(gui_fcolour, 1);
				SColour(gui_bcolour, 0);
				Option.DISPLAY_CONSOLE = sc;
			}
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"VBUF")))
	{
		// FRAME VBUF panel_id, vwidth, vheight
		// Allocate a virtual buffer larger than the visible panel area.
		// Content is written into the vbuf; a viewport is flushed to the
		// frame/overlay buffer before rendering.
		getcsargs(&p, 5);
		if (argc < 5)
			SyntaxError();
		int id = getint(argv[0], 1, num_panels);
		if (id > num_panels || !framepanels[id - 1].active)
			error("Panel not active");
		int vw = getint(argv[2], 1, 10000);
		int vh = getint(argv[4], 1, 10000);
		FramePanel *pnl = &framepanels[id - 1];
		// Free any previous vbuf
		if (pnl->vbuf)
			FreeMemorySafe((void **)&pnl->vbuf);
		pnl->vbuf = (uint16_t *)GetMemory(vw * vh * sizeof(uint16_t));
		pnl->vw = vw;
		pnl->vh = vh;
		pnl->sx = 0;
		pnl->sy = 0;
		// Fill with spaces using panel default colours
		uint16_t blank = ' ' | ((uint16_t)pnl->default_fc << 8) | ((uint16_t)pnl->default_bc << 12);
		for (int i = 0; i < vw * vh; i++)
			pnl->vbuf[i] = blank;
		// Reset cursor
		pnl->cx = 0;
		pnl->cy = 0;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SCROLL")))
	{
		// FRAME SCROLL panel_id, sx, sy
		// Set the viewport scroll offset for a vbuf panel.
		getcsargs(&p, 5);
		if (argc < 5)
			SyntaxError();
		int id = getint(argv[0], 1, num_panels);
		if (id > num_panels || !framepanels[id - 1].active)
			error("Panel not active");
		FramePanel *pnl = &framepanels[id - 1];
		if (!pnl->vbuf)
			error("Panel has no virtual buffer");
		int pw = pnl->x2 - pnl->x1 + 1;
		int ph = pnl->y2 - pnl->y1 + 1;
		int sx = getint(argv[2], 0, pnl->vw - pw);
		int sy = getint(argv[4], 0, pnl->vh - ph);
		pnl->sx = sx;
		pnl->sy = sy;
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"DESTROY")))
	{
		// FRAME DESTROY panel_id
		// Destroys an overlay and frees its resources.
		int id = getint(p, 1, num_panels);
		FrameOverlay *ov = find_overlay_by_panel(id);
		if (!ov)
			error("Not an overlay panel");
		// Free vbuf if present
		if (framepanels[id - 1].vbuf)
			FreeMemorySafe((void **)&framepanels[id - 1].vbuf);
		FreeMemorySafe((void **)&ov->buffer);
		framepanels[id - 1].active = false;
		framepanels[id - 1].buffer = NULL;
		// Remove from overlay array by shifting
		int idx = ov - frame_overlays;
		for (int i = idx; i < num_overlays - 1; i++)
			frame_overlays[i] = frame_overlays[i + 1];
		num_overlays--;
	}
	else
	{
		// Legacy: FRAME x, y, text$ [, fc [, bc]]
		int bc = 0, fc = gui_fcolour;
		getcsargs(&cmdline, 9);
		if (argc < 5)
			SyntaxError();
		int x = getint(argv[0], 0, framex - 1);
		int y = getint(argv[2], 0, framey - 1);
		p = getCstring(argv[4]);
		if (argc >= 7 && *argv[6])
			fc = getint(argv[6], 0, WHITE);
		if (argc == 9)
			bc = RGB121(getint(argv[8], 0, WHITE));
		int l = strlen((char *)p);
		fc = RGB121(fc);
		while (l--)
		{
			if (x == framex)
			{
				y++;
				x = 0;
			}
			if (y == framey)
				return;
			framewritechar(x++, y, *p++, fc, bc);
		}
	}
}

// ============================================================================
// FRAME() query function
// ============================================================================
void MIPS16 fun_frame(void)
{
	unsigned char *tp;

	// FRAME(WIDTH) - frame width in characters
	if (checkstring(ep, (unsigned char *)"WIDTH"))
	{
		iret = framex;
		targ = T_INT;
		return;
	}
	// FRAME(HEIGHT) - frame height in characters
	else if (checkstring(ep, (unsigned char *)"HEIGHT"))
	{
		iret = framey;
		targ = T_INT;
		return;
	}
	// FRAME(PANELS) - number of active panels
	else if (checkstring(ep, (unsigned char *)"PANELS"))
	{
		int count = 0;
		for (int i = 0; i < num_panels; i++)
			if (framepanels[i].active)
				count++;
		iret = count;
		targ = T_INT;
		return;
	}
	// FRAME(OVERLAYS) - number of overlays
	else if (checkstring(ep, (unsigned char *)"OVERLAYS"))
	{
		iret = num_overlays;
		targ = T_INT;
		return;
	}
	// FRAME(PW id) - panel interior width
	else if ((tp = checkstring(ep, (unsigned char *)"PW")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].x2 - framepanels[id - 1].x1 + 1;
		targ = T_INT;
		return;
	}
	// FRAME(PH id) - panel interior height
	else if ((tp = checkstring(ep, (unsigned char *)"PH")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].y2 - framepanels[id - 1].y1 + 1;
		targ = T_INT;
		return;
	}
	// FRAME(PX id) - panel cursor X position
	else if ((tp = checkstring(ep, (unsigned char *)"PX")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].cx;
		targ = T_INT;
		return;
	}
	// FRAME(PY id) - panel cursor Y position
	else if ((tp = checkstring(ep, (unsigned char *)"PY")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].cy;
		targ = T_INT;
		return;
	}
	// FRAME(FC id) - panel default foreground colour
	else if ((tp = checkstring(ep, (unsigned char *)"FC")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].default_fc;
		targ = T_INT;
		return;
	}
	// FRAME(BC id) - panel default background colour
	else if ((tp = checkstring(ep, (unsigned char *)"BC")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].default_bc;
		targ = T_INT;
		return;
	}
	// FRAME(ACTIVE id) - whether panel is active (1/0)
	else if ((tp = checkstring(ep, (unsigned char *)"ACTIVE")))
	{
		int id = (int)getint(tp, 1, num_panels);
		iret = framepanels[id - 1].active ? 1 : 0;
		targ = T_INT;
		return;
	}
	// FRAME(VISIBLE id) - whether overlay is visible (1/0)
	else if ((tp = checkstring(ep, (unsigned char *)"VISIBLE")))
	{
		int id = (int)getint(tp, 1, num_panels);
		FrameOverlay *ov = find_overlay_by_panel(id);
		if (!ov)
			error("Panel % is not an overlay", id);
		iret = ov->visible ? 1 : 0;
		targ = T_INT;
		return;
	}
	// FRAME(CELL x, y) - read cell from main frame (returns raw uint16_t)
	else if ((tp = checkstring(ep, (unsigned char *)"CELL")))
	{
		getcsargs(&tp, 3);
		int x = (int)getint(argv[0], 0, framex - 1);
		int y = (int)getint(argv[2], 0, framey - 1);
		iret = framegetchar(x, y);
		targ = T_INT;
		return;
	}
	// FRAME(PCELL id, x, y) - read cell from panel buffer
	// For vbuf panels, reads from the virtual buffer (full virtual coords).
	else if ((tp = checkstring(ep, (unsigned char *)"PCELL")))
	{
		getcsargs(&tp, 5);
		int id = (int)getint(argv[0], 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		FramePanel *pnl = &framepanels[id - 1];
		uint16_t *buf;
		int stride;
		panel_buf(pnl, &buf, &stride);
		int ox, oy, pw, ph;
		panel_write_geometry(pnl, &ox, &oy, &pw, &ph);
		int x = (int)getint(argv[2], 0, pw - 1);
		int y = (int)getint(argv[4], 0, ph - 1);
		iret = buf[(oy + y) * stride + (ox + x)];
		targ = T_INT;
		return;
	}
	// FRAME(VW id) - virtual buffer width (0 if no vbuf)
	else if ((tp = checkstring(ep, (unsigned char *)"VW")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].vbuf ? framepanels[id - 1].vw : 0;
		targ = T_INT;
		return;
	}
	// FRAME(VH id) - virtual buffer height (0 if no vbuf)
	else if ((tp = checkstring(ep, (unsigned char *)"VH")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].vbuf ? framepanels[id - 1].vh : 0;
		targ = T_INT;
		return;
	}
	// FRAME(SX id) - scroll X offset (0 if no vbuf)
	else if ((tp = checkstring(ep, (unsigned char *)"SX")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].vbuf ? framepanels[id - 1].sx : 0;
		targ = T_INT;
		return;
	}
	// FRAME(SY id) - scroll Y offset (0 if no vbuf)
	else if ((tp = checkstring(ep, (unsigned char *)"SY")))
	{
		int id = (int)getint(tp, 1, num_panels);
		if (!framepanels[id - 1].active)
			error("Panel % not active", id);
		iret = framepanels[id - 1].vbuf ? framepanels[id - 1].sy : 0;
		targ = T_INT;
		return;
	}
	// FRAME(INKEY) - frame-aware non-blocking key read (returns string like INKEY$)
	else if (checkstring(ep, (unsigned char *)"INKEY"))
	{
		if (!frame)
			error("Frame not created");

		// Animate cursor blink if cursor is enabled and we know which panel
		if (framecursor_on && frame_last_panel > 0 && frame_last_panel <= num_panels)
		{
			FramePanel *pnl = &framepanels[frame_last_panel - 1];
			if (pnl->active)
			{
				int cx_abs = pnl->x1 + pnl->cx - (pnl->vbuf ? pnl->sx : 0);
				int cy_abs = pnl->y1 + pnl->cy - (pnl->vbuf ? pnl->sy : 0);
				FrameOverlay *ov = find_overlay_by_panel(frame_last_panel);
				if (ov && ov->visible)
				{
					cx_abs += ov->ox;
					cy_abs += ov->oy;
				}
				frame_blink_cursor(cx_abs, cy_abs);
			}
		}

		sret = GetTempStrMemory();
		int c = MMInkey();
		if (c != -1)
		{
			sret[0] = 1; // length
			sret[1] = c; // character
		}
		targ = T_STR;
		return;
	}
	else
		error("Unknown FRAME function");
}
#endif
#ifdef rp2350
void parse_and_strip(char *string, int *dims)
{
#else
void parse_and_strip(char *string, short *dims)
{
#endif
	// Initialize dims to zero
	for (int i = 0; i < MAXDIM; i++)
	{
		dims[i] = 0;
	}
	char *open = strchr(string, '(');
	char *close = strchr(string, ')');

	if (open && close && close > open)
	{
		// Parse dimension expressions inside parentheses
		char buffer[256];
		strncpy(buffer, open + 1, close - open - 1);
		buffer[close - open - 1] = '\0';

		// Parse comma-separated expressions directly using getinteger
		// This evaluates BASIC expressions like variables
		unsigned char *p = (unsigned char *)buffer;
		int idx = 0;
		while (*p && idx < MAXDIM)
		{
			skipspace(p);
			if (*p == 0)
				break;
			// Find end of this expression (comma or end of string)
			unsigned char *start = p;
			int paren_depth = 0;
			while (*p && !(*p == ',' && paren_depth == 0))
			{
				if (*p == '(')
					paren_depth++;
				else if (*p == ')')
					paren_depth--;
				p++;
			}
			// Temporarily terminate this expression
			unsigned char saved = *p;
			*p = 0;
			// Evaluate the expression
			dims[idx++] = getinteger(start);
			// Restore and skip comma
			*p = saved;
			if (*p == ',')
				p++;
		}

		// Replace with name()
		*(open + 1) = '\0';	   // truncate after '('
		strcpy(open + 1, ")"); // append ')'
	}
}
#ifdef rp2350
bool array_comp(int in[MAXDIM], int out[MAXDIM])

#else
bool array_comp(short in[MAXDIM], short out[MAXDIM])

#endif
{
	int last_in = -1, last_out = -1;

	// Find last non-zero index in each array
	for (int i = MAXDIM - 1; i >= 0; i--)
	{
		if (last_in == -1 && in[i] != 0)
			last_in = i;
		if (last_out == -1 && out[i] != 0)
			last_out = i;
	}

	// If positions of last non-zero differ, not allowed
	if (last_in != last_out)
		return false;

	// If both arrays are all zeros, they are identical
	if (last_in == -1)
		return true;

	// Compare all elements except at the last non-zero index
	for (int i = 0; i < MAXDIM; i++)
	{
		if (i == last_in)
			continue; // allow difference at the last non-zero
		if (in[i] != out[i])
			return false;
	}

	return true;
}

void cmd_redim(void)
{
#ifdef rp2350
	int dims[MAXDIM] = {0}, newdims[MAXDIM] = {0};
#else
	short dims[MAXDIM] = {0}, newdims[MAXDIM] = {0};
#endif
	uint8_t *oldmemory = NULL, *newmemory;
	int oldsize = 0, newsize = 0;
	int length = -1;
#ifdef STRUCTENABLED
	int structIdx = -1;
#endif
	unsigned char *tp;
	unsigned char old[MAXVARLEN + 1];
	int preserve = ((tp = checkstring(cmdline, (unsigned char *)"PRESERVE")) ? 1 : 0);
	if (tp == NULL)
		tp = cmdline;
	{
		getcsargs(&tp, MAX_ARG_COUNT);
		for (int i = 0; i < argc; i += 2)
		{ // step through the arguments
			strncpy((char *)old, (char *)argv[i], MAXVARLEN);
			parse_and_strip((char *)old, newdims);
			findvar(old, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
			if (g_vartbl[g_VarIndex].type & T_STR)
				length = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
			if (g_vartbl[g_VarIndex].type & T_STRUCT)
				structIdx = (int)g_vartbl[g_VarIndex].size; // Save struct type index
#endif
			if (!g_vartbl[g_VarIndex].dims[0])
				error("$ is not an array ", argv[i]);
			int type = TypeMask(g_vartbl[g_VarIndex].type);
			if (g_vartbl[g_VarIndex].dims[0] > 0)
			{
				oldmemory = g_vartbl[g_VarIndex].val.s;
				oldsize = MemSize(oldmemory);
				for (int i = 0; i < MAXDIM; i++)
				{
					dims[i] = g_vartbl[g_VarIndex].dims[i];
				}
				if (!array_comp(dims, newdims) && preserve)
					error("Only the last array index can be changed");
			}
			uint32_t addr = erase((char *)old, (preserve ? true : false));
			if (type & T_STR)
			{
				unsigned char *newstring = GetTempStrMemory();
				strcpy((char *)newstring, (char *)argv[i]);
				strcat((char *)newstring, " LENGTH ");
				IntToStr((char *)&newstring[strlen((char *)newstring)], length, 10);
				findvar(newstring, type | V_FIND | V_DIM_VAR);
			}
#ifdef STRUCTENABLED
			else if (type & T_STRUCT)
			{
				// Strip "AS structtype" from the argument - find and terminate at AS token
				unsigned char *vararg = GetTempStrMemory();
				strcpy((char *)vararg, (char *)argv[i]);
				unsigned char *asp = skipvar(vararg, false);
				while (*asp && *asp != tokenAS)
					asp++;
				if (*asp == tokenAS)
					*asp = 0;			 // Terminate string before AS
				g_StructArg = structIdx; // Set struct type for findvar
				findvar(vararg, type | T_IMPLIED | V_FIND | V_DIM_VAR);
				g_StructArg = -1; // Reset
			}
#endif
			else
				findvar(argv[i], type | V_FIND | V_DIM_VAR);
			newmemory = g_vartbl[g_VarIndex].val.s;
			newsize = MemSize(g_vartbl[g_VarIndex].val.s);
			if (preserve)
			{
				if (newsize < oldsize)
					oldsize = newsize;
				memcpy(newmemory, oldmemory, oldsize);
				// Check if in heap
				if (addr > (uint32_t)MMHeap && addr < (uint32_t)MMHeap + heap_memory_size)
				{
					FreeMemorySafe((void **)&addr);
				}
#ifdef rp2350
				else if (addr > (uint32_t)PSRAMbase && addr < (uint32_t)PSRAMbase + PSRAMsize)
				{
					FreeMemorySafe((void **)&addr);
				}
#endif
			}
		}
	}
}
void MIPS16 cmd_erase(void)
{
	int i;
	char p[MAXVARLEN + 1];
	getcsargs(&cmdline, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
	if ((argc & 0x01) == 0)
		StandardError(2);
	for (i = 0; i < argc; i += 2)
	{
		strcpy((char *)p, (char *)argv[i]);
		if (*argv[i] & 0x80)
			error("You can't erase an in-built function");
		erase(p, false);
	}
}

void cmd_endfun(void)
{
	checkend(cmdline);
	if (gosubindex == 0 || gosubstack[gosubindex - 1] != NULL)
		error("Nothing to return to");
	nextstmt = (unsigned char *)"\0\0\0"; // now terminate this run of ExecuteProgram()
}

void MIPS16 cmd_read(void)
{
	int i, j, k, len, card;
	unsigned char *p, *lineptr = NULL, *ptr;
	unsigned short datatoken;
	int vcnt, vidx, num_to_read = 0;
	if (checkstring(cmdline, (unsigned char *)"SAVE"))
	{
		if (restorepointer == MAXRESTORE - 1)
			error((char *)"Too many saves");
		datastore[restorepointer].SaveNextDataLine = NextDataLine;
		datastore[restorepointer].SaveNextData = NextData;
		restorepointer++;
		return;
	}
	if (checkstring(cmdline, (unsigned char *)"RESTORE"))
	{
		if (!restorepointer)
			error((char *)"Nothing to restore");
		restorepointer--;
		NextDataLine = datastore[restorepointer].SaveNextDataLine;
		NextData = datastore[restorepointer].SaveNextData;
		return;
	}
	getcsargs(&cmdline, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
	if (argc == 0)
		SyntaxError();
	;
	// first count the elements and do the syntax checking
	for (vcnt = i = 0; i < argc; i++)
	{
		if (i & 0x01)
		{
			if (*argv[i] != ',')
				SyntaxError();
			;
		}
		else
		{
			findvar(argv[i], V_FIND | V_EMPTY_OK);
			if (g_vartbl[g_VarIndex].type & T_CONST)
				StandardError(22);
			card = 1;
			if (emptyarray)
			{ // empty array
				for (k = 0; k < MAXDIM; k++)
				{
					j = (g_vartbl[g_VarIndex].dims[k] - g_OptionBase + 1);
					if (j)
						card *= j;
				}
			}
			num_to_read += card;
		}
	}
	char **vtbl = GetTempMemory(num_to_read * sizeof(char *));
	int *vtype = GetTempMemory(num_to_read * sizeof(int));
	int *vsize = GetTempMemory(num_to_read * sizeof(int));
	// step through the arguments and save the pointer and type
	for (vcnt = i = 0; i < argc; i += 2)
	{
		ptr = findvar(argv[i], V_FIND | V_EMPTY_OK);
		vtbl[vcnt] = (char *)ptr;
		card = 1;
		if (emptyarray)
		{ // empty array
			for (k = 0; k < MAXDIM; k++)
			{
				j = (g_vartbl[g_VarIndex].dims[k] - g_OptionBase + 1);
				if (j)
					card *= j;
			}
		}
		for (k = 0; k < card; k++)
		{
			if (k)
			{
				if (g_vartbl[g_VarIndex].type & (T_INT | T_NBR))
					ptr += 8;
				else
					ptr += g_vartbl[g_VarIndex].size + 1;
				vtbl[vcnt] = (char *)ptr;
			}
#ifdef STRUCTENABLED
			if (g_StructMemberType != 0)
			{
				vtype[vcnt] = TypeMask(g_StructMemberType);
				vsize[vcnt] = (g_StructMemberType & T_STR) ? g_StructMemberSize : g_vartbl[g_VarIndex].size;
			}
			else
			{
				vtype[vcnt] = TypeMask(g_vartbl[g_VarIndex].type);
				vsize[vcnt] = g_vartbl[g_VarIndex].size;
			}
#else
			vtype[vcnt] = TypeMask(g_vartbl[g_VarIndex].type);
			vsize[vcnt] = g_vartbl[g_VarIndex].size;
#endif
			vcnt++;
		}
	}

	// setup for a search through the whole memory
	vidx = 0;
	datatoken = GetCommandValue((unsigned char *)"Data");
	p = lineptr = NextDataLine;
	if (*p == 0xff)
		error("No DATA to read"); // error if there is no program

	// search looking for a DATA statement.  We keep returning to this point until all the data is found
search_again:
	while (1)
	{
		if (*p == 0)
			p++; // if it is at the end of an element skip the zero marker
		if (*p == 0 /* || *p == 0xff*/)
			error("No DATA to read"); // end of the program and we still need more data
		if (*p == T_NEWLINE)
			lineptr = p++;
		if (*p == T_LINENBR)
			p += 3;
		skipspace(p);
		if (*p == T_LABEL)
		{				   // if there is a label here
			p += p[1] + 2; // skip over the label
			skipspace(p);  // and any following spaces
		}
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == datatoken)
			break; // found a DATA statement
		while (*p)
			p++; // look for the zero marking the start of the next element
	}
	NextDataLine = lineptr;
	p += sizeof(CommandToken); // step over the token
	skipspace(p);
	if (!*p || *p == '\'')
	{
		CurrentLinePtr = lineptr;
		error("No DATA to read");
	}

	// we have a DATA statement, first split the line into arguments
	{ // new block, the macro must be the first executable stmt in a block
		getcsargs(&p, (MAX_ARG_COUNT * 2) - 1);
		if ((argc & 1) == 0)
		{
			CurrentLinePtr = lineptr;
			SyntaxError();
			;
		}
		// now step through the variables on the READ line and get their new values from the argument list
		// we set the line number to the number of the DATA stmt so that any errors are reported correctly
		while (vidx < vcnt)
		{
			// check that there is some data to read if not look for another DATA stmt
			if (NextData > argc)
			{
				skipline(p);
				NextData = 0;
				goto search_again;
			}
			CurrentLinePtr = lineptr;
			if (vtype[vidx] & T_STR)
			{
				char *p1, *p2;
				if (*argv[NextData] == '"')
				{ // if quoted string
					int toggle = 0;
					for (len = 0, p1 = vtbl[vidx], p2 = (char *)argv[NextData] + 1; *p2 && *p2 != '"'; len++)
					{
						if (*p2 == '\\' && p2[1] != '"' && OptionEscape)
							toggle ^= 1;
						if (toggle)
						{
							if (*p2 == '\\' && isdigit((unsigned char)p2[1]) && isdigit((unsigned char)p2[2]) && isdigit((unsigned char)p2[3]))
							{
								p2++;
								i = (*p2++) - 48;
								i *= 10;
								i += (*p2++) - 48;
								i *= 10;
								i += (*p2++) - 48;
								if (i == 0)
									StandardErrorParamS(46, "$");
								*p1++ = i;
							}
							else
							{
								p2++;
								switch (*p2)
								{
								case '\\':
									*p1++ = '\\';
									p2++;
									break;
								case 'a':
									*p1++ = '\a';
									p2++;
									break;
								case 'b':
									*p1++ = '\b';
									p2++;
									break;
								case 'e':
									*p1++ = '\e';
									p2++;
									break;
								case 'f':
									*p1++ = '\f';
									p2++;
									break;
								case 'n':
									*p1++ = '\n';
									p2++;
									break;
								case 'q':
									*p1++ = '\"';
									p2++;
									break;
								case 'r':
									*p1++ = '\r';
									p2++;
									break;
								case 't':
									*p1++ = '\t';
									p2++;
									break;
								case 'v':
									*p1++ = '\v';
									p2++;
									break;
								case '&':
									p2++;
									if (isxdigit((unsigned char)*p2) && isxdigit((unsigned char)p2[1]))
									{
										i = 0;
										i = (i << 4) | ((mytoupper(*p2) >= 'A') ? mytoupper(*p2) - 'A' + 10 : *p2 - '0');
										p++;
										i = (i << 4) | ((mytoupper(*p2) >= 'A') ? mytoupper(*p2) - 'A' + 10 : *p2 - '0');
										if (i == 0)
											StandardErrorParamS(46, "$");
										p2++;
										*p1++ = i;
									}
									else
										*p1++ = 'x';
									break;
								default:
									*p1++ = *p2++;
								}
							}
							toggle = 0;
						}
						else
							*p1++ = *p2++;
					}
				}
				else
				{ // else if not quoted
					for (len = 0, p1 = vtbl[vidx], p2 = (char *)argv[NextData]; *p2 && *p2 != '\''; len++, p1++, p2++)
					{
						if (*p2 < 0x20 || *p2 >= 0x7f)
							error("Invalid character");
						*p1 = *p2; // copy up to the comma
					}
				}
				if (len > vsize[vidx])
					error("String too long");
				*p1 = 0;						   // terminate the string
				CtoM((unsigned char *)vtbl[vidx]); // convert to a MMBasic string
			}
			else if (vtype[vidx] & T_INT)
				*((long long int *)vtbl[vidx]) = getinteger(argv[NextData]); // much easier if integer variable
			else
				*((MMFLOAT *)vtbl[vidx]) = getnumber(argv[NextData]); // same for numeric variable

			vidx++;
			NextData += 2;
		}
	}
}

void cmd_call(void)
{
	int i;
	unsigned char *p = getCstring(cmdline); // get the command we want to call
	unsigned char *q = skipexpression(cmdline);
	if (*q == ',')
		q++;
	i = FindSubFun(p, false); // it could be a defined command
	strcat((char *)p, " ");
	strcat((char *)p, (char *)q);
	if (i >= 0)
	{ // >= 0 means it is a user defined command
		DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
	}
	else
		error("Unknown user subroutine");
}

void MIPS16 cmd_restore(void)
{
	if (*cmdline == 0 || *cmdline == '\'')
	{
		if (CurrentLinePtr >= ProgMemory && CurrentLinePtr < ProgMemory + MAX_PROG_SIZE)
			NextDataLine = ProgMemory;
		else
			NextDataLine = LibMemory;
		NextData = 0;
	}
	else
	{
#ifdef CACHE
		unsigned char *restore_key = cmdline; /* stable key before skipspace  */
#endif
		skipspace(cmdline);
		if (*cmdline == '"')
		{
#ifdef CACHE
			if (g_trace_cache_flags & TCF_RESTORE)
			{
				unsigned char *cached_tgt;
				if (TraceCacheTryJump(restore_key, &cached_tgt))
				{
					NextDataLine = cached_tgt;
					NextData = 0;
					return;
				}
			}
#endif
			NextDataLine = findlabel(getCstring(cmdline));
			NextData = 0;
#ifdef CACHE
			if (g_trace_cache_flags & TCF_RESTORE)
				TraceCacheStoreJump(restore_key, NextDataLine);
#endif
		}
		else if (isdigit(*cmdline) || *cmdline == GetTokenValue((unsigned char *)"+") || *cmdline == GetTokenValue((unsigned char *)"-") || *cmdline == '.')
		{
#ifdef CACHE
			if (g_trace_cache_flags & TCF_RESTORE)
			{
				unsigned char *cached_tgt;
				if (TraceCacheTryJump(restore_key, &cached_tgt))
				{
					NextDataLine = cached_tgt;
					NextData = 0;
					return;
				}
			}
#endif
			NextDataLine = findline(getinteger(cmdline), true); // try for a line number
			NextData = 0;
#ifdef CACHE
			if (g_trace_cache_flags & TCF_RESTORE)
				TraceCacheStoreJump(restore_key, NextDataLine);
#endif
		}
		else
		{
			/* Variable argument — target changes at run time; never cache.   */
			void *ptr = findvar(cmdline, V_NOFIND_NULL);
			if (ptr)
			{
				if (g_vartbl[g_VarIndex].type & T_NBR)
				{
					if (g_vartbl[g_VarIndex].dims[0] > 0)
					{ // Not an array
						SyntaxError();
						;
					}
					NextDataLine = findline(getinteger(cmdline), true);
				}
				else if (g_vartbl[g_VarIndex].type & T_INT)
				{
					if (g_vartbl[g_VarIndex].dims[0] > 0)
					{ // Not an array
						SyntaxError();
						;
					}
					NextDataLine = findline(getinteger(cmdline), true);
				}
				else
				{
					NextDataLine = findlabel(getCstring(cmdline)); // must be a label
				}
			}
			else if (isnamestart(*cmdline))
			{
#ifdef CACHE
				if (g_trace_cache_flags & TCF_RESTORE)
				{
					unsigned char *cached_tgt;
					if (TraceCacheTryJump(restore_key, &cached_tgt))
					{
						NextDataLine = cached_tgt;
						NextData = 0;
						return;
					}
				}
#endif
				NextDataLine = findlabel(cmdline); // must be a label
#ifdef CACHE
				if (g_trace_cache_flags & TCF_RESTORE)
					TraceCacheStoreJump(restore_key, NextDataLine);
#endif
			}
			NextData = 0;
		}
	}
}

void cmd_lineinput(void)
{
	unsigned char *vp;
	int i, fnbr;
	getargs(&cmdline, 3, (unsigned char *)",;"); // this is a macro and must be the first executable stmt
	if (argc == 0 || argc == 2)
		SyntaxError();
	;

	i = 0;
	fnbr = 0;
	if (argc == 3)
	{
		// is the first argument a file number specifier?  If so, get it
		if (*argv[0] == '#' && *argv[1] == ',')
		{
			argv[0]++;
			fnbr = getinteger(argv[0]);
		}
		else
		{
			// is the first argument a prompt?  if so, print it otherwise there are too many arguments
			if (*argv[1] != ',' && *argv[1] != ';')
				SyntaxError();
			;
			MMfputs((unsigned char *)getstring(argv[0]), 0);
		}
		i = 2;
	}

	if (argc - i != 1)
		SyntaxError();
	;
	vp = findvar(argv[i], V_FIND);
	if (g_vartbl[g_VarIndex].type & T_CONST)
		StandardError(22);
	int linp_vtype = g_vartbl[g_VarIndex].type;
	int linp_size = g_vartbl[g_VarIndex].size;
#ifdef STRUCTENABLED
	if (g_StructMemberType != 0)
	{
		linp_vtype = g_StructMemberType;
		if (g_StructMemberType & T_STR)
			linp_size = g_StructMemberSize;
	}
#endif
	if (!(linp_vtype & T_STR))
		StandardError(6);
	MMgetline(fnbr, (char *)inpbuf); // get the input line
	if (strlen((char *)inpbuf) > linp_size)
		error("String too long");
	strcpy((char *)vp, (char *)inpbuf);
	CtoM(vp); // convert to a MMBasic string
}

void cmd_on(void)
{
	int r;
	unsigned char ss[4]; // this will be used to split up the argument line
	unsigned char *p;
	// first check if this is:   ON KEY location
	p = checkstring(cmdline, (unsigned char *)"PS2");
	if (p)
	{
		getcsargs(&p, 1);
		if (*argv[0] == '0' && !isdigit(*(argv[0] + 1)))
		{
			OnPS2GOSUB = NULL; // the program wants to turn the interrupt off
		}
		else
		{
			OnPS2GOSUB = GetIntAddress(argv[0]); // get a pointer to the interrupt routine
			InterruptUsed = true;
		}
		return;
	}
	p = checkstring(cmdline, (unsigned char *)"KEY");
	if (p)
	{
		getcsargs(&p, 3);
		if (argc == 1)
		{
			if (*argv[0] == '0' && !isdigit(*(argv[0] + 1)))
			{
				OnKeyGOSUB = NULL; // the program wants to turn the interrupt off
			}
			else
			{
				OnKeyGOSUB = GetIntAddress(argv[0]); // get a pointer to the interrupt routine
				InterruptUsed = true;
			}
			return;
		}
		else
		{
			keyselect = getint(argv[0], 0, 255);
			if (keyselect == 0)
			{
				KeyInterrupt = NULL; // the program wants to turn the interrupt off
			}
			else
			{
				if (*argv[2] == '0' && !isdigit(*(argv[2] + 1)))
				{
					KeyInterrupt = NULL; // the program wants to turn the interrupt off
				}
				else
				{
					KeyInterrupt = (char *)GetIntAddress(argv[2]); // get a pointer to the interrupt routine
					InterruptUsed = true;
				}
			}
			return;
		}
	}
	p = checkstring(cmdline, (unsigned char *)"ERROR");
	if (p)
	{
		if (checkstring(p, (unsigned char *)"ABORT"))
		{
			OptionErrorSkip = 0;
			return;
		}
		MMerrno = 0; // clear the error flags
		*MMErrMsg = 0;
		if (checkstring(p, (unsigned char *)"CLEAR"))
			return;
		else if (checkstring(p, (unsigned char *)"IGNORE"))
		{
			OptionErrorSkip = -1;
			return;
		}
		else if (checkstring(p, (unsigned char *)"RESTART"))
		{
			OptionErrorSkip = 999999;
			return;
		}
		else if ((p = checkstring(p, (unsigned char *)"SKIP")))
		{
			if (*p == 0 || *p == (unsigned char)'\'')
				OptionErrorSkip = 2;
			else
				OptionErrorSkip = getint(p, 1, 10000) + 1;
			return;
		}

		SyntaxError();
		;
	}

	// if we got here the command must be the traditional:  ON nbr GOTO|GOSUB line1, line2,... etc

	ss[0] = tokenGOTO;
	ss[1] = tokenGOSUB;
	ss[2] = ',';
	ss[3] = 0;
	{													// start a new block
		getargs(&cmdline, (MAX_ARG_COUNT * 2) - 1, ss); // macro must be the first executable stmt in a block
		if (argc < 3 || !(*argv[1] == ss[0] || *argv[1] == ss[1]))
			SyntaxError();
		;
		if (argc % 2 == 0)
			SyntaxError();
		;

		r = getint(argv[0], 0, 255); // evaluate the expression controlling the statement
		if (r == 0 || r > argc / 2)
			return; // microsoft say that we just go on to the next line

		if (*argv[1] == ss[1])
		{
			// this is a GOSUB, same as a GOTO but we need to first push the return pointer
			if (gosubindex >= MAXGOSUB)
				error("Too many nested GOSUB");
			errorstack[gosubindex] = CurrentLinePtr;
			gosubstack[gosubindex++] = nextstmt;
			g_LocalIndex++;
		}

		if (isnamestart(*argv[r * 2]))
			nextstmt = findlabel(argv[r * 2]); // must be a label
		else
			nextstmt = findline(getinteger(argv[r * 2]), true); // try for a line number
	}
	//    IgnorePIN = false;
}

/**
 * @cond
 * The following section will be excluded from the documentation.
 */
// utility routine used by DoDim() below and other places in the interpreter
// checks if the type has been explicitly specified as in DIM FLOAT A, B, ... etc
// For structures, sets g_StructArg to the structure index
unsigned char *CheckIfTypeSpecified(unsigned char *p, int *type, int AllowDefaultType)
{
	unsigned char *tp;

#ifdef STRUCTENABLED
	g_StructArg = -1; // Reset struct index
#endif

	if ((tp = checkstring(p, (unsigned char *)"INTEGER")) != NULL)
		*type = T_INT | T_IMPLIED;
	else if ((tp = checkstring(p, (unsigned char *)"STRING")) != NULL)
		*type = T_STR | T_IMPLIED;
	else if ((tp = checkstring(p, (unsigned char *)"FLOAT")) != NULL)
		*type = T_NBR | T_IMPLIED;
#ifdef STRUCTENABLED
	else
	{
		// Check if it's a structure type name
		skipspace(p); // Skip any leading whitespace before type name
		int structidx = FindStructType(p);
		if (structidx >= 0)
		{
			*type = T_STRUCT | T_IMPLIED;
			g_StructArg = structidx; // Store struct index in global
			// Advance past the type name
			tp = p;
			while (isnamechar(*tp))
				tp++;
			skipspace(tp);
		}
		else
		{
			if (!AllowDefaultType)
				error("Variable type");
			tp = p;
			*type = DefaultType; // if the type is not specified use the default
		}
	}
#else
	else
	{
		if (!AllowDefaultType)
			error("Variable type");
		tp = p;
		*type = DefaultType; // if the type is not specified use the default
	}
#endif
	return tp;
}

unsigned char *SetValue(unsigned char *p, int t, void *v)
{
	MMFLOAT f;
	long long int i64;
	unsigned char *s;
	char TempCurrentSubFunName[MAXVARLEN + 1];
	strcpy(TempCurrentSubFunName, (char *)CurrentSubFunName); // save the current sub/fun name
	if (t & T_STR)
	{
		p = evaluate(p, &f, &i64, &s, &t, true);
		Mstrcpy(v, s);
	}
	else if (t & T_NBR)
	{
		p = evaluate(p, &f, &i64, &s, &t, false);
		if (t & T_NBR)
			(*(MMFLOAT *)v) = f;
		else
			(*(MMFLOAT *)v) = (MMFLOAT)i64;
	}
	else
	{
		p = evaluate(p, &f, &i64, &s, &t, false);
		if (t & T_INT)
			(*(long long int *)v) = i64;
		else
			(*(long long int *)v) = FloatToInt64(f);
	}
	strcpy((char *)CurrentSubFunName, TempCurrentSubFunName); // restore the current sub/fun name
	return p;
}

/** @endcond */

// define a variable
// DIM [AS INTEGER|FLOAT|STRING] var[(d1 [,d2,...]] [AS INTEGER|FLOAT|STRING] [, ..., ...]
// LOCAL also uses this function the routines only differ in that LOCAL can only be used in a sub/fun
void MIPS16 cmd_dim(void)
{
	int i, j, k, type, typeSave, ImpliedType = 0, VIndexSave, StaticVar = false;
	unsigned char *p, chSave, *chPosit;
	unsigned char VarName[(MAXVARLEN * 2) + 1];
	void *v, *tv;

	if (*cmdline == tokenAS)
		cmdline++;									// this means that we can use DIM AS INTEGER a, b, etc
	p = CheckIfTypeSpecified(cmdline, &type, true); // check for DIM FLOAT A, B, ...
	ImpliedType = type;
	{ //  macro must be the first executable stmt in a block
		getcsargs(&p, (MAX_ARG_COUNT * 2) - 1);
		if ((argc & 0x01) == 0)
			SyntaxError();
		;

		for (i = 0; i < argc; i += 2)
		{
			p = skipvar(argv[i], false); // point to after the variable
			while (!(*p == 0 || *p == tokenAS || *p == (unsigned char)'\'' || *p == tokenEQUAL))
				p++; // skip over a LENGTH keyword if there and see if we can find "AS"
			chSave = *p;
			chPosit = p;
			*p = 0; // save the char then terminate the string so that LENGTH is evaluated correctly
			if (chSave == tokenAS)
			{ // are we using Microsoft syntax (eg, AS INTEGER)?
				if (ImpliedType & T_IMPLIED)
					error("Type specified twice");
				p++;									  // step over the AS token
				skipspace(p);							  // skip any whitespace after AS
				p = CheckIfTypeSpecified(p, &type, true); // and get the type
				if (!(type & T_IMPLIED))
					error("Variable type");
			}

			if (cmdtoken == cmdLOCAL)
			{
				if (g_LocalIndex == 0)
					error("Invalid here");
				type |= V_LOCAL; // local if defined in a sub/fun
			}

			if (cmdtoken == cmdSTATIC)
			{
				if (g_LocalIndex == 0)
					error("Invalid here");
				// create a unique global name
				if (*CurrentInterruptName)
					strcpy((char *)VarName, CurrentInterruptName); // we must be in an interrupt sub
				else
					strcpy((char *)VarName, CurrentSubFunName); // normal sub/fun
				for (k = 1; k <= MAXVARLEN; k++)
					if (!isnamechar(VarName[k]))
					{
						VarName[k] = 0; // terminate the string on a non valid char
						break;
					}
				strcat((char *)VarName, "\x1e");		  // use 0x1E (record separator) to avoid conflict with struct member syntax
				strcat((char *)VarName, (char *)argv[i]); // by prefixing the var name with the sub/fun name
				StaticVar = NAMELEN_STATIC;				  // flag for marking the variable as static
			}
			else
				strcpy((char *)VarName, (char *)argv[i]);

			typeSave = type;
			if (StaticVar)
			{
				// STATIC: the global mangled-name backing variable may be
				// reused across calls, so we must NOT raise "$ already
				// declared".  Use the historical two-step: probe first, then
				// create only if missing.
				v = findvar(VarName, type | V_NOFIND_NULL);
				VIndexSave = g_VarIndex;
			}
			else
			{
				// Plain DIM / LOCAL: collapse the legacy "check then create"
				// pair into a single findvar() call by passing V_DIM_NEW,
				// which makes findvar emit "$ already declared" itself if
				// the name is taken.
				v = NULL;
				VIndexSave = -1;
			}
			if (v == NULL)
			{																 // not found (or not yet looked up for non-STATIC case)
				v = findvar(VarName, type | V_FIND | V_DIM_VAR | V_DIM_NEW); // create the variable
				type = TypeMask(g_vartbl[g_VarIndex].type);
				VIndexSave = g_VarIndex;
				// Mark static variables with NAMELEN_STATIC so struct member lookup skips them
				if (StaticVar)
					g_vartbl[VIndexSave].namelen |= NAMELEN_STATIC;
				*chPosit = chSave; // restore the char previously removed
				if (g_vartbl[g_VarIndex].dims[0] == -1)
					error("Array dimensions");
				if (g_vartbl[g_VarIndex].dims[0] > 0)
				{
					g_DimUsed = true; // prevent OPTION BASE from being used
					v = g_vartbl[g_VarIndex].val.s;
				}
				while (*p && *p != '\'' && tokenfunction(*p) != op_equal)
					p++; // search through the line looking for the equals sign
				if (tokenfunction(*p) == op_equal)
				{
					p++; // step over the equals sign
					skipspace(p);
#ifdef STRUCTENABLED
					// Handle struct initialization: DIM var AS StructType = (val1, val2, ...)
					if (g_vartbl[g_VarIndex].type & T_STRUCT)
					{
						if (*p != '(')
							error("Expected '(' for structure initialisation");

						int struct_idx = (int)g_vartbl[VIndexSave].size;
						struct s_structdef *sd = g_structtbl[struct_idx];
						int struct_size = sd->total_size;
						unsigned char *struct_ptr = (unsigned char *)v;

						// Calculate number of struct elements (1 for simple, more for array)
						int num_elements = 1;
						if (g_vartbl[VIndexSave].dims[0] > 0)
						{
							for (j = 1, k = 0; k < MAXDIM && g_vartbl[VIndexSave].dims[k]; k++)
							{
								num_elements *= (g_vartbl[VIndexSave].dims[k] + 1 - g_OptionBase);
							}
						}

						p++; // step over opening '('
						skipspace(p);

						// Process each struct element
						for (int elem = 0; elem < num_elements; elem++)
						{
							// Process each member of the struct
							for (int m = 0; m < sd->num_members; m++)
							{
								struct s_structmember *member = &sd->members[m];
								unsigned char *member_ptr = struct_ptr + member->offset;

								// Calculate number of array elements for this member (1 if not array)
								int member_elements = 1;
								if (member->dims[0] != 0)
								{
									for (k = 0; k < MAXDIM && member->dims[k]; k++)
									{
										member_elements *= (member->dims[k] + 1 - g_OptionBase);
									}
								}

								// Process each element of member array (or just 1 if not array)
								for (int me = 0; me < member_elements; me++)
								{
									skipspace(p);
									if (*p == ')' || *p == 0)
										error("Not enough initialisation values");

									// Determine member size for pointer advancement
									int elem_size = 0;
									if (member->type & T_STR)
										elem_size = member->size + 1;
									else if (member->type & T_NBR)
										elem_size = sizeof(MMFLOAT);
									else if (member->type & T_INT)
										elem_size = sizeof(long long int);
									else
										error("Unsupported member type in initialisation");

									// Use SetValue to parse and assign the value
									p = SetValue(p, member->type, member_ptr);
									member_ptr += elem_size;

									skipspace(p);
									// Check for comma (more values) or closing paren
									if (*p == ',')
									{
										p++; // skip comma
									}
									else if (*p != ')')
									{
										error("Expected ',' or ')' in structure initialisation");
									}
								}
							}
							// Move to next struct element in array
							struct_ptr += struct_size;
						}

						skipspace(p);
						if (*p != ')')
							error("Expected ')' at end of structure initialisation");
					}
					else
#endif
						if (g_vartbl[g_VarIndex].dims[0] > 0 && *p == '(')
					{
						// calculate the overall size of the array
						for (j = 1, k = 0; k < MAXDIM && g_vartbl[VIndexSave].dims[k]; k++)
						{
							j *= (g_vartbl[VIndexSave].dims[k] + 1 - g_OptionBase);
						}
						do
						{
							p++; // step over the opening bracket or terminating comma
							p = SetValue(p, type, v);
							if (type & T_STR)
								v = (char *)v + g_vartbl[VIndexSave].size + 1;
							if (type & T_NBR)
								v = (char *)v + sizeof(MMFLOAT);
							if (type & T_INT)
								v = (char *)v + sizeof(long long int);
							skipspace(p);
							j--;
						} while (j > 0 && *p == ',');
						if (*p != ')')
							error("Number of initialising values");
						if (j != 0)
							error("Number of initialising values");
					}
					else
						SetValue(p, type, v);
				}
				type = ImpliedType;
			}
			else
			{
				if (!StaticVar)
					error("$ already declared", VarName);
			}

			// if it is a STATIC var create a local var pointing to the global var
			if (StaticVar)
			{
				// Single call: V_DIM_NEW errors with "$ already declared" if the
				// local pointer name is already taken, otherwise creates it.
				tv = findvar(argv[i], typeSave | V_LOCAL | V_FIND | V_DIM_VAR | V_DIM_NEW);
#ifdef STRUCTENABLED
				if (g_vartbl[VIndexSave].dims[0] > 0 || (g_vartbl[VIndexSave].type & (T_STR | T_STRUCT)))
#else
				if (g_vartbl[VIndexSave].dims[0] > 0 || (g_vartbl[VIndexSave].type & T_STR))
#endif
				{
					FreeMemorySafe((void **)&tv);							 // we don't need the memory allocated to the local
					g_vartbl[g_VarIndex].val.s = g_vartbl[VIndexSave].val.s; // point to the memory of the global variable
				}
				else
					g_vartbl[g_VarIndex].val.ia = &(g_vartbl[VIndexSave].val.i); // point to the data of the variable
				g_vartbl[g_VarIndex].type = g_vartbl[VIndexSave].type | T_PTR;	 // set the type to a pointer
				g_vartbl[g_VarIndex].size = g_vartbl[VIndexSave].size;			 // just in case it is a string copy the size
				for (j = 0; j < MAXDIM; j++)
					g_vartbl[g_VarIndex].dims[j] = g_vartbl[VIndexSave].dims[j]; // just in case it is an array copy the dimensions
			}
		}
	}
}

void cmd_const(void)
{
	unsigned char *p;
	void *v;
	int i, type;

	getcsargs(&cmdline, (MAX_ARG_COUNT * 2) - 1); //  macro must be the first executable stmt in a block
	if ((argc & 0x01) == 0)
		SyntaxError();
	;

	for (i = 0; i < argc; i += 2)
	{
		p = skipvar(argv[i], false); // point to after the variable
		skipspace(p);
		if (tokenfunction(*p) != op_equal)
			SyntaxError();
		;	 // must be followed by an equals sign
		p++; // step over the equals sign
		type = T_NOTYPE;
		v = DoExpression(p, &type); // evaluate the constant's value
		type = TypeMask(type);
		type |= V_FIND | V_DIM_VAR | T_CONST | T_IMPLIED;
		if (g_LocalIndex != 0)
			type |= V_LOCAL;	// local if defined in a sub/fun
		findvar(argv[i], type); // create the variable
		if (g_vartbl[g_VarIndex].dims[0] != 0)
			error("Invalid constant");
		if (TypeMask(g_vartbl[g_VarIndex].type) != TypeMask(type))
			error("Invalid constant");
		else
		{
			if (type & T_NBR)
				g_vartbl[g_VarIndex].val.f = *(MMFLOAT *)v; // and set its value
			if (type & T_INT)
				g_vartbl[g_VarIndex].val.i = *(long long int *)v;
			if (type & T_STR)
			{
				if ((unsigned char)*(unsigned char *)v < (MAXDIM - 1) * sizeof(g_vartbl[g_VarIndex].dims[1]))
				{
					FreeMemorySafe((void **)&g_vartbl[g_VarIndex].val.s);
					g_vartbl[g_VarIndex].val.s = (void *)&g_vartbl[g_VarIndex].dims[1];
				}
				Mstrcpy((unsigned char *)g_vartbl[g_VarIndex].val.s, (unsigned char *)v);
			}
		}
	}
}

#ifdef STRUCTENABLED
// TYPE typename - At runtime, just skip to END TYPE (like SUB/FUN)
// Structure definition is processed in PrepareProgramExt
#ifdef rp2350
void cmd_type(void)
#else
void MIPS16 cmd_type(void)
#endif
{
	unsigned char *p;

	// At runtime, we just skip past the TYPE block
	// The structure definition was already built during PrepareProgram
	p = nextstmt;
	while (1)
	{
		p = GetNextCommand(p, NULL, (unsigned char *)"No matching END TYPE");
		CommandToken tkn = commandtbl_decode(p);
		if (tkn == cmdTYPE)
			error("Nested TYPE not allowed");
		if (tkn == cmdEND_TYPE)
		{
			skipelement(p);
			nextstmt = p;
			break;
		}
	}
}

// END TYPE - should never be executed directly (only reached via cmd_type skip)
#ifdef rp2350
void cmd_endtype(void)
#else
void MIPS16 cmd_endtype(void)
#endif
{
	error("END TYPE without TYPE");
}

// STRUCT command - operations on structure variables
// Syntax:
//   STRUCT COPY source TO destination
//   STRUCT COPY source() TO destination()  - copy entire array
//   (future: STRUCT PRINT var, STRUCT CLEAR var, etc.)
#ifdef rp2350
void cmd_struct(void)
#else
void MIPS16 cmd_struct(void)
#endif
{
	unsigned char *p;

	if ((p = checkstring(cmdline, (unsigned char *)"COPY")) != NULL)
	{
		// STRUCT COPY source TO destination
		// STRUCT COPY source() TO destination()  - copy entire array
		unsigned char *src_ptr, *dst_ptr;
		int src_idx, dst_idx, src_struct_type, dst_struct_type;
		unsigned char *tp;
		int src_is_array = 0, dst_is_array = 0;
		int src_num_elements = 1, dst_num_elements = 1;

		skipspace(p);

		// Get source variable - V_EMPTY_OK allows empty () for arrays
		src_ptr = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		src_idx = g_VarIndex;

		// Check source is a struct (not a member access)
		if (!(g_vartbl[src_idx].type & T_STRUCT))
			error("Source must be a structure variable");

		// For struct arrays, the pointer returned is to the specific element
		// but we need to verify this is a whole struct, not a member
		if (g_StructMemberType != 0)
			error("Cannot copy structure member, use whole structure");

		src_struct_type = (int)g_vartbl[src_idx].size; // struct type index stored in size field

		// Check if source is an array with empty parentheses (whole array copy)
		// V_EMPTY_OK returns base pointer when () is empty
		if (g_vartbl[src_idx].dims[0] != 0)
		{
			// It's an array - check if empty parentheses were used
			unsigned char *paren = (unsigned char *)strchr((char *)p, '(');
			if (paren)
			{
				paren++;
				skipspace(paren);
				if (*paren == ')')
				{
					// Empty parentheses - whole array copy
					src_is_array = 1;
					for (int d = 0; d < MAXDIM && g_vartbl[src_idx].dims[d] != 0; d++)
					{
						src_num_elements *= (g_vartbl[src_idx].dims[d] + 1 - g_OptionBase);
					}
				}
			}
		}

		// Skip past the source variable to find TO
		tp = skipvar(p, false);
		skipspace(tp);

		// Check for TO keyword (tokenized)
		if (*tp != tokenTO)
			error("Expected TO");
		tp++; // skip TO token
		skipspace(tp);

		// Get destination variable
		dst_ptr = findvar(tp, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		dst_idx = g_VarIndex;

		// Check destination is a struct
		if (!(g_vartbl[dst_idx].type & T_STRUCT))
			error("Destination must be a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot copy to structure member, use whole structure");

		dst_struct_type = (int)g_vartbl[dst_idx].size;

		// Check if destination is an array with empty parentheses
		if (g_vartbl[dst_idx].dims[0] != 0)
		{
			unsigned char *paren = (unsigned char *)strchr((char *)tp, '(');
			if (paren)
			{
				paren++;
				skipspace(paren);
				if (*paren == ')')
				{
					dst_is_array = 1;
					for (int d = 0; d < MAXDIM && g_vartbl[dst_idx].dims[d] != 0; d++)
					{
						dst_num_elements *= (g_vartbl[dst_idx].dims[d] + 1 - g_OptionBase);
					}
				}
			}
		}

		// Validate same struct type
		if (src_struct_type != dst_struct_type)
			error("Structure types must match");

		// Validate array copy consistency
		if (src_is_array != dst_is_array)
			error("Both source and destination must be arrays or both must be single structs");

		// For array copy, destination must be at least as large as source
		if (src_is_array && dst_num_elements < src_num_elements)
			error("Destination array too small");

		// Perform the copy
		int struct_size = g_structtbl[src_struct_type]->total_size;
		int copy_size = struct_size * src_num_elements;
		memcpy(dst_ptr, src_ptr, copy_size);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SORT")) != NULL)
	{
		// STRUCT SORT array().membername [, flags]
		// flags: bit0=reverse, bit1=case insensitive (strings), bit2=empty strings at end (strings)
		int arr_idx, struct_type;
		unsigned char *tp;
		int flags = 0;
		int member_type, member_offset, member_size;
		int num_elements;
		int struct_size;

		skipspace(p);

		// Get array variable with member access: array().membername
		// findvar will resolve the member access and set g_StructMemberType/Offset/Size
		findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		arr_idx = g_VarIndex;

		// Check it's a struct array with member access
		if (!(g_vartbl[arr_idx].type & T_STRUCT))
			error("Expected a structure array");
		if (g_vartbl[arr_idx].dims[0] == 0)
			error("Expected a structure array");
		if (g_StructMemberType == 0)
			error("Expected structarray().membername syntax");

		// Get member info from globals set by findvar
		member_type = g_StructMemberType;
		member_offset = g_StructMemberOffset;
		member_size = g_StructMemberSize;
		(void)member_size; // Used for validation, suppress unused warning

		// Member cannot be a nested struct
		if (member_type == T_STRUCT)
			error("Cannot sort by nested structure member");

		struct_type = (int)g_vartbl[arr_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Calculate number of elements
		num_elements = 1;
		for (int d = 0; d < MAXDIM && g_vartbl[arr_idx].dims[d] != 0; d++)
		{
			num_elements *= (g_vartbl[arr_idx].dims[d] + 1 - g_OptionBase);
		}

		// Skip past array().membername to find optional flags
		tp = skipvar(p, false);
		skipspace(tp);

		// Check for optional flags parameter
		if (*tp == ',')
		{
			tp++;
			skipspace(tp);
			flags = (int)getint(tp, 0, 7);
		}

		// Get pointer to array data
		unsigned char *base_ptr = g_vartbl[arr_idx].val.s;

		// Allocate temporary buffer for swap
		unsigned char *temp = GetTempMemory(struct_size);

		// Shell sort implementation (efficient for medium arrays, in-place)
		int gap, i, j;
		int reverse = flags & 1;
		int case_insensitive = flags & 2;
		int empty_at_end = flags & 4;

		for (gap = num_elements / 2; gap > 0; gap /= 2)
		{
			for (i = gap; i < num_elements; i++)
			{
				memcpy(temp, base_ptr + i * struct_size, struct_size);

				for (j = i; j >= gap; j -= gap)
				{
					unsigned char *elem_j_gap = base_ptr + (j - gap) * struct_size;
					unsigned char *val_a = elem_j_gap + member_offset;
					unsigned char *val_b = temp + member_offset;
					int cmp = 0;

					// Compare based on member type
					if (member_type & T_INT)
					{
						long long int a = *(long long int *)val_a;
						long long int b = *(long long int *)val_b;
						if (a < b)
							cmp = -1;
						else if (a > b)
							cmp = 1;
						else
							cmp = 0;
					}
					else if (member_type & T_NBR)
					{
						MMFLOAT a = *(MMFLOAT *)val_a;
						MMFLOAT b = *(MMFLOAT *)val_b;
						if (a < b)
							cmp = -1;
						else if (a > b)
							cmp = 1;
						else
							cmp = 0;
					}
					else if (member_type & T_STR)
					{
						// MMBasic strings: first byte is length
						int len_a = *val_a;
						int len_b = *val_b;

						// Handle empty strings at end option
						if (empty_at_end)
						{
							if (len_a == 0 && len_b != 0)
							{
								cmp = 1; // Empty string a goes after b
							}
							else if (len_a != 0 && len_b == 0)
							{
								cmp = -1; // Non-empty a goes before empty b
							}
							else if (len_a == 0 && len_b == 0)
							{
								cmp = 0; // Both empty, equal
							}
							else
							{
								// Both non-empty, compare normally
								int minlen = (len_a < len_b) ? len_a : len_b;
								if (case_insensitive)
								{
									for (int k = 1; k <= minlen; k++)
									{
										int ca = toupper(val_a[k]);
										int cb = toupper(val_b[k]);
										if (ca < cb)
										{
											cmp = -1;
											break;
										}
										if (ca > cb)
										{
											cmp = 1;
											break;
										}
									}
								}
								else
								{
									for (int k = 1; k <= minlen; k++)
									{
										if (val_a[k] < val_b[k])
										{
											cmp = -1;
											break;
										}
										if (val_a[k] > val_b[k])
										{
											cmp = 1;
											break;
										}
									}
								}
								if (cmp == 0)
								{
									if (len_a < len_b)
										cmp = -1;
									else if (len_a > len_b)
										cmp = 1;
								}
							}
						}
						else
						{
							// Normal string comparison
							int minlen = (len_a < len_b) ? len_a : len_b;
							if (case_insensitive)
							{
								for (int k = 1; k <= minlen; k++)
								{
									int ca = toupper(val_a[k]);
									int cb = toupper(val_b[k]);
									if (ca < cb)
									{
										cmp = -1;
										break;
									}
									if (ca > cb)
									{
										cmp = 1;
										break;
									}
								}
							}
							else
							{
								for (int k = 1; k <= minlen; k++)
								{
									if (val_a[k] < val_b[k])
									{
										cmp = -1;
										break;
									}
									if (val_a[k] > val_b[k])
									{
										cmp = 1;
										break;
									}
								}
							}
							if (cmp == 0)
							{
								if (len_a < len_b)
									cmp = -1;
								else if (len_a > len_b)
									cmp = 1;
							}
						}
					}

					// Apply reverse flag
					if (reverse)
						cmp = -cmp;

					// If elem[j-gap] > temp, shift it up
					if (cmp > 0)
					{
						memcpy(base_ptr + j * struct_size, elem_j_gap, struct_size);
					}
					else
					{
						break;
					}
				}
				memcpy(base_ptr + j * struct_size, temp, struct_size);
			}
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"CLEAR")) != NULL)
	{
		// STRUCT CLEAR var or STRUCT CLEAR array()
		// Resets all members to defaults (0 for numbers, "" for strings)
		int var_idx, struct_type;
		unsigned char *var_ptr;
		int struct_size;

		skipspace(p);

		// Get variable - V_EMPTY_OK allows empty () for arrays
		var_ptr = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		var_idx = g_VarIndex;

		// Check it's a struct
		if (!(g_vartbl[var_idx].type & T_STRUCT))
			error("Expected a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot clear a structure member, use whole structure");

		struct_type = (int)g_vartbl[var_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Calculate total size if array
		int num_elements = 1;
		if (g_vartbl[var_idx].dims[0] != 0)
		{
			for (int d = 0; d < MAXDIM && g_vartbl[var_idx].dims[d] != 0; d++)
			{
				num_elements *= (g_vartbl[var_idx].dims[d] + 1 - g_OptionBase);
			}
		}

		// Zero the memory
		memset(var_ptr, 0, struct_size * num_elements);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SWAP")) != NULL)
	{
		// STRUCT SWAP var1, var2
		// Swaps two struct variables (must be same type)
		unsigned char *src_ptr, *dst_ptr;
		int src_idx, dst_idx;
		int src_struct_type, dst_struct_type;
		unsigned char *tp;

		skipspace(p);

		// Get first variable
		src_ptr = findvar(p, V_FIND | V_NOFIND_ERR);
		src_idx = g_VarIndex;

		if (!(g_vartbl[src_idx].type & T_STRUCT))
			error("First argument must be a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot swap structure member, use whole structure");

		src_struct_type = (int)g_vartbl[src_idx].size;

		// Skip past first variable to find comma
		tp = skipvar(p, false);
		skipspace(tp);

		if (*tp != ',')
			error("Expected comma");
		tp++;
		skipspace(tp);

		// Get second variable
		dst_ptr = findvar(tp, V_FIND | V_NOFIND_ERR);
		dst_idx = g_VarIndex;

		if (!(g_vartbl[dst_idx].type & T_STRUCT))
			error("Second argument must be a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot swap structure member, use whole structure");

		dst_struct_type = (int)g_vartbl[dst_idx].size;

		// Validate same struct type
		if (src_struct_type != dst_struct_type)
			error("Structure types must match");

		// Perform the swap using temp memory
		int swap_size = g_structtbl[src_struct_type]->total_size;
		unsigned char *temp = GetTempMemory(swap_size);
		memcpy(temp, src_ptr, swap_size);
		memcpy(src_ptr, dst_ptr, swap_size);
		memcpy(dst_ptr, temp, swap_size);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"SAVE")) != NULL)
	{
		// STRUCT SAVE #n, var or STRUCT SAVE #n, array() or STRUCT SAVE #n, array(i)
		// Writes struct data as binary to open file
		int fnbr;
		int var_idx, struct_type, struct_size;
		unsigned char *var_ptr;

		skipspace(p);

		// Get file number
		if (*p != '#')
			error("Expected #filenumber");
		p++;
		fnbr = getinteger(p);

		// Validate file number and that it's a disk file
		if (fnbr < 1 || fnbr > MAXOPENFILES)
			error("Invalid file number");
		if (FileTable[fnbr].com == 0)
			error("File not open");
		if (FileTable[fnbr].com <= MAXCOMPORTS)
			error("Not a disk file");

		// Skip past file number to comma
		while (*p && *p != ',')
			p++;
		if (*p != ',')
			error("Expected comma");
		p++;
		skipspace(p);

		// Save pointer to variable name for parenthesis check
		unsigned char *varname = p;

		// Get variable - V_EMPTY_OK allows empty () for arrays
		var_ptr = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		var_idx = g_VarIndex;

		if (!(g_vartbl[var_idx].type & T_STRUCT))
			error("Expected a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot save a structure member, use whole structure");

		struct_type = (int)g_vartbl[var_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Determine if it's an array and how to handle it
		int num_elements = 1;
		int is_array = (g_vartbl[var_idx].dims[0] != 0);

		if (is_array)
		{
			// Check for parentheses
			unsigned char *paren = (unsigned char *)strchr((char *)varname, '(');
			if (!paren)
				error("Array variable requires () or (index)");

			paren++;
			skipspace(paren);
			if (*paren == ')')
			{
				// Empty brackets - save entire array
				for (int d = 0; d < MAXDIM && g_vartbl[var_idx].dims[d] != 0; d++)
				{
					num_elements *= (g_vartbl[var_idx].dims[d] + 1 - g_OptionBase);
				}
			}
			else
			{
				// Has index - findvar already resolved to the correct element
				// var_ptr points to the specific element, save just one
				num_elements = 1;
			}
		}

		// Write struct data to file
		FilePutData((char *)var_ptr, fnbr, struct_size * num_elements);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"LOAD")) != NULL)
	{
		// STRUCT LOAD #n, var or STRUCT LOAD #n, array() or STRUCT LOAD #n, array(i)
		// Reads struct data as binary from open file
		int fnbr;
		int var_idx, struct_type, struct_size;
		unsigned char *var_ptr;
		unsigned int bytes_read;

		skipspace(p);

		// Get file number
		if (*p != '#')
			error("Expected #filenumber");
		p++;
		fnbr = getinteger(p);

		// Validate file number and that it's a disk file
		if (fnbr < 1 || fnbr > MAXOPENFILES)
			error("Invalid file number");
		if (FileTable[fnbr].com == 0)
			error("File not open");
		if (FileTable[fnbr].com <= MAXCOMPORTS)
			error("Not a disk file");

		// Skip past file number to comma
		while (*p && *p != ',')
			p++;
		if (*p != ',')
			error("Expected comma");
		p++;
		skipspace(p);

		// Save pointer to variable name for parenthesis check
		unsigned char *varname = p;

		// Get variable - V_EMPTY_OK allows empty () for arrays
		var_ptr = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		var_idx = g_VarIndex;

		if (!(g_vartbl[var_idx].type & T_STRUCT))
			error("Expected a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot load into a structure member, use whole structure");

		struct_type = (int)g_vartbl[var_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Determine if it's an array and how to handle it
		int num_elements = 1;
		int is_array = (g_vartbl[var_idx].dims[0] != 0);

		if (is_array)
		{
			// Check for parentheses
			unsigned char *paren = (unsigned char *)strchr((char *)varname, '(');
			if (!paren)
				error("Array variable requires () or (index)");

			paren++;
			skipspace(paren);
			if (*paren == ')')
			{
				// Empty brackets - load entire array
				for (int d = 0; d < MAXDIM && g_vartbl[var_idx].dims[d] != 0; d++)
				{
					num_elements *= (g_vartbl[var_idx].dims[d] + 1 - g_OptionBase);
				}
			}
			else
			{
				// Has index - findvar already resolved to the correct element
				// var_ptr points to the specific element, load just one
				num_elements = 1;
			}
		}

		// Read struct data from file
		FileGetData(fnbr, var_ptr, struct_size * num_elements, &bytes_read);
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"PRINT")) != NULL)
	{
		// STRUCT PRINT var or STRUCT PRINT array() or STRUCT PRINT array(n)
		// Prints all members of a structure for debugging
		int var_idx, struct_type, struct_size;
		unsigned char *var_ptr;
		struct s_structdef *sd;

		skipspace(p);

		// Get variable - V_EMPTY_OK allows empty () for arrays
		var_ptr = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		var_idx = g_VarIndex;

		if (!(g_vartbl[var_idx].type & T_STRUCT))
			error("Expected a structure variable");

		if (g_StructMemberType != 0)
			error("Cannot print a structure member, use whole structure");

		struct_type = (int)g_vartbl[var_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;
		sd = g_structtbl[struct_type];

		// Calculate number of elements to print
		int num_elements = 1;
		int is_array = (g_vartbl[var_idx].dims[0] != 0);
		int single_element = 0;

		// Check if this is an indexed array access (e.g., arr(2)) vs whole array (arr())
		// If findvar resolved to a specific element, var_ptr points to that element
		// We detect this by checking if parentheses contain a value
		unsigned char *paren = (unsigned char *)strchr((char *)p, '(');
		if (paren && is_array)
		{
			paren++;
			skipspace(paren);
			if (*paren != ')')
			{
				// Has an index - print single element
				single_element = 1;
				num_elements = 1;
			}
		}

		if (is_array && !single_element)
		{
			for (int d = 0; d < MAXDIM && g_vartbl[var_idx].dims[d] != 0; d++)
			{
				num_elements *= (g_vartbl[var_idx].dims[d] + 1 - g_OptionBase);
			}
		}

		// Print structure type name
		MMPrintString((char *)sd->name);
		if (is_array && !single_element)
		{
			char buf[32];
			sprintf(buf, " array (%d elements):\r\n", num_elements);
			MMPrintString(buf);
		}
		else if (single_element)
		{
			MMPrintString(":\r\n");
		}
		else
		{
			MMPrintString(":\r\n");
		}

		// Print each element
		for (int elem = 0; elem < num_elements; elem++)
		{
			unsigned char *elem_ptr = var_ptr + (elem * struct_size);

			if (is_array && !single_element)
			{
				char buf[32];
				sprintf(buf, "[%d]:\r\n", elem + g_OptionBase);
				MMPrintString(buf);
			}

			// Print each member
			for (int m = 0; m < sd->num_members; m++)
			{
				struct s_structmember *sm = &sd->members[m];
				unsigned char *member_ptr = elem_ptr + sm->offset;
				char buf[STRINGSIZE];

				// Calculate array elements for this member
				int member_elements = 1;
				for (int d = 0; d < MAXDIM && sm->dims[d] != 0; d++)
				{
					member_elements *= (sm->dims[d] + 1 - g_OptionBase);
				}

				if (sm->type == T_STRUCT)
				{
					// Nested structure - print recursively with indentation
					struct s_structdef *nested_sd = g_structtbl[sm->size];
					sprintf(buf, "  .%s = %s:\r\n", sm->name, nested_sd->name);
					MMPrintString(buf);

					// Print nested members with extra indent
					for (int nm = 0; nm < nested_sd->num_members; nm++)
					{
						struct s_structmember *nsm = &nested_sd->members[nm];
						unsigned char *nested_ptr = member_ptr + nsm->offset;

						sprintf(buf, "    .%s = ", nsm->name);
						MMPrintString(buf);

						if (nsm->type == T_INT)
						{
							long long int val = *(long long int *)nested_ptr;
							sprintf(buf, "%lld", val);
							MMPrintString(buf);
						}
						else if (nsm->type == T_NBR)
						{
							MMFLOAT val = *(MMFLOAT *)nested_ptr;
							sprintf(buf, "%g", val);
							MMPrintString(buf);
						}
						else if (nsm->type == T_STR)
						{
							MMPrintString("\"");
							int len = *nested_ptr;
							for (int c = 0; c < len; c++)
							{
								char ch[2] = {nested_ptr[c + 1], 0};
								MMPrintString(ch);
							}
							MMPrintString("\"");
						}
						else if (nsm->type == T_STRUCT)
						{
							MMPrintString("(nested struct - use deeper access)");
						}
						MMPrintString("\r\n");
					}
				}
				else if (member_elements == 1)
				{
					// Simple member (not an array)
					sprintf(buf, "  .%s = ", sm->name);
					MMPrintString(buf);

					if (sm->type == T_INT)
					{
						long long int val = *(long long int *)member_ptr;
						sprintf(buf, "%lld", val);
						MMPrintString(buf);
					}
					else if (sm->type == T_NBR)
					{
						MMFLOAT val = *(MMFLOAT *)member_ptr;
						sprintf(buf, "%g", val);
						MMPrintString(buf);
					}
					else if (sm->type == T_STR)
					{
						MMPrintString("\"");
						// String: first byte is length
						int len = *member_ptr;
						for (int c = 0; c < len; c++)
						{
							char ch[2] = {member_ptr[c + 1], 0};
							MMPrintString(ch);
						}
						MMPrintString("\"");
					}
					MMPrintString("\r\n");
				}
				else
				{
					// Array member
					sprintf(buf, "  .%s() = ", sm->name);
					MMPrintString(buf);

					int elem_size;
					if (sm->type == T_STR)
						elem_size = sm->size + 1; // +1 for length byte
					else
						elem_size = sm->size;

					for (int ai = 0; ai < member_elements; ai++)
					{
						unsigned char *arr_ptr = member_ptr + (ai * elem_size);

						if (ai > 0)
							MMPrintString(", ");

						if (sm->type == T_INT)
						{
							long long int val = *(long long int *)arr_ptr;
							sprintf(buf, "%lld", val);
							MMPrintString(buf);
						}
						else if (sm->type == T_NBR)
						{
							MMFLOAT val = *(MMFLOAT *)arr_ptr;
							sprintf(buf, "%g", val);
							MMPrintString(buf);
						}
						else if (sm->type == T_STR)
						{
							MMPrintString("\"");
							int len = *arr_ptr;
							for (int c = 0; c < len; c++)
							{
								char ch[2] = {arr_ptr[c + 1], 0};
								MMPrintString(ch);
							}
							MMPrintString("\"");
						}
					}
					MMPrintString("\r\n");
				}
			}
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"EXTRACT")) != NULL)
	{
		// STRUCT EXTRACT structarray().membername, destarray()
		// Extracts a single member from each structure in an array into a simple array
		// This allows structure data to be used with commands that expect contiguous arrays
		// (e.g., LINE PLOT, MATH commands)
		int src_idx, dst_idx, struct_type, struct_size;
		unsigned char *tp;
		int member_type, member_offset, member_size;
		int src_num_elements, dst_num_elements;
		unsigned char *src_base, *dst_base;

		skipspace(p);

		// Get source struct array with member access: structarray().membername
		// findvar will resolve the member access and set g_StructMemberType/Offset/Size
		findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		src_idx = g_VarIndex;

		// Check it's a struct array with member access
		if (!(g_vartbl[src_idx].type & T_STRUCT))
			error("Expected a structure array");
		if (g_vartbl[src_idx].dims[0] == 0)
			error("Expected a structure array, not a single structure");
		if (g_vartbl[src_idx].dims[1] != 0)
			error("Only 1-dimensional structure arrays are supported");
		if (g_StructMemberType == 0)
			error("Expected structarray().membername syntax");

		// Get member info from globals set by findvar
		member_type = g_StructMemberType;
		member_offset = g_StructMemberOffset;
		member_size = g_StructMemberSize;

		// Member cannot be a nested struct
		if (member_type == T_STRUCT)
			error("Cannot extract nested structure member");

		struct_type = (int)g_vartbl[src_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Calculate number of source elements
		src_num_elements = g_vartbl[src_idx].dims[0] + 1 - g_OptionBase;
		src_base = g_vartbl[src_idx].val.s;

		// Skip past structarray().membername to find the comma
		tp = skipvar(p, false);
		skipspace(tp);

		if (*tp != ',')
			error("Expected comma and destination array");
		tp++;
		skipspace(tp);

		// Get destination array with empty ()
		dst_base = findvar(tp, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		dst_idx = g_VarIndex;

		// Check destination is a simple array (not struct)
		if (g_vartbl[dst_idx].type & T_STRUCT)
			error("Destination must be a simple array, not a structure");
		if (g_vartbl[dst_idx].dims[0] == 0)
			error("Destination must be an array");
		if (g_vartbl[dst_idx].dims[1] != 0)
			error("Destination must be a 1-dimensional array");

		// Calculate destination array size
		dst_num_elements = g_vartbl[dst_idx].dims[0] + 1 - g_OptionBase;

		// Check cardinality matches
		if (dst_num_elements != src_num_elements)
			error("Arrays must have the same size (source=%, dest=%)", src_num_elements, dst_num_elements);

		// Check types match
		int dst_type = g_vartbl[dst_idx].type & (T_INT | T_NBR | T_STR);
		if (dst_type != member_type)
			error("Type mismatch: structure member and destination array must have same type");

		// For strings, check length matches
		if (member_type == T_STR)
		{
			int dst_str_size = g_vartbl[dst_idx].size; // max string length for dest array
			if (dst_str_size != member_size)
				error("String length mismatch: member length=%, array length=%", member_size, dst_str_size);
		}

		// Perform the extraction
		if (member_type == T_INT)
		{
			long long int *dst = (long long int *)dst_base;
			for (int i = 0; i < src_num_elements; i++)
			{
				unsigned char *src_elem = src_base + (i * struct_size) + member_offset;
				dst[i] = *(long long int *)src_elem;
			}
		}
		else if (member_type == T_NBR)
		{
			MMFLOAT *dst = (MMFLOAT *)dst_base;
			for (int i = 0; i < src_num_elements; i++)
			{
				unsigned char *src_elem = src_base + (i * struct_size) + member_offset;
				dst[i] = *(MMFLOAT *)src_elem;
			}
		}
		else if (member_type == T_STR)
		{
			int str_size = member_size + 1; // +1 for length byte
			for (int i = 0; i < src_num_elements; i++)
			{
				unsigned char *src_elem = src_base + (i * struct_size) + member_offset;
				unsigned char *dst_elem = dst_base + (i * str_size);
				memcpy(dst_elem, src_elem, str_size);
			}
		}
	}
	else if ((p = checkstring(cmdline, (unsigned char *)"INSERT")) != NULL)
	{
		// STRUCT INSERT srcarray(), structarray().membername
		// Inserts values from a simple array into the specified member of each structure element
		// This is the reverse of STRUCT EXTRACT
		int src_idx, dst_idx, struct_type, struct_size;
		unsigned char *tp;
		int member_type, member_offset, member_size;
		int src_num_elements, dst_num_elements;
		unsigned char *src_base, *dst_base;

		skipspace(p);

		// Get source simple array variable with empty ()
		src_base = findvar(p, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		src_idx = g_VarIndex;

		// Check source is a simple array (not struct)
		if (g_vartbl[src_idx].type & T_STRUCT)
			error("Source must be a simple array, not a structure");
		if (g_vartbl[src_idx].dims[0] == 0)
			error("Source must be an array");
		if (g_vartbl[src_idx].dims[1] != 0)
			error("Source must be a 1-dimensional array");

		// Calculate source array size
		src_num_elements = g_vartbl[src_idx].dims[0] + 1 - g_OptionBase;
		int src_type = g_vartbl[src_idx].type & (T_INT | T_NBR | T_STR);
		int src_str_size = g_vartbl[src_idx].size; // for strings

		// Skip past source array to find comma
		tp = skipvar(p, false);
		skipspace(tp);

		if (*tp != ',')
			error("Expected comma after source array");
		tp++;
		skipspace(tp);

		// Get destination struct array with member access: structarray().membername
		// findvar will resolve the member access and set g_StructMemberType/Offset/Size
		dst_base = findvar(tp, V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		dst_idx = g_VarIndex;

		// Check it's a struct array with member access
		if (!(g_vartbl[dst_idx].type & T_STRUCT))
			error("Expected a structure array");
		if (g_vartbl[dst_idx].dims[0] == 0)
			error("Expected a structure array, not a single structure");
		if (g_vartbl[dst_idx].dims[1] != 0)
			error("Only 1-dimensional structure arrays are supported");
		if (g_StructMemberType == 0)
			error("Expected structarray().membername syntax");

		// Get member info from globals set by findvar
		member_type = g_StructMemberType;
		member_offset = g_StructMemberOffset;
		member_size = g_StructMemberSize;

		// Member cannot be a nested struct
		if (member_type == T_STRUCT)
			error("Cannot insert into nested structure member");

		struct_type = (int)g_vartbl[dst_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;

		// Calculate number of destination elements
		dst_num_elements = g_vartbl[dst_idx].dims[0] + 1 - g_OptionBase;

		// Use dst_base from the struct variable, not from member resolution
		dst_base = g_vartbl[dst_idx].val.s;

		// Check cardinality matches
		if (src_num_elements != dst_num_elements)
			error("Arrays must have the same size (source=%, dest=%)", src_num_elements, dst_num_elements);

		// Check types match
		if (src_type != member_type)
			error("Type mismatch: source array and structure member must have same type");

		// For strings, check length matches
		if (member_type == T_STR)
		{
			if (src_str_size != member_size)
				error("String length mismatch: array length=%, member length=%", src_str_size, member_size);
		}

		// Perform the insertion
		if (member_type == T_INT)
		{
			long long int *src = (long long int *)src_base;
			for (int i = 0; i < dst_num_elements; i++)
			{
				unsigned char *dst_elem = dst_base + (i * struct_size) + member_offset;
				*(long long int *)dst_elem = src[i];
			}
		}
		else if (member_type == T_NBR)
		{
			MMFLOAT *src = (MMFLOAT *)src_base;
			for (int i = 0; i < dst_num_elements; i++)
			{
				unsigned char *dst_elem = dst_base + (i * struct_size) + member_offset;
				*(MMFLOAT *)dst_elem = src[i];
			}
		}
		else if (member_type == T_STR)
		{
			int str_size = member_size + 1; // +1 for length byte
			for (int i = 0; i < dst_num_elements; i++)
			{
				unsigned char *src_elem = src_base + (i * str_size);
				unsigned char *dst_elem = dst_base + (i * struct_size) + member_offset;
				memcpy(dst_elem, src_elem, str_size);
			}
		}
	}
	else
	{
		error("Unknown STRUCT subcommand");
	}
}

// Parse a structure member definition line (called from PrepareProgramExt)
// Line format: membername[(dim1[,dim2,...])] AS type [LENGTH n]
// Returns: NULL if valid member parsed, error message string if error
const char *ParseStructMember(unsigned char *p, struct s_structdef *sd)
{
	unsigned char name[MAXVARLEN + 1];
	int namelen = 0;
	int type = T_NOTYPE;
	int size = 0;
	int offset;
	struct s_structmember *sm;
	short dims[MAXDIM] = {0};
	int ndims = 0;
	int array_elements = 1;

	if (sd->num_members >= MAX_STRUCT_MEMBERS)
		return "Too many members in TYPE";

	skipspace(p);

	// Parse member name
	if (!isnamestart(*p))
		return "Invalid member definition in TYPE"; // Not a valid member definition

	while (isnamechar(*p) && *p != '(' && namelen < MAXVARLEN)
	{
		name[namelen++] = mytoupper(*p++);
	}
	name[namelen] = 0;

	skipspace(p);

	// Check for array dimensions
	if (*p == '(')
	{
		p++; // Skip opening parenthesis
		skipspace(p);

		while (*p && *p != ')' && ndims < MAXDIM)
		{
			// Parse dimension value manually (can't use getint during preprocess)
			int dim = 0;
			int have_digits = 0;
			while (*p >= '0' && *p <= '9')
			{
				dim = dim * 10 + (*p - '0');
				have_digits = 1;
				p++;
			}
			if (!have_digits)
				return "Dimensions";
			if (dim <= g_OptionBase)
				return "Dimensions";
			dims[ndims++] = dim;
			array_elements *= (dim + 1 - g_OptionBase); // Account for OPTION BASE

			skipspace(p);
			if (*p == ',')
			{
				p++;
				skipspace(p);
			}
		}

		if (*p == ')')
			p++; // Skip closing parenthesis
		skipspace(p);
	}

	skipspace(p);

	// Expect AS keyword (tokenized or literal)
	if (*p == tokenAS)
	{
		p++; // Skip past token
	}
	else if ((p[0] == 'A' || p[0] == 'a') && (p[1] == 'S' || p[1] == 's') && !isnamechar(p[2]))
	{
		p += 2; // Skip past literal AS
	}
	else
	{
		return "Invalid member definition in TYPE"; // Not a valid member definition (no AS keyword)
	}
	skipspace(p);

	// Parse type (checkstring handles both tokenized and literal forms)
	unsigned char *tp;
	if ((tp = checkstring(p, (unsigned char *)"INTEGER")) != NULL ||
		(tp = checkstring(p, (unsigned char *)"INT")) != NULL)
	{
		type = T_INT;
		size = sizeof(long long int);
		p = tp;
	}
	else if ((tp = checkstring(p, (unsigned char *)"FLOAT")) != NULL)
	{
		type = T_NBR;
		size = sizeof(MMFLOAT);
		p = tp;
	}
	else if ((tp = checkstring(p, (unsigned char *)"STRING")) != NULL)
	{
		type = T_STR;
		p = tp;
		skipspace(p);
		// Check for STRING LENGTH n (consistent with DIM syntax)
		if ((tp = checkstring(p, (unsigned char *)"LENGTH")) != NULL)
		{
			p = tp;
			skipspace(p);
			// Parse the size number manually (can't use getint during preprocess)
			size = 0;
			while (*p >= '0' && *p <= '9')
			{
				size = size * 10 + (*p - '0');
				p++;
			}
			if (size < 1)
				size = 1;
			if (size > MAXSTRLEN)
				size = MAXSTRLEN;
		}
		else
		{
			size = MAXSTRLEN; // Default string length
		}
	}
	else
	{
		// Check if it's a previously defined structure type
		unsigned char typename[MAXVARLEN + 1];
		int typenamelen = 0;
		unsigned char *tp2 = p;

		// Parse the type name
		while (isnamechar(*tp2) && typenamelen < MAXVARLEN)
		{
			typename[typenamelen++] = mytoupper(*tp2++);
		}
		typename[typenamelen] = 0;

		if (typenamelen > 0)
		{
			// Search for this type name in already-defined struct types
			int nested_idx = -1;
			for (int i = 0; i < g_structcnt; i++)
			{
				if (g_structtbl[i] != NULL && strcmp((char *)typename, (char *)g_structtbl[i]->name) == 0)
				{
					nested_idx = i;
					break;
				}
			}

			if (nested_idx >= 0)
			{
				// Found a nested structure type
				type = T_STRUCT;
				size = nested_idx; // Store struct type index in size field
				p = tp2;
			}
			else
			{
				return "Unknown type in TYPE definition";
			}
		}
		else
		{
			return "Unknown type in TYPE definition";
		}
	}

	// Calculate offset (align to natural boundary)
	offset = sd->total_size;
	// Align integers, floats, and nested structures to 8-byte boundary
	if ((type == T_INT || type == T_NBR || type == T_STRUCT) && (offset % 8) != 0)
	{
		offset = ((offset / 8) + 1) * 8;
	}

	// Add the member
	sm = &sd->members[sd->num_members];
	memcpy(sm->name, name, namelen + 1);
	sm->type = type;
	sm->size = size;
	sm->offset = offset;

	// Store array dimensions
	for (int i = 0; i < MAXDIM; i++)
	{
		sm->dims[i] = dims[i];
	}

	sd->num_members++;

	// Update total size (accounting for array elements)
	if (type == T_STR)
		sd->total_size = offset + (size + 1) * array_elements; // +1 for length byte per element
	else if (type == T_STRUCT)
	{
		// For nested structures, size field contains the struct type index
		int nested_size = g_structtbl[size]->total_size;
		sd->total_size = offset + nested_size * array_elements;
	}
	else
		sd->total_size = offset + size * array_elements;

	return NULL; // Successfully parsed a member (NULL means no error)
}

// Helper function to find a structure type by name (RP2350 only)
#ifdef rp2350
int FindStructType(unsigned char *name)
#else
int MIPS16 FindStructType(unsigned char *name)
#endif
{
	int i, namelen = 0;
	unsigned char uname[MAXVARLEN + 1];

	// Convert to uppercase for comparison
	while (isnamechar(*name) && *name != '.' && namelen < MAXVARLEN)
	{
		uname[namelen++] = mytoupper(*name++);
	}
	uname[namelen] = 0;

	for (i = 0; i < g_structcnt; i++)
	{
		if (strcmp((char *)uname, (char *)g_structtbl[i]->name) == 0)
			return i;
	}
	return -1;
}

// Helper function to find a member within a structure definition (RP2350 only)
// Returns member index or -1 if not found
// Also sets *member_type, *member_offset, *member_size if found
// member_dims should point to array of MAXDIM shorts, will be filled with dimensions
#ifdef rp2350
int FindStructMember(int struct_idx, unsigned char *membername, int *member_type, int *member_offset, int *member_size, short *member_dims)
#else
int MIPS16 FindStructMember(int struct_idx, unsigned char *membername, int *member_type, int *member_offset, int *member_size, short *member_dims)
#endif
{
	int i, namelen = 0;
	unsigned char uname[MAXVARLEN + 1];
	struct s_structdef *sd;

	if (struct_idx < 0 || struct_idx >= g_structcnt)
		return -1;

	sd = g_structtbl[struct_idx];

	// Convert member name to uppercase (stop at '.', '(' or end of name)
	while (isnamechar(*membername) && *membername != '.' && *membername != '(' && namelen < MAXVARLEN)
	{
		uname[namelen++] = mytoupper(*membername++);
	}
	uname[namelen] = 0;

	// Search for member
	for (i = 0; i < sd->num_members; i++)
	{
		if (strcmp((char *)uname, (char *)sd->members[i].name) == 0)
		{
			if (member_type)
				*member_type = sd->members[i].type;
			if (member_offset)
				*member_offset = sd->members[i].offset;
			if (member_size)
				*member_size = sd->members[i].size;
			if (member_dims)
			{
				for (int j = 0; j < MAXDIM; j++)
				{
					member_dims[j] = sd->members[i].dims[j];
				}
			}
			return i;
		}
	}
	return -1;
}

// STRUCT(FIND array(), membername$, value [, start])
// General structure function with subfunctions
// FIND: Searches a struct array for an element where the specified member equals the value
//       Returns the index of the first match (starting from 'start'), or -1 if not found
//       Optional 'start' parameter allows iteration through multiple matches
#ifdef rp2350
void fun_struct(void)
#else
void MIPS16 fun_struct(void)
#endif
{
	unsigned char *p;

	// Check for FIND subfunction: STRUCT(FIND array().member, value [, start] [, size])
	// argc==3: array().member, value (simple match)
	// argc==5: array().member, value, start (simple match with start index)
	// argc==7: array().member, value [, start], size (regex match, start optional)
	if ((p = checkstring(ep, (unsigned char *)"FIND")) != NULL)
	{
		unsigned char *member_base;
		int var_idx, struct_type, struct_size;
		int member_type, member_offset, member_size;
		int num_elements, i, start_idx;
		int use_regex = 0;
		void *size_var = NULL;
		int64_t *size_var_int = NULL;
		MMFLOAT *size_var_float = NULL;

		// Parse remaining arguments: array().member, value [, start] [, size]
		getcsargs(&p, 7); // array().member, value [, start] [, size] = up to 7 tokens
		if (argc != 3 && argc != 5 && argc != 7)
			error("Syntax: STRUCT(FIND array().member, value [, start] [, size])");

		// Determine if regex mode (argc==7 means regex with size variable)
		if (argc == 7)
		{
#ifdef PICOMITEMIN
			error("Regular expressions not supported on PICOMITEMIN");
#endif
			use_regex = 1;
		}

		// Get the struct array member variable (argv[0])
		// This uses findvar which will populate g_StructMemberOffset and g_StructMemberSize
		member_base = findvar(argv[0], V_FIND | V_NOFIND_ERR | V_EMPTY_OK);
		var_idx = g_VarIndex;

		if (!(g_vartbl[var_idx].type & T_STRUCT))
			error("Expected a structure array");

		if (g_vartbl[var_idx].dims[0] == 0)
			error("Expected an array of structures");

		// Check that a member was specified (findvar sets g_StructMemberOffset)
		if (g_StructMemberOffset < 0)
			error("Must specify a member (e.g., array().membername)");

		struct_type = (int)g_vartbl[var_idx].size;
		struct_size = g_structtbl[struct_type]->total_size;
		member_offset = g_StructMemberOffset;
		member_size = g_StructMemberSize;

		// Get array base pointer (member_base points to member in element 0)
		unsigned char *array_base = member_base - member_offset;

		// Calculate number of elements
		num_elements = 1;
		for (int d = 0; d < MAXDIM && g_vartbl[var_idx].dims[d] != 0; d++)
		{
			num_elements *= (g_vartbl[var_idx].dims[d] + 1 - g_OptionBase);
		}

		// Determine member type from member_size and structure definition
		// member_size > 8 means string (includes length byte)
		if (member_size > 8)
		{
			member_type = T_STR;
		}
		else
		{
			// Check if the member is integer or float by looking at the structure definition
			struct s_structdef *sd = g_structtbl[struct_type];
			member_type = T_NBR; // default
			for (int m = 0; m < sd->num_members; m++)
			{
				if (sd->members[m].offset == member_offset)
				{
					member_type = sd->members[m].type & (T_INT | T_NBR | T_STR);
					break;
				}
			}
		}

		// Get search value and determine its type (argv[2])
		MMFLOAT f;
		long long int i64;
		unsigned char *s = NULL;
		int t = T_NOTYPE;
		evaluate(argv[2], &f, &i64, &s, &t, false);

		// Get optional start index (argv[4] if provided and non-empty)
		// For regex mode (argc==7): start is in argv[4] (optional), size var is in argv[6]
		// For simple mode (argc==5): start is in argv[4]
		start_idx = 0;
		if (argc >= 5 && *argv[4])
		{
			start_idx = getint(argv[4], g_OptionBase, g_OptionBase + num_elements) - g_OptionBase;
			// If start is past end of array, return -1 immediately
			if (start_idx >= num_elements)
			{
				targ = T_INT;
				iret = -1;
				return;
			}
		}

		// For regex mode, get the size variable (argv[6])
		if (use_regex)
		{
			if (member_type != T_STR)
				error("Regex search only works with string members");
			if (!(t & T_STR))
				error("Regex pattern must be a string");

			int size_vtype;
			size_var = findvar(argv[6], V_FIND);
			size_vtype = g_vartbl[g_VarIndex].type;
#ifdef STRUCTENABLED
			if (g_StructMemberType != 0)
				size_vtype = g_StructMemberType;
#endif
			if (!(size_vtype & (T_NBR | T_INT)))
				error("Size variable must be numeric");
			if (size_vtype & T_INT)
				size_var_int = size_var;
			else
				size_var_float = size_var;
		}

		// Search the array starting from start_idx
		targ = T_INT;
		for (i = start_idx; i < num_elements; i++)
		{
			unsigned char *elem_ptr = array_base + (i * struct_size);
			unsigned char *member_ptr = elem_ptr + member_offset;

			if (member_type == T_INT)
			{
				long long int val = *(long long int *)member_ptr;
				long long int search_val = 0;
				if (t & T_INT)
					search_val = i64;
				else if (t & T_NBR)
					search_val = (long long int)f;
				else
					error("Type mismatch: expected numeric value");

				if (val == search_val)
				{
					iret = i + g_OptionBase;
					return;
				}
			}
			else if (member_type == T_NBR)
			{
				MMFLOAT val = *(MMFLOAT *)member_ptr;
				MMFLOAT search_val = 0;
				if (t & T_NBR)
					search_val = f;
				else if (t & T_INT)
					search_val = (MMFLOAT)i64;
				else
					error("Type mismatch: expected numeric value");

				if (val == search_val)
				{
					iret = i + g_OptionBase;
					return;
				}
			}
			else if (member_type == T_STR)
			{
				unsigned char *val = member_ptr; // Points to length byte

				if (use_regex)
				{
					// Regex search mode
					int match_length;
					char *text_cstr = GetTempMemory(STRINGSIZE);
					char *pattern_cstr = GetTempMemory(STRINGSIZE);

					// Convert MMBasic string to C string
					memcpy(text_cstr, val + 1, *val);
					text_cstr[*val] = '\0';

					// Convert pattern (s is MMBasic string)
					memcpy(pattern_cstr, s + 1, *s);
					pattern_cstr[*s] = '\0';

					int tmp = OptionEscape;
					OptionEscape = 0;
					int match_idx = re_match(pattern_cstr, text_cstr, &match_length);
					OptionEscape = tmp;

					if (match_idx != -1)
					{
						// Found a match - update size variable with match length
						if (size_var_float)
							*size_var_float = (MMFLOAT)match_length;
						else
							*size_var_int = (int64_t)match_length;
						iret = i + g_OptionBase;
						return;
					}
				}
				else
				{
					// Simple string match
					if (!(t & T_STR))
						error("Type mismatch: expected string value");

					// Compare strings (MMBasic string format: first byte is length)
					if (*val == *s && memcmp(val + 1, s + 1, *s) == 0)
					{
						iret = i + g_OptionBase;
						return;
					}
				}
			}
		}

		// Not found - set size to 0 for regex mode
		if (use_regex)
		{
			if (size_var_float)
				*size_var_float = 0.0;
			else
				*size_var_int = 0;
		}
		iret = -1;
		return;
	}

	// Check for OFFSET subfunction: STRUCT(OFFSET typename$, element$)
	if ((p = checkstring(ep, (unsigned char *)"OFFSET")) != NULL)
	{
		unsigned char *typename_str, *element_str;
		int i, member_type, member_offset, member_size;

		// Parse remaining arguments: typename$, element$
		getcsargs(&p, 3); // typename$, element$ = 3 tokens
		if (argc != 3)
			error("Syntax: STRUCT(OFFSET typename$, element$)");

		// Get the structure type name (argv[0])
		typename_str = getstring(argv[0]);

		// Get the element/member name (argv[2])
		element_str = getstring(argv[2]);

		// Search for the structure type by name
		for (i = 0; i < g_structcnt; i++)
		{
			if (g_structtbl[i] != NULL &&
				strlen((char *)g_structtbl[i]->name) == *typename_str &&
				strncasecmp((char *)g_structtbl[i]->name, (char *)(typename_str + 1), *typename_str) == 0)
			{
				// Found the structure type, now find the member
				int member_idx = FindStructMember(i, element_str + 1, &member_type, &member_offset, &member_size, NULL);
				if (member_idx < 0)
					error("Member not found in structure");
				targ = T_INT;
				iret = member_offset;
				return;
			}
		}

		error("Structure type not found");
	}

	// Check for SIZEOF subfunction: STRUCT(SIZEOF typename$)
	if ((p = checkstring(ep, (unsigned char *)"SIZEOF")) != NULL)
	{
		unsigned char *typename_str;
		int i;

		// Parse remaining argument: typename$
		getcsargs(&p, 1); // typename$ = 1 token
		if (argc != 1)
			error("Syntax: STRUCT(SIZEOF typename$)");

		// Get the structure type name (argv[0])
		typename_str = getstring(argv[0]);

		// Search for the structure type by name
		for (i = 0; i < g_structcnt; i++)
		{
			if (g_structtbl[i] != NULL &&
				strlen((char *)g_structtbl[i]->name) == *typename_str &&
				strncasecmp((char *)g_structtbl[i]->name, (char *)(typename_str + 1), *typename_str) == 0)
			{
				targ = T_INT;
				iret = g_structtbl[i]->total_size;
				return;
			}
		}

		error("Structure type not found");
	}

	// Check for TYPE subfunction: STRUCT(TYPE typename$, element$)
	// Returns T_INT, T_NBR or T_STR for the base type of the element
	if ((p = checkstring(ep, (unsigned char *)"TYPE")) != NULL)
	{
		unsigned char *typename_str, *element_str;
		int i, member_type, member_offset, member_size;

		// Parse remaining arguments: typename$, element$
		getcsargs(&p, 3); // typename$, element$ = 3 tokens
		if (argc != 3)
			error("Syntax: STRUCT(TYPE typename$, element$)");

		// Get the structure type name (argv[0])
		typename_str = getstring(argv[0]);

		// Get the element/member name (argv[2])
		element_str = getstring(argv[2]);

		// Search for the structure type by name
		for (i = 0; i < g_structcnt; i++)
		{
			if (g_structtbl[i] != NULL &&
				strlen((char *)g_structtbl[i]->name) == *typename_str &&
				strncasecmp((char *)g_structtbl[i]->name, (char *)(typename_str + 1), *typename_str) == 0)
			{
				// Found the structure type, now find the member
				int member_idx = FindStructMember(i, element_str + 1, &member_type, &member_offset, &member_size, NULL);
				if (member_idx < 0)
					error("Member not found in structure");
				targ = T_INT;
				// Return only the base type (T_INT, T_NBR or T_STR)
				iret = member_type & (T_INT | T_NBR | T_STR);
				return;
			}
		}

		error("Structure type not found");
	}

	error("Unknown STRUCT subfunction");
}
#endif // rp2350

/**
 * @cond
 * The following section will be excluded from the documentation.
 */

// utility function used by llist() below
// it copys a command or function honouring the case selected by the user
void strCopyWithCase(char *d, char *s)
{
	if (Option.Listcase == CONFIG_LOWER)
	{
		while (*s)
			*d++ = tolower(*s++);
	}
	else if (Option.Listcase == CONFIG_UPPER)
	{
		while (*s)
			*d++ = mytoupper(*s++);
	}
	else
	{
		while (*s)
			*d++ = *s++;
	}
	*d = 0;
}

void replaceAlpha(char *str, const char *replacements[MMEND])
{
	char buffer[STRINGSIZE]; // Buffer to store the modified string
	int bufferIndex = 0;
	int len = strlen(str);
	int i = 0;

	while (i < len)
	{
		// Check for the pattern "~(X)" where X is an uppercase letter
		if (i < len - 3 && str[i] == '~' && str[i + 1] == '(' && isupper((int)str[i + 2]) && str[i + 3] == ')')
		{
			char alpha = str[i + 2];							 // Extract the letter 'alpha'
			const char *replacement = replacements[alpha - 'A']; // Get the replacement string

			// Copy the replacement string into the buffer
			strcpy(&buffer[bufferIndex], replacement);
			bufferIndex += strlen(replacement);

			i += 4; // Move past "~(X)"
		}
		else
		{
			// Copy the current character to the buffer
			buffer[bufferIndex++] = str[i++];
		}
	}

	buffer[bufferIndex] = '\0'; // Null-terminate the buffer
	strcpy(str, buffer);		// Copy the buffer back into the original string
}
int format_string(char *c, int n)
{
	int count = 0;
	n -= 2;
	int len = strlen(c);
	if (*c == 0)
		return 0;
	char *result = GetMemory(len * 2); // Allocate enough space for the modified string
	int pos = 0, start = 0;

	while (start < len)
	{
		int split_pos = start + n - 1;

		if (split_pos >= len)
		{ // If remaining text fits in one line
			strcpy(result + pos, c + start);
			pos += strlen(c + start);
			break;
		}

		while (split_pos > start && !(c[split_pos] == ' ' || c[split_pos] == ','))
		{
			split_pos--; // Try to find a space to break at
		}

		if (split_pos == start)
		{
			split_pos = start + n - 1; // No space found, force a split
		}

		strncpy(result + pos, c + start, split_pos - start + 1);
		pos += (split_pos - start + 1);

		start = split_pos + 1;

		if (start < len)
		{ // Only add underscore if not the last substring
			result[pos++] = ' ';
			result[pos++] = Option.continuation;
			result[pos++] = '\n';
			count++;
		}
	}

	result[pos] = '\0';
	strcpy(c, result);
	FreeMemory((void *)result);
	return count;
}
// list a line into a buffer (b) given a pointer to the beginning of the line (p).
// the returned string is a C style string (terminated with a zero)
// this is used by cmd_list(), cmd_edit() and cmd_xmodem()
unsigned char *llist(unsigned char *b, unsigned char *p)
{
	int i, firstnonwhite = true;
	unsigned char *b_start = b;

	while (1)
	{
		if (*p == T_NEWLINE)
		{
			p++;
			firstnonwhite = true;
			continue;
		}

		if (*p == T_LINENBR)
		{
			i = (((p[1]) << 8) | (p[2])); // get the line number
			p += 3;						  // and step over the number
			IntToStr((char *)b, i, 10);
			b += strlen((char *)b);
			if (*p != ' ')
				*b++ = ' ';
		}

		if (*p == T_LABEL)
		{ // got a label
			for (i = p[1], p += 2; i > 0; i--)
				*b++ = *p++; // copy to the buffer
			*b++ = ':';		 // terminate with a colon
			if (*p && *p != ' ')
				*b++ = ' '; // and a space if necessary
			firstnonwhite = true;
		} // this deliberately drops through in case the label is the only thing on the line

		if (*p >= C_BASETOKEN)
		{
			if (firstnonwhite)
			{
				CommandToken tkn = commandtbl_decode(p);
				if (tkn == GetCommandValue((unsigned char *)"Let"))
					*b = 0; // use nothing if it LET
				else
				{
					strCopyWithCase((char *)b, (char *)commandname(tkn)); // expand the command (if it is not LET)
					if (*b == '_')
					{
						if (!strncasecmp((char *)&b[1], "SIDE SET", 8) ||
							!strncasecmp((char *)&b[1], "END PROGRAM", 11) ||
							!strncasecmp((char *)&b[1], "WRAP", 4) ||
							!strncasecmp((char *)&b[1], "LINE", 4) ||
							!strncasecmp((char *)&b[1], "PROGRAM", 7) ||
							!strncasecmp((char *)&b[1], "LABEL", 5))
							*b = '.';
						else if (b[1] == '(')
							*b = '&';
					}
					b += strlen((char *)b); // update pointer to the end of the buffer
					if (isalpha(*(b - 1)))
						*b++ = ' '; // add a space to the end of the command name
				}
				firstnonwhite = false;
				p += sizeof(CommandToken);
			}
			else
			{													   // not a command so must be a token
				strCopyWithCase((char *)b, (char *)tokenname(*p)); // expand the token
				b += strlen((char *)b);							   // update pointer to the end of the buffer
				if (*p == tokenTHEN || *p == tokenELSE)
					firstnonwhite = true;
				else
					firstnonwhite = false;
				p++;
			}
			continue;
		}

		// hey, an ordinary char, just copy it to the output
		if (*p)
		{
			*b = *p; // place the char in the buffer
			if (*p != ' ')
				firstnonwhite = false;
			p++;
			b++; // move the pointers
			continue;
		}

		// at this point the char must be a zero
		// zero char can mean both a separator or end of line
		if (!(p[1] == T_NEWLINE || p[1] == 0))
		{
			*b++ = ':'; // just a separator
			firstnonwhite = true;
			p++;
			continue;
		}

		// must be the end of a line - so return to the caller
		while (*(b - 1) == ' ' && b > b_start)
			--b; // eat any spaces on the end of the line
		*b = 0;
		replaceAlpha((char *)b_start, overlaid_functions); // replace the user version of all the MM. functions
		STR_REPLACE((char *)b_start, "PEEK(INT8", "PEEK(BYTE", 0);
		return ++p;
	} // end while
}

void execute_one_command(unsigned char *p)
{
	int i;

	CheckAbort();
	targ = T_CMD;
	skipspace(p); // skip any whitespace
	if (p[0] >= C_BASETOKEN && p[1] >= C_BASETOKEN)
	{
		//                    if(*(char*)p >= C_BASETOKEN && *(char*)p - C_BASETOKEN < CommandTableSize - 1 && (commandtbl[*(char*)p - C_BASETOKEN].type & T_CMD)) {
		CommandToken cmd = commandtbl_decode(p);
		if (cmd == cmdWHILE || cmd == cmdDO || cmd == cmdFOR)
			error("Invalid inside THEN ... ELSE");
		cmdtoken = cmd;
		cmdline = p + sizeof(CommandToken);
		skipspace(cmdline);
		commandtbl[cmd].fptr(); // execute the command
	}
	else
	{
		if (!isnamestart(*p))
			error("Invalid character");
		i = FindSubFun(p, false); // it could be a defined command
		if (i >= 0)				  // >= 0 means it is a user defined command
			DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
		else
			StandardError(36);
	}
	ClearTempMemory(); // at the end of each command we need to clear any temporary string vars
}

void execute(char *mycmd)
{
	//    char *temp_tknbuf;
	unsigned char *ttp = NULL;
	int i = 0, toggle = 0;
	//    temp_tknbuf = GetTempStrMemory();
	//    strcpy(temp_tknbuf, tknbuf);
	// first save the current token buffer in case we are in immediate mode
	// we have to fool the tokeniser into thinking that it is processing a program line entered at the console
	skipspace(mycmd);
	strcpy((char *)inpbuf, (const char *)getCstring((unsigned char *)mycmd)); // then copy the argument
	if (!(mytoupper(inpbuf[0]) == 'R' && mytoupper(inpbuf[1]) == 'U' && mytoupper(inpbuf[2]) == 'N'))
	{ // convert the string to upper case
		while (inpbuf[i])
		{
			if (inpbuf[i] == 34)
			{
				if (toggle == 0)
					toggle = 1;
				else
					toggle = 0;
			}
			if (!toggle)
			{
				if (inpbuf[i] == ':')
					error((char *)"Only single statements allowed");
				inpbuf[i] = mytoupper(inpbuf[i]);
			}
			i++;
		}
		multi = false;
		tokenise(true); // and tokenise it (the result is in tknbuf)
		memset(inpbuf, 0, STRINGSIZE);
		tknbuf[strlen((char *)tknbuf)] = 0;
		tknbuf[strlen((char *)tknbuf) + 1] = 0;
		if (CurrentLinePtr)
			ttp = nextstmt; // save the globals used by commands
		ScrewUpTimer = 1000;
		ExecuteProgram(tknbuf); // execute the function's code
		ScrewUpTimer = 0;
		// g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
		if (CurrentLinePtr)
			nextstmt = ttp;
		return;
	}
	else
	{
		unsigned char *p = inpbuf;
		//		char* q;
		//		char fn[STRINGSIZE] = { 0 };
		unsigned short tkn = GetCommandValue((unsigned char *)"RUN");
		tknbuf[0] = (tkn & 0x7f) + C_BASETOKEN;
		tknbuf[1] = (tkn >> 7) + C_BASETOKEN; // tokens can be 14-bit
		p[0] = (tkn & 0x7f) + C_BASETOKEN;
		p[1] = (tkn >> 7) + C_BASETOKEN; // tokens can be 14-bit
		memmove(&p[2], &p[4], strlen((char *)p) - 4);
		/*		if ((q = strchr((char *)p, ':'))) {
					q--;
					*q = '0';
				}*/
		p[strlen((char *)p) - 2] = 0;
		//		MMPrintString(fn); PRet();
		//		CloseAudio(1);
		strcpy((char *)tknbuf, (char *)inpbuf);
		if (CurrentlyPlaying != P_NOTHING)
			CloseAudio(1);
		longjmp(jmprun, 1);
	}
}
/** @endcond */

void cmd_execute(void)
{
	execute((char *)cmdline);
}
