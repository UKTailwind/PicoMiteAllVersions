/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Hardware_Includes.h

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
#include "AllCommands.h"
#include "Memory.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#ifdef PICOMITEVGA
#include "pico/multicore.h"
#endif
#include "lfs.h"



#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#include <stdio.h>
#include <stddef.h>
#include "Version.h"
#include "configuration.h"
#include "FileIO.h"
#include "ff.h"
// global variables used in MMBasic but must be maintained outside of the interpreter
extern int MMerrno;
extern int ListCnt;
extern int MMCharPos;
extern unsigned char *StartEditPoint;
extern int StartEditChar;
extern int OptionErrorSkip;
extern int ExitMMBasicFlag;
extern unsigned char *InterruptReturn;
extern unsigned int _excep_peek;
extern volatile long long int mSecTimer;
extern volatile unsigned int PauseTimer;
extern volatile unsigned int IntPauseTimer;
extern volatile unsigned int Timer1, Timer2, Timer3, Timer4, Timer5;		                       //1000Hz decrement timer
extern volatile unsigned int diskchecktimer;
extern volatile int ds18b20Timer;
extern volatile int CursorTimer;
extern volatile unsigned int I2CTimer;
#ifndef USBKEYBOARD
extern volatile unsigned int MouseTimer;
extern void initMouse0(int sensitivity);
extern bool mouse0;
extern int MOUSE_CLOCK,MOUSE_DATA;
#endif
//extern volatile int second;
//extern volatile int minute;
//extern volatile int hour;
//extern volatile int day;
//extern volatile int month;
//extern volatile int year;
extern volatile unsigned int SecondsTimer;
extern volatile int day_of_week;
extern unsigned char WatchdogSet;
extern unsigned char IgnorePIN;
extern MMFLOAT VCC;
extern volatile unsigned int WDTimer;                               // used for the watchdog timer
extern unsigned char PulsePin[];
extern unsigned char PulseDirection[];
extern int PulseCnt[];
extern int PulseActive;
extern volatile int ClickTimer;
extern int calibrate;
extern volatile unsigned int InkeyTimer;                            // used to delay on an escape character
extern volatile int DISPLAY_TYPE;
extern void routinechecks(void);
extern volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE];
extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
extern volatile char ConsoleTxBuf[CONSOLE_TX_BUF_SIZE];
extern volatile int ConsoleTxBufHead;
extern volatile int ConsoleTxBufTail;
extern unsigned char SPIatRisk;
extern uint8_t RGB121(uint32_t c);
extern uint8_t RGB332(uint32_t c);
extern uint16_t RGB555(uint32_t c);
#ifndef rp2350
extern datetime_t rtc_t;
#else
extern bool rp2350a;
extern uint32_t PSRAMsize;
extern const uint32_t MAP16DEF[16];
extern void _Z10copy_wordsPKmPmm(uint32_t *s, uint32_t *d, int n);
#endif
#ifdef HDMI
extern uint16_t *tilefcols;
extern uint16_t *tilebcols;
extern uint8_t *tilefcols_w; 
extern uint8_t *tilebcols_w;
extern void settiles(void);
extern uint16_t map256[256];
extern uint16_t map16[16];
extern uint16_t map16d[16];
extern uint8_t map16s[16];
extern uint32_t map16q[16];
extern uint32_t map16pairs[16];
extern const uint32_t MAP256DEF[256];
extern volatile int32_t v_scanline;
#else
#ifdef rp2350
extern uint16_t *tilefcols;
extern uint16_t *tilebcols;
#else
extern uint16_t tilefcols[];
extern uint16_t tilebcols[];
#endif
#endif
extern void __not_in_flash_func(QVgaCore)(void);
extern uint32_t core1stack[];
extern int QVGA_CLKDIV;
extern int getslice(int pin);
extern void setpwm(int pin, int *PWMChannel, int *PWMSlice, MMFLOAT frequency, MMFLOAT duty);
struct s_PinDef {
	int pin;
	int GPno;
    char pinname[5];
    uint64_t mode;
    unsigned char ADCpin;
	unsigned char slice;
};
typedef struct s_HID {
	uint8_t Device_address;
	uint8_t Device_instance;
	uint8_t Device_type;
	uint8_t report_rate;
	int16_t report_timer;
	uint16_t vid;
	uint16_t pid;
	bool active;
	bool report_requested;
	bool notfirsttime;
	uint8_t motorleft;
	uint8_t motorright;
	uint8_t r,g,b;
	uint8_t sendlights;
	uint8_t report[65];
} a_HID;
extern volatile struct s_HID HID[4];
extern uint32_t _excep_code;
extern const struct s_PinDef PinDef[NBRPINS + 1];
#define VCHARS  25					// nbr of lines in the DOS box (used in LIST)

