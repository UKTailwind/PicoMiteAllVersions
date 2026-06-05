/*
 * ports/pico_sdk_common/bc_crash_pico.c — device impl of the two
 * bc_debug.c port hooks that need ARM-specific access:
 *
 *   - port_bc_crash_get_sp()          : read the current stack pointer
 *   - port_bc_crash_save_fault_regs() : snapshot CFSR/HFSR/BFAR/MMFAR
 *
 * The fault-register decode print stays in bc_debug.c since dbg_print
 * is static there — with bits always zero on host the decode collapses
 * to a single line of zeros, which is harmless.
 */

#include <stdint.h>
#include "bytecode.h"

uint32_t port_bc_crash_get_sp(void) {
    register uint32_t sp_val __asm("sp");
    return sp_val;
}

void port_bc_crash_save_fault_regs(BCCrashInfo * info) {
    info->cfsr = *(volatile uint32_t *)0xE000ED28;  /* CFSR  */
    info->hfsr = *(volatile uint32_t *)0xE000ED2C;  /* HFSR  */
    info->bfar = *(volatile uint32_t *)0xE000ED38;  /* BFAR  */
    info->mmfar = *(volatile uint32_t *)0xE000ED34; /* MMFAR */
}
