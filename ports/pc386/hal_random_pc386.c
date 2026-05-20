/*
 * ports/pc386/hal_random_pc386.c — random source.
 *
 * xorshift32. Not cryptographic; matches the quality of host_native's
 * libc rand() and is fine for MMBasic's RND() function.
 */

#include <stdint.h>

#include "hal/hal_random.h"

static uint32_t state = 0x12345678u;

uint32_t hal_random_u32(void)
{
    /* Marsaglia xorshift32 */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
