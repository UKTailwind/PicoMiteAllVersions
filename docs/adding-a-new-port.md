# Adding a New Port

There are two flavours of port:

- **Hardware board port** — a physical board running on RP2040 or RP2350 silicon. Lives under `ports/<board>/`, composes drivers from `drivers/*/`, integrates into the top-level CMake recipe. Existing examples: `ports/pico/`, `ports/pico_rp2350/`, `ports/vga/`, `ports/hdmi_rp2350/`.
- **Simulation port** — a desktop binary that runs MMBasic against a synthetic peripheral set (stdin/stdout, terminal, web canvas, …). Lives under `ports/<name>/`, has its own Makefile, reuses the `host_native` port's HAL implementations for the shared parts. Existing examples: `ports/host_native/` (macOS/Linux REPL with framebuffer canvas), `ports/host_wasm/` (browser build), `ports/mmbasic_stdio/` (pure stdio), `ports/mmbasic_ansi/` (half-block terminal renderer).

The two flavours share almost no build machinery. Pick the right section below.

Prerequisites for either: read `docs/real-hal-plan.md` (the index) + `docs/real-hal/architecture.md` (directory layout).

---

# Part A — Hardware board port

## The shape of a hardware port

Three files under `ports/<your_board>/` plus a selection in the top-level CMake recipe. Drivers are not written here — they live under `drivers/*/`, and your port selects which ones it links. If your board needs a peripheral no existing driver covers, see "New drivers" at the end.

### Required files

```
ports/<your_board>/
    port_config.h        — port-scoped #define constants (plain #defines, no #ifdef)
    port_sources.cmake   — source list + per-port build config (compile flags, libs)
    pin_tables.c         — PINMAP[] + codemap(pin)
    port_defaults.c      — port_set_default_options() for factory reset
```

Four files. Copy the closest existing port (usually `ports/pico/` for RP2040 variants or `ports/pico_rp2350/` for RP2350), then change the values.

## Step A1 — port_config.h

All port-config macros have the `HAL_PORT_` prefix. They are **values** — core reads them inside C expressions and array sizes, never as preprocessor gates.

Start from `ports/pico/port_config.h` (the simplest). Key knobs, grouped:

- **Chip identity:** `HAL_PORT_PWM_SLICE_COUNT`, `HAL_PORT_GPIO_COUNT`, `HAL_PORT_PIO_COUNT`, `HAL_PORT_HAS_PIO2`, `HAL_PORT_HAS_FAST_TIMER`, `HAL_PORT_HAS_INT5`.
- **Chip features present:** `HAL_PORT_HAS_PSRAM`, `HAL_PORT_HAS_UPNG`, `HAL_PORT_HAS_DEFINES`, `HAL_PORT_HAS_MP3`.
- **Board features:** `HAL_PORT_HAS_HEARTBEAT`, `HAL_PORT_HAS_SSD1963`, `HAL_PORT_HAS_HDMI`, `HAL_PORT_HAS_NEXTGEN_DISPLAY`, `HAL_PORT_IS_VGA`.
- **Decascade palette flags:** `HAL_PORT_HAS_WIFI`, `HAL_PORT_HAS_VGA_PIO`, `HAL_PORT_HAS_GUICONTROLS`. Drive WiFi-stack inclusion, VGA-PIO scanout family, and the GUI widget command set (drivers/gui_touch/Touch.c + Option.MaxCtrls). Pick 0 / 1 per your board's hardware and feature set.
- **Numeric caps:** `HAL_PORT_ADC_CHANNEL_MAX`, `HAL_PORT_FILES_MAX`, `HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ`, `HAL_PORT_AUDIO_MOD_BUFFER_SIZE`.
- **Memory + clock + MMBasic-table:** `HAL_PORT_HEAP_MEMORY_SIZE`, `HAL_PORT_MAX_CPU`, `HAL_PORT_MIN_CPU`, `HAL_PORT_MAX_VARS`, `HAL_PORT_MAX_SUBFUN`, `HAL_PORT_MAX_MODES` (VGA-family ports only), `HAL_PORT_FLASH_TARGET_OFFSET[_USB]`, `HAL_PORT_MAGIC_KEY[_USB]`, `HAL_PORT_HEAP_TOP[_USB]`, `HAL_PORT_CONSOLE_RX_BUF_SIZE` (non-WiFi ports), `HAL_PORT_PIOMAX`, `HAL_PORT_NBR_PINS`, `HAL_PORT_PSRAM_BASE` + `HAL_PORT_PSRAM_BLOCK_SIZE` (rp2350 non-WEB ports). The `_USB` siblings let configuration.h pick the USB-keyboard variant via a single USBKEYBOARD ifdef.
- **Memory layout:** `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES`, `HAL_PORT_ALLMEMORY_ALIGN`, `HAL_PORT_CORE1_STACK_WORDS` (size of the shared core1 stack — 512 for ports running the SPI-LCD merge pipeline, 128 for VGA/HDMI scanout, 1 for WEB ports that never launch core1).
- **Flash-vs-RAM placement macros:** `PORT_RAM_FUNC`, `MMB_HOT_FUNC`, `MMB_DISPATCH_FUNC`. Expand to `__not_in_flash_func(name)` where RAM is available; plain `name` on RAM-constrained variants (rp2040 WEB, rp2040 VGA).
- **Per-board references:** `HAL_PORT_LCD_SPI_CLK_PIN` (which `Option.*` field carries the LCD SPI clock pin), `HAL_PORT_CONSOLE_FONT_MEDIUM` (symbol name for the medium console font).
- **Port hooks:** `HAL_PORT_RANDOMIZE_DEFAULT_SEED()`, `BC_CRASH_INFO_ATTR`.

If a macro doesn't apply to your board, define it to `0` (or the null form). The point is that every port defines every macro — core code reads unconditionally.

### `HAL_PORT_FLASH_TARGET_OFFSET` — must beat `binary_end`

This is the most common bring-up trap on rp2350. `HAL_PORT_FLASH_TARGET_OFFSET` is where MMBasic writes saved Options on first boot. It **must** be larger than the firmware's actual `binary_end`, plus a guard band (≥30 KB recommended) to absorb future toolchain / SDK growth. If the saved-options sector overlaps the firmware image, the first `SaveOptions()` call stomps the bootrom's `IMAGE_DEF` Block 2 marker — the device then drops to BOOTSEL on every reboot, looking like a bad flash. `picotool info <flashed device>` reports `Block loop is not valid`. After verifying the offset, re-flash and the next first-boot succeeds. See [flash-layout-note.md](flash-layout-note.md) for the full mechanics.

How to check before first flash:

```bash
picotool info build_<TARGET>/PicoMite.uf2 | grep "binary end"
# Compare the "binary end" hex against your HAL_PORT_FLASH_TARGET_OFFSET.
# binary_end - 0x10000000 must be < HAL_PORT_FLASH_TARGET_OFFSET by ≥ 30 KB.
```

### `HAL_PORT_HEAP_MEMORY_SIZE` — keep it flash-sector aligned

`HAL_PORT_HEAP_MEMORY_SIZE` normally becomes `HEAP_MEMORY_SIZE`, and unless a
port overrides it, `MAX_PROG_SIZE` follows that value. Program save/load paths
erase `MAX_PROG_SIZE` bytes at a time, so device builds need the heap size to be
a multiple of the flash erase sector size, currently 4096 bytes.

