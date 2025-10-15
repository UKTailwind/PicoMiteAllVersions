/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
 * PicoMite - Main Include Header
 *
 * This header contains common definitions, macros, and includes for the PicoMite system
 **********************************************************************************************************************/

#ifndef _INCLUDE_H
#define _INCLUDE_H

/* ==============================================================================================================
 * BASIC TYPE DEFINITIONS
 * ============================================================================================================== */
typedef unsigned char Bool;
#define True 1
#define False 0

// NULL pointer definition
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* ==============================================================================================================
 * COMPILER ATTRIBUTES AND MODIFIERS
 * ============================================================================================================== */
#define __IO volatile								 // I/O port prefix
#define INLINE __attribute__((always_inline)) inline // Force inline
#define NOINLINE __attribute__((noinline))			 // Prevent inline
#define WEAK __attribute__((weak))					 // Weak function
#define ALIGNED __attribute__((aligned(4)))			 // Align to 4-bytes

/* ==============================================================================================================
 * INLINE ASSEMBLY FUNCTIONS
 * ============================================================================================================== */

// No-operation instruction
INLINE void nop()
{
	__asm volatile(" nop\n");
}

// Compiler barrier
INLINE void cb()
{
	__asm volatile("" ::: "memory");
}

/* ==============================================================================================================
 * COMMON CONSTANTS
 * ============================================================================================================== */
#define BIT(pos) (1UL << (pos)) // Bit position macro
#define LED_PIN 25				// Default LED pin

/* ==============================================================================================================
 * STANDARD LIBRARY INCLUDES
 * ============================================================================================================== */
#include <stdio.h>
#include <string.h>

/* ==============================================================================================================
 * PICO SDK INCLUDES
 * ============================================================================================================== */
// Core Pico includes
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"

// Hardware peripheral includes
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/divider.h"
#include "hardware/dma.h"
#include "hardware/exception.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

// Hardware structures
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"

/* ==============================================================================================================
 * USB INCLUDES (conditional)
 * ============================================================================================================== */
#ifndef USBKEYBOARD
#include "class/cdc/cdc_device.h"
#endif

/* ==============================================================================================================
 * PROJECT-SPECIFIC INCLUDES
 * ============================================================================================================== */
#include "configuration.h"

// PIO program includes (conditional on HDMI)
#ifndef HDMI
#include "PicoMiteVGA.pio.h"
#include "PicoMiteI2S.pio.h"
#endif

/* ==============================================================================================================
 * QVGA DISPLAY CONFIGURATION
 * ============================================================================================================== */

// QVGA GPIO Pin Definitions
#define QVGA_GPIO_FIRST PinDef[Option.VGA_BLUE].GPno		 // First QVGA GPIO
#define QVGA_GPIO_NUM 4										 // Number of QVGA color GPIOs
#define QVGA_GPIO_LAST (QVGA_GPIO_FIRST + QVGA_GPIO_NUM - 1) // Last QVGA GPIO
#define QVGA_GPIO_HSYNC PinDef[Option.VGA_HSYNC].GPno		 // HSYNC/CSYNC GPIO
#define QVGA_GPIO_VSYNC (QVGA_GPIO_HSYNC + 1)				 // VSYNC GPIO

/* --------------------------------------------------------------------------------------------------------------
 * QVGA Horizontal Timing (126 MHz clock)
 *
 * Note: HSYNC is inverted - negative SYNC=LOW=0x80, BLACK=HIGH=0x00
 * -------------------------------------------------------------------------------------------------------------- */
#define QVGA_TOTAL_F 4000 // Total horizontal clock ticks
						  // (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
#define QVGA_HSYNC_F 480  // Horizontal sync pulse width (clock ticks)
#define QVGA_BP_F 240	  // Back porch duration (clock ticks)
#define QVGA_FP_F 80	  // Front porch duration (clock ticks)

/* --------------------------------------------------------------------------------------------------------------
 * QVGA Vertical Timing
 * -------------------------------------------------------------------------------------------------------------- */
#define QVGA_VTOT_F 525	 // Total scanlines
						 // (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
#define QVGA_VSYNC_F 2	 // Vertical sync pulse (scanlines)
#define QVGA_VBACK_F 33	 // Vertical back porch (scanlines)
#define QVGA_VACT_F 480	 // Active display lines (= 2*HEIGHT)
#define QVGA_VFRONT_F 10 // Vertical front porch (scanlines)

/* --------------------------------------------------------------------------------------------------------------
 * QVGA Display Memory
 * -------------------------------------------------------------------------------------------------------------- */
// #define FRAMESIZE        38400   // Display frame size in bytes

#endif // _INCLUDE_H
	   /* @endcond */