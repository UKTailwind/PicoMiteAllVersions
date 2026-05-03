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
#include "hal/hal_gui_controls.h"
#include "hal/hal_i2c_keypad.h"
#include "hal/hal_main_init.h"
#include "hal/hal_heartbeat.h"
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
/* start_vga_i2s is implemented in the VGA-PIO scanout driver on
 * pure-VGA ports; non-VGA ports never call it. The extern is harmless
 * to declare unconditionally — link-time dead-code-elimination drops
 * the symbol where unused. */
extern void start_vga_i2s(void);
/* COPYRIGHT text moved to Version.h as MMBASIC_COPYRIGHT; banner is
 * emitted via MMBasic_PrintBanner() in MMBasic_REPL.c. */

#ifndef rp2350
    #include "hardware/structs/ssi.h"
    #include "hardware/vreg.h"
#else
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
/* VGA-family scratch globals — used only inside the VGA / HDMI scanout
 * cores (drivers/vga_pio/, drivers/hdmi/). Defined unconditionally so
 * Hardware_Includes.h externs resolve on every port; LTO drops them
 * where unused. */
volatile uint8_t transparent=0;
volatile uint8_t transparents=0;
volatile int RGBtransparent=0;
int MODE1SIZE, MODE2SIZE, MODE3SIZE, MODE4SIZE, MODE5SIZE;
/* ProcessWeb / TelnetPutC / wifi_serial_telnet_configured are real
 * impls on WiFi ports and no-op stubs in MMweb_stubs.c elsewhere.
 * Declared unconditionally so PicoMite.c references link cleanly. */
extern void ProcessWeb(int mode);
extern void TelnetPutC(int c, int flush);
extern int  wifi_serial_telnet_configured(void);

/* Boot banner. HAL_PORT_DEVICE_NAME is set per port via CMake
 * target_compile_options ("PicoMite" / "PicoMiteVGA" / "PicoMiteHDMI" /
 * "WebMite" / "PicoMiteUSB" / etc.) so a single template covers every
 * port shape — including novel WiFi+display combinations that the old
 * per-target #define cascade couldn't represent without conflicts. */
#define MES_SIGNON  "\r" HAL_PORT_DEVICE_NAME " MMBasic " CHIP " Edition V" VERSION "\r\n"
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
/* QVGA scanout clock divider — used by the QVGA PIO state machine on
 * pure-VGA ports; HDMI ports use a different timing source. Defined
 * unconditionally; on HDMI ports the variable is unread. */
int QVGA_CLKDIV;
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

/* PinDef[] now lives in each port's pin_tables.c, composed from the
 * shared row-block macros in ports/pico_sdk_common/pindef_blocks.h.
 * Each port owns its own flat array — no preprocessor gates here. */
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
        /* SPI-LCD ports take the framebuffer mutex around the WAV
         * input poll because the merge pipeline writes the same
         * memory; no-op on non-merge-pipeline ports. */
        if (SPIatRisk) hal_display_merge_lock_fb();
        checkWAVinput();
        if (SPIatRisk) hal_display_merge_unlock_fb();
    }
    if(CurrentlyPlaying == P_MOD || CurrentlyPlaying==P_ARRAY ) checkWAVinput();
    if(++when & 7 && CurrentLinePtr) return;
    /* USB-host-keyboard ports drive tuh_task / hid_app_task here;
     * non-USB ports drain USB-CDC stdio characters into the console
     * ring buffer. */
    hal_keyboard_routinechecks_pump();
	if(Option.DISPLAY_TYPE>=NEXTGEN && !(low_x==silly_low && high_x==silly_high && low_y==silly_low && high_y==silly_high)){// Buffered LCD displays
        if(Option.Refresh){
            hal_display_nextgen_refresh_rect(low_x, low_y, high_x, high_y);
            low_x=silly_low; high_y=silly_high; low_y=silly_low; high_x=silly_high;
        }
	}
	if(GPSchannel)processgps();
    if(diskchecktimer == 0)CheckSDCard();
    hal_gui_controls_routine_check_touch();

