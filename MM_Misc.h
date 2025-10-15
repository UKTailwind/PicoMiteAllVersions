/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

MM_Misc.h

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

#ifndef MISC_HEADER
#define MISC_HEADER

/* ============================================================================
 * Token table section
 * ============================================================================ */
#ifdef INCLUDE_TOKEN_TABLE
/* All other tokens (keywords, functions, operators) should be inserted in this table */
#endif

/* ============================================================================
 * Main header content
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

/* ============================================================================
 * Constants - Interrupt trigger conditions
 * ============================================================================ */
#define T_LOHI 1 // Trigger on low to high transition
#define T_HILO 2 // Trigger on high to low transition
#define T_BOTH 3 // Trigger on both transitions

#define NBRINTERRUPTS 10 // Number of interrupts that can be set

/* ============================================================================
 * Type definitions - Interrupt configuration
 * ============================================================================ */
struct s_inttbl
{
	int pin;	// Pin on which the interrupt is set
	int last;	// Last value of the pin (high or low)
	char *intp; // Pointer to the interrupt routine
	int lohi;	// Trigger condition (T_LOHI, T_HILO, T_BOTH)
};

/* ============================================================================
 * External variables - Interrupt management
 * ============================================================================ */
extern struct s_inttbl inttbl[NBRINTERRUPTS];
extern unsigned char *InterruptReturn;
extern int InterruptUsed;

/* ============================================================================
 * External variables - Tick timer configuration
 * ============================================================================ */
extern int TickPeriod[NBRSETTICKS];
extern volatile int TickTimer[NBRSETTICKS];
extern unsigned char *TickInt[NBRSETTICKS];
extern volatile unsigned char TickActive[NBRSETTICKS];

/* ============================================================================
 * External variables - CPU and peripheral configuration
 * ============================================================================ */
extern unsigned int CurrentCpuSpeed;
extern unsigned int PeripheralBusSpeed;

/* ============================================================================
 * External variables - Input event handlers
 * ============================================================================ */
extern unsigned char *OnKeyGOSUB;
extern unsigned char *OnPS2GOSUB;
extern unsigned char EchoOption;

/* ============================================================================
 * External variables - MQTT and CSub completion
 * ============================================================================ */
extern char *MQTTInterrupt;
extern volatile bool MQTTComplete;
extern char *CSubInterrupt;
extern volatile bool CSubComplete;

/* ============================================================================
 * External variables - Options and settings
 * ============================================================================ */
extern int OptionErrorCheck;
extern MMFLOAT optionangle;
extern bool useoptionangle;
extern bool optionfastaudio;
extern bool optionlogging;
extern bool optionsuppressstatus;
extern bool optionfulltime;

/* ============================================================================
 * External variables - Time and date
 * ============================================================================ */
extern int64_t TimeOffsetToUptime;

/* ============================================================================
 * External variables - Persistent storage
 * ============================================================================ */
extern uint64_t __uninitialized_ram(_persistent);

/* ============================================================================
 * Function declarations - Interrupt handling
 * ============================================================================ */
int check_interrupt(void);
unsigned char *GetIntAddress(unsigned char *p);

/* ============================================================================
 * Function declarations - Memory and system
 * ============================================================================ */
uint32_t getFreeHeap(void);
uint32_t __get_MSP(void);

/* ============================================================================
 * Function declarations - Address operations
 * ============================================================================ */
unsigned int GetPeekAddr(unsigned char *p);
unsigned int GetPokeAddr(unsigned char *p);

/* ============================================================================
 * Function declarations - Data operations
 * ============================================================================ */
void CrunchData(unsigned char **p, int c);

/* ============================================================================
 * Function declarations - Hardware disable
 * ============================================================================ */
void disable_sd(void);
void disable_systemspi(void);
void disable_systemi2c(void);
void disable_audio(void);

/* ============================================================================
 * Function declarations - File system operations
 * ============================================================================ */
int ExistsFile(char *p);
int ExistsDir(char *p, char *q, int *filesystem);

/* ============================================================================
 * Function declarations - Time and date utilities
 * ============================================================================ */
time_t get_epoch(int year, int month, int day, int hour, int minute, int second);
uint_fast64_t gettimefromepoch(int *year, int *month, int *day, int *hour, int *minute, int *second);

/* ============================================================================
 * Function declarations - Configuration
 * ============================================================================ */
void OtherOptions(void);
void printoptions(void);

#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

#endif /* MISC_HEADER */

/*  @endcond */