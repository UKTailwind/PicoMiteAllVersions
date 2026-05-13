/*
 * ports/pc386/port_config.h — port-config for bare-metal IBM-PC 386+.
 *
 * Inherits HAL_PORT_* defaults from ports/host_native/port_config.h via
 * a relative include (same pattern as ports/host_wasm/port_config.h
 * and ports/esp32_s3_metro/port_config.h), then overrides what differs
 * on a freestanding 32-bit x86 target.
 *
 * The kernel Makefile lists this directory before ports/host_native on
 * the include path, so this file wins resolution.
 */

#ifndef PC386_PORT_CONFIG_H
#define PC386_PORT_CONFIG_H

#include "../host_native/port_config.h"

/* Banner identifies the port to anyone connected to COM1 or watching
 * VGA text mode. Override host_native's "MMBasic Anywhere (host)". */
#undef  MMBASIC_BANNER_NAME
#define MMBASIC_BANNER_NAME "MMBasic Anywhere (pc386)"

#undef  MMBASIC_BANNER_TRAILER
#define MMBASIC_BANNER_TRAILER "PC/386 bare-metal REPL.\r\n\r\n"

/* 1 MB MMBasic heap. The realistic floor target is a 386 with 4 MB
 * RAM; with kernel + drivers + stack budget at ~256 KB and the
 * multiboot memory map giving us the rest, 1 MB of BASIC heap leaves
 * plenty of slack. Compares to 128 KB on RP2040 PicoMite and 104 KB on
 * ESP32-S3. May raise to 2-4 MB once Stage 1 lands the heap-over-
 * memory-map allocator and we see what the actual budget looks like. */
#undef  HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (1 * 1024 * 1024)

/* Pc386 exposes the DB-25 LPT1 connector as BASIC-addressable GPIO.
 * The user-facing pin numbers are the connector pins: data 2..9,
 * control 1/14/16/17, and status inputs 10/11/12/13/15. */
#undef  HAL_PORT_NBR_PINS
#define HAL_PORT_NBR_PINS                17

/* No flash partitioning on PC. File persistence lives on a FAT16
 * volume reachable through hal_storage; legacy absolute-flash-offset
 * code paths in FileIO.c go through stub translations. Zero these out
 * so any caller that does try to use them faults loudly. */
#undef  HAL_PORT_HEAP_TOP
#define HAL_PORT_HEAP_TOP                0
#undef  HAL_PORT_HEAP_TOP_USB
#define HAL_PORT_HEAP_TOP_USB            0

/* HAL_PORT_PWM_SLICE_COUNT / HAL_PORT_PIO_COUNT / etc. inherit zero or
 * inert values from host_native. The PIT-based tone driver doesn't use
 * the PWM_SLICE abstraction — it goes through hal_audio with a single
 * channel. Leave as inherited. */

/* Identify ourselves to any code that needs to know the port shape at
 * compile time (rare — the HAL purity rules forbid this in core, but
 * driver TUs and port-local files may legitimately need it). */
#define PORT_PC386                       1

#endif /* PC386_PORT_CONFIG_H */
