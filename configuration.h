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

#ifndef __CONFIGURATION_H
#define __CONFIGURATION_H

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================================
 * Platform-specific configuration - PICOMITEVGA
 * ============================================================================ */
#ifdef PICOMITEVGA

/* RP2350 configuration */
#ifdef rp2350
#define MAXSUBFUN 512
#define MAXVARS 768

#ifdef USBKEYBOARD
#define HEAP_MEMORY_SIZE (184 * 1024)
#else
#define HEAP_MEMORY_SIZE (184 * 1024)
#endif

#define FLASH_TARGET_OFFSET (880 * 1024)

/* HDMI-specific settings */
#ifdef HDMI
#define MAXMODES 5
#ifdef USBKEYBOARD
#define MagicKey 0x84223124
#else
#define MagicKey 0x9687B2A0
#endif
#define MAX_CPU Freq378P
#define MIN_CPU FreqX
#else
#define MAXMODES 3
#ifdef USBKEYBOARD
#define MagicKey 0x82115904
#else
#define MagicKey 0x84005FAF
#endif
#define MAX_CPU 378000
#define MIN_CPU 252000
#endif

/* Non-RP2350 configuration */
#else
#define MAXSUBFUN 256

#ifdef USBKEYBOARD
#define FLASH_TARGET_OFFSET (880 * 1024)
#define MagicKey 0x4776A715
#define MAXVARS 480
#else
#define FLASH_TARGET_OFFSET (864 * 1024)
#define MagicKey 0xA2349A2F
#define MAXVARS 480
#endif

#define MAXMODES 2
#define HEAP_MEMORY_SIZE (100 * 1024)
#define MAX_CPU 378000
#define MIN_CPU 252000
#endif

/* VGA display mode definitions - Standard (640x480) */
#define MODE_H_S_ACTIVE_PIXELS 640
#define MODE_V_S_ACTIVE_LINES 480
#define MODE1SIZE_S (MODE_H_S_ACTIVE_PIXELS * MODE_V_S_ACTIVE_LINES / 8)
#define MODE2SIZE_S ((MODE_H_S_ACTIVE_PIXELS / 2) * (MODE_V_S_ACTIVE_LINES / 2) / 2)
#define MODE3SIZE_S ((MODE_H_S_ACTIVE_PIXELS) * (MODE_V_S_ACTIVE_LINES) / 2)
#define MODE4SIZE_S ((MODE_H_S_ACTIVE_PIXELS / 2) * (MODE_V_S_ACTIVE_LINES / 2) * 2)
#define MODE5SIZE_S ((MODE_H_S_ACTIVE_PIXELS / 2) * (MODE_V_S_ACTIVE_LINES / 2))

/* VGA display mode definitions - 720x400 */
#define MODE_H_4_ACTIVE_PIXELS 720
#define MODE_V_4_ACTIVE_LINES 400
#define MODE1SIZE_4 (MODE_H_4_ACTIVE_PIXELS * MODE_V_4_ACTIVE_LINES / 8)
#define MODE2SIZE_4 ((MODE_H_4_ACTIVE_PIXELS / 2) * (MODE_V_4_ACTIVE_LINES / 2) / 2)
#define MODE3SIZE_4 ((MODE_H_4_ACTIVE_PIXELS) * (MODE_V_4_ACTIVE_LINES) / 2)
#define MODE4SIZE_4 ((MODE_H_4_ACTIVE_PIXELS / 2) * (MODE_V_4_ACTIVE_LINES / 2) * 2)
#define MODE5SIZE_4 ((MODE_H_4_ACTIVE_PIXELS / 2) * (MODE_V_4_ACTIVE_LINES / 2))

/* VGA display mode definitions - 1280x720 */
#define MODE_H_W_ACTIVE_PIXELS 1280
#define MODE_V_W_ACTIVE_LINES 720
#define MODE1SIZE_W (MODE_H_W_ACTIVE_PIXELS * MODE_V_W_ACTIVE_LINES / 8)
#define MODE2SIZE_W ((MODE_H_W_ACTIVE_PIXELS / 4) * (MODE_V_W_ACTIVE_LINES / 4) / 2)
#define MODE3SIZE_W ((MODE_H_W_ACTIVE_PIXELS / 2) * (MODE_V_W_ACTIVE_LINES / 2) / 2)
#define MODE5SIZE_W ((MODE_H_W_ACTIVE_PIXELS / 4) * (MODE_V_W_ACTIVE_LINES / 4))