#define FILENAME_LENGTH 12

//extern unsigned char *ModuleTable[MAXMODULES];           // list of pointers to modules loaded in memory;
//extern int NbrModules;                          // the number of modules currently loaded
extern void mT4IntEnable(int status);
extern int BasicFileOpen(char *fname, int fnbr, int mode);
extern int kbhitConsole(void);
extern int InitSDCard(void);
extern void FileClose(int fnbr);
#define NBRERRMSG 17				// number of file error messages
extern void PRet(void);
extern void PInt(int64_t n);
extern void PIntComma(int64_t n);
extern void SRet(void);
extern void SInt(int64_t n);
extern void SPIClose(void);
extern void SPI2Close(void);
extern void SIntComma(int64_t n);
extern void PIntH(unsigned long long int n);
extern void PIntB(unsigned long long int n);
extern void PIntBC(unsigned long long int n);
extern void PIntHC(unsigned long long int n) ;
extern void PFlt(MMFLOAT flt);
extern void PFltComma(MMFLOAT n) ;
extern void putConsole(int c, int flush);
extern void MMPrintString(char* s);
extern void SSPrintString(char* s);
extern void myprintf(char *s);
extern int getConsole(void);
extern void InitReservedIO(void);
extern char SerialConsolePutC(char c, int flush);
extern void CallExecuteProgram(char *p);
extern long long int *GetReceiveDataBuffer(unsigned char *p, unsigned int *nbr);
extern int ticks_per_second;
extern volatile unsigned int GPSTimer;
extern uint16_t AUDIO_L_PIN, AUDIO_R_PIN, AUDIO_SLICE;
extern uint16_t AUDIO_WRAP;
extern int PromptFont, PromptFC, PromptBC;                             // the font and colours selected at the prompt
extern const uint8_t *flash_progmemory;
extern lfs_t lfs;
extern lfs_dir_t lfs_dir;
extern struct lfs_info lfs_info;
extern int FatFSFileSystem;
extern void uSec(int us);
extern int volatile ytileheight;
extern volatile int X_TILE, Y_TILE;
extern int CameraSlice;
extern int CameraChannel;
extern char id_out[];
extern uint8_t *buff320;
extern uint16_t SD_CLK_PIN,SD_MOSI_PIN,SD_MISO_PIN, SD_CS_PIN;
extern bool screen320;
extern void clear320(void);
#ifdef PICOMITEVGA
	extern volatile uint8_t transparent;
	extern volatile uint8_t transparents;
	extern volatile int RGBtransparent;
	extern uint16_t map16[16];
	#ifndef HDMI
		extern uint16_t __attribute__ ((aligned (256))) M_Foreground[16];
		extern uint16_t __attribute__ ((aligned (256))) M_Background[16];
		#ifdef rp2350
			extern uint16_t *tilefcols;
			extern uint16_t *tilebcols;
		#else
			extern uint16_t __attribute__ ((aligned (256))) tilefcols[80*40];
			extern uint16_t __attribute__ ((aligned (256))) tilebcols[80*40];
		#endif
		extern void VGArecovery(int pin);
	#else
	extern int MODE_H_SYNC_POLARITY, MODE_V_TOTAL_LINES, MODE_ACTIVE_LINES, MODE_ACTIVE_PIXELS;
	extern int MODE_H_ACTIVE_PIXELS, MODE_H_FRONT_PORCH, MODE_H_SYNC_WIDTH, MODE_H_BACK_PORCH;
	extern int MODE_V_SYNC_POLARITY ,MODE_V_ACTIVE_LINES ,MODE_V_FRONT_PORCH, MODE_V_SYNC_WIDTH, MODE_V_BACK_PORCH;
	#endif
	extern int MODE1SIZE;
	extern int MODE2SIZE;
	extern int MODE3SIZE;
	extern int MODE4SIZE;
	extern int MODE5SIZE;
#endif
#ifdef PICOMITEWEB
	extern volatile int WIFIconnected;
	extern volatile int scantimer;
	extern int startupcomplete;
	extern void ProcessWeb(int mode);
	extern void WebConnect(void);
	extern void close_tcpclient(void);
