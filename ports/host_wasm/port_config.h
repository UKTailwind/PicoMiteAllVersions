/*
 * ports/host_wasm/port_config.h — port-config for the WASM browser build.
 *
 * Inherits every HAL_PORT_* value from ports/host_native/port_config.h
 * via a relative include, then overrides only the three things WASM
 * differs on:
 *
 *   HAL_PORT_HEAP_MEMORY_SIZE   8 MB browser budget (vs. 128 KB native).
 *   MAX_PROG_SIZE               512 KB cap on flash_prog_buf + slot
 *                               mirror (vs. tracking the full heap;
 *                               unconstrained, static data balloons to
 *                               ~40 MB).
 *   MMBASIC_BANNER_NAME         REPL sign-on string.
 *
 * The browser build intentionally does not inherit host-native POSIX network
 * sockets. `host_wasm_web.c` keeps the WEB hooks linkable, supports simple
 * HTTP `WEB TCP CLIENT REQUEST` calls through browser fetch, supports browser
 * MQTT-over-WebSocket, and reports raw TCP stream/server, UDP, TFTP, and
 * Telnet as unsupported unless a trusted proxy advertises those transports.
 *
 * The Makefile orders -I$(WASM_DIR) before -I$(NATIVE_DIR) so this
 * file wins resolution.
 */

#ifndef HOST_WASM_PORT_CONFIG_H
#define HOST_WASM_PORT_CONFIG_H

#include "../host_native/port_config.h"

#undef HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (8 * 1024 * 1024)

#define MAX_PROG_SIZE (512 * 1024)

/* Override the host_native banner. WASM canvas font is 7-bit ASCII, so
 * keep all banner strings ASCII-only (em/en-dashes render as boxes). */
#undef MMBASIC_BANNER_NAME
#define MMBASIC_BANNER_NAME "MMBasic Anywhere (web)"

#undef MMBASIC_BANNER_TRAILER
#define MMBASIC_BANNER_TRAILER "Browser REPL.\r\n\r\n"

/* Compiler-table sizing.  Fixed for WASM regardless of DEVICE_SIM —
 * the browser runtime heap is user-selectable at run time (128 KB ...
 * 8 MB via wasm_set_heap_size) but the *compile-time* table sizes
 * must fit in the smallest realistic profile.  RP2350-class sizing
 * (≈64 KB of tables, 1024 line-map entries) handles real programs
 * (Picovaders = 697 lines) and still leaves room in a 300 KB heap
 * for the program's runtime data.  bytecode.h sees BC_MAX_CODE
 * defined and skips its legacy target-macro chain. */
#define BC_MAX_CODE       (32 * 1024)
#define BC_MAX_CONSTANTS  96
#define BC_MAX_SLOTS      192
#define BC_MAX_SUBFUNS    96
#define BC_MAX_FIXUPS     512
#define BC_MAX_LINEMAP    1024
#define BC_MAX_LOCALS     64
#define BC_MAX_PARAMS     16
#define BC_MAX_LOCAL_META 384
#define BC_MAX_NEST       32
#define BC_MAX_DATA_ITEMS 1024

#endif /* HOST_WASM_PORT_CONFIG_H */
