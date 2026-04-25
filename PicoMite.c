/***********************************************************************************************************************
PicoMite MMBasic

Picomite.c

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
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "configuration.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hal/hal_display_merge.h"
#include "hal/hal_flash.h"
#include "hal/hal_keyboard.h"
#include "hardware/regs/addressmap.h"     /* XIP_BASE */
#include "hardware/adc.h"
#include "hardware/exception.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "hardware/vreg.h"
#include "hardware/structs/pads_qspi.h"
#include "pico/unique_id.h"
#include "hardware/pwm.h"
#include "configuration.h"

extern void start_i2s(int pio, int sm);
#ifdef rp2350
#include "hardware/structs/qmi.h"
#include "psram.h"
#endif
#ifdef PICOMITEVGA
extern void start_vga_i2s(void);
#ifndef HDMI
#endif
#endif
/* COPYRIGHT text moved to Version.h as MMBASIC_COPYRIGHT; banner is
 * emitted via MMBasic_PrintBanner() in MMBasic_REPL.c. */

#ifdef USBKEYBOARD
    #include "tusb.h"
    #include "host/hcd.h"
    #include "usb_host_files/tusb_config.h"
#else
    #include "pico/unique_id.h"
    #include "class/cdc/cdc_device.h" 
#endif
#ifndef rp2350
    #include "hardware/structs/ssi.h"
    #include "hardware/vreg.h"
#else
    #ifdef HDMI
        #include "hardware/structs/hstx_ctrl.h"
        #include "hardware/structs/hstx_fifo.h"
    #endif
    #include "hardware/dma.h"
    #include "hardware/gpio.h"
    #include "hardware/irq.h"
    #include "hardware/structs/bus_ctrl.h"
    #include "hardware/structs/xip_ctrl.h"
    #include "hardware/structs/sio.h"
    #include "hardware/vreg.h"
    #include "pico/multicore.h"
    #include "pico/sem.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include "pico/stdlib.h"
    #include "hardware/clocks.h"
    #include <string.h>
    #include "hardware/regs/sysinfo.h"
    #include "hardware/regs/powman.h"
    uint8_t PSRAMpin;
#endif
/* PSRAMsize is fixed at 0 on RP2040 (no PSRAM); runtime-detected to the
 * chip's reported size on RP2350. The definition lives outside any
 * target guard so `extern uint32_t PSRAMsize;` (Hardware_Includes.h)
 * resolves on every port — core code reads it as a runtime value. */
uint32_t PSRAMsize = 0;
/* rp2350a: on RP2350 this is runtime-detected in the init path from the
 * chip's SYSINFO_PACKAGE_SEL register; on RP2040 the variable still exists
 * and is fixed at true (RP2040 matches the "30-pin / no PWM8..11"
 * behaviour the flag gates on). Defining it unconditionally lets portable
 * code check the flag without wrapping every reference in #ifdef rp2350. */
bool rp2350a=true;
#include "hardware/structs/bus_ctrl.h"
#include <pico/bootrom.h>
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#ifdef PICOMITEWEB
    #include "lwipopts.h"
    #include "pico/cyw43_arch.h"
    #include "lwip/pbuf.h"
    #include "lwip/tcp.h"
    #include "lwip/dns.h"
    #include "lwip/pbuf.h"
    #include "lwip/udp.h"
#endif
#ifdef PICOMITEVGA
    volatile uint8_t transparent=0;
    volatile uint8_t transparents=0;
    volatile int RGBtransparent=0;
    int MODE1SIZE, MODE2SIZE, MODE3SIZE, MODE4SIZE, MODE5SIZE;
#ifdef HDMI
    uint32_t map16quads[16];
    uint32_t map16pairs[16];
    // 126 MHz timings
#else
    uint8_t map16[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    int QVGA_TOTAL;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
    int QVGA_HSYNC;	// horizontal sync clock ticks
    int QVGA_BP	;	// back porch clock ticks
    int QVGA_FP;	// front porch clock ticks

    // QVGA vertical timings
    int QVGA_VACT;	// V active scanlines (= 2*HEIGHT)
    int QVGA_VFRONT;	// V front porch
    int QVGA_VSYNC;	// length of V sync (number of scanlines)
    int QVGA_VBACK;	// V back porch
    int QVGA_VTOT;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    int QVGA_HACT;	// V active scanlines (= 2*HEIGHT)
#endif

    #ifndef HDMI
    #include "Include.h"
    #endif
    #ifdef USBKEYBOARD
        #ifdef HDMI
            #define MES_SIGNON  "\rPicoMiteHDMI MMBasic USB " CHIP " Edition V"VERSION "\r\n"
        #else
            #define MES_SIGNON  "\rPicoMiteVGA MMBasic USB " CHIP " Edition V"VERSION "\r\n"
        #endif
        extern void hid_app_task(void);
        volatile int keytimer=0;
        extern void USB_bus_reset(void);
        bool USBenabled=false;
    #else
        #ifdef HDMI
            #define MES_SIGNON  "\rPicoMiteHDMI MMBasic " CHIP " Edition V"VERSION "\r\n"
        #else
            #define MES_SIGNON  "\rPicoMiteVGA MMBasic " CHIP " Edition V"VERSION "\r\n"
        #endif
#endif

#endif
#ifdef PICOMITEWEB
    #define MES_SIGNON  "\rWebMite MMBasic " CHIP " Edition V"VERSION "\r\n"
    volatile int WIFIconnected=0;
    int startupcomplete=0;
    void ProcessWeb(int mode);
    char LCDAttrib=0;
    #endif
#ifdef PICOMITE
    #ifdef USBKEYBOARD
        #include "tusb.h"
        #include "host/hcd.h"
        #define MES_SIGNON  "\rPicoMite MMBasic USB " CHIP " Edition V"VERSION "\r\n"
        extern void hid_app_task(void);
        volatile int keytimer=0;
        extern void USB_bus_reset(void);
        bool USBenabled=false;
        #include "pico/multicore.h"
        mutex_t	frameBufferMutex;					// mutex to lock frame buffer
    #else
        #define MES_SIGNON  "\rPicoMite MMBasic " CHIP " V"VERSION "\r\n"
        #include "pico/multicore.h"
        mutex_t	frameBufferMutex;					// mutex to lock frame buffer
        #endif
        char LCDAttrib=0;
#endif
#define KEYCHECKTIME 16
int ListCnt;
int MMCharPos;
/* MMPromptPos, lastcmd[], InsertLastcmd, EditInputLine → MMBasic_Prompt.c */
int busfault=0;
int ExitMMBasicFlag = false;
volatile int MMAbort = false;
unsigned int _excep_peek;
void CheckAbort(void);
void TryLoadProgram(void);
unsigned char lastchar=0;
int adc_clk_div;
unsigned char BreakKey = BREAK_KEY;                                          // defaults to CTRL-C.  Set to zero to disable the break function
volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE]={0};
volatile int ConsoleRxBufHead = 0;
volatile int ConsoleRxBufTail = 0;
volatile char ConsoleTxBuf[CONSOLE_TX_BUF_SIZE]={0};
volatile int ConsoleTxBufHead = 0;
volatile int ConsoleTxBufTail = 0;
uint I2SOff;

volatile unsigned int MouseTimer = 0;
volatile unsigned int AHRSTimer = 0;
volatile unsigned int InkeyTimer = 0;
volatile long long int mSecTimer = 0;                               // this is used to count mSec
volatile unsigned int WDTimer = 0;
volatile unsigned int diskchecktimer = DISKCHECKRATE;
volatile unsigned int clocktimer=60*60*1000;
volatile unsigned int PauseTimer = 0;
volatile unsigned int ClassicTimer = 0;
volatile unsigned int NunchuckTimer = 0;
volatile unsigned int IntPauseTimer = 0;
volatile unsigned int Timer1=0, Timer2=0, Timer3=0, Timer4=0, Timer5=0;		                       //1000Hz decrement timer
volatile unsigned int KeyCheck=2000;
volatile int ds18b20Timer = -1;
volatile unsigned int ScrewUpTimer = 0;
//volatile int second = 0;                                            // date/time counters
//volatile int minute = 0;
//volatile int hour = 0;
//volatile int day = 1;
//volatile int month = 1;
//volatile int year = 2000;
volatile unsigned int GPSTimer = 0;
volatile unsigned int SecondsTimer = 0;
volatile unsigned int I2CTimer = 0;
volatile int day_of_week=1;
unsigned char PulsePin[NBR_PULSE_SLOTS];
unsigned char PulseDirection[NBR_PULSE_SLOTS];
int PulseCnt[NBR_PULSE_SLOTS];
int PulseActive;
const uint8_t *flash_option_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
const uint8_t *SavedVarsFlash = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET +  FLASH_ERASE_SIZE);
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE);
const uint8_t *flash_progmemory = (const uint8_t *) (XIP_BASE + PROGSTART);
const uint8_t *flash_libmemory = (const uint8_t *) (XIP_BASE + PROGSTART - MAX_PROG_SIZE);
int ticks_per_second; 
int InterruptUsed;
int calibrate=0;
char id_out[12];
MMFLOAT VCC=3.3;
int PromptFont, PromptFC=0xFFFFFF, PromptBC=0;                             // the font and colours selected at the prompt
volatile int DISPLAY_TYPE;
volatile bool processtick = true;
unsigned char WatchdogSet = false;
unsigned char IgnorePIN = false;
unsigned char SPIatRisk = false;
uint32_t restart_reason=0;
uint32_t __uninitialized_ram(_excep_code);
uint64_t __uninitialized_ram(_persistent);
FATFS fs;                 // Work area (file system object) for logical drive
bool timer_callback(repeating_timer_t *rt);
static uint64_t __not_in_flash_func(uSecFunc)(uint64_t a){
    uint64_t b=time_us_64()+a;
    while(time_us_64()<b){}
    return b;
}
extern void MX470Display(int fn);
//Vector to CFunction routine called every command (ie, from the BASIC interrupt checker)
extern unsigned int CFuncInt1;
//Vector to CFunction routine called by the interrupt 2 handler
extern unsigned int CFuncInt2;
extern unsigned int CFuncmSec;
extern void CallCFuncInt1(void);
extern void CallCFuncInt2(void);
extern volatile bool CSubComplete;
static uint64_t __not_in_flash_func(uSecTimer)(void){ return time_us_64();}
static int64_t PinReadFunc(int a){return gpio_get(PinDef[a].GPno);}
extern void CallCFuncmSec(void);
extern volatile uint32_t irqs;
#define CFUNCRAM_SIZE   256
int CFuncRam[CFUNCRAM_SIZE/sizeof(int)];
repeating_timer_t timer;
MMFLOAT IntToFloat(long long int a){ return a; }
MMFLOAT FMul(MMFLOAT a, MMFLOAT b){ return a * b; }
MMFLOAT FAdd(MMFLOAT a, MMFLOAT b){ return a + b; }
MMFLOAT FSub(MMFLOAT a, MMFLOAT b){ return a - b; }
MMFLOAT FDiv(MMFLOAT a, MMFLOAT b){ return a / b; }
uint32_t CFunc_delay_us;
#ifndef HDMI
int QVGA_CLKDIV;	// SM divide clock ticks
#endif
void PIOExecute(int pion, int sm, uint32_t ins){
    PIO pio = (pion ? pio1: pio0);
    pio_sm_exec(pio, sm, ins);
}

int IDiv(int a, int b){return a/b;}
int   FCmp(MMFLOAT a,MMFLOAT b){if(a>b) return 1;else if(a<b)return -1; else return 0;}
MMFLOAT LoadFloat(unsigned long long c){union ftype{ unsigned long long a; MMFLOAT b;}f;f.a=c;return f.b; }
const void * const CallTable[] __attribute__((section(".text")))  = {	(void *)uSecFunc,	//0x00
																		(void *)putConsole,	//0x04
																		(void *)getConsole,	//0x08
																		(void *)ExtCfg,	//0x0c
																		(void *)ExtSet,	//0x10
																		(void *)ExtInp,	//0x14
																		(void *)PinSetBit,	//0x18
																		(void *)PinReadFunc,	//0x1c
																		(void *)MMPrintString,	//0x20
																		(void *)IntToStr,	//0x24
																		(void *)CheckAbort,	//0x28
																		(void *)GetMemory,	//0x2c
																		(void *)GetTempMemory,	//0x30
																		(void *)FreeMemory, //0x34
																		(void *)&DrawRectangle,	//0x38
																		(void *)&DrawBitmap,	//0x3c
																		(void *)DrawLine,	//0x40
																		(void *)FontTable,	//0x44
																		(void *)&ExtCurrentConfig,	//0x48
																		(void *)&HRes,	//0x4C
																		(void *)&VRes,	//0x50
																		(void *)SoftReset, //0x54
																		(void *)error,	//0x58
																		(void *)&ProgMemory,	//0x5c
																		(void *)&g_vartbl, //0x60
																		(void *)&g_varcnt,  //0x64
																		(void *)&DrawBuffer,	//0x68
																		(void *)&ReadBuffer,	//0x6c
																		(void *)&FloatToStr,	//0x70
                                                                        (void *)CallExecuteProgram, //0x74
                                                                        (void *)&CFuncmSec, //0x78
                                                                        (void *)CFuncRam,	//0x7c
                                                                        (void *)&ScrollLCD,	//0x80
																		(void *)IntToFloat, //0x84
																		(void *)FloatToInt64,	//0x88
																		(void *)&Option,	//0x8c
																		(void *)sin,	//0x90
																		(void *)DrawCircle,	//0x94
																		(void *)DrawTriangle,	//0x98
																		(void *)uSecTimer,	//0x9c
                                                                        (void *)FMul,//0xa0
                                                                        (void *)FAdd,//0xa4
                                                                        (void *)FSub,//0xa8
                                                                        (void *)FDiv,//0xac
                                                                        (void *)FCmp,//0xb0
                                                                        (void *)&LoadFloat,//0xb4
                                                                        (void *)&CFuncInt1,	//0xb8
                                                                        (void *)&CFuncInt2,	//0xbc
																		(void *)&CSubComplete,	//0xc0
																		(void *)&AudioOutput,	//0xc4
                                                                        (void *)IDiv,//0x0xc8
                                                                        (void *)&AUDIO_WRAP,//0x0xcc
                                                                        (void *)&CFuncInt3,	//0xb8
                                                                        (void *)&CFuncInt4,	//0xbc
                                                                        (void *)PIOExecute,
									   	   	   	   	   	   	   	   	   	   };

