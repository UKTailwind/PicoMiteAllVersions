/* Stub for host build */
#ifndef _HARDWARE_STRUCTS_WATCHDOG_H
#define _HARDWARE_STRUCTS_WATCHDOG_H
#include <stdint.h>
typedef struct { volatile uint32_t ctrl; } watchdog_hw_t;
extern watchdog_hw_t *watchdog_hw;
#define WATCHDOG_CTRL_ENABLE_BITS 1
#define hw_clear_bits(addr, mask) do { *(volatile uint32_t*)(addr) &= ~(mask); } while(0)
#endif
