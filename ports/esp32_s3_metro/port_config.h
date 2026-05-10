/*
 * ports/esp32_s3_metro/port_config.h — port-config for the ESP32-S3 build.
 *
 * Inherits HAL_PORT_* defaults from ports/host_native/port_config.h via
 * a relative include (same pattern as ports/host_wasm/port_config.h),
 * then overrides what differs on Xtensa/ESP32.
 *
 * The IDF main component's CMakeLists.txt lists this directory before
 * ports/host_native on the include path, so this file wins resolution.
 */

#ifndef ESP32_S3_METRO_PORT_CONFIG_H
#define ESP32_S3_METRO_PORT_CONFIG_H

#include "../host_native/port_config.h"

/* Banner identifies the port to anyone connected to the USB Serial/JTAG
 * console. Override host_native's "MMBasic Anywhere (host)". */
#undef  MMBASIC_BANNER_NAME
#define MMBASIC_BANNER_NAME "MMBasic Anywhere (esp32-s3)"

#undef  MMBASIC_BANNER_TRAILER
#define MMBASIC_BANNER_TRAILER "ESP32-S3 REPL.\r\n\r\n"

/* 104 KB MMBasic heap. ESP32-S3 has 512 KB internal SRAM split across
 * dram0_0_seg / dram0_1_seg / etc.; AllMemory has to land in a single
 * contiguous segment, so dram0_0_seg (which holds .bss for this
 * component) caps practical heap at ~110 KB after the static BSS for
 * flash_prog_buf (2 × MAX_PROG_SIZE) + variable tables. The remaining
 * ~400 KB stays on the IDF heap (heap_caps_malloc) for FreeRTOS stacks,
 * drivers, USB Serial/JTAG buffers, etc. Compares to 128 KB on RP2040
 * PicoMite. PSRAM (8 MB) is currently disabled — see Stage E3 in
 * docs/real-hal/esp32-s3-port.md for why. */
#undef  HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (104 * 1024)

/* ESP32-S3 has 49 GPIOs (0–48). Host inherits 44 (RP2040 pin count);
 * left unchanged, PIN(48) reads/writes go out of bounds on
 * PinDef[NBRPINS+1] and friends. */
#undef  HAL_PORT_NBR_PINS
#define HAL_PORT_NBR_PINS                49

/* Inert here (no one dereferences these on ESP32; they're for RP2040's
 * boot stage 2 + program flash split + heap-top mmap address). Real
 * device-shape replacements land in Stage E1 (esp_partition_*). Zero
 * these out so any new caller that does try to use them faults loudly
 * instead of silently reading RP2040 numbers. */
#undef  HAL_PORT_HEAP_TOP
#define HAL_PORT_HEAP_TOP                0
#undef  HAL_PORT_HEAP_TOP_USB
#define HAL_PORT_HEAP_TOP_USB            0

/* HAL_PORT_FLASH_TARGET_OFFSET / *_USB stay at host-inherited values
 * for now because FileIO.c still computes legacy absolute flash offsets.
 * esp32_flash_storage.c translates those offsets to the `mmslots`
 * esp_partition at the port boundary. */

/* HAL_PORT_PWM_SLICE_COUNT / HAL_PORT_PIO_COUNT keep their RP2040
 * numbers from host_native. Inert until a real PWM (LEDC-backed) or
 * PIO equivalent (RMT? probably not — leave as 0 if a port ever needs
 * one) lands. */

#endif /* ESP32_S3_METRO_PORT_CONFIG_H */