const struct s_PinDef PinDef[]={
	    { 0, 99, "NULL",  UNUSED  ,99, 99},
	    { 1,  0, "GP0",  DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART0TX  | I2C0SDA | PWM0A,99,0},  	// pin 1
		{ 2,  1, "GP1",  DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM0B ,99,128},    		    // pin 2
		{ 3, 99, "GND",  UNUSED  ,99,99},                                                           // pin 3
		{ 4,  2, "GP2",  DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM1A ,99,1},   		    // pin 4
		{ 5,  3, "GP3",  DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM1B ,99,129},    			// pin 5
		{ 6,  4, "GP4",  DIGITAL_IN | DIGITAL_OUT | SPI0RX| UART1TX  | I2C0SDA | PWM2A ,99,2},  	// pin 6
		{ 7,  5, "GP5",   DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM2B	,99,130},    		// pin 7
		{ 8, 99, "GND",  UNUSED  ,99, 99},                                                          // pin 8
		{ 9,  6, "GP6",  DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM3A	,99, 3},  			// pin 9
		{ 10,  7, "GP7",  DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM3B	,99, 131}, 		    // pin 10
	    { 11,  8, "GP8",  DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART1TX  | I2C0SDA | PWM4A ,99, 4}, 	// pin 11
		{ 12,  9, "GP9",  DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM4B	,99, 132},    		// pin 12
		{ 13, 99, "GND",  UNUSED  ,99, 99},                                                         // pin 13
		{ 14, 10, "GP10",  DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM5A	,99, 5},  			// pin 14
		{ 15, 11, "GP11",  DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL | PWM5B	,99, 133},       	// pin 15
#ifdef HDMI
		{ 16, 12, "HDMI", UNUSED  ,99, 99},                                                          // pin 16
		{ 17, 13, "HDMI", UNUSED  ,99, 99},                                                          // pin 17
		{ 18, 99, "GND", UNUSED  ,99, 99},                                                          // pin 18
		{ 19, 14, "HDMI", UNUSED  ,99, 99},                                                          // pin 19
		{ 20, 15, "HDMI", UNUSED  ,99, 99},                                                          // pin 20
		{ 21, 16, "HDMI", UNUSED  ,99, 99},                                                          // pin 21
		{ 22, 17, "HDMI", UNUSED  ,99, 99},                                                          // pin 22
		{ 23, 99, "GND", UNUSED  ,99, 99},                                                          // pin 23
		{ 24, 18, "HDMI", UNUSED  ,99, 99},                                                          // pin 24
		{ 25, 19, "HDMI", UNUSED  ,99, 99},                                                          // pin 25
#else	
		{ 16, 12, "GP12",  DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART0TX | I2C0SDA | PWM6A ,99, 6},   // pin 16
		{ 17, 13, "GP13",  DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM6B	,99, 134},    		// pin 17
		{ 18, 99, "GND", UNUSED  ,99, 99},                                                          // pin 18
		{ 19, 14, "GP14",  DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM7A	,99, 7},    		// pin 19
		{ 20, 15, "GP15",  DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL | PWM7B	,99, 135},  		// pin 20

		{ 21, 16, "GP16",  DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART0TX | I2C0SDA | PWM0A ,99, 0},   // pin 21
		{ 22, 17, "GP17",  DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM0B	,99, 128},    		// pin 22
		{ 23, 99, "GND",  UNUSED  ,99, 99},                                                         // pin 23
	    { 24, 18, "GP18",  DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM1A	,99, 1}, 			// pin 24
	    { 25, 19, "GP19",  DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM1B	,99, 129},  		// pin 25
#endif
	    { 26, 20, "GP20",  DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART1TX| I2C0SDA | PWM2A	,99, 2},    // pin 26
	    { 27, 21, "GP21",  DIGITAL_IN | DIGITAL_OUT | UART1RX| I2C0SCL | PWM2B	,99, 130},    		// pin 27
		{ 28, 99, "GND",  UNUSED  ,99, 99},                                                         // pin 28
		{ 29, 22, "GP22",  DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA| PWM3A	,99, 3},    				// pin 29
		{ 30, 99, "RUN",  UNUSED  ,99, 99},                                                         // pin 30
	    { 31, 26, "GP26",  DIGITAL_IN | DIGITAL_OUT	| ANALOG_IN | SPI1SCK| I2C1SDA | PWM5A , 0 , 5},// pin 31
	    { 32, 27, "GP27",  DIGITAL_IN | DIGITAL_OUT	| ANALOG_IN | SPI1TX| I2C1SCL | PWM5B , 1, 133},// pin 32
		{ 33, 99, "AGND", UNUSED  ,99, 99},                                                         // pin 33
		{ 34, 28, "GP28", DIGITAL_IN |DIGITAL_OUT| ANALOG_IN| SPI1RX| UART0TX|I2C0SDA| PWM6A, 2, 6},// pin 34
	    { 35, 99, "VREF", UNUSED  ,99, 99},                                                         // pin 35
		{ 36, 99, "3V3", UNUSED  ,99, 99},                                                          // pin 36
		{ 37, 99, "3V3E", UNUSED  ,99, 99},                                                         // pin 37
		{ 38, 99, "GND", UNUSED  ,99, 99},                                                          // pin 38
		{ 39, 99, "VSYS", UNUSED  ,99, 99},                                                         // pin 39
		{ 40, 99, "VBUS", UNUSED  ,99, 99},                                                         // pin 40
    #ifndef PICOMITEWEB
		{ 41, 23, "GP23", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL| PWM3B  ,99 , 131},           // pseudo pin 41
		{ 42, 24, "GP24", DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART1TX | I2C0SDA| PWM4A  ,99 , 4},   // pseudo pin 42
		{ 43, 25, "GP25", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL| PWM4B  ,99 , 132},          // pseudo pin 43
		{ 44, 29, "GP29", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | UART0RX | I2C0SCL | PWM6B, 3, 134},// pseudo pin 44
    #endif
    #ifdef rp2350
    #ifndef PICOMITEWEB
		{ 45, 30, "GP30", DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM7A  ,99 , 7},           // pseudo pin 45
		{ 46, 31, "GP31", DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL| PWM7B  ,99 , 135},           // pseudo pin 46
		{ 47, 32, "GP32", DIGITAL_IN | DIGITAL_OUT | UART0TX | SPI0RX | I2C0SDA| PWM8A  ,99 , 8},           // pseudo pin 47
		{ 48, 33, "GP33", DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL| PWM8B  ,99 , 136},           // pseudo pin 48
		{ 49, 34, "GP34", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA| PWM9A  ,99 , 9},           // pseudo pin 49
		{ 50, 35, "GP35", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL| PWM9B  ,99 , 137},           // pseudo pin 50
		{ 51, 36, "GP36", DIGITAL_IN | DIGITAL_OUT | UART1TX | SPI0RX | I2C0SDA| PWM10A  ,99 , 10},           // pseudo pin 51
		{ 52, 37, "GP37", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL| PWM10B  ,99 , 138},           // pseudo pin 52
		{ 53, 38, "GP38", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA| PWM11A  ,99 , 11},           // pseudo pin 53
		{ 54, 39, "GP39", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL| PWM11B  ,99 , 139},           // pseudo pin 54
		{ 55, 40, "GP40", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN| UART1TX | SPI1RX | I2C0SDA| PWM8A  ,0 , 8},           // pseudo pin 55
		{ 56, 41, "GP41", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN| UART1RX | I2C0SCL| PWM8B  ,1 , 136},           // pseudo pin 56
		{ 57, 42, "GP42", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN| SPI1SCK | I2C1SDA| PWM9A  ,2 , 9},           // pseudo pin 57
		{ 58, 43, "GP43", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN| SPI1TX | I2C1SCL| PWM9B  ,3 , 137},           // pseudo pin 58
		{ 59, 44, "GP44", DIGITAL_IN | DIGITAL_OUT | UART0TX | ANALOG_IN | SPI1RX | I2C0SDA| PWM10A  ,4 , 10},           // pseudo pin 59
		{ 60, 45, "GP45", DIGITAL_IN | DIGITAL_OUT | UART0RX | ANALOG_IN | I2C0SCL| PWM10B  ,5 , 138},           // pseudo pin 60
		{ 61, 46, "GP46", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1SCK | I2C1SDA| PWM11A  ,6 , 11},           // pseudo pin 61
		{ 62, 47, "GP47", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1TX | I2C1SCL| PWM11B  ,7 , 139},           // pseudo pin 62

    #endif
    #endif
};
char alive[]="\033[?25h";
const char DaysInMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
/* commandtbl_decode moved to MMBasic_REPL.c. */
char banner[64];
void __not_in_flash_func(routinechecks)(void){
    static int when=0;
    if(abs((time_us_64()-mSecTimer*1000))> 5000){
        cancel_repeating_timer(&timer);
        add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
        mSecTimer=time_us_64()/1000;
    }
    if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying==P_MP3 || CurrentlyPlaying==P_MIDI ){
#ifdef PICOMITE
        if(SPIatRisk)mutex_enter_blocking(&frameBufferMutex);			// lock the frame buffer
#endif
        checkWAVinput();
#ifdef PICOMITE
        if(SPIatRisk)mutex_exit(&frameBufferMutex);
#endif
    }
    if(CurrentlyPlaying == P_MOD || CurrentlyPlaying==P_ARRAY ) checkWAVinput();
    if(++when & 7 && CurrentLinePtr) return;
#ifdef USBKEYBOARD
    if(USBenabled){
        if(mSecTimer>2000){
            tuh_task();
            hid_app_task();
        }
    }
#else
	 static int c, read=0;
     if(tud_cdc_connected() && (Option.SerialConsole==0 || Option.SerialConsole>4) && Option.Telnet!=-1){
        while(( c=tud_cdc_read_char())!=-1){
            ConsoleRxBuf[ConsoleRxBufHead] = c;
            if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
                MMAbort = true;                                        // set the flag for the interpreter to see
                ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
            } else if(ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt!=NULL){
                Keycomplete=true;
            } else {
                ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;     // advance the head of the queue
                if(ConsoleRxBufHead == ConsoleRxBufTail) {                           // if the buffer has overflowed
                    ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
                }
            }
        }
    }
#endif
	if(Option.DISPLAY_TYPE>=NEXTGEN && !(low_x==silly_low && high_x==silly_high && low_y==silly_low && high_y==silly_high)){// Buffered LCD displays
        if(Option.Refresh){
            hal_display_nextgen_refresh_rect(low_x, low_y, high_x, high_y);
            low_x=silly_low; high_y=silly_high; low_y=silly_low; high_x=silly_high;
        }
	}
	if(GPSchannel)processgps();
    if(diskchecktimer == 0)CheckSDCard();
#ifdef GUICONTROLS
    if(Ctrl && TOUCH_GETIRQTRIS && !calibrate)ProcessTouch();
#endif

//        if(tud_cdc_connected() && KeyCheck==0){
//            SSPrintString(alive);
//        }
    if(clocktimer==0 && Option.RTC){
        if(classicread==0 && nunchuckread==0){
            RtcGetTime(0);
        }
    }
#ifndef USBKEYBOARD
    if(Option.KeyboardConfig==CONFIG_I2C && KeyCheck==0){
        if(read==0){
            CheckI2CKeyboard(0,0);
            read=1;
        } else {
            CheckI2CKeyboard(0,1);
            read=0;
        }
        KeyCheck=KEYCHECKTIME;
    }
#endif
    if(classic1 && ClassicTimer>=10){
        if(classicread==0){
			WiiSend(sizeof(readcontroller),(char *)readcontroller);
            if(!mmI2Cvalue)classicread=1;
        } else if(classicread==1){
			WiiReceive(6, (char *)nunbuff);
            if(!mmI2Cvalue)classicread=2;
            else classicread=0;
        } else {
            classicproc();
            classicread=0;
            classic1=2;
        }
        ClassicTimer=0; 
    }
    if(nunchuck1 && NunchuckTimer>=10){ 
        if(nunchuckread==false){
			WiiSend(sizeof(readcontroller),(char *)readcontroller);
            if(!mmI2Cvalue)nunchuckread=1;
        } else if(nunchuckread==1){
			WiiReceive(6, (char *)nunbuff);
            if(!mmI2Cvalue)nunchuckread=2;
            else nunchuckread=0;
        } else {
            nunproc();
            nunchuckread=0;
            nunchuck1=2;
        }
        NunchuckTimer=0;
    }
