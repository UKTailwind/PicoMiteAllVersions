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

/* ============================================================================
 * Project includes
 * ============================================================================ */
#include "AllCommands.h"
#include "Memory.h"

/* ============================================================================
 * Hardware SDK includes
 * ============================================================================ */
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"

#ifdef PICOMITEVGA
#include "pico/multicore.h"
#endif

/* ============================================================================
 * File system includes
 * ============================================================================ */
#include "lfs.h"

/* ============================================================================
 * Configuration-dependent includes and declarations
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

/* Standard library includes */
#include <stdio.h>
#include <stddef.h>

/* Project includes */
#include "Version.h"
#include "configuration.h"
#include "FileIO.h"
#include "ff.h"

/* ============================================================================
 * Constants - Display configuration
 * ============================================================================ */
#define VCHARS 25		   // Number of lines in the DOS box (used in LIST)
#define FILENAME_LENGTH 12 // Maximum filename length

/* ============================================================================
 * Constants - Cursor timing
 * ============================================================================ */
#define CURSOR_OFF 350 // Cursor off time in ms
#define CURSOR_ON 650  // Cursor on time in ms

/* ============================================================================
 * Constants - Special characters
 * ============================================================================ */
#define TAB 0x09
#define BKSP 0x08
#define ENTER 0x0D
#define ESC 0x1B
#define DEL 0x7F

/* ============================================================================
 * Constants - Function keys
 * ============================================================================ */
#define F1 0x91
#define F2 0x92
#define F3 0x93
#define F4 0x94
#define F5 0x95
#define F6 0x96
#define F7 0x97
#define F8 0x98
#define F9 0x99
#define F10 0x9A
#define F11 0x9B
#define F12 0x9C

/* ============================================================================
 * Constants - Special control keys
 * ============================================================================ */
#define UP 0x80
#define DOWN 0x81
#define LEFT 0x82
#define RIGHT 0x83
#define DOWNSEL 0xA1
#define RIGHTSEL 0xA3
#define INSERT 0x84
#define HOME 0x86
#define END 0x87
#define PUP 0x88
#define PDOWN 0x89
#define NUM_ENT ENTER
#define SLOCK 0x8C
#define ALT 0x8B
#define SHIFT_TAB 0x9F
#define SHIFT_DEL 0xA0

/* ============================================================================
 * Constants - Display control
 * ============================================================================ */
#define DISPLAY_CLS 1
#define REVERSE_VIDEO 3
#define CLEAR_TO_EOL 4
#define CLEAR_TO_EOS 5
#define SCROLL_DOWN 6
#define DRAW_LINE 7

/* ============================================================================
 * Constants - Configuration
 * ============================================================================ */
#define CONFIG_TAB2 0b111
#define CONFIG_TAB4 0b001
#define CONFIG_TAB8 0b010

#define WPN 65 // Framebuffer page number

/* ============================================================================
 * Constants - GPIO
 * ============================================================================ */
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0

/* ============================================================================
 * Constants - SPI speeds
 * ============================================================================ */
#define SD_SLOW_SPI_SPEED 0
#define SD_FAST_SPI_SPEED 1
#define NONE_SPI_SPEED 4

/* ============================================================================
 * Constants - Reset reasons
 * ============================================================================ */
#define RESET_COMMAND 9999	   // Reset caused by the RESET command
#define WATCHDOG_TIMEOUT 9998  // Reset caused by the watchdog timer
#define PIN_RESTART 9997	   // Reset caused by entering 0 at the PIN prompt
#define RESTART_NOAUTORUN 9996 // Reset required after changing the LCD or touch config
#define RESTART_DOAUTORUN 9995 // Reset required by OPTION SET (re-runs the program)

/* ============================================================================
 * Constants - Hardware pins
 * ============================================================================ */
#define KEYBOARDCLOCK 11
#define KEYBOARDDATA 12
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

/* ============================================================================
 * Constants - Random number generator
 * ============================================================================ */
#define STATE_VECTOR_LENGTH 624
#define STATE_VECTOR_M 397 // Changes to STATE_VECTOR_LENGTH require changes to this

/* ============================================================================
 * Constants - Miscellaneous
 * ============================================================================ */
#define nunaddr (0xA4 / 2)
#define NBRERRMSG 17 // Number of file error messages
#define NBRFONTS 0

/* ============================================================================
 * Macros - Utility functions
 * ============================================================================ */
