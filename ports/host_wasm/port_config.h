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
 * Telnet as unsupported.
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

#endif /* HOST_WASM_PORT_CONFIG_H */
