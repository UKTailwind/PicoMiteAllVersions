/*
 * ports/pico/port_config.h — COMPILE=PICO and COMPILE=PICOUSB (rp2040).
 *
 * Port-scoped compile-time constants. Every value is a plain #define — no
 * #ifdef gates live in this file. Core may use these constants as *values*
 * in C expressions and array sizes; never as preprocessor gates. See
 * docs/real-hal-fixup-plan.md.
 *
 * The build system selects this file by adding ports/pico/ to the include
 * path for COMPILE=PICO and COMPILE=PICOUSB. Every other port directory
 * has its own port_config.h with the values appropriate to that target.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Chip-level: RP2040. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_HAS_PIO2                0
#define HAL_PORT_HAS_FAST_TIMER          0
#define HAL_PORT_HAS_INT5                0
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0

/* Chip-feature: RP2040 variants don't ship PSRAM, upng, or the DEFINES
 * compile-time dictionary. */
#define HAL_PORT_HAS_PSRAM               0
#define HAL_PORT_HAS_UPNG                0
#define HAL_PORT_HAS_DEFINES             0

/* Board-level features. HAL_PORT_HAS_HEARTBEAT is set on boards that
 * expose a user-configurable heartbeat LED pin. PICOMITEWEB omits it
 * because the CYW43 wireless chip claims the onboard LED. */
#define HAL_PORT_HAS_HEARTBEAT           1

/* Hot-path placement: SPI-LCD RP2040 has spare RAM for the GPIO hot loops
 * so we force them into SRAM via __not_in_flash_func. */
#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

#endif /* PORT_CONFIG_H */
