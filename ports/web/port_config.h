/*
 * ports/web/port_config.h — COMPILE=WEB (rp2040 PICOMITEWEB).
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
/* WEB variants claim the onboard LED for the CYW43 radio — no heartbeat. */
#define HAL_PORT_HAS_HEARTBEAT           0
/* WEB reserves GP29 for the CYW43 radio — only 3 ADC channels. */
#define HAL_PORT_ADC_CHANNEL_MAX         3
#define HAL_PORT_HAS_SSD1963             1

/* WEB on rp2040 runs lwIP + CYW43 network stacks and can't afford the RAM
 * pressure of pinning GPIO loops in SRAM. Keep them in flash. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap (RP2040 → 44.1 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define HAL_PORT_RAM_FUNC(name)          name

#endif /* PORT_CONFIG_H */
