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
#include "PicoMite.h"
#include "MMBasic.h"
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
#include "hardware/flash.h"
#include "hardware/adc.h"
#include "hardware/exception.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "hardware/vreg.h"
#include "hardware/structs/pads_qspi.h"
#include "pico/unique_id.h"
#include "hardware/pwm.h"
extern void start_i2s(int pio, int sm);
#ifdef rp2350
#include "hardware/structs/qmi.h"
#endif
#ifdef PICOMITEVGA
extern void start_vga_i2s(void);
#ifndef HDMI
#endif
#endif
#define COPYRIGHT   "Copyright " YEAR " Geoff Graham\r\n"\
                    "Copyright " YEAR2 " Peter Mather\r\n\r\n"

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
    bool rp2350a=true;
    uint32_t PSRAMsize=0;
#endif
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
    uint16_t map16[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint32_t map16pairs[16];
    volatile uint8_t transparent=0;
    volatile uint8_t transparents=0;
    volatile int RGBtransparent=0;
    int MODE1SIZE, MODE2SIZE, MODE3SIZE, MODE4SIZE, MODE5SIZE;
#ifdef HDMI
    uint16_t map16d[16];
    uint32_t map16q[16];
    uint8_t  map16s[16];
#endif
    // 126 MHz timings
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
            #define MES_SIGNON  "\rPicoMiteHDMI MMBasic " CHIP " M2 Edition V" VERSION "\r\n"
        #else
            #define MES_SIGNON  "\rPicoMiteVGA MMBasic " CHIP " m1p2 Edition V" VERSION "\r\n"
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
        #define MES_SIGNON  "\rPicoMite MMBasic " CHIP " Edition V"VERSION "\r\n"
        #include "pico/multicore.h"
        mutex_t	frameBufferMutex;					// mutex to lock frame buffer
        #endif
        char LCDAttrib=0;
#endif
#define KEYCHECKTIME 16
int ListCnt;
int MMCharPos;
int MMPromptPos;
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

#ifndef USBKEYBOARD
extern void initMouse0(int sensitivity);
volatile unsigned int MouseTimer = 0;
#endif
volatile unsigned int AHRSTimer = 0;
volatile unsigned int InkeyTimer = 0;
volatile uint64_t mSecTimer = 0;                               // this is used to count mSec
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
const FSIZE_t sd_target_contents = 0;
const FSIZE_t sd_progmemory = PROGSTART;
const FSIZE_t sd_libmemory = PROGSTART - MAX_PROG_SIZE;
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
unsigned char lastcmd[STRINGSIZE*2];                                           // used to store the last command in case it is needed by the EDIT command
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
extern "C"
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
																		(void *)(double (*)(double))sin,	//0x90
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

static inline CommandToken commandtbl_decode(const unsigned char *p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}
char banner[64];
void __not_in_flash_func(routinechecks)(void){
    static int when=0;
    if((time_us_64() - mSecTimer * 1000) > 5000){
        cancel_repeating_timer(&timer);
        add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
        mSecTimer = time_us_64() / 1000;
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
    if(CurrentlyPlaying == P_MOD) checkWAVinput();
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
     if(tud_cdc_connected() && (Option.SerialConsole==0 || Option.SerialConsole>4) && Option.Telnet != (unsigned int)-1){
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

    if(c1 != (unsigned int)-1) {                                                  // check if there are discarded chars from a previous sequence
        c = c1; c1 = c2; c2 = c3; c3 = c4; c4 = -1;                 // shuffle the queue down
        return c;                                                   // and return the head of the queue
    }

    c = getConsole();                                               // do discarded chars so get the char
#ifndef USBKEYBOARD
    if(c == (unsigned int)-1)CheckKeyboard();
#endif
    if(!(c==0x1b))return c;
    InkeyTimer = 0;                                             // start the timer
    while((c = getConsole()) == (unsigned int)-1 && InkeyTimer < 30);         // get the second char with a delay of 30mS to allow the next char to arrive
    if(c == 'O'){   //support for many linux terminal emulators
        while((c = getConsole()) == (unsigned int)-1 && InkeyTimer < 50);        // delay some more to allow the final chars to arrive, even at 1200 baud
        if(c == 'P') return F1;
        if(c == 'Q') return F2;
        if(c == 'R') return F3;
        if(c == 'S') return F4;
        if(c == 'T') return F5;
        if(c == '2'){
            while((tc = getConsole()) == (unsigned int)-1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
            if(tc == 'R') return F3 + 0x20;
            c1 = 'O'; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
        }
        c1 = 'O'; c2 = c; return 0x1b;                 // not a valid 4 char code
    }
    if(c != '[') { c1 = c; return 0x1b; }                       // must be a square bracket
    while((c = getConsole()) == (unsigned int)-1 && InkeyTimer < 50);         // get the third char with delay
    if(c == 'A') return UP;                                     // the arrow keys are three chars
    if(c == 'B') return DOWN;
    if(c == 'C') return RIGHT;
    if(c == 'D') return LEFT;
    if(c < '1' && c > '6') { c1 = '['; c2 = c; return 0x1b; }   // the 3rd char must be in this range
    while((tc = getConsole()) == (unsigned int)-1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
    if(tc == '~') {                                             // all 4 char codes must be terminated with ~
        if(c == '1') return HOME;
        if(c == '2') return INSERT;
        if(c == '3') return DEL;
        if(c == '4') return END;
        if(c == '5') return PUP;
        if(c == '6') return PDOWN;
        c1 = '['; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
    }
    while((ttc = getConsole()) == (unsigned int)-1 && InkeyTimer < 90);       // get the 5th char with delay
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
            if(c == F2)  tp = (char *)"RUN";
            if(c == F3)  tp = (char *)"LIST";
            if(c == F4)  tp = (char *)"EDIT";
            if(c == F10) tp = (char *)"AUTOSAVE";
            if(c == F11) tp = (char *)"XMODEM RECEIVE";
            if(c == F12) tp = (char *)"XMODEM SEND";
            if(c == F1) tp = (char *)Option.F1key;
            if(c == F5) tp = (char *)Option.F5key;
            if(c == F6) tp = (char *)Option.F6key;
            if(c == F7) tp = (char *)Option.F7key;
            if(c == F8) tp = (char *)Option.F8key;
            if(c == F9) tp = (char *)Option.F9key;
            if(tp) {
                strcpy(p, tp);
                if(EchoOption) { MMPrintString(tp); MMPrintString((char *)"\r\n"); }
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
// insert a string into the start of the lastcmd buffer.
// the buffer is a sequence of strings separated by a zero byte.
// using the up arrow usere can call up the last few commands executed.
void MIPS16 InsertLastcmd(unsigned char *s) {
    int i;
    if(strcmp((const char *)lastcmd, (const char *)s) == 0) return;                             // don't duplicate
    size_t slen = strlen((const char *)s);
    if(slen < 1 || slen > sizeof(lastcmd) - 1) return;
    slen++;
    for(i = sizeof(lastcmd) - 1; i >= (int)slen ; i--)
        lastcmd[i] = lastcmd[i - slen];                             // shift the contents of the buffer up
    strcpy((char *)lastcmd, (char *)s);                                             // and insert the new string in the beginning
    for(i = sizeof(lastcmd) - 1; lastcmd[i]; i--) lastcmd[i] = 0;             // zero the end of the buffer
}

void MIPS16 EditInputLine(void) {
    char *p = NULL;
    char buf[MAXKEYLEN + 3];
    char goend[10];
    int lastcmd_idx, lastcmd_edit;
    int insert, /*startline,*/ maxchars;
    int CharIndex, BufEdited;
    int c, i, j;
    int l4,l3,l2;
    maxchars=255; 
    if(Option.DISPLAY_CONSOLE && Option.Width<=SCREENWIDTH){     //We will always assume the Vt100 is 80 colums if LCD is the console <=80.
      l2=SCREENWIDTH+1-MMPromptPos;
      l3=2*SCREENWIDTH+2-MMPromptPos;
      l4=3*SCREENWIDTH+3-MMPromptPos;
    }else{                         // otherwise assume the VT100 matches Option.Width
      l2=Option.Width +1-MMPromptPos;
      l3=2*Option.Width+2-MMPromptPos;
      l4=3*Option.Width+3-MMPromptPos;
    }
    // Build "\e[80C" equivalent string for the line length
    //strcpy(goend,"\e[");IntToStr(linelen,l2+MMPromptPos, 10);strcat(goend,linelen); strcat(goend, "C");
     strcpy(goend,"\e[");IntToStr(&goend[strlen(goend)],l2+MMPromptPos, 10);strcat(goend, "C");
    
    MMPrintString((char *)inpbuf);                                                     // display the contents of the input buffer (if any)
    CharIndex = strlen((const char *)inpbuf);                                          // get the current cursor position in the line
    insert = false;
//    Cursor = C_STANDARD;
    lastcmd_edit = lastcmd_idx = 0;
    BufEdited = false; //(CharIndex != 0);
    while(1) {
        c = MMgetchar();
        if(c == TAB) {
            strcpy(buf, "        ");
            switch (Option.Tab) {
              case 2:
                buf[2 - (CharIndex % 2)] = 0; break;
              case 3:
                buf[3 - (CharIndex % 3)] = 0; break;
              case 4:
                buf[4 - (CharIndex % 4)] = 0; break;
              case 8:
                buf[8 - (CharIndex % 8)] = 0; break;
            }
        } else {
            buf[0] = c;
            buf[1] = 0;
        }
        do {
            switch(buf[0]) {
                case '\r':
                case '\n':  //if(autoOn && atoi(inpbuf) > 0) autoNext = atoi(inpbuf) + autoIncr;
                            //if(autoOn && !BufEdited) *inpbuf = 0;
                            goto saveline;
                            break;

                case '\b':
                            if(CharIndex > 0) {
                                BufEdited = true;
                                i = CharIndex - 1;
                                j= CharIndex;
                                for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
 
                                // Lets put the cursor at the beginning of where the command is displayed.
                                // backspace to the beginning of line
#define USEBACKSPACE
#ifdef USEBACKSPACE                                
                                while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                                }
                                fflush(stdout);                                
                                 MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
#else
                                 CurrentX=0;CurrentY=CurrentY-((CharIndex+1)/Option.Width * gui_font_height);
                                 if (CharIndex>l4-1)SSPrintString("\e[3A");
                                 else if (CharIndex>l3-1)SSPrintString("\e[2A");
                                 else if(CharIndex>l2-1)SSPrintString("\e[1A");
                                 SSPrintString("\r");
                                 //CurrentX=0;SerUSBPutS("\r");
                                 MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");
        				         MMPrintString("> ");
        				         fflush(stdout);


#endif

                                 j=0;
                                 while(j < strlen((const char *)inpbuf)) {
                                      MMputchar(inpbuf[j],0);
                                      if((j==l4-1 || j==l3-1 || j==l2-1 ) && j == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                                      if((j==l4-1 || j==l3-1 || j==l2-1 ) && j < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[j+1],0);SSPrintString("\b");}
                                      j++;
                                 }
                                 fflush(stdout);

                                 // return the cursor to the right position
                                 for(j = strlen((const char *)inpbuf); j > i; j--){
                                   if (j==l4 || j==l3 || j==l2) {DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',0);}
                                 }
                                 CharIndex--;
                                 fflush(stdout);
                                 if(strlen((const char *)inpbuf)==0)BufEdited = false;
                            }
                            break;

                case CTRLKEY('S'):
                case LEFT:

                	    BufEdited = true;
                	    insert=false; //left at first char will turn OVR on
                	    if(CharIndex > 0) {
                               // if(CharIndex == strlen((const char *)inpbuf)) {
                                    //insert = true;
                               // }
                                if (CharIndex==l4 || CharIndex==l3 || CharIndex==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',1);}
                                insert=true; //Any left turns on INS
                                CharIndex--;
                         }
                     break;

                case CTRLKEY('D'):
                case RIGHT:

                	  if(CharIndex < strlen((const char *)inpbuf)) {
                	   	BufEdited = true;
                	    MMputchar(inpbuf[CharIndex],1);
                	    if((CharIndex==l4-1 || CharIndex==l3-1|| CharIndex==l2-1 ) && CharIndex == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                	    if((CharIndex==l4-1 || CharIndex==l3-1|| CharIndex==l2-1 ) && CharIndex < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[CharIndex+1],0);SSPrintString("\b");}
                        CharIndex++;
                      }
//                      insert=false; //right always switches to OVER
                     break;
                case CTRLKEY(']'):
                case DEL:

                	      if(CharIndex < strlen((const char *)inpbuf)) {
                	           BufEdited = true;
                	           i = CharIndex;

                	           for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
                	           j = strlen((const char *)inpbuf);
                	           // Lets put the cursor at the beginning of where the command is displayed.
                               // backspace to the beginning of line
                	           j=CharIndex;
                               while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                               }
                               fflush(stdout);
                               MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
                               j=0;
                               while(j < strlen((const char *)inpbuf)) {
                                    MMputchar(inpbuf[j],0);
                                    if((j==l4-1 || j==l3-1 || j==l2-1 ) && j == strlen((const char *)inpbuf)-1 ){SSPrintString(" ");SSPrintString("\b");}
                                    if((j==l4-1 || j==l3-1 || j==l2-1 ) && j < strlen((const char *)inpbuf)-1 ){SerialConsolePutC(inpbuf[j+1],0);SSPrintString("\b");}
                                    j++;
                               }
                               fflush(stdout);
                               // return the cursor to the right position
                               for(j = strlen((const char *)inpbuf); j > i; j--){
                                 if (j==l4 || j==l3 || j==l2) {DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                               }
                               fflush(stdout);
                           }
                	       break;


                case CTRLKEY('N'):
                case INSERT:insert = !insert;
//                            Cursor = C_STANDARD + insert;
                            break;

                case CTRLKEY('U'):
                case HOME:  
                           BufEdited = true;
                           if(CharIndex > 0) {
                                if(CharIndex == strlen((const char *)inpbuf)) {
                                    insert = true;
//                                    Cursor = C_INSERT;
                                }
                                // backspace to the beginning of line
                                while(CharIndex)  {
                                 	 if (CharIndex==l4 || CharIndex==l3 || CharIndex==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{MMputchar('\b',0);}
                                   	 CharIndex--;
                                }
                                fflush(stdout);
                            }else{ //HOME @ home turns off edit mode
                            	BufEdited = false;
                                insert=false; //home at first char will turn OVR on
                            }
                            break;

                case CTRLKEY('K'):
                case END:   
                            BufEdited = true;
                            while(CharIndex < strlen((const char *)inpbuf)){
                                MMputchar(inpbuf[CharIndex++],0);
                            }   
                            fflush(stdout);
                            break;

/*            if(c == F2)  tp = "RUN";
            if(c == F3)  tp = "LIST";
            if(c == F4)  tp = "EDIT";
            if(c == F10) tp = "AUTOSAVE";
            if(c == F11) tp = "XMODEM RECEIVE";
            if(c == F12) tp = "XMODEM SEND";
            if(c == F5) tp = Option.F5key;
            if(c == F6) tp = Option.F6key;
            if(c == F7) tp = Option.F7key;
            if(c == F8) tp = Option.F8key;
            if(c == F9) tp = Option.F9key;
*/
                case 0x91:
                    if(*Option.F1key)strcpy(&buf[1],(char *)Option.F1key);
                    break;
                case 0x92:
                    strcpy(&buf[1],"RUN\r\n");
                    break;
                case 0x93:
                    strcpy(&buf[1],"LIST\r\n");
                    break;
                case 0x94:
                    strcpy(&buf[1],"EDIT\r\n");
                    break;
                case 0x95:
                    if(*Option.F5key){
                        strcpy(&buf[1],(char *)Option.F5key);
                    }else{
                         /*** F5 will clear the VT100  ***/
            	         SSPrintString("\e[2J\e[H");
            	         fflush(stdout);
                         if(Option.DISPLAY_CONSOLE){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
                         if(FindSubFun(CombinedPtr((uint8_t*)"MM.PROMPT"), 0) >= 0) {
                            ExecuteProgram(CombinedPtr((uint8_t*)"MM.PROMPT\0"));
                         } else{
                             MMPrintString("> ");                                    // print the prompt
                         }                           
            	         fflush(stdout);
                    }    
                    break;
                case 0x96:
                    if(*Option.F6key)strcpy(&buf[1],(char *)Option.F6key);
                    break;
                case 0x97:
                    if(*Option.F7key)strcpy(&buf[1],(char *)Option.F7key);
                    break;
                case 0x98:
                    if(*Option.F8key)strcpy(&buf[1],(char *)Option.F8key);
                    break;
                case 0x99:
                    if(*Option.F9key)strcpy(&buf[1],(char *)Option.F9key);
                    break;
                case 0x9a:
                    strcpy(&buf[1],"AUTOSAVE\r\n");
                    break;
                case 0x9b:
                    strcpy(&buf[1],"XMODEM RECEIVE\r\n");
                    break;
                 case 0x9c:
                    strcpy(&buf[1],"XMODEM SEND\r\n");
                    break;
                case CTRLKEY('E'):
                case UP:    if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                              
                                if(lastcmd_edit) {
                                    i = lastcmd_idx + strlen((const char *)&lastcmd[lastcmd_idx]) + 1;    // find the next command
                                    if(lastcmd[i] != 0 && i < sizeof(lastcmd) - 1) lastcmd_idx = i;  // and point to it for the next time around
                                } else
                                    lastcmd_edit = true;
                                strcpy((char *)inpbuf, (const char *)&lastcmd[lastcmd_idx]);                      // get the command into the buffer for editing
                                goto insert_lastcmd;
                            }
                            break;

                case CTRLKEY('X'):
                case DOWN:  
                           if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                               if(lastcmd_idx == 0)
                                    *inpbuf = lastcmd_edit = 0;
                                else {
                                    for(i = lastcmd_idx - 2; i > 0 && lastcmd[i - 1] != 0; i--);// find the start of the previous command
                                    lastcmd_idx = i;                                        // and point to it for the next time around
                                    strcpy((char *)inpbuf, (const char *)&lastcmd[i]);                            // get the command into the buffer for editing
                                }
                                goto insert_lastcmd;                                        // gotos are bad, I know, I know
                            }
                            break;

                insert_lastcmd: 

                            // If NoScroll and its near the bottom then clear screen and write command at top
                            //if(Option.NoScroll && Option.DISPLAY_CONSOLE && (CurrentY + 2*gui_font_height >= VRes)){
                            if(Option.NoScroll && Option.DISPLAY_CONSOLE && (CurrentY + (2 + strlen((const char *)inpbuf)/Option.Width)*gui_font_height >= VRes)){    
                                      ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;
                                      if(FindSubFun(CombinedPtr((unsigned char *)"MM.PROMPT"), 0) >= 0) {
                                         SSPrintString("\r");
                                         ExecuteProgram(CombinedPtr((unsigned char *)"MM.PROMPT\0"));
                                      } else{
                                         SSPrintString("\r");
                                         MMPrintString("> ");                           // print the prompt
                                      }    
                          
                            }else{
			                   // Lets put the cursor at the beginning of where the command is displayed.
                               // backspace to the beginning of line
                                j=CharIndex;  //????????????????????????????????
                                while(j)  {
                                  if (j==l4 || j==l3 ||j==l2 ){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                  j--;
                                }
                                fflush(stdout);
                                MX470Display(CLEAR_TO_EOS);SSPrintString("\033[0J");        //Clear to End Of Screen
                            }

				            CharIndex = strlen((const char *)inpbuf);
                            MMPrintString((char *)inpbuf);                                          // display the line
                            if(CharIndex==l4 || CharIndex==l3 || CharIndex==l2){SSPrintString(" ");SSPrintString("\b");}
                            fflush(stdout);
                            CharIndex = strlen((const char *)inpbuf);                                     // get the current cursor position in the line
                            break;
                            

 
 
                default:    if(buf[0] >= ' ' && buf[0] < 0x7f) {
                               // BufEdited = true;  
                               
                                i = CharIndex;
                                j = strlen((const char *)inpbuf);
                                if(insert) {
                                    if(strlen((const char *)inpbuf) >= maxchars - 1) break;               // sorry, line full
                                    for(p = (char *)inpbuf + strlen((const char *)inpbuf); j >= CharIndex; p--, j--) *(p + 1) = *p;
                                    inpbuf[CharIndex] = buf[0];                             // insert the char
                                    MMPrintString((char *)&inpbuf[CharIndex]);                      // display new part of the line
                                    CharIndex++;
                                   // return the cursor to the right position
                                    for(j = strlen((const char *)inpbuf); j > CharIndex; j--){
                                      if (j==l4 || j==l3 || j==l2){DisplayPutC('\b');SSPrintString("\e[1A");SSPrintString(goend);}else{ MMputchar('\b',0);}
                                    }
                                    fflush(stdout);  
                                } else {
                                    if(strlen((const char *)inpbuf) >= maxchars-1 ) break;               // sorry, line full  just ignore
                                    inpbuf[strlen((const char *)inpbuf) + 1] = 0;                         // incase we are adding to the end of the string
                                    inpbuf[CharIndex++] = buf[0];                           // overwrite the char
                                    MMputchar(buf[0],0);  
                                    if(j==l4-1 || j==l3-1 || j==l2-1){SSPrintString(" ");SSPrintString("\b");}
                                    fflush(stdout);
                                  
                                }
#ifndef PICOMITEVGA                                                                     
                                i = CharIndex;
                                j = strlen((const char *)inpbuf);
                                // If its going to scroll then clear screen
                                if(Option.NoScroll && Option.DISPLAY_CONSOLE){
                                   if(CurrentY + 2*gui_font_height >= VRes) {
                                      ClearScreen(gui_bcolour);/*CurrentX=0*/;CurrentY=0;
                                      CurrentX = (MMPromptPos-2)*gui_font_width  ;          
                                      //if(FindSubFun((unsigned char *)"MM.PROMPT", 0) >= 0) {
                                      //   ExecuteProgram((unsigned char *)"MM.PROMPT\0");
                                      //} else{
                                         //SSPrintString("\r");
                                         //MMPrintString("> ");                           // print the prompt
                                         DisplayPutC('>');
                                         DisplayPutC(' ');
                                      //}    
                                      DisplayPutS((char *)inpbuf);                      // display the line
                                      
                                    }
                                }
#endif                                
 
                            }
                            break;
            }
            for(i = 0; i < MAXKEYLEN + 1; i++) buf[i] = buf[i + 1];                             // shuffle down the buffer to get the next char
        } while(*buf);
        if(CharIndex == strlen((const char *)inpbuf)) {
          insert = false;
//        Cursor = C_STANDARD;
        }
    }
    
    saveline:
//    Cursor = C_STANDARD;
   
   if(strlen((const char *)inpbuf) < maxchars)InsertLastcmd(inpbuf);
   MMPrintString("\r\n");
}

#ifdef OLDSTUFF
void MIPS16 EditInputLine(void) {
    char *p = NULL;
    char buf[MAXKEYLEN + 3];
    int lastcmd_idx, lastcmd_edit;
    int insert, startline, maxchars;
    int CharIndex, BufEdited;
    int c, i, j;
    maxchars = Option.Width;
    if(strlen((const char *)inpbuf) >= maxchars) {
        MMPrintString((char *)inpbuf);
        error("Line is too long to edit");
    }
    startline = MMCharPos - 1;                                                          // save the current cursor position
    MMPrintString((char *)inpbuf);                                                              // display the contents of the input buffer (if any)
    CharIndex = strlen((const char *)inpbuf);                                                         // get the current cursor position in the line
    insert = false;
//    Cursor = C_STANDARD;
    lastcmd_edit = lastcmd_idx = 0;
    BufEdited = false; //(CharIndex != 0);
    while(1) {
        c = MMgetchar();
        if(c == TAB) {
            strcpy(buf, "        ");
            switch (Option.Tab) {
              case 2:
                buf[2 - (CharIndex % 2)] = 0; break;
              case 3:
                buf[3 - (CharIndex % 3)] = 0; break;
              case 4:
                buf[4 - (CharIndex % 4)] = 0; break;
              case 8:
                buf[8 - (CharIndex % 8)] = 0; break;
            }
        } else {
            buf[0] = c;
            buf[1] = 0;
        }
        do {
            switch(buf[0]) {
                case '\r':
                case '\n':  //if(autoOn && atoi(inpbuf) > 0) autoNext = atoi(inpbuf) + autoIncr;
                            //if(autoOn && !BufEdited) *inpbuf = 0;
                            goto saveline;
                            break;

                case '\b':  if(CharIndex > 0) {
                                BufEdited = true;
                                i = CharIndex - 1;
                                for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; }         // go to the beginning of the line
                                MMPrintString((char *)inpbuf); MMputchar(' ',0); MMputchar('\b',0);     // display the line and erase the last char
                                for(CharIndex = strlen((const char *)inpbuf); CharIndex > i; CharIndex--)
                                    MMputchar('\b',0);  
                                fflush(stdout);                                      // return the cursor to the righ position
                            }
                            break;

                case CTRLKEY('S'):
                case LEFT:  if(CharIndex > 0) {
                                if(CharIndex == strlen((const char *)inpbuf)) {
                                    insert = true;
      //                              Cursor = C_INSERT;
                                }
                                MMputchar('\b',1);
                                CharIndex--;
                            }
                            break; 

                case CTRLKEY('D'):
                case RIGHT: if(CharIndex < strlen((const char *)inpbuf)) {
                                MMputchar(inpbuf[CharIndex],1);
                                CharIndex++;
                            }
                            break;

                case CTRLKEY(']'):
                case DEL:   if(CharIndex < strlen((const char *)inpbuf)) {
                                BufEdited = true;
                                i = CharIndex;
                                for(p = (char *)inpbuf + i; *p; p++) *p = *(p + 1);                 // remove the char from inpbuf
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; }         // go to the beginning of the line
                                MMPrintString((char *)inpbuf); MMputchar(' ',0); MMputchar('\b',0);     // display the line and erase the last char
                                for(CharIndex = strlen((const char *)inpbuf); CharIndex > i; CharIndex--)
                                    MMputchar('\b',0);   
                                fflush(stdout);                                     // return the cursor to the right position
                            }
                            break;

                case CTRLKEY('N'):
                case INSERT:insert = !insert;
//                            Cursor = C_STANDARD + insert;
                            break;

                case CTRLKEY('U'):
                case HOME:  if(CharIndex > 0) {
                                if(CharIndex == strlen((const char *)inpbuf)) {
                                    insert = true;
//                                    Cursor = C_INSERT;
                                }
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; }
                                fflush(stdout);
                            }
                            break;

                case CTRLKEY('K'):
                case END:   while(CharIndex < strlen((const char *)inpbuf)){
                                MMputchar(inpbuf[CharIndex++],0);
                            }   
                            fflush(stdout);
                            break;

/*            if(c == F2)  tp = "RUN";
            if(c == F3)  tp = "LIST";
            if(c == F4)  tp = "EDIT";
            if(c == F10) tp = "AUTOSAVE";
            if(c == F11) tp = "XMODEM RECEIVE";
            if(c == F12) tp = "XMODEM SEND";
            if(c == F5) tp = Option.F5key;
            if(c == F6) tp = Option.F6key;
            if(c == F7) tp = Option.F7key;
            if(c == F8) tp = Option.F8key;
            if(c == F9) tp = Option.F9key;*/
                case 0x91:
                    if(*Option.F1key)strcpy(&buf[1],(char *)Option.F1key);
                    break;
                case 0x92:
                    strcpy(&buf[1],"RUN\r\n");
                    break;
                case 0x93:
                    strcpy(&buf[1],"LIST\r\n");
                    break;
                case 0x94:
                    strcpy(&buf[1],"EDIT\r\n");
                    break;
                case 0x95:
                    if(*Option.F5key){
                        strcpy(&buf[1],(char *)Option.F5key);
                    }else{
                         /*** F5 will clear the VT100  ***/
            	         SSPrintString("\e[2J\e[H");
            	         fflush(stdout);
                         if(Option.DISPLAY_CONSOLE){ClearScreen(gui_bcolour);CurrentX=0;CurrentY=0;}
            	         MMPrintString("> ");
            	         fflush(stdout);
                    }    
                    break;
                case 0x96:
                    if(*Option.F6key)strcpy(&buf[1],(char *)Option.F6key);
                    break;
                case 0x97:
                    if(*Option.F7key)strcpy(&buf[1],(char *)Option.F7key);
                    break;
                case 0x98:
                    if(*Option.F8key)strcpy(&buf[1],(char *)Option.F8key);
                    break;
                case 0x99:
                    if(*Option.F9key)strcpy(&buf[1],(char *)Option.F9key);
                    break;
                case 0x9a:
                    strcpy(&buf[1],"AUTOSAVE\r\n");
                    break;
                case 0x9b:
                    strcpy(&buf[1],"XMODEM RECEIVE\r\n");
                    break;
                 case 0x9c:
                    strcpy(&buf[1],"XMODEM SEND\r\n");
                    break;
                case CTRLKEY('E'):
                case UP:    if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; } 
                                fflush(stdout);       // go to the beginning of line
                                if(lastcmd_edit) {
                                    i = lastcmd_idx + strlen((const char *)&lastcmd[lastcmd_idx]) + 1;    // find the next command
                                    if(lastcmd[i] != 0 && i < sizeof(lastcmd) - 1) lastcmd_idx = i;  // and point to it for the next time around
                                } else
                                    lastcmd_edit = true;
                                strcpy((char *)inpbuf, (const char *)&lastcmd[lastcmd_idx]);                      // get the command into the buffer for editing
                                goto insert_lastcmd;
                            }
                            break;


                case CTRLKEY('X'):
                case DOWN:  if(!(BufEdited /*|| autoOn || CurrentLineNbr */)) {
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; }   
                                fflush(stdout);      // go to the beginning of line
                                if(lastcmd_idx == 0)
                                    *inpbuf = lastcmd_edit = 0;
                                else {
                                    for(i = lastcmd_idx - 2; i > 0 && lastcmd[i - 1] != 0; i--);// find the start of the previous command
                                    lastcmd_idx = i;                                        // and point to it for the next time around
                                    strcpy((char *)inpbuf, (const char *)&lastcmd[i]);                            // get the command into the buffer for editing
                                }
                                goto insert_lastcmd;                                        // gotos are bad, I know, I know
                            }
                            break;

                insert_lastcmd:                                                             // goto here if we are just editing a command
                            if(strlen((const char *)inpbuf) + startline >= maxchars) {                    // if the line is too long
                                while(CharIndex)  { MMputchar('\b',0); CharIndex--; }         // go to the start of the line
                                MMPrintString((char *)inpbuf);                                      // display the offending line
                                error("Line is too long to edit");
                            }
                            MMPrintString((char *)inpbuf);                                          // display the line
                            CharIndex = strlen((const char *)inpbuf);                                     // get the current cursor position in the line
                            for(i = 1; i <= maxchars - strlen((const char *)inpbuf) - startline; i++) {
                                MMputchar(' ',0);                                             // erase the rest of the line
                                CharIndex++;
                            }
                            while(CharIndex > strlen((const char *)inpbuf)) { MMputchar('\b',0); CharIndex--; } // return the cursor to the right position

                            SSPrintString("\033[0K");
                            break;

                default:    if(buf[0] >= ' ' && buf[0] < 0x7f) {
                                BufEdited = true;                                           // this means that something was typed
                                i = CharIndex;
                                j = strlen((const char *)inpbuf);
                                if(insert) {
                                    if(strlen((const char *)inpbuf) >= maxchars - 1) break;               // sorry, line full
                                    for(p = (char *)inpbuf + strlen((const char *)inpbuf); j >= CharIndex; p--, j--) *(p + 1) = *p;
                                    inpbuf[CharIndex] = buf[0];                             // insert the char
                                    MMPrintString((char *)&inpbuf[CharIndex]);                      // display new part of the line
                                    CharIndex++;
                                    for(j = strlen((const char *)inpbuf); j > CharIndex; j--) MMputchar('\b',0); 
                                        fflush(stdout);                                   // return the cursor to the right position
                                } else {
                                    inpbuf[strlen((const char *)inpbuf) + 1] = 0;                         // incase we are adding to the end of the string
                                    inpbuf[CharIndex++] = buf[0];                           // overwrite the char
                                    MMputchar(buf[0],1);                                      // display it
                                    if(CharIndex + startline >= maxchars) {                 // has the input gone beyond the end of the line?
                                        MMgetline(0, (char *)inpbuf);                               // use the old fashioned way of getting the line
                                        //if(autoOn && atoi(inpbuf) > 0) autoNext = atoi(inpbuf) + autoIncr;
                                        goto saveline;
                                    }
                                }
                            }
                            break;
            }
            for(i = 0; i < MAXKEYLEN + 1; i++) buf[i] = buf[i + 1];                             // shuffle down the buffer to get the next char
        } while(*buf);
    if(CharIndex == strlen((const char *)inpbuf)) {
        insert = false;
//        Cursor = C_STANDARD;
        }
    }

    saveline:
//    Cursor = C_STANDARD;
    MMPrintString("\r\n");
}
#endif

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
void MMPrintString(const char* s) {
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
#ifdef PICOMITE
        if(mergerunning){
            multicore_fifo_push_blocking(0xFF);
            busy_wait_ms(mergetimer+200);
            if(mergerunning){
                _excep_code = RESET_COMMAND;
                SoftReset();
            }
        }
#endif
        do_end(false);
        longjmp(mark, 1);												// jump back to the input prompt
}
}
void PRet(void){
    MMPrintString("\r\n");
}
void SRet(void){
    SSPrintString("\r\n");
}

void PInt(int64_t n) {
    char s[20];
    IntToStr(s, (int64_t)n, 10);
    MMPrintString(s);
}
void SInt(int64_t n) {
    char s[20];
    IntToStr(s, (int64_t)n, 10);
    SSPrintString(s);
}

void SIntComma(int64_t n) {
    SSPrintString(", "); SInt(n);
}

void PIntComma(int64_t n) {
    MMPrintString(", "); PInt(n);
}

void PIntH(unsigned long long int n) {
    char s[20];
    IntToStr(s, (int64_t)n, 16);
    MMPrintString(s);
}
void PIntB(unsigned long long int n) {
    char s[65];
    IntToStr(s, (int64_t)n, 2);
    MMPrintString(s);
}
void PIntHC(unsigned long long int n) {
    MMPrintString(", "); PIntH(n);
}
void PIntBC(unsigned long long int n) {
    MMPrintString(", "); PIntB(n);
}

void PFlt(MMFLOAT flt){
	   char s[20];
	   FloatToStr(s, flt, 4,4, ' ');
	    MMPrintString(s);
}
void PFltComma(MMFLOAT n) {
    MMPrintString(", "); PFlt(n);
}
void sigbus(void){
    MMPrintString("Error: Invalid address - resetting\r\n");
	uSec(250000);
	/** disable_interrupts_pico(); */
//	sd_range_erase(PROGSTART, MAX_PROG_SIZE);
    LoadOptions();
    if(Option.NoReset==0){
        Option.Autorun=0;
        SaveOptions();
    }
	/** enable_interrupts_pico(); */
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

// TODO:
void graphics_init();
void graphics_set_buffer(uint8_t* buffer, const uint16_t width, const uint16_t height);
void graphics_set_bgcolor(const uint32_t color888);
void graphics_set_flashmode(const bool flash_line, const bool flash_frame);

// initialize QVGA (can change system clock)
void QVgaInit()
{
    X_TILE=Option.X_TILE;
    Y_TILE=Option.Y_TILE;
    ytileheight=(X_TILE==80 || X_TILE==90 || X_TILE==106 || X_TILE==100)? 12 : 16;
	// initialize PIO
//	QVgaPioInit();
    I2SOff = pio_add_program(QVGA_PIO, &i2s_program);
    graphics_init();
    graphics_set_buffer(NULL, 320, 480); /// TODO:
    graphics_set_bgcolor(0x000000);
    graphics_set_flashmode(false, false);

	// initialize scanline buffers
//	QVgaBufInit();

	// initialize DMA
//	QVgaDmaInit();

	// initialize parameters
	QVgaScanLine = 0; // currently processed scanline
//	QVgaBufInx = 0; // at first, control buffer 1 will be sent out
//	QVgaFrame = 0; // current frame
//	ScanLineCBNext = &ScanLineCB[CB_MAX]; // send control buffer 2 next

	// enable DMA IRQ
	irq_set_enabled(DMA_IRQ_0, true);

	// start DMA
	dma_channel_start(QVGA_DMA_CB);

	// run state machine
	pio_sm_set_enabled(QVGA_PIO, QVGA_SM, true);
}

void (* volatile Core1Fnc)() = NULL; // core 1 remote function

// QVGA core
void __not_in_flash_func(QVgaCore)()
{
	// initialize QVGA
	QVgaInit();

	// infinite loop
	while (true)
	{
		// data memory barrier
		__dmb();
        if (multicore_fifo_rvalid()) {
            int command=multicore_fifo_pop_blocking();
            if(command==0x5555){
                irq_set_enabled(DMA_IRQ_0, false);
            };
            if(command==0xAAAA){
                irq_set_enabled(DMA_IRQ_0, true);
            }
        } 
    }
}
uint32_t core1stack[128];
#else
extern uint16_t HDMIlines[2][800];
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_S_SYNC_POLARITY 0
#define MODE_H_S_FRONT_PORCH   (Option.CPU_Speed % 126000==0 ? 16 : 16)
#define MODE_H_S_SYNC_WIDTH    (Option.CPU_Speed % 126000==0 ? 96 : 64)
#define MODE_H_S_BACK_PORCH    (Option.CPU_Speed % 126000==0 ? 48 : 120)

#define MODE_V_S_SYNC_POLARITY 0
#define MODE_V_S_FRONT_PORCH   (Option.CPU_Speed % 126000==0 ? 10 : 1)
#define MODE_V_S_SYNC_WIDTH    (Option.CPU_Speed % 126000==0 ? 2 : 3)
#define MODE_V_S_BACK_PORCH    (Option.CPU_Speed % 126000==0 ? 33 : 16)

#define MODE_H_F_SYNC_POLARITY 0
#define MODE_H_F_ACTIVE_PIXELS 640
#define MODE_H_F_FRONT_PORCH   16
#define MODE_H_F_SYNC_WIDTH    96
#define MODE_H_F_BACK_PORCH    48

#define MODE_V_F_SYNC_POLARITY 0
#define MODE_V_F_ACTIVE_LINES 480
#define MODE_V_F_FRONT_PORCH   10
#define MODE_V_F_SYNC_WIDTH    2
#define MODE_V_F_BACK_PORCH    33

#define MODE_H_8_SYNC_POLARITY 1
#define MODE_H_8_FRONT_PORCH   16
#define MODE_H_8_SYNC_WIDTH    112
#define MODE_H_8_BACK_PORCH    112

#define MODE_V_8_SYNC_POLARITY 1
#define MODE_V_8_FRONT_PORCH   8
#define MODE_V_8_SYNC_WIDTH    6
#define MODE_V_8_BACK_PORCH    23

#define MODE_H_4_SYNC_POLARITY 0
#define MODE_H_4_FRONT_PORCH   18
#define MODE_H_4_SYNC_WIDTH    108
#define MODE_H_4_BACK_PORCH    54

#define MODE_V_4_SYNC_POLARITY 0
#define MODE_V_4_FRONT_PORCH   12
#define MODE_V_4_SYNC_WIDTH    2
#define MODE_V_4_BACK_PORCH    35

#define MODE_H_W_SYNC_POLARITY 1
#define MODE_H_W_FRONT_PORCH   110
#define MODE_H_W_SYNC_WIDTH    40
#define MODE_H_W_BACK_PORCH    220

#define MODE_V_W_SYNC_POLARITY 1
#define MODE_V_W_FRONT_PORCH   5
#define MODE_V_W_SYNC_WIDTH    5
#define MODE_V_W_BACK_PORCH    20


#define MODE_H_L_SYNC_POLARITY 0
#define MODE_H_L_FRONT_PORCH   24
#define MODE_H_L_SYNC_WIDTH    136
#define MODE_H_L_BACK_PORCH    144

#define MODE_V_L_SYNC_POLARITY 0
#define MODE_V_L_FRONT_PORCH   3
#define MODE_V_L_SYNC_WIDTH    6
#define MODE_V_L_BACK_PORCH    29

#define MODE_H_V_SYNC_POLARITY 1
#define MODE_H_V_FRONT_PORCH   24
#define MODE_H_V_SYNC_WIDTH    72
#define MODE_H_V_BACK_PORCH    128

#define MODE_V_V_SYNC_POLARITY 1
#define MODE_V_V_FRONT_PORCH   1
#define MODE_V_V_SYNC_WIDTH    2
#define MODE_V_V_BACK_PORCH    22

#define MODE_H_S_TOTAL_PIXELS ( \
    MODE_H_S_FRONT_PORCH + MODE_H_S_SYNC_WIDTH + \
    MODE_H_S_BACK_PORCH  + MODE_H_S_ACTIVE_PIXELS \
)
#define MODE_V_S_TOTAL_LINES  ( \
    MODE_V_S_FRONT_PORCH + MODE_V_S_SYNC_WIDTH + \
    MODE_V_S_BACK_PORCH  + MODE_V_S_ACTIVE_LINES \
)
#define MODE_H_F_TOTAL_PIXELS ( \
    MODE_H_F_FRONT_PORCH + MODE_H_F_SYNC_WIDTH + \
    MODE_H_F_BACK_PORCH  + MODE_H_F_ACTIVE_PIXELS \
)
#define MODE_V_F_TOTAL_LINES  ( \
    MODE_V_F_FRONT_PORCH + MODE_V_F_SYNC_WIDTH + \
    MODE_V_F_BACK_PORCH  + MODE_V_F_ACTIVE_LINES \
)
#define MODE_H_W_TOTAL_PIXELS ( \
    MODE_H_W_FRONT_PORCH + MODE_H_W_SYNC_WIDTH + \
    MODE_H_W_BACK_PORCH  + MODE_H_W_ACTIVE_PIXELS \
)
#define MODE_V_W_TOTAL_LINES  ( \
    MODE_V_W_FRONT_PORCH + MODE_V_W_SYNC_WIDTH + \
    MODE_V_W_BACK_PORCH  + MODE_V_W_ACTIVE_LINES \
)
#define MODE_H_L_TOTAL_PIXELS ( \
    MODE_H_L_FRONT_PORCH + MODE_H_L_SYNC_WIDTH + \
    MODE_H_L_BACK_PORCH  + MODE_H_L_ACTIVE_PIXELS \
)
#define MODE_V_L_TOTAL_LINES  ( \
    MODE_V_L_FRONT_PORCH + MODE_V_L_SYNC_WIDTH + \
    MODE_V_L_BACK_PORCH  + MODE_V_L_ACTIVE_LINES \
)
#define MODE_H_V_TOTAL_PIXELS ( \
    MODE_H_V_FRONT_PORCH + MODE_H_V_SYNC_WIDTH + \
    MODE_H_V_BACK_PORCH  + MODE_H_V_ACTIVE_PIXELS \
)
#define MODE_V_V_TOTAL_LINES  ( \
    MODE_V_V_FRONT_PORCH + MODE_V_V_SYNC_WIDTH + \
    MODE_V_V_BACK_PORCH  + MODE_V_V_ACTIVE_LINES \
)
#define MODE_H_8_TOTAL_PIXELS ( \
    MODE_H_8_FRONT_PORCH + MODE_H_8_SYNC_WIDTH + \
    MODE_H_8_BACK_PORCH  + MODE_H_8_ACTIVE_PIXELS \
)
#define MODE_V_8_TOTAL_LINES  ( \
    MODE_V_8_FRONT_PORCH + MODE_V_8_SYNC_WIDTH + \
    MODE_V_8_BACK_PORCH  + MODE_V_8_ACTIVE_LINES \
)
#define MODE_H_4_TOTAL_PIXELS ( \
    MODE_H_4_FRONT_PORCH + MODE_H_4_SYNC_WIDTH + \
    MODE_H_4_BACK_PORCH  + MODE_H_4_ACTIVE_PIXELS \
)
#define MODE_V_4_TOTAL_LINES  ( \
    MODE_V_4_FRONT_PORCH + MODE_V_4_SYNC_WIDTH + \
    MODE_V_4_BACK_PORCH  + MODE_V_4_ACTIVE_LINES \
)

