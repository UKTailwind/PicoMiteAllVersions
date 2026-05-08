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

/* Phase B: heap pinned at 32 KB so that Mthe combined static BSS
 * (AllMemory + the host_fs_shims RAM mirrors flash_prog_buf and
 * host_flash_target_buf, both sized as multiples of MAX_PROG_SIZE)
 * fits the 512 KB ESP32-S3 internal SRAM. Those mirrors only exist
 * because we're reusing host_native/host_fs_shims.c — they're a
 * *host* port artefact, not real flash. Phase C replaces them with
 * an esp_partition-backed flash impl that doesn't keep a RAM
 * mirror, and bumps heap back up to the 128 KB+ range matching
 * RP2040, with PSRAM available for any larger arrays. */
#undef  HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (32 * 1024)

#endif /* ESP32_S3_METRO_PORT_CONFIG_H */
