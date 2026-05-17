/*
 * ports/mmbasic_ansi/port_config.h — port-config for the ANSI terminal build.
 *
 * Inherits every HAL_PORT_* value from ports/host_native/port_config.h
 * and bumps the heap to 2 MB. Native's 128 KB default fragments
 * bc_compiler_alloc on BC_SIM_RP2350 compile-time arrays and fails
 * "NEM[vm:comptbl]" on anything non-trivial.
 *
 * The Makefile orders -I$(PORT_DIR) before -I$(HOST_DIR) so this
 * file wins resolution.
 */

#ifndef MMBASIC_ANSI_PORT_CONFIG_H
#define MMBASIC_ANSI_PORT_CONFIG_H

#include "../host_native/port_config.h"

#undef HAL_PORT_HEAP_MEMORY_SIZE
#define HAL_PORT_HEAP_MEMORY_SIZE (2 * 1024 * 1024)

#undef MMBASIC_BANNER_NAME
#define MMBASIC_BANNER_NAME "PicoMite MMBasic ANSI"

/* Host-native's "Ctrl-D to exit" trailer doesn't apply: the ANSI port
 * runs in alt-screen raw mode and exits via the REPL's own quit path. */
#undef MMBASIC_BANNER_TRAILER


/* Compiler-table sizes. */
#include "../bc_tables_rp2350.h"

#endif /* MMBASIC_ANSI_PORT_CONFIG_H */
