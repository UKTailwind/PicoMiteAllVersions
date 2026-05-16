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

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: no hardware. Values still need to be in-range for the
 * arrays they bound on device (PinFunction[] etc.) — using the RP2040
 * numbers keeps host arrays the same size as the smallest device target. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0

/* Chip-features absent on host. */

/* Board-features absent on host. */
#define HAL_PORT_ADC_CHANNEL_MAX         0
#define HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C 0

/* cmd_files flist[] cap. Host bc_alloc backs both the heap *and* the
 * live VMState — wiping it via SaveContext+InitHeap mid-FRUN is unsafe,
 * so the alloc has to fit in whatever heap space happens to be free.
 * 128 entries (≈36 KB at FF_MAX_LFN=255) fits the test harness's 128 KB
 * heap; mmbasic_ansi's 2 MB heap has plenty of headroom. Covers any
 * realistic test fixture or interactive use. */
#define HAL_PORT_FILES_MAX               128

/* No flash sections to place hot loops in. */
/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* Stage-A palette flags (decascade plan). Host has no hardware backends
 * for any of these — stubs cover every gated path. */
#define HAL_PORT_HAS_WIFI                0
#define HAL_PORT_HAS_GUICONTROLS         0
/* Keyboard backend selector: 0 = PS/2 matrix (drivers/ps2_matrix/),
 *                              1 = USB-host (drivers/usb_host_kbd/).
 * Wires up via port_sources.cmake linkage; configuration.h reads it for
 * USB-vs-PS/2 flash offset / magic key / heap top selection. */
#define HAL_PORT_KEYBOARD_USB_HOST        0
#define HAL_PORT_HAS_I2C_KEYPAD          0

/* I²C bus timing constants — used by I2C.h for the master-timeout
 * macro and by drivers/sd_spi/mmc_stm32.c for the i2c_init() clock
 * rate. Host doesn't drive real I²C, but the constants still need
 * to compile. */
#define HAL_PORT_I2C_TIMEOUT_MS          5
#define HAL_PORT_I2C_SLOW_HZ             100000

/* FLAC decoder base sample-rate cap. Host Audio.c body doesn't decode
 * FLAC, but the port-config standard wants the constant defined on
 * every port. RP2040 number is fine here. */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define PORT_RAM_FUNC(name)          name

/* Placement for MMBasic's hot interpreter functions. Host has no
 * flash/RAM distinction — everything is normal RAM. */
#define MMB_HOT_FUNC(name)    name
#define MMB_DISPATCH_FUNC(name) name

#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN           256

/* Device-name string returned by `fun_device` (MM.DEVICE$). Host
 * advertises itself as "MMBasic Host" so test programs can branch on it. */
#define HAL_PORT_DEVICE_NAME             "MMBasic Host"


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

/* Host uses a deterministic seed for test reproducibility. */
#define HAL_PORT_RANDOMIZE_DEFAULT_SEED() ((int64_t)42)

/* REPL sign-on banner. MMBasic_REPL.c renders "\r<NAME> <VERSION>\r\n"
 * when MMBASIC_BANNER_NAME is defined; if MMBASIC_BANNER_TRAILER is
 * also defined, it follows the copyright line. Device ports define
 * neither and fall through to the runtime `banner[]` array.
 *
 * Strings are restricted to 7-bit ASCII so the WASM canvas console's
 * bitmap font can render them — em-dash / en-dash / typographic quotes
 * fall back to box glyphs in the browser. */
#define MMBASIC_BANNER_NAME              "MMBasic Anywhere (host)"
#define MMBASIC_BANNER_TRAILER           "Host REPL - Ctrl-D to exit.\r\n\r\n"

/* BCCrashInfo storage placement — host has no persistent section,
 * so use plain BSS. */
#define BC_CRASH_INFO_ATTR

/* Stage-D per-port memory + clock + MMBasic-table values. Native host
 * default (128 KB) matches the rp2040 device profile. The WASM and
 * ANSI ports override HAL_PORT_HEAP_MEMORY_SIZE in their own
 * port_config.h via #undef + redef on top of this file. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (128 * 1024)
#define HAL_PORT_MAX_CPU                 420000
#define HAL_PORT_MIN_CPU                 48000
#define HAL_PORT_MAX_VARS                512
#define HAL_PORT_MAX_SUBFUN              256
#define HAL_PORT_FLASH_TARGET_OFFSET     (1024 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1024 * 1024)
#define HAL_PORT_MAGIC_KEY               0xE1799B93
#define HAL_PORT_MAGIC_KEY_USB           0xE1799B93
#define HAL_PORT_HEAP_TOP                0x2003EC00
#define HAL_PORT_HEAP_TOP_USB            0x2003EC00
#define HAL_PORT_CONSOLE_RX_BUF_SIZE     256
#define HAL_PORT_PIOMAX                  2
#define HAL_PORT_NBR_PINS                44
/* Host has no PIO hardware — no claims. */
#define HAL_PORT_PIO0_CLAIMED            false
#define HAL_PORT_PIO1_CLAIMED            false
#define HAL_PORT_PIO2_CLAIMED            false

#endif /* PORT_CONFIG_H */