#define RoundUptoInt(a) (((a) + (32 - 1)) & (~(32 - 1)))
#define CTRLKEY(a) ((a) & 0x1F)

/* Debug print macro */
#define dp(...)                          \
	{                                    \
		unsigned char s[140];            \
		sprintf((char *)s, __VA_ARGS__); \
		MMPrintString((char *)s);        \
		MMPrintString((char *)"\r\n");   \
	}

/* ============================================================================
 * Type definitions - Pin definition
 * ============================================================================ */
struct s_PinDef
{
	int pin;
	int GPno;
	char pinname[5];
	uint64_t mode;
	unsigned char ADCpin;
	unsigned char slice;
};

/* ============================================================================
 * Type definitions - HID device
 * ============================================================================ */
typedef struct s_HID
{
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
	uint8_t r, g, b;
	uint8_t sendlights;
	uint8_t report[65];
} a_HID;

/* ============================================================================
 * Type definitions - Random number generator
 * ============================================================================ */
typedef struct tagMTRand
{
	unsigned long mt[STATE_VECTOR_LENGTH];
	int index;
} MTRand;

/* ============================================================================
 * External variables - Error handling and control
 * ============================================================================ */
extern int MMerrno;
extern int OptionErrorSkip;
extern int ExitMMBasicFlag;
extern unsigned char *InterruptReturn;
extern unsigned int _excep_peek;
extern uint32_t _excep_code;

/* ============================================================================
 * External variables - Timing and counters
 * ============================================================================ */
extern volatile long long int mSecTimer;
extern volatile unsigned int PauseTimer;
extern volatile unsigned int IntPauseTimer;
extern volatile unsigned int Timer1, Timer2, Timer3, Timer4, Timer5;
extern volatile unsigned int diskchecktimer;
extern volatile unsigned int clocktimer;
extern volatile int ds18b20Timer;
extern volatile int CursorTimer;
extern volatile unsigned int SecondsTimer;
extern volatile int ClickTimer;
extern volatile unsigned int InkeyTimer;
extern volatile unsigned int I2CTimer;
extern volatile unsigned int GPSTimer;
extern volatile unsigned int WDTimer;
extern int ticks_per_second;

#ifndef USBKEYBOARD
extern volatile unsigned int MouseTimer;
#endif

/* ============================================================================
 * External variables - Date and time
 * ============================================================================ */
extern volatile int day_of_week;

#ifndef rp2350
extern datetime_t rtc_t;
#endif

/* ============================================================================
 * External variables - Console buffers
 * ============================================================================ */
extern volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE];
extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
extern volatile char ConsoleTxBuf[CONSOLE_TX_BUF_SIZE];
extern volatile int ConsoleTxBufHead;
extern volatile int ConsoleTxBufTail;

/* ============================================================================
 * External variables - Display and graphics
 * ============================================================================ */
extern volatile int DISPLAY_TYPE;
extern int MMCharPos;
extern unsigned char *StartEditPoint;
extern int StartEditChar;
extern int PromptFont, PromptFC, PromptBC;
extern volatile int ytileheight;
extern volatile int X_TILE, Y_TILE;
extern uint8_t screen320;
extern uint8_t *buff320;

/* ============================================================================
 * External variables - Hardware configuration
 * ============================================================================ */
extern unsigned char WatchdogSet;
extern unsigned char IgnorePIN;
extern MMFLOAT VCC;
extern uint32_t restart_reason;
extern int calibrate;

/* ============================================================================
 * External variables - Pulse functionality
 * ============================================================================ */
extern unsigned char PulsePin[];
extern unsigned char PulseDirection[];
extern int PulseCnt[];
extern int PulseActive;

/* ============================================================================
 * External variables - Pin definitions
 * ============================================================================ */
extern const struct s_PinDef PinDef[NBRPINS + 1];

/* ============================================================================
 * External variables - Audio
 * ============================================================================ */
extern uint16_t AUDIO_L_PIN, AUDIO_R_PIN, AUDIO_SLICE;
extern uint16_t AUDIO_WRAP;

/* ============================================================================
 * External variables - SD card
 * ============================================================================ */
extern uint16_t SD_CLK_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_CS_PIN;

/* ============================================================================
 * External variables - SPI
 * ============================================================================ */
extern unsigned char SPIatRisk;