/*frame
    if(frame && CurrentLinePtr)ShowCursor(framecursor);
*/
}

int __not_in_flash_func(getConsole)(void) {
    int c=-1;
#ifdef PICOMITEWEB
    ProcessWeb(1);
#endif
    CheckAbort();
    if(ConsoleRxBufHead != ConsoleRxBufTail) {                            // if the queue has something in it
        c = ConsoleRxBuf[ConsoleRxBufTail];
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;   // advance the head of the queue
	}
    return c;
}

void __not_in_flash_func(putConsole)(int c, int flush) {
    if(OptionConsole & 2)DisplayPutC(c);
    if(OptionConsole & 1)SerialConsolePutC(c, flush);
}
// put a character out to the serial console
char  __not_in_flash_func(SerialConsolePutC)(char c, int flush) {
	if(c == '\b') {
	   	if (MMCharPos!=1){
		   MMCharPos -= 1;
	   	}
	}    
#ifdef PICOMITEWEB
    if(Option.Telnet!=-1){
#endif
#ifndef USBKEYBOARD
    if(Option.SerialConsole==0 || Option.SerialConsole>4){
        if(tud_cdc_connected()){
            putc(c,stdout);
            if(flush){
                fflush(stdout);
            }
        }
    } 
#endif
    if(Option.SerialConsole){
        int empty=uart_is_writable((Option.SerialConsole & 3)==1 ? uart0 : uart1);
		while(ConsoleTxBufTail == ((ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE)); //wait if buffer full
		ConsoleTxBuf[ConsoleTxBufHead] = c;							// add the char
		ConsoleTxBufHead = (ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE;		   // advance the head of the queue
		if(empty){
            while(irqs){}
	        uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, true, true);
			irq_set_pending((Option.SerialConsole & 3)==1 ? UART0_IRQ : UART1_IRQ);
		}
    }
#ifdef PICOMITEWEB
    }
    TelnetPutC(c,flush);
    ProcessWeb(1);
#endif
    return c;
}
char MMputchar(char c, int flush) {
    putConsole(c, flush);
    if(isprint(c)) MMCharPos++;
    if(c == '\r') {
        MMCharPos = 1;
    }
    return c;
}
// returns the number of character waiting in the console input queue
int kbhitConsole(void) {
    int i;
    i = ConsoleRxBufHead - ConsoleRxBufTail;
    if(i < 0) i += CONSOLE_RX_BUF_SIZE;
    return i;
}
// check if there is a keystroke waiting in the buffer and, if so, return with the char
// returns -1 if no char waiting
// the main work is to check for vt100 escape code sequences and map to Maximite codes
#if (defined(PICOMITEVGA) || defined(PICOMITEWEB)) && !defined(rp2350)
int MMInkey(void) {
#else
int __not_in_flash_func(MMInkey)(void) {
#endif
    unsigned int c = -1;                                            // default no character
    unsigned int tc = -1;                                           // default no character
    unsigned int ttc = -1;                                          // default no character
    static unsigned int c1 = -1;
    static unsigned int c2 = -1;
    static unsigned int c3 = -1;
    static unsigned int c4 = -1;
//	static int crseen = 0;

    if(c1 != -1) {                                                  // check if there are discarded chars from a previous sequence
        c = c1; c1 = c2; c2 = c3; c3 = c4; c4 = -1;                 // shuffle the queue down
        return c;                                                   // and return the head of the queue
    }

    c = getConsole();                                               // do discarded chars so get the char
#ifndef USBKEYBOARD
    if(c==-1)CheckKeyboard();
#endif
    if(!(c==0x1b))return c;
    InkeyTimer = 0;                                             // start the timer
    while((c = getConsole()) == -1 && InkeyTimer < 30);         // get the second char with a delay of 30mS to allow the next char to arrive
    if(c == 'O'){   //support for many linux terminal emulators
        while((c = getConsole()) == -1 && InkeyTimer < 50);        // delay some more to allow the final chars to arrive, even at 1200 baud
        if(c == 'P') return F1;
        if(c == 'Q') return F2;
        if(c == 'R') return F3;
        if(c == 'S') return F4;
        if(c == 'T') return F5;
        if(c == '2'){
            while((tc = getConsole()) == -1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
            if(tc == 'R') return F3 + 0x20;
            c1 = 'O'; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
        }
        c1 = 'O'; c2 = c; return 0x1b;                 // not a valid 4 char code
    }
    if(c != '[') { c1 = c; return 0x1b; }                       // must be a square bracket
    while((c = getConsole()) == -1 && InkeyTimer < 50);         // get the third char with delay
    if(c == 'A') return UP;                                     // the arrow keys are three chars
    if(c == 'B') return DOWN;
    if(c == 'C') return RIGHT;
    if(c == 'D') return LEFT;
    if(c < '1' && c > '6') { c1 = '['; c2 = c; return 0x1b; }   // the 3rd char must be in this range
    while((tc = getConsole()) == -1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
    if(tc == '~') {                                             // all 4 char codes must be terminated with ~
        if(c == '1') return HOME;
        if(c == '2') return INSERT;
        if(c == '3') return DEL;
        if(c == '4') return END;
        if(c == '5') return PUP;
        if(c == '6') return PDOWN;
        c1 = '['; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
    }
    while((ttc = getConsole()) == -1 && InkeyTimer < 90);       // get the 5th char with delay
    if(ttc == '~') {                                            // must be a ~
        if(c == '1') {
            if(tc >='1' && tc <= '5') return F1 + (tc - '1');   // F1 to F5
            if(tc >='7' && tc <= '9') return F6 + (tc - '7');   // F6 to F8
        }
        if(c == '2') {
            if(tc =='0' || tc == '1') return F9 + (tc - '0');   // F9 and F10
            if(tc =='3' || tc == '4') return F11 + (tc - '3');  // F11 and F12
            if(tc =='5' || tc=='6') return F3 + 0x20 + tc-'5';                      // SHIFT-F3 and F4
            if(tc =='8' || tc=='9') return F5 + 0x20 + tc-'8';                      // SHIFT-F5 and F6
        }
        if(c == '3') {
            if(tc >='1' && tc <= '4') return F7 + 0x20 + (tc - '1');   // SHIFT-F7 to F10
        }
        //NB: SHIFT F1, F2, F11, and F12 don't appear to generate anything
    }
    // nothing worked so bomb out
    c1 = '['; c2 = c; c3 = tc; c4 = ttc;
    return 0x1b;
}
// get a line from the keyboard or a serial file handle
// filenbr == 0 means the console input
void MMgetline(int filenbr, char *p) {
	int c, nbrchars = 0;
	char *tp;

    while(1) {
        CheckAbort();	
        if(FileTable[filenbr].com > MAXCOMPORTS && FileEOF(filenbr)) break;
        c = MMfgetc(filenbr);
        if(c <= 0) continue;                                       // keep looping if there are no chars

        // if this is the console, check for a programmed function key and insert the text
        if(filenbr == 0) {
            tp = NULL;
            if(c == F2)  tp = "RUN";
            if(c == F3)  tp = "LIST";
            if(c == F4)  tp = "EDIT";
            if(c == F10) tp = "AUTOSAVE";
            if(c == F11) tp = "XMODEM RECEIVE";
            if(c == F12) tp = "XMODEM SEND";
            if(c == F1) tp = (char *)Option.F1key;
            if(c == F5) tp = (char *)Option.F5key;
            if(c == F6) tp = (char *)Option.F6key;
            if(c == F7) tp = (char *)Option.F7key;
            if(c == F8) tp = (char *)Option.F8key;
            if(c == F9) tp = (char *)Option.F9key;
            if(tp) {
                strcpy(p, tp);
                if(EchoOption) { MMPrintString(tp); MMPrintString("\r\n"); }
                return;
            }
        }

		if(c == '\t') {												// expand tabs to spaces
			 do {
				if(++nbrchars > MAXSTRLEN) error("Line is too long");
				*p++ = ' ';
				if(filenbr == 0 && EchoOption) MMputchar(' ',1);
			} while(nbrchars % 4);
			continue;
		}

		if(c == '\b') {												// handle the backspace
			if(nbrchars) {
				if(filenbr == 0 && EchoOption) MMPrintString("\b \b");
				nbrchars--;
				p--;
			}
			continue;
		}

        if(c == '\n') {                                             // what to do with a newline
                break;                                              // a newline terminates a line (for a file)
        }

        if(c == '\r') {
            if(filenbr == 0 && EchoOption) {
                MMPrintString("\r\n");
                break;                                              // on the console this means the end of the line - stop collecting
            } else
                continue ;                                          // for files loop around looking for the following newline
        }
        
		if(isprint(c)) {
			if(filenbr == 0 && EchoOption) MMputchar(c,1);           // The console requires that chars be echoed
		}
		if(++nbrchars > MAXSTRLEN) error("Line is too long");		// stop collecting if maximum length
		*p++ = c;													// save our char
	}
	*p = 0;
}

// get a keystroke.  Will wait forever for input
// if the unsigned char is a cr then replace it with a newline (lf)
int MMgetchar(void) {
	int c;
	do {
		ShowCursor(1);
		c=MMInkey();
	} while(c == -1);
	ShowCursor(0);
	return c;
}
// print a string to the console interfaces
void MMPrintString(char* s) {
    while(*s) {
        if(s[1])MMputchar(*s,0);
        else MMputchar(*s,1);
        s++;
    }
    fflush(stdout);
}
void SSPrintString(char* s) {
    while(*s) {
        SerialConsolePutC(*s,0);
        s++;
    }
    fflush(stdout);
}

/*void myprintf(char *s){
   fputs(s,stdout);
     fflush(stdout);
}*/

void __not_in_flash_func(mT4IntEnable)(int status){
	if(status){
		processtick=true;
	} else{
		processtick=false;
	}
}

volatile int onoff=0;
bool MIPS16 __not_in_flash_func(timer_callback)(repeating_timer_t *rt)
{
    mSecTimer++;                                                      // used by the TIMER function
    if(processtick){
        static int IrTimeout, IrTick, NextIrTick;
        int ElapsedMicroSec, IrDevTmp, IrCmdTmp;
#ifdef rp2350
    if(ExtCurrentConfig[FAST_TIMER_PIN] == EXT_FAST_TIMER && --INT5Timer <= 0) { 
        static uint64_t now,last=0;
        uint32_t hi = INT5Count;
        uint32_t lo;
        do {
            // Read the lower 32 bits
            lo = pwm_get_counter(0);
            // Now read the upper 32 bits again and
            // check that it hasn't incremented. If it has loop around
            // and read the lower 32 bits again to get an accurate value
            uint32_t next_hi = INT5Count;
            if (hi == next_hi) break;
            hi = next_hi;
        } while (true);
        now=((uint64_t) hi *50000) + lo;
        INT5Value=now-last;
        last=now;
        INT5Timer = INT5InitTimer; 
    }
#if defined(PICOMITE) && defined(rp2350)
    if(Option.LOCAL_KEYBOARD && mSecTimer % LOCALKEYSCANRATE==0)cmd_keyscan();
#endif
#endif
        AHRSTimer++;
        InkeyTimer++;                                                     // used to delay on an escape character
        PauseTimer++;													// used by the PAUSE command
        IntPauseTimer++;												// used by the PAUSE command inside an interrupt
        ds18b20Timer++;
		GPSTimer++;
        I2CTimer++;
#ifdef USBKEYBOARD
		keytimer++;
        for(int i=0;i<4;i++){
            if(HID[i].Device_type){ 
                HID[i].report_timer++;
            }
        }
#else
		nunstruct[2].type++;
        MouseTimer++;
#endif
        if(clocktimer)clocktimer--;
        if(Timer5)Timer5--;
        if(Timer4)Timer4--;
        if(Timer3)Timer3--;
        if(Timer2)Timer2--;
        if(Timer1)Timer1--;
        if(KeyCheck)KeyCheck--;
        ClassicTimer++;
        NunchuckTimer++;
        if(diskchecktimer && (Option.SD_CS || Option.CombinedCS))diskchecktimer--;
	    if(++CursorTimer > CURSOR_OFF + CURSOR_ON) CursorTimer = 0;		// used to control cursor blink rate
        if(CFuncmSec) CallCFuncmSec();                                  // the 1mS tick for CFunctions (see CFunction.c)
        if(InterruptUsed) {
            int i;
            for(i = 0; i < NBRSETTICKS; i++) if(TickActive[i])TickTimer[i]++;			// used in the interrupt tick
         }
    if(WDTimer) {
        if(--WDTimer == 0) {
            _excep_code = WATCHDOG_TIMEOUT;
            watchdog_enable(1, 1);
            while(1);
        }
    }
        if (ScrewUpTimer) {
            if (--ScrewUpTimer == 0) {
                _excep_code = SCREWUP_TIMEOUT;
                watchdog_enable(1, 1);
                while(1);
            }
        }
        if(PulseActive) {
            int i;
            for(PulseActive = i = 0; i < NBR_PULSE_SLOTS; i++) {
                if(PulseCnt[i] > 0) {                                   // if the pulse timer is running
                    PulseCnt[i]--;                                      // and decrement our count
                    if(PulseCnt[i] == 0)                                // if this is the last count reset the pulse
                        PinSetBit(PulsePin[i], LATINV);
                    else
                        PulseActive = true;                             // there is at least one pulse still active
                }
            }
        }
        ElapsedMicroSec = readIRclock();
        if(IrState > IR_WAIT_START && ElapsedMicroSec > 15000) IrReset();
        IrCmdTmp = -1;
        
        // check for any Sony IR receive activity
        if(IrState == SONY_WAIT_BIT_START && ElapsedMicroSec > 2800 && (IrCount == 12 || IrCount == 15 || IrCount == 20)) {
            IrDevTmp = ((IrBits >> 7) & 0b11111);
            IrCmdTmp = (IrBits & 0b1111111) | ((IrBits >> 5) & ~0b1111111);
        }
        
        // check for any NEC IR receive activity
        if(IrState == NEC_WAIT_BIT_END && IrCount == 32) {
            // check if it is a NON extended address and adjust if it is
            if((IrBits >> 24) == ~((IrBits >> 16) & 0xff)) IrBits = (IrBits & 0x0000ffff) | ((IrBits >> 8) & 0x00ff0000);
            IrDevTmp = ((IrBits >> 16) & 0xffff);
            IrCmdTmp = ((IrBits >> 8) & 0xff);
        }
#ifdef GUICONTROLS
    // check on the touch panel, is the pen down?

    TouchTimer++;
    if(CheckGuiFlag) CheckGuiTimeouts();                            // are blinking LEDs in use?  If so count down their timers

    if(TOUCH_GETIRQTRIS){                       // is touch enabled and the PEN IRQ pin an input?
        if(TOUCH_DOWN) {                                            // is the pen down
            if(!TouchState) {                                       // yes, it is.  If we have not reported this before
                TouchState = TouchDown = true;                      // set the flags
//                TouchUp = false;
            }
        } else {
            if(TouchState) {                                        // the pen is not down.  If we have not reported this before
                TouchState/* = TouchDown*/ = false;                     // set the flags
                TouchUp = true;
            }
        }
    }

    if(ClickTimer) {
        ClickTimer--;
        if(Option.TOUCH_Click) PinSetBit(Option.TOUCH_Click, ClickTimer ? LATSET : LATCLR);
    }
#endif
    // now process the IR message, this includes handling auto repeat while the key is held down
    // IrTick counts how many mS since the key was first pressed
    // NextIrTick is used to time the auto repeat
    // IrTimeout is used to detect when the key is released
    // IrGotMsg is a signal to the interrupt handler that an interrupt is required
    if(IrCmdTmp != -1) {
        if(IrTick > IrTimeout) {
            // this is a new keypress
            IrTick = 0;
            NextIrTick = 650;
        }
        if(IrTick == 0 || IrTick > NextIrTick) {
            if(IrVarType & 0b01)
                *(MMFLOAT *)IrDev = IrDevTmp;
            else
                *(long long int *)IrDev = IrDevTmp;
            if(IrVarType & 0b10)
                *(MMFLOAT *)IrCmd = IrCmdTmp;
            else
                *(long long int *)IrCmd = IrCmdTmp;
            IrGotMsg = true;
            NextIrTick += 250;
        }
        IrTimeout = IrTick + 150;
        IrReset();
    }
    IrTick++;
	if(ExtCurrentConfig[Option.INT1pin] == EXT_PER_IN) INT1Count++;
	if(ExtCurrentConfig[Option.INT2pin] == EXT_PER_IN) INT2Count++;
	if(ExtCurrentConfig[Option.INT3pin] == EXT_PER_IN) INT3Count++;
	if(ExtCurrentConfig[Option.INT4pin] == EXT_PER_IN) INT4Count++;
    if(ExtCurrentConfig[Option.INT1pin] == EXT_FREQ_IN && --INT1Timer <= 0) { INT1Value = INT1Count; INT1Count = 0; INT1Timer = INT1InitTimer; }
    if(ExtCurrentConfig[Option.INT2pin] == EXT_FREQ_IN && --INT2Timer <= 0) { INT2Value = INT2Count; INT2Count = 0; INT2Timer = INT2InitTimer; }
    if(ExtCurrentConfig[Option.INT3pin] == EXT_FREQ_IN && --INT3Timer <= 0) { INT3Value = INT3Count; INT3Count = 0; INT3Timer = INT3InitTimer; }
    if(ExtCurrentConfig[Option.INT4pin] == EXT_FREQ_IN && --INT4Timer <= 0) { INT4Value = INT4Count; INT4Count = 0; INT4Timer = INT4InitTimer; }

    ////////////////////////////////// this code runs once a second /////////////////////////////////
    if(++SecondsTimer >= 1000) {
        SecondsTimer -= 1000; 
    #ifndef PICOMITEWEB
        if(ExtCurrentConfig[PinDef[HEARTBEATpin].pin]==EXT_HEARTBEAT)gpio_xor_mask64(1<<PinDef[HEARTBEATpin].GPno);
    #endif
            // keep track of the time and date
/*        if(++second >= 60) {
            second = 0 ;
            if(++minute >= 60) {
                minute = 0;
                if(++hour >= 24) {
                    hour = 0;
                    if(++day > DaysInMonth[month] + ((month == 2 && (year % 4) == 0)?1:0)) {
                        day = 1;
                        if(++month > 12) {
                            month = 1;
                            year++;
                        }
                    }
                }
            }
        }*/
        }
    }
  return 1;
}
void __not_in_flash_func(uSec)(int us) {
#ifdef PICOMITEWEB
	if(us<500){
		busy_wait_us(us);
	} else {
    	uint64_t end=time_us_64()+us;
    	while(time_us_64()<end){
        if(time_us_64() % 500 ==0)ProcessWeb(1);
    }
}
#else
	busy_wait_us(us);
#endif
}
#ifdef PICOMITEWEB
void __not_in_flash_func(ProcessWeb)(int mode){
    static uint64_t flushtimer=0;
    static uint64_t lastusec=0;
    static int testcount=0;  
    static int lastonoff=0;
    static uint64_t lastheartmsec=0;
    uint64_t timenow=time_us_64();   
    if(!WIFIconnected && startupcomplete)goto flashonly;
    TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
    if(!state)return;
    int t=0;
    for(int i=0;i<MaxPcb;i++){
        if(state->client_pcb[i]==NULL){
                t++;
        } else if(state->client_pcb[i]==(struct tcp_pcb *)44){
            if(timenow-state->pcbopentime[i] > 1000*(uint32_t)Option.ServerResponceTime + 20000000 && !state->keepalive[i]){
                state->client_pcb[i]=NULL;
//                    printf("PCB %d should be closed by now\r\n", i);
            }
        } else {
            if(timenow-state->pcbopentime[i] > 1000*(uint32_t)Option.ServerResponceTime && !state->keepalive[i]){
//                    printf("Warning PCB %d still open\r\n", i);
                    if(state->buffer_recv[i]){
                            tcp_server_close(state,i);
                            error("No response to request from connection no. %",i+1);
//                            printf("Warning: No response to request from connection no. %d\r\n",i+1);
                    }
                    tcp_server_close(state,i);
                    state->client_pcb[i]=(struct tcp_pcb *)44;
            }
        }
    }
    if(testcount == 0 || timenow>lastusec){
        lastusec=timenow+1000;
        testcount = 0 ;
        if(startupcomplete)cyw43_arch_poll();
    }
    testcount++;
    if(testcount==100)testcount=0;
    if(!mode)return;
    if(state->telnet_pcb_no!=99){
        if(timenow > flushtimer){
            TelnetPutC(0,-1);
            flushtimer=timenow+5000;
        }
    }
    flashonly:;
    if(Option.NoHeartbeat){
        if(lastonoff!=2){
            if(startupcomplete){
                if(cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN)) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                lastonoff=2;
            }
        }
    } else {
        if(lastonoff==2)lastonoff=0;
        if(timenow-lastheartmsec>(WIFIconnected ? 500000:1000000) && startupcomplete){
            lastheartmsec=timenow;
            if(lastonoff)cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            else cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            lastonoff^=1;
        }
    }
}
#endif
void __not_in_flash_func(CheckAbort)(void) {
#ifdef PICOMITEWEB
    ProcessWeb(1);
#endif
    routinechecks();
    if(MMAbort) {
        WDTimer = 0;                                                // turn off the watchdog timer
        calibrate=0;
        ShowCursor(false);
        hal_display_merge_abort();
        do_end(false);
        longjmp(mark, 1);												// jump back to the input prompt
}
}
extern void bc_crash_save_fault(void);
extern void bc_crash_dump_if_any(void);
void sigbus(void){
    bc_crash_save_fault();
    MMPrintString("Error: Invalid address - resetting\r\n");
	uSec(5000000);
	disable_interrupts_pico();
//	flash_range_erase(PROGSTART, MAX_PROG_SIZE);
    LoadOptions();
    if(Option.NoReset==0){
        Option.Autorun=0;
        SaveOptions();
    }
	enable_interrupts_pico();
    memset(inpbuf,0,STRINGSIZE);
    SoftReset();
}

#ifdef PICOMITEVGA
int vgaloop1,vgaloop2, vgaloop4, vgaloop8, vgaloop16, vgaloop32;

#ifndef HDMI
// ****************************************************************************
//
//                                  QVGA
//
// ****************************************************************************
// VGA resolution:
// - 640x480 pixels
// - vertical frequency 60 Hz
// - horizontal frequency 31.4685 kHz
// - pixel clock 25.175 MHz
//
// QVGA resolution:
// - 320x240 pixels
// - vertical double image scanlines
// - vertical frequency 60 Hz
// - horizontal frequency 31.4685 kHz
// - pixel clock 12.5875 MHz
//
// VGA vertical timings:
// - 525 scanlines total
// - line 1,2: (2) vertical sync
// - line 3..35: (33) dark
// - line 36..515: (480) image lines 0..479
// - line 516..525: (10) dark
//
// VGA horizontal timings:
// - 31.77781 total scanline in [us] (800 pixels, QVGA 400 pixels)
// - 0.63556 H front porch (after image, before HSYNC) in [us] (16 pixels, QVGA 8 pixels)
// - 3.81334 H sync pulse in [us] (96 pixels, QVGA 48 pixels)
// - 1.90667 H back porch (after HSYNC, before image) in [us] (48 pixels, QVGA 24 pixels)
// - 25.42224 H full visible in [us] (640 pixels, QVGA 320 pixels)
// - 0.0397222625 us per pixel at VGA, 0.079444525 us per pixel at QVGA
//
// We want reach 25.175 pixel clock (at 640x480). Default system clock is 125 MHz, which is
// approx. 5x pixel clock. We need 25.175*5 = 125.875 MHz. We use nearest frequency 126 MHz.
//	126000, 1512000, 126, 6, 2,     // 126.00MHz, VC0=1512MHz, FBDIV=126, PD1=6, PD2=2
//	126000, 504000, 42, 4, 1,       // 126.00MHz, VC0=504MHz, FBDIV=42, PD1=4, PD2=1
//	sysclk=126.000000 MHz, vco=504 MHz, fbdiv=42, pd1=4, pd2=1
//	sysclk=126.000000 MHz, vco=504 MHz, fbdiv=42, pd1=2, pd2=2
//	sysclk=126.000000 MHz, vco=756 MHz, fbdiv=63, pd1=6, pd2=1
//	sysclk=126.000000 MHz, vco=756 MHz, fbdiv=63, pd1=3, pd2=2
//	sysclk=126.000000 MHz, vco=1008 MHz, fbdiv=84, pd1=4, pd2=2 !!!!!
//	sysclk=126.000000 MHz, vco=1260 MHz, fbdiv=105, pd1=5, pd2=2
//	sysclk=126.000000 MHz, vco=1512 MHz, fbdiv=126, pd1=6, pd2=2
//	sysclk=126.000000 MHz, vco=1512 MHz, fbdiv=126, pd1=4, pd2=3
// Pixel clock is now:
//      5 system clock ticks per pixel at VGA ... pixel clock = 25.2 MHz, 0.039683 us per pixel
//     10 system clock ticks per pixel at QVGA ... pixel clock = 12.6 MHz, 0.079365 us per pixel
//
// - active image is 640*5=3200 clock ticks = 25.3968 us (QVGA: 1600 clock ticks)
// - total scanline is 126*31.77781=4004 clock ticks (QVGA: 2002 clock ticks)
// - H front porch = 82 clock ticks (QVGA: 41 clock ticks)
// - H sync pulse = 480 clock ticks (QVGA: 240 clock ticks)
// - H back porch = 242 clock ticks (QVGA: 121 clock ticks)

// in case of system clock = 125 MHz
// - PIO clock = system clock / 2
// - 5 PIO clocks per pixel = 10 system clocks per pixel
// - PIO clock = 62.5 MHz
// - pixel clock = 12.5 MHz
// - active image (320 pixels): 320*5 = 1600 PIO clocks = 3200 system ticks = 25.6 us (2.2 pixels stays invisible)
// - total scanline: 125*31.77781 = 3972 system clocks = 1986 PIO clocks
// - H front porch = 34 PIO clk
// - H sync = 238 PIO clk
// - H back = 114 PIO clk

extern volatile int VGAscrolly;

// PIO command (jmp=program address, num=loop counter)
#define QVGACMD(jmp, num) ( ((uint32_t)((jmp)+QVGAOff)<<27) | (uint32_t)(num))

// display frame buffer

// pointer to current frame buffer
uint QVGAOff;	// offset of QVGA PIO program
// Scanline data buffers (commands sent to PIO)
uint32_t ScanLineImg[3];	// image: HSYNC ... back porch ... image command
uint32_t ScanLineFp;		// front porch
uint32_t ScanLineDark[2];	// dark: HSYNC ... back porch + dark + front porch
uint32_t ScanLineSync[2];	// vertical sync: VHSYNC ... VSYNC(back porch + dark + front porch)

// Scanline control buffers
#define CB_MAX 8	// size of one scanline control buffer (1 link to data buffer requires 2x uint32_t)
uint32_t ScanLineCB[2*CB_MAX]; // 2 control buffers
int QVgaBufInx;		// current running control buffer
uint32_t* ScanLineCBNext;	// next control buffer

// handler variables
volatile int QVgaScanLine; // current processed scan line 0... (next displayed scan line)
volatile uint32_t QVgaFrame;	// frame counter
#ifdef rp2350
uint16_t fbuff[2][212]={0};
#else
uint16_t fbuff[2][180]={0};
#endif
volatile int X_TILE=80, Y_TILE=40;
// saved integer divider state
// VGA DMA handler - called on end of every scanline
static int VGAnextbuf=0,VGAnowbuf=1, tile=0, tc=0;

void MIPS32 __not_in_flash_func(QVgaLine1)()
{
    int i,line;
    uint8_t l,d;
    uint8_t transparent16=(uint8_t)transparent;
#ifdef rp2350
    uint8_t s;
    uint8_t transparent16s=(uint8_t)transparents;
#endif
	// Clear the interrupt request for DMA control channel
	dma_hw->ints0 = (1u << QVGA_DMA_PIO);

	// update DMA control channel and run it
	dma_channel_set_read_addr(QVGA_DMA_CB, ScanLineCBNext, true);
	// save integer divider state
//	hw_divider_save_state(&SaveDividerState);

	// prepare control buffer to be processed
	uint32_t* cb = &ScanLineCB[QVgaBufInx*CB_MAX];
	// switch current buffer index (bufinx = current preparing buffer, MiniVgaBufInx = current running buffer)
    QVgaBufInx ^= 1;
	ScanLineCBNext = cb;

	// increment scanline (1..)
	line = QVgaScanLine+1; // current scanline
//	line++; 		// new current scanline
	if (line >= QVGA_VTOT) // last scanline?
	{
		QVgaFrame++;	// increment frame counter
		line = 0; 	// restart scanline
        tile=0;
        tc=0;
	}
	QVgaScanLine = line;	// store new scanline

	// check scanline
	line -= QVGA_VSYNC;
	if (line < 0)
	{
		// VSYNC
		*cb++ = 2;
		*cb++ = (uint32_t)&ScanLineSync[0];
	}
	else
	{
		// front porch and back porch
		line -= QVGA_VBACK;
		if ((line < 0) || (line >= QVGA_VACT))
		{
			// dark line
			*cb++ = 2;
			*cb++ = (uint32_t)&ScanLineDark[0];
		}

		// image scanlines
		else
		{
        // prepare image line
            if(DISPLAY_TYPE==SCREENMODE1){
                uint16_t *q=&fbuff[VGAnextbuf][0];
                volatile unsigned char *p=&DisplayBuf[line * vgaloop8];
                volatile unsigned char *pp=&LayerBuf[line * vgaloop8];
                if(tc==ytileheight){
                    tile++;
                    tc=0;
                }
                tc++;
                register int pos=tile*X_TILE;
                for(i=0;i<vgaloop16;i++){
                    register int d=*p++ | *pp++;
                    register int low= d & 0xF;
                    register int high=d >>4;
                    *q++=(M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]) ;
                    *q++=(M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
                    pos++;
                    d=*p++ | *pp++;
                    low= d & 0xF;
                    high=d++ >>4;
                    *q++=(M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]) ;
                    *q++=(M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
                    pos++;
                }
#ifdef rp2350
            } else if(DISPLAY_TYPE==SCREENMODE3){
                register unsigned char *p=&DisplayBuf[line * vgaloop2];
                register unsigned char *q=&LayerBuf[line * vgaloop2];
                register int low, high, low2, high2;
                register uint8_t *r=(uint8_t *)fbuff[VGAnextbuf];
                for(int i=0;i<vgaloop2;i++){
                    low= map16[p[i] & 0xF];
                    high=map16[(p[i] & 0xF0)>>4];
                    low2= map16[q[i] & 0xF];
                    high2=map16[(q[i] & 0xF0)>>4];
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
                    *r++=low | (high<<4);
                }
#endif
            } else { //mode 2
                line>>=1;
                register unsigned char *dd=&DisplayBuf[line * vgaloop4];
                register unsigned char *ll=&LayerBuf[line * vgaloop4];
#ifdef rp2350
                register unsigned char *ss=&SecondLayer[line * vgaloop4];
                if(ss==dd){
                    ss=ll;
                    transparent16s=transparent16;
                }
                register int low3, high3;
#endif
                register int low, high, low2, high2;
                register uint16_t *r=(uint16_t *)fbuff[VGAnextbuf];
                for(int i=0;i<vgaloop4;i+=2){
                    d=*dd++;
                    l=*ll++;
                    low= map16[d & 0xF];
                    d>>=4;
                    high=map16[d];
                    low2= map16[l & 0xF];
                    l>>=4;
                    high2=map16[l];
#ifdef rp2350
                    s=*ss++;
                    low3= map16[s & 0xF];
                    s>>=4;
                    high3=map16[s];
#endif
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
#ifdef rp2350
                    if(low3!=transparent16s)low=low3;
                    if(high3!=transparent16s)high=high3;
#endif
                    *r++=(low | (low<<4) | (high<<8) | (high<<12));
                    d=*dd++;
                    l=*ll++;
                    low= map16[d & 0xF];
                    d>>=4;
                    high=map16[d];
                    low2= map16[l & 0xF];
                    l>>=4;
                    high2=map16[l];
#ifdef rp2350
                    s=*ss++;
                    low3= map16[s & 0xF];
                    s>>=4;
                    high3=map16[s];
#endif
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
#ifdef rp2350
                    if(low3!=transparent16s)low=low3;
                    if(high3!=transparent16s)high=high3;
#endif
                    *r++=(low | (low<<4) | (high<<8) | (high<<12));
                }
            }
            VGAnextbuf ^=1;
            VGAnowbuf ^=1;

			// HSYNC ... back porch ... image command
			*cb++ = 3;
			*cb++ = (uint32_t)&ScanLineImg[0];

			// image data
			*cb++ = vgaloop8;
			*cb++ = (uint32_t)fbuff[VGAnowbuf];

			// front porch
			*cb++ = 1;
			*cb++ = (uint32_t)&ScanLineFp;
		}
	}

	// end mark
	*cb++ = 0;
	*cb = 0;

	// restore integer divider state
//	hw_divider_restore_state(&SaveDividerState);
}

void QVgaPioInit()
{
	int i;
    if((Option.CPU_Speed % 126000) ==0){
        QVGA_TOTAL= 4000;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 480;	// horizontal sync clock ticks
        QVGA_HACT = 640;
        QVGA_BP	= 240;	// back porch clock ticks
        QVGA_FP	= 80;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 10;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 33;	// V back porch
        QVGA_VTOT	= 525;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == Freq848){
        QVGA_TOTAL= 1088*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 112*5;	// horizontal sync clock ticks
        QVGA_HACT = 848;
        QVGA_BP	= 112*5;	// back porch clock ticks
        QVGA_FP	= 16*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 8;	// V front porch
        QVGA_VSYNC	= 6;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 23;	// V back porch
        QVGA_VTOT	= 517;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == FreqSVGA){
        QVGA_TOTAL= 1024*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 72*5;	// horizontal sync clock ticks
        QVGA_HACT = 800;
        QVGA_BP	= 128*5;	// back porch clock ticks
        QVGA_FP	= 24*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 600;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 1;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 22;	// V back porch
        QVGA_VTOT	= 625;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == Freq400){
        QVGA_TOTAL= 900*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 108*5;	// horizontal sync clock ticks
        QVGA_HACT = 720;
        QVGA_BP	= 54*5;	// back porch clock ticks
        QVGA_FP	= 18*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 400;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 12;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 35;	// V back porch
        QVGA_VTOT	= 449;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else {
        QVGA_TOTAL= 4200;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC =	320;	// horizontal sync clock ticks
        QVGA_HACT = 640;
        QVGA_BP	= 600;	// back porch clock ticks
        QVGA_FP	= 80;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 1;	// V front porch
        QVGA_VSYNC	= 3;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 16;	// V back porch
        QVGA_VTOT	= 500;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    }
	// load PIO program
#ifdef rp2350
    if(piomap[QVGA_PIO_NUM] & (uint64_t)0xFFFF00000000)pio_set_gpio_base(QVGA_PIO,16);
#endif
    I2SOff = pio_add_program(QVGA_PIO, &i2s_program);
	QVGAOff = pio_add_program(QVGA_PIO, &qvga_program);
	// configure GPIOs for use by PIO
	for (i = QVGA_GPIO_FIRST; i <= QVGA_GPIO_LAST; i++) pio_gpio_init(QVGA_PIO, i);
	pio_gpio_init(QVGA_PIO, QVGA_GPIO_HSYNC);
	pio_gpio_init(QVGA_PIO, QVGA_GPIO_VSYNC);

	// set pin direction to output
	pio_sm_set_consecutive_pindirs(QVGA_PIO, QVGA_SM, QVGA_GPIO_FIRST, QVGA_GPIO_NUM, true);
	pio_sm_set_consecutive_pindirs(QVGA_PIO, QVGA_SM, QVGA_GPIO_HSYNC, 2, true);

	// negate HSYNC and VSYNC output
    if(!(Option.CPU_Speed==Freq848 || Option.CPU_Speed==FreqSVGA)){
        gpio_set_outover(QVGA_GPIO_HSYNC, GPIO_OVERRIDE_INVERT);
        gpio_set_outover(QVGA_GPIO_VSYNC, GPIO_OVERRIDE_INVERT);
    }

	// prepare default PIO program config
	pio_sm_config cfg = qvga_program_get_default_config(QVGAOff);

	// map state machine's OUT and MOV pins	
	sm_config_set_out_pins(&cfg, QVGA_GPIO_FIRST, QVGA_GPIO_NUM);

	// set sideset pins (HSYNC and VSYNC)
	sm_config_set_sideset_pins(&cfg, QVGA_GPIO_HSYNC);

	// join FIFO to send only
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

	// PIO clock divider
	sm_config_set_clkdiv(&cfg, QVGA_CLKDIV);

	// shift right, autopull, pull threshold
	sm_config_set_out_shift(&cfg, true, true, 32);

	// initialize state machine
	pio_sm_init(QVGA_PIO, QVGA_SM, QVGAOff+qvga_offset_entry, &cfg);
}

// initialize scanline buffers
void QVgaBufInit()
{
	// image scanline data buffer: HSYNC ... back porch ... image command
	ScanLineImg[0] = QVGACMD(qvga_offset_hsync, QVGA_HSYNC-3); // HSYNC
	ScanLineImg[1] = QVGACMD(qvga_offset_dark, QVGA_BP-4); // back porch
	ScanLineImg[2] = QVGACMD(qvga_offset_output, QVGA_HACT-2); // image

	// front porch
	ScanLineFp = QVGACMD(qvga_offset_dark, QVGA_FP-4); // front porch

	// dark scanline: HSYNC ... back porch + dark + front porch
	ScanLineDark[0] = QVGACMD(qvga_offset_hsync, QVGA_HSYNC-3); // HSYNC
	ScanLineDark[1] = QVGACMD(qvga_offset_dark, QVGA_TOTAL-QVGA_HSYNC-4); // back porch + dark + front porch

	// vertical sync: VHSYNC ... VSYNC(back porch + dark + front porch)
	ScanLineSync[0] = QVGACMD(qvga_offset_vhsync, QVGA_HSYNC-3); // VHSYNC
	ScanLineSync[1] = QVGACMD(qvga_offset_vsync, QVGA_TOTAL-QVGA_HSYNC-3); // VSYNC(back porch + dark + front porch)

	// control buffer 1 - initialize to VSYNC
	ScanLineCB[0] = 2; // send 2x uint32_t (send ScanLineSync)
	ScanLineCB[1] = (uint32_t)&ScanLineSync[0]; // VSYNC data buffer
	ScanLineCB[2] = 0; // stop mark
	ScanLineCB[3] = 0; // stop mark

	// control buffer 1 - initialize to VSYNC
	ScanLineCB[CB_MAX+0] = 2; // send 2x uint32_t (send ScanLineSync)
	ScanLineCB[CB_MAX+1] = (uint32_t)&ScanLineSync[0]; // VSYNC data buffer
	ScanLineCB[CB_MAX+2] = 0; // stop mark
	ScanLineCB[CB_MAX+3] = 0; // stop mark
}

// initialize QVGA DMA
//   control blocks aliases:
//                  +0x0        +0x4          +0x8          +0xC (Trigger)
// 0x00 (alias 0):  READ_ADDR   WRITE_ADDR    TRANS_COUNT   CTRL_TRIG
// 0x10 (alias 1):  CTRL        READ_ADDR     WRITE_ADDR    TRANS_COUNT_TRIG
// 0x20 (alias 2):  CTRL        TRANS_COUNT   READ_ADDR     WRITE_ADDR_TRIG
// 0x30 (alias 3):  CTRL        WRITE_ADDR    TRANS_COUNT   READ_ADDR_TRIG ... we use this!
void QVgaDmaInit()
{

// ==== prepare DMA control channel
	// prepare DMA default config
	dma_channel_config cfg = dma_channel_get_default_config(QVGA_DMA_CB);

	// increment address on read from memory
	channel_config_set_read_increment(&cfg, true);

	// increment address on write to DMA port
	channel_config_set_write_increment(&cfg, true);

	// each DMA transfered entry is 32-bits
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);

	// write ring - wrap to 8-byte boundary (TRANS_COUNT and READ_ADDR_TRIG of data DMA)
	channel_config_set_ring(&cfg, true, 3);

	// DMA configure
	dma_channel_configure(
		QVGA_DMA_CB,		// channel
		&cfg,			// configuration
		&dma_hw->ch[QVGA_DMA_PIO].al3_transfer_count, // write address
		&ScanLineCB[0],		// read address - as first, control buffer 1 will be sent out
		2,			// number of transfers in uint32_t (number of transfers per one request from data DMA)
		false			// do not start yet
	);

// ==== prepare DMA data channel

	// prepare DMA default config

	cfg = dma_channel_get_default_config(QVGA_DMA_PIO);

	// increment address on read from memory
	channel_config_set_read_increment(&cfg, true);

	// do not increment address on write to PIO
	channel_config_set_write_increment(&cfg, false);

	// each DMA transfered entry is 32-bits
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);

	// DMA data request for sending data to PIO
	channel_config_set_dreq(&cfg, pio_get_dreq(QVGA_PIO, QVGA_SM, true));

	// chain channel to DMA control block
	channel_config_set_chain_to(&cfg, QVGA_DMA_CB);

	// raise the IRQ flag when 0 is written to a trigger register (end of chain)
	channel_config_set_irq_quiet(&cfg, true);

	// set high priority
	cfg.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;

	// DMA configure
	dma_channel_configure(
		QVGA_DMA_PIO,		// channel
		&cfg,			// configuration
		&QVGA_PIO->txf[QVGA_SM], // write address
		NULL,			// read address
		0,			// number of transfers in uint32_t
		false			// do not start immediately
	);

// ==== initialize IRQ0, raised from DMA data channel

	// enable DMA channel IRQ0
	dma_channel_set_irq0_enabled(QVGA_DMA_PIO, true);

	// set DMA IRQ handler
    irq_set_exclusive_handler(DMA_IRQ_0, QVgaLine1);
    vgaloop4=QVGA_HACT/4;
    vgaloop8=QVGA_HACT/8;
    vgaloop16=QVGA_HACT/16;
    vgaloop2=QVGA_HACT/2;
    MODE1SIZE=QVGA_HACT*QVGA_VACT/8;
    MODE2SIZE=(QVGA_HACT/2)*(QVGA_VACT/2)/2;
    MODE3SIZE=QVGA_HACT*QVGA_VACT/2;
    HRes=QVGA_HACT;
    VRes=QVGA_VACT;
// set highest IRQ priority
	irq_set_priority(DMA_IRQ_0, 0);
}

// initialize QVGA (can change system clock)
void QVgaInit()
{
    X_TILE=Option.X_TILE;
    Y_TILE=Option.Y_TILE;
    ytileheight=(X_TILE==80 || X_TILE==90 || X_TILE==106 || X_TILE==100)? 12 : 16;
	// initialize PIO
	QVgaPioInit();

	// initialize scanline buffers
	QVgaBufInit();

	// initialize DMA
	QVgaDmaInit();

	// initialize parameters
	QVgaScanLine = 0; // currently processed scanline
	QVgaBufInx = 0; // at first, control buffer 1 will be sent out
	QVgaFrame = 0; // current frame
	ScanLineCBNext = &ScanLineCB[CB_MAX]; // send control buffer 2 next

	// enable DMA IRQ
	irq_set_enabled(DMA_IRQ_0, true);

	// start DMA
	dma_channel_start(QVGA_DMA_CB);

	// run state machine
	pio_sm_set_enabled(QVGA_PIO, QVGA_SM, true);
}

void (* volatile Core1Fnc)() = NULL; // core 1 remote function

/* QVgaCore (core1 QVGA scanout entry) + its 128-word core1stack live
 * in drivers/vga_pio/vga_qvga_modes.c — see phase-8 notes. The launch
 * site below references the QVgaCore symbol via the extern in
 * Hardware_Includes.h. QVgaInit() still lives in this file and is
 * called from the driver on core1 start-up. */
#endif /* !HDMI — QVGA scanout */
#else
/* UpdateCore (core1 merge receiver) + its 512-word stack live in
 * drivers/display_merge/display_merge_pico.c — see phase-8 notes. The
 * launch site below still references the UpdateCore symbol via the
 * extern in Hardware_Includes.h. */
#endif
#ifndef rp2350
void __no_inline_not_in_flash_func(modclock)(uint16_t speed){
       ssi_hw->ssienr=0;
       ssi_hw->baudr=0;
       ssi_hw->baudr=speed;
       ssi_hw->ssienr=1;
}
#else
#ifndef PICOMITEWEB
uint32_t testPSRAM(void){
    uint32_t *p=(uint32_t *)PSRAMbase;
    uint32_t *q=(uint32_t *)PSRAMbase;
    for(int i=0;i<65536;i++)*p++=i;
    __dmb();
    for(int i=0;i<65536;i++)if(*q++!=i) return 0;
    p=(uint32_t *)PSRAMbase;
    q=(uint32_t *)PSRAMbase;
    p[8*1024*1024/4-1]=0x12345678;
    __dmb();
    if(q[8*1024*1024/4-1]==0x12345678)return 6*1024*1024;
    else return 0;
}
#endif
#endif
lfs_t lfs;
lfs_dir_t lfs_dir;
struct lfs_info lfs_info;
void MIPS16 updatebootcount(void){
    lfs_file_t lfs_file;
    pico_lfs_cfg.block_count = (Option.FlashSize-RoundUpK4(TOP_OF_SYSTEM_FLASH)-(Option.modbuff ? 1024*Option.modbuffsize : 0))/4096;
    int err,boot_count=0;
 	    err= lfs_mount(&lfs, &pico_lfs_cfg);
    // reformat if we can't mount the filesystem
    // this should only happen on the first boot
    if (err) {
        err=lfs_format(&lfs, &pico_lfs_cfg);
        err=lfs_mount(&lfs, &pico_lfs_cfg);
    }

    err=lfs_file_open(&lfs, &lfs_file, "bootcount", LFS_O_RDWR | LFS_O_CREAT);;
    int dt=get_fattime();
    err=lfs_setattr(&lfs, "bootcount", 'A', &dt,   4);
    err=lfs_file_read(&lfs, &lfs_file, &boot_count, sizeof(boot_count));;
    boot_count+=1;
    err=lfs_file_rewind(&lfs, &lfs_file);
    err=lfs_file_write(&lfs, &lfs_file, &boot_count, sizeof(boot_count));
    err=lfs_file_close(&lfs, &lfs_file);
}
/**
 * @brief Transforms input beginning with * into a corresponding RUN command.
 *
 * e.g.
 *   *foo              =>  RUN "foo"
 *   *"foo bar"        =>  RUN "foo bar"
 *   *foo --wombat     =>  RUN "foo", "--wombat"
 *   *foo "wom"        =>  RUN "foo", Chr$(34) + "wom" + Chr$(34)
 *   *foo "wom" "bat"  =>  RUN "foo", Chr$(34) + "wom" + Chr$(34) + " " + Chr$(34) + "bat" + Chr$(34)
 *   *foo --wom="bat"  =>  RUN "foo", "--wom=" + Chr$(34) + "bat" + Chr$(34)
 */

#ifdef PICOMITEWEB
void WebConnect(void){
    if(*Option.SSID){
        if(*Option.ipaddress){
            cyw43_arch_enable_sta_mode();
            dhcp_stop(cyw43_state.netif);
            ip4_addr_t ipaddr, gateway, mask;
            ip4addr_aton(Option.ipaddress, &ipaddr);
            ip4addr_aton(Option.gateway, &gateway);
            ip4addr_aton(Option.mask, &mask);
            netif_set_addr( cyw43_state.netif,&ipaddr,&mask,&gateway);
        } else cyw43_arch_enable_sta_mode();
        if(*Option.hostname){
            MMPrintString(Option.hostname);
            netif_set_hostname(cyw43_state.netif, Option.hostname);
        }
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);        
        MMPrintString(" connecting to WiFi...\r\n");
        if (cyw43_arch_wifi_connect_timeout_ms((char *)Option.SSID, (char *)(*Option.PASSWORD ? Option.PASSWORD : NULL), (*Option.PASSWORD ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN), 30000)) {
            MMPrintString("failed to connect.\r\n");
            WIFIconnected=0;
        } else {
            char buff[STRINGSIZE]={0};
            sprintf(buff,"Connected %s\r\n",ip4addr_ntoa(netif_ip4_addr(netif_list)));
            MMPrintString(buff);
            WIFIconnected=1;
            open_tcp_server();
            if(!Option.disabletftp)cmd_tftp_server_init();
            if(Option.UDP_PORT)open_udp_server();
        }
    } else {
        cyw43_arch_enable_sta_mode();
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);        
    }
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf);
}
#endif

