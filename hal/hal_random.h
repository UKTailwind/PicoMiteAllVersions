/*
 * hal/hal_random.h - random number source HAL.
 *
 * Surface core code uses when it needs non-deterministic/random bytes.
 * Ports may back this with hardware entropy or a libc PRNG depending on
 * platform capability.
 */

#ifndef HAL_RANDOM_H
#define HAL_RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t hal_random_u32(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RANDOM_H */
