#include "pico/rand.h"

#include "hal/hal_random.h"

uint32_t hal_random_u32(void) {
    return get_rand_32();
}
