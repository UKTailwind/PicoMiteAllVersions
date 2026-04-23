/*
 * ports/web_rp2350/port_config.h — COMPILE=WEBRP2350.
 *
 * Port-scoped compile-time constants. See ports/pico/port_config.h for
 * the mechanism; values below are plain #defines with no #ifdef gates.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Chip-level: RP2350. */
#define HAL_PORT_PWM_SLICE_COUNT         12
#define HAL_PORT_GPIO_COUNT              48
#define HAL_PORT_PIO_COUNT               3
#define HAL_PORT_HAS_PIO2                1
#define HAL_PORT_HAS_FAST_TIMER          1
#define HAL_PORT_HAS_INT5                1
#define HAL_PORT_PULLDOWN_NEEDS_RESET    1

#define HAL_PORT_HAS_PSRAM               1
#define HAL_PORT_HAS_UPNG                1
#define HAL_PORT_HAS_DEFINES             1
/* WEBRP2350 claims the onboard LED for the CYW43 radio. */
#define HAL_PORT_HAS_HEARTBEAT           0
/* WEBRP2350 reserves GP29 for the CYW43 radio. */
#define HAL_PORT_ADC_CHANNEL_MAX         3
#define HAL_PORT_HAS_SSD1963             1

/* WEBRP2350 runs lwIP + CYW43; the deleted hal_port_config.h kept GPIO
 * loops out of SRAM for every PICOMITEWEB build (both rp2040 and rp2350).
 * Preserve that behaviour here. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

#define HAL_PORT_RAM_FUNC(name)          name

#endif /* PORT_CONFIG_H */
