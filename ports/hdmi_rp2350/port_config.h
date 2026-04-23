/*
 * ports/hdmi_rp2350/port_config.h — COMPILE=HDMI and HDMIUSB.
 *
 * Port-scoped compile-time constants. See ports/pico/port_config.h for
 * the mechanism; values below are plain #defines with no #ifdef gates.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Chip-level: RP2350 (HDMI is rp2350-only). */
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
#define HAL_PORT_HAS_HEARTBEAT           1
#define HAL_PORT_ADC_CHANNEL_MAX         4
#define HAL_PORT_HAS_SSD1963             0

/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

/* HDMI variant of the VGA family. */
#define HAL_PORT_HAS_HDMI                1
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

#endif /* PORT_CONFIG_H */
