/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

configuration.h

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
#ifndef __CONFIGURATION_H
#define __CONFIGURATION_H
#ifdef __cplusplus
extern "C" {
#endif
/* HAL_PORT_HAS_* palette must be visible before the gates below. */
#include "port_config.h"

/* Stage-D alias layer (decascade plan). Heap, flash, CPU, MMBasic-table,
 * console, pin, and PSRAM values come from ports/<port>/port_config.h's
 * HAL_PORT_* set. Each port's port_config.h is the single source of
 * truth; configuration.h just renames them to the legacy macro names
 * core code already references (HEAP_MEMORY_SIZE, MAX_CPU, etc.).
 *
 * USB-keyboard variants pick the _USB sibling. Stage F is expected to
 * split USB ports into their own port directories, after which this
 * HAL_PORT_KEYBOARD_USB_HOST ifdef collapses too. */
#if HAL_PORT_KEYBOARD_USB_HOST
    #define FLASH_TARGET_OFFSET HAL_PORT_FLASH_TARGET_OFFSET_USB
    #define MagicKey            HAL_PORT_MAGIC_KEY_USB
    #define HEAPTOP             HAL_PORT_HEAP_TOP_USB
#else
    #define FLASH_TARGET_OFFSET HAL_PORT_FLASH_TARGET_OFFSET
    #define MagicKey            HAL_PORT_MAGIC_KEY
    #define HEAPTOP             HAL_PORT_HEAP_TOP
#endif

/* PICOMITE rp2350 PICOCALC trims heap by 4 KB to fit the VM alongside
 * the full interpreter — handled here so port_config.h stays ifdef-free. */
#if defined(PICOCALC) && defined(HAL_PORT_HEAP_MEMORY_SIZE_PICOCALC)
    #define HEAP_MEMORY_SIZE    HAL_PORT_HEAP_MEMORY_SIZE_PICOCALC
#else
    #define HEAP_MEMORY_SIZE    HAL_PORT_HEAP_MEMORY_SIZE
#endif

#define MAX_CPU     HAL_PORT_MAX_CPU
#define MIN_CPU     HAL_PORT_MIN_CPU
#define MAXVARS     HAL_PORT_MAX_VARS
#define MAXSUBFUN   HAL_PORT_MAX_SUBFUN
#ifdef HAL_PORT_MAX_MODES
    #define MAXMODES    HAL_PORT_MAX_MODES
#endif

/* WiFi ports include the lwIP options + a TCP control-block ceiling. */
#if HAL_PORT_HAS_WIFI
    #include "lwipopts_examples_common.h"
    #define MaxPcb 8
#endif

/* VGA-family display constants — mode-size tables and CPU-speed name
 * aliases. Compiled into VGA + HDMI ports only; gated on
 * HAL_PORT_IS_VGA which both ports set to 1. Keeps the VGA-specific
 * vocabulary out of non-VGA TUs. */
#if HAL_PORT_IS_VGA
    #define MODE_H_S_ACTIVE_PIXELS 640
    #define MODE_V_S_ACTIVE_LINES 480
    #define MODE1SIZE_S   MODE_H_S_ACTIVE_PIXELS     * MODE_V_S_ACTIVE_LINES /8
    #define MODE2SIZE_S  (MODE_H_S_ACTIVE_PIXELS/2) * (MODE_V_S_ACTIVE_LINES/2)/2
    #define MODE3SIZE_S  (MODE_H_S_ACTIVE_PIXELS)   * (MODE_V_S_ACTIVE_LINES)/2
    #define MODE4SIZE_S  (MODE_H_S_ACTIVE_PIXELS/2) * (MODE_V_S_ACTIVE_LINES/2)*2
    #define MODE5SIZE_S  (MODE_H_S_ACTIVE_PIXELS/2) * (MODE_V_S_ACTIVE_LINES/2)
    #define MODE_H_4_ACTIVE_PIXELS 720
    #define MODE_V_4_ACTIVE_LINES 400
    #define MODE1SIZE_4   MODE_H_4_ACTIVE_PIXELS     * MODE_V_4_ACTIVE_LINES /8
    #define MODE2SIZE_4  (MODE_H_4_ACTIVE_PIXELS/2) * (MODE_V_4_ACTIVE_LINES/2)/2
    #define MODE3SIZE_4  (MODE_H_4_ACTIVE_PIXELS)   * (MODE_V_4_ACTIVE_LINES)/2
    #define MODE4SIZE_4  (MODE_H_4_ACTIVE_PIXELS/2) * (MODE_V_4_ACTIVE_LINES/2)*2
    #define MODE5SIZE_4  (MODE_H_4_ACTIVE_PIXELS/2) * (MODE_V_4_ACTIVE_LINES/2)
    #define MODE_H_W_ACTIVE_PIXELS 1280
    #define MODE_V_W_ACTIVE_LINES 720
    #define MODE1SIZE_W  MODE_H_W_ACTIVE_PIXELS * MODE_V_W_ACTIVE_LINES /8
    #define MODE2SIZE_W  (MODE_H_W_ACTIVE_PIXELS/4) * (MODE_V_W_ACTIVE_LINES/4)/2
    #define MODE3SIZE_W  (MODE_H_W_ACTIVE_PIXELS/2) * (MODE_V_W_ACTIVE_LINES/2)/2
    #define MODE5SIZE_W  (MODE_H_W_ACTIVE_PIXELS/4) * (MODE_V_W_ACTIVE_LINES/4)
    #define MODE_H_8_ACTIVE_PIXELS 848
    #define MODE_V_8_ACTIVE_LINES 480
    #define MODE1SIZE_8   MODE_H_8_ACTIVE_PIXELS     * MODE_V_8_ACTIVE_LINES /8
    #define MODE2SIZE_8  (MODE_H_8_ACTIVE_PIXELS/2) * (MODE_V_8_ACTIVE_LINES/2)/2
    #define MODE3SIZE_8  (MODE_H_8_ACTIVE_PIXELS)   * (MODE_V_8_ACTIVE_LINES)/2
    #define MODE5SIZE_8  (MODE_H_8_ACTIVE_PIXELS/2) * (MODE_V_8_ACTIVE_LINES/2)
    #define MODE_H_L_ACTIVE_PIXELS 1024
    #define MODE_V_L_ACTIVE_LINES 768
    #define MODE1SIZE_L  MODE_H_L_ACTIVE_PIXELS * MODE_V_L_ACTIVE_LINES /8
    #define MODE2SIZE_L  (MODE_H_L_ACTIVE_PIXELS/4) * (MODE_V_L_ACTIVE_LINES/4)/2
    #define MODE3SIZE_L  (MODE_H_L_ACTIVE_PIXELS/2) * (MODE_V_L_ACTIVE_LINES/2)/2
    #define MODE5SIZE_L  (MODE_H_L_ACTIVE_PIXELS/4) * (MODE_V_L_ACTIVE_LINES/4)
    #define MODE_H_V_ACTIVE_PIXELS 800
    #define MODE_V_V_ACTIVE_LINES 600
    #define MODE1SIZE_V  MODE_H_V_ACTIVE_PIXELS * MODE_V_V_ACTIVE_LINES /8
    #define MODE2SIZE_V  (MODE_H_V_ACTIVE_PIXELS/2) * (MODE_V_V_ACTIVE_LINES/2)/2
    #define MODE3SIZE_V  (MODE_H_V_ACTIVE_PIXELS) * (MODE_V_V_ACTIVE_LINES)/2
    #define MODE5SIZE_V  (MODE_H_V_ACTIVE_PIXELS/2) * (MODE_V_V_ACTIVE_LINES/2)
    #define MODE_H_X_ACTIVE_PIXELS 1024
    #define MODE_V_X_ACTIVE_LINES 600
    #define MODE1SIZE_X  MODE_H_X_ACTIVE_PIXELS * MODE_V_X_ACTIVE_LINES /8
    #define MODE2SIZE_X  (MODE_H_X_ACTIVE_PIXELS/4) * (MODE_V_X_ACTIVE_LINES/4)/2
    #define MODE3SIZE_X  (MODE_H_X_ACTIVE_PIXELS/2) * (MODE_V_X_ACTIVE_LINES/2)/2
    #define MODE5SIZE_X  (MODE_H_X_ACTIVE_PIXELS/4) * (MODE_V_X_ACTIVE_LINES/4)
    #define MODE_H_Y_ACTIVE_PIXELS 800
    #define MODE_V_Y_ACTIVE_LINES 480
    #define MODE1SIZE_Y  MODE_H_Y_ACTIVE_PIXELS * MODE_V_Y_ACTIVE_LINES /8
    #define MODE2SIZE_Y  (MODE_H_Y_ACTIVE_PIXELS/2) * (MODE_V_Y_ACTIVE_LINES/2)/2
    #define MODE3SIZE_Y  (MODE_H_Y_ACTIVE_PIXELS) * (MODE_V_Y_ACTIVE_LINES)/2
    #define MODE5SIZE_Y  (MODE_H_Y_ACTIVE_PIXELS/2) * (MODE_V_Y_ACTIVE_LINES/2)
    #define Freq720P 372000
    #define Freq480P 315000
    #define Freq252P 252000
    #define Freq378P 378000
    #define FreqXGA  375000
    #define FreqSVGA 360000
    #define Freq848  336000
    #define Freq400  283200
    #define FreqY    333000
    #define FreqX    250000
    #define FullColour (Option.CPU_Speed ==Freq252P || Option.CPU_Speed==Freq378P || Option.CPU_Speed ==Freq480P || Option.CPU_Speed ==Freq400)
    #define MediumRes (Option.CPU_Speed==FreqSVGA || Option.CPU_Speed==Freq848 || Option.CPU_Speed==FreqY || Option.CPU_Speed==FreqX)
#endif

#define MMFLOAT double
#define FLOAT3D float
#define sqrt3d sqrtf
#define round3d roundf
#define fabs3d fabsf
/* MAX_PROG_SIZE defaults to the heap ceiling. The WASM port sets it
 * to a fixed 512 KB in port_config.h to keep flash_prog_buf and the
 * flash-slot mirror in host_fs_shims.c from ballooning into ~40 MB
 * of static data. Programs larger than 512 KB aren't expected in
 * practice; the MEMORY command and TryGetMemory read heap_memory_size
 * (runtime) instead. */
#ifndef MAX_PROG_SIZE
#define MAX_PROG_SIZE HEAP_MEMORY_SIZE
#endif
#define SAVEDVARS_FLASH_SIZE 16384
#define FLASH_ERASE_SIZE 4096
#define MAXFLASHSLOTS 3
#define MAXRAMSLOTS 5
#define MAXVARHASH				MAXVARS/2


// more static memory allocations (less important)
#define MAXFORLOOPS         20                      // each entry uses 17 bytes
#define MAXDOLOOPS          20                      // each entry uses 12 bytes
#define MAXGOSUB            50                     // each entry uses 4 bytes
#define MAX_MULTILINE_IF    20                      // each entry uses 8 bytes
#define MAXTEMPSTRINGS      64                      // each entry takes up 4 bytes
#define MAXSUBHASH          MAXSUBFUN
// operating characteristics
#define MAXVARLEN           32                      // maximum length of a variable name
#define MAXSTRLEN           255                     // maximum length of a string
#define STRINGSIZE          256                     // must be 1 more than MAXSTRLEN.  2 of these buffers are staticaly created
#define MAXOPENFILES        10                      // maximum number of open files
#ifdef rp2350
#define MAXDIM              5                       // maximum nbr of dimensions to an array
#define PSRAMCSPIN PSRAMpin
extern uint8_t PSRAMpin;
#else
#define MAXDIM              6                       // maximum nbr of dimensions to an array
#endif
/* CONSOLE_RX_BUF_SIZE: WiFi ports size the console RX buffer to a single
 * lwIP TCP segment so character flow into the BASIC parser matches the
 * network MTU. Non-WiFi ports use a 256-byte ring; HAL_PORT_CONSOLE_RX_BUF_SIZE
 * is undefined on WiFi ports (the macro is gated below). */
#if HAL_PORT_HAS_WIFI
#define CONSOLE_RX_BUF_SIZE TCP_MSS
#else
#define CONSOLE_RX_BUF_SIZE HAL_PORT_CONSOLE_RX_BUF_SIZE
#endif
#define CONSOLE_TX_BUF_SIZE 256
#define MAXOPENFILES  10
#define MAXCOMPORTS 2
#define MAXERRMSG           64                      // max error msg size (MM.ErrMsg$ is truncated to this)
#define MAXSOUNDS           4
#define MAXKEYLEN           64 
#define MAXPID 8

// TYPE / STRUCT limits (ported from UKTailwind 6.02).
#define MAX_STRUCT_TYPES    32                  // max distinct TYPE definitions per program
#define MAX_STRUCT_MEMBERS  16                  // max members per TYPE
#define MAX_STRUCT_NEST_DEPTH 8                 // max nesting depth of TYPE-of-TYPE
// define the maximum number of arguments to PRINT, INPUT, WRITE, ON, DIM, ERASE, DATA and READ
// each entry uses zero bytes.  The number is limited by the length of a command line
#define MAX_ARG_COUNT       75
#define STR_AUTO_PRECISION  999 
#define STR_FLOAT_PRECISION  998 
#define STR_SIG_DIGITS 9                            // number of significant digits to use when converting MMFLOAT to a string
#define STR_FLOAT_DIGITS 6                            // number of significant digits to use when converting MMFLOAT to a string
#define NBRSETTICKS         4                       // the number of SETTICK interrupts available
#define PIOMAX  HAL_PORT_PIOMAX
#define NBRPINS HAL_PORT_NBR_PINS

/* QSPI PSRAM region. `PSRAMbase` is a runtime extern (declared in
 * Hardware_Includes.h, defined in PicoMite.c or each non-Pico port's
 * peripheral stubs) set by hal_psram_init() at boot — 0x11000000 on
 * RP2350 with QSPI PSRAM detected, the heap_caps_aligned_alloc() return
 * value on ESP32-S3, 0 elsewhere. HAL_PORT_PSRAM_BLOCK_SIZE stays a
 * compile-time constant because it sizes the slot region used by the
 * `RAM` command independently of the runtime allocator. */
#ifdef HAL_PORT_PSRAM_BLOCK_SIZE
    #define PSRAMblocksize HAL_PORT_PSRAM_BLOCK_SIZE
#else
    /* Ports without a PSRAM slot region (RP2040, host, ESP32 pre-Phase-4)
     * still need a compilable expression so shared/cmd_psram.c links; the
     * runtime guard `if (!PSRAMsize) error("PSRAM not enabled");` keeps the
     * slot pointer from ever being dereferenced. */
    #define PSRAMblocksize 0
#endif
#define PSRAMblock (PSRAMbase + PSRAMsize + 0x60000)
#define MAXPROMPTLEN        49                      // max length of a prompt incl the terminating null
#define BREAK_KEY           3                       // the default value (CTRL-C) for the break key.  Reset at the command prompt.
#define FNV_prime           16777619
#define FNV_offset_basis    2166136261
#define use_hash
#define DISKCHECKRATE       500                    //check for removal of SDcard every 200mSec
#define EDIT_BUFFER_SIZE    heap_memory_size-3072-3*HRes// this is the maximum RAM that we can get
#define SCREENWIDTH     80
#define SCREENHEIGHT    24                          // this is the default and it can be changed using the OPTION command
#define CONSOLE_BAUDRATE        115200               // only applies to the serial console
#define MAXCFUNCTION	20
#define SAVEDVARS_FLASH_SIZE 16384
#define FLASH_ERASE_SIZE 4096
#define MAX3D   8 
#define MAXCAM  3
#define MAX_POLYGON_VERTICES 10
#ifdef rp2350
    #define FreqDefault 150000
#else
    #define FreqDefault 200000
#endif
#define MAXBLITBUF 64
#define MAXRESTORE          8
#define CONFIG_TITLE		0
#define CONFIG_LOWER		1
#define CONFIG_UPPER		2 
#define UNUSED       (1 << 0)
#define ANALOG_IN    (1 << 1)
#define DIGITAL_IN   (1 << 2)
#define DIGITAL_OUT   (1 << 3)
#define UART1TX     (1 << 4)
#define UART1RX     (1 << 5)
#define UART0TX     (1 << 6)
#define UART0RX     (1 << 7)
#define I2C0SDA     (1 << 8)
#define I2C0SCL     (1 << 9)
#define I2C1SDA     (1 << 10)
#define I2C1SCL     (1 << 11)
#define SPI0RX     (1 << 12)
#define SPI0TX     (1 << 13)
#define SPI0SCK     (1 << 14)
#define SPI1RX     (1 << 15)
#define SPI1TX     (1 << 16)
#define SPI1SCK     (1 << 17)
#define PWM0A     (1 << 18)
#define PWM0B     (1 << 19)
#define PWM1A     (1 << 20)
#define PWM1B     (1 << 21)
#define PWM2A     (1 << 22)
#define PWM2B     (1 << 23)
#define PWM3A     (1 << 24)
#define PWM3B     (1 << 25)
#define PWM4A     (1 << 26)
#define PWM4B     (1 << 27)
#define PWM5A     (1 << 28)
#define PWM5B     (1 << 29)
#define PWM6A     (1 << 30)
#define PWM6B     2147483648
#define PWM7A     4294967296
#define PWM7B     8589934592
/* RP2350-only PWM slices 8..11 and fast-timer pin. Defined on every target
 * so the PinDef[].mode bitfield masks are valid to reference in portable
 * code; the actual bits are never set on RP2040 (no PinDef entry there
 * carries a PWM8A..PWM11B capability), so the checks that test them just
 * fall through to the "Invalid configuration" error arm. */
#define PWM8A     17179869184
#define PWM8B     34359738368
#define PWM9A     68719476736
#define PWM9B     137438953472
#define PWM10A    274877906944
#define PWM10B    549755813888
#define PWM11A    1099511627776
#define PWM11B    2199023255552
#define FAST_TIMER 4398046511104
#define FAST_TIMER_PIN 2
#define MAXCOLLISIONS 4
#define MAXLAYER   4
#define MAXCONTROLS 200
#define MAXDEFINES  16
#define silly_low 2000
#define silly_high -1
//#define DO_NOT_RESET (1 << 5)
//#define HEARTBEAT    (1 << 6)
#define HEARTBEATpin  Option.heartbeatpin
#define PATH_MAX 1024
// QVGA PIO and state machines
#define QVGA_PIO_NUM 0	
#ifdef rp2350
#define QVGA_PIO (QVGA_PIO_NUM==0 ? pio0: (QVGA_PIO_NUM==1 ? pio1: pio2))
#else
#define QVGA_PIO (QVGA_PIO_NUM==0 ?  pio0: pio1)
#endif
/* ScreenBuffer is an alias for FRAMEBUFFER. On rp2350 PICOMITE it
 * points at the NEXTGEN MEM332 shadow buffer; on every other target
 * FRAMEBUFFER is either NULL or the VGA/HDMI scanout buffer. Defining
 * unconditionally lets core code reference ScreenBuffer without a
 * target gate; branches that actually dereference it are guarded by
 * their own runtime DISPLAY_TYPE check. */
#define ScreenBuffer FRAMEBUFFER
// QVGA PIO
#define QVGA_SM		0	// QVGA state machine
#define QVGA_I2S_SM 1   //I2S state machine when running VGA
#define MIPS16 __attribute__ ((optimize("-Os")))
#define MIPS32 __attribute__ ((optimize("-O2")))
#define MIPS64 __attribute__ ((optimize("-O3")))
// QVGA DMA channel
#define QVGA_DMA_CB	0	// DMA control block of base layer
#define QVGA_DMA_PIO	1	// DMA copy data to PIO (raises IRQ0 on quiet)
#define ADC_DMA 2
#define ADC_DMA2 7
#define PIO_RX_DMA 8
#define PIO_TX_DMA 4
#define PIO_RX_DMA2 9
#define PIO_TX_DMA2 6
#define LOCALKEYSCANRATE 10
#define ADC_CLK_SPEED   (Option.CPU_Speed*500)
#define PROGSTART (FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + ((MAXFLASHSLOTS) * MAX_PROG_SIZE))
#define TOP_OF_SYSTEM_FLASH  (FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + ((MAXFLASHSLOTS+1) * MAX_PROG_SIZE))
#define RoundUpK4(a)     (((a) + (4096 - 1)) & (~(4096 - 1)))// round up to the nearest page size      [position 131:9]	
/* MM.* overlaid-function enum. All entries are unconditional so the
 * enum layout is identical across targets; case bodies in fun_tilde
 * that are hardware- or port-specific dispatch through port hooks or
 * runtime Option checks, and harmless default values (empty string,
 * -1.0) fall out on targets that don't own the underlying feature. */
typedef enum {
    MMHRES,
    MMVRES,
    MMVER,
    MMI2C,
	MMFONTHEIGHT,
	MMFONTWIDTH,
	MMPS2,
	MMHPOS,
	MMVPOS,
	MMONEWIRE,
    MMERRNO,
    MMERRMSG,
	MMWATCHDOG,
	MMDEVICE,
	MMCMDLINE,
	MMMESSAGE,
    MMADDRESS,
    MMTOPIC,
    MMFLAG,
    MMDISPLAY,
    MMWIDTH,
    MMHEIGHT,
    MMPERSISTENT,
    MMSUPPLY,
    MMEND
} Operation;
extern const char* overlaid_functions[];
#ifdef __cplusplus
}
#endif
#endif /* __CONFIGURATION_H */
/*  @endcond */
