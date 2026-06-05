/* Legacy Pico SDK compatibility shim. */
#ifndef _HARDWARE_SYNC_H
#define _HARDWARE_SYNC_H
#include <stdint.h>
static inline uint32_t save_and_disable_interrupts(void) {
    return 0;
}
static inline void restore_interrupts(uint32_t saved) {
    (void)saved;
}
#endif