/* VGA display mode definitions - 848x480 */
#define MODE_H_8_ACTIVE_PIXELS 848
#define MODE_V_8_ACTIVE_LINES 480
#define MODE1SIZE_8 (MODE_H_8_ACTIVE_PIXELS * MODE_V_8_ACTIVE_LINES / 8)
#define MODE2SIZE_8 ((MODE_H_8_ACTIVE_PIXELS / 2) * (MODE_V_8_ACTIVE_LINES / 2) / 2)
#define MODE3SIZE_8 ((MODE_H_8_ACTIVE_PIXELS) * (MODE_V_8_ACTIVE_LINES) / 2)
#define MODE5SIZE_8 ((MODE_H_8_ACTIVE_PIXELS / 2) * (MODE_V_8_ACTIVE_LINES / 2))

/* VGA display mode definitions - 1024x768 (XGA) */
#define MODE_H_L_ACTIVE_PIXELS 1024
#define MODE_V_L_ACTIVE_LINES 768
#define MODE1SIZE_L (MODE_H_L_ACTIVE_PIXELS * MODE_V_L_ACTIVE_LINES / 8)
#define MODE2SIZE_L ((MODE_H_L_ACTIVE_PIXELS / 4) * (MODE_V_L_ACTIVE_LINES / 4) / 2)
#define MODE3SIZE_L ((MODE_H_L_ACTIVE_PIXELS / 2) * (MODE_V_L_ACTIVE_LINES / 2) / 2)
#define MODE5SIZE_L ((MODE_H_L_ACTIVE_PIXELS / 4) * (MODE_V_L_ACTIVE_LINES / 4))

/* VGA display mode definitions - 800x600 (SVGA) */
#define MODE_H_V_ACTIVE_PIXELS 800
#define MODE_V_V_ACTIVE_LINES 600
#define MODE1SIZE_V (MODE_H_V_ACTIVE_PIXELS * MODE_V_V_ACTIVE_LINES / 8)
#define MODE2SIZE_V ((MODE_H_V_ACTIVE_PIXELS / 2) * (MODE_V_V_ACTIVE_LINES / 2) / 2)
#define MODE3SIZE_V ((MODE_H_V_ACTIVE_PIXELS) * (MODE_V_V_ACTIVE_LINES) / 2)
#define MODE5SIZE_V ((MODE_H_V_ACTIVE_PIXELS / 2) * (MODE_V_V_ACTIVE_LINES / 2))

/* VGA display mode definitions - 1024x600 */
#define MODE_H_X_ACTIVE_PIXELS 1024
#define MODE_V_X_ACTIVE_LINES 600
#define MODE1SIZE_X (MODE_H_X_ACTIVE_PIXELS * MODE_V_X_ACTIVE_LINES / 8)
#define MODE2SIZE_X ((MODE_H_X_ACTIVE_PIXELS / 4) * (MODE_V_X_ACTIVE_LINES / 4) / 2)
#define MODE3SIZE_X ((MODE_H_X_ACTIVE_PIXELS / 2) * (MODE_V_X_ACTIVE_LINES / 2) / 2)
#define MODE5SIZE_X ((MODE_H_X_ACTIVE_PIXELS / 4) * (MODE_V_X_ACTIVE_LINES / 4))

/* VGA display mode definitions - 800x480 */
#define MODE_H_Y_ACTIVE_PIXELS 800
#define MODE_V_Y_ACTIVE_LINES 480
#define MODE1SIZE_Y (MODE_H_Y_ACTIVE_PIXELS * MODE_V_Y_ACTIVE_LINES / 8)
#define MODE2SIZE_Y ((MODE_H_Y_ACTIVE_PIXELS / 2) * (MODE_V_Y_ACTIVE_LINES / 2) / 2)
#define MODE3SIZE_Y ((MODE_H_Y_ACTIVE_PIXELS) * (MODE_V_Y_ACTIVE_LINES) / 2)
#define MODE5SIZE_Y ((MODE_H_Y_ACTIVE_PIXELS / 2) * (MODE_V_Y_ACTIVE_LINES / 2))

/* CPU frequency definitions */
#define Freq720P 372000
#define Freq480P 315000
#define Freq252P 252000
#define Freq378P 378000
#define FreqXGA 375000
#define FreqSVGA 360000
#define Freq848 336000
#define Freq400 283200
#define FreqY 333000
#define FreqX 250000

