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
 * The Makefile orders -I$(WASM_DIR) before -I$(NATIVE_DIR) so this
 * file wins resolution.
 */

#ifndef HOST_WASM_PORT_CONFIG_H
#define HOST_WASM_PORT_CONFIG_H

#include "../host_native/port_config.h"

#undef HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (8 * 1024 * 1024)

#define MAX_PROG_SIZE (512 * 1024)
#define MMBASIC_BANNER_NAME "MMBasic Web"

#endif /* HOST_WASM_PORT_CONFIG_H */
