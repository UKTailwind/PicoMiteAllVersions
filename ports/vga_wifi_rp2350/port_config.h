/*
 * ports/vga_wifi_rp2350/port_config.h — F-stage validation port:
 * VGA-PIO scanout + WiFi (CYW43 polled) on RP2350.
 *
 * This is a Stage-F validation port — exists to prove the decascade
 * lets HAS_WIFI=1 + HAS_VGA_PIO=1 coexist cleanly. Real hardware is the
 * pico2_w board with the QVGA scanout PIO running on core1 alongside
 * the CYW43 polled stack on core0. CYW43 owns the QSPI pins so PSRAM
 * is unavailable; ADC channel 3 (GP29) is reserved for the radio.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: RP2350 (pico2_w is RP2350A — 30 GPIO). */
#define HAL_PORT_PWM_SLICE_COUNT         12
#define HAL_PORT_GPIO_COUNT              48
#define HAL_PORT_PIO_COUNT               3
#define HAL_PORT_PULLDOWN_NEEDS_RESET    1
/* VGA + WiFi: I²S shares PIO 0 with QVGA scanout. */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       0
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   252000

/* MMInkey pinned to RAM — rp2350 has plenty of SRAM. */
#define HAL_PORT_MMINKEY_DECL(name)      __not_in_flash_func(name)

/* CYW43 owns the QSPI pins — no PSRAM heap. */
/* Radio claims the onboard LED. */
/* GP29 reserved for CYW43 — only ADC0..2 user-accessible. */
#define HAL_PORT_ADC_CHANNEL_MAX         3

#define HAL_PORT_FILES_MAX               1000

/* VGA family (PICOMITEVGA defined). */
#define HAL_PORT_IS_VGA                  1
#define HAL_PORT_HAS_HDMI                0

/* Stage-A palette flags — F2 combines WiFi + VGA-PIO. No GUICONTROLS
 * (matches WEB on rp2040 — a tighter heap budget than WEBRP2350). */
#define HAL_PORT_HAS_WIFI                1
#define HAL_PORT_HAS_GUICONTROLS         0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY     0
/* Keyboard backend selector: 0 = PS/2 matrix (drivers/ps2_matrix/),
 *                              1 = USB-host (drivers/usb_host_kbd/).
 * Wires up via port_sources.cmake linkage; configuration.h reads it for
 * USB-vs-PS/2 flash offset / magic key / heap top selection. */
#define HAL_PORT_KEYBOARD_USB_HOST        0
#define HAL_PORT_HAS_I2C_KEYPAD          0

/* core1stack[] in words. QVGA scanout core1 (vga_qvga_modes::QVgaCore)
 * needs 128 words; CYW43 polled stack runs on core0, no extra core1
 * consumer. */
#define HAL_PORT_CORE1_STACK_WORDS       128

/* Memory + clock + MMBasic-table values. RP2350A on pico2_w only has
 * 264 KB SRAM, and CYW43 + lwIP buffers eat ~30 KB before any
 * BASIC heap. The QVGA scanout framebuffer trailer dominates if
 * sized like vga_rp2350 (153 KB). Reduce heap + use the smaller
 * rp2040-style scanout trailer (38 KB) so the overall image fits. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (80 * 1024)
#define HAL_PORT_MAX_CPU                 378000
#define HAL_PORT_MIN_CPU                 252000
#define HAL_PORT_MAX_VARS                768
#define HAL_PORT_MAX_SUBFUN              512
#define HAL_PORT_MAX_MODES               3
#define HAL_PORT_FLASH_TARGET_OFFSET     (1376 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1376 * 1024)
#define HAL_PORT_MAGIC_KEY               0x6B12C7E1
#define HAL_PORT_MAGIC_KEY_USB           0x6B12C7E1
#define HAL_PORT_HEAP_TOP                0x2007C000
#define HAL_PORT_HEAP_TOP_USB            0x2007C000
#define HAL_PORT_PIOMAX                  3
#define HAL_PORT_NBR_PINS                40
/* F2 combines VGA-PIO scanout (PIO1+PIO2) with CYW43 SPI (PIO0); all
 * three PIO instances are claimed. No user PIO instances available. */
#define HAL_PORT_PIO0_CLAIMED            true
#define HAL_PORT_PIO1_CLAIMED            true
#define HAL_PORT_PIO2_CLAIMED            true
/* No PSRAM — base 0 short-circuits the address-range guard
 * (PSRAMsize is unconditionally 0 on this port). */
#define HAL_PORT_PSRAM_BASE              0
#define HAL_PORT_PSRAM_BLOCK_SIZE        0

#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          name
#define HAL_PORT_MMBASIC_HOT_FUNC(name)  __not_in_flash_func(name)
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

/* QVGA scanout framebuffer trailer — smaller rp2040-style 1bpp QVGA
 * (640*480/8 = 38400 bytes) so the image fits on pico2_w's 264 KB
 * SRAM. RP2350A doesn't expose enough RAM for the 16-bit QVGA mode
 * once CYW43 is also resident. */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES (640*480/8)
#define HAL_PORT_ALLMEMORY_ALIGN           4096

#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK
#define HAL_PORT_CONSOLE_FONT_MEDIUM     arial_bold
#define HAL_PORT_RANDOMIZE_DEFAULT_SEED() ((int64_t)hal_time_us_64())
#define HAL_PORT_BC_CRASH_INFO_ATTR __attribute__((section(".uninitialized_data.bc_crash_info")))

#endif /* PORT_CONFIG_H */
