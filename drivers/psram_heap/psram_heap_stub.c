/*
 * drivers/psram_heap/psram_heap_stub.c — no-op PSRAM heap for
 * targets without QSPI PSRAM (rp2040 PICO/VGA/WEB, rp2350 WEB, host).
 *
 * Memory.c + Commands.c reference psmap / SBitsGet / SBitsSet /
 * GetPSMemory unconditionally; those callers all runtime-guard with
 * `if(PSRAMsize)` or an address-range check that fails when
 * PSRAMsize is 0, so the stubs are never invoked at runtime but need
 * to satisfy the linker.
 *
 * psmap is a 1-word stub so Commands.c's `sizeof(psmap)` references
 * (SaveContext/RestoreContext record the bitmap alongside MMHeap for
 * the LFS fallback) resolve without paying the full 24 KB BSS cost
 * that the real bitmap would need on targets without PSRAM.
 */

#include "MMBasic_Includes.h"

unsigned int psmap[1] = {0};
const unsigned int psmap_size_bytes = sizeof(psmap);

unsigned int SBitsGet(unsigned char * addr) {
    (void)addr;
    return 0;
}

void SBitsSet(unsigned char * addr, int bits) {
    (void)addr;
    (void)bits;
}

void * GetPSMemory(int size) {
    (void)size;
    return NULL;
}

/* PSRAM init stubs for ports that don't link psram.c (rp2040 ports
 * never run this code path since the call is inside #ifdef rp2350;
 * rp2350 WEB/VGA-WIFI ports' Option.PSRAM_CS_PIN stays 0 because the
 * QSPI pins are owned by CYW43, so the runtime guard at the call site
 * keeps these stubs unreached). */
void psram_setup(void) {}

#include <stddef.h>
size_t psram_size(void) {
    return 0;
}
