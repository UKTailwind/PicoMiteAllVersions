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
#include "runtime/runtime.h"
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
/* PSRAMbase is the runtime base address of the PSRAM region; set by
 * hal_psram_init() (pico_boot.c on RP2350, 0 on RP2040). Stays 0 on
 * targets without PSRAM — the `if (PSRAMsize)` guards keep the
 * dereferences out of reach there. */
uintptr_t PSRAMbase = 0;
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
#define MES_SIGNON  "\r" HAL_PORT_DEVICE_NAME " MMBasic Anywhere v" VERSION "\r\n"
#define KEYCHECKTIME 16
int ListCnt;
int MMCharPos;
/* MMPromptPos, lastcmd[], InsertLastcmd, EditInputLine → MMBasic_Prompt.c */
int busfault=0;
int ExitMMBasicFlag = false;
volatile int MMAbort = false;
unsigned int _excep_peek;
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
volatile int64_t mSecTimer = 0;                               // this is used to count mSec
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
/* PinDef[] now lives in each port's pin_tables.c, composed from the
 * shared row-block macros in ports/pico_sdk_common/pindef_blocks.h.
 * Each port owns its own flat array — no preprocessor gates here. */
char alive[]="\033[?25h";
const char DaysInMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
/* commandtbl_decode moved to MMBasic_REPL.c. */
char banner[64];



#ifdef __cplusplus
}
#endif

/// \end:uart_advanced[]