/* Display capability macros */
#define FullColour (Option.CPU_Speed == Freq252P || Option.CPU_Speed == Freq378P || \
                    Option.CPU_Speed == Freq480P || Option.CPU_Speed == Freq400)
#define MediumRes (Option.CPU_Speed == FreqSVGA || Option.CPU_Speed == Freq848 || \
                   Option.CPU_Speed == FreqY || Option.CPU_Speed == FreqX)

#endif /* PICOMITEVGA */

/* ============================================================================
 * Platform-specific configuration - PICOMITEWEB
 * ============================================================================ */
#ifdef PICOMITEWEB

#ifdef rp2350
#define MAXSUBFUN 512
#define MAXVARS 768
#define HEAP_MEMORY_SIZE (208 * 1024)
#else
#define MAXSUBFUN 256
#define MAXVARS 480
#define HEAP_MEMORY_SIZE (88 * 1024)
#endif

#include "lwipopts_examples_common.h"

#define FLASH_TARGET_OFFSET (1200 * 1024)
#define MagicKey 0x53472B1C
#define MaxPcb 8
#define MAX_CPU 252000
#define MIN_CPU 126000

#endif /* PICOMITEWEB */

/* ============================================================================
 * Platform-specific configuration - PICOMITE
 * ============================================================================ */
#ifdef PICOMITE

#define MIN_CPU 48000

#ifdef rp2350
#define HEAP_MEMORY_SIZE (308 * 1024)
#define MAXVARS 768
#define FLASH_TARGET_OFFSET (912 * 1024)
#define MAX_CPU 396000
#define MAXSUBFUN 512

#ifdef USBKEYBOARD
#define MagicKey 0xD27F4F27
#else
#define MagicKey 0x182084D7
#endif
#else
#define HEAP_MEMORY_SIZE (124 * 1024)
#define MAXVARS 512
#define FLASH_TARGET_OFFSET (864 * 1024)
#define MAX_CPU 420000
#define MAXSUBFUN 256

#ifdef USBKEYBOARD
#define MagicKey 0x6110519E
#else
#define MagicKey 0xE0799B93
#endif
#endif

#endif /* PICOMITE */

/* ============================================================================
 * Type definitions - Float types
 * ============================================================================ */
#define MMFLOAT double
#define FLOAT3D float
#define sqrt3d sqrtf
#define round3d roundf
#define fabs3d fabsf

/* ============================================================================
 * Memory configuration
 * ============================================================================ */
#define MAX_PROG_SIZE HEAP_MEMORY_SIZE
#define SAVEDVARS_FLASH_SIZE 16384
#define FLASH_ERASE_SIZE 4096
#define MAXFLASHSLOTS 3
#define MAXRAMSLOTS 5
#define MAXVARHASH (MAXVARS / 2)

/* ============================================================================
 * Static memory allocations
 * ============================================================================ */
#define MAXFORLOOPS 20      // Each entry uses 17 bytes
#define MAXDOLOOPS 20       // Each entry uses 12 bytes
#define MAXGOSUB 50         // Each entry uses 4 bytes
#define MAX_MULTILINE_IF 20 // Each entry uses 8 bytes
#define MAXTEMPSTRINGS 64   // Each entry takes up 4 bytes
#define MAXSUBHASH MAXSUBFUN

/* ============================================================================
 * Operating characteristics - Strings and variables
 * ============================================================================ */
#define MAXVARLEN 32   // Maximum length of a variable name
#define MAXSTRLEN 255  // Maximum length of a string
#define STRINGSIZE 256 // Must be 1 more than MAXSTRLEN

/* ============================================================================
 * Operating characteristics - Files and I/O
 * ============================================================================ */
#define MAXOPENFILES 10 // Maximum number of open files
#define MAXCOMPORTS 2   // Maximum number of COM ports

#ifdef rp2350
#define MAXDIM 5 // Maximum number of dimensions to an array
#define PSRAMCSPIN PSRAMpin
    extern uint8_t PSRAMpin;
#else
#define MAXDIM 6 // Maximum number of dimensions to an array
#endif

/* Console buffer sizes */
#ifdef PICOMITEWEB
#define CONSOLE_RX_BUF_SIZE TCP_MSS
#else
#define CONSOLE_RX_BUF_SIZE 2280
#endif
#define CONSOLE_TX_BUF_SIZE 256

/* ============================================================================
 * Operating characteristics - Limits and maximums
 * ============================================================================ */