#endif
// console related I/O
#ifdef USBKEYBOARD
extern void clearrepeat(void);
	extern uint8_t Current_USB_devices;
	extern void cmd_mouse(void);
	extern bool USBenabled;
#endif
int __not_in_flash_func(MMInkey)(void);
int MMgetchar(void);
char MMputchar(char c, int flush);
void SaveProgramToFlash(unsigned char *pm, int msg);

void CheckAbort(void);
void EditInputLine(void);
// empty functions used in MMBasic but must be maintained outside of the interpreter
void UnloadFont(int);
#define NBRFONTS 0
#define STATE_VECTOR_LENGTH 624
#define STATE_VECTOR_M      397 /* changes to STATE_VECTOR_LENGTH also require changes to this */

typedef struct tagMTRand {
  unsigned long mt[STATE_VECTOR_LENGTH];
  int index;
} MTRand;

void seedRand(unsigned long seed);
unsigned long genRandLong(MTRand* rand);
MMFLOAT genRand(MTRand* rand);
extern struct tagMTRand *g_myrand;
#if defined(MSVCC)
#define mkdir _mkdir
#define rmdir _rmdir
#define chdir _chdir
#define getcwd _getcwd
#define kbhit _kbhit
#define getch _getch
#define ungetch _ungetch
#define putch _putch
#endif
#endif
#define CURSOR_OFF        350              // cursor off time in mS
#define CURSOR_ON     650                  // cursor on time in mS

#define dp(...) {unsigned char s[140];sprintf((char *)s,  __VA_ARGS__); MMPrintString((char *)s); MMPrintString((char *)"\r\n");}

#define TAB     	0x9
#define BKSP    	0x8
#define ENTER   	0xd
#define ESC     	0x1b

// the values returned by the function keys
#define F1      	0x91
#define F2      	0x92
#define F3      	0x93
#define F4      	0x94
#define F5      	0x95
#define F6      	0x96
#define F7      	0x97
#define F8      	0x98
#define F9      	0x99
#define F10     	0x9a
#define F11     	0x9b
#define F12     	0x9c

// the values returned by special control keys
#define UP			0x80
#define DOWN		0x81
#define LEFT		0x82
#define RIGHT		0x83
#define DOWNSEL     0xA1
#define RIGHTSEL    0xA3
#define INSERT		0x84
#define DEL			0x7f
#define HOME		0x86
#define END			0x87
#define PUP			0x88
#define PDOWN		0x89
#define NUM_ENT		ENTER
#define SLOCK		0x8c
#define ALT			0x8b
#define	SHIFT_TAB 	0x9F
#define SHIFT_DEL   0xa0
#define CTRLKEY(a) (a & 0x1f)
#define DISPLAY_CLS             1
#define REVERSE_VIDEO           3
#define CLEAR_TO_EOL            4
#define CLEAR_TO_EOS            5
#define SCROLL_DOWN             6
#define DRAW_LINE               7
#define CONFIG_TAB2		0b111
#define CONFIG_TAB4		0b001
#define CONFIG_TAB8		0b010
#define WPN 65   //Framebuffer page no.
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define SD_SLOW_SPI_SPEED 0
#define SD_FAST_SPI_SPEED 1
#define NONE_SPI_SPEED 4

#define RESET_COMMAND       9999                                    // indicates that the reset was caused by the RESET command
#define WATCHDOG_TIMEOUT    9998                                    // reset caused by the watchdog timer
#define PIN_RESTART         9997                                    // reset caused by entering 0 at the PIN prompt
#define RESTART_NOAUTORUN   9996                                    // reset required after changing the LCD or touch config
#define RESTART_DOAUTORUN   9995                                    // reset required by OPTION SET (ie, re runs the program)
#define KEYBOARDCLOCK 11
#define KEYBOARDDATA 12
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

#include "External.h"
#include "MM_Misc.h"
#include "Editor.h"
#include "Draw.h"
#include "XModem.h"
#include "MATHS.h"
#include "Onewire.h"
#include "I2C.h"
#include "SPI.h"
#include "Serial.h"
#include "SPI-LCD.h"
#ifndef PICOMITEVGA
#ifndef PICOMITEWEB
	#include "SSD1963.h"
	#include "Touch.h"
	#include "GUI.h"
#endif
#endif
#ifdef PICOMITEWEB
	#include "SSD1963.h"
	#include "Touch.h"
	#ifdef rp2350
		#include "GUI.h"
	#endif
#endif
#include "GPS.h"
#include "Audio.h"
#include "PS2Keyboard.h"
/*  @endcond */
