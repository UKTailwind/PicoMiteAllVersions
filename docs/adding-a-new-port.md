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
    port_config.h      — 20-odd #define constants (plain #defines, no #ifdef)
    pin_tables.c       — PINMAP[] + codemap(pin)
    port_defaults.c    — port_set_default_options() for factory reset
```

That's it. Copy the closest existing port (usually `ports/pico/` for RP2040 variants or `ports/pico_rp2350/` for RP2350), then change the values.

## Step A1 — port_config.h

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

One export:

```c
void port_set_default_options(void);
```

Called by `FileIO.c::ResetOptions()` after shared defaults. Set any `Option.*` field whose factory value differs from the shared default — typical: SSD_RESET, TOUCH_XSCALE, SerialTX/RX, KeyboardConfig, ColourCode.

Port impl files may contain target-macro `#ifdef` blocks — that's where conditional bodies belong. The purity gate only enforces `core/` and `hal/`.

## Step A4 — wire into CMake

Top-level `CMakeLists.txt` (RP2040) or `CMakeLists 2350.txt` (RP2350) has big `if (COMPILE STREQUAL "<NAME>")` blocks. Add your target name to:

1. The include-path block (so `ports/<your_board>/port_config.h` is found).
2. The `add_executable` source list (add `ports/<your_board>/pin_tables.c` and `ports/<your_board>/port_defaults.c`).
3. The driver-selection blocks — add your target name to each `if (COMPILE STREQUAL ...)` that gates a driver you need (spi_lcd, vga_pio, hdmi, pwm_synth, sd_spi, etc.). See the existing branches for the pattern.
4. Any preprocessor-define blocks (`add_compile_definitions`) that set target macros your drivers expect (`PICOMITE`, `PICOMITEVGA`, `USBKEYBOARD`, etc.).

Finally add your target name to `buildall.sh` under `TARGETS=(...)`.

## Step A5 — acceptance

A hardware port is done when **all** of these pass:

1. `tools/check_hal_purity.sh` is green (no regression in core/ or hal/).
2. `./host/run_tests.sh` is 239/239 passed + `HAL purity gate: clean`.
3. `./buildall.sh` builds every target including yours, clean.
4. Your target boots to the MMBasic prompt on real hardware.

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