Non-aligned values can build and boot but later fail on hardware with errors
such as `Flash erase problem` when `RUN "A:..."`, `LOAD`, or flash-slot
operations try to erase the program area. Use whole-KiB values that are also
4 KiB multiples, for example `(200 * 1024)` rather than `(202 * 1024)`.

## Step A2 — pin_tables.c

**Background: `PinDef[]` and the two numbering systems.** MMBasic has two ways of naming a pin in user code: the physical *package pin number* (1, 2, 4, 5, … — what's silkscreened on the header), and the *GPIO number* (`GP0`, `GP1`, `GP2`, … — what the chip datasheet calls the line). BASIC syntax accepts either. Internally, everything goes through `PinDef[]` — an array indexed by package-pin number. Each entry carries `{pin, GPno, pinname, mode, ADCpin, slice}`: the chip's GPIO number, the printable label, a bitmask of supported modes (`DIGITAL_IN`, `PWM4A`, `I2C0SDA`, `SPI1SCK`, …), the ADC channel number if any, and the PWM slice number. `PinDef[]` lives in `PicoMite.c`. As a new-port author you're not redefining `PinDef[]`, you're supplying two smaller lookup tables that reference it.

**PINMAP** is the GPIO-number → package-pin-number map: `PINMAP[gpio]` gives the `PinDef[]` index for that GPIO. One entry per GPIO on your chip (30 on RP2040, 48 on RP2350). When a BASIC program writes `SETPIN GP0, DOUT`, the parser resolves `GP0` to GPIO 0, then the runtime calls `codemap(0)` → `PINMAP[0]` → the `PinDef[]` slot for whichever header pin GP0 is wired to.

**codemap** is the validated accessor:

```c
int codemap(int pin)
{
    if (pin > <HAL_PORT_GPIO_COUNT - 1> || pin < 0) error("Invalid GPIO");
    return (int)PINMAP[pin];
}
```

Bounds-check the GPIO, error if out of range, return the `PinDef[]` slot. The bound is your chip's GPIO count, not its package pin count.

**Worked example — `ports/pico/pin_tables.c`:**

```c
const uint8_t PINMAP[30] = {
     1,  2,  4,  5,  6,  7,  9, 10, 11, 12,   // GP0..GP9
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,   // GP10..GP19
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44    // GP20..GP29
};
```

Read line by line: GPIO 0 is on package pin 1, GPIO 1 is on package pin 2, GPIO 2 is on package pin 4 (pin 3 is GND, so the sequence skips it), and so on. The "skipped" numbers are the GND / VSYS / RUN / etc. pins on the board header.

**How to build PINMAP for a new board:**

1. Open `PicoMite.c` and find the `const struct s_PinDef PinDef[] = {...}` definition. Skim it — each entry's comment gives the package-pin number.
2. For GPIO 0, find the `PinDef[]` entry whose `GPno` field is 0. Its `pin` field is what goes in `PINMAP[0]`.
3. Repeat for every GPIO 0..`HAL_PORT_GPIO_COUNT - 1`.
4. If your board routes a GPIO to something other than a user header (e.g. onboard LED, QSPI to flash, CYW43 SDIO on PicoW), that GPIO still gets a `PinDef[]` slot — just one marked `UNUSED` or with a special label. It still needs a `PINMAP[]` entry pointing there.

**Why `codemap()` matters:** it's the single point of translation between GPIO-numbered BASIC syntax (`SETPIN GP0, DOUT`) and `PinDef[]`. If your `PINMAP` is wrong, every GPIO-numbered BASIC statement on that pin silently targets the wrong `PinDef[]` slot — the mode-mask check will refuse legal operations, the PWM slice will come back wrong, `PIN READ` will read a different line. There's no secondary cross-check; verify PINMAP against `PinDef[]` by hand before first boot.

**Pitfall to watch:** if you change `PinDef[]` to add a new pin entry for your board, the package-pin indices after the insertion point shift and every `PINMAP[]` entry that pointed past the insertion needs updating. Safer to append new `PinDef[]` entries at the end rather than inserting into the middle.

### Pitfall — `PinDef[]` is indexed *by array position*, not by the row's `pin` field

`PINMAP[gpio]` returns a value that gets passed straight to `PinDef[<value>]` — i.e. used as the C array index. The `pin` integer that's the first field of each `PinDef` row is just *data* inside the struct; it doesn't drive the C lookup. They line up only if your `PinDef[]` is contiguous (row 0 is `PinDef[0]`, row 1 is `PinDef[1]`, …) and every numeric `pin` value in those rows matches its array position.

This bites if you build `PinDef[]` from `PINDEF_BLOCK_*` macros and **omit a block in the middle**. For example:

```c
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,        // rows with pin=0..15
    PINDEF_BLOCK_PINS_16_25_HDMI,          // rows with pin=16..25
    PINDEF_BLOCK_PINS_26_40,                // rows with pin=26..40
    /* PINDEF_BLOCK_PSEUDO_GP23_29 omitted because CYW43 owns these */
    PINDEF_BLOCK_PSEUDO_RP2350_EXTRAS,     // rows with pin=45..62
};
```

The C array is now positions `0..40` followed by `41..58` — but the **rows at positions 41..58 carry `pin=45..62` in their `pin` field**, not `pin=41..58`. If `PINMAP` is the standard `pico_sdk_common` table that assumes the GP23-29 block is present, `PINMAP[33] = 48`. Then `PinDef[48]` returns the row whose `pin` field is `52` (i.e. GP37), not GP33. Every `OPTION ... GP33` silently drives GP37, every `SETPIN GP30..GP47` is off by four pins, and the only symptom is "wrong GPIO toggles" — no error, no fault.

Two ways to handle blocks you don't want exposed:

1. **Keep the block as placeholders** so `PinDef[]` array positions stay in 1:1 sync with the `pin` field. Marking the rows `UNUSED` is fine — they just need to exist so positions 41-44 (etc.) are filled. This is what hdmi_rp2350 does.
2. **Renumber `PINMAP[]` to match the gap.** Tedious, easy to get wrong, and breaks if you later add another port that shares the same `PINMAP` template. Prefer option 1.

`PINDEF_BLOCK_PSEUDO_GP23_29` exists for exactly this reason on WiFi ports — the radio owns those GPIOs at runtime, but the rows must stay in `PinDef[]` to keep the `PINMAP` indexing dense.

**How to detect this in advance:** add a build-time assertion that walks `PinDef[]` and checks `PinDef[i].pin == i` for every `i`. If your port can't satisfy that, your `PINMAP[]` is fragile.

### Pitfall — `Option.AUDIO_SLICE` / saved-Option fields cache `PinDef[]` lookups

`OPTION AUDIO I2S GPxx, GPxx` runs `checkslice()` on the user's pins, stores the result into `Option.AUDIO_SLICE`, and `SaveOptions()` writes it to the options sector in flash. If `PinDef[]` is wrong at the moment the user issues that command, **a garbage slice number gets persisted** — and surfaces on every subsequent boot as silent audio (best case: slice 0, which collides with whatever else uses slice 0) or hard fault (worst case). Reflashing the firmware doesn't fix it because the bad value lives in the options sector, not in the binary.

The same applies to any other `Option.*` field computed from a `PinDef[]` lookup at OPTION-set time. After fixing a `PinDef[]` / `PINMAP` bug, instruct users (or your own test scripts) to **re-issue the relevant `OPTION ...` command** so the saved blob is regenerated against the correct table.

### Pitfall — RP2350 XIP cache survives `sysresetreq`

When debugging a freshly-reflashed RP2350 image and seeing inexplicable `UNDEFINSTR` HardFaults at addresses that decode to valid instructions in the new ELF, suspect the XIP cache. The cache holds bytes from the *previous* firmware and `sysresetreq` (the SoftReset path) does not invalidate it; only a power-cycle or an explicit `xip_cache_invalidate_all()` does. Compare the cached read at `0x10xxxxxx` against the uncached alias at `0x14xxxxxx` (XIP_NOCACHE_NOALLOC) — if they disagree, the cache is stale. Via OpenOCD: write byte 0 to each address in `0x18FFC000..0x18FFFFF8` step 8 to issue invalidate-by-set-way for the whole 16 KB cache, then `reset run`. This is a debug-environment artifact only — once a real power cycle happens, normal operation is fine.

## Step A3 — port_defaults.c

This file owns the **per-port behaviour core code calls into**. Core
calls each entry point unconditionally (no #ifdef gates in core), so
**every port must define every entry point** — provide a no-op stub
when the behaviour isn't relevant to your hardware.

### Single-board vs multi-board ports

Before you start: decide which kind of port you're writing.

- **Multi-board port** — one firmware binary, multiple physical boards
  the user picks between with `OPTION RESET <BOARD>`. The existing
  `ports/pico/`, `ports/hdmi_rp2350/`, etc. are this kind: each ships
  a `port_factory_reset_board()` that's a long `if (checkstring(p,
  "GAMEMITE")) … else if (checkstring(p, "PICOCALC")) …` ladder of
  per-board profiles, plus a `port_print_supported_boards()` that
  lists them all for `CONFIGURE LIST`. This pattern made sense for
  upstream PicoMite, which shipped one binary across many community
  boards.

- **Single-board port** — one firmware binary for one specific PCB.
  The user never picks a profile because there's nothing to pick
  between. `port_factory_reset_board()` is `return 0;`,
  `port_print_supported_boards()` is empty (or prints a single name
  for diagnostics), `port_set_default_options()` is one unconditional
  block applying that board's defaults. **No `if (checkstring(...))`
  ladder.**

If you're building a custom board for yourself or a small audience,
you almost certainly want single-board. The multi-board mechanism is
genuinely useful only when you're shipping ONE firmware to multiple
physically different boards and need runtime board selection.

**Don't copy a multi-board port_defaults.c and trim it** — you'll
inherit conditionals you don't need. Write the file from scratch
using the entry-point list below; total length for a single-board
port is ~150 lines, mostly the display-OPTION setter and the
console-colour helper.

### Required exports

| Function | What it does |
|---|---|
| `void port_set_default_options(void)` | First-boot factory defaults. Set `Option.*` fields that differ from the shared `ResetOptions()` baseline (display type, CPU speed, keyboard config, board-specific pin assignments). Called by `core/mmbasic/FileIO.c::ResetOptions()` after the shared defaults run. |
| `void port_print_supported_boards(void)` | What `CONFIGURE LIST` prints. One `MMPrintString("YourBoard\r\n")` per board profile this port ships. |
| `int port_factory_reset_board(unsigned char *p)` | Body of `OPTION RESET <BOARD>`. Match `p` against each board name your port supports; on match, set `Option.*` fields, `SaveOptions()`, `SoftReset()`, return 1. Return 0 if no board name matched. |
| `int port_display_option_setter(unsigned char *cmdline)` | Per-port `OPTION` setters for display config. SPI-LCD ports handle `OPTION CPUSPEED`, `OPTION LCDPANEL`, `OPTION TOUCH`. VGA/HDMI ports handle `OPTION RESOLUTION`, `OPTION VGA PINS`, `OPTION DEFAULT MODE`. Return 1 if `cmdline` matched a setter, 0 otherwise. |
| `void port_clear_lcd_spi_if_shares_system(void)` | Hook called when reassigning the SYSTEM SPI pins. Clears LCD-SPI state on ports where the LCD shares SYSTEM SPI; no-op on ports with a dedicated LCD SPI bus or no LCD. |
| `int port_pinno_alias_for_name(const char *name)` | `MM.PINNO`-style name-to-pin aliasing. Return non-zero pin number for a recognised alias (e.g. WiFi ports map `GP23..GP29` → virtual pins 41-44 because CYW43 owns those GPIOs). Return 0 for "no alias." |
| `int port_pin_is_reserved_alias(int pin)` | Companion to the above — return non-zero if `pin` is a board-reserved alias number. |
| `const char *port_pin_reserved_label(int pin)` | Companion — return human-readable label for a reserved pin (e.g. `"Boot Reserved : CYW43"`), or `NULL`. |
| `void port_apply_default_console_colors(int default_fc, int default_bc)` | `OPTION LCDPANEL CONSOLE` colour reset hook. VGA-family ports pre-fill the tile-colour arrays so existing tiles render in the new colours; non-VGA ports stub it. |

VGA-family ports also export `void VGArecovery(int pin)` — called from
`drivers/sd_spi/mmc_stm32.c` when SD-card pin reassignment needs to
reclaim a VGA-PIO pin. Copy the body verbatim from the closest existing
VGA-family port_defaults.c if your port has `HAL_PORT_HAS_VGA_PIO=1`.

WiFi-family ports also need to declare which GPIOs the CYW43 radio
reserves — `port_pinno_alias_for_name`, `port_pin_is_reserved_alias`,
`port_pin_reserved_label` map `GP23/24/25/29` (the standard CYW43 SPI
pins on pico_w / pico2_w / pimoroni_pico_plus2_w) to virtual pins
41-44. Copy from `ports/web_rp2350/port_defaults.c`.

For a concrete walkthrough of writing each of these for a custom
single-board port, see the worked example below.

### `port_set_default_options` — populate every Option your hardware uses

The shared `ResetOptions()` baseline only covers options every port
needs (`Tab`, `Baudrate`, `heartbeatpin`, `Magic`, …). Anything specific
to your hardware must be set in `port_set_default_options()` — the
universal Option struct exposes every field on every build, but a
field zeroed at boot means the corresponding hardware path doesn't
work even if the driver code is linked.

The fields that bit hardest during dvi_wifi bring-up:

- **HDMI lane mapping** (`Option.HDMIclock / HDMId0 / HDMId1 / HDMId2`).
  The HSTX peripheral maps logical lanes to physical pins via these
  fields. Default zero means every lane drives GPIO 0 — no DVI signal
  at the actual HDMI pins. Standard pico_w-style breakouts use
  `HDMIclock=2, HDMId0=0, HDMId1=6, HDMId2=4`. Copy from
  `ports/hdmi_rp2350/port_defaults.c`.

- **USB-host keyboard config** (`Option.USBKeyboard`, `SerialConsole`,
  `SerialTX/RX`, `capslock`, `numlock`, `ColourCode`). Required when
  `HAL_PORT_KEYBOARD_USB_HOST=1`. Without them the keyboard layout is
  unset and key events are garbled.

- **Heartbeat pin policy.** On boards where the user LED is owned by
  CYW43 (any rp2350 + RM2 / pico2_w) there's no user-pickable pin —
  set `Option.NoHeartbeat = 1` in your defaults.

Cross-check against the gold reference for your port shape:
`ports/hdmi_rp2350/port_defaults.c` for HDMI ports,
`ports/pico_rp2350/port_defaults.c` for SPI-LCD + USB-host,
`ports/web_rp2350/port_defaults.c` for WEB. If your defaults block is
shorter than those, you're probably missing fields.

### The runtime/compile-time dual gate

Because the Option struct is universal, `OPTION X = Y` always parses
on every port. But the *driver* behind that option may not be linked
on your build — in which case the runtime accepts the input and
silently does nothing. This affects PSRAM, WiFi, HDMI, USB-host
keyboard, I²S audio, MP3 decoding, GUI controls, and 3D graphics.

When picking driver linkage in `port_sources.cmake` (next step), make
sure every Option field your factory profile sets has the driver
behind it linked into your build. If you're a single-board port and
won't expose those options anyway, you don't need the driver linked.

**Per-port `#ifdef` is fine in this file.** The HAL purity gate only
enforces `core/*.c` and `hal/*.h`. Port-implementation files like
`port_defaults.c` may use `#ifdef USBKEYBOARD`, `#ifdef PICOMITEVGA`,
etc. for body-shaped divergence — that's the right scope for them.
But if you're writing a single-board port, you usually don't need any
ifdefs at all.