#if PICOMITERP2350
extern uint16_t LCD_CLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN;
#endif

/* ============================================================================
 * External variables - Camera
 * ============================================================================ */
extern int CameraSlice;
extern int CameraChannel;
extern char id_out[];

/* ============================================================================
 * External variables - Memory management
 * ============================================================================ */
extern const uint8_t *flash_progmemory;
extern char __heap_start;
extern uint32_t heapend;

/* ============================================================================
 * External variables - File system
 * ============================================================================ */
extern lfs_t lfs;
extern lfs_dir_t lfs_dir;
extern struct lfs_info lfs_info;
extern int FatFSFileSystem;

/* ============================================================================
 * External variables - Multicore
 * ============================================================================ */
extern uint32_t core1stack[];
extern volatile bool mergedone;

/* ============================================================================
 * External variables - Random number generation
 * ============================================================================ */
extern struct tagMTRand *g_myrand;

/* ============================================================================
 * External variables - HID devices
 * ============================================================================ */
extern volatile struct s_HID HID[4];

#ifdef USBKEYBOARD
extern uint8_t Current_USB_devices;
extern bool USBenabled;
#else
extern bool mouse0;
extern int MOUSE_CLOCK, MOUSE_DATA;
#endif

/* ============================================================================
 * External variables - Platform-specific (RP2350)
 * ============================================================================ */
#ifdef rp2350
extern bool rp2350a;
extern uint32_t PSRAMsize;
extern const uint32_t MAP16DEF[16];
#endif

/* ============================================================================
 * External variables - VGA/HDMI display modes
 * ============================================================================ */
#ifdef PICOMITEVGA
extern volatile uint8_t transparent;
extern volatile uint8_t transparents;
extern volatile int RGBtransparent;
extern int QVGA_CLKDIV;

#ifndef HDMI
extern uint8_t remap[];
extern uint8_t map16[16];
extern uint16_t __attribute__((aligned(256))) M_Foreground[16];
extern uint16_t __attribute__((aligned(256))) M_Background[16];

#ifdef rp2350
extern uint16_t *tilefcols;
extern uint16_t *tilebcols;
#else
extern uint16_t __attribute__((aligned(256))) tilefcols[80 * 40];
extern uint16_t __attribute__((aligned(256))) tilebcols[80 * 40];
#endif
#else
extern uint32_t remap555[];
extern uint32_t remap332[];
extern uint16_t remap256[];
extern uint16_t map256[256];
extern uint32_t map16quads[16];
extern uint32_t map16pairs[16];
extern const uint32_t MAP256DEF[256];
extern volatile int32_t v_scanline;
extern uint16_t *tilefcols;
extern uint16_t *tilebcols;
extern uint8_t *tilefcols_w;
extern uint8_t *tilebcols_w;

extern int MODE_H_SYNC_POLARITY, MODE_V_TOTAL_LINES, MODE_ACTIVE_LINES, MODE_ACTIVE_PIXELS;
extern int MODE_H_ACTIVE_PIXELS, MODE_H_FRONT_PORCH, MODE_H_SYNC_WIDTH, MODE_H_BACK_PORCH;
extern int MODE_V_SYNC_POLARITY, MODE_V_ACTIVE_LINES, MODE_V_FRONT_PORCH, MODE_V_SYNC_WIDTH, MODE_V_BACK_PORCH;
#endif

extern int MODE1SIZE;
extern int MODE2SIZE;
extern int MODE3SIZE;
extern int MODE4SIZE;
extern int MODE5SIZE;

#else
#ifdef rp2350
extern uint16_t *tilefcols;
extern uint16_t *tilebcols;
#else
extern uint16_t tilefcols[];
extern uint16_t tilebcols[];
#endif
#endif

/* ============================================================================
 * External variables - WiFi/Web (PICOMITEWEB)
 * ============================================================================ */
#ifdef PICOMITEWEB
extern volatile int WIFIconnected;
extern volatile int scantimer;
extern int startupcomplete;
#endif

/* ============================================================================
 * Function declarations - Color conversion
 * ============================================================================ */
uint8_t RGB121(uint32_t c);
uint8_t RGB332(uint32_t c);
uint16_t RGB555(uint32_t c);
uint16_t RGB121pack(uint32_t c);

/* ============================================================================
 * Function declarations - PWM and hardware control
 * ============================================================================ */
