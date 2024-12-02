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

/* ********************************************************************************
 All other tokens (keywords, functions, operators) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_TOKEN_TABLE


#endif


#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
    // General definitions used by other modules

    #ifndef MISC_HEADER
    #define MISC_HEADER

   	extern void OtherOptions(void);
	extern int InterruptUsed;
	extern int OptionErrorCheck;

    extern unsigned char *InterruptReturn;
    extern int check_interrupt(void);
    extern unsigned char *GetIntAddress(unsigned char *p);
    extern void CrunchData(unsigned char **p, int c);
	extern uint32_t getFreeHeap(void);
    // struct for the interrupt configuration
    #define T_LOHI   1
    #define T_HILO   2
    #define T_BOTH   3
    struct s_inttbl {
            int pin;                                   // the pin on which the interrupt is set
            int last;					// the last value of the pin (ie, hi or low)
            char *intp;					// pointer to the interrupt routine
            int lohi;                                  // trigger condition (T_LOHI, T_HILO, etc).
    };
    #define NBRINTERRUPTS	    10			// number of interrupts that can be set
    extern struct s_inttbl inttbl[NBRINTERRUPTS];

    extern int TickPeriod[NBRSETTICKS];
    extern volatile int TickTimer[NBRSETTICKS];
    extern unsigned char *TickInt[NBRSETTICKS];
	extern volatile unsigned char TickActive[NBRSETTICKS];
	extern unsigned int CurrentCpuSpeed;
	extern unsigned int PeripheralBusSpeed;
	extern unsigned char *OnKeyGOSUB;
	extern unsigned char *OnPS2GOSUB;
	extern unsigned char EchoOption;
	extern unsigned int GetPeekAddr(unsigned char *p);
	extern unsigned int GetPokeAddr(unsigned char *p);
	extern void disable_sd(void);
	extern void disable_systemspi(void);
	extern void disable_systemi2c(void);
	extern void disable_audio(void);
	extern char *MQTTInterrupt;
	extern volatile bool MQTTComplete;
	extern char *CSubInterrupt;
	extern volatile bool CSubComplete;
	extern uint32_t __get_MSP(void);
	extern int ExistsFile(char *p);
	extern int ExistsDir(char *p, char *q, int *filesystem);
	extern MMFLOAT optionangle;
	extern bool optionfastaudio;
	extern bool optionlogging;
	extern bool optionsuppressstatus;
	extern bool optionfulltime;
	extern int64_t TimeOffsetToUptime;
	extern time_t get_epoch(int year, int month,int day, int hour,int minute, int second);
	extern uint_fast64_t gettimefromepoch(int *year, int *month, int *day, int *hour, int *minute, int *second);
#endif
#endif
/*  @endcond */