int MIPS16 main(){
    int i=0;
    /* ErrorInPrompt + savewatchdog moved with the prompt loop to
     * MMBasic_REPL.c. */
        i=watchdog_caused_reboot();
#ifdef rp2350
    restart_reason=powman_hw->chip_reset | i;
    rp2350a=(*((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1);
#else
    restart_reason=vreg_and_chip_reset_hw->chip_reset | i;
#endif
    if(_excep_code == SOFT_RESET || _excep_code == SCREWUP_TIMEOUT )restart_reason=0xFFFFFFFF;
    if((_excep_code == WATCHDOG_TIMEOUT) & i) restart_reason=0xFFFFFFFE;
    if((_excep_code == POSSIBLE_WATCHDOG) & i)restart_reason=0xFFFFFFFD;
    LoadOptions();
#ifdef rp2350
    if(rom_get_last_boot_type()==BOOT_TYPE_FLASH_UPDATE)restart_reason=0xFFFFFFFC;
#else
    if(restart_reason==0x10001 || restart_reason==0x101)restart_reason=0xFFFFFFFC;
#endif
    uint32_t excep=_excep_code;
    if(  Option.Baudrate == 0 ||
        !(Option.Tab==2 || Option.Tab==3 || Option.Tab==4 ||Option.Tab==8) ||
        !(Option.Autorun>=0 && Option.Autorun<=MAXFLASHSLOTS+1) ||
        Option.CPU_Speed<MIN_CPU || Option.CPU_Speed>MAX_CPU ||
        Option.PROG_FLASH_SIZE!=MAX_PROG_SIZE ||
        (Option.heartbeatpin==0 && Option.NoHeartbeat==0) ||
        !(Option.Magic==MagicKey)
        ){
        ResetAllFlash();              // init the options if this is the very first startup
        _excep_code=0;
        watchdog_enable(1, 1);
        while(1);
    }
#ifndef HDMI
    if(Option.VGA_HSYNC==0){
        Option.VGA_HSYNC=21;
        Option.VGA_BLUE=24;
        SaveOptions();
    }
#else
    if(!(Option.CPU_Speed==Freq720P || Option.CPU_Speed==Freq378P || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq848  || Option.CPU_Speed==Freq400 || Option.CPU_Speed==FreqSVGA || Option.CPU_Speed==Freq480P|| Option.CPU_Speed==FreqXGA || Option.CPU_Speed==FreqX  || Option.CPU_Speed==FreqY)){
        Option.CPU_Speed=Freq480P;
        SaveOptions();
    }
#endif
    m_alloc(M_PROG);                                           // init the variables for program memory
    LibMemory = (uint8_t *)flash_libmemory;
    uSec(100);
    if(_excep_code == RESET_CLOCKSPEED) {
#ifdef PICOMITEVGA
#ifdef HDMI
        Option.CPU_Speed=Freq480P;              // init the options if this is the very first startup
#else
        Option.CPU_Speed=Freq252P;              // init the options if this is the very first startup
#endif
#else
        Option.CPU_Speed=200000;              // init the options if this is the very first startup
#endif
        SaveOptions();
        _excep_code=INVALID_CLOCKSPEED;
        watchdog_enable(1, 1);
        while(1);
    } else {
        _excep_code=RESET_CLOCKSPEED;
        watchdog_enable(1000, 1);
    }
#ifdef rp2350
    if(!rp2350a){
        if(!Option.AllPins){
            Option.AllPins=true;
            SaveOptions();
        }
    }
#endif
    vreg_disable_voltage_limit ();
    if(Option.CPU_Speed<=200000)vreg_set_voltage(VREG_VOLTAGE_1_15);
    else if(Option.CPU_Speed>200000  && Option.CPU_Speed<=320000 )vreg_set_voltage(VREG_VOLTAGE_1_30);  // Std default @ boot is 1_10
#ifdef rp2350
    else if(Option.CPU_Speed>320000  && Option.CPU_Speed<=360000 )vreg_set_voltage(VREG_VOLTAGE_1_40);  // Std default @ boot is 1_10
    else vreg_set_voltage(VREG_VOLTAGE_1_60);  // Std default @ boot is 1_10
#else
    else vreg_set_voltage(VREG_VOLTAGE_1_30); 
#endif
    sleep_ms(10);
#ifdef rp2350
    pads_qspi_hw->io[0]=0x67;
    pads_qspi_hw->io[1]=0x67;
    pads_qspi_hw->io[2]=0x67;
    pads_qspi_hw->io[3]=0x6B;
    pads_qspi_hw->io[4]=0x6B;
    pads_qspi_hw->io[5]=0x6B;
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    sleep_ms(2);
#endif
#if defined(HDMI) && defined(rp2350)
    set_sys_clock_khz(Option.CPU_Speed==FreqX ? 252000 : Option.CPU_Speed, false);
#else
    set_sys_clock_khz(Option.CPU_Speed, false);
#endif
#ifdef rp2350
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    sleep_ms(2);
#endif
    PWM_FREQ=44100;
    pico_get_unique_board_id_string (id_out,12);
#ifdef rp2350
#ifndef PICOMITEWEB
    if(Option.PSRAM_CS_PIN){
        PSRAMpin=PinDef[Option.PSRAM_CS_PIN].GPno;
        psram_setup();
        if(!(PSRAMsize=psram_size())){
            Option.PSRAM_CS_PIN=0;
            SaveOptions();
        } else PSRAMsize-=2*1024*1024;
    }
#endif
#endif
    if(clock_get_hz(clk_usb)!=48000000){
        ResetAllFlash();              // init the options if this is the very first startup
        _excep_code=INVALID_CLOCKSPEED;
        watchdog_enable(1, 1);
        while(1);
    }
    clock_configure(
        clk_adc,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        Option.CPU_Speed * 1000,                               // Input frequency
        ADC_CLK_SPEED                                 // Output (must be same as no divider)
    );
    SetADCFreq(500000);
    adc_clk_div=adc_hw->div;
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
#ifdef PICOMITE
	mutex_init( &frameBufferMutex );						// create a mutex to lock frame buffer
#endif


#ifndef rp2350
    if(Option.CPU_Speed<=200000)modclock(2);
#else
#if defined(PICOMITE) && defined(rp2350)
    if(Option.DISPLAY_TYPE>=NEXTGEN){ //adjust the size of the heap
        framebuffersize=display_details[Option.DISPLAY_TYPE].horizontal*display_details[Option.DISPLAY_TYPE].vertical;
        heap_memory_size-=framebuffersize;
        FRAMEBUFFER=AllMemory+heap_memory_size+256;
    }
#endif
#ifdef HDMI
    if((FullColour || MediumRes) && !(Option.CPU_Speed==FreqX)){
        clock_configure(
            clk_hstx,
            0,                                                // No glitchless mux
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
            Option.CPU_Speed * 1000,                               // Input frequency
            Option.CPU_Speed * (Option.CPU_Speed==Freq378P ? 332:500) // Output (must be same as no divider)
        );
    }
    if(Option.CPU_Speed==FreqSVGA){ //adjust the size of the heap
        framebuffersize=400*300*2;
        heap_memory_size=HEAP_MEMORY_SIZE-framebuffersize+320*240*2;
        FRAMEBUFFER=AllMemory+heap_memory_size+256;
    }
    if(Option.CPU_Speed==Freq848){ //adjust the size of the heap
        framebuffersize=424*240*2;
        heap_memory_size=HEAP_MEMORY_SIZE-framebuffersize+320*240*2;
        FRAMEBUFFER=AllMemory+heap_memory_size+256;
    }
    if(Option.CPU_Speed==FreqY){ //adjust the size of the heap
        framebuffersize=400*240*2;
        heap_memory_size=HEAP_MEMORY_SIZE-framebuffersize+320*240*2;
        FRAMEBUFFER=AllMemory+heap_memory_size+256;
    }
#endif 
#endif
    uSec(100);
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    _excep_code=excep;
#ifdef PICOMITEVGA
#ifndef HDMI
    if(Option.CPU_Speed == Freq252P || Option.CPU_Speed == Freq480P  || Option.CPU_Speed == Freq848  || Option.CPU_Speed == Freq400   || Option.CPU_Speed == FreqSVGA )QVGA_CLKDIV= 2;
    else if(Option.CPU_Speed == 378000)QVGA_CLKDIV= 3;
    else QVGA_CLKDIV= 1;
#ifdef rp2350
if(Option.CPU_Speed==Freq848){ //adjust the size of the heap
    framebuffersize=424*240*2;
    heap_memory_size=HEAP_MEMORY_SIZE-framebuffersize+320*240*2;
    FRAMEBUFFER=AllMemory+heap_memory_size+256;
    MODE1SIZE=MODE1SIZE_8;
    MODE2SIZE=MODE2SIZE_8;
    MODE2SIZE=MODE2SIZE_8;
    MODE2SIZE=MODE2SIZE_8;
    MODE2SIZE=MODE2SIZE_8;
    HRes=848;
}
if(Option.CPU_Speed==FreqSVGA){ //adjust the size of the heap
    framebuffersize=400*300*2;
    heap_memory_size=HEAP_MEMORY_SIZE-framebuffersize+320*240*2;
    FRAMEBUFFER=AllMemory+heap_memory_size+256;
    MODE1SIZE=MODE1SIZE_V;
    MODE2SIZE=MODE2SIZE_V;
    MODE3SIZE=MODE3SIZE_V;
    MODE5SIZE=MODE5SIZE_V;
    HRes=800;
    VRes=600;
}

#endif
#endif
#endif
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
    ticks_per_second = Option.CPU_Speed*1000;
    // The serial clock won't vary from this point onward, so we can configure
    // the UART etc.
#ifndef USBKEYBOARD
    stdio_set_translate_crlf(&stdio_usb, false);
#endif
    LoadOptions();
	stdio_init_all();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    mSecTimer=time_us_64()/1000;
    add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
    InitReservedIO();
    ClearExternalIO();
    ConsoleRxBufHead = 0;
    ConsoleRxBufTail = 0;
    ConsoleTxBufHead = 0;
    ConsoleTxBufTail = 0;
    PromptFC=gui_fcolour=Option.DefaultFC;
    PromptBC=gui_bcolour=Option.DefaultBC;
    InitHeap(true);              										// initilise memory allocation
    uSecFunc(1000);
    disable_interrupts_pico();
    enable_interrupts_pico();
    mSecTimer=time_us_64()/1000;
    DISPLAY_TYPE = Option.DISPLAY_TYPE;
    // negative timeout means exact delay (rather than delay between callbacks)
	OptionErrorSkip = false;
#ifndef USBKEYBOARD
    if(!(Option.SerialConsole==1 || Option.SerialConsole==2) || Option.Telnet==-1) {
        uint64_t t=time_us_64();
        while(1){
            if(tud_cdc_connected())break;
            if(time_us_64()-t>5000000)break;
        }
    }
    hal_keyboard_init();
#endif
	InitBasic();
#ifndef PICOMITEVGA
#ifndef PICOCALC
    InitDisplaySSD();
#endif
    InitDisplaySPI(0);
#ifndef PICOCALC
    InitDisplayI2C(0);
    InitDisplayVirtual();
#endif
    InitTouch();
#ifdef PICOCALC
    uSec(300000);
#endif
    if(Option.BackLightLevel)setBacklight(Option.BackLightLevel, 0);
#endif
    /* ErrorInPrompt is a static inside MMBasic_RunPromptLoop; initialized there. */
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION,sigbus);
    exception_set_exclusive_handler(SVCALL_EXCEPTION,sigbus);
    exception_set_exclusive_handler(PENDSV_EXCEPTION,sigbus);
    exception_set_exclusive_handler(NMI_EXCEPTION ,sigbus);
    exception_set_exclusive_handler(SYSTICK_EXCEPTION,sigbus);
    while((i=getConsole())!=-1){}
    
#ifdef PICOMITEVGA
//        bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    #ifdef HDMI
//    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_PROC1_BITS;
        multicore_launch_core1_with_stack(HDMICore,core1stack,512);
        core1stack[0]=0x12345678;
        uSec(1000);
        #else
    #ifdef rp2350
    piomap[QVGA_PIO_NUM]=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.VGA_BLUE].GPno);
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+1));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+2));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+3));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.VGA_HSYNC].GPno);
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_HSYNC].GPno+1));
    if(Option.audio_i2s_bclk){
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_data].GPno);
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_bclk].GPno);
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.audio_i2s_bclk].GPno+1));
    }
    #endif        
        X_TILE=Option.X_TILE;
        Y_TILE=Option.Y_TILE;
        ytileheight=(X_TILE==80 || X_TILE==106)? 12 : 16;
        bus_ctrl_hw->priority=0x100;
        multicore_launch_core1_with_stack(QVgaCore,core1stack,512);
        core1stack[0]=0x12345678;
        memset((void *)WriteBuf, 0, 38400);
    #endif
    ResetDisplay();
    ClearScreen(Option.DefaultBC);
    #else
    #ifdef PICOMITE
        bus_ctrl_hw->priority=0x100;
        multicore_launch_core1_with_stack(UpdateCore,core1stack,2048);
        core1stack[0]=0x12345678;
    #endif
