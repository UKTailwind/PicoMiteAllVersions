/* Legacy Pico SDK compatibility shim for hardware/regs/addressmap.h.
 *
 * The native/WASM host has no XIP — flash is simulated in a RAM buffer
 * at its real heap address — so XIP-relative pointer arithmetic in core
 * code collapses to plain buffer offsets. Setting XIP_BASE to 0 keeps
 * those expressions well-formed. */
#ifndef _HARDWARE_REGS_ADDRESSMAP_H
#define _HARDWARE_REGS_ADDRESSMAP_H
#define XIP_BASE 0
#endif
