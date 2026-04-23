/*
 * ports/vga/port_config.h — COMPILE=VGA and COMPILE=VGAUSB (rp2040 VGA).
 *
 * Port-scoped compile-time constants. See ports/pico/port_config.h for
 * the mechanism; values below are plain #defines with no #ifdef gates.
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

#define HAL_PORT_HAS_PSRAM               0
#define HAL_PORT_HAS_UPNG                0
#define HAL_PORT_HAS_DEFINES             0
#define HAL_PORT_HAS_HEARTBEAT           1
#define HAL_PORT_ADC_CHANNEL_MAX         4
/* VGA boards don't ship SSD1963 support. */
#define HAL_PORT_HAS_SSD1963             0

/* VGA on rp2040 runs scanout from flash via XIP — forcing GPIO hot loops
 * into SRAM would starve the scanout buffer and cause tearing. Leave them
 * in flash; scanout bandwidth takes priority over pin-write latency. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap (RP2040 → 44.1 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100

#define HAL_PORT_RAM_FUNC(name)          name

#endif /* PORT_CONFIG_H */
