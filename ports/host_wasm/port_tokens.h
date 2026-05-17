/*
 * ports/host_wasm/port_tokens.h — token table for the WASM browser build.
 *
 * Inherits the host_native token set, then adds MODE.  Native host has no
 * notion of screen modes (single fixed framebuffer per launch), but WASM
 * supports BASIC `MODE N` by mapping mode numbers to resolutions chosen
 * by the user in the config dialog and reallocating the framebuffer
 * in-place — see ports/host_wasm/host_wasm_mode.c.
 *
 * The Makefile orders -I$(WASM_DIR) before -I$(NATIVE_DIR), so this
 * file wins resolution over the host_native sibling.
 */

#ifndef PORT_TOKENS_WASM_H
#define PORT_TOKENS_WASM_H

/* Pull in the host_native token set first, then add MODE on top.  The
 * inner file uses #ifndef PORT_TOKENS_H as its own guard; do NOT match
 * that here or the inner include becomes a no-op and every HAL_PORT_*
 * macro it defines goes missing. */
#include "../host_native/port_tokens.h"

#undef HAL_PORT_VIDEO_CMD_TOKENS
#define HAL_PORT_VIDEO_CMD_TOKENS \
    { (unsigned char *)"Camera",  T_CMD, 0, cmd_camera }, \
    { (unsigned char *)"Refresh", T_CMD, 0, cmd_refresh }, \
    { (unsigned char *)"MODE",    T_CMD, 0, cmd_mode },

#endif /* PORT_TOKENS_WASM_H */
