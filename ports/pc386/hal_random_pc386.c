/*
 * ports/pc386/hal_random_pc386.c — random source.
 *
 * xorshift32, with state also reachable from libc srand() so that
 * MMBasic's RANDOMIZE n actually seeds the same stream RND() reads.
 * MMBasic's cmd_randomize calls srand(); without shared state below,
 * RANDOMIZE was a no-op for RND() determinism.
 *
 * Not cryptographic — matches host_native's libc-rand quality. Fine for
 * BASIC RND().
 */

#include <stdint.h>

#include "hal/hal_random.h"

/* Visible to pc386_libc.c so srand() can reseed us. */
uint32_t pc386_rand_state = 0x12345678u;

uint32_t hal_random_u32(void)
{
    /* Marsaglia xorshift32. Avoid the 0 fixed point: srand(0) becomes 1. */
    if (pc386_rand_state == 0) pc386_rand_state = 1;
    pc386_rand_state ^= pc386_rand_state << 13;
    pc386_rand_state ^= pc386_rand_state >> 17;
    pc386_rand_state ^= pc386_rand_state << 5;
    return pc386_rand_state;
}