int getslice(int pin);
void setpwm(int pin, int *PWMChannel, int *PWMSlice, MMFLOAT frequency, MMFLOAT duty);

/* ============================================================================
 * Function declarations - VGA/Graphics core
 * ============================================================================ */
void __not_in_flash_func(QVgaCore)(void);

#ifdef HDMI
void settiles(void);
void mapreset(void);
#else
#ifdef PICOMITEVGA
void VGArecovery(int pin);
#endif
#endif

/* ============================================================================
 * Function declarations - Display operations
 * ============================================================================ */
void clear320(void);

/* ============================================================================
 * Function declarations - I2S audio
 * ============================================================================ */
void start_i2s(int pio, int sm);

/* ============================================================================
 * Function declarations - Timing
 * ============================================================================ */
void uSec(int us);
void routinechecks(void);

/* ============================================================================
 * Function declarations - File I/O
 * ============================================================================ */
int BasicFileOpen(char *fname, int fnbr, int mode);
void FileClose(int fnbr);
int InitSDCard(void);

/* ============================================================================
 * Function declarations - SPI
 * ============================================================================ */
void SPIClose(void);
void SPI2Close(void);

/* ============================================================================
 * Function declarations - Timer control
 * ============================================================================ */
void mT4IntEnable(int status);

/* ============================================================================
 * Function declarations - Console I/O
 * ============================================================================ */
int kbhitConsole(void);
void putConsole(int c, int flush);
int getConsole(void);
char SerialConsolePutC(char c, int flush);

/* ============================================================================
 * Function declarations - Keyboard input
 * ============================================================================ */
int MMInkey(void);
int MMgetchar(void);
char MMputchar(char c, int flush);

/* ============================================================================
 * Function declarations - Program execution
 * ============================================================================ */
void CallExecuteProgram(char *p);
void CheckAbort(void);
void EditInputLine(void);

/* ============================================================================
 * Function declarations - Flash memory
 * ============================================================================ */
void SaveProgramToFlash(unsigned char *pm, int msg);

/* ============================================================================
 * Function declarations - Output formatting
 * ============================================================================ */
void PRet(void);
void PInt(int64_t n);
void PIntComma(int64_t n);
void PIntH(unsigned long long int n);
void PIntB(unsigned long long int n);
void PIntBC(unsigned long long int n);
void PIntHC(unsigned long long int n);
void PFlt(MMFLOAT flt);
void PFltComma(MMFLOAT n);

void SRet(void);
void SInt(int64_t n);
void SIntComma(int64_t n);

/* ============================================================================
 * Function declarations - String output
 * ============================================================================ */
void MMPrintString(char *s);
void SSPrintString(char *s);
void myprintf(char *s);

/* ============================================================================
 * Function declarations - Hardware initialization
 * ============================================================================ */
void InitReservedIO(void);

/* ============================================================================
 * Function declarations - Data buffers
 * ============================================================================ */
long long int *GetReceiveDataBuffer(unsigned char *p, unsigned int *nbr);

/* ============================================================================
 * Function declarations - Font management
 * ============================================================================ */
void UnloadFont(int);

/* ============================================================================
 * Function declarations - Random number generation
 * ============================================================================ */
void seedRand(unsigned long seed);
unsigned long genRandLong(MTRand *rand);
MMFLOAT genRand(MTRand *rand);

/* ============================================================================
 * Function declarations - Platform-specific (RP2350)
 * ============================================================================ */
#ifdef rp2350
void _Z10copy_wordsPKmPmm(uint32_t *s, uint32_t *d, int n);
#endif

/* ============================================================================
 * Function declarations - Mouse (non-USB keyboard builds)
 * ============================================================================ */
#ifndef USBKEYBOARD
void initMouse0(int sensitivity);
#endif

/* ============================================================================
 * Function declarations - USB keyboard builds
 * ============================================================================ */
#ifdef USBKEYBOARD
void clearrepeat(void);
void cmd_mouse(void);
#endif

/* ============================================================================
 * Function declarations - WiFi/Web (PICOMITEWEB)
 * ============================================================================ */
#ifdef PICOMITEWEB
void ProcessWeb(int mode);
void WebConnect(void);
void close_tcpclient(void);
#endif

/* ============================================================================
 * Platform-specific compatibility (MSVCC)
 * ============================================================================ */
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

#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

/* ============================================================================
 * Module includes - Peripheral and feature support
 * ============================================================================ */
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