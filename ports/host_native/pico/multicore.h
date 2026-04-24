/* Stub for host build */
#ifndef _PICO_MULTICORE_H
#define _PICO_MULTICORE_H
#include <stdint.h>
#include "pico/mutex.h"   /* pico_multicore transitively pulls this in on device */
static inline void multicore_fifo_push_blocking(uint32_t data) { (void)data; }
static inline uint32_t multicore_fifo_pop_blocking(void) { return 0; }
static inline void multicore_reset_core1(void) {}
static inline void multicore_launch_core1(void (*entry)(void)) { (void)entry; }
#endif
