/* Stub for host build — FileIO.c / MM_Misc.c call save_and_disable_interrupts
 * and restore_interrupts inside disable_interrupts_pico / enable_interrupts_pico.
 * On host those wrapper bodies are empty (gated with #ifdef MMBASIC_HOST), so
 * these symbols are never referenced. We still provide declarations so code
 * outside the gates that happens to include this header keeps compiling. */
#ifndef _HARDWARE_SYNC_H
#define _HARDWARE_SYNC_H
#include <stdint.h>
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t saved) { (void)saved; }
#endif