//        if(tud_cdc_connected() && KeyCheck==0){
//            SSPrintString(alive);
//        }
    if(clocktimer==0 && Option.RTC){
        if(classicread==0 && nunchuckread==0){
            RtcGetTime(0);
        }
    }
    /* I²C keyboard polling moved into the PS/2 backend's
     * hal_keyboard_routinechecks_pump() — runs alongside the USB-CDC
     * console drain. USB-host-keyboard backend's pump only does
     * tuh_task / hid_app_task. */
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
    ProcessWeb(1);          /* stub no-op on non-WiFi (MMweb_stubs.c) */
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
    /* On WiFi ports the stdio (USB-CDC + UART) console only runs
     * when telnet is also configured (mirroring); on non-WiFi ports
     * the hook returns 1 so stdio always runs. TelnetPutC and
     * ProcessWeb are no-op stubs on non-WiFi. */
    if (wifi_serial_telnet_configured()) {
        hal_console_usb_cdc_putc(c, flush);
        if(Option.SerialConsole){
            int empty=uart_is_writable((Option.SerialConsole & 3)==1 ? uart0 : uart1);
            while(ConsoleTxBufTail == ((ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE));
            ConsoleTxBuf[ConsoleTxBufHead] = c;
            ConsoleTxBufHead = (ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE;
            if(empty){
                while(irqs){}
                uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, true, true);
                irq_set_pending((Option.SerialConsole & 3)==1 ? UART0_IRQ : UART1_IRQ);
            }
        }
    }
    TelnetPutC(c, flush);
    ProcessWeb(1);
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
int HAL_PORT_MMINKEY_DECL(MMInkey)(void) {
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
    /* hal_keyboard_service is a no-op on USB ports (TinyUSB pumps
     * itself); on PS/2 it runs CheckKeyboard. */
    if (c == -1) hal_keyboard_service();
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
    hal_i2c_keypad_periodic_scan((uint64_t)mSecTimer);
#endif
        AHRSTimer++;
        InkeyTimer++;                                                     // used to delay on an escape character
        PauseTimer++;													// used by the PAUSE command
        IntPauseTimer++;												// used by the PAUSE command inside an interrupt
        ds18b20Timer++;
		GPSTimer++;
        I2CTimer++;
        hal_keyboard_timer_tick();
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
    // GUI controls timer tick — touch pen-state machine + ClickTimer
    // countdown. No-op on stub ports.
    hal_gui_controls_timer_tick();
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
        hal_heartbeat_tick();      /* no-op on ports without an onboard LED */
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
    /* On WiFi ports a long busy-wait would starve the lwIP poll —
     * pump ProcessWeb() every 500us. On non-WiFi the stub is a
     * no-op so the inner branch reduces to the busy-wait below. */
    if (us < 500) {
        busy_wait_us(us);
    } else {
        uint64_t end = time_us_64() + us;
        while (time_us_64() < end) {
            if (time_us_64() % 500 == 0) ProcessWeb(1);
        }
    }
}
void __not_in_flash_func(CheckAbort)(void) {
    ProcessWeb(1);          /* stub no-op on non-WiFi */
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

#ifndef rp2350
void __no_inline_not_in_flash_func(modclock)(uint16_t speed){
       ssi_hw->ssienr=0;
       ssi_hw->baudr=0;
       ssi_hw->baudr=speed;
       ssi_hw->ssienr=1;
}
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

/* WebConnect body relocated to MMsetwifi.c (WiFi ports) +
 * MMweb_stubs.c (non-WiFi). Hardware_Includes.h declares the symbol
 * unconditionally so call sites stay clean. */

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
    port_video_validate_boot_options();
    m_alloc(M_PROG);                                           // init the variables for program memory
    LibMemory = (uint8_t *)flash_libmemory;
    uSec(100);
    if(_excep_code == RESET_CLOCKSPEED) {
        /* HAL_PORT_DEFAULT_CPU_SPEED_KHZ is set per port in
         * port_config.h (200 MHz for SPI-LCD, 252 MHz for pure VGA,
         * 315 MHz for HDMI HSTX). */
        Option.CPU_Speed = HAL_PORT_DEFAULT_CPU_SPEED_KHZ;
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
    set_sys_clock_khz(port_video_sys_clock_khz(Option.CPU_Speed), false);
#ifdef rp2350
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    sleep_ms(2);
#endif
    PWM_FREQ=44100;
    pico_get_unique_board_id_string (id_out,12);
#ifdef rp2350
    /* PSRAM init: psram_setup/psram_size have stubs in
     * drivers/psram_heap/psram_heap_stub.c on ports that don't link
     * psram.c (web_rp2350, vga_wifi_rp2350 — CYW43 owns the QSPI
     * pins). Option.PSRAM_CS_PIN is always 0 on those ports so the
     * stub call never fires. */
    if (Option.PSRAM_CS_PIN) {
        PSRAMpin = PinDef[Option.PSRAM_CS_PIN].GPno;
        psram_setup();
        if (!(PSRAMsize = psram_size())) {
            Option.PSRAM_CS_PIN = 0;
            SaveOptions();
        } else PSRAMsize -= 2 * 1024 * 1024;
    }
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
    hal_display_merge_init_fb_mutex();           /* SPI-LCD ports only */


#ifndef rp2350
    if(Option.CPU_Speed<=200000)modclock(2);
#else
    /* NEXTGEN displays are MEM332-family SPI-LCD with shadow
     * framebuffer; the runtime guard `Option.DISPLAY_TYPE >= NEXTGEN`
     * is dead on ports whose OPTION setter rejects those values. */
    if (Option.DISPLAY_TYPE >= NEXTGEN) {
        framebuffersize = display_details[Option.DISPLAY_TYPE].horizontal *
                          display_details[Option.DISPLAY_TYPE].vertical;
        heap_memory_size -= framebuffersize;
        FRAMEBUFFER = AllMemory + heap_memory_size + 256;
    }
#endif
    uSec(100);
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    _excep_code=excep;
    port_video_post_clock_init();
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
    ticks_per_second = Option.CPU_Speed*1000;
    // The serial clock won't vary from this point onward, so we can configure
    // the UART etc.
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
    /* USB-CDC stdio boot setup — runs the translate_crlf reset and
     * the 5-second host-attach wait on PS/2 ports; no-op on USB-host
     * ports (USB-A in host mode). */
    hal_console_usb_cdc_boot_init();
	InitBasic();
    /* Display + keypad init order. SPI-LCD ports run the SSD / SPI /
     * I²C / virtual display + touch helpers; on VGA family these are
     * stubs in spi_lcd_periph_io_stub.c. The PicoCalc keypad MCU
     * shares the I²C bus, so on that port the SSD/I²C display init
     * helpers are skipped (the keypad is busy on those addresses)
     * and a 300 ms settle delay runs after touch-init. Other SPI-LCD
     * boards run the standard sequence with no delay.
     *
     * SSD1963.h / Touch.h are gated in Hardware_Includes.h, so
     * declare InitDisplaySSD / InitTouch here unconditionally. */
    extern void InitDisplaySSD(void);
    extern void InitDisplaySPI(int InitOnly);
    extern void InitDisplayI2C(int InitOnly);
    extern void InitTouch(void);
    if (!hal_i2c_keypad_owns_i2c_bus()) InitDisplaySSD();
    InitDisplaySPI(0);
    if (!hal_i2c_keypad_owns_i2c_bus()) {
        InitDisplayI2C(0);
        InitDisplayVirtual();
    }
    InitTouch();
    hal_i2c_keypad_boot_init();
    if (Option.BackLightLevel) setBacklight(Option.BackLightLevel, 0);
    /* ErrorInPrompt is a static inside MMBasic_RunPromptLoop; initialized there. */
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION,sigbus);
    exception_set_exclusive_handler(SVCALL_EXCEPTION,sigbus);
    exception_set_exclusive_handler(PENDSV_EXCEPTION,sigbus);
    exception_set_exclusive_handler(NMI_EXCEPTION ,sigbus);
    exception_set_exclusive_handler(SYSTICK_EXCEPTION,sigbus);
    while((i=getConsole())!=-1){}
    
    /* core1 launch + post-launch display prep dispatched per port via
     * hal_main_init.h. */
    port_main_launch_core1();
        strcpy((char *)banner,MES_SIGNON);
#ifdef rp2350
        /* Stamp the package suffix (A/B) over the trailing space of
         * CHIP="RP2350 ". Independent of HAL_PORT_DEVICE_NAME length —
         * which is what the previous per-port position table was
         * compensating for. */
        {
            char *_pkg = strstr((char *)banner, "RP2350");
            if (_pkg) _pkg[6] = (rp2350a ? 'A' : 'B');
        }
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
    hal_keyboard_init();        /* USB: TinyUSB init; PS/2: initKeyboard */
    /* PS/2 ports also init mouse0 here. The hal_keyboard_init() PS/2
     * impl already calls initKeyboard(); add the mouse init alongside. */
    hal_keyboard_init_external_mouse();
#ifdef rp2350
    if(PSRAMsize){MMPrintString("Total of ");PInt(PSRAMsize/(1024*1024));MMPrintString(" Mbytes PSRAM available\r\n");}
#endif
    /* HAL_PORT_AUDIO_I2S_PIO_NUM is set per port in port_config.h. */
    start_i2s(HAL_PORT_AUDIO_I2S_PIO_NUM, 1);

   
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
    hal_keyboard_clear_repeat_state();          /* USB only — stub no-op */
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
    hal_keyboard_clear_repeat_state();         /* USB only — stub no-op */
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