#endif
        strcpy((char *)banner,MES_SIGNON); 
#ifdef rp2350
    #ifdef PICOMITEVGA
        #ifdef HDMI
            #ifdef USBKEYBOARD
                banner[32]=(rp2350a?'A':'B');
            #else
                banner[28]=(rp2350a?'A':'B');
            #endif
        #else
            #ifdef USBKEYBOARD
                banner[31]=(rp2350a?'A':'B');
            #else
                banner[27]=(rp2350a?'A':'B');
            #endif
        #endif
    #else
        #ifdef USBKEYBOARD
            banner[28]=(rp2350a?'A':'B');
        #else
            #ifdef PICOMITEWEB
                banner[23]=(rp2350a?'A':'B');
            #else
                banner[24]=(rp2350a?'A':'B');
            #endif
        #endif
    #endif
#endif
    extern void MMBasic_PrintBanner(void);
    if(!(_excep_code == RESTART_NOAUTORUN || _excep_code == INVALID_CLOCKSPEED || _excep_code == SCREWUP_TIMEOUT || _excep_code == WATCHDOG_TIMEOUT || (_excep_code==POSSIBLE_WATCHDOG && watchdog_caused_reboot()))){
        if(Option.Autorun==0 ){
            if(!(_excep_code == RESET_COMMAND || _excep_code == SOFT_RESET)){
                MMBasic_PrintBanner();
            }
        } else {
            if(Option.Autorun!=MAXFLASHSLOTS+1){
                ProgMemory=(unsigned char *)(flash_target_contents+(Option.Autorun-1)*MAX_PROG_SIZE);
            }
            if(*ProgMemory != 0x01 ) {
                MMBasic_PrintBanner();
            }
        }
    }
    bc_crash_dump_if_any();
    memset(inpbuf,0,STRINGSIZE);
    WatchdogSet = false;
    if(_excep_code == INVALID_CLOCKSPEED) {
        MMPrintString("\r\nInvalid clock speed - reset to default\r\n");
        restart_reason=0xFFFFFFFF;
    }
    if(_excep_code == SCREWUP_TIMEOUT) {
        MMPrintString("\r\nCommand timeout\r\n");
        restart_reason=0xFFFFFFFF;
    }
    if(restart_reason==0xFFFFFFFE) {
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        MMPrintString("\r\nMMBasic Watchdog timeout\r\n");
    }
    if(restart_reason==0xFFFFFFFD){
        MMPrintString("\r\nHW Watchdog timeout\r\n");
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        _excep_code=0;
    }
    if(restart_reason==0xFFFFFFFC) {
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        MMPrintString("\rFirmware updated\r\n");
    }
    /* savewatchdog is now a local of MMBasic_RunPromptLoop(). */
    if(noRTC){
        noRTC=0;
        Option.RTC=0;
        SaveOptions();
        MMPrintString("RTC not found, OPTION RTC AUTO disabled\r\n");
    }
    if(noI2C){
        noI2C=0;
        Option.KeyboardConfig=NO_KEYBOARD;
        SaveOptions();
        MMPrintString("I2C Keyboard not found, OPTION KEYBOARD disabled\r\n");
    }
    updatebootcount();
    *tknbuf = 0;
     ContinuePoint = nextstmt;                               // in case the user wants to use the continue command
