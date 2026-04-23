/*
 * ports/pico_rp2350/port_config.h — COMPILE=PICORP2350 and PICOUSBRP2350.
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
#define HAL_PORT_HAS_HEARTBEAT           1
#define HAL_PORT_ADC_CHANNEL_MAX         4
#define HAL_PORT_HAS_SSD1963             1

/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

/* PICOMITE rp2350 has the BufferedPanel..NEXTGEN range of display types. */
#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    1

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

/* SPI-LCD clock-pin field: rp2350 PICOMITE adds a dedicated Option.LCD_CLK
 * alongside SYSTEM_CLK so the LCD can sit on its own SPI instance. On
 * rp2040 PICOMITE (and non-PICOMITE ports) SYSTEM_CLK doubles as the LCD
 * clock. Core code uses the macro as a plain expression value — never
 * as a preprocessor gate — so it complies with the port-config standard. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.LCD_CLK

#endif /* PORT_CONFIG_H */
