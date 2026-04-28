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
- **Decascade palette flags:** `HAL_PORT_HAS_WIFI`, `HAL_PORT_HAS_VGA_PIO`, `HAL_PORT_HAS_GUICONTROLS`. Drive WiFi-stack inclusion, VGA-PIO scanout family, and the GUI widget command set (Touch.c + Option.MaxCtrls). Pick 0 / 1 per your board's hardware and feature set.
- **Numeric caps:** `HAL_PORT_ADC_CHANNEL_MAX`, `HAL_PORT_FILES_MAX`, `HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ`, `HAL_PORT_AUDIO_MOD_BUFFER_SIZE`.
- **Memory + clock + MMBasic-table:** `HAL_PORT_HEAP_MEMORY_SIZE`, `HAL_PORT_MAX_CPU`, `HAL_PORT_MIN_CPU`, `HAL_PORT_MAX_VARS`, `HAL_PORT_MAX_SUBFUN`, `HAL_PORT_MAX_MODES` (VGA-family ports only), `HAL_PORT_FLASH_TARGET_OFFSET[_USB]`, `HAL_PORT_MAGIC_KEY[_USB]`, `HAL_PORT_HEAP_TOP[_USB]`, `HAL_PORT_CONSOLE_RX_BUF_SIZE` (non-WiFi ports), `HAL_PORT_PIOMAX`, `HAL_PORT_NBR_PINS`, `HAL_PORT_PSRAM_BASE` + `HAL_PORT_PSRAM_BLOCK_SIZE` (rp2350 non-WEB ports). The `_USB` siblings let configuration.h pick the USB-keyboard variant via a single USBKEYBOARD ifdef.
- **Memory layout:** `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES`, `HAL_PORT_ALLMEMORY_ALIGN`, `HAL_PORT_CORE1_STACK_WORDS` (size of the shared core1 stack — 512 for ports running the SPI-LCD merge pipeline, 128 for VGA/HDMI scanout, 1 for WEB ports that never launch core1).
- **Flash-vs-RAM placement macros:** `HAL_PORT_RAM_FUNC`, `HAL_PORT_MMBASIC_HOT_FUNC`, `HAL_PORT_MMBASIC_SUBFUN_FUNC`. Expand to `__not_in_flash_func(name)` where RAM is available; plain `name` on RAM-constrained variants (rp2040 WEB, rp2040 VGA).
- **Per-board references:** `HAL_PORT_LCD_SPI_CLK_PIN` (which `Option.*` field carries the LCD SPI clock pin), `HAL_PORT_CONSOLE_FONT_MEDIUM` (symbol name for the medium console font).
- **Port hooks:** `HAL_PORT_RANDOMIZE_DEFAULT_SEED()`, `HAL_PORT_BC_CRASH_INFO_ATTR`.

If a macro doesn't apply to your board, define it to `0` (or the null form). The point is that every port defines every macro — core code reads unconditionally.

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

## Step A3 — port_defaults.c

