#include <stdint.h>
#include <stdlib.h>

#include "hal/hal_random.h"

uint32_t hal_random_u32(void) {
    return (uint32_t)rand();
}