volatile int mode = 1;
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)
#define DMACH_PING 0
#define DMACH_PONG 1
const uint32_t MAP256DEF[256] =
{
0x0,0x55,0xAA,0xFF,0x2400,0x2455,0x24AA,0x24FF,
0x4900,0x4955,0x49AA,0x49FF,0x6D00,0x6D55,0x6DAA,0x6DFF,
0x9200,0x9255,0x92AA,0x92FF,0xB600,0xB655,0xB6AA,0xB6FF,
0xDB00,0xDB55,0xDBAA,0xDBFF,0xFF00,0xFF55,0xFFAA,0xFFFF,
0x240000,0x240055,0x2400AA,0x2400FF,0x242400,0x242455,0x2424AA,0x2424FF,
0x244900,0x244955,0x2449AA,0x2449FF,0x246D00,0x246D55,0x246DAA,0x246DFF,
0x249200,0x249255,0x2492AA,0x2492FF,0x24B600,0x24B655,0x24B6AA,0x24B6FF,
0x24DB00,0x24DB55,0x24DBAA,0x24DBFF,0x24FF00,0x24FF55,0x24FFAA,0x24FFFF,
0x490000,0x490055,0x4900AA,0x4900FF,0x492400,0x492455,0x4924AA,0x4924FF,
0x494900,0x494955,0x4949AA,0x4949FF,0x496D00,0x496D55,0x496DAA,0x496DFF,
0x499200,0x499255,0x4992AA,0x4992FF,0x49B600,0x49B655,0x49B6AA,0x49B6FF,
0x49DB00,0x49DB55,0x49DBAA,0x49DBFF,0x49FF00,0x49FF55,0x49FFAA,0x49FFFF,
0x6D0000,0x6D0055,0x6D00AA,0x6D00FF,0x6D2400,0x6D2455,0x6D24AA,0x6D24FF,
0x6D4900,0x6D4955,0x6D49AA,0x6D49FF,0x6D6D00,0x6D6D55,0x6D6DAA,0x6D6DFF,
0x6D9200,0x6D9255,0x6D92AA,0x6D92FF,0x6DB600,0x6DB655,0x6DB6AA,0x6DB6FF,
0x6DDB00,0x6DDB55,0x6DDBAA,0x6DDBFF,0x6DFF00,0x6DFF55,0x6DFFAA,0x6DFFFF,
0x920000,0x920055,0x9200AA,0x9200FF,0x922400,0x922455,0x9224AA,0x9224FF,
0x924900,0x924955,0x9249AA,0x9249FF,0x926D00,0x926D55,0x926DAA,0x926DFF,
0x929200,0x929255,0x9292AA,0x9292FF,0x92B600,0x92B655,0x92B6AA,0x92B6FF,
0x92DB00,0x92DB55,0x92DBAA,0x92DBFF,0x92FF00,0x92FF55,0x92FFAA,0x92FFFF,
0xB60000,0xB60055,0xB600AA,0xB600FF,0xB62400,0xB62455,0xB624AA,0xB624FF,
0xB64900,0xB64955,0xB649AA,0xB649FF,0xB66D00,0xB66D55,0xB66DAA,0xB66DFF,
0xB69200,0xB69255,0xB692AA,0xB692FF,0xB6B600,0xB6B655,0xB6B6AA,0xB6B6FF,
0xB6DB00,0xB6DB55,0xB6DBAA,0xB6DBFF,0xB6FF00,0xB6FF55,0xB6FFAA,0xB6FFFF,
0xDB0000,0xDB0055,0xDB00AA,0xDB00FF,0xDB2400,0xDB2455,0xDB24AA,0xDB24FF,
0xDB4900,0xDB4955,0xDB49AA,0xDB49FF,0xDB6D00,0xDB6D55,0xDB6DAA,0xDB6DFF,
0xDB9200,0xDB9255,0xDB92AA,0xDB92FF,0xDBB600,0xDBB655,0xDBB6AA,0xDBB6FF,
0xDBDB00,0xDBDB55,0xDBDBAA,0xDBDBFF,0xDBFF00,0xDBFF55,0xDBFFAA,0xDBFFFF,
0xFF0000,0xFF0055,0xFF00AA,0xFF00FF,0xFF2400,0xFF2455,0xFF24AA,0xFF24FF,
0xFF4900,0xFF4955,0xFF49AA,0xFF49FF,0xFF6D00,0xFF6D55,0xFF6DAA,0xFF6DFF,
0xFF9200,0xFF9255,0xFF92AA,0xFF92FF,0xFFB600,0xFFB655,0xFFB6AA,0xFFB6FF,
0xFFDB00,0xFFDB55,0xFFDBAA,0xFFDBFF,0xFFFF00,0xFFFF55,0xFFFFAA,0xFFFFFF
};
const uint32_t MAP16DEF[16] = {0x00,0xFF,0x5500,0x55ff,0xAA00,0xAAff,0xff00,0xffff,0xff0000,0xff00FF,0xff5500,0xff55ff,0xffAA00,0xffAAff,0xffff00,0xffffff};
const uint32_t MAP4DEF[4] = {0,0xFF,0xFF00,0xFF0000};
uint16_t map256[256];
static uint32_t vblank_line_vsync_off[7] ;
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];
static bool dma_pong = false;
int MODE_H_SYNC_POLARITY, MODE_V_TOTAL_LINES, MODE_ACTIVE_LINES, MODE_ACTIVE_PIXELS;
int MODE_H_ACTIVE_PIXELS, MODE_H_FRONT_PORCH, MODE_H_SYNC_WIDTH, MODE_H_BACK_PORCH;
int MODE_V_SYNC_POLARITY ,MODE_V_ACTIVE_LINES ,MODE_V_FRONT_PORCH, MODE_V_SYNC_WIDTH, MODE_V_BACK_PORCH;
int PIXELS_PER_WORD, TRANSFER_COUNT, BLANKING_COUNT;
// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
volatile int32_t v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;
void MIPS64 __not_in_flash_func(dma_irq_handler0)() {
    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;
 
    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        ch->transfer_count = TRANSFER_COUNT;
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    } 
}