This file owns the **per-port behaviour core code calls into**. Core
calls each entry point unconditionally (no #ifdef gates in core), so
**every port must define every entry point** — provide a no-op stub
when the behaviour isn't relevant to your hardware.

The closest existing port's `port_defaults.c` is the right starting
point — copy it and adjust. The full set of required exports:

| Function | What it does |
|---|---|
| `void port_set_default_options(void)` | First-boot factory defaults. Set `Option.*` fields that differ from the shared `ResetOptions()` baseline (display type, CPU speed, keyboard config, board-specific pin assignments). Called by `FileIO.c::ResetOptions()` after the shared defaults run. |
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
reclaim a VGA-PIO pin. Copy verbatim from the closest existing
VGA-family port_defaults.c if your port has `HAL_PORT_HAS_VGA_PIO=1`.

For a concrete walkthrough of writing each of these for a custom
board, see the worked example below.

**Per-port `#ifdef` is fine in this file.** The HAL purity gate only
enforces `core/*.c` and `hal/*.h`. Port-implementation files like
`port_defaults.c` may use `#ifdef USBKEYBOARD`, `#ifdef PICOMITEVGA`,
etc. for body-shaped divergence — that's the right scope for them.

## Step A4 — port_sources.cmake

Each port directory ships a `port_sources.cmake` snippet that owns the
source list AND the per-port build config (compile flags, link
libraries, SDK feature toggles). Top-level `CMakeLists.txt` includes
exactly one snippet per build via a small `COMPILE → PORT_DIR` map. You
do **not** edit a central driver-selection ladder anymore — there isn't
one.

Copy the closest existing snippet (`ports/pico/port_sources.cmake` for
RP2040 SPI-LCD, `ports/hdmi_rp2350/port_sources.cmake` for an
HDMI-family board, `ports/web_rp2350/port_sources.cmake` for a
WiFi-family board) and adjust two sections:

1. **Source list** — `target_sources(PicoMite PRIVATE …)` lists every
   driver and stub your port links. Driver flavours come in pairs
   (real impl vs no-op stub); pick one per axis. Existing axes:
   `display_merge_pico` vs `display_merge_stub`, `vm_framebuffer_picomite`
   vs `vm_framebuffer_stub`, `audio_mp3_real` vs `audio_mp3_stub`,
   `psram_heap_pico` vs `psram_heap_stub`, `upng_sprite` vs
   `upng_sprite_stub`, `gui_touch` vs `gui_touch_stub`, `spi_lcd_fastgfx`
   vs `spi_lcd_fastgfx_stub`. Add `MMweb_stubs.c` if your port doesn't
   link the WiFi stack, `gfx_3d.c` if it does link the 3D family
   (everyone except WiFi), `SSD1963.c` + `Touch.c` if you have an
   SPI-LCD touch panel, the `drivers/vga_pio/*` family if your port has
   `HAL_PORT_HAS_VGA_PIO=1`, the `drivers/hdmi/*` files if HDMI=1, etc.

2. **Per-port build config** — at the bottom of the snippet, add the
   `target_compile_options`, `target_link_libraries`, and SDK feature
   toggles your board needs:
     - `-DPICOMITE` / `-DPICOMITEVGA` (still consulted by core for the
       PicoMite-vs-VGA-vs-WEB dialect axis until that decascade lands)
     - `-DPICO_HEAP_SIZE=…` and `-DPICO_CORE0_STACK_SIZE=…`
     - `-Drp2350` (rp2350 ports only)
     - `-DHAL_PORT_DEVICE_NAME="…"`
     - USB-keyboard axis: `-DUSBKEYBOARD` + `tinyusb_host` /
       `tinyusb_board` linkage + `Pico_enable_stdio_usb(PicoMite 0)`,
       OR `Pico_enable_stdio_usb(PicoMite 1)` for non-USB
     - `target_link_libraries(PicoMite pico_multicore)` (most ports
       except WEBRP2350)
     - `target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)`
       (WiFi ports)
     - `pico_set_float_implementation(PicoMite pico_dcp)` (rp2350)
     - `pico_define_boot_stage2(slower_boot2 …)` (rp2040)

Then add your port's name to the COMPILE → PORT_DIR map at the top of
CMakeLists.txt:

```cmake
elseif (COMPILE STREQUAL "<YOURPORT>")
    set(PORT_DIR <your_board>)
```

That's the only edit to the top-level file.

Finally add your target name to `buildall.sh` under `TARGETS=(...)` so
the local + CI buildall gate exercises it.

## Step A5 — acceptance

A hardware port is done when **all** of these pass:

1. `tools/check_hal_purity.sh` is green (no regression in core/ or hal/).
2. `./host/run_tests.sh` is 239/239 passed + `HAL purity gate: clean`.
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

### Step 2 — pick a COMPILE name

Convention is uppercase-no-underscore: `MYMITE`. This is what users
pass to `cmake -DCOMPILE=MYMITE` and what appears in `buildall.sh`'s
TARGETS list. (After Stage E2e collapses the COMPILE enum, the name
will simply be `mymite` — same as the directory.)

### Step 3 — wire `mymite` into the COMPILE → PORT_DIR map

`CMakeLists.txt` has a single map at ~line 195:

```cmake
elseif (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    set(PORT_DIR hdmi_rp2350)
elseif (COMPILE STREQUAL "VGAWIFIRP2350")
    set(PORT_DIR vga_wifi_rp2350)
+ elseif (COMPILE STREQUAL "MYMITE")
+     set(PORT_DIR mymite)
endif()
```

**Also add `MYMITE` to the rp2350-platform branch and the board branch**
at the top of CMakeLists.txt:

```cmake
if (COMPILE STREQUAL "HDMI" OR ... OR COMPILE STREQUAL "MYMITE" )
    set(PICO_PLATFORM rp2350)
    if (COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "VGAWIFIRP2350")
        set(PICO_BOARD pico2_w)
    else()
        set(PICO_BOARD pimoroni_pga2350)   # MYMITE picks up this default
    endif()
```

If `mymite` uses a different board (custom PCB, different cyw43 module,
etc.), add a board-name branch the same way `pico2_w` was added for
WiFi ports.

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

### Step 8 — add `MYMITE` to `buildall.sh`

```bash
TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
    VGAWIFIRP2350
+   MYMITE
)
```

### Step 9 — first build

```bash
mkdir build_mymite && cd build_mymite
cmake -DCOMPILE=MYMITE -DPICOCALC=false -DPICO_SDK_PATH="$HOME/pico/pico-sdk" ..
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
./host/run_tests.sh                   # 239/239 expected
SKIP_HAL_PURITY=1 ./buildall.sh       # all 14 targets clean
```

Your port being in `buildall.sh`'s TARGETS list means a regression in
any other port that breaks yours blocks `main`.

### Common decisions for an HDMI + PSRAM board

| Question | Answer |
|---|---|
| Set `HAL_PORT_HAS_HDMI`? | `1` |
| Set `HAL_PORT_HAS_VGA_PIO`? | `1` (HDMI rides on the VGA-PIO scaffolding) |
| Set `HAL_PORT_HAS_PSRAM`? | `1` |
| Set `HAL_PORT_IS_VGA`? | `1` (PICOMITEVGA family) |
| Set `HAL_PORT_HAS_WIFI`? | `0` (HDMI + WiFi together is F1 — much harder, see `ports/vga_wifi_rp2350/README.md`) |
| Set `HAL_PORT_HAS_GUICONTROLS`? | `0` (no touch panel on HDMI) |
| `core1stack[]` words? | `128` (HDMI scanout uses 512 bytes = 128 words) |
| `FRAMEBUFFER_TRAILER_BYTES`? | `(320*240*2) = 153600` (HDMI 16-bit QVGA shadow) |
| Link `pico_multicore`? | yes (HDMI scanout runs on core1) |
| Link `pico_cyw43_arch_lwip_poll`? | no |
| `pico_set_float_implementation pico_dcp`? | yes (rp2350) |
| `pico_define_boot_stage2 slower_boot2`? | no (rp2040-only) |

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
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c ...
BC_SRCS   = bc_source.c bc_vm.c bc_runtime.c ...
NATIVE_SRCS = host_runtime.c host_fs_shims.c ...  # reused from host_native
PORT_SRCS = main.c <port>_*.c                      # your code
```

Start by copying the lists from `mmbasic_stdio` or `mmbasic_ansi`, then trim:

- **Always include** `MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c MMBasic_Print.c`, the shared graphics (`gfx_*_shared.c`), the state (`display_state.c pin_state.c option_state.c audio_state.c`), `Draw.c RGB121.c FileIO.c`, the bytecode VM (`bc_*.c`), and the filesystem (`ff.c ffunicode.c ffsystem.c`).
- **Omit** `Editor.c MMBasic_REPL.c MMBasic_Prompt.c` if your port has no interactive REPL (like `mmbasic_stdio`).
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

Background: the default `FF_MAX_LFN=63` was chosen for 8.3 FAT. POSIX cwds on macOS/Linux routinely exceed that (e.g. `/Users/joshv/picocalc/PicoMiteAllVersions` is 44 chars before any filename), and core MMBasic path buffers in `FileIO.c` / `Editor.c` are sized by `FF_MAX_LFN`. At 63, deep cwds silently truncate `LOAD` / `SAVE` / `RUN`.

Cost of the 255-byte cap: `flist[]` (allocated transiently by `cmd_files`) is sized `HAL_PORT_FILES_MAX × sizeof(s_flist{fn[FF_MAX_LFN+1]; …})`. `ports/host_native/port_config.h` caps `HAL_PORT_FILES_MAX=128` so the peak allocation (~36 KB) fits the test harness's 128 KB heap. If your port provides its own `port_config.h` on a tighter heap, keep this cap similar or lower.

## Step B4 — configuration overrides

Two shared config files accept port-specific overrides:

- **`configuration.h`** — `HEAP_MEMORY_SIZE` (default 128 KB, match your port's needs), `MAX_PROG_SIZE`. Each override is `#ifdef MMBASIC_<YOURPORT>` / `#undef` / `#define` / `#endif`.
- **`ffconf.h`** — `FF_MAX_LFN` (see above).

Keep each override gated by your `MMBASIC_<YOURPORT>` flag. Don't touch device or other ports' layouts.

## Step B5 — acceptance

A simulation port is done when:

1. `make` under your port directory produces a binary.
2. `./host/run_tests.sh` remains 239/239 + `HAL purity gate: clean` (you didn't break host_native or the core).
3. `tools/check_hal_purity.sh` remains green (you didn't add `#ifdef` to a core or HAL file).
4. Your binary runs a representative BASIC program to completion.

The `mmbasic_stdio` port is the strictest test of core HAL cleanliness: its link line deliberately omits Editor/REPL/Prompt/display/audio, so any undefined reference at link time points directly at a core file that's reaching into a hardware-specific symbol. If your simulation port has link failures, check whether the real fix is in core first.

---

# New drivers

If your board needs a peripheral not already under `drivers/*/`, add it there — **not in your port directory**. Drivers are shared across ports; port directories are per-board recipes, not per-board code.

Short version of the rules: one peripheral per driver, no cross-driver includes, at most one MCU shim, local target-macro `#ifdef` gates are fine inside driver files, RAM-resident annotations honoured, ship conformance tests under `drivers/<name>/tests/` if feasible. Full detail in `drivers/CONTRIBUTING.md`.

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
