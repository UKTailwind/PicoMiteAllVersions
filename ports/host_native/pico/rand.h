/* Stub for host build */
#ifndef _PICO_RAND_H
#define _PICO_RAND_H
#include <stdint.h>
#include <stdlib.h>
static inline uint32_t get_rand_32(void) { return (uint32_t)rand(); }
static inline uint64_t get_rand_64(void) { return ((uint64_t)rand() << 32) | rand(); }
#endif
