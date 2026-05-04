/*
 * drivers/heartbeat/heartbeat_real.c — real heartbeat-LED toggle.
 * Linked on ports with an onboard heartbeat LED owned by GPIO.
 * Mutually exclusive with heartbeat_stub.c (linked on Web rp2040
 * etc., where the LED is owned by the CYW43 module).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_heartbeat.h"
#include "hal/hal_pin.h"
#include "hardware/gpio.h"

void hal_heartbeat_tick(void) {
    if (ExtCurrentConfig[PinDef[HEARTBEATpin].pin] == EXT_HEARTBEAT) {
        gpio_xor_mask64(1ULL << PinDef[HEARTBEATpin].GPno);
    }
}

void hal_heartbeat_assert_supported(void) { /* heartbeat LED present, OK. */ }

void hal_heartbeat_init_pins(void) {
    if (!Option.AllPins) {
        if (CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))
            ExtCfg(41, EXT_DIG_OUT, Option.PWM);
        if (CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))
            ExtCfg(42, EXT_DIG_IN, 0);
        if (CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))
            ExtCfg(44, EXT_ANA_IN, 0);
    }
    if (CheckPin(HEARTBEATpin, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED) && !Option.NoHeartbeat) {
        hal_pin_init_digital(PinDef[HEARTBEATpin].GPno);
        hal_pin_set_dir(PinDef[HEARTBEATpin].GPno, HAL_PIN_DIR_OUT);
        ExtCurrentConfig[PinDef[HEARTBEATpin].pin] = EXT_HEARTBEAT;
    }
}
