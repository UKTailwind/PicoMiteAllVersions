/*
 * drivers/upng_sprite/upng_sprite_stub.c — LOADPNG stub for targets
 * without the amalgamated upng decoder (rp2040 variants, WEB, host).
 *
 * The rp2350 LOADPNG body was ~80 lines in Draw.c guarded by
 * `#ifdef rp2350`. On smaller chips the flash+RAM budget for the
 * decoder isn't there, so we emit a BASIC error at runtime.
 */

#include "MMBasic_Includes.h"

void hal_port_sprite_loadpng(unsigned char * p) {
    (void)p;
    error("LOADPNG not supported on this device");
}
