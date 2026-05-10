#include <stdint.h>

#include "esp_random.h"
#include "hal/hal_random.h"

uint32_t hal_random_u32(void)
{
    return esp_random();
}
