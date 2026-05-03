/*
 * drivers/heartbeat/heartbeat_real.c — real heartbeat-LED toggle.
 * Linked on ports with an onboard heartbeat LED owned by GPIO
 * (HAL_PORT_HAS_HEARTBEAT=1).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_heartbeat.h"
#include "hardware/gpio.h"

void hal_heartbeat_tick(void) {
    if (ExtCurrentConfig[PinDef[HEARTBEATpin].pin] == EXT_HEARTBEAT) {
        gpio_xor_mask64(1ULL << PinDef[HEARTBEATpin].GPno);
    }
}