// ----------------------------------------------------------------------------
// Main program

/*void MIPS32 __not_in_flash_func(HDMIloop1)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1280x720x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*vgaloop8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*vgaloop8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<vgaloop32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq);
                        }
                    }
                    break;
                case SCREENMODE2: //320 x 180 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*vgaloop8;
                        for(int i=0; i<vgaloop8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //640 x 360 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*vgaloop4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        register uint8_t *dd=&DisplayBuf[pp];
                        register uint8_t *ll=&LayerBuf[pp];
                        for(int i=0; i<vgaloop16 ; i++){
                            l=*ll++;d=*dd++;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            l=*ll++;d=*dd++;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            l=*ll++;d=*dd++;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            l=*ll++;d=*dd++;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //320 x 180 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*vgaloop4;
                        for(int i=0; i<vgaloop4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}*/
void MIPS32 __not_in_flash_func(HDMIloop1)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_W_TOTAL_LINES - MODE_V_W_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_W_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1280x720x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*MODE_H_W_ACTIVE_PIXELS/8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*MODE_H_W_ACTIVE_PIXELS/8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //320 x 180 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_W_ACTIVE_PIXELS/8;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //640 x 360 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*MODE_H_W_ACTIVE_PIXELS/4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //320 x 180 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_W_ACTIVE_PIXELS/4;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void MIPS32 __not_in_flash_func(HDMIloop2)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_L_TOTAL_LINES - MODE_V_L_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_L_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1024x768x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*MODE_H_L_ACTIVE_PIXELS/8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*MODE_H_L_ACTIVE_PIXELS/8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //256 x 192 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_L_ACTIVE_PIXELS/8;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16q[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16q[l&0xf];
                                } else {
                                    *p++=map16q[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //512 x 384 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*MODE_H_L_ACTIVE_PIXELS/4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //256 x 192 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_L_ACTIVE_PIXELS/4;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void MIPS32 __not_in_flash_func(HDMIloop3)(void){
    int last_line=2,load_line, line_to_load, Line_dup;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            Line_dup=load_line>>1;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //800x600x2 or 848x480x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint16_t *pp=(uint16_t *)&DisplayBuf[load_line*vgaloop8];
                        uint16_t *qq=(uint16_t *)&LayerBuf[load_line*vgaloop8];
                        uint16_t d=*pp | *qq;
                        for(int i=0; i<vgaloop16 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //400 X 300 x 4bit-colour mapped to 256 or 424 X 240 x 4bit-colour mapped to 256
                    {
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        int pp= (Line_dup)*vgaloop4;
                        for(int i=0; i<vgaloop4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16d[l&0xf];
                            } else {
                                *p++=map16d[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE3: //800 x 600 x 4bit-colour mapped to 256 or 848 x 480 x 4bit-colour mapped to 256
                    {
                        int pp= load_line*vgaloop2;
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<vgaloop2 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16s[l&0xf];
                            } else {
                                *p++=map16s[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16s[l&0xf];
                            } else {
                                *p++=map16s[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //400 x 300 x 8bit-colour or 424 x 240 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        int pp= (Line_dup)*vgaloop2;
                        for(int i=0; i<vgaloop2 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if(l!=transparent){
                                *p++=l;
                                *p++=l;
                            } else {
                                *p++=d;
                                *p++=d;
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}

void MIPS32 __not_in_flash_func(HDMIloop0)(void){
    int last_line=2,load_line, line_to_load, line;
    while (1){
        if(v_scanline!=last_line){
            uint8_t transparent16=(uint8_t)transparent;
            uint8_t transparent16s=(uint8_t)transparents;
            last_line=v_scanline;
            load_line=line=v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            if(HRes==vgaloop2)line>>=1;
            line_to_load = last_line & 1;
            uint8_t l,d,s;
            register unsigned char *dd;
            register unsigned char *ll;
            int pp;
            uint16_t *p=HDMIlines[line_to_load];
            if(load_line>=0 && load_line<MODE_V_ACTIVE_LINES){
                __dsb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //720x400x2 colour
                    uint16_t *fcol=tilefcols+line/ytileheight*X_TILE, *bcol=tilebcols+line/ytileheight*X_TILE; //get the relevant tile
                    pp= line*vgaloop8;
                    dd=&DisplayBuf[pp];
                    ll=&LayerBuf[pp];
                    for(int i=0; i<vgaloop8 ; i++){
                        d=*dd | *ll;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        ll++;dd++;
                    }
                    break;
                case SCREENMODE2: //360x200x16 colour with support for a top layer
                    {
                        pp= (line)*vgaloop4;
                        uint32_t *up=(uint32_t *)p;
                        dd=&DisplayBuf[pp];
                        ll=&LayerBuf[pp];
                        volatile register unsigned char *ss=&SecondLayer[pp];
                        if(ss==dd){
                            ss=ll;
                            transparent16s=transparent16;
                        }
                        for(int i=0; i<vgaloop4 ; i++){
                            l=ll[i];d=dd[i];s=ss[i];
                            if((s&0xf)!=transparent16s){
                                *up++=map16pairs[s&0xf];
                            } else {
                                if((l&0xf)!=transparent16){
                                    *up++=map16pairs[l&0xf];
                                } else {
                                    *up++=map16pairs[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparent16s){
                                *up++=map16pairs[s&0xf];
                            } else {
                                if((l&0xf)!=transparent16){
                                    *up++=map16pairs[l&0xf];
                                } else {
                                    *up++=map16pairs[d&0xf];
                                }
                            }
                        }
                    }
                    break;
                case SCREENMODE3: //720x400x16 colour
                    pp= line*vgaloop2;
                    for(int i=0;i<vgaloop2;i++){
                        d=DisplayBuf[pp+i];
                        l=LayerBuf[pp+i];
                        if((l&0xf)!=transparent16){
                            *p++=map16[l&0xf];
                        } else {
                            *p++=map16[d&0xf];
                        }
                        d>>=4;l>>=4;
                        if((l&0xf)!=transparent16){
                            *p++=map16[l&0xf];
                        } else {
                            *p++=map16[d&0xf];
                        }
                    }
                    break;
                case SCREENMODE4: //360x200xRGB555 colour
                    pp=line*vgaloop1;
                    uint16_t* d=(uint16_t *)&DisplayBuf[pp];
                    uint16_t* l=(uint16_t *)&LayerBuf[pp];
                    for(int i=0; i<vgaloop2 ; i++){
                        if(*l!=RGBtransparent){
                            *p++=*l;
                            *p++=*l;
                        } else {
                            *p++=*d;
                            *p++=*d;
                        }
                        l++;d++;
                    }
                    break;
                case SCREENMODE5: //360x200x256 colour
                    pp=line*vgaloop2;
                    dd=&DisplayBuf[pp];
                    ll=&LayerBuf[pp];
                    register unsigned char *ss=&SecondLayer[pp];
                    if(ss==dd){
                        ss=ll;
                        transparent16s=transparent16;
                    }
                    for(int i=0; i<vgaloop2 ; i++){
                        int d=dd[i];
                        int l=ll[i];
                        int s=ss[i];
                        if(s!=transparent16s){
                            *p++=map256[s];
                            *p++=map256[s];
                        } else {
                            if(l!=transparent16){
                                *p++=map256[l];
                                *p++=map256[l];
                            } else {
                                *p++=map256[d];
                                *p++=map256[d];
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void HDMICore(void){
    for(int i=0;i<256;i++)map256[i]=RGB555(MAP256DEF[i]);
    for(int i=0;i<16;i++){
        map16[i]=RGB555(MAP16DEF[i]);
        map16pairs[i]=map16[i] | (map16[i]<<16);
        map16d[i]=(RGB332(MAP16DEF[i])<<8) | RGB332(MAP16DEF[i]);
        map16q[i]=(RGB332(MAP16DEF[i])<<24) | (RGB332(MAP16DEF[i])<<16) | (RGB332(MAP16DEF[i])<<8) | RGB332(MAP16DEF[i]);
        map16s[i]=RGB332(MAP16DEF[i]);
    }
    if(Option.CPU_Speed==FreqXGA){
        MODE_H_SYNC_POLARITY=MODE_H_L_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_L_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_L_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_L_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_L_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_L_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_L_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_L_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_L_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_L_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_L_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_L_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_L_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_L;
        MODE2SIZE=MODE2SIZE_L;
        MODE3SIZE=MODE3SIZE_L;
        MODE4SIZE=0L;
        MODE5SIZE=MODE5SIZE_L;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq720P){
        MODE_H_SYNC_POLARITY=MODE_H_W_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_W_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_W_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_W_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_W_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_W_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_W_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_W_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_W_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_W_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_W_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_W_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_W_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_W;
        MODE2SIZE=MODE2SIZE_W;
        MODE3SIZE=MODE3SIZE_W;
        MODE4SIZE=0L;
        MODE5SIZE=MODE5SIZE_W;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==FreqSVGA){
        MODE_H_SYNC_POLARITY=MODE_H_V_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_V_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_V_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_V_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_V_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_V_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_V_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_V_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_V_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_V_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_V_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_V_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_V_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_V;
        MODE2SIZE=MODE2SIZE_V;
        MODE3SIZE=MODE3SIZE_V;
        MODE5SIZE=MODE5SIZE_V;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq848){
        MODE_H_SYNC_POLARITY=MODE_H_8_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_8_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_8_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_8_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_8_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_8_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_8_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_8_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_8_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_8_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_8_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_8_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_8_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_8;
        MODE2SIZE=MODE2SIZE_8;
        MODE3SIZE=MODE3SIZE_8;
        MODE5SIZE=MODE5SIZE_8;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq400){
        MODE_H_SYNC_POLARITY=MODE_H_4_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_4_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_4_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_4_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_4_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_4_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_4_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_4_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_4_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_4_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_4_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_4_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_4_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_4;
        MODE2SIZE=MODE2SIZE_4;
        MODE3SIZE=MODE3SIZE_4;
        MODE4SIZE=MODE3SIZE_4;
        MODE5SIZE=MODE5SIZE_4;
        PIXELS_PER_WORD=2;
    }  else {
        MODE_H_SYNC_POLARITY=MODE_H_S_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_S_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_S_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_S_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_S_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_S_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_S_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_S_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_S_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_S_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_S_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_S_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_S_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_S;
        MODE2SIZE=MODE2SIZE_S;
        MODE3SIZE=MODE3SIZE_S;
        MODE4SIZE=MODE4SIZE_S;
        MODE5SIZE=MODE5SIZE_S;
        PIXELS_PER_WORD=2;
    }
    vgaloop1=MODE_H_ACTIVE_PIXELS;
    vgaloop2=MODE_H_ACTIVE_PIXELS/2;
    vgaloop4=MODE_H_ACTIVE_PIXELS/4;
    vgaloop8=MODE_H_ACTIVE_PIXELS/8;
    vgaloop16=MODE_H_ACTIVE_PIXELS/16;
    vgaloop32=MODE_H_ACTIVE_PIXELS/32;
    TRANSFER_COUNT=MODE_H_ACTIVE_PIXELS/PIXELS_PER_WORD;
    BLANKING_COUNT=MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH;

    vblank_line_vsync_off[0] =       HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vblank_line_vsync_off[1] =       SYNC_V1_H1;
    vblank_line_vsync_off[2] =       HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vblank_line_vsync_off[3] =       SYNC_V1_H0;
    vblank_line_vsync_off[4] =       HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
    vblank_line_vsync_off[5] =       SYNC_V1_H1;
    vblank_line_vsync_off[6] =       HSTX_CMD_NOP;

    vblank_line_vsync_on[0] =    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vblank_line_vsync_on[1] =    SYNC_V0_H1;
    vblank_line_vsync_on[2] =    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vblank_line_vsync_on[3] =    SYNC_V0_H0,
    vblank_line_vsync_on[4] =    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
    vblank_line_vsync_on[5] =    SYNC_V0_H1;
    vblank_line_vsync_on[6] =    HSTX_CMD_NOP;

    vactive_line[0] =    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vactive_line[1] =    SYNC_V1_H1;
    vactive_line[2] =    HSTX_CMD_NOP;
    vactive_line[3] =    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vactive_line[4] =    SYNC_V1_H0;
    vactive_line[5] =    HSTX_CMD_NOP;
    vactive_line[6] =    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH;
    vactive_line[7] =    SYNC_V1_H1;
    vactive_line[8] =    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS;
        // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds =
        ((FullColour) ? 
            (29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            2 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            7 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB) 
            :
            (2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB));

    // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
    // entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        ((FullColour) ? 
            (2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB)
            :
            (4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB));


        // Serial output config: clock period of 5 cycles, pop from command
        // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS; 

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    // Pinout on Pico DVI sock:
    //
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-

    // Assign clock pair to two neighbouring pins:
    if(Option.HDMIclock & 1){
        hstx_ctrl_hw->bit[Option.HDMIclock] = HSTX_CTRL_BIT0_CLK_BITS;
        hstx_ctrl_hw->bit[Option.HDMIclock-1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;

    } else {
        hstx_ctrl_hw->bit[Option.HDMIclock] = HSTX_CTRL_BIT0_CLK_BITS;
        hstx_ctrl_hw->bit[Option.HDMIclock+1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    }
    int lane_to_output_bit[3];
    lane_to_output_bit[0]=Option.HDMId0;
    lane_to_output_bit[1]=Option.HDMId1;
    lane_to_output_bit[2]=Option.HDMId2;
        
    for (uint lane = 0; lane < 3; ++lane) {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        if(bit & 1){
            hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
            hstx_ctrl_hw->bit[bit - 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
        } else {
            hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
            hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
        }
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX
        gpio_set_drive_strength (i, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(i,GPIO_SLEW_RATE_FAST);
        gpio_set_input_enabled(i, false);
        gpio_set_pulls(i,false,false);
        gpio_set_input_hysteresis_enabled(i,false);
    }

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler0);
    irq_set_enabled(DMA_IRQ_0, true);

//    bus_ctrl_hw->priority = 1;
    dma_channel_start(DMACH_PING);
    if(Option.CPU_Speed==Freq480P || Option.CPU_Speed==Freq378P  || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq400)HDMIloop0();
    else if(Option.CPU_Speed==Freq720P)HDMIloop1();
    else if(Option.CPU_Speed==FreqXGA)HDMIloop2();
    else HDMIloop3();
}
void settiles(void){
    if(DISPLAY_TYPE!=SCREENMODE1)return;
    if(FullColour){
        tilefcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3));
        tilebcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3)+(MODE1SIZE>>1));
        ytileheight=gui_font_height;
        Y_TILE=VRes/ytileheight;
        X_TILE=HRes/8;
        if(VRes % ytileheight)Y_TILE++;
            for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
                tilefcols[y*X_TILE+x]=RGB555(gui_fcolour);
                tilebcols[y*X_TILE+x]=RGB555(gui_bcolour);
            }
        }
    } else {
        tilefcols_w=(uint8_t *)DisplayBuf+MODE1SIZE;
        tilebcols_w=tilefcols_w+(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8); //minimum tilesize is 8x8
        memset(tilefcols_w,RGB332(gui_fcolour),(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8)*sizeof(uint8_t));
        memset(tilebcols_w,RGB332(gui_bcolour),(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8)*sizeof(uint8_t));
        ytileheight=(MediumRes? 12:24);
        X_TILE=MODE_H_ACTIVE_PIXELS/8;Y_TILE=MODE_V_ACTIVE_LINES/ytileheight;
    }
}

#endif
#else
#ifdef PICOMITE
#include "pico/multicore.h"
void __not_in_flash_func(UpdateCore)()
{
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
    while(multicore_fifo_rvalid()) {
        multicore_fifo_pop_blocking();
    }

	while (true)
	{
		// data memory barrier
		__dmb();
        if (multicore_fifo_rvalid()) {
            int command=multicore_fifo_pop_blocking();
			if(command==3){
                uint8_t colour=(uint8_t)multicore_fifo_pop_blocking();
                uint32_t timer=(uint32_t)multicore_fifo_pop_blocking();
                uint64_t delaytime=0;
                if(timer)delaytime=time_us_64()+timer;
                mergerunning=true;
                while(1){
                    if (multicore_fifo_rvalid()){
                        int a;
                        if(((a=multicore_fifo_pop_blocking())==0xff)){
                            mergerunning=false;
                            break;
                        }
                    }
                    if(timer){
                        busy_wait_until(delaytime);
                        delaytime=time_us_64()+timer;
                    }
                    merge(colour);
                }
            } else if(command==2){
                uint8_t colour=(uint8_t)multicore_fifo_pop_blocking();
                merge(colour);
            } else if(command==4){
                int x1=multicore_fifo_pop_blocking();
                int y1=multicore_fifo_pop_blocking();
                int w=multicore_fifo_pop_blocking();
                int h=multicore_fifo_pop_blocking();
                uint8_t colour=(uint8_t)multicore_fifo_pop_blocking();
                blitmerge(x1,y1,w,h,colour);
			} else if(command==5){
                int x1=multicore_fifo_pop_blocking();
                int y1=multicore_fifo_pop_blocking();
                int w=multicore_fifo_pop_blocking();
                int h=multicore_fifo_pop_blocking();
                uint8_t colour=(uint8_t)multicore_fifo_pop_blocking();
                uint32_t timer=(uint32_t)multicore_fifo_pop_blocking();
                uint64_t delaytime=0;
                if(timer)delaytime=time_us_64()+timer;
                mergerunning=true;
                while(1){
                    if (multicore_fifo_rvalid()){
                        int a;
                        if(((a=multicore_fifo_pop_blocking())==0xff)){
                            mergerunning=false;
                            break;
                        }
                    }
                    if(timer){
                        busy_wait_until(delaytime);
                        delaytime=time_us_64()+timer;
                    }
                    blitmerge(x1,y1,w,h,colour);
                }
            } else if(command==1){ 
                uint8_t *s=(uint8_t *)multicore_fifo_pop_blocking();
                mutex_enter_blocking(&frameBufferMutex);			// lock the frame buffer
                copyframetoscreen(s,0,HRes-1,0,VRes-1,0);
                mutex_exit(&frameBufferMutex);
            }
        } 
    }

}
uint32_t core1stack[512];
#endif
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
static void MIPS16 transform_star_command(char *input) {
    char *src = input;
    while (isspace((uint8_t)*src)) src++; // Skip leading whitespace.
    if (*src != '*') error("Internal fault");
    src++;

    // Trim any trailing whitespace from the input.
    char *end = input + strlen(input) - 1;
    while (isspace((uint8_t)*end)) *end-- = '\0';

    // Allocate extra space to avoid string overrun.
    char *tmp = (char *) GetTempMemory(STRINGSIZE + 32);
    strcpy(tmp, "RUN");
    char *dst = tmp + 3;

    if (*src == '"') {
        // Everything before the second quote is the name of the file to RUN.
        *dst++ = ' ';
        *dst++ = *src++; // Leading quote.
        while (*src && *src != '"') *dst++ = *src++;
        if (*src == '"') *dst++ = *src++; // Trailing quote.
    } else {
        // Everything before the first space is the name of the file to RUN.
        int count = 0;
        while (*src && !isspace((uint8_t)*src)) {
            if (++count == 1) {
                *dst++ = ' ';
                *dst++ = '\"';
            }
            *dst++ = *src++;
        }
        if (count) *dst++ = '\"';
    }

    while (isspace((uint8_t)*src)) src++; // Skip whitespace.

    // Anything else is arguments.
    if (*src) {
        *dst++ = ',';
        *dst++ = ' ';

        // If 'src' starts with double-quote then replace with: Chr$(34) +
        if (*src == '"') {
            memcpy(dst, "Chr$(34) + ", 11);
            dst += 11;
            src++;
        }

        *dst++ = '\"';

        // Copy from 'src' to 'dst'.
        while (*src) {
            if (*src == '"') {
                // Close current set of quotes to insert a Chr$(34)
                memcpy(dst, "\" + Chr$(34)", 12);
                dst += 12;

                // Open another set of quotes unless this was the last character.
                if (*(src + 1)) {
                    memcpy(dst, " + \"", 4);
                    dst += 4;
                }
                src++;
            } else {
                *dst++ = *src++;
            }
            if (dst - tmp >= STRINGSIZE) error("String too long");
        }

        // End with a double quote unless 'src' ended with one.
        if (*(src - 1) != '"') *dst++ = '\"';

        *dst = '\0';
    }

    if (dst - tmp >= STRINGSIZE) error("String too long");

    // Copy transformed string back into the input buffer.
    memcpy(input, tmp, STRINGSIZE);
    input[STRINGSIZE - 1] = '\0';

    ClearSpecificTempMemory(tmp);
}
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
    static int ErrorInPrompt;
    char savewatchdog = false;
    int i = watchdog_caused_reboot();
#ifdef rp2350
    restart_reason=powman_hw->chip_reset | i;
    rp2350a=(*((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1);
#else
    restart_reason=vreg_and_chip_reset_hw->chip_reset | i;
#endif
    if(_excep_code == SOFT_RESET || _excep_code == SCREWUP_TIMEOUT )restart_reason=0xFFFFFFFF;
    if((_excep_code == WATCHDOG_TIMEOUT) & i) restart_reason=0xFFFFFFFE;
    if((_excep_code == POSSIBLE_WATCHDOG) & i)restart_reason=0xFFFFFFFD;
    ResetOptionsNoSave();
#ifdef rp2350
    if(rom_get_last_boot_type()==BOOT_TYPE_FLASH_UPDATE)restart_reason=0xFFFFFFFC;
#else
    if(restart_reason==0x10001 || restart_reason==0x101)restart_reason=0xFFFFFFFC;
#endif
    uint32_t excep = _excep_code;
    InitSDCARD();
    LoadOptions();
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
    if(Option.VGA_BLUE == 0){
        Option.VGA_BLUE = 9;
        SaveOptions();
    }
#else
    if(!(Option.CPU_Speed==Freq720P || Option.CPU_Speed==Freq378P || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq848  || Option.CPU_Speed==Freq400 || Option.CPU_Speed==FreqSVGA || Option.CPU_Speed==Freq480P|| Option.CPU_Speed==FreqXGA )){
        Option.CPU_Speed=Freq480P;
        SaveOptions();
    }
#endif
    m_alloc(M_PROG);                                           // init the variables for program memory
    LibMemory = sd_libmemory;
    uSec(100);
    if(_excep_code == RESET_CLOCKSPEED) {
#ifdef PICOMITEVGA
#ifdef HDMI
        Option.CPU_Speed=Freq480P;              // init the options if this is the very first startup
#else
        Option.CPU_Speed=Freq378P; ///  Freq252P;              // init the options if this is the very first startup
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
#ifdef rp2350
//    volatile uint32_t *qmi_m0_timing=(uint32_t *)0x400d000c;
//    volatile uint32_t *qmi_m1_timing=(uint32_t *)0x400d0020;
#endif
    if(Option.CPU_Speed<=200000)vreg_set_voltage(VREG_VOLTAGE_1_15);
//    else if(Option.CPU_Speed>200000 && Option.CPU_Speed<=300000 )vreg_set_voltage(VREG_VOLTAGE_1_25);  // Std default @ boot is 1_10
    else if(Option.CPU_Speed>200000  && Option.CPU_Speed<=320000 )vreg_set_voltage(VREG_VOLTAGE_1_30);  // Std default @ boot is 1_10
#ifdef rp2350
    else if(Option.CPU_Speed>320000  && Option.CPU_Speed<=360000 )vreg_set_voltage(VREG_VOLTAGE_1_40);  // Std default @ boot is 1_10
    else vreg_set_voltage(VREG_VOLTAGE_1_60);  // Std default @ boot is 1_10
#else
    else vreg_set_voltage(VREG_VOLTAGE_1_30); 
#endif
    sleep_ms(10);
#ifdef rp2350
#define QMI_COOLDOWN 30    // 0xc0000000 [31:30] COOLDOWN     (0x1) Chip select cooldown period
#define QMI_PAGEBREAK 28        // 0x30000000 [29:28] PAGEBREAK    (0x0) When page break is enabled, chip select will...
#define QMI_SELECT_SETUP 25        // 0x02000000 [25]    SELECT_SETUP (0) Add up to one additional system clock cycle of setup...
#define QMI_SELECT_HOLD 23        // 0x01800000 [24:23] SELECT_HOLD  (0x0) Add up to three additional system clock cycles of active...
#define QMI_MAX_SELECT 17        // 0x007e0000 [22:17] MAX_SELECT   (0x00) Enforce a maximum assertion duration for this window's...
#define QMI_MIN_DESELECT 12        // 0x0001f000 [16:12] MIN_DESELECT (0x00) After this window's chip select is deasserted, it...
#define QMI_RXDELAY 8          // 0x00000700 [10:8]  RXDELAY      (0x0) Delay the read data sample timing, in units of one half...
#define QMI_CLKDIV 0           // 0x000000ff [7:0]   CLKDIV       (0x04) Clock divisor
    pads_qspi_hw->io[0]=0x67;
    pads_qspi_hw->io[1]=0x67;
    pads_qspi_hw->io[2]=0x67;
    pads_qspi_hw->io[3]=0x6B;
    pads_qspi_hw->io[4]=0x6B;
    pads_qspi_hw->io[5]=0x6B;
    qmi_hw->m[1].timing = (1<<QMI_COOLDOWN) | (2<<QMI_PAGEBREAK) | (3<<QMI_SELECT_HOLD) | (18<<QMI_MAX_SELECT) | (4<<QMI_MIN_DESELECT) | (6<<QMI_RXDELAY) | (5<<QMI_CLKDIV);
    if(Option.CPU_Speed<=324000)qmi_hw->m[1].timing = (1<<QMI_COOLDOWN) | (2<<QMI_PAGEBREAK) | (3<<QMI_SELECT_HOLD) | (18<<QMI_MAX_SELECT) | (4<<QMI_MIN_DESELECT) | (4<<QMI_RXDELAY) | (4<<QMI_CLKDIV);
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    if(Option.CPU_Speed<=150000)qmi_hw->m[1].timing = 0x60006102;
    sleep_ms(2);
#endif
    set_sys_clock_khz(Option.CPU_Speed, false);
#ifdef rp2350
    qmi_hw->m[1].timing = (1<<QMI_COOLDOWN) | (2<<QMI_PAGEBREAK) | (3<<QMI_SELECT_HOLD) | (18<<QMI_MAX_SELECT) | (4<<QMI_MIN_DESELECT) | (6<<QMI_RXDELAY) | (5<<QMI_CLKDIV);
    if(Option.CPU_Speed<=324000)qmi_hw->m[1].timing = (1<<QMI_COOLDOWN) | (2<<QMI_PAGEBREAK) | (3<<QMI_SELECT_HOLD) | (18<<QMI_MAX_SELECT) | (4<<QMI_MIN_DESELECT) | (4<<QMI_RXDELAY) | (4<<QMI_CLKDIV);
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    if(Option.CPU_Speed<=150000)qmi_hw->m[1].timing = 0x60006102;

    sleep_ms(2);
#endif
    PWM_FREQ=44100;
    pico_get_unique_board_id_string (id_out, 12);
#ifdef rp2350
#ifndef PICOMITEWEB
    if(Option.PSRAM_CS_PIN){
        gpio_set_function(PinDef[Option.PSRAM_CS_PIN].GPno, GPIO_FUNC_XIP_CS1); // CS for PSRAM
        xip_ctrl_hw->ctrl|=XIP_CTRL_WRITABLE_M1_BITS;
        if(!(PSRAMsize=testPSRAM())){
            Option.PSRAM_CS_PIN=0;
            SaveOptions();
        }
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
#ifdef HDMI
    if(FullColour || MediumRes){
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
    /** disable_interrupts_pico(); */
    /** enable_interrupts_pico(); */
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
    initKeyboard();
#endif
	InitBasic();
#ifndef PICOMITEVGA
    InitDisplaySSD();
    InitDisplaySPI(0);
    InitDisplayI2C(0);
    InitDisplayVirtual();
    InitTouch();
    if(Option.BackLightLevel)setBacklight(Option.BackLightLevel, 0);
#endif
    ErrorInPrompt = false;
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
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+4));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+5));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+6));
    piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+7));
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
    if(!(_excep_code == RESTART_NOAUTORUN || _excep_code == INVALID_CLOCKSPEED || _excep_code == SCREWUP_TIMEOUT || _excep_code == WATCHDOG_TIMEOUT || (_excep_code==POSSIBLE_WATCHDOG && watchdog_caused_reboot()))){
        if(Option.Autorun==0 ){
            if(!(_excep_code == RESET_COMMAND || _excep_code == SOFT_RESET)){
                MMPrintString((char *)banner); // print sign on message
                MMPrintString((char *)COPYRIGHT); // print sign on message
            }
        } else {
            if(Option.Autorun!=MAXFLASHSLOTS+1){
                ProgMemory = (sd_target_contents+(Option.Autorun-1)*MAX_PROG_SIZE);
            }
            if(*ProgMemory != 0x01 ) {
                MMPrintString((char *)banner);
                MMPrintString((char *)COPYRIGHT); // print sign on message
            }
        }
    }
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
    savewatchdog = WatchdogSet;
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
	clearrepeat();
     for(int i=0;i<4;i++){
        memset((void *)&HID[i],0,sizeof(struct s_HID));
        HID[i].report_requested=true;
    }
//    USB_bus_reset();
    hcd_port_reset(BOARD_TUH_RHPORT);
    uSec(10000); //wait for any hub to power up
    hcd_port_reset_end(BOARD_TUH_RHPORT);
    tuh_init(BOARD_TUH_RHPORT);
    USBenabled=true;
#else
    initMouse0(0);
#endif
#ifdef rp2350
    if(PSRAMsize){MMPrintString("Total of ");PInt(PSRAMsize/(1024*1024));MMPrintString(" Mbytes PSRAM available\r\n");}
    #if defined(PICOMITEVGA) && !defined(HMDI)
        start_i2s(QVGA_PIO_NUM,1);
    #else
        start_i2s(2,1);
    #endif
#else
    start_i2s(QVGA_PIO_NUM,1);
#endif
    char *p=(char *)inpbuf;
	if(setjmp(mark) != 0) {
     // we got here via a long jump which means an error or CTRL-C or the program wants to exit to the command prompt
        FlashLoad = 0;
//        LoadOptions();
#ifdef USBKEYBOARD
	    clearrepeat();
#endif	    
        ScrewUpTimer = 0;
        ProgMemory=sd_progmemory;
        ContinuePoint = nextstmt;                               // in case the user wants to use the continue command
		*tknbuf = 0;											// we do not want to run whatever is in the token buffer
		optionangle=1.0;
        useoptionangle=false;
        savewatchdog = WatchdogSet = false;
		char *ptr = (char *)findvar((unsigned char *)"MM.ENDLINE$", V_NOFIND_NULL);
        if(ptr && *ptr){
            CurrentLinePtr = nullptr;
            memcpy((void*)inpbuf, ptr, *ptr+1);
            *ptr=0;
            MtoC(inpbuf);
            *ptr=0;
            tokenise(true);
            goto autorun;
        }
    } else {
        if(*ProgMemory == 0x01 ) ClearVars(0,true);
        else {
            ClearProgram(true);
        }
    #ifdef PICOMITEWEB
    if (cyw43_arch_init()==0) {
        startupcomplete=1;
        WebConnect();
    }
    #endif
#ifdef PICOMITE
    SPIatRisk=((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel) && Option.SD_CLK_PIN==0);
#endif
        PrepareProgram(true);
        if(FindSubFun(CombinedPtr((unsigned char *)"MM.STARTUP"), 0) >= 0) {
            ExecuteProgram(CombinedPtr((unsigned char *)"MM.STARTUP\0"));
            memset(inpbuf,0,STRINGSIZE);
        }
        if(Option.Autorun && _excep_code != RESTART_DOAUTORUN) {
            ClearRuntime(true);
            PrepareProgram(true);
            if(*ProgMemory == 0x01 ){
                memset(tknbuf,0,STRINGSIZE);
                unsigned short tkn=GetCommandValue((unsigned char *)"RUN");
                tknbuf[0] = (tkn & 0x7f ) + C_BASETOKEN;
                tknbuf[1] = (tkn >> 7) + C_BASETOKEN; //tokens can be 14-bit
                goto autorun;
            }  else {
                Option.Autorun=0;
                SaveOptions();
            }       
        }
    }
    while(1) {
        if(Option.DISPLAY_CONSOLE) {
            SetFont(PromptFont);
            gui_fcolour = PromptFC;
            gui_bcolour = PromptBC;
            if(CurrentX != 0) MMPrintString("\r\n");                    // prompt should be on a new line
        }
        MMAbort = false;
        BreakKey = BREAK_KEY;
        EchoOption = true;
        g_LocalIndex = 0;                                             // this should not be needed but it ensures that all space will be cleared
        ClearTempMemory();                                          // clear temp string space (might have been used by the prompt)
        CurrentLinePtr = nullptr;                                      // do not use the line number in error reporting
        if(MMCharPos > 1) MMPrintString("\r\n");                    // prompt should be on a new line
        while(Option.PIN && !IgnorePIN) {
            _excep_code = PIN_RESTART;
            if(Option.PIN == 99999999)                              // 99999999 is permanent lockdown
                MMPrintString("Console locked, press enter to restart: ");
            else
                MMPrintString("Enter PIN or 0 to restart: ");
            MMgetline(0, (char *)inpbuf);
            if(Option.PIN == 99999999) SoftReset();
            if(*inpbuf != 0) {
                uSec(3000000);
                i = atoi((char *)inpbuf);
                if(i == 0) SoftReset();
                if(i == Option.PIN) {
                    IgnorePIN = true;
                    break;
                }
            }
        }
        if(_excep_code!=POSSIBLE_WATCHDOG)_excep_code = 0;
        PrepareProgram(false);
        if(!ErrorInPrompt && FindSubFun(CombinedPtr((unsigned char *)"MM.PROMPT"), 0) >= 0) {
            ErrorInPrompt = true;
            ExecuteProgram(CombinedPtr((unsigned char *)"MM.PROMPT\0"));
            MMPromptPos=MMCharPos-1;    //Save length of prompt
        } else{
            MMPrintString("> ");                                    // print the prompt
            MMPromptPos=2;    //Save length of prompt
        }    
        ErrorInPrompt = false;
        EditInputLine();
        //InsertLastcmd(inpbuf);                                  // save in case we want to edit it later
        if(!*inpbuf) continue;                                      // ignore an empty line
        p=(char *)inpbuf;
        skipspace(p);
//        executelocal(p);
        if(strlen(p)==2 && p[1]==':'){
            if(toupper(*p)=='A')strcpy(p,"drive \"a:\"");
            if(toupper(*p)=='B')strcpy(p,"drive \"b:\"");
        }
        if(*p=='*' && p[1]!='('){ //shortform RUN command so convert to a normal version
                transform_star_command((char *)inpbuf);
                p = (char *)inpbuf;
        }
        multi=false;
        tokenise(true);                                             // turn into executable code
autorun:
        i=0;
        WatchdogSet=savewatchdog;
        CommandToken tkn=commandtbl_decode(tknbuf);
        if(tkn==GetCommandValue((unsigned char *)"RUN") || tkn==GetCommandValue((unsigned char *)"EDIT") || tkn==GetCommandValue((unsigned char *)"AUTOSAVE"))i=1;
        if (setjmp(jmprun) != 0) {
            PrepareProgram(false);
            CurrentLinePtr = nullptr;
        }
        ExecuteProgram(CombinedPtr(tknbuf));                                     // execute the line straight away
        if(i){
            cmdline = nullptr;
            do_end(false);
            longjmp(mark, 1);												// jump back to the input prompt
        }
        else {
            memset(inpbuf,0,STRINGSIZE);
	        longjmp(mark, 1);												// jump back to the input prompt
        }
	}
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
    unsigned char fontnbr, prevchar = 0, buf[STRINGSIZE];
    CombinedPtr p;
    unsigned short endtoken, tkn;
    int nbr, i, j, n, SaveSizeAddr;
    bool continuation=false;
    multi=false;
    uint32_t storedupdates[MAXCFUNCTION], updatecount=0, realflashsave;
    int firsthex=1;
    initFonts();
#ifdef rp2350
    __dsb();
#endif
#ifdef USBKEYBOARD
	clearrepeat();
#endif	
    memcpy((void*)buf, (void*)tknbuf, STRINGSIZE);                                // save the token buffer because we are going to use it
    FlashWriteInit(PROGRAM_FLASH);
    sd_range_erase(realflashpointer, MAX_PROG_SIZE);
    j=MAX_PROG_SIZE/4;
    CombinedPtrI pp = CombinedPtr(sd_progmemory);
        while(j--) if(*pp++ != 0xFFFFFFFF) {
            /** enable_interrupts_pico(); */
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
                do {p.write_byte(' ')++;
                    if((p.raw() - inpbuf) >= MAXSTRLEN) goto exiterror;
                } while((p.raw() - inpbuf) % 2);
            } else {
                if(isprint((uint8_t)*pm)) {
                    p.write_byte(*pm)++;
                    if((p.raw() - inpbuf) >= MAXSTRLEN) goto exiterror;
                }
            }
            prevchar = *pm++;
        }
        if(*pm) prevchar = *pm++;                                   // step over the end of line char but not the terminating zero
        p.write_byte(0);                                                     // terminate the string in inpbuf

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
    firsthex=1;
    realflashsave= realflashpointer;
    p = sd_progmemory;                                              // start scanning program memory
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
                /** enable_interrupts_pico(); */
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
                    /** enable_interrupts_pico(); */
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
                            /** enable_interrupts_pico(); */
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
                            /** enable_interrupts_pico(); */
                            error("Can't define non-printing characters");
                         }
                     }
                 }
                 // we are at the end of a embedded code line
                 while(*p) p++;                                      // make sure that we move to the end of the line
                 p++;                                                // step to the start of the next line
                 if(*p == 0) {
                     /** enable_interrupts_pico(); */
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
    p = sd_progmemory;                                              // start scanning program memory
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
                 /** enable_interrupts_pico(); */
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
             FlashWriteWord((unsigned int)(p.raw() - sd_progmemory));               // if a CFunction/CSub save a relative pointer to the declaration
             fontnbr = 0;
             p++;
         }
            SaveSizeAddr = realflashpointer;                                // save where we are so that we can write the CFun size in here
             FlashWriteWord(storedupdates[updatecount++]);                        // leave this blank so that we can later do the write
             p++;
             skipspace(p);
             if(!fontnbr) {
                 if(!isnamestart((uint8_t)*p))  {
                     /** enable_interrupts_pico(); */
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
                            /** enable_interrupts_pico(); */
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
                    /** enable_interrupts_pico(); */
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
    memcpy((void*)tknbuf, (void*)buf, STRINGSIZE);                                // restore the token buffer in case there are other commands in it
//    initConsole();
#ifdef USBKEYBOARD
	clearrepeat();
#endif
/// TODO: ensure
    /** enable_interrupts_pico(); */
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

uint8_t CombinedPtr::buff[CombinedPtrBufSize];
FSIZE_t CombinedPtr::buff_base_offset = (FSIZE_t)-1;

unsigned char CombinedPtr::operator*() {
    if (p.f >= XIP_BASE) { // in Flash or in RAM
        return *p.c;
    }
    // in file on SD card (less than 256 MB of space on file is supported)
    size_t offset = p.f % CombinedPtrBufSize;
    if (p.f / CombinedPtrBufSize == buff_base_offset / CombinedPtrBufSize) { // we are in buffer
        return buff[offset];
    }
    buff_base_offset = p.f & ~(FSIZE_t)(CombinedPtrBufSize - 1);
    SDBlock(buff_base_offset, buff, CombinedPtrBufSize);
    return buff[offset];
}

unsigned char CombinedPtr::operator[](std::ptrdiff_t i) {
    FSIZE_t pi = p.f + i;
    if (pi >= XIP_BASE) { // in Flash or in RAM
        return p.c[i];
    }
    // in file on SD card (less than 256 MB of space on file is supported)
    size_t offset = pi % CombinedPtrBufSize;
    if (pi / CombinedPtrBufSize == buff_base_offset / CombinedPtrBufSize) { // we are in buffer
        return buff[offset];
    }
    buff_base_offset = pi & ~(FSIZE_t)(CombinedPtrBufSize - 1);
    SDBlock(buff_base_offset, buff, CombinedPtrBufSize);
    return buff[offset];
}

void sd_range_erase(FSIZE_t offset, FSIZE_t size) {
    CombinedPtr::flush();
    SDErraseBlock(offset, size);
}
void sd_range_program(FSIZE_t offset, CombinedPtr p, FSIZE_t size) {
    CombinedPtr::flush();
    SDWriteBlockPP(offset, p, size);
}

double CombinedPtr::as_double() {
    if (p.f >= XIP_BASE)
        return *(double *)p.c;
    double res;
    for (int i = 0; i < 8; ++i)
        ((uint8_t*)&res)[i] = (*this)[i];  // считываем байты с учётом кеша и SD
    return res;
}

long long CombinedPtr::as_i64a() {
    if (p.f >= XIP_BASE)
        return *(long long *)p.c;
    long long res;
    for (int i = 0; i < 8; ++i)
        ((uint8_t*)&res)[i] = (*this)[i];  // считываем байты с учётом кеша и SD
    return res;
}

CombinedPtr& CombinedPtr::write_byte(uint8_t v) {
    if (p.f >= XIP_BASE) {
        *p.c = v;
        return *this;
    }
    // in file on SD card
    size_t offset = p.f % CombinedPtrBufSize;
    FSIZE_t block_base = p.f & ~(FSIZE_t)(CombinedPtrBufSize - 1);

    if (block_base != buff_base_offset) {
        if (buff_dirty && buff_base_offset != (FSIZE_t)-1) {
            SDWriteBlock(buff_base_offset, buff, CombinedPtrBufSize);
            buff_dirty = false;
        }
        SDBlock(block_base, buff, CombinedPtrBufSize);
        buff_base_offset = block_base;
    }
    buff[offset] = v;
    buff_dirty = true; // Помечаем буфер как изменённый
    return *this;
}
bool CombinedPtr::buff_dirty = false;
void CombinedPtr::flush() {
    if (buff_dirty && buff_base_offset != (FSIZE_t)-1) {
        SDWriteBlock(buff_base_offset, buff, CombinedPtrBufSize);
        buff_dirty = false;
    }
}

// print a string to the console interfaces
void MMPrintStringPP(CombinedPtr s) {
    while(*s) {
        if(s[1])MMputchar(*s,0);
        else MMputchar(*s,1);
        s++;
    }
    fflush(stdout);
}

/// \end:uart_advanced[]
