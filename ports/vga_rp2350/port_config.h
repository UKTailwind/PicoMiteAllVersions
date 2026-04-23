/*
 * ports/vga_rp2350/port_config.h — COMPILE=VGARP2350 and VGAUSBRP2350.
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
#define HAL_PORT_HAS_SSD1963             0

/* RP2350 has the RAM budget to place GPIO hot loops in SRAM even on VGA. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)


/* SPI-LCD clock-pin field: rp2040 PICOMITE shares SYSTEM_CLK for the
 * LCD; rp2350 PICOMITE breaks it out as Option.LCD_CLK. Ports without
 * an SPI LCD at all (VGA/HDMI/WEB) never read it at runtime but need
 * the macro defined so Draw.c compiles. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK

#endif /* PORT_CONFIG_H */
