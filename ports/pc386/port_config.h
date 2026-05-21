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

/* 2 MB MMBasic heap. The Pocket386 target has 8 MB RAM; this still
 * leaves room for the kernel, BSS-backed program/flash buffer, drivers,
 * and stack while giving BASIC a materially larger workspace. */
#undef  HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (2 * 1024 * 1024)

/* Keep the RAM-backed flash/program buffer smaller than the heap. The
 * default MAX_PROG_SIZE follows HEAP_MEMORY_SIZE, which is too expensive
 * on an 8 MB PC because pc386 also needs a BSS-backed ProgMemory image. */
#define MAX_PROG_SIZE (1 * 1024 * 1024)

/* Separate C-library bump heap. This is for port/libc allocations, not
 * MMBasic variables; do not tie it to HAL_PORT_HEAP_MEMORY_SIZE. */
#define PC386_LIBC_HEAP_SIZE (256 * 1024)

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


/* Compiler-table sizes are inherited from host_native/port_config.h:
 * rp2350-class tables are a better RAM fit here than host-sized tables. */

#endif /* PC386_PORT_CONFIG_H */