## Step A4 — port_sources.cmake

Each port directory ships a `port_sources.cmake` snippet that owns the
source list AND the per-port build config (compile flags, link
libraries, SDK feature toggles). Top-level `CMakeLists.txt` includes
exactly one snippet per build — `include(${CMAKE_SOURCE_DIR}/ports/${PORT}/port_sources.cmake)`,
where `PORT` is the directory name. You do **not** edit a central
driver-selection ladder anymore — there isn't one.

Copy the closest existing snippet (`ports/pico/port_sources.cmake` for
RP2040 SPI-LCD, `ports/hdmi_rp2350/port_sources.cmake` for an
HDMI-family board, `ports/web_rp2350/port_sources.cmake` for a
WiFi-family board) and adjust two sections:

1. **Source list** — `target_sources(PicoMite PRIVATE …)` lists every
   driver and stub your port links. Driver flavours come in pairs
   (real impl vs no-op stub); pick one per axis. Existing axes:
   `display_merge_pico` vs `display_merge_stub`, `vm_framebuffer_picomite`
   vs `vm_framebuffer_stub`, `audio_mp3_real` vs `audio_mp3_stub`,
   `psram_heap_real` vs `psram_heap_stub`, `upng_sprite` vs
   `upng_sprite_stub`, `gui_touch` vs `gui_touch_stub`, `spi_lcd_fastgfx`
   vs `spi_lcd_fastgfx_stub`. Add `shared/net/MMweb_stubs.c` if your port doesn't
   link the WiFi stack, `gfx_3d.c` if it does link the 3D family
   (everyone except WiFi), `drivers/ssd1963/SSD1963.c` + `drivers/gui_touch/Touch.c` if you have an
   SPI-LCD touch panel, the `drivers/vga_pio/*` family if your port has
   `HAL_PORT_HAS_VGA_PIO=1`, the `drivers/hdmi/*` files if HDMI=1, etc.

