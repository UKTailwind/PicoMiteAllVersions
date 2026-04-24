# Adding a New Board

One-page guide to onboarding a new device target onto the real-HAL layout.

Prerequisites: read `docs/real-hal-plan.md` (the index) + `docs/real-hal/architecture.md` (directory layout) first.

## The shape of a port

A port is a directory under `ports/<your_board>/` that supplies three files plus a selection in the top-level CMake recipe. Drivers are not written here — they live under `drivers/*/`, and your port selects which ones it links. If your board needs a peripheral no existing driver covers, see "New drivers" at the end.

### Required files

```
ports/<your_board>/
    port_config.h      — 20-odd #define constants (plain #defines, no #ifdef)
    pin_tables.c       — PINMAP[] + codemap(pin)
    port_defaults.c    — port_set_default_options() for factory reset
```

That's it. Copy the closest existing port (usually `ports/pico/` for RP2040 variants or `ports/pico_rp2350/` for RP2350), then change the values.

## Step 1 — port_config.h

All port-config macros have the `HAL_PORT_` prefix. They are **values** — core reads them inside C expressions and array sizes, never as preprocessor gates.

Start from `ports/pico/port_config.h` (the simplest). Key knobs, grouped:

- **Chip identity:** `HAL_PORT_PWM_SLICE_COUNT`, `HAL_PORT_GPIO_COUNT`, `HAL_PORT_PIO_COUNT`, `HAL_PORT_HAS_PIO2`, `HAL_PORT_HAS_FAST_TIMER`, `HAL_PORT_HAS_INT5`.
- **Chip features present:** `HAL_PORT_HAS_PSRAM`, `HAL_PORT_HAS_UPNG`, `HAL_PORT_HAS_DEFINES`, `HAL_PORT_HAS_MP3`.
- **Board features:** `HAL_PORT_HAS_HEARTBEAT`, `HAL_PORT_HAS_SSD1963`, `HAL_PORT_HAS_HDMI`, `HAL_PORT_HAS_NEXTGEN_DISPLAY`, `HAL_PORT_IS_VGA`.
- **Numeric caps:** `HAL_PORT_ADC_CHANNEL_MAX`, `HAL_PORT_FILES_MAX`, `HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ`, `HAL_PORT_AUDIO_MOD_BUFFER_SIZE`.
- **Memory layout:** `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES`, `HAL_PORT_ALLMEMORY_ALIGN`.
- **Flash-vs-RAM placement macros:** `HAL_PORT_RAM_FUNC`, `HAL_PORT_MMBASIC_HOT_FUNC`, `HAL_PORT_MMBASIC_SUBFUN_FUNC`. Expand to `__not_in_flash_func(name)` where RAM is available; plain `name` on RAM-constrained variants (rp2040 WEB, rp2040 VGA).
- **Per-board references:** `HAL_PORT_LCD_SPI_CLK_PIN` (which `Option.*` field carries the LCD SPI clock pin), `HAL_PORT_CONSOLE_FONT_MEDIUM` (symbol name for the medium console font).
- **Port hooks:** `HAL_PORT_RANDOMIZE_DEFAULT_SEED()`, `HAL_PORT_BC_CRASH_INFO_ATTR`.

If a macro doesn't apply to your board, define it to `0` (or the null form). The point is that every port defines every macro — core code reads unconditionally.

## Step 2 — pin_tables.c

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

## Step 3 — port_defaults.c

One export:

```c
void port_set_default_options(void);
```

Called by `FileIO.c::ResetOptions()` after shared defaults. Set any `Option.*` field whose factory value differs from the shared default — typical: SSD_RESET, TOUCH_XSCALE, SerialTX/RX, KeyboardConfig, ColourCode.

Port impl files may contain target-macro `#ifdef` blocks — that's where conditional bodies belong. The purity gate only enforces `core/` and `hal/`.

## Step 4 — wire into CMake

Top-level `CMakeLists.txt` (RP2040) or `CMakeLists 2350.txt` (RP2350) has big `if (COMPILE STREQUAL "<NAME>")` blocks. Add your target name to:

1. The include-path block (so `ports/<your_board>/port_config.h` is found).
2. The `add_executable` source list (add `ports/<your_board>/pin_tables.c` and `ports/<your_board>/port_defaults.c`).
3. The driver-selection blocks — add your target name to each `if (COMPILE STREQUAL ...)` that gates a driver you need (spi_lcd, vga_pio, hdmi, pwm_synth, sd_spi, etc.). See the existing branches for the pattern.
4. Any preprocessor-define blocks (`add_compile_definitions`) that set target macros your drivers expect (`PICOMITE`, `PICOMITEVGA`, `USBKEYBOARD`, etc.).

Finally add your target name to `buildall.sh` under `TARGETS=(...)`.

## Step 5 — acceptance

A port is done when **all** of these pass:

1. `tools/check_hal_purity.sh` is green (no regression in core/ or hal/).
2. `./host/run_tests.sh` is 239/239 passed + `HAL purity gate: clean`.
3. `./buildall.sh` builds every target including yours, clean.
4. Your target boots to the MMBasic prompt on real hardware.

If (1) or (2) regresses while adding your board, an `#ifdef` has landed in a core or HAL file — the answer is a port-config macro, a port hook, or a driver flavour (see "Ground rules" below), not a widened exemption.

## New drivers

If your board needs a peripheral not already under `drivers/*/`, add it there — **not in your port directory**. Drivers are shared across ports; port directories are per-board recipes, not per-board code.

Rules for new drivers:

- One peripheral per driver.
- Driver depends on at most one MCU shim (`ports/pico_sdk_common/`).
- Driver may contain local target-macro `#ifdef` gates (e.g. rp2040-vs-rp2350 register differences).
- No cross-driver coupling.
- `HAL_PORT_RAM_FUNC` / `__not_in_flash_func` annotations honoured for hot paths.
- Ships with a `tests/` subdir if the peripheral admits an off-board conformance test.

## Ground rules

Core files (`core/*.c`, `hal/*.h`) must compile identically for every port with zero preprocessor conditionals on target macros or `HAL_PORT_*` macros. Differences between boards are expressed three ways:

- **Port-config macro** — value-shaped knob. Add to `HAL_PORT_*` in your `port_config.h`; core reads it as a C value (in an expression or array size), never as a preprocessor gate.
- **Port hook function** — body-shaped divergence. Add a `port_*()` entry point with a real impl in your port dir and stub impls in the other ports.
- **Driver flavour** — whole-peripheral swap. Add a new directory under `drivers/` and select it in your CMake branch.

If you find yourself wanting an `#ifdef` in a core or HAL file, one of the three above is the answer.

## Further reading

- `docs/real-hal-plan.md` — the plan index.
- `docs/real-hal/architecture.md` — directory layout + composition example.
- `docs/real-hal/port-config.md` — how port-config macros work.
- `docs/real-hal/contracts.md` — HAL contract sketches per surface.