#ifdef USBKEYBOARD
    hal_keyboard_init();
#else
    initMouse0(0);
#endif
#ifdef rp2350
    if(PSRAMsize){MMPrintString("Total of ");PInt(PSRAMsize/(1024*1024));MMPrintString(" Mbytes PSRAM available\r\n");}
    #if defined(PICOMITEVGA) && !defined(HDMI)
        start_i2s(QVGA_PIO_NUM,1);
    #else
        start_i2s(2,1);
    #endif
#else
    start_i2s(QVGA_PIO_NUM,1);
#endif

   
    extern void MMBasic_RunPromptLoop(void);
    MMBasic_RunPromptLoop();
}
void stripcomment(char *p){
    char *q=p;
    int toggle=0;
    while(*q){
        if(*q=='\'' && toggle==0){
            *q=0;
            break;
        }
        if(*q=='"')toggle^=1;
        q++;
    }
}

// takes a pointer to RAM containing a program (in clear text) and writes it to memory in tokenised format
void MIPS16 SaveProgramToFlash(unsigned char *pm, int msg) {
    unsigned char *p, fontnbr, prevchar = 0, buf[STRINGSIZE];
    unsigned short endtoken, tkn;
    int nbr, i, j, n, SaveSizeAddr;
    bool continuation=false;
    multi=false;
    uint32_t storedupdates[MAXCFUNCTION], updatecount=0, realflashsave;
    initFonts();
#ifdef rp2350
    __dsb();
#endif
#ifdef USBKEYBOARD
	clearrepeat();
#endif	
    memcpy(buf, tknbuf, STRINGSIZE);                                // save the token buffer because we are going to use it
    FlashWriteInit(PROGRAM_FLASH);
    hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
    j=MAX_PROG_SIZE/4;
    int *pp=(int *)(flash_progmemory);
        while(j--)if(*pp++ != 0xFFFFFFFF){
            enable_interrupts_pico();
            error("Flash erase problem");
        }
    nbr = 0;
    // this is used to count the number of bytes written to flash
    while(*pm) {
contloop:
        if(continuation){
            p=&inpbuf[strlen((char *)inpbuf)];
            continuation=false;
        }
        else p = inpbuf;
        while(!(*pm == 0 || *pm == '\r' || (*pm == '\n' && prevchar != '\r'))) {
            if(*pm == TAB) {
                do {*p++ = ' ';
                    if((p - inpbuf) >= MAXSTRLEN) goto exiterror;
                } while((p - inpbuf) % 2);
            } else {
                if(isprint((uint8_t)*pm)) {
                    *p++ = *pm;
                    if((p - inpbuf) >= MAXSTRLEN) goto exiterror;
                }
            }
            prevchar = *pm++;
        }
        if(*pm) prevchar = *pm++;                                   // step over the end of line char but not the terminating zero
        *p = 0;                                                     // terminate the string in inpbuf

        if(*inpbuf == 0 && (*pm == 0 || (!isprint((uint8_t)*pm) && pm[1] == 0))) break; // don't save a trailing newline
        if(inpbuf[strlen((char *)inpbuf)-1]==Option.continuation && inpbuf[strlen((char *)inpbuf)-2]==' ' && Option.continuation){
            continuation=true;
            inpbuf[strlen((char *)inpbuf)-2]=0; //strip the continuation character
            goto contloop;
        }
        tokenise(false);                                            // turn into executable code
        p = tknbuf;
        while(!(p[0] == 0 && p[1] == 0)) {
            FlashWriteByte(*p++); nbr++;

            if((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5)  goto exiterror;
        }
        FlashWriteByte(0); nbr++;                              // terminate that line in flash
    }
    FlashWriteByte(0);
    FlashWriteAlign();                                            // this will flush the buffer and step the flash write pointer to the next word boundary
    // now we must scan the program looking for CFUNCTION/CSUB/DEFINEFONT statements, extract their data and program it into the flash used by  CFUNCTIONs
     // programs are terminated with two zero bytes and one or more bytes of 0xff.  The CFunction area starts immediately after that.
     // the format of a CFunction/CSub/Font in flash is:
     //   Unsigned Int - Address of the CFunction/CSub in program memory (points to the token representing the "CFunction" keyword) or NULL if it is a font
     //   Unsigned Int - The length of the CFunction/CSub/Font in bytes including the Offset (see below)
     //   Unsigned Int - The Offset (in words) to the main() function (ie, the entry point to the CFunction/CSub).  Omitted in a font.
     //   word1..wordN - The CFunction/CSub/Font code
     // The next CFunction/CSub/Font starts immediately following the last word of the previous CFunction/CSub/Font
    int firsthex=1;
    realflashsave= realflashpointer;
    p = (unsigned char *)flash_progmemory;                                              // start scanning program memory
    while(*p != 0xff) {
    	nbr++;
        if(*p == 0) p++;                                            // if it is at the end of an element skip the zero marker
        if(*p == 0) break;                                          // end of the program
        if(*p == T_NEWLINE) {
            CurrentLinePtr = p;
            p++;                                                    // skip the newline token
        }
        if(*p == T_LINENBR) p += 3;                                 // step over the line number

        skipspace(p);
        if(*p == T_LABEL) {
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }
        tkn=p[0] & 0x7f;
        tkn |= ((unsigned short)(p[1] & 0x7f)<<7);
        if(tkn == cmdCSUB || tkn == GetCommandValue((unsigned char *)"DefineFont")) {      // found a CFUNCTION, CSUB or DEFINEFONT token
            if(tkn == GetCommandValue((unsigned char *)"DefineFont")) {
             endtoken = GetCommandValue((unsigned char *)"End DefineFont");
             p+=2;                                                // step over the token
             skipspace(p);
             if(*p == '#') p++;
             fontnbr = getint(p, 1, FONT_TABLE_SIZE);
                                                 // font 6 has some special characters, some of which depend on font 1
             if(fontnbr == 1 || fontnbr == 6 || fontnbr == 7) {
                enable_interrupts_pico();
                error("Cannot redefine fonts 1, 6 or 7");
             }
             realflashpointer+=4;
             skipelement(p);                                     // go to the end of the command
             p--;
            } else {
                endtoken = GetCommandValue((unsigned char *)"End CSub");
                realflashpointer+=4;
                fontnbr = 0;
                firsthex=0;
                p++;
            }
             SaveSizeAddr = realflashpointer;                                // save where we are so that we can write the CFun size in here
             realflashpointer+=4;
             p++;
             skipspace(p);
             if(!fontnbr) { //process CSub 
                 if(!isnamestart((uint8_t)*p)){
                    enable_interrupts_pico();
                    error("Function name");
                 }  
                 do { p++; } while(isnamechar((uint8_t)*p));
                 skipspace(p);
                 if(!(isxdigit((uint8_t)p[0]) && isxdigit((uint8_t)p[1]) && isxdigit((uint8_t)p[2]))) {
                     skipelement(p);
                     p++;
                    if(*p == T_NEWLINE) {
                        CurrentLinePtr = p;
                        p++;                                        // skip the newline token
                    }
                    if(*p == T_LINENBR) p += 3;                     // skip over a line number
                 }
             }
             do {
                 while(*p && *p != '\'') {
                     skipspace(p);
                     n = 0;
                     for(i = 0; i < 8; i++) {
                         if(!isxdigit((uint8_t)*p)) {
                            enable_interrupts_pico();
                            error("Invalid hex word");
                         }
                         if((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5) goto exiterror;
                         n = n << 4;
                         if(*p <= '9')
                             n |= (*p - '0');
                         else
                             n |= (toupper(*p) - 'A' + 10);
                         p++;
                     }
                     realflashpointer+=4;
                     skipspace(p);
                     if(firsthex){
                    	 firsthex=0;
                    	 if(((n>>16) & 0xff) < 0x20){
                            enable_interrupts_pico();
                            error("Can't define non-printing characters");
                         }
                     }
                 }
                 // we are at the end of a embedded code line
                 while(*p) p++;                                      // make sure that we move to the end of the line
                 p++;                                                // step to the start of the next line
                 if(*p == 0) {
                     enable_interrupts_pico();
                     error("Missing END declaration");
                 }
                 if(*p == T_NEWLINE) {
                     CurrentLinePtr = p;
                     p++;                                            // skip the newline token
                 }
                 if(*p == T_LINENBR) p += 3;                         // skip over the line number
                 skipspace(p);
                tkn=p[0] & 0x7f;
                tkn |= ((unsigned short)(p[1] & 0x7f)<<7);
             } while(tkn != endtoken);
             storedupdates[updatecount++]=realflashpointer - SaveSizeAddr - 4;
         }
         while(*p) p++;                                              // look for the zero marking the start of the next element
     }
    realflashpointer = realflashsave ;
    updatecount=0;
    p = (unsigned char *)flash_progmemory;                                              // start scanning program memory
     while(*p != 0xff) {
     	nbr++;
         if(*p == 0) p++;                                            // if it is at the end of an element skip the zero marker
         if(*p == 0) break;                                          // end of the program
         if(*p == T_NEWLINE) {
             CurrentLinePtr = p;
             p++;                                                    // skip the newline token
         }
         if(*p == T_LINENBR) p += 3;                                 // step over the line number

         skipspace(p);
         if(*p == T_LABEL) {
             p += p[1] + 2;                                          // skip over the label
             skipspace(p);                                           // and any following spaces
         }
         tkn=p[0] & 0x7f;
         tkn |= ((unsigned short)(p[1] & 0x7f)<<7);
         if(tkn == cmdCSUB || tkn == GetCommandValue((unsigned char *)"DefineFont")) {      // found a CFUNCTION, CSUB or DEFINEFONT token
         if(tkn == GetCommandValue((unsigned char *)"DefineFont")) {      // found a CFUNCTION, CSUB or DEFINEFONT token
             endtoken = GetCommandValue((unsigned char *)"End DefineFont");
             p+=2;                                                // step over the token
             skipspace(p);
             if(*p == '#') p++;
             fontnbr = getint(p, 1, FONT_TABLE_SIZE);
                                                 // font 6 has some special characters, some of which depend on font 1
             if(fontnbr == 1 || fontnbr == 6 || fontnbr == 7) {
                 enable_interrupts_pico();
                 error("Cannot redefine fonts 1, 6, or 7");
             }

             //FlashWriteWord(fontnbr - 1);                        // a low number (< FONT_TABLE_SIZE) marks the entry as a font
             // B31 = 1 now marks entry as font.
             FlashWriteByte(fontnbr - 1);
             FlashWriteByte(0x00);  
             FlashWriteByte(0x00);
             FlashWriteByte(0x80);    
           

             skipelement(p);                                     // go to the end of the command
             p--;
         } else {
             endtoken = GetCommandValue((unsigned char *)"End CSub");
             FlashWriteWord((unsigned int)(p-flash_progmemory));               // if a CFunction/CSub save a relative pointer to the declaration
             fontnbr = 0;
             p++;
         }
            SaveSizeAddr = realflashpointer;                                // save where we are so that we can write the CFun size in here
             FlashWriteWord(storedupdates[updatecount++]);                        // leave this blank so that we can later do the write
             p++;
             skipspace(p);
             if(!fontnbr) {
                 if(!isnamestart((uint8_t)*p))  {
                     enable_interrupts_pico();
                     error("Function name");
                 }
                 do { p++; } while(isnamechar(*p));
                 skipspace(p);
                 if(!(isxdigit(p[0]) && isxdigit(p[1]) && isxdigit(p[2]))) {
                     skipelement(p);
                     p++;
                    if(*p == T_NEWLINE) {
                        CurrentLinePtr = p;
                        p++;                                        // skip the newline token
                    }
                    if(*p == T_LINENBR) p += 3;                     // skip over a line number
                 }
             }
             do {
                 while(*p && *p != '\'') {
                     skipspace(p);
                     n = 0;
                     for(i = 0; i < 8; i++) {
                         if(!isxdigit(*p)) {
                            enable_interrupts_pico();
                            error("Invalid hex word");
                         }
                         if((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5) goto exiterror;
                         n = n << 4;
                         if(*p <= '9')
                             n |= (*p - '0');
                         else
                             n |= (toupper(*p) - 'A' + 10);
                         p++;
                     }

                     FlashWriteWord(n);
                     skipspace(p);
                 }
                 // we are at the end of a embedded code line
                 while(*p) p++;                                      // make sure that we move to the end of the line
                 p++;                                                // step to the start of the next line
                 if(*p == 0) {
                    enable_interrupts_pico();
                    error("Missing END declaration");
                 }
                 if(*p == T_NEWLINE) {
                    CurrentLinePtr = p;
                    p++;                                        // skip the newline token
                 }
                 if(*p == T_LINENBR) p += 3;                     // skip over a line number
                 skipspace(p);
                tkn=p[0] & 0x7f;
                tkn |= ((unsigned short)(p[1] & 0x7f)<<7);
              } while(tkn != endtoken);
         }
         while(*p) p++;                                              // look for the zero marking the start of the next element
     }
     FlashWriteWord(0xffffffff);                                // make sure that the end of the CFunctions is terminated with an erased word
     FlashWriteClose();                                              // this will flush the buffer and step the flash write pointer to the next word boundary
    if(msg) {                                                       // if requested by the caller, print an informative message
        if(MMCharPos > 1) MMPrintString("\r\n");                    // message should be on a new line
        MMPrintString("Saved ");
        IntToStr((char *)tknbuf, nbr + 3, 10);
        MMPrintString((char *)tknbuf);
        MMPrintString(" bytes\r\n");
    }
    memcpy(tknbuf, buf, STRINGSIZE);                                // restore the token buffer in case there are other commands in it
//    initConsole();
#ifdef USBKEYBOARD
	clearrepeat();
#endif
    enable_interrupts_pico();
    return;

    // we only get here in an error situation while writing the program to flash
    exiterror:
        FlashWriteByte(0); FlashWriteByte(0); FlashWriteByte(0);    // terminate the program in flash
        FlashWriteClose();
        error("Not enough memory");
}
 

#ifdef __cplusplus
}
#endif

/// \end:uart_advanced[]