2. **Per-port build config** — at the bottom of the snippet, add the
   `target_compile_options`, `target_link_libraries`, and SDK feature
   toggles your board needs:
     - `-DPICO_HEAP_SIZE=…` and `-DPICO_CORE0_STACK_SIZE=…`
     - `-Drp2350` (rp2350 ports only)
     - `-DHAL_PORT_DEVICE_NAME="…"`
     - USB-host keyboard: set `HAL_PORT_HAS_USB_KEYBOARD 1` in
       `port_config.h`, link `tinyusb_host` + `tinyusb_board`,
       `Pico_enable_stdio_usb(PicoMite 0)`. PS/2 ports leave
       `HAL_PORT_HAS_USB_KEYBOARD 0` and call
       `Pico_enable_stdio_usb(PicoMite 1)`.
     - `target_link_libraries(PicoMite pico_multicore)` (most ports
       except WEBRP2350)
     - `target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)`
       (WiFi ports). **Tune `CYW43_PIO_CLOCK_DIV_INT` to your `clk_sys`** —
       the SDK default of 2 is calibrated for the stock 150 MHz pico_w
       (gSPI ~37 MHz). Higher `clk_sys` overruns the CYW43's ~50 MHz gSPI
       spec and the join handshake stalls at `WIFI_JOIN_STATE_ACTIVE`.
       Rule of thumb: `divider = ceil(clk_sys_mhz / 75)`. e.g. 200 MHz
       → 3, 252/315 MHz → 4. Add `-DCYW43_PIO_CLOCK_DIV_INT=N` to your
       compile options.
     - `pico_set_float_implementation(PicoMite pico_dcp)` (rp2350)
     - `pico_define_boot_stage2(slower_boot2 …)` (rp2040)

### Scanout DMA channels must be claimed before launching core1

If your port runs HDMI HSTX or VGA-PIO scanout on core1 *and* links
the WiFi stack on core0, the cyw43-driver's bus PIO calls
`dma_claim_unused_channel()` and will silently pick whichever scanout
channel hasn't been claimed via the SDK's software-tracking bitmap —
stomping pixel pump traffic. The HDMI and VGA-PIO scanout drivers'
`port_main_launch_core1` already call `dma_channel_claim()` for their
ping-pong channels (`drivers/hdmi/hdmi_scanout.c`,
`drivers/vga_pio/vga_qvga_modes.c`). If you write a new scanout
driver, do the same — claim before launch.

No top-level edit is needed for new ports — `cmake -DPORT=<your_board>`
picks up `ports/<your_board>/port_sources.cmake` directly. Only the
chip-platform / `PICO_BOARD` lookup at the top of `CMakeLists.txt`
needs an entry if your board uses a non-default platform or board
file (see Step 3 in the worked example).

The legacy `-DCOMPILE=<TARGET>` enum still works for the historical
ports (PICO, HDMIUSB, etc.) via a backwards-compat shim in
`CMakeLists.txt`, but new ports should pass `-DPORT=<dir>` directly
and not extend the COMPILE→PORT inference table.

Finally add your port's directory name to `buildall.sh` under
`TARGETS=(...)` so the local + CI buildall gate exercises it.

## Step A5 — acceptance

A hardware port is done when **all** of these pass:

1. `tools/check_hal_purity.sh` is green (no regression in core/ or hal/).
2. `./ports/host_native/run_tests.sh` is 239/239 passed + `HAL purity gate: clean`.
3. `./buildall.sh` builds every target including yours, clean.
4. Your target boots to the MMBasic prompt on real hardware.

---

## Worked example: custom RP2350 + HDMI + PSRAM board

Exact recipe for a hypothetical board called **`mymite`** — RP2350B
(80-pin), HDMI display via DVI scanout, external QSPI PSRAM module.
Closest existing port: `ports/hdmi_rp2350/` (HDMI + PSRAM, RP2350B,
no WiFi, no USB-keyboard variant). We'll copy and adjust.