#define MAXERRMSG 64            // Max error msg size (MM.ErrMsg$ is truncated to this)
#define MAXSOUNDS 4             // Maximum simultaneous sounds
#define MAXKEYLEN 64            // Maximum key length
#define MAXPID 8                // Maximum PIDs
#define MAX_ARG_COUNT 75        // Max arguments to PRINT, INPUT, WRITE, ON, DIM, ERASE, DATA, READ
#define MAXCFUNCTION 20         // Maximum C functions
#define MAX3D 8                 // Maximum 3D objects
#define MAXCAM 3                // Maximum cameras
#define MAX_POLYGON_VERTICES 10 // Maximum vertices in a polygon
#define MAXBLITBUF 64           // Maximum blit buffers
#define MAXRESTORE 8            // Maximum restore points
#define MAXCOLLISIONS 4         // Maximum collision checks
#define MAXLAYER 4              // Maximum layers
#define MAXCONTROLS 200         // Maximum GUI controls
#define MAXDEFINES 16           // Maximum defines

/* ============================================================================
 * Operating characteristics - Number formatting
 * ============================================================================ */
#define STR_AUTO_PRECISION 999  // Auto precision for numbers
#define STR_FLOAT_PRECISION 998 // Float precision indicator
#define STR_SIG_DIGITS 9        // Significant digits when converting MMFLOAT to string
#define STR_FLOAT_DIGITS 6      // Float digits when converting MMFLOAT to string

/* ============================================================================
 * Operating characteristics - Hardware
 * ============================================================================ */
#define NBRSETTICKS 4 // Number of SETTICK interrupts available

#ifndef PICOMITEWEB
#ifdef rp2350
#define PIOMAX 3
#define NBRPINS 62
#define PSRAMbase 0x11000000
#define PSRAMblock (PSRAMbase + PSRAMsize + 0x60000)
#define PSRAMblocksize 0x1C0000
#else
#define PIOMAX 2
#define NBRPINS 44
#endif
#else
#ifdef rp2350
#define PIOMAX 3
#else
#define PIOMAX 2
#endif
#define NBRPINS 40
#endif

/* ============================================================================
 * Operating characteristics - Display and console
 * ============================================================================ */
#define MAXPROMPTLEN 49         // Max length of a prompt including terminating null
#define SCREENWIDTH 80          // Default screen width
#define SCREENHEIGHT 24         // Default screen height (can be changed with OPTION)
#define CONSOLE_BAUDRATE 115200 // Serial console baud rate

/* ============================================================================
 * Operating characteristics - Miscellaneous
 * ============================================================================ */
#define BREAK_KEY 3 // Default value (CTRL-C) for the break key
#define FNV_prime 16777619
#define FNV_offset_basis 2166136261
#define DISKCHECKRATE 500 // Check for SD card removal every 500ms
#define EDIT_BUFFER_SIZE (heap_memory_size - 3072 - 3 * HRes)

#ifdef rp2350
#define FreqDefault 150000
#define FAST_TIMER_PIN 2
#else
#define FreqDefault 200000
#endif

/* ============================================================================
 * Operating characteristics - Display configuration
 * ============================================================================ */
#define CONFIG_TITLE 0
#define CONFIG_LOWER 1
#define CONFIG_UPPER 2

#define silly_low 2000
#define silly_high -1

/* ============================================================================
 * Pin capability flags (bit flags)
 * ============================================================================ */
#define UNUSED (1 << 0)
#define ANALOG_IN (1 << 1)
#define DIGITAL_IN (1 << 2)
#define DIGITAL_OUT (1 << 3)
#define UART1TX (1 << 4)
#define UART1RX (1 << 5)
#define UART0TX (1 << 6)
#define UART0RX (1 << 7)
#define I2C0SDA (1 << 8)
#define I2C0SCL (1 << 9)
#define I2C1SDA (1 << 10)
#define I2C1SCL (1 << 11)
#define SPI0RX (1 << 12)
#define SPI0TX (1 << 13)
#define SPI0SCK (1 << 14)
#define SPI1RX (1 << 15)
#define SPI1TX (1 << 16)
#define SPI1SCK (1 << 17)
#define PWM0A (1 << 18)
#define PWM0B (1 << 19)
#define PWM1A (1 << 20)
#define PWM1B (1 << 21)
#define PWM2A (1 << 22)
#define PWM2B (1 << 23)
#define PWM3A (1 << 24)
#define PWM3B (1 << 25)
#define PWM4A (1 << 26)
#define PWM4B (1 << 27)
#define PWM5A (1 << 28)
#define PWM5B (1 << 29)
#define PWM6A (1 << 30)
#define PWM6B 2147483648ULL
#define PWM7A 4294967296ULL
#define PWM7B 8589934592ULL

