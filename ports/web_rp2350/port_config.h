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

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          name

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2350 WEB has enough
 * RAM for the interpreter hot paths even with the CYW43 stack; the
 * small GPIO helpers covered by HAL_PORT_RAM_FUNC stay in flash
 * alongside network buffers. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    __not_in_flash_func(name)
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN           256


/* SPI-LCD clock-pin field: rp2040 PICOMITE shares SYSTEM_CLK for the
 * LCD; rp2350 PICOMITE breaks it out as Option.LCD_CLK. Ports without
 * an SPI LCD at all (VGA/HDMI/WEB) never read it at runtime but need
 * the macro defined so Draw.c compiles. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK


/* Console medium font (FontTable[2]) — VGA ports at QVGA
 * resolution use the narrower arial_bold so an 80-column console
 * fits; every other port (including HDMI, which runs true VGA or
 * higher) uses the wider Hom_16x24_LE.  Value is a symbol name, the
 * actual const arrays are included by Draw.c. */
#define HAL_PORT_CONSOLE_FONT_MEDIUM Hom_16x24_LE

#endif /* PORT_CONFIG_H */
