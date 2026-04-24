# Adding a New Board

One-page guide to onboarding a new device target onto the real-HAL layout.

Prerequisites: read `docs/real-hal-plan.md` (the index) + `docs/real-hal/architecture.md` (directory layout) first. The standard is non-negotiable — see §"The standard" in the plan doc. This guide is the paved path; it assumes you accept it.

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

All port-config macros have the `HAL_PORT_` prefix. They are **values**, not gates — core reads them inside C expressions and array sizes. Renaming a `#ifdef` to `#if HAL_PORT_FOO` is gaming the rule, not following it.

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

Two exports:

```c
const uint8_t PINMAP[<N>] = { /* BASIC-pin → GPIO mapping */ };
int codemap(int pin) { /* validate + return PinDef[] slot */ }
```

Copy from an existing port with the same GPIO count. Update the array to match your board's silkscreen or header layout. `codemap()` is the one place BASIC's `PIN(n)` numbering lands; if it returns the wrong slot, every GPIO operation is wrong.

## Step 3 — port_defaults.c

One export:

```c
void port_set_default_options(void);
```

Called by `FileIO.c::ResetOptions()` after shared defaults. Set any `Option.*` field whose factory value differs from the shared default — typical: SSD_RESET, TOUCH_XSCALE, SerialTX/RX, KeyboardConfig, ColourCode.

Port impl files are **exempt from the purity gate** (the core/header rule only applies to files under `core/` and `hal/`). Target-macro `#ifdef` is fine here — that's where conditional bodies are supposed to live.

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

If any of (1)–(3) regresses while adding your board, you've introduced an ifdef into core or violated the purity standard — fix that, don't exempt the file. Physical boot-up is the hardware-specific check the other three don't cover.

## New drivers

If your board needs a peripheral not already under `drivers/*/`, add it there — **not in your port directory**. Drivers are shared across ports; port directories are per-board recipes, not per-board code.

Rules for new drivers:

- One peripheral per driver.
- Driver depends on at most one MCU shim (`ports/pico_sdk_common/`).
- Driver may contain local target-macro `#ifdef` gates (e.g. rp2040-vs-rp2350 register differences).
- No cross-driver coupling.
- `HAL_PORT_RAM_FUNC` / `__not_in_flash_func` annotations honoured for hot paths.
- Ships with a `tests/` subdir if the peripheral admits an off-board conformance test.

## Two ground rules

1. **Don't add `#ifdef YOUR_BOARD` to core files.** The whole refactor exists to make that unnecessary. If you're tempted, stop — the answer is either a new port-config macro (if it's a value-shaped knob), a new port hook function (if it's a body-shaped divergence), or a new driver flavour (if it's a whole peripheral swap).

2. **Don't rename a `#ifdef rp2350` to `#if HAL_PORT_FOO`.** Per the fixup-plan standard, that's gaming, not eliminating. Core files must have zero preprocessor conditionals on any target-macro OR port-config macro. Port-config constants are values, not gates.

## Further reading

- `docs/real-hal-plan.md` — the plan index.
- `docs/real-hal/architecture.md` — directory layout + composition example.
- `docs/real-hal/port-config.md` — how port-config macros work + what to avoid.
- `docs/real-hal/contracts.md` — HAL contract sketches per surface.
- `docs/real-hal-fixup-plan.md` — worked examples of port hooks + port-config moves.