### Step 1 — copy the closest port

```bash
cp -r ports/hdmi_rp2350 ports/mymite
```

You now have:

```
ports/mymite/
    port_config.h
    port_sources.cmake
    pin_tables.c
    port_defaults.c
    mmbasic_port_hdmi.c   # HDMI-specific MMBasic port hook
```

**If your board adds a feature the closest port doesn't have** (WiFi
in particular, but the same applies to any axis): you're splicing
two ports, not copying one. Pick the second port that has the
missing feature and merge into the four files above.

For HDMI + WiFi specifically (`ports/dvi_wifi_rp2350/` is a worked
example of this exact splice), you start from `ports/hdmi_rp2350/`
and copy in:
- WiFi stack source list from `ports/web_rp2350/port_sources.cmake`
  (cJSON / mqtt / MMMqtt / MMTCPclient / MMtelnet / MMntp /
  MMtcpserver / tftp / MMtftp / MMudp / MMsetwifi)
- `pico_cyw43_arch_lwip_poll` link library
- `-DCYW43_HOST_NAME` and `-DPICO_CYW43_ARCH_POLL` compile options
- CYW43-pin alias hooks from `ports/web_rp2350/port_defaults.c`
  (`port_pinno_alias_for_name` / `port_pin_is_reserved_alias` /
  `port_pin_reserved_label` mapping `GP23/24/25/29` → 41-44)
