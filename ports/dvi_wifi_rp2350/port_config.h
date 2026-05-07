/*
 * ports/dvi_wifi_rp2350/port_config.h — COMPILE=DVIWIFIRP2350.
 *
 * F1-class hybrid port: HDMI/DVI scanout + CYW43 WiFi (RM2 module) +
 * QSPI PSRAM + I2S audio on RP2350B. Built from the union of
 * hdmi_rp2350 (HDMI sink + PSRAM heap + audio MP3) and
 * vga_wifi_rp2350 (WiFi stack + cyw43_arch). RM2 wires CYW43 SPI on
 * dedicated GPIOs (GP23/24/25/29 by default — see board file
 * pimoroni_pico_plus2_w_rp2350.h), so the QSPI pins are free and PSRAM
 * stays available — the genuine "all features at once" port shape the
 * decascade plan was for.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: RP2350 (HDMI is rp2350-only). */
#define HAL_PORT_PWM_SLICE_COUNT         12
#define HAL_PORT_GPIO_COUNT              48
#define HAL_PORT_PIO_COUNT               3
#define HAL_PORT_PULLDOWN_NEEDS_RESET    1
/* HDMI + WiFi: HSTX scanout (no PIO), audio I²S on PIO 2 matching the
 * legacy HDMI port's choice. The CYW43 bus PIO uses
 * pio_claim_free_sm_and_add_program_for_gpio_range so it picks
 * whichever PIO instance has a free SM — typically PIO 0 once I²S
 * has claimed PIO 2. KNOWN ISSUE: combining HDMI HSTX + WiFi + I²S
 * audio with high-numbered pins (>GP31) can hard-fault during boot;
 * not yet root-caused. Use lower-numbered I²S pins or skip I²S on
 * this port until the conflict is understood. */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       2
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   378000

/* MMInkey pinned to RAM — rp2350 has plenty of SRAM. */
#define HAL_PORT_MMINKEY_DECL(name)      __not_in_flash_func(name)


/* HDMI + WiFi combined. PSRAM + UPNG + DEFINES inherit from HDMI
 * (RM2's CYW43 lives off the QSPI pins so PSRAM stays available).
 * GUICONTROLS off — no touch panel on a DVI display. */
#define HAL_PORT_HAS_WIFI                1
#define HAL_PORT_HAS_GUICONTROLS         0
/* Keyboard backend selector: 0 = no physical keyboard / CDC-only console
 * (drivers/console_cdc/), 1 = USB-host (drivers/usb_host_kbd/).
 * Wires up via port_sources.cmake linkage; configuration.h reads it for
 * USB-vs-non-USB flash offset / magic key / heap top selection. */
#define HAL_PORT_KEYBOARD_USB_HOST        1
#define HAL_PORT_HAS_I2C_KEYPAD          0

/* core1stack[] size in words. HDMI runs the DVI scanout loop on core1
 * (hdmi_scanout::HDMICore, 512-byte stack). */
#define HAL_PORT_CORE1_STACK_WORDS       128

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). HDMI's MIN_CPU is FreqX (250 MHz in the VGA-family freq table);
 * MAX_CPU is Freq378P (378 MHz). Numeric values pass through; the
 * Freq* constant aliases are defined in configuration.h under
 * HAL_PORT_IS_VGA so HDMI / VGA programs can still write the symbolic
 * speed constants. */
/* HDMI 320x240x16 framebuffer trailer is 153 KB. CYW43 + lwIP buffers
 * are another ~30 KB. With those two priorities + interpreter
 * scratch, the BASIC heap fits at ~120 KB on RP2350's 520 KB SRAM
 * (about 80 KB lower than HDMI-only's 184 KB). The intent is that
 * BASIC programs use PSRAM for big allocations via cmd_psram. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (120 * 1024)
#define HAL_PORT_MAX_CPU                 378000
#define HAL_PORT_MIN_CPU                 250000
#define HAL_PORT_MAX_VARS                768
#define HAL_PORT_MAX_SUBFUN              512
#define HAL_PORT_MAX_MODES               5
#define HAL_PORT_FLASH_TARGET_OFFSET     (1408 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1408 * 1024)
/* Unique magic key — pick anything not used by an existing port. */
#define HAL_PORT_MAGIC_KEY               0xD51F77E4
#define HAL_PORT_MAGIC_KEY_USB           0xD51F77E4
#define HAL_PORT_HEAP_TOP                0x2007D000
#define HAL_PORT_HEAP_TOP_USB            0x2007D000
#define HAL_PORT_CONSOLE_RX_BUF_SIZE     256
#define HAL_PORT_PIOMAX                  3
#define HAL_PORT_NBR_PINS                62
/* HDMI scanout + DVI mode tables claim all three PIO instances. */
#define HAL_PORT_PIO0_CLAIMED            true
#define HAL_PORT_PIO1_CLAIMED            true
#define HAL_PORT_PIO2_CLAIMED            true
#define HAL_PORT_PSRAM_BASE              0x11000000
#define HAL_PORT_PSRAM_BLOCK_SIZE        0x1C0000
/* CYW43 owns the LED on standard pico2_w pinout (RM2 module on
 * pico_stretch wires the same way). No user-pickable heartbeat pin. */
/* GP29 is CYW43 SPI clock; ADC3 (GP29) unavailable. RP2350B exposes
 * GP40-47 with additional ADC channels; bump this number once the
 * core ADC code is taught about them. */
#define HAL_PORT_ADC_CHANNEL_MAX         3

/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

/* HDMI variant of the VGA family. */
#define HAL_PORT_HAS_HDMI                1
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2350 HDMI has plenty
 * of RAM; put both in SRAM. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    __not_in_flash_func(name)
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

/* HDMI framebuffer trailer: 320*240*2 = 153600 bytes (same as VGA
 * rp2350 — HDMI scanout reads the same 320x240 logical buffer and
 * upscales via the DVI PHY). */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES (320*240*2)
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
