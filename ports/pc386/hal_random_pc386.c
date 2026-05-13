/*
 * ports/pc386/hal_random_pc386.c — random source.
 *
 * xorshift32, seeded from RDTSC on first call. Not cryptographic;
 * matches the quality of host_native's libc rand() and is fine for
 * MMBasic's RND() function.
 */

#include <stdint.h>

#include "hal/hal_random.h"

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t state = 0;

uint32_t hal_random_u32(void)
{
    if (state == 0) {
        uint32_t seed = (uint32_t)rdtsc();
        if (seed == 0) seed = 0x12345678u;  /* xorshift wedges on 0 */
        state = seed;
    }
    /* Marsaglia xorshift32 */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