#ifdef rp2350
#define PWM8A 17179869184ULL
#define PWM8B 34359738368ULL
#define PWM9A 68719476736ULL
#define PWM9B 137438953472ULL
#define PWM10A 274877906944ULL
#define PWM10B 549755813888ULL
#define PWM11A 1099511627776ULL
#define PWM11B 2199023255552ULL
#define FAST_TIMER 4398046511104ULL
#endif

/* ============================================================================
 * Hardware configuration macros
 * ============================================================================ */
#define HEARTBEATpin Option.heartbeatpin
#define PATH_MAX 1024

/* ============================================================================
 * QVGA PIO and state machine configuration
 * ============================================================================ */
#define QVGA_PIO_NUM 0

#ifdef rp2350
#define QVGA_PIO (QVGA_PIO_NUM == 0 ? pio0 : (QVGA_PIO_NUM == 1 ? pio1 : pio2))
#define ScreenBuffer FRAMEBUFFER
#else
#define QVGA_PIO (QVGA_PIO_NUM == 0 ? pio0 : pio1)
#endif

#define QVGA_SM 0     // QVGA state machine
#define QVGA_I2S_SM 1 // I2S state machine when running VGA

/* ============================================================================
 * Compiler optimization attributes
 * ============================================================================ */
#define MIPS16 __attribute__((optimize("-Os")))
#define MIPS32 __attribute__((optimize("-O2")))
#define MIPS64 __attribute__((optimize("-O3")))

/* ============================================================================
 * DMA channel assignments
 * ============================================================================ */
#define QVGA_DMA_CB 0  // DMA control block of base layer
#define QVGA_DMA_PIO 1 // DMA copy data to PIO (raises IRQ0 on quiet)
#define ADC_DMA 2
#define PIO_TX_DMA 4
#define PIO_TX_DMA2 6
#define ADC_DMA2 7
#define PIO_RX_DMA 8
#define PIO_RX_DMA2 9

/* ============================================================================
 * Timing and frequency configuration
 * ============================================================================ */
#define LOCALKEYSCANRATE 10
#define ADC_CLK_SPEED (Option.CPU_Speed * 500)

/* ============================================================================
 * Flash memory layout
 * ============================================================================ */
#define PROGSTART (FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + \
                   ((MAXFLASHSLOTS) * MAX_PROG_SIZE))
#define TOP_OF_SYSTEM_FLASH (FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + \
                             ((MAXFLASHSLOTS + 1) * MAX_PROG_SIZE))

/* ============================================================================
 * Utility macros
 * ============================================================================ */
#define RoundUpK4(a) (((a) + (4096 - 1)) & (~(4096 - 1))) // Round up to nearest page size
#define use_hash

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/* ============================================================================
 * Platform detection macros
 * ============================================================================ */
#define LOWRAM (!defined(rp2350) && (defined(PICOMITEVGA) || defined(PICOMITEWEB)))
#define PICOMITERP2350 (defined(PICOMITE) && defined(rp2350))
#define WEBRP2350 (!defined(rp2350) && defined(PICOMITEWEB))

    /* ============================================================================
     * Type definitions - MM operations enum
     * ============================================================================ */
    typedef enum
    {
        MMHRES,
        MMVRES,
        MMVER,
        MMI2C,
        MMFONTHEIGHT,
        MMFONTWIDTH,
#ifndef USBKEYBOARD
        MMPS2,
#else
    MMUSB,
#endif
        MMHPOS,
        MMVPOS,
        MMONEWIRE,
        MMERRNO,
        MMERRMSG,
        MMWATCHDOG,
        MMDEVICE,
        MMCMDLINE,
#ifdef PICOMITEWEB
        MMMESSAGE,
        MMADDRESS,
        MMTOPIC,
#endif
        MMFLAG,
        MMDISPLAY,
        MMWIDTH,
        MMHEIGHT,
        MMPERSISTENT,
#ifndef PICOMITEWEB
        MMSUPPLY,
#endif
        MMEND
    } Operation;

    /* ============================================================================
     * External variables
     * ============================================================================ */
    extern const char *overlaid_functions[];

#ifdef __cplusplus
}
#endif

#endif /* __CONFIGURATION_H */

/*  @endcond */