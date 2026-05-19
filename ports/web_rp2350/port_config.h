/*
 * ports/web_rp2350/port_config.h — COMPILE=WEBRP2350.
 *
 * Port-scoped compile-time constants. See ports/pico/port_config.h for
 * the mechanism; values below are plain #defines with no #ifdef gates.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: RP2350. */
#define HAL_PORT_PWM_SLICE_COUNT         12
#define HAL_PORT_GPIO_COUNT              48
#define HAL_PORT_PIO_COUNT               3
#define HAL_PORT_PULLDOWN_NEEDS_RESET    1
/* WEB rp2350: SPI-LCD framebuffer, audio I²S on PIO 2. */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       2
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   200000

#define HAL_PORT_HAS_I2C_KEYPAD          0
#define HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C 0
#define HAL_PORT_I2C_TIMEOUT_MS          5
#define HAL_PORT_I2C_SLOW_HZ             100000

/* MMInkey pinned to RAM — rp2350 has plenty of SRAM. */
#define MMINKEY_DECL(name)      __not_in_flash_func(name)

/* WEBRP2350 claims the onboard LED for the CYW43 radio. */
/* WEBRP2350 reserves GP29 for the CYW43 radio. */
#define HAL_PORT_ADC_CHANNEL_MAX         3

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

/* Stage-A palette flags (decascade plan). WEBRP2350 is the only port that
 * combines WiFi with GUICONTROLS today (GUI.c references Option.MaxCtrls
 * unconditionally so the widget tables must exist). */
#define HAL_PORT_HAS_WIFI                1
#define HAL_PORT_HAS_GUICONTROLS         1
/* Keyboard backend selector: 0 = PS/2 matrix (drivers/ps2_matrix/),
 *                              1 = USB-host (drivers/usb_host_kbd/).
 * Wires up via port_sources.cmake linkage; configuration.h reads it for
 * USB-vs-PS/2 flash offset / magic key / heap top selection. */
#define HAL_PORT_KEYBOARD_USB_HOST        0

/* core1stack[] size in words. WEBRP2350 with SPI LCD uses core1 for the
 * same display merge / FASTGFX worker as the PicoMite SPI-LCD ports. */
#define HAL_PORT_CORE1_STACK_WORDS       512

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). WEBRP2350 has no USB-keyboard variant. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (208 * 1024)
/* PicoCalc WEBRP2350 leaves extra SRAM headroom for WiFi, GUI controls,
 * and the keypad/display stack. Keep this 4 KiB-sector aligned because
 * MAX_PROG_SIZE also drives flash erase lengths. */
#define HAL_PORT_MAX_CPU                 252000
#define HAL_PORT_MIN_CPU                 126000
#define HAL_PORT_MAX_VARS                768
#define HAL_PORT_MAX_SUBFUN              512
#define HAL_PORT_FLASH_TARGET_OFFSET     (1472 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1472 * 1024)
#define HAL_PORT_MAGIC_KEY               0x54472B1C
#define HAL_PORT_MAGIC_KEY_USB           0x54472B1C
#define HAL_PORT_HEAP_TOP                0x2006E000
#define HAL_PORT_HEAP_TOP_USB            0x2006E000
#define HAL_PORT_PIOMAX                  3
#define HAL_PORT_NBR_PINS                40
/* CYW43 SPI runs on PIO0; rp2350 also claims PIO2; PIO1 free. */
#define HAL_PORT_PIO0_CLAIMED            true
#define HAL_PORT_PIO1_CLAIMED            false
#define HAL_PORT_PIO2_CLAIMED            true
/* QSPI PSRAM region. WebMite RP2350B boards use CYW43 on regular GPIOs,
 * so QSPI PSRAM remains available when OPTION PSRAM PIN is configured. */
#define HAL_PORT_PSRAM_BASE              0x11000000
#define HAL_PORT_PSRAM_BLOCK_SIZE        0x1A0000

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define PORT_RAM_FUNC(name)          name

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2350 WEB has enough
 * RAM for the interpreter hot paths even with the CYW43 stack; the
 * small GPIO helpers covered by PORT_RAM_FUNC stay in flash
 * alongside network buffers. */
#define MMB_HOT_FUNC(name)    __not_in_flash_func(name)
#define MMB_DISPATCH_FUNC(name) __not_in_flash_func(name)

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

/* RANDOMIZE default seed when the BASIC program passes 0 (or omits
 * the seed). Device ports use the hardware timer for entropy; host
 * ports have their own policy (see host/port_config.h). */
#define HAL_PORT_RANDOMIZE_DEFAULT_SEED() ((int64_t)hal_time_us_64())

/* BCCrashInfo storage placement. On device it lives in an
 * uninitialized section (.uninitialized_data) that survives soft /
 * watchdog reset so the next-boot bc_crash_dump_if_any can read
 * register values stored before the fault. Host has no such
 * section — plain BSS. */
#define BC_CRASH_INFO_ATTR __attribute__((section(".uninitialized_data.bc_crash_info")))


/* Compiler-table sizes. */
#include "../bc_tables_rp2350.h"

#endif /* PORT_CONFIG_H */