- `Option.ServerResponceTime = 5000;` in `port_set_default_options`
- `HAL_PORT_HAS_WIFI 1` in `port_config.h`
- Drop `shared/net/MMweb_stubs.c` and `drivers/ssd1963/SSD1963.c`/`drivers/gui_touch/Touch.c` from the source list
  (drivers/gui_touch/Touch.c references `Option.TOUCH_XSCALE` which doesn't exist on
  PICOMITEVGA's `struct option_s` layout)
- Trim `HAL_PORT_HEAP_MEMORY_SIZE` by ~30-60 KB to make room for
  CYW43 + lwIP buffers (HDMI alone uses 184 KB; with WiFi added,
  ~120 KB fits cleanly on RP2350's 520 KB SRAM)

### Step 2 — pick a port-directory name

Convention is lowercase snake_case: `mymite`. This is the directory
name under `ports/`, the value users pass to `cmake -DPORT=mymite`,
and what appears in `buildall.sh`'s TARGETS list.

### Step 3 — wire `mymite` into the platform/board lookup

`CMakeLists.txt` has a per-port platform/board lookup near the top.
You only need to extend it if your board is on RP2350 and uses a
different `PICO_BOARD` than the default `pimoroni_pga2350`:

```cmake
set(_RP2350_PORTS pico_rp2350 vga_rp2350 web_rp2350 hdmi_rp2350
                  vga_wifi_rp2350 dvi_wifi_rp2350 mymite)   # add yours

if (PORT IN_LIST _RP2350_PORTS)
    set(PICO_PLATFORM rp2350)
    if (PORT STREQUAL "web_rp2350" OR PORT STREQUAL "vga_wifi_rp2350")
        set(PICO_BOARD pico2_w)
    elseif (PORT STREQUAL "mymite")                       # your branch
        set(PICO_BOARD pimoroni_pico_plus2_w_rp2350)      # if RP2350B + CYW43
    else()
        set(PICO_BOARD pimoroni_pga2350)                  # default RP2350B + no CYW43
    endif()
endif()
```

If your board uses the default `pimoroni_pga2350` (RP2350B no CYW43),
just add the directory name to `_RP2350_PORTS` and you're done — no
elseif branch needed.

**Picking the right `PICO_BOARD`** (this is critical and undermentioned
elsewhere — pico-sdk uses the board file to define chip variant,
default LED, USB IDs, and especially `CYW43_*` pin assignments and
`PICO_CYW43_SUPPORTED`):

| Your hardware | `PICO_BOARD` to use |
|---|---|
| RP2350**A** (30-pin), no WiFi | `pimoroni_pga2350` (or `pico2`) |
| RP2350**B** (80-pin), no WiFi | `pimoroni_pga2350` |
| RP2350**A** + CYW43 (Pico 2 W) | `pico2_w` |
| RP2350**B** + CYW43 (Pico Plus 2 W, RM2 module) | `pimoroni_pico_plus2_w_rp2350` |
| Custom board with non-standard CYW43 wiring | Custom board file (drop a `.h` in `boards/include/boards/` and override `CYW43_DEFAULT_PIN_WL_*`) |

For RP2350B + RM2 specifically, `pimoroni_pico_plus2_w_rp2350` is the
right stock SDK board file: `PICO_RP2350A 0` (selects 80-pin chip),
CYW43 SPI on GP23/24/25/29 (matches RM2 module's standard pinout),
and SDK's `cyw43_arch` library is built (because the board file calls
`pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)`). You don't need to
write a custom board file unless your PCB wires CYW43 to different
GPIOs.

### Step 4 — adjust `port_config.h`

Open `ports/mymite/port_config.h`. The HDMI base values are mostly
correct for your scenario. You'll typically only need to:

```c
/* Update if your board is RP2350A (30 GPIO) instead of RP2350B (48): */
#define HAL_PORT_GPIO_COUNT              48      /* 30 for RP2350A */
#define HAL_PORT_NBR_PINS                62      /* 44 for RP2350A */

/* PSRAM region — usually unchanged across RP2350+PSRAM boards: */
#define HAL_PORT_PSRAM_BASE              0x11000000
#define HAL_PORT_PSRAM_BLOCK_SIZE        0x1C0000

/* Heap budget — depends on your PSRAM size + board layout. The
 * existing HDMI port uses 184 KB for the BASIC heap with a
 * 153 KB framebuffer trailer (320*240*16-bit). If your board has
 * a much bigger PSRAM module you can increase HEAP_MEMORY_SIZE; if
 * tighter, decrease. */
#define HAL_PORT_HEAP_MEMORY_SIZE        (184 * 1024)

/* Magic key — pick something unique to your board. The save area
 * stamps this on first boot; if a saved-options blob doesn't match
 * (because the layout changed or the binary is from a different
 * port), MMBasic re-initialises Option from defaults. */
#define HAL_PORT_MAGIC_KEY               0xAB12CD34
#define HAL_PORT_MAGIC_KEY_USB           0xAB12CD34   /* same if no USB variant */
```

Everything else (PIO claims, fast-timer, MP3 decoder, FLAC sample-rate
cap, scanout core1 stack words, framebuffer trailer size) usually
stays the same as `hdmi_rp2350`'s values for an HDMI+PSRAM board.

### Step 5 — write `pin_tables.c` for your board

This is the **only file that requires real per-board work**. It maps
GPIO numbers to the package pins your board exposes on its header.

For an off-the-shelf RP2350B reference board, the `PINMAP[]` from
`ports/hdmi_rp2350/pin_tables.c` (which uses the pimoroni_pga2350
package layout) often works as-is. For a custom PCB:

1. Open `PicoMite.c` and find `const struct s_PinDef PinDef[]`.
2. For each GPIO 0..47, identify which `PinDef[]` slot it should
   point at. Most custom RP2350 boards reuse the standard package
   pin layout, so `PINMAP[gpio] = PinDef[].pin` lookup is mechanical.
3. Pin-out anything board-specific. Example: if your board routes
   GP12 → an HDMI pin pair (DVI clock+/clock-), make sure
   `PinDef[]`'s entry for the corresponding package pin marks that
   GPIO as a PIO output (the existing PinDef entries already do this
   for the standard pin map).

Verify by hand: every `PINMAP[gpio]` entry should point to a `PinDef[]`
slot whose `GPno` field matches that `gpio`. Mistakes here silently
break GPIO-numbered BASIC syntax — see "Why codemap() matters" in
Step A2.

### Step 6 — write `port_defaults.c` for your board

Three things to customise in the copied `hdmi_rp2350` version:

1. **`port_set_default_options`** — set Option fields whose factory
   value differs from the shared default. For an HDMI board:

   ```c
   void port_set_default_options(void)
   {
       Option.DISPLAY_TYPE = SCREENMODE1;
       Option.HDMIclock = 0;        /* HDMI clock pair on GP12+13 */
       Option.HDMId0    = 1;        /* GP14+15 */
       Option.HDMId1    = 2;        /* GP16+17 */
       Option.HDMId2    = 3;        /* GP18+19 */
       Option.X_TILE = 80;
       Option.Y_TILE = 30;
       Option.CPU_Speed = Freq378P;
       Option.KeyboardConfig = NO_KEYBOARD;
       Option.SSD_RESET = -1;
   }
   ```

2. **`port_print_supported_boards`** — what `CONFIGURE LIST` prints:

   ```c
   void port_print_supported_boards(void)
   {
       MMPrintString("MYMITE\r\n");
   }
   ```

3. **`port_factory_reset_board`** — what `OPTION RESET <BOARD>`
   accepts. Add one entry per board profile your firmware ships:

   ```c
   int port_factory_reset_board(unsigned char *p)
   {
       if (checkstring(p, (unsigned char *)"MYMITE")) {
           ResetOptions(false);
           Option.CPU_Speed     = Freq378P;
           Option.HDMIclock     = 0;
           Option.HDMId0        = 1;
           Option.HDMId1        = 2;
           Option.HDMId2        = 3;
           Option.SD_CS         = PINMAP[20];
           Option.SD_CLK_PIN    = PINMAP[18];
           Option.SD_MOSI_PIN   = PINMAP[19];
           Option.SD_MISO_PIN   = PINMAP[16];
           Option.PSRAM_CS_PIN  = 1;       /* GP0 */
           strcpy((char *)Option.platform, "MYMITE");
           SaveOptions();
           printoptions();
           uSec(100000);
           _excep_code = RESET_COMMAND;
           SoftReset();
           return 1;
       }
       return 0;
   }
   ```

Leave the OPTION setters (`port_display_option_setter`, `VGArecovery`,
`port_apply_default_console_colors`, etc.) alone — they're shared
HDMI behaviour and don't usually need per-board tweaking.

### Step 7 — adjust `port_sources.cmake`

The HDMI snippet you copied already has the right source-list
(VGA-PIO scaffolding + HDMI sink + PSRAM heap + audio MP3 +
upng_sprite + gfx_3d + gui_touch_stub + ...). You only need to:

1. **Set the device name string:**

   ```cmake
   target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="MyMite")
   ```

   This appears in the boot banner and `MM.DEVICE$`.

2. **Decide whether to ship a USB-host keyboard variant.** If yes,
   keep the `if (COMPILE STREQUAL "MYMITEUSB")` axis (rename the
   COMPILE label) — same pattern as `hdmi_rp2350`. If no, simplify
   to the PS/2 branch only.

Everything else — `pico_multicore` linkage, `pico_set_float_implementation`,
the `-Drp2350` flag, USB stdio config — stays the same.

### Step 8 — add `mymite` to `buildall.sh`

`buildall.sh` still drives `cmake -DCOMPILE=…` for the historical
multi-board targets, so the TARGETS list is uppercase. Single-board
new ports get added the same way (the COMPILE→PORT inference shim in
`CMakeLists.txt` falls through to a `FATAL_ERROR`, so for a new port
you instead want buildall.sh to call `-DPORT=mymite` directly).

A simple way: add a small branch in buildall.sh that handles new
single-board PORTs separately, or extend buildall.sh's invocation to
prefer `-DPORT` when the target name doesn't match a legacy COMPILE
value. Either way add the directory-style name to TARGETS:

```bash
TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
    VGAWIFIRP2350 DVIWIFIRP2350
+   mymite
)
```

### Step 9 — first build

Direct `-DPORT` invocation (preferred for new single-board ports):

```bash
mkdir build_mymite && cd build_mymite
cmake -DPORT=mymite -DPICOCALC=false -DPICO_SDK_PATH="$HOME/pico/pico-sdk" ..
make -j8
```

Expect either a clean `PicoMite.uf2` or a focused error. The most
common first-build failures and what they mean:

- **`undefined reference to <symbol>`** — your snippet's source list
  is missing a file. Cross-reference `ports/hdmi_rp2350/port_sources.cmake`
  for what your closest sibling links.
- **`region 'RAM' overflowed by N bytes`** — `HEAP_MEMORY_SIZE` +
  `FRAMEBUFFER_TRAILER_BYTES` + CYW43/lwIP stack (if WiFi) > available
  SRAM. Reduce `HEAP_MEMORY_SIZE` in `port_config.h`.
- **`region 'FLASH' overflowed by N bytes`** — your firmware image
  is bigger than the flash region above `FLASH_TARGET_OFFSET`.
  Either increase `HAL_PORT_FLASH_TARGET_OFFSET` (eats into save
  region) or trim the source list.
- **`'struct option_s' has no member named …`** — you're referencing
  an `Option.*` field that's gated by a target macro you don't have.
  Check the field's definition in `FileIO.h`'s `struct option_s`.

### Step 10 — first boot

```bash
picotool load -v build_mymite/PicoMite.uf2 -fx   # or drag-and-drop
```

Connect a serial terminal (USB-CDC at 115200 baud). You should see:

```
MyMite MMBasic RP2350 Edition V6.00.05
> _
```

If the prompt appears but `MM.DEVICE$` returns the wrong name, your
`-DHAL_PORT_DEVICE_NAME` got overridden somewhere — re-check
`port_sources.cmake`. If `OPTION RESET MYMITE` doesn't resolve,
`port_factory_reset_board` isn't matching the string — check spelling
and `checkstring()`'s case-sensitivity (it's case-insensitive).

### Step 11 — verify acceptance

From the repo root:

```bash
./ports/host_native/run_tests.sh                   # 239/239 expected
SKIP_HAL_PURITY=1 ./buildall.sh       # all 14 targets clean
```

Your port being in `buildall.sh`'s TARGETS list means a regression in
any other port that breaks yours blocks `main`.

### Common decisions for an HDMI + PSRAM board

| Question | HDMI alone | HDMI + WiFi (RM2) |
|---|---|---|
| Set `HAL_PORT_HAS_HDMI`? | `1` | `1` |
| Set `HAL_PORT_HAS_VGA_PIO`? | `1` (HDMI rides on the VGA-PIO scaffolding) | `1` |
| Set `HAL_PORT_HAS_PSRAM`? | `1` | `1` (RM2 wires CYW43 off the QSPI pins, so PSRAM stays available — different from `web_rp2350` where CYW43 owns QSPI) |
| Set `HAL_PORT_IS_VGA`? | `1` (PICOMITEVGA family) | `1` |
| Set `HAL_PORT_HAS_WIFI`? | `0` | `1` |
| Set `HAL_PORT_HAS_GUICONTROLS`? | `0` (no touch panel on HDMI) | `0` |
| `HAL_PORT_HAS_HEARTBEAT`? | `1` (board has a user LED) | `0` (CYW43 owns the LED on standard RP2350+CYW43 boards) |
| `HAL_PORT_ADC_CHANNEL_MAX`? | `4` | `3` (GP29 reserved for CYW43 SPI clock → ADC3 unavailable) |
| `core1stack[]` words? | `128` (HDMI scanout uses 512 bytes = 128 words) | `128` |
| `FRAMEBUFFER_TRAILER_BYTES`? | `(320*240*2) = 153600` (HDMI 16-bit QVGA shadow) | same |
| `HAL_PORT_HEAP_MEMORY_SIZE`? | ~184 KB | ~120 KB (CYW43 + lwIP eat ~30-60 KB; trim heap to fit on RP2350's 520 KB SRAM) |
| Link `pico_multicore`? | yes (HDMI scanout runs on core1) | yes |
| Link `pico_cyw43_arch_lwip_poll`? | no | yes |
| `pico_set_float_implementation pico_dcp`? | yes (rp2350) | yes |
| `pico_define_boot_stage2 slower_boot2`? | no (rp2040-only) | no |
| `PICO_BOARD`? | `pimoroni_pga2350` (RP2350B no CYW43) | `pimoroni_pico_plus2_w_rp2350` (RP2350B + CYW43, matches RM2 standard pinout) |

`ports/dvi_wifi_rp2350/` is a complete worked example of the right column.

If (1) or (2) regresses while adding your board, an `#ifdef` has landed in a core or HAL file — the answer is a port-config macro, a port hook, or a driver flavour (see "Ground rules" below), not a widened exemption.

---

# Part B — Simulation port

Simulation ports run MMBasic against a host OS — there's no silicon, no `PinDef[]`, no factory options, no CMake target. Instead a simulation port supplies its own entry point, picks which parts of `host_native` it reuses, and compiles via a standalone Makefile.

The worked examples are:

- **`ports/mmbasic_stdio/`** — pure stdin/stdout runner. Simplest possible case: no framebuffer, no REPL, no editor. The HAL litmus test.
- **`ports/mmbasic_ansi/`** — terminal half-block renderer. Medium complexity: reuses host_native's framebuffer + keyboard, adds its own render thread.
- **`ports/host_native/`** — the canonical macOS/Linux REPL with an in-memory framebuffer canvas. The port everything else inherits from.
- **`ports/host_wasm/`** — browser build via emscripten. Specialised but otherwise uses the same pattern.

## The shape of a simulation port

```
ports/<your_port>/
    Makefile              — standalone build; selects sources + -D flags
    main.c                — entry point (or <name>_main.c)
    <port>_*.c            — port-specific glue (rendering, terminal, etc.)
```

No `port_config.h`, no `pin_tables.c`, no `port_defaults.c`. No top-level CMake integration.

## Step B1 — the Makefile

Every simulation port's Makefile follows the same structure. Copy `ports/mmbasic_stdio/Makefile` for the simplest starting point, or `ports/mmbasic_ansi/Makefile` if you need a framebuffer:

```make
PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
HOST_DIR  := $(REPO_ROOT)/ports/host_native
BUILD_DIR := $(PORT_DIR)/build

# DEVICE_SIM selects the compile-time array sizes (flist[], etc.) to
# mirror. Keep this knob for consistency with host_native.
DEVICE_SIM ?= rp2350
ifeq ($(DEVICE_SIM),rp2040)
  SIM_FLAGS = -DBC_SIM_RP2040
else
  SIM_FLAGS = -DBC_SIM_RP2350
endif

CFLAGS = -std=gnu11 -g -O0 -Wall ... -funsigned-char \
         -DPICOMITE -DMMBASIC_HOST -DMMBASIC_<YOURPORT> $(SIM_FLAGS) \
         -include host_platform.h \
         -I$(PORT_DIR) -I$(HOST_DIR) -I$(REPO_ROOT)
```

Three `-D` flags are non-negotiable:

- **`-DPICOMITE`** — selects the PicoMite (not PicoMiteVGA/WEB/HDMI) BASIC dialect. Simulation ports always use the PicoMite dialect.
- **`-DMMBASIC_HOST`** — routes through host_native's HAL implementations.
- **`-DMMBASIC_<YOURPORT>`** — your own flag. Used to scope port-specific overrides in shared config files (e.g. `configuration.h`, `ffconf.h`).

Then define four source-file groups:

```make
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c ...
BC_SRCS   = runtime/vm/bc_source.c runtime/vm/bc_vm.c runtime/vm/bc_runtime.c ...
NATIVE_SRCS = host_runtime.c host_fs_shims.c ...  # reused from host_native
PORT_SRCS = main.c <port>_*.c                      # your code
```

Start by copying the lists from `mmbasic_stdio` or `mmbasic_ansi`, then trim:

- **Always include** `core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c core/mmbasic/Memory.c core/mmbasic/MMBasic_Print.c`, the shared graphics (`gfx_*_shared.c`), the state (`display_state.c pin_state.c option_state.c audio_state.c`), `core/mmbasic/Draw.c RGB121.c core/mmbasic/FileIO.c`, the bytecode VM (`bc_*.c`), and the filesystem (`ff.c ffunicode.c ffsystem.c`).
- **Omit** `core/mmbasic/Editor.c core/mmbasic/MMBasic_REPL.c core/mmbasic/MMBasic_Prompt.c` if your port has no interactive REPL (like `mmbasic_stdio`).
- **Omit** `host_fb.c host_terminal.c host_main.c host_fastgfx.c` if you're replacing them. mmbasic_ansi keeps `host_fb.c` (reuses the framebuffer) + `host_fastgfx.c` (reuses the VM syscalls) but replaces `host_main.c` (own entry) and omits `host_terminal.c` (own alt-screen handling).
- **Always include the HAL host impls:** `hal_flash_host.c hal_time_host.c hal_pin_host.c hal_storage_host.c hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c`.

If linking fails with undefined references to `Editor_*` / `MMBasic_REPL_*` / display / audio symbols, that's a HAL leak — **fix it in core**, don't add stub files to your port. See `ports/mmbasic_stdio/stdio_runtime.c` for the short list of deliberate no-op stubs it carries (40-ish symbols for framebuffer / fastgfx / editor / terminal that the omitted files used to supply).

## Step B2 — the entry point

Your `main.c` replaces `host_native/host_main.c`. It:

1. Parses argv (your flags + the `.bas` file).
2. Configures display + console state to match your output channel.
3. Sets up `host_output_hook` if you own stdout.
4. Calls `MMBasic_Init` / `MMBasic_PrintBanner`, opens the `.bas` file or connects stdin, and invokes `run_repl` / `run_program` / equivalent.

For a minimum example see `ports/mmbasic_stdio/main.c` (~150 lines). For a framebuffer-renderer example see `ports/mmbasic_ansi/ansi_main.c` (~350 lines, includes resolution-selection + render-thread launch).

### The `host_output_hook` contract

When text would otherwise go to stdout (PRINT output, error messages, `SSPrintString` escape sequences), it first passes through `host_output_hook` if one is installed. This is the canonical way to redirect stdout when your port owns it for another purpose.

The hook is hit at exactly two layers: `SerialConsolePutC` (for per-char routing) and `host_print` (for chunked output via `MMfputs(stdout)`). **Do not hook at `MMputchar`** — that layer is upstream of the `OptionConsole` routing, and hooking it bypasses the SERIAL-vs-SCREEN dispatch, breaking the REPL echo.

Declaration is in `ports/host_native/host_runtime.c`:

```c
extern void (*host_output_hook)(const char *text, int len);
```

Install it in your entry point before the first `PRINT`:

```c
static void my_swallow(const char *text, int len) { (void)text; (void)len; }
// ...
host_output_hook = my_swallow;
```

The test harness uses the same hook to capture output; mmbasic_ansi uses it to discard stray stdout writes that would otherwise corrupt the render thread's ANSI stream.

## Step B3 — POSIX path sizing (gotcha)

`ffconf.h` automatically sets `FF_MAX_LFN=255` for any build that defines `MMBASIC_HOST` or `MMBASIC_WASM`; `HOST_PATH_MAX=4096` in `host_fs_shims.c` is unconditional. You get the right path sizing for free as long as your Makefile includes `-DMMBASIC_HOST`, which every simulation port does.

Background: the default `FF_MAX_LFN=63` was chosen for 8.3 FAT. POSIX cwds on macOS/Linux routinely exceed that (e.g. `/Users/joshv/picocalc/PicoMiteAllVersions` is 44 chars before any filename), and core MMBasic path buffers in `core/mmbasic/FileIO.c` / `core/mmbasic/Editor.c` are sized by `FF_MAX_LFN`. At 63, deep cwds silently truncate `LOAD` / `SAVE` / `RUN`.

Cost of the 255-byte cap: `flist[]` (allocated transiently by `cmd_files`) is sized `HAL_PORT_FILES_MAX × sizeof(s_flist{fn[FF_MAX_LFN+1]; …})`. `ports/host_native/port_config.h` caps `HAL_PORT_FILES_MAX=128` so the peak allocation (~36 KB) fits the test harness's 128 KB heap. If your port provides its own `port_config.h` on a tighter heap, keep this cap similar or lower.

## Step B4 — configuration overrides

Two shared config files accept port-specific overrides:

- **`configuration.h`** — `HEAP_MEMORY_SIZE` (default 128 KB, match your port's needs), `MAX_PROG_SIZE`. Each override is `#ifdef MMBASIC_<YOURPORT>` / `#undef` / `#define` / `#endif`.
- **`ffconf.h`** — `FF_MAX_LFN` (see above).

Keep each override gated by your `MMBASIC_<YOURPORT>` flag. Don't touch device or other ports' layouts.

## Step B5 — acceptance

A simulation port is done when:

1. `make` under your port directory produces a binary.
2. `./ports/host_native/run_tests.sh` remains 239/239 + `HAL purity gate: clean` (you didn't break host_native or the core).
3. `tools/check_hal_purity.sh` remains green (you didn't add `#ifdef` to a core or HAL file).
4. Your binary runs a representative BASIC program to completion.

The `mmbasic_stdio` port is the strictest test of core HAL cleanliness: its link line deliberately omits Editor/REPL/Prompt/display/audio, so any undefined reference at link time points directly at a core file that's reaching into a hardware-specific symbol. If your simulation port has link failures, check whether the real fix is in core first.

---

# New drivers

If your board needs a peripheral not already under `drivers/*/`, add it there — **not in your port directory**. Drivers are shared across ports; port directories are per-board recipes, not per-board code.

Short version of the rules: one peripheral per driver, no cross-driver includes, at most one MCU shim, local target-macro `#ifdef` gates are fine inside driver files, RAM-resident annotations honoured, ship conformance tests under `drivers/<name>/tests/` if feasible. Full detail in `drivers/CONTRIBUTING.md`.

## PSRAM HAL contract

If your board has external PSRAM (or any SoC-managed SPIRAM region) and
you want MMBasic's `RAM` command, `MM.INFO(PSRAM SIZE)`, and core/mmbasic/Memory.c
large-allocation routing to light up, implement `hal/hal_psram.h` in a
port-owned TU (e.g. `ports/<your_board>/hal_psram_<chip>.c`) and link
`drivers/psram_heap/psram_heap_real.c`. The four entry points are:

- `hal_psram_init()` — acquire the PSRAM region (QSPI detect on RP2350;
  `esp_psram_init()` + `heap_caps_aligned_alloc(MALLOC_CAP_SPIRAM)` on
  ESP32) and publish `PSRAMbase` / `PSRAMsize`. Idempotent. Must run
  before any code reads those globals.
- `hal_psram_cache_sync()` — clean + invalidate the PSRAM-side cache.
  No-op on ports with no cache between CPU and PSRAM.
- `hal_psram_nocache_alias(base)` — return a CPU pointer that bypasses
  the cache (RP2350 returns `base + 0x04000000`). Return `NULL` if the
  port has no uncached aliased view; shared code translates that into
  the `RAM TEST NOCACHE` "not supported" error.
- `hal_psram_save_settings()` / `_restore_settings()` — paired bracket
  around an operation that may clobber PSRAM controller state (RP2350
  flash erase/program touches the QMI M[0] registers). No-ops on ports
  whose flash controller is independent of PSRAM (ESP32, host).

Ports without PSRAM link `drivers/psram_heap/psram_heap_stub.c` and
`drivers/psram_heap/hal_psram_stub.c` — both leave `PSRAMsize = 0`, and
every `if (!PSRAMsize)` guard in the shared `RAM` / core/mmbasic/Memory.c paths
short-circuits cleanly. See `hal/hal_psram.h` for the full contract
text and `ports/pico_sdk_common/hal_psram_pico.c` /
`ports/esp32_s3_metro/main/hal_psram_esp32.c` for working impls.

# Ground rules

Core files (`core/*.c`, `hal/*.h`) must compile identically for every port with zero preprocessor conditionals on target macros or `HAL_PORT_*` macros. Differences between boards are expressed three ways:

- **Port-config macro** — value-shaped knob. Add to `HAL_PORT_*` in your `port_config.h` (hardware ports) or your `MMBASIC_*`-gated override (simulation ports); core reads it as a C value.
- **Port hook function** — body-shaped divergence. Add a `port_*()` or `host_*_hook` entry point with a real impl in your port dir and stub impls in the other ports.
- **Driver flavour** — whole-peripheral swap. Add a new directory under `drivers/` and select it in your CMake branch (hardware port) or your Makefile's source list (simulation port).

If you find yourself wanting an `#ifdef YOUR_FLAG` in a core or HAL file, one of the three above is the answer.

# Further reading

- `docs/real-hal-plan.md` — the plan index.
- `docs/real-hal/architecture.md` — directory layout + composition example.
- `docs/real-hal/port-config.md` — how port-config macros work.
- `docs/real-hal/contracts.md` — HAL contract sketches per surface.
- `drivers/CONTRIBUTING.md` — rules for writing a new driver.
