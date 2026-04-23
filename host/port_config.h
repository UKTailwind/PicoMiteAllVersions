/*
 * host/port_config.h — port-config constants for the host build
 * (mmbasic_test, mmbasic_sim, picomite.wasm).
 *
 * Same shape as ports/<board>/port_config.h: plain #defines, no #ifdef
 * gates inside. The host Makefile/build script puts host/ on the include
 * path so FileIO.c / External.c / etc. pick this up alongside their other
 * headers.
 *
 * Host has no hardware: no PWM slices, no PIO, no PSRAM, no SSD1963, no
 * UPNG, no DEFINES dictionary. The HAS_* feature gates resolve to 0 so
 * the corresponding `if (HAL_PORT_HAS_FOO) { … }` branches in core
 * compile-time-dead out of the host build.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Chip-level: no hardware. Values still need to be in-range for the
 * arrays they bound on device (PinFunction[] etc.) — using the RP2040
 * numbers keeps host arrays the same size as the smallest device target. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_HAS_PIO2                0
#define HAL_PORT_HAS_FAST_TIMER          0
#define HAL_PORT_HAS_INT5                0
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0

/* Chip-features absent on host. */
#define HAL_PORT_HAS_PSRAM               0
#define HAL_PORT_HAS_UPNG                0
#define HAL_PORT_HAS_DEFINES             0

/* Board-features absent on host. */
#define HAL_PORT_HAS_HEARTBEAT           0
#define HAL_PORT_ADC_CHANNEL_MAX         0
#define HAL_PORT_HAS_SSD1963             0
#define HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C 0

/* cmd_files flist[] cap. Host bc_alloc backs both the heap *and* the
 * live VMState — wiping it via SaveContext+InitHeap mid-FRUN is unsafe,
 * so the alloc has to fit in whatever heap space happens to be free.
 * 256 entries (≈19 KB) covers any test fixture or interactive use. */
#define HAL_PORT_FILES_MAX               256

/* No flash sections to place hot loops in. */
/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap. Host Audio.c body doesn't decode
 * FLAC, but the port-config standard wants the constant defined on
 * every port. RP2040 number is fine here. */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define HAL_PORT_RAM_FUNC(name)          name

/* Device-name string returned by `fun_device` (MM.DEVICE$). Host
 * advertises itself as "MMBasic Host" so test programs can branch on it. */
#define HAL_PORT_DEVICE_NAME             "MMBasic Host"


/* SPI-LCD clock-pin field: rp2040 PICOMITE shares SYSTEM_CLK for the
 * LCD; rp2350 PICOMITE breaks it out as Option.LCD_CLK. Ports without
 * an SPI LCD at all (VGA/HDMI/WEB) never read it at runtime but need
 * the macro defined so Draw.c compiles. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK

#endif /* PORT_CONFIG_H */
