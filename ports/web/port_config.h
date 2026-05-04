/*
 * ports/web/port_config.h — COMPILE=WEB (rp2040 PICOMITEWEB).
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

/* Chip-level: RP2040. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0
/* rp2040 WEB has only 2 PIOs and CYW43 + scanout share them; audio
 * goes on PIO 0. */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       0
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   200000

/* MMInkey placement: rp2040 WEB has tight RAM (CYW43 firmware + lwIP
 * heap eat most of it), so MMInkey stays in flash. */
#define HAL_PORT_MMINKEY_DECL(name)      name

/* WEB variants claim the onboard LED for the CYW43 radio — no heartbeat. */
/* WEB reserves GP29 for the CYW43 radio — only 3 ADC channels. */
#define HAL_PORT_ADC_CHANNEL_MAX         3

/* WEB on rp2040 runs lwIP + CYW43 network stacks and can't afford the RAM
 * pressure of pinning GPIO loops in SRAM. Keep them in flash. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* Stage-A palette flags (decascade plan). PICOMITEWEB on rp2040 has the
 * CYW43+lwIP+mongoose stack but doesn't fit GUICONTROLS in heap. */
#define HAL_PORT_HAS_WIFI                1
#define HAL_PORT_HAS_GUICONTROLS         0
/* Keyboard backend selector: 0 = PS/2 matrix (drivers/ps2_matrix/),
 *                              1 = USB-host (drivers/usb_host_kbd/).
 * Wires up via port_sources.cmake linkage; configuration.h reads it for
 * USB-vs-PS/2 flash offset / magic key / heap top selection. */
#define HAL_PORT_KEYBOARD_USB_HOST        0
#define HAL_PORT_HAS_I2C_KEYPAD          0

/* core1stack[] size in words. WEB never launches core1 — single canary
 * word satisfies MMBasic.c's overflow check at core1stack[0]. */
#define HAL_PORT_CORE1_STACK_WORDS       1

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). WEB has no USB-keyboard variant; the _USB siblings are defined
 * equal to the non-USB ones so configuration.h's HAL_PORT_KEYBOARD_USB_HOST-axis macro
 * resolves correctly. CONSOLE_RX_BUF_SIZE = TCP_MSS via lwIP — handled
 * in configuration.h since TCP_MSS lives in lwipopts.h. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (88 * 1024)
#define HAL_PORT_MAX_CPU                 252000
#define HAL_PORT_MIN_CPU                 126000
#define HAL_PORT_MAX_VARS                480
#define HAL_PORT_MAX_SUBFUN              256
#define HAL_PORT_FLASH_TARGET_OFFSET     (1080 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1080 * 1024)
#define HAL_PORT_MAGIC_KEY               0x54472B1C
#define HAL_PORT_MAGIC_KEY_USB           0x54472B1C
#define HAL_PORT_HEAP_TOP                0x2003D000
#define HAL_PORT_HEAP_TOP_USB            0x2003D000
#define HAL_PORT_PIOMAX                  2
#define HAL_PORT_NBR_PINS                40
/* CYW43 SPI runs on PIO0; PIO1 free for user on rp2040 WIFI. */
#define HAL_PORT_PIO0_CLAIMED            true
#define HAL_PORT_PIO1_CLAIMED            false
#define HAL_PORT_PIO2_CLAIMED            false

/* FLAC decoder base sample-rate cap (RP2040 → 44.1 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define HAL_PORT_RAM_FUNC(name)          name

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2040 WEB is the
 * most RAM-tight target (CYW43 + lwIP stack + TCP buffers); keep
 * all three in flash. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    name
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) name

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
#define HAL_PORT_BC_CRASH_INFO_ATTR __attribute__((section(".uninitialized_data.bc_crash_info")))

#endif /* PORT_CONFIG_H */
