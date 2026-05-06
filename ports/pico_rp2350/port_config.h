/*
 * ports/pico_rp2350/port_config.h — COMPILE=PICORP2350 and PICOUSBRP2350.
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
/* rp2350 SPI-LCD ports run audio I²S on PIO 2 (PIO 0 is reserved for
 * scanout / merge tasks). */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       2
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   200000

/* MMInkey pinned to RAM — rp2350 has plenty of SRAM. */
#define HAL_PORT_MMINKEY_DECL(name)      __not_in_flash_func(name)

#define HAL_PORT_ADC_CHANNEL_MAX         4

/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

/* PICOMITE rp2350 has the BufferedPanel..NEXTGEN range of display types. */
#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    1

/* Stage-A palette flags (decascade plan). PICORP2350 gets GUICONTROLS
 * because rp2350 PICOMITE has the RAM for the widget tables. */
#define HAL_PORT_HAS_WIFI                0
#define HAL_PORT_HAS_GUICONTROLS         1

/* core1stack[] size in words. PICOMITE runs the SPI-LCD merge pipeline
 * on core1 (display_merge_pico, ~2 KB stack). */
#define HAL_PORT_CORE1_STACK_WORDS       512

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). PICOCALC trims heap by 4 KB to fit the VM alongside the full
 * interpreter — handled in configuration.h with a runtime PICOCALC ifdef. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (300 * 1024)
#define HAL_PORT_HEAP_MEMORY_SIZE_PICOCALC (296 * 1024)
#define HAL_PORT_MAX_CPU                 396000
#define HAL_PORT_MIN_CPU                 48000
#define HAL_PORT_MAX_VARS                768
#define HAL_PORT_MAX_SUBFUN              512
#define HAL_PORT_FLASH_TARGET_OFFSET     (1152 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1152 * 1024)
#define HAL_PORT_MAGIC_KEY               0x192084D7
#define HAL_PORT_MAGIC_KEY_USB           0xD37F4F27
#define HAL_PORT_HEAP_TOP                0x20078000
#define HAL_PORT_HEAP_TOP_USB            0x20078000
#define HAL_PORT_CONSOLE_RX_BUF_SIZE     256
#define HAL_PORT_PIOMAX                  3
#define HAL_PORT_NBR_PINS                62
/* PICOMITE rp2350 claims all three PIO instances. */
#define HAL_PORT_PIO0_CLAIMED            true
#define HAL_PORT_PIO1_CLAIMED            true
#define HAL_PORT_PIO2_CLAIMED            true
/* QSPI PSRAM region. PSRAMblock = base + size + 0x60000; size is filled
 * in by runtime PSRAM detect. PSRAMbase is the XIP cache region. */
#define HAL_PORT_PSRAM_BASE              0x11000000
#define HAL_PORT_PSRAM_BLOCK_SIZE        0x1C0000

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2350 has plenty of
 * RAM; put both categories in SRAM. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    __not_in_flash_func(name)
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN           256

/* SPI-LCD clock-pin field: rp2350 PICOMITE adds a dedicated Option.LCD_CLK
 * alongside SYSTEM_CLK so the LCD can sit on its own SPI instance. On
 * rp2040 PICOMITE (and non-PICOMITE ports) SYSTEM_CLK doubles as the LCD
 * clock. Core code uses the macro as a plain expression value — never
 * as a preprocessor gate — so it complies with the port-config standard. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.LCD_CLK


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
