/*
 * ports/esp32_s3_metro/port_config.h — port-config for the ESP32-S3 build.
 *
 * Port-scoped compile-time constants. Every value is defined here rather
 * than inherited from host_native, so ESP32's hardware shape is explicit.
 */

#ifndef ESP32_S3_METRO_PORT_CONFIG_H
#define ESP32_S3_METRO_PORT_CONFIG_H

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: ESP32-S3.  GPIO 0..48 exist on the chip.  The port does not
 * expose RP2040-style PWM slices or PIO blocks; PWM/servo commands error
 * explicitly until LEDC support is wired. */
#define HAL_PORT_PWM_SLICE_COUNT         0
#define HAL_PORT_GPIO_COUNT              49
#define HAL_PORT_PIO_COUNT               0
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0

/* ADC OPEN streaming is not wired on ESP32. BASIC SETPIN ...,ARAW uses the
 * Metro pin table and hal_pin_esp32.c directly, so this remains zero. */
#define HAL_PORT_ADC_CHANNEL_MAX         0
#define HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C 0

/* cmd_files flist[] cap. The ESP32 MMBasic heap is intentionally small
 * while WiFi is enabled, so keep the conservative host-sized listing cap
 * rather than the larger Pico device cap. */
#define HAL_PORT_FILES_MAX               128

/* Non-VGA serial REPL port. */
#define HAL_PORT_IS_VGA                  0
#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* ESP32 has a native ESP-IDF network implementation selected by its
 * port-local WEB commands and HAL sources. This macro gates the legacy
 * Pico WEB compile path, so it stays disabled here. */
#define HAL_PORT_HAS_WIFI                0
#define HAL_PORT_HAS_GUICONTROLS         0
#define HAL_PORT_KEYBOARD_USB_HOST        0
#define HAL_PORT_HAS_I2C_KEYPAD          0

/* I2C and audio constants are compile-time defaults for shared code paths.
 * The current ESP32 stdio scope links stubs for these feature areas. */
#define HAL_PORT_I2C_TIMEOUT_MS          5
#define HAL_PORT_I2C_SLOW_HZ             100000
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define HAL_PORT_RAM_FUNC(name)          name
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    name
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) name

#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN           256

#define HAL_PORT_DEVICE_NAME             "MMBasic ESP32-S3"

/* Ports without SPI LCD still need this as a compile-time expression for
 * shared display code that is linked with stubs. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK

/* Serial console medium font. Value is a symbol name, resolved in Draw.c. */
#define HAL_PORT_CONSOLE_FONT_MEDIUM Hom_16x24_LE

/* Preserve current ESP32 behaviour: RANDOMIZE with no seed uses the same
 * deterministic seed the port had before this config was made explicit. */
#define HAL_PORT_RANDOMIZE_DEFAULT_SEED() ((int64_t)42)

/* Banner identifies the port to anyone connected to the USB Serial/JTAG
 * console. */
#define MMBASIC_BANNER_NAME "MMBasic Anywhere (esp32-s3)"

#define MMBASIC_BANNER_TRAILER "ESP32-S3 REPL.\r\n\r\n"

/* 48 KB MMBasic heap while WiFi is enabled. ESP32-S3 has 512 KB internal
 * SRAM split across dram0_0_seg / dram0_1_seg / etc.; AllMemory has to
 * land in a single contiguous segment, so dram0_0_seg (which holds .bss
 * for this component) is the limiting resource. WiFi consumes enough
 * internal DRAM that the old larger stdio heap no longer links.
 * ESP32 bytecode compiler scratch tables allocate from ESP-IDF internal
 * heap, but VM runtime allocations still come from this MMBasic heap.
 * The board has ESP-IDF-managed Octal PSRAM available to explicit
 * heap_caps users only. Do not route AllMemory there implicitly. */
#define HAL_PORT_HEAP_MEMORY_SIZE (48 * 1024)

/* Stage-D per-port memory + clock + MMBasic-table values. The flash
 * offsets remain the legacy 1 MB values because FileIO.c still computes
 * absolute offsets; esp32_flash_storage.c translates them to the mmslots
 * partition at the port boundary. */
#define HAL_PORT_MAX_CPU                 420000
#define HAL_PORT_MIN_CPU                 48000
#define HAL_PORT_MAX_VARS                512
#define HAL_PORT_MAX_SUBFUN              256
#define HAL_PORT_FLASH_TARGET_OFFSET     (1024 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1024 * 1024)
#define HAL_PORT_MAGIC_KEY               0xE1799B93
#define HAL_PORT_MAGIC_KEY_USB           0xE1799B93
#define HAL_PORT_HEAP_TOP                0
#define HAL_PORT_HEAP_TOP_USB            0
/* WiFi-capable port: telnet delivers data in TCP segments (lwIP MSS
 * ~1460 on ESP32-S3). The 256-byte ring used by non-WiFi ports
 * overflows on long single-segment bursts before MMgetline can drain,
 * silently dropping the tail of the line. Match the WiFi-port pattern
 * in configuration.h (CONSOLE_RX_BUF_SIZE = TCP_MSS on `HAL_PORT_HAS_WIFI`
 * ports) by reserving a similar-sized buffer here; ESP32-S3 PSRAM gives
 * us the headroom for free. */
#define HAL_PORT_CONSOLE_RX_BUF_SIZE     1536
#define HAL_PORT_PIOMAX                  0
#define HAL_PORT_NBR_PINS                49

/* ESP32 has no RP2040 PIO blocks. */
#define HAL_PORT_PIO0_CLAIMED            false
#define HAL_PORT_PIO1_CLAIMED            false
#define HAL_PORT_PIO2_CLAIMED            false

/* BCCrashInfo storage placement. ESP32 currently uses regular BSS. */
#define HAL_PORT_BC_CRASH_INFO_ATTR

/* Slab size reserved from ESP-IDF SPIRAM at boot for MMBasic PSRAM
 * ownership. The Metro N16R8 module is 8 MB total; ESP-IDF retains the
 * remainder for its own WiFi RX / SmartConfig buffers. Tunable in the
 * 4–7 MB range — start conservative and bump after measuring free
 * SPIRAM after WiFi join. This is the *heap* portion of the slab; the
 * physical slab acquired by hal_psram_esp32.c is larger, see below. */
#define HAL_PORT_PSRAM_SLAB_BYTES        (6u * 1024u * 1024u)

/* Slot region size for `RAM SAVE` / `RAM LOAD` numbered slots. The
 * shared formula PSRAMblock = PSRAMbase + PSRAMsize + 0x60000 puts this
 * past the heap region, so hal_psram_esp32.c allocates a physical slab
 * of HAL_PORT_PSRAM_SLAB_BYTES + 0x60000 + HAL_PORT_PSRAM_BLOCK_SIZE
 * bytes and publishes only HAL_PORT_PSRAM_SLAB_BYTES as PSRAMsize so
 * Memory.c's bitmap allocator stays within the heap portion. */
#define HAL_PORT_PSRAM_BLOCK_SIZE        (MAXRAMSLOTS * MAX_PROG_SIZE)

#endif /* ESP32_S3_METRO_PORT_CONFIG_H */
