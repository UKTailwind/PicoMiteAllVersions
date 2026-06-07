/*
 * ESP32-S3 GPIO map.
 *
 * BASIC's GPn syntax maps through codemap() into PinDef[] slots. Keep slot
 * zero as the legacy NULL row, then map GP0..GP48 to slots 1..49 so shared
 * pin-state arrays keep their usual 1-based indexing.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#define ESP32_ADC_NONE 99
#define ESP32_NO_PWM 99
#define ESP32_DIGITAL (DIGITAL_IN | DIGITAL_OUT)
#define ESP32_ANALOG (DIGITAL_IN | DIGITAL_OUT | ANALOG_IN)

#define ESP32_PIN(gpio, modes, adc) \
    {(gpio) + 1, (gpio), "GP" #gpio, (modes), (adc), ESP32_NO_PWM}

#define ESP32_UNUSED(gpio) \
    {(gpio) + 1, (gpio), "GP" #gpio, UNUSED, ESP32_ADC_NONE, ESP32_NO_PWM}

const uint8_t PINMAP[49] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49};

const struct s_PinDef PinDef[NBRPINS + 1] = {
    {0, 99, "NULL", UNUSED, ESP32_ADC_NONE, ESP32_NO_PWM},
    ESP32_PIN(0, DIGITAL_IN, ESP32_ADC_NONE), /* BOOT switch */
    ESP32_PIN(1, ESP32_ANALOG, 0),
    ESP32_PIN(2, ESP32_ANALOG, 1),
    ESP32_PIN(3, ESP32_ANALOG, 2),
    ESP32_PIN(4, ESP32_ANALOG, 3),
    ESP32_PIN(5, ESP32_ANALOG, 4),
    ESP32_PIN(6, ESP32_ANALOG, 5),
    ESP32_PIN(7, ESP32_ANALOG, 6),
    ESP32_PIN(8, ESP32_ANALOG, 7),
    ESP32_PIN(9, ESP32_ANALOG, 8),
    ESP32_PIN(10, ESP32_ANALOG, 9),
    ESP32_PIN(11, ESP32_ANALOG, 10),
    ESP32_PIN(12, ESP32_ANALOG, 11),
    ESP32_PIN(13, ESP32_ANALOG, 12), /* onboard LED */
    ESP32_PIN(14, ESP32_ANALOG, 13),
    ESP32_PIN(15, ESP32_ANALOG, 14),
    ESP32_PIN(16, ESP32_ANALOG, 15),
    ESP32_PIN(17, ESP32_ANALOG, 16),
    ESP32_PIN(18, ESP32_ANALOG, 17),
    ESP32_UNUSED(19), /* native USB D- */
    ESP32_UNUSED(20), /* native USB D+ */
    ESP32_PIN(21, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_UNUSED(22),
    ESP32_UNUSED(23),
    ESP32_UNUSED(24),
    ESP32_UNUSED(25),
    ESP32_UNUSED(26),
    ESP32_UNUSED(27),
    ESP32_UNUSED(28),
    ESP32_UNUSED(29),
    ESP32_UNUSED(30),
    ESP32_UNUSED(31),
    ESP32_UNUSED(32),
    ESP32_UNUSED(33),
    ESP32_UNUSED(34),
    ESP32_UNUSED(35),
    ESP32_UNUSED(36),
    ESP32_UNUSED(37),
    ESP32_PIN(38, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(39, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(40, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(41, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(42, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(43, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(44, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(45, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(46, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(47, ESP32_DIGITAL, ESP32_ADC_NONE),
    ESP32_PIN(48, ESP32_DIGITAL, ESP32_ADC_NONE),
};

int codemap(int pin) {
    if (pin < 0 || pin >= 49) error("Invalid GPIO");
    return (int)PINMAP[pin];
}
