/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
// ****************************************************************************
//
//                                 Main code
//
// ****************************************************************************
#ifndef _INCLUDE_H
#define _INCLUDE_H

#include "port_config.h"   /* HAL_PORT_HAS_HDMI gates the PIO header
                              picks near the end of this file. */

typedef unsigned char Bool;
#define True 1
#define False 0

// NULL
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

// I/O port prefix
#define __IO	volatile

// request to use inline
#define INLINE __attribute__((always_inline)) inline

// avoid to use inline
#define NOINLINE __attribute__((noinline))

// weak function
#define WEAK __attribute__((weak))

// align array to 4-bytes
#define ALIGNED __attribute__((aligned(4)))

// default LED pin
#define LED_PIN 25

// (The original `INLINE void nop()` and `INLINE void cb()` definitions
//  lived here but were never called. They also collided with SSD1963.h's
//  `#define nop asm("NOP")` macro on any port that combined HAS_VGA_PIO
//  with HAS_WIFI — Include.h gets pulled in via `#ifdef PICOMITEVGA` in
//  PicoMite.c / mmc_stm32.c, and SSD1963.h gets pulled in via
//  HAS_WIFI=1's branch in Hardware_Includes.h. Removed to unblock the
//  combined VGA+WiFi port shape.)

// ----------------------------------------------------------------------------
//                               Constants
// ----------------------------------------------------------------------------


#define BIT(pos) (1UL<<(pos))


// ----------------------------------------------------------------------------
//                                   Includes
// ----------------------------------------------------------------------------

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <string.h>
#include "hardware/divider.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "configuration.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/adc.h"
#include "hardware/exception.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/scb.h"
#include "hardware/vreg.h"
#include <pico/bootrom.h>
#include "hardware/irq.h"
#include "hardware/pio.h"
#if !HAL_PORT_HAS_HDMI
	#include "PicoMiteVGA.pio.h"
	#include "PicoMiteI2S.pio.h"
#endif
#if !HAL_PORT_HAS_USB_KEYBOARD
	#include "pico/unique_id.h"
	#include "class/cdc/cdc_device.h" 
#endif


// ****************************************************************************
//
//                                    QVGA configuration
//
// ****************************************************************************
// port pins
//	GP22... VGA
//	GP16 ... VGA HSYNC/CSYNC synchronization (inverted: negative SYNC=LOW=0x80, BLACK=HIGH=0x00)
//	GP17 ... VSYNC

// QVGA port pins
#define QVGA_GPIO_FIRST	PinDef[Option.VGA_BLUE].GPno	// first QVGA GPIO
#define QVGA_GPIO_NUM	4	// number of QVGA color GPIOs, without HSYNC and VSYNC
#define QVGA_GPIO_LAST	(QVGA_GPIO_FIRST+QVGA_GPIO_NUM-1) // last QVGA GPIO
#define QVGA_GPIO_HSYNC	PinDef[Option.VGA_HSYNC].GPno	// QVGA HSYNC/CSYNC GPIO
#define QVGA_GPIO_VSYNC	(QVGA_GPIO_HSYNC+1) // QVGA VSYNC GPIO


// QVGA display resolution
//#define FRAMESIZE (38400) // display frame size in bytes (=38400)

// 126 MHz timings
#define QVGA_TOTAL_F	4000// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
#define QVGA_HSYNC_F	480	// horizontal sync clock ticks
#define QVGA_BP_F	 240	// back porch clock ticks
#define QVGA_FP_F	80	// front porch clock ticks

// QVGA vertical timings
#define QVGA_VTOT_F	525	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
#define QVGA_VSYNC_F	2	// length of V sync (number of scanlines)
#define QVGA_VBACK_F	33	// V back porch
#define QVGA_VACT_F	480	// V active scanlines (= 2*HEIGHT)
#define QVGA_VFRONT_F	10	// V front porch


#endif // _MAIN_H
/*  @endcond */
