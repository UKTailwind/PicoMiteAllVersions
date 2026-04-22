# Real HAL Plan

**There are no pre-existing failures.** Every commit on `real-hal` must leave `./run_tests.sh` at the full pass count. If a test fails, it's a regression you caused — fix it, do not dismiss it, do not move on. This applies to commits already landed on `real-hal`: if tip shows failures, the last migration commit that "landed green" actually didn't, and those failures are owned by this plan until they're fixed.

**Never `git stash` + check out an earlier commit to prove a failure is "pre-existing."** There are no pre-existing errors. The exercise is wasted motion — any failure seen at tip is one to fix now, on the branch, in the next commit. Do not investigate blame, investigate the root cause.

**When you fix something, rebuild every artefact the user might be running.** The source tree is not the deliverable. `mmbasic_test`, `mmbasic_sim`, and `host/web/picomite.{mjs,wasm,data}` are three distinct binaries — fixing FileIO.c and only rebuilding the one you happened to run tests against leaves the user on stale bytes. Before reporting a fix as landed: rebuild `./host/build.sh`, `./host/build_sim.sh`, and `./host/build_wasm.sh` (when emcc is available). If a fix touches shared core code, all three must be regenerated in the same step.

**Commit-message pass counts must be scraped, not recalled.** Any commit that states a pass count ("Host N/N", "239/239 green", etc.) must derive that number from the `Results: X passed, Y failed` line of a fresh `./run_tests.sh` invocation run against the built binary immediately before `git commit`. Not from an earlier session, not from `--interp`-only, not from a single-file run. If `Y != 0`, the commit does not go out — fix first. Commit 20a8629 ("Host 239/239") actually shipped at 237/239; the two failures (t195, t197) sat on `real-hal` tip for two commits before they were noticed. This rule exists because of that.

**Verify-baseline ritual at session start.** Before the first code edit of any new session on `real-hal`, run `./host/build.sh && ./host/run_tests.sh` and report the verbatim `Results:` line. If `Y != 0`, the first task of the session is to fix it — no matter what the user asked for. This catches regressions the previous session shipped but didn't notice. Without this ritual, the "are the failures pre-existing?" trap reappears: a mid-session test run produces ambiguous signal (my change or tip's?) and invites exactly the `git stash` investigation the prior rule forbids.

**`run_tests.sh` single-file mode must honour `RUN_ARGS:`.** Today single-file invocations (`./run_tests.sh tests/t195_save_image_posix.bas --interp`) pass `$MODE` but ignore the test file's `RUN_ARGS:` header, so `--sd-root=/tmp/mmbasic-t195` never reaches the binary. That's why spot-checking a named failing test returned PASS while the full suite was red — the POSIX-routing path isn't exercised in single-file mode. Fix: parse RUN_ARGS in the single-file branch the same way `run_one_test()` already does.

Promote the implicit hardware-abstraction layer that emerged from the host port (`docs/host-hal-plan.md`) into a **first-class HAL spanning every device target** — RP2040 and RP2350 in all 12 board variants — so the BASIC interpreter and bytecode VM compile with **zero references to hardware target macros**, and target variants are selected by directory composition, not by preprocessor surgery.

## Status (2026-04-22)

**31 commits on `real-hal`.** Host tests 239/239 after every commit. All 12 device CMake variants green (`./buildall.sh` sweeps every `COMPILE` target: PICO/USB/VGA/VGAUSB/WEB for RP2040 and PICO/USB/VGA/VGAUSB/HDMI/HDMIUSB/WEB for RP2350). WASM link green.

### What "done" means

A phase is ✅ when **the target-macro `#ifdef` blocks are gone from core files** — the conditional bodies have moved into HAL implementations or port-config headers, and core calls a single HAL function with no branching on hardware target. Adding a HAL wrapper that core calls while leaving the `#ifdef` blocks in place is *infrastructure* (marked 🔧), not completion. The scoreboard measures actual `#ifdef` counts via `tools/hal_scoreboard.sh`; a phase's exit gate is the measured count, not the presence of HAL headers.

### Phase status

- Phase 0 ✅ — `hal/`, `drivers/`, `ports/` scaffolding; `hal/CONTRACT.md`; `tools/check_hal_purity.sh` (raw-grep mode); `tools/hal_scoreboard.sh`. Scoreboard rebaselined 476 → 606 after fixing regex that missed `PICOMITEPLUS`, `PICOCALC`, wider `PICOMITEWEB`. Deferred: `check_ram_baseline.sh`, `perf_microbench/`, Tier-B display-inline prototype (all need physical device).
- Phase 0.5 ✅ — cross-cutting state hoisted to `core/state/`: `display_state.c`, `pin_state.c` (ExtCurrentConfig), `option_state.c`, `audio_state.c`. Plan correction: `PinDef[]` is board-level const in `PicoMite.c`/`host_runtime.c`, not mutable core state — stays where it is.
- Phase 1 ✅ — `hal_flash.h` + device (`ports/pico_sdk_common/hal_flash_pico.c`) + host (`host/hal_flash_host.c`) impls. All 52 `flash_range_*` + `flash_do_cmd` call sites routed through the HAL. Added `hal_flash_read_jedec_id` and `host/hardware/regs/addressmap.h` stub along the way. Note: `Include.h` still pulls `hardware/flash.h` unconditionally — the header is available everywhere even though no core file calls the SDK directly. Clean that up in Phase 3b (port-config).
- Phase 2 (`hal_time`) ✅ — contract + impls + migration of 42 core call sites (`MM_Misc.c`, `Audio.c`, `Commands.c`, `Draw.c`, `External.c`, `FileIO.c`, `MATHS.c`, `bc_vm.c`, `mm_misc_shared.c`). All scored files call `hal_time_us_64()` — zero direct `time_us_64()`. Peripheral files (`PicoMite.c`, `I2C.c`, `USBKeyboard.c`, `MMMqtt`, `MMntp`, `MMtcpserver.c`, `XModem.c`) still call SDK time directly — migrate with their HALs.
- Phase 3 (`hal_pin`) 🔧 **infrastructure landed, ifdef elimination pending** — HAL API surface complete: `set_mode`, `read`, `write`, `toggle`, `read_output_latch`, `set_drive_mA`, `set_pulls`, `set_dir`, `set_input_enabled`, `select_digital`, `init_digital`, `deinit`, `set_function`, `adc_select`, `adc_init`, `adc_set_temp_sensor`, `adc_read`, `set_input_hysteresis`, `set_slew_fast`, `irq_set_edge`, `bank_read_all`, `bank_read_out_latch`, `bank_{set,clr,xor}_mask`; Tier-B fast inlines (`read_fast`/`write_fast`/`toggle_fast`) via per-port `hal_pin_inlines.h`. 107 `hal_pin_*` call sites in External.c (real migration). Direct `gpio_put`/`gpio_get`/`gpio_init`/`gpio_set_function`/`gpio_set_pulls` calls eliminated. **Remaining SDK calls not yet in HAL:** `adc_select_input` + `adc_read` (single-shot ADC in `ExtInp`, lines 1050-1052 — these are NOT DMA-streaming), `adc_hw->div`/`adc_hw->cs` register access, `sio_hw->gpio_hi_in`, 15 `picomite_gpio_irq_set_enabled` calls, ADC DMA-streaming path (`adc_set_round_robin`, `adc_fifo_setup`, `adc_run`, `adc_set_clkdiv`). **Ifdef count unchanged:** External.c is still 120 (baseline was 120). The 37 rp2350 PWM-slice/PIO-count blocks and other structural `#ifdef`s were not moved into HAL impls. → Phase 3b.
- Phase 4 (`hal_storage` + `hal_filesystem`) 🔧 **infrastructure landed, ifdef elimination pending** — HAL contracts, device/host impls, and call-site migration all done. `hal_fs_open`/`close`/`read`/`write`/`seek`/`tell`/`eof`/`sync` working on device and host. `BasicFileOpen`/`FileGetChar`/`FilePutChar`/`FilePutStr`/`FileEOF`/`FileClose`/`ForceFileClose`/`positionfile`/`cmd_seek`/`cmd_flush`/`fun_loc`/`fun_lof`/copy variants all HAL-routed. `FileTable` collapsed to `{ com }`. BMP/JPEG/WAV/TFTP/HTTP callers migrated. Drivers relocated: `mmc_stm32.c` → `drivers/sd_spi/`, `fs_flash_*` → `drivers/pico_flash/`. **Ifdef count: FileIO.c went from 75 → 60, not the target of <10.** 24 `MMBASIC_HOST` blocks, 8 `rp2350` blocks, 8 `PICOMITEVGA` blocks, and 10 `PICOMITEWEB` blocks remain — SD card init, flash erase geometry, VGA image paths, and network file ops still branch on target in the core file. → Phase 4b.
- Phase 5 (`hal_keyboard`) 🔧 **infrastructure landed, ifdef elimination pending** — HAL surface complete (`service`, `clear_repeat_state`, `init`, `keydown_count`, `keydown_slot`, `lock_state`, `set_layout`). Device impl dispatches PS/2 vs USB. `fun_keydown` unified in `vm_sys_input.c`. Drivers relocated: `Keyboard.c` → `drivers/ps2_matrix/`, `USBKeyboard.c` → `drivers/usb_host_kbd/`, `picocalc/i2ckbd.{c,h}` → `drivers/i2c_picocalc_kbd/`. **Ifdef count: MM_Misc.c went from 135 → 134, not the target of <50.** 21 `USBKEYBOARD` blocks remain in MM_Misc.c — OPTION output formatting, interrupt config, keyboard buffer handling still branch on `USBKEYBOARD` in the core file instead of routing through HAL. → Phase 5b.
- Phase 6 (`hal_audio`) 🟡 — Phase 6a landed: `hal_audio.h` contract (tone/sound/stop/volume/pause/resume; init is no-op on host), `host/hal_audio_host.c` forwarding to the existing `host_sim_audio_*` family. `Audio.c` host arm migrated to call `hal_audio_*` instead of `host_sim_audio_*` directly. VS1053 relocated to `drivers/vs1053/`. Device arm (~2000 lines of PWM/DMA/codec) untouched. `hal_audio_sample_push()` for WAV/MP3/FLAC/MOD streaming and `drivers/pwm_synth/` relocation pending.
- Phase 3b (port-config + rp2350 ifdef elimination) 🔧 **in progress** — `hal/hal_port_config.h` created with compile-time constants: `HAL_PORT_PWM_SLICE_COUNT`, `HAL_PORT_GPIO_COUNT`, `HAL_PORT_PIO_COUNT`, `HAL_PORT_HAS_PIO2`, `HAL_PORT_HAS_FAST_TIMER`, `HAL_PORT_HAS_INT5`, `HAL_PORT_PULLDOWN_NEEDS_RESET`, `HAL_PORT_HAS_PSRAM`, `HAL_PORT_HAS_UPNG`, `HAL_PORT_HAS_DEFINES`, `HAL_PORT_RAM_FUNC`. **External.c: 0 `rp2350` remaining** (was 41; converted to port-config equivalents). **FileIO.c: 0 `rp2350` remaining** (was 14; converted to PSRAM/UPNG/DEFINES/GPIO_COUNT). Scoreboard: 587 → 508 (-79). Remaining rp2350 in scored files: 163 — mostly Draw.c (56, display-mode/driver code → Phase 7+), MM_Misc.c (42, board config/display/SPI → Phase 7+), Commands.c (27), Memory.c (19). Many of these are driver-level algorithmic dispatch that belongs in driver files after Phase 7, not port-config constants.
- Phases 4b/5b — ifdef-elimination passes for Phases 4-5. See phase descriptions below.
- Phases 7–13 — not started.

**Commits:** f89e9a9 (scaffolding), 9a53573 / f7a06f4 / 896eaa9 / f1207a6 (state hoists), 33163ad / ed610a2 (hal_flash), 029170b / f2d840f (hal_time), 67b4092 / bbbb4ec / 344def0 / 2b374da / dca6ba2 / 8919341 / 67c1313 / e733405 (hal_pin), e8e1439 (buildall sweep), 293c98d / b929139 / d171e87 / 0d7940d / 25d6bd7 / 1ca8347 / 581a1db / 20a8629 (hal_storage + hal_filesystem + drivers/sd_spi + drivers/pico_flash + fun_dir/chdir migration + hal_fs_open/read/write/close scaffolding + FileIO.c file-table primitives routed through HAL), 2d76e16 (SAVE/LOAD IMAGE regression fix + BMP/JPEG migration + hal_fds[] exposed), f0e83c9 (external-caller sweep + FileTable retirement), 1c24fee (hal_keyboard service + clear_repeat_state), 95a92e4 (hal_keyboard driver relocations), 5232a46 / 7e733d6 (hal_audio Phase 6a + VS1053 relocation), this commit (hal_keyboard init + keydown + lock_state + set_layout — Phase 5 closed).

---

- **Branch:** `real-hal` (off `main`).
- **Predecessor plans:** [`bridge-restoration-plan.md`](bridge-restoration-plan.md), [`host-hal-plan.md`](host-hal-plan.md), [`web-host-plan.md`](web-host-plan.md).
  - Bridge restoration locked in: interpreter is the primary runtime; VM is a perf backend behind `FRUN`.
  - Host-HAL refactor proved the HAL pattern across one axis (host vs device).
  - Web-host port proved the HAL pattern composes across a third target (WASM).
- **This plan does not** revisit those invariants. It generalises one axis (host vs device) into N axes (per-MCU, per-display, per-keyboard, per-storage, per-audio, per-net) **across the device targets**.
- **Host and WASM ports are deliberately deferred.** They stay in their current `host/` directory shape, untouched, until the device HAL is proven across all 12 device targets. Their relocation is the *last* phase (Phase 12), not the first. Rationale: see "Host + WASM stay as-is until the end" below.

## Goals

1. **Hardware-clean core.** `MMBasic.c`, `Operators.c`, `Functions.c`, `Commands.c`, `bc_*.c`, and the VM syscall layer reference no target macros (`PICOMITE`, `PICOMITEVGA`, `HDMI`, `PICOMITEWEB`, `rp2350`, `USBKEYBOARD`, `MMBASIC_HOST`, `MMBASIC_WASM`) — including inside macro bodies (see "Scoreboard metric" below).
2. **HAL-clean device files.** `Draw.c`, `FileIO.c`, `Audio.c`, `MM_Misc.c`, `External.c`, `Memory.c` call HAL entry points instead of branching on target macros. Algorithmic dispatch (e.g. SCREENMODE2 vs SCREENMODE5 pixel packing) moves into `gfx_*_shared.c` or driver files; what's left in the core file is BASIC dialect logic.
3. **Composable directory layout.** A device target = a port directory + a list of HAL implementations to link. Adding a new board = creating one port directory and choosing existing drivers, not editing core files. *Multi-mode* drivers (a single VGA driver that handles SCREENMODE1/2/3) are allowed and expected; a driver may have its own internal `#ifdef HDMI` because that gate is *local* to the driver.
4. **No performance regression.** HAL is **compile-time-bound** (link-time symbol selection, `static inline` in headers where hot). No vtables, no function pointers in pixel-write or sample-output paths. The VM dispatch loop and `gfx_*_shared.c` primitives keep their current inlining. RAM-resident hot-path placement (`__not_in_flash_func`) is treated as a load-bearing contract, not a hint (see "RAM-resident code contract" below).
5. **Incremental and safe.** Every commit: `./run_tests.sh` 192/192 green, every device target in `buildall.sh` builds clean, the host build (`cd host/ && ./build.sh && ./run_tests.sh`) passes, and the WASM build links. Performance gates (per-opcode microbench, on-device pixel-fill, IRQ jitter probe) run on phases that touch hot paths.
6. **Pure-stdio MMBasic executable.** Once the HAL is complete, a `mmbasic` binary exists that has no display, no filesystem simulation, no editor, no REPL — it reads a `.bas` file from `argv[1]` (or BASIC source from stdin), executes it, prints `PRINT` output to stdout, and exits. Every HAL implementation it links is either a stdio shim, a real-libc shim (file I/O against the actual filesystem), or a hard error stub for hardware-only commands. This is a **separate port** from `host/`, not a flag on it. The pure-stdio port is the litmus test that the HAL contract is genuinely complete: if `mmbasic` can't be built without dragging in display/REPL/editor code, the HAL still leaks.

## Non-goals

- **Runtime polymorphism.** Target selection happens at build time, not runtime. (One binary still maps to one board. The point is to stop expressing that selection through `#ifdef` in shared files.)
- **API neutrality across the BASIC dialect.** Programs see the same MMBasic. The HAL is invisible above C.
- **Rewriting drivers.** Existing `SPI-LCD.c`, `SSD1963.c`, `Keyboard.c`, `USBKeyboard.c`, `Audio.c`, `VS1053.c`, `psram.c`, `lfs.c`, `mmc_stm32.c` move into per-driver directories with **no behavioural change**. They get a HAL-conformant header but their internals stay.
- **Reorganising or renaming the host/WASM ports.** They stay in `host/` until Phase 12.

## Host + WASM stay as-is until the end

The host and web-host ports are working test infrastructure. `./run_tests.sh` (192/192), `mmbasic_sim`, and the WASM smoke harnesses (`smoke_audio.mjs`, `smoke_phase4.mjs`) gate every commit on this branch. **Tearing those apart concurrently with HAL definition is gratuitous risk** — if a test fails, we'd be unable to attribute the failure to a HAL design bug vs a Makefile / include-path bug vs a renamed-symbol miss. The `-Wl,--allow-multiple-definition` workaround already in MEMORY.md is a sign the link order is fragile.

The pattern instead:

- **Phases 0-11 add `hal/`, `drivers/`, `ports/` alongside the existing tree.** The new HAL headers are designed to satisfy device targets first. The host port keeps using its current `host_*.h` headers.
- **Where a HAL header naturally subsumes a host header,** the host source either (a) keeps including its old `host_*.h` header which now forwards to the HAL header (one-line shim), or (b) is migrated to include the HAL header directly with a `host/` thin-wrapper for behavioural shortcuts that don't fit the HAL contract.
- **No symbols in `host/` are renamed or moved during Phases 0-11.** A failing test bisects to a single causal change.
- **Phase 12** does the host + WASM relocation in one focused effort, with the device HAL contract already locked. By then the HAL has shaken out across 12 device targets, so any remaining host-isms are real divergences worth examining, not noise from a still-evolving interface.

The CLAUDE.md rule "Never overwrite or delete working code to replace it with something different" applies most strongly to the host port. Phase 12 may rename and relocate; it must not change behaviour, and it lands as its own atomic phase with its own gate.

## RAM-resident code contract

`__not_in_flash_func(NAME)` is `__attribute__((section(".time_critical." #NAME)))` on Pico SDK targets — it places a function in SRAM so XIP cache misses don't add 5-10× latency on hot paths. RP2040 has 264 KB SRAM total; both *aggressive use* and *aggressive removal* matter. There are 6 in `Draw.c` and 210 project-wide.

**Contract:**

- Any function called from a PIO/DMA/scanline IRQ context, or from the FASTGFX SWAP critical path, must be marked `HAL_TIME_CRITICAL`.
- `HAL_TIME_CRITICAL` expands to `__not_in_flash_func` on Pico SDK ports, to nothing on host/WASM, and to a section attribute on any future MCU port.
- A driver that calls into a HAL function from such a context must document that requirement in its README (`drivers/<name>/README.md`).
- **CI gate:** a script parses the linker map for each device target, lists all functions placed in `.text` (flash) over a size threshold (1 KB) that are reachable from a known IRQ root (PIO IRQs, DMA IRQs, `bc_fastgfx_swap`). If the list grows between commits without an explicit baseline update, the phase fails. The baseline lives in `tools/ram_baseline_<target>.txt` and is updated as part of any phase that intentionally moves code in or out of SRAM.

This is not a Phase 10 cleanup — it's enforced from Phase 0 so a regression can't accumulate silently.

## Cross-cutting state — hoisted in Phase 0.5

Several globals are referenced by both the interpreter and would-be drivers:

- **Display state:** `HRes`, `VRes`, `FontTable[]`, `gui_font*`, `CursorTimer`, `spritebuff[]`, `struct3d[]`, `layer_in_use[]`, `frameBufferMutex` — currently defined in `Draw.c`, referenced from `MM_Misc.c`, `External.c`, `Editor.c`, `Commands.c`, `GUI.c`, and the VM.
- **Pin state:** `PinDef[]` — defined in `External.c`, written from both interpreter (`cmd_setpin`) and host stub init.
- **Option block:** `Option` struct — touched everywhere; persisted by `hal_flash`.
- **Audio state:** sample buffers, voice slots — currently in `Audio.c`.

If `Draw.c` is split into `drivers/<display>/` files in Phase 7+, every driver that references `HRes` would either re-declare it (multiple-definition link error) or include `Draw.c` indirectly (defeats the split). **Phase 0.5 hoists these globals into `core/state/` files** (`core/state/display_state.c`, `core/state/pin_state.c`, `core/state/option_state.c`, `core/state/audio_state.c`) before any driver split begins. The current files keep `extern` declarations; the global definitions move. This is mechanical, low-risk, and unblocks every later phase.

## Port-config mechanism

Many `#ifdef` blocks in core files don't dispatch to different *code* — they select different *constants*: how many PWM slices, how many PIO blocks, how many pins, which ADC channel is the temperature sensor, what the board name string is, whether PSRAM exists. These don't need HAL functions — they need compile-time constants that vary per port.

**Mechanism:** each port directory provides a `port_config.h` that defines a set of required constants. Core files `#include "port_config.h"` (resolved via include path, not relative path — each port directory is on the include path for its build). The constants replace `#ifdef rp2350` / `#ifdef PICOMITEVGA` / `#ifdef PICOCALC` blocks with direct references.

**Required constants (initial set, grows as phases eliminate ifdefs):**

```c
// port_config.h — provided by each port directory
#define PORT_BOARD_NAME        "PicoMite VGA RP2350"
#define PORT_PWM_SLICE_COUNT   12          // 8 on rp2040, 12 on rp2350
#define PORT_PIO_COUNT         3           // 2 on rp2040, 3 on rp2350
#define PORT_PIN_COUNT         48          // 30 on rp2040, 48 on rp2350
#define PORT_ADC_TEMP_CHANNEL  4           // 4 on both, but rp2350 has more ADC channels
#define PORT_ADC_CHANNEL_COUNT 5           // rp2040=5, rp2350=6+
#define PORT_HAS_PSRAM         1           // 0 or 1
#define PORT_HAS_NETWORK       0           // 1 only on PICOMITEWEB
#define PORT_HAS_DISPLAY       1           // 0 on mmbasic_stdio
#define PORT_MAX_CPU_KHZ       250000      // rp2040=200000, rp2350=250000
```

Core code uses these directly: `for (int i = 0; i < PORT_PWM_SLICE_COUNT; i++)` instead of `#ifdef rp2350 ... 12 ... #else ... 8 ... #endif`. The compiler dead-code-eliminates unreachable paths when a constant is 0/1, so `if (PORT_HAS_PSRAM) { ... }` compiles to nothing on ports without PSRAM — same binary effect as `#ifdef`, no preprocessor branching in core.

The host port's `port_config.h` defines `PORT_HAS_DISPLAY 1` (it simulates a display), `PORT_HAS_NETWORK 0`, `PORT_PWM_SLICE_COUNT 0`, etc. The `mmbasic_stdio` port (Phase 12.5) defines `PORT_HAS_DISPLAY 0`, `PORT_HAS_NETWORK 0`, `PORT_PWM_SLICE_COUNT 0` — any BASIC program that tries to use those features gets an error from the hard-error HAL stubs, not from an `#ifdef` in core.

This mechanism is introduced alongside Phase 3b (which needs it most heavily for the rp2350 PWM/PIO/pin-count blocks) and used by every subsequent phase.

## Combinatorial gates: drivers may have local `#ifdef`s

The "no `#ifdef` in core" rule does **not** mean "no `#ifdef` anywhere." Many `Draw.c` gates are *algorithmic*:

- `Draw.c:90-106` selects fonts based on `PICOMITEVGA && HDMI`.
- `Draw.c:577-606` `ClearScreen` branches on `DISPLAY_TYPE` (SCREENMODE2/3/4/5) with separate paths for `WriteBuf == LayerBuf` vs `SecondLayer`; SCREENMODE4/5 only exist under HDMI.
- `Draw.c:3249-3340` is 4-bit nibble packing for VGA SCREENMODE2 vs 8-bit for HDMI SCREENMODE5.

These are **driver-private**: they belong inside the relevant driver, where local `#ifdef` (or runtime mode dispatch *within* the driver) is fine. The contract is that the gate is local to its driver — nothing outside the driver needs to know. A driver may freely contain `#ifdef HDMI` or `#ifdef rp2350` because the driver only compiles into ports that selected it.

We will **not** create cross-product driver variants (`vga_pio_rp2040`, `vga_pio_rp2350`, `hdmi_rp2350_usb`...). One driver per peripheral; the driver internally handles MCU-version differences.

## HAL contract sketches

Full per-function contracts live in `hal/CONTRACT.md` (landed in Phase 0). The sketches below define the *shape* of each HAL — function set, ownership of error reporting, IRQ-safety expectations — so the reader can judge whether the architecture is plausible before the code lands. Detailed signatures, parameter encodings, and edge-case behaviour are deliberately deferred to the phase that defines each HAL, because the implementation will reveal constraints the plan can't predict.

Conventions across all HALs:
- **Return code:** `int` where `0` = success, negative = errno-style error. Functions returning a value (e.g. `hal_time_us_64`) cannot fail and return the value directly.
- **Ownership:** caller owns all buffers; HAL impls do not allocate. Where a HAL needs scratch (e.g. block I/O bounce buffer), it allocates at init and reuses.
- **Threading:** HAL functions are *not* reentrant unless explicitly marked `_irq_safe` in the header. Callers from IRQ context use `_irq_safe` variants only.
- **Error reporting:** the HAL returns the error code; translating to a BASIC-visible message is the *caller's* job (typically `error_throw_ex(code, "...")` in the interp). HAL impls do not call `error()` directly — that would couple them to MMBasic's longjmp-based error machinery.
- **Init order:** every HAL has `hal_<name>_init()` called from `board_init.c` in a documented order (time → flash → pin → storage → display → keyboard → audio → net). HALs may not depend on uninitialised peers.

### `hal_time.h`
```
uint64_t hal_time_us_64(void);                  // monotonic since boot; never wraps in practice
void     hal_time_sleep_us(uint32_t us);        // blocks ≥ us; may yield to cooperative scheduler (web host)
uint32_t hal_time_ms_tick(void);                // free-running ms counter for timeouts
int      hal_time_rtc_get(struct hal_datetime *out);
int      hal_time_rtc_set(const struct hal_datetime *in);
```
Hard part: web-host's cooperative-yield hook. `hal_time_sleep_us` on WASM must call `emscripten_sleep` for the cache-warm-friendly `wasm_last_yield_us` deduplication that web-host Phase 4 depends on.

### `hal_flash.h`
```
int hal_flash_read_options(void *buf, size_t len);
int hal_flash_write_options(const void *buf, size_t len);
int hal_flash_unique_id(uint8_t out[8]);
int hal_flash_erase_program_area(void);
```
Hard part: host's RAM-backed flash buffer must zero-fill on init or `Option.PIN` reads as -1 (truthy) and trips the lockdown prompt — see MEMORY.md note. The HAL impl on host owns this; the contract requires "freshly initialised flash reads as all-zero, not 0xFF."

### `hal_pin.h`
```
int hal_pin_set_mode(uint pin, hal_pin_mode_t mode);
int hal_pin_write(uint pin, bool high);
int hal_pin_read(uint pin, bool *out);
int hal_pin_pwm_start(uint pin, uint freq_hz, uint duty_pct);
int hal_pin_pwm_stop(uint pin);
int hal_pin_adc_read(uint channel, uint16_t *out);
int hal_pin_irq_attach(uint pin, hal_pin_edge_t edge, hal_pin_irq_cb cb, void *ctx);
int hal_pin_apply_clock_speed(uint khz);        // RP2350 accepts up to 250 MHz; RP2040 returns -EINVAL > 200 MHz
```
Hard part: `hal_pin_irq_attach` callback runs in IRQ context — the cb signature must be marked `HAL_TIME_CRITICAL` and document that it cannot allocate, longjmp, or call non-`_irq_safe` HAL functions.

### `hal_storage.h` (block-level)
```
int    hal_storage_init(hal_storage_dev_t dev);
size_t hal_storage_block_size(hal_storage_dev_t dev);
size_t hal_storage_block_count(hal_storage_dev_t dev);
int    hal_storage_read(hal_storage_dev_t dev, uint32_t lba, uint32_t count, void *buf);
int    hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf);
int    hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count);
int    hal_storage_sync(hal_storage_dev_t dev);
```
Hard part: SD-card removal between operations. The HAL must surface "media changed" as a distinct error so `cmd_mount` can re-init.

### `hal_filesystem.h` (POSIX-style on top of storage)
```
int     hal_fs_open(const char *path, int flags, int *fd);
int     hal_fs_close(int fd);
ssize_t hal_fs_read(int fd, void *buf, size_t n);
ssize_t hal_fs_write(int fd, const void *buf, size_t n);
off_t   hal_fs_seek(int fd, off_t off, int whence);
int     hal_fs_eof(int fd);
int     hal_fs_unlink(const char *path);
int     hal_fs_rename(const char *from, const char *to);
int     hal_fs_mkdir(const char *path);
int     hal_fs_rmdir(const char *path);
int     hal_fs_chdir(const char *path);
char   *hal_fs_getcwd(char *buf, size_t n);
int     hal_fs_dir_open(const char *path, hal_dir_t *out);
int     hal_fs_dir_next(hal_dir_t *dir, struct hal_dirent *out);
int     hal_fs_dir_close(hal_dir_t *dir);
int     hal_fs_stat(const char *path, struct hal_stat *out);
```
Hard part: `chdir` shadows libc on host (see MEMORY.md). Mount-point management for multi-drive ports (A: SD, B: flash). The function set is large because FILES/LOAD/SAVE/COPY/SEEK/OPEN all need it — but the surface is well-trodden POSIX, no novelty.

### `hal_keyboard.h`
```
int      hal_keyboard_service(void);            // pump hardware; called from main loop
int      hal_keyboard_get(uint16_t *out);       // non-blocking; 0 = no key, 1 = key returned
int      hal_keyboard_peek(uint16_t *out);      // non-destructive
uint32_t hal_keyboard_modifiers(void);          // shift/ctrl/alt/meta bitmap
int      hal_keyboard_set_layout(hal_kbd_layout_t layout);
int      hal_keyboard_paste(const char *utf8, size_t len);
```
Hard part: USB host keyboard runs on TinyUSB which expects to be polled at ≥1 kHz; PS/2 matrix scan runs from a timer IRQ. `hal_keyboard_service` is the single callsite the interpreter knows about; the driver decides what "service" means.

### `hal_audio.h`
```
int hal_audio_init(void);
int hal_audio_tone(uint channel, uint freq_hz, uint8_t vol_pct);
int hal_audio_sound(uint slot, uint freq_hz, uint8_t vol_pct, hal_sound_wave_t wave);  // SOUND command
int hal_audio_sample_push(const int16_t *samples, size_t frames);                       // streaming
int hal_audio_stop(uint channel_or_slot);
int hal_audio_set_master_volume(uint8_t vol_pct);
int hal_audio_pause(void);
int hal_audio_resume(void);
```
Hard part: web host's gesture-armed AudioContext (must resume on first input; requires the JS bridge — see web-host-plan Phase 3). VS1053 codec stream vs PWM tone share the channel namespace.

### `hal_display.h`
Two tiers, as established in "Tier-B inlining mechanism":

**Tier A (extern, slow path):**
```
int  hal_display_init(void);
int  hal_display_set_mode(hal_display_mode_t mode);
void hal_display_get_dimensions(int *w, int *h);
int  hal_display_sync(void);                                              // FASTGFX SWAP
int  hal_display_vsync_wait(void);
int  hal_display_scroll(int dx, int dy);
int  hal_display_blit_rect(int x, int y, int w, int h, const void *pixels, hal_pixfmt_t fmt);
int  hal_display_layer_select(uint layer);
int  hal_display_layer_merge(uint dst_layer, uint src_layer, hal_blend_t blend);
int  hal_display_close(void);
```

**Tier B (inline, hot path) — body in per-port `hal_display_inlines.h`:**
```
static inline void     hal_display_put_pixel(int x, int y, uint32_t rgb);
static inline uint32_t hal_display_get_pixel(int x, int y);
static inline void     hal_display_fill_rect_fast(int x, int y, int w, int h, uint32_t rgb);
```

Hard part: `__not_in_flash_func` placement — Tier B inlines must not pull flash-resident helpers when called from FASTGFX swap. The conformance test exercises `put_pixel` from a tight loop and measures throughput; a regression here is a HAL design bug, not a driver bug.

Multi-mode displays (VGA SCREENMODE1/2/3, HDMI SCREENMODE4/5) are handled by the *driver*: `hal_display_set_mode` takes a mode enum, and the driver internally dispatches to the right pixel format. Core never branches on display mode.

### `hal_multicore.h`
```
int hal_multicore_init(void);
int hal_multicore_post(uint channel, uint32_t payload);                   // non-blocking
int hal_multicore_post_blocking(uint channel, uint32_t payload);
int hal_multicore_recv(uint channel, uint32_t *out);                      // non-blocking
int hal_multicore_recv_blocking(uint channel, uint32_t *out);
```
Channel IDs are project-wide and live in `hal/hal_multicore_channels.h`: `HAL_MC_CH_DISPLAY_SCROLL`, `HAL_MC_CH_DISPLAY_CLEAR`, `HAL_MC_CH_FASTGFX_SWAP`, etc. RP2040 single-core ports get a no-op impl (`post` runs the work synchronously); host gets a stub.

Hard part: this isn't a peripheral — it's a *protocol*. The channel-ID list is the actual contract. Adding a new cross-core message means adding a channel ID and documenting the payload format in `hal_multicore_channels.h`, not editing the HAL.

### `hal_net.h`
```
int hal_net_init(void);
int hal_net_tcp_listen(uint16_t port, hal_tcp_handle_t *out);
int hal_net_tcp_accept(hal_tcp_handle_t listener, hal_tcp_handle_t *conn);
int hal_net_tcp_send(hal_tcp_handle_t conn, const void *buf, size_t n);
int hal_net_tcp_recv(hal_tcp_handle_t conn, void *buf, size_t n);
int hal_net_tcp_close(hal_tcp_handle_t conn);
int hal_net_udp_bind(uint16_t port, hal_udp_handle_t *out);
int hal_net_udp_send(hal_udp_handle_t sock, const struct hal_addr *to, const void *buf, size_t n);
int hal_net_udp_recv(hal_udp_handle_t sock, struct hal_addr *from, void *buf, size_t n);
int hal_net_dns_resolve(const char *host, struct hal_addr *out);
int hal_net_http_register(uint16_t port, hal_http_cb cb, void *ctx);
```
Hard part: WASM has no raw TCP — the WASM impl maps `hal_net_http_register` to `fetch()` callbacks and rejects raw TCP/UDP with `-ENOSYS`. The contract must allow this (functions can return "not supported on this port").

### `hal_irq.h`
Macros and helpers, no functions:
```
#define HAL_TIME_CRITICAL                       /* port-supplied: __not_in_flash_func or empty */
#define HAL_IRQ_SAFE                            /* annotation for review/grep */
bool hal_in_irq_context(void);                  /* for assertions */
void hal_critical_section_enter(hal_critical_t *cs);
void hal_critical_section_exit(hal_critical_t *cs);
```

### Stub HAL impls for the pure-stdio port (Phase 12.5)

Every hardware-only HAL has a `hal_<name>_hard_error.c` stub in `ports/mmbasic_stdio/stubs/`:
```
int hal_pin_set_mode(uint pin, hal_pin_mode_t mode) { return -ENOSYS; }
int hal_audio_tone(uint c, uint f, uint8_t v)       { return -ENOSYS; }
... etc
```
These exist so the linker is satisfied and any BASIC program calling `PIN(0)` or `PLAY TONE` gets the documented MMBasic error. The pure-stdio port links these instead of any peripheral driver.

---

These sketches don't pin every signature — that happens in the phase that defines each HAL. They do pin the *shape*: function count per HAL, who owns errors, where IRQ context lives, what's inline vs extern. If a sketch turns out to be wrong during Phase N, this section gets updated as part of that phase's commit.

## Scoreboard metric (replaces naive grep count)

Counting `#ifdef` occurrences with grep is gameable: a phase that introduces `#define HAL_FOR_RP2350(x) x` and uses `HAL_FOR_RP2350(multicore_fifo_push_blocking(7))` reduces the textual count to zero while preserving every target-specific code path.

**Real metric, enforced by `tools/check_hal_purity.sh`:**

1. **Zero references** to target macros (`PICOMITE`, `PICOMITEVGA`, `HDMI`, `rp2350`, `MMBASIC_HOST`, `MMBASIC_WASM`, `USBKEYBOARD`, `PICOMITEWEB`) in any file under `core/`, `hal/`, or `gfx_*_shared.c`. Detection runs against preprocessor-expanded source (`gcc -E -P -dD`) so macro hiding is caught.
2. **Zero conditional compilation directives** (`#if`, `#ifdef`, `#ifndef`, `#elif defined(...)`) in core files referencing those macros, before or after expansion.
3. **All HAL functions called from core have a declaration in `hal/*.h`.** No accidental "core file calls a driver-private symbol" leaks.

The numeric scoreboard at the bottom of this doc tracks the *raw grep count* as a progress metric, but the *gate* is `tools/check_hal_purity.sh`. A phase passes when the gate passes; the scoreboard just shows the trend.

## Current state (from 2026-04-21 survey, re-measured Phase 0 with `tools/hal_scoreboard.sh`)

```
Hardware-related #ifdefs in core files (Phase 0 baseline):
  Draw.c        162
  MM_Misc.c     135
  External.c    120
  FileIO.c       75
  Commands.c     46
  Memory.c       37
  Functions.c    17
  Audio.c        14
  Operators.c     0   ← already clean

Per-macro totals across the scored files:
  rp2350           258
  PICOMITE         112    (excluding PICOMITEVGA/WEB/PLUS)
  PICOMITEVGA       88
  PICOMITEWEB       79
  HDMI              62
  USBKEYBOARD       43
  MMBASIC_HOST      41
  PICOCALC          16

The initial draft of this plan counted 476 and used a regex that missed
`PICOMITEPLUS`, `PICOCALC`, and wider `PICOMITEWEB` patterns. The real total
is 606. Phase-by-phase deltas in the scoreboard below remain estimates; the
gate is `tools/check_hal_purity.sh`, not the raw count.

12 device targets:
  PicoMite, PicoMite USB, PicoMite VGA, PicoMite VGA USB,
  PicoMite Web (RP2040 + RP2350),
  PicoMite RP2350, PicoMite USB RP2350, PicoMite VGA RP2350,
  PicoMite VGA USB RP2350, PicoMite HDMI, PicoMite HDMI USB.
Plus host (native) and host_wasm (Phase 12).
```

Existing HAL-ish surfaces (informal, host-only, **kept as-is**):
- `host_fb.h`, `host_fs_hal.h`, `host_keys.h`, `host_terminal.h`, `host_time.h`
- `vm_sys_pin.h`, `vm_sys_file.h`, `vm_sys_graphics.h`, `vm_sys_time.h`, `vm_sys_input.h`

These prove the pattern works without performance loss. The task is to **define the real HAL for device targets, leave host/WASM alone until Phase 12, and let the device HAL be informed by what worked on host.**

## Target architecture

```
core/                              ← BASIC interpreter + compiler + VM
   MMBasic.c, Operators.c, Functions.c, Commands.c (interp parts)
   bc_source.c, bc_vm.c, bc_runtime.c, bc_alloc.c
   gfx_*_shared.c                  ← target-agnostic graphics primitives
   mm_misc_shared.c                ← target-agnostic command bodies
   state/                          ← hoisted cross-cutting globals (Phase 0.5)
       display_state.c             (HRes, VRes, FontTable, layer_in_use, ...)
       pin_state.c                 (PinDef[], pin mode arrays)
       option_state.c              (Option struct + persistence callbacks)
       audio_state.c               (voice slots, sample buffers)
   No hardware #ifdefs anywhere. Enforced by tools/check_hal_purity.sh.

hal/                               ← HAL interface headers (declarations + Tier-B inlines only)
   hal_time.h                      ← monotonic µs clock, sleep, RTC
   hal_flash.h                     ← persistent option block, unique ID
   hal_pin.h                       ← GPIO read/write, PWM, ADC, edge IRQ
   hal_storage.h                   ← block I/O for SD / LFS / RAM-disk
   hal_filesystem.h                ← POSIX-style fopen/fread on top of storage
   hal_keyboard.h                  ← key polling, modifier state, layout
   hal_audio.h                     ← PWM tone, sample DMA, MP3 stream
   hal_display.h                   ← framebuffer + raw pixel + scroll + sync
   hal_multicore.h                 ← cross-core IPC (RP2350 dual-core LCD)
   hal_net.h                       ← TCP/UDP/HTTP (PICOMITEWEB)
   hal_irq.h                       ← HAL_TIME_CRITICAL, IRQ-context helpers
   CONTRACT.md                     ← what each function must guarantee

drivers/                           ← reusable device drivers
   ili9341/                        ← SPI LCD: implements hal_display
   vga_pio/                        ← rp2040/2350 PIO + DMA VGA (multi-mode internally)
   hdmi/                           ← rp2350 HDMI (DVI over PIO + DMA)
   ssd1963/                        ← parallel LCD for Web variants
   ps2_matrix/                     ← implements hal_keyboard
   usb_host_kbd/                   ← TinyUSB host keyboard
   i2c_picocalc_kbd/               ← PicoCalc I²C keyboard
   sd_spi/                         ← SPI SD card → hal_storage
   pico_flash/                     ← XIP-banked LFS → hal_storage
   pwm_synth/                      ← PWM tone generator → hal_audio
   vs1053/                         ← MP3 codec → hal_audio
   cyw43/                          ← Pico W WiFi → hal_net
   psram/                          ← rp2350 external RAM (extra heap)
   goodix_touch/                   ← capacitive touchscreen
   gui_controls/                   ← optional UI widget layer (any port with display+touch)
   gps_uart/                       ← GPS NMEA parser
   watchdog_pico/                  ← watchdog + reboot
   Each driver:
     - includes hal/<corresponding>.h and implements its API
     - knows about its peripheral, not about other peripherals
     - depends on at most one MCU shim (e.g. ports/pico_sdk_common/) for register access
     - may contain local #ifdef gates (HDMI vs VGA inside vga_pio is fine)
     - ships with conformance tests under drivers/<name>/tests/

ports/                             ← target board recipes (device only)
   pico_sdk_common/                ← shared rp2040+rp2350 SDK glue
                                     hal_time/hal_flash impls,
                                     irq dispatch, timer wiring
   rp2040/                         ← MCU-specific overrides
   rp2350/                         ← ditto
   picomite_rp2040/, picomite_usb_rp2040/,
   picomite_vga_rp2040/, picomite_vga_usb_rp2040/,
   picomite_web_rp2040/,
   picomite_rp2350/, picomite_usb_rp2350/,
   picomite_vga_rp2350/, picomite_vga_usb_rp2350/,
   picomite_hdmi/, picomite_hdmi_usb/, picomite_web_rp2350/

   Each port directory contains:
     - port_config.h               ← #defines that drivers/HAL impls read
     - CMakeLists.txt              ← lists which drivers + hal-impls to link
     - main.c                      ← entry point
     - board_init.c                ← thin glue (pin assignments, clock setup, init order)

host/                              ← UNCHANGED until Phase 12
   host_*.c, host_wasm_*.c         ← present-day code, untouched
   Makefile, Makefile.wasm         ← present-day build, untouched

ports/mmbasic_stdio/               ← Phase 12.5: pure-stdio binary, no display/REPL/editor
   main.c                          ← argv[1] = .bas file, or read stdin to EOF
   CMakeLists.txt                  ← links core + minimal HAL impls only:
                                       hal_time → real libc clock
                                       hal_filesystem → real POSIX
                                       hal_storage → null (no block device)
                                       hal_keyboard → stdin
                                       hal_display → null (PRINT goes to stdout)
                                       hal_audio → hard-error stub
                                       hal_pin/flash/multicore/net → hard-error stubs
   No editor, no REPL, no graphics, no MEMFS, no IDBFS, no canvas.
```

The `mmbasic_stdio` port is structurally distinct from `host/`. The host port simulates a full PicoMite environment (display canvas, MEMFS filesystem, REPL, editor, FASTGFX) for behavioural-equivalence testing. The stdio port simulates *nothing* — it's MMBasic the language, talking to a real OS through narrow HAL surfaces. Together they prove the HAL: the host port shows the contract supports rich device-equivalent behaviour; the stdio port shows the contract supports trivial Unix integration without dragging in any display or interactive code.

### How it composes (device example)

```cmake
add_executable(picomite_vga_rp2350
    ports/picomite_vga_rp2350/main.c
    ports/picomite_vga_rp2350/board_init.c
)
target_link_libraries(picomite_vga_rp2350 PRIVATE
    mmbasic_core
    pico_sdk_common
    rp2350
    driver_vga_pio
    driver_ps2_matrix
    driver_sd_spi
    driver_pwm_synth
    driver_psram
    driver_watchdog_pico
)
```

No `target_compile_definitions(... PICOMITEVGA)` poisoning the core. The `vga_pio` driver knows it's VGA; nothing else needs to.

## Tier-B inlining mechanism (decided in Phase 0)

The hot-path question — how does `hal_display_put_pixel` inline across the HAL boundary without a function-call overhead — is **the** perf-critical decision and is locked in Phase 0, not deferred.

**Decision (subject to Phase 0 prototype validation):** per-port inline header.

- `hal/hal_display.h` declares the slow-path API and includes a port-specific inline header at the bottom: `#include "hal_display_inlines.h"`.
- Each port directory provides its own `hal_display_inlines.h` containing `static inline` bodies for hot functions. The port's CMake adds the port directory to the include path *before* any driver, so the resolution is unambiguous.
- For non-perf paths (init, mode-set, sync), regular extern functions resolved at link time.

**Phase 0 validates this** by prototyping `hal_display_put_pixel` for ILI9341 and measuring `pico_blocks_tilemap` SWAP rate on RP2040 hardware (or accurate RP2040 emulation if hardware-in-loop isn't available). If the inline mechanism costs >2% on FASTGFX, fall back to per-port `.h`-included `.c` (header-only library style) and re-measure. If both lose, the HAL boundary itself moves up the call stack so the hot loop runs entirely inside the driver.

## Phased plan

Each phase: green host build, green WASM build, every device CMake target builds clean (`buildall.sh`), `./run_tests.sh` passes, `tools/check_hal_purity.sh` passes for the files in scope, performance gates pass. Each phase ends with a commit. **Each phase's exit gate includes a measured `tools/hal_scoreboard.sh` result showing the target ifdef count was reached.**

Phases are ordered to **(a) lock the architecture in Phase 0, (b) hoist shared state in Phase 0.5 to unblock everything, (c) build HAL infrastructure (contracts + impls), (d) eliminate `#ifdef` blocks by moving conditional bodies into HAL impls and port-config, and (e) leave host/WASM relocation for the very end.** Steps (c) and (d) happen together for new phases (6+). For phases 3-5 where infrastructure landed without ifdef elimination, sub-phases 3b/4b/5b do the cleanup.

## Retrospective and course correction (end-of-phase-5a, 2026-04-22)

Phases 0–5a + 6a landed in 31 commits. HAL headers, device/host impls, call-site migrations, and driver relocations are real work — 7 HAL contracts, 7 device impls, 7 host impls, 6 driver directories. Tests 239/239. All 12 device CMake targets build.

**But the primary goal — eliminating hardware `#ifdef`s from core files — barely moved.** The scoreboard went from 606 → 587 (−3%). The phases added HAL wrappers and migrated call sites, but left the `#ifdef` blocks in place. That means core files still branch on `PICOMITEVGA`, `USBKEYBOARD`, `rp2350`, `MMBASIC_HOST`, etc. — the HAL infrastructure exists but the core isn't clean.

**Course correction:** Phases 3, 4, 5 are relabelled from ✅ to 🔧 (infrastructure landed). New sub-phases 3b, 4b, 5b do the actual ifdef elimination by moving conditional bodies into the HAL impls that already exist. A new "port-config mechanism" (see section below) absorbs structural constants (PWM slice count, PIO count, pin count, etc.) that don't need function calls.

From this point forward, every phase's exit gate is measured by `tools/hal_scoreboard.sh`. If the number didn't go down, the phase isn't done. No more marking phases complete based on infrastructure alone.

**Per-phase commit-count targets:**
- Phase 3b/4b/5b: **1–2 commits each.** Infrastructure exists; this is moving `#ifdef` bodies.
- Phase 6b (audio device arm): **2–3 commits.**
- Phase 7a–d (display): **3–4 commits each.**
- Phase 8 (multicore): **1 commit.**
- Phase 9 (net): **1–2 commits.** 67 `PICOMITEWEB` blocks across 8 files.
- Phase 10 (heap): **1 commit.**
- Phase 11 (sweep): **3–5 commits** for remaining drivers + final cleanup of every file to 0.

### Phase 0 — Architecture lock + tooling (no behaviour change) ✅ partial

Core scaffolding + purity gate + scoreboard landed. `check_ram_baseline.sh`, `perf_microbench/`, and the Tier-B display-inline prototype/measurement still pending — all need physical device access.

- Land this doc.
- Create empty `hal/`, `drivers/`, `ports/` directories with `.gitkeep`.
- Land `hal/CONTRACT.md` (per-function guarantees).
- Land `tools/check_hal_purity.sh` — fails if any target macro appears in `core/` or `hal/*.h` (preprocessor-expanded check).
- Land `tools/check_ram_baseline.sh` — parses linker maps for each device target, compares against `tools/ram_baseline_<target>.txt`. Initial baselines captured from current main.
- Land `tools/perf_microbench/` — per-opcode VM benchmark (10M iterations of one opcode each), pixel-fill benchmark on host + on at least one device target, IRQ jitter probe (timer-IRQ pin toggle, scope-measured or logic-analyser dump committed as baseline).
- Prototype Tier-B inline mechanism on a throwaway display HAL stub. Measure on `pico_blocks_tilemap`. Commit the measurement. Lock the mechanism.
- Inventory every hardware-related preprocessor directive in core files; record in scoreboard.

**Exit gate:** plan reviewed; all tooling lands and runs green on current main; Tier-B mechanism chosen and measured; scoreboard captured.

### Phase 0.5 — Hoist cross-cutting state into `core/state/` ✅

- Move global definitions (not declarations) for `HRes`, `VRes`, `FontTable[]`, `layer_in_use[]`, `spritebuff[]`, `struct3d[]`, `frameBufferMutex` from `Draw.c` to `core/state/display_state.c`.
- Move `PinDef[]` definition from `External.c` to `core/state/pin_state.c`.
- Move `Option` struct definition (currently scattered) into `core/state/option_state.c`.
- Move audio voice slots and sample buffers from `Audio.c` into `core/state/audio_state.c`.
- Original files keep `extern` declarations and continue to compile; only the storage moved.
- This unblocks every later phase that splits a core file into drivers without hitting multiple-definition link errors.

**Exit gate:** all 12 device targets + host + WASM build clean. `./run_tests.sh` 192/192. No behaviour change. Linker maps show globals in their new TUs.

### Phase 1 — `hal_flash.h` (true lowest-risk dry run) ✅

- **Why this and not `hal_time`:** `hal_time` crosses the cooperative-yield hook (`host_runtime_check_timeout`, `wasm_last_yield_us`) that web-host Phase 2.5/4 depends on. `hal_flash` has no IRQ/jitter semantics — it's the persistent option block + unique device ID, called rarely.
- Define `hal/hal_flash.h`: `hal_flash_read_options(buf, size)`, `hal_flash_write_options(buf, size)`, `hal_flash_unique_id(out8)`, `hal_flash_erase_program_area()`.
- Implement in `ports/pico_sdk_common/hal_flash_pico.c` (calls `hardware_flash`).
- Replace direct `hardware/flash.h` calls in `MM_Misc.c`, `External.c`, `MMBasic.c` with HAL calls.
- Host stays on its current `host_options_snapshot()` machinery; the host port file gets a 5-line `hal_flash_*` shim that calls the existing host code. **No host file is renamed or moved.**

**Exit gate:** core no longer includes `hardware/flash.h`. Tools green. `./run_tests.sh` 192/192. All 12 device targets build.

### Phase 2 — `hal_time.h` ✅

- Now that the tooling and Phase 0.5 are locked, time can be migrated safely.
- Define `hal/hal_time.h`: `hal_time_us_64()`, `hal_time_sleep_us()`, `hal_time_ms_tick()`, RTC accessors.
- Implement in `ports/pico_sdk_common/hal_time_pico.c`.
- Replace device-side `time_us_64()`, `sleep_us()` calls in `MM_Misc.c`, `Audio.c`, `Commands.c` (~12 sites).
- Host port: thin `host_time.c` shim already exists; add a 1-line forward to it from `hal_time.h` extern declarations. No host file renamed.
- **Performance gate:** `pico_blocks_tilemap` SWAP rate held within 1% of the Phase 1 baseline on web host AND at least one physical device. If jitter degrades, the HAL has a regression — do not paper over it.

**Exit gate:** core no longer calls `time_us_64()` directly. FASTGFX 50 Hz invariant verified.

### Phase 3 — `hal_pin.h` (pins / PWM / ADC) 🔧

HAL API surface and call-site migration landed (see status above). The HAL contract is complete and both device and host impls exist. What remains is eliminating the `#ifdef` blocks from core files — the conditional bodies need to move *into* HAL implementations and port-config headers so core calls a single function with no target branching.

**What landed (Phase 3a):**
- `hal/hal_pin.h` with full API surface (26 functions + Tier-B inlines).
- `ports/pico_sdk_common/hal_pin_pico.c` + `host/hal_pin_host.c` impls.
- 107 `hal_pin_*` call sites in External.c replacing direct `gpio_*` SDK calls.
- `vm_sys_pin.c` adapted to call the HAL.

**What remains (Phase 3b):**
- External.c still has 120 hardware `#ifdef`s (unchanged from baseline). The bodies of 37 rp2350 PWM-slice-count / PIO-block-count / pin-count blocks need to move into HAL impls or port-config constants.
- Single-shot ADC calls (`adc_select_input`, `adc_read` at line 1050-1052, `adc_hw` register access) need to go through `hal_pin_adc_*`.
- 15 `picomite_gpio_irq_set_enabled` calls need `hal_pin_irq_set_edge` or equivalent.
- `sio_hw->gpio_hi_in` direct register read needs a HAL accessor.
- ADC DMA-streaming path (`adc_set_round_robin`, `adc_fifo_setup`, `adc_run`, `adc_set_clkdiv`) — consider `hal_adc_stream_*` or leave in a driver.

**Exit gate:** External.c hardware-ifdef count → 0. All pin/PWM/ADC differences between rp2040 and rp2350 live in HAL impls or port-config, not in External.c. `tools/check_hal_purity.sh` passes for External.c and `vm_sys_pin.c`.

### Phase 4 — `hal_storage.h` + `hal_filesystem.h` 🔧

HAL contracts, device/host impls, and file-op call-site migration all landed (see status above). What remains is eliminating the `#ifdef` blocks from FileIO.c.

**What landed (Phase 4a):**
- `hal/hal_storage.h` + `hal/hal_filesystem.h` contracts.
- `hal_storage_pico.c`, `hal_filesystem_pico.c`, `hal_filesystem_host.c` impls.
- `BasicFileOpen`/`FileGetChar`/`FilePutChar`/`FilePutStr`/copy variants/BMP/JPEG/WAV/TFTP/HTTP all HAL-routed.
- `FileTable` collapsed. `host_fs_posix_*` shim family deleted.
- Drivers relocated: `mmc_stm32.c` → `drivers/sd_spi/`, `fs_flash_*` → `drivers/pico_flash/`.

**What remains (Phase 4b):**
- FileIO.c still has 60 hardware `#ifdef`s (baseline was 75). The remaining blocks:
  - 24 `MMBASIC_HOST` blocks: SD card init, flash erase geometry, POSIX-vs-FatFS dispatch. These conditional bodies need to move into `hal_filesystem_pico.c` / `hal_filesystem_host.c`.
  - 8 `rp2350` blocks: flash sector sizes, PSRAM bank switching. Move into `hal_flash_pico.c` or port-config constants.
  - 8 `PICOMITEVGA` blocks: display-specific image load paths. Move into display HAL (Phase 7) or a display-aware file helper.
  - 10 `PICOMITEWEB` blocks: MQTT config, network file ops. Move into `hal_net` (Phase 9) or network-aware file helper.
  - 1 `PICOCALC` + `rp2350` block: flash layout. Port-config.

**Exit gate:** FileIO.c hardware-ifdef count → 0. Every target-specific file-system behaviour lives in a HAL impl, not in FileIO.c.

### Phase 5 — `hal_keyboard.h` 🔧

HAL surface, driver relocations, and key call-site migrations landed (see status above). What remains is eliminating the `#ifdef USBKEYBOARD` blocks from MM_Misc.c and other core files.

**What landed (Phase 5a):**
- `hal/hal_keyboard.h` with full surface (service, clear_repeat_state, init, keydown_count, keydown_slot, lock_state, set_layout).
- `ports/pico_sdk_common/hal_keyboard_pico.c` + `host/hal_keyboard_host.c` impls.
- `fun_keydown` unified in `vm_sys_input.c`.
- Drivers relocated: `Keyboard.c` → `drivers/ps2_matrix/`, `USBKeyboard.c` → `drivers/usb_host_kbd/`, `picocalc/i2ckbd.{c,h}` → `drivers/i2c_picocalc_kbd/`.

**What remains (Phase 5b):**
- MM_Misc.c still has 21 `USBKEYBOARD` blocks (baseline was ~21 — no reduction). The conditional bodies — OPTION output formatting, interrupt config, keyboard buffer handling, repeat-rate setup, PS/2 pin configuration — need to move into `hal_keyboard_pico.c` (which already dispatches USB vs PS/2) or into a new HAL function (e.g. `hal_keyboard_get_option_string`, `hal_keyboard_get_config`).
- FileIO.c has 2 `USBKEYBOARD` blocks: keyboard buffer persistence. Move into HAL.
- Functions.c has 3 `USBKEYBOARD` blocks: keyboard read functions. Move into HAL.
- Commands.c has 3 keyboard-related blocks. Move into HAL.

**Exit gate:** Zero `USBKEYBOARD` references in any scored core file. All keyboard-type dispatch lives in HAL impls.

### Phase 3b/4b/5b — ifdef elimination for landed HAL surfaces

These are the cleanup passes that complete the phases whose infrastructure landed but whose `#ifdef` blocks weren't moved into HAL impls. Each pass:
1. Identifies every remaining `#ifdef TARGET_MACRO` block in the phase's core files.
2. Moves the conditional body into the appropriate HAL implementation (already exists) or into port-config constants (see "Port-config mechanism" below).
3. Replaces the `#ifdef` block with a single HAL call or port-config constant reference.

**Phase 3b** (External.c + pin/PWM/ADC): target 120 → 0 in External.c. Requires port-config constants for `HAL_PWM_SLICE_COUNT`, `HAL_PIO_COUNT`, `HAL_PIN_COUNT`, `HAL_ADC_TEMP_CHANNEL`. ADC single-shot and DMA-streaming paths move into HAL. `picomite_gpio_irq_set_enabled` → `hal_pin_irq_*`.

**Phase 4b** (FileIO.c): target 60 → 0. Host-stub blocks move into `hal_filesystem_host.c`. rp2350 flash geometry → port-config or `hal_flash`. PICOMITEVGA image blocks → Phase 7 (display HAL). PICOMITEWEB file blocks → Phase 9 (net HAL). After 4b + 7 + 9, FileIO.c is clean.

**Phase 5b** (MM_Misc.c keyboard blocks): target 21 USBKEYBOARD refs → 0. Keyboard option formatting, interrupt config, buffer handling move into `hal_keyboard_pico.c`. Functions.c/Commands.c keyboard blocks also cleaned.

These can run in any order. Each is a 1-2 commit job since the HAL infrastructure already exists — it's just moving the `#ifdef` bodies into the impls. Do them before Phase 6 so the pattern is validated before tackling audio, display, and net.

**Exit gate for all three:** `tools/hal_scoreboard.sh` shows the target reductions. The original phase exit gates (External.c → 0, FileIO.c → 0, MM_Misc.c USBKEYBOARD → 0) are now met.

### Phase 6 — `hal_audio.h`

- `Audio.c` is 115 KB but mostly dialect logic.
- Phase 6a landed: `hal_audio.h` contract, host impl, VS1053 relocated to `drivers/vs1053/`. Audio.c host arm calls `hal_audio_*`.
- Phase 6b: device arm. Move ~2000 lines of PWM/DMA/codec code into `drivers/pwm_synth/`. Audio.c becomes pure BASIC dialect logic (parsing PLAY command syntax, managing voice slots) with zero hardware `#ifdef`s — all hardware dispatch goes through `hal_audio_*` impls linked per target.
- A `mmbasic_stdio` port would link `hal_audio_hard_error.c` — any PLAY statement returns a BASIC error.

**Exit gate:** Audio.c hardware-ifdef count → 0. Audio bench (`PLAY TONE` 1 kHz for 5 s, capture buffer underruns) shows zero regressions.

### Phase 7a — `hal_display.h` for ILI9341 (the proof)

- **Pilot one display end-to-end before scaling.** ILI9341 SPI LCD is the simplest display backend (no PIO, no multicore, no layer composition), so it pilots the HAL contract.
- Define `hal/hal_display.h` (Tier A slow path: init, set-mode, sync, vsync-wait, scroll, blit-rect; Tier B hot path: `static inline put_pixel` via per-port inline header chosen in Phase 0).
- Define `hal/hal_irq.h` with `HAL_TIME_CRITICAL` macro.
- Implement `drivers/ili9341/` from the current `SPI-LCD.c`, lifted with no behaviour change. Function bodies move; their internals are unchanged.
- `Draw.c` for ILI9341 ports: replace direct `SPI-LCD.c` calls with HAL calls. `gfx_*_shared.c` calls Tier-B inlines for hot pixel paths.
- Picomite (RP2040 + RP2350 USB and non-USB variants) — 4 ports — switch to `drivers/ili9341/`.

**Exit gate:** Draw.c `#ifdef` count drops by the ILI9341-specific gates (measured before and after). Draw.c must have zero `PICOMITE`-only display ifdefs for the ILI9341 path. 4 ports build clean. Smoke-boot on physical RP2040 PicoMite + RP2350 PicoMite. `pico_blocks_tilemap` FPS held; `mand` wall time held. RAM baseline check passes.

### Phase 7b — `hal_display.h` for VGA (PIO)

- `drivers/vga_pio/` lifted from current VGA scanout code. Multi-mode (SCREENMODE1/2/3) — driver internally handles mode dispatch with local `#ifdef` allowed where it simplifies.
- `Draw.c` for VGA ports: VGA-specific code moves into the driver; the driver implements `hal_display`.
- All `PICOMITEVGA` ifdefs in Draw.c move into `drivers/vga_pio/` — the driver owns the conditional logic, not Draw.c.
- 4 VGA ports (RP2040 + RP2350 × USB / non-USB) switch over.

**Exit gate:** Zero `PICOMITEVGA` references in Draw.c. VGA ports build + boot. Scanout integrity verified visually. FASTGFX FPS held.

### Phase 7c — `hal_display.h` for HDMI

- `drivers/hdmi/` lifted from rp2350 HDMI code. Includes the multicore scrolling/clearing path (uses `hal_multicore`, see Phase 8).
- All `HDMI` ifdefs in Draw.c move into `drivers/hdmi/`.
- 2 HDMI ports switch over.

**Exit gate:** Zero `HDMI` references in Draw.c. HDMI ports build + boot. SCREENMODE4/5 verified.

### Phase 7d — `hal_display.h` for SSD1963 + Web variants

- `drivers/ssd1963/` lifted from current `SSD1963.c`. Web variants pull in `drivers/cyw43/` (Phase 9) at the same time.
- 2 Web ports switch over.

**Exit gate:** Draw.c hardware-ifdef count → 0. Zero display-target macros (`PICOMITE`, `PICOMITEVGA`, `HDMI`) in Draw.c. All 12 device ports use `hal_display`. RAM baseline check passes for every target.

### Phase 8 — `hal_multicore.h`

- 23 `multicore_fifo_push_blocking` calls in Draw.c (RP2350 PicoMite + Web). This isn't a peripheral — it's a *cross-core protocol* for scrolling/clearing.
- Define `hal/hal_multicore.h`: `hal_multicore_post(channel, payload)`, `hal_multicore_recv()`, `hal_multicore_init()`, with channel IDs in `hal/hal_multicore_channels.h`.
- Implementations: `ports/pico_sdk_common/hal_multicore_pico.c` (real FIFO), `ports/host_native/hal_multicore_stub.c` (single-core no-op for host).
- Drivers that use multicore (HDMI in particular) call the HAL.

**Exit gate:** No direct `multicore_fifo_*` calls outside `ports/pico_sdk_common/` and drivers. Zero multicore-related ifdefs in scored core files.

### Phase 9 — `hal_net.h`

- PICOMITEWEB only. 67 `PICOMITEWEB` blocks across 8 files.
- Define `hal/hal_net.h` (TCP listen/accept, UDP send/recv, DNS, HTTP server hook).
- `drivers/cyw43/` lifted from current Pico W network code.
- Move all `PICOMITEWEB` conditional bodies out of External.c (13 blocks), MM_Misc.c (15 blocks), Memory.c (12 blocks), FileIO.c (10 blocks), Commands.c (6 blocks), Functions.c (5 blocks), Draw.c (5 blocks), Audio.c (1 block) into `hal_net` impls, `drivers/cyw43/`, or port-config (`PORT_HAS_NETWORK`).
- Non-network ports (including host, mmbasic_stdio) link `hal_net_hard_error.c` — network commands return a BASIC error.

**Exit gate:** Zero `PICOMITEWEB` references in any scored core file. All network dispatch lives in HAL impls or drivers.

### Phase 10 — `hal_heap.h` + Memory.c cleanup

- `Memory.c`'s 37 hardware `#ifdef`s: heap-size choice, PSRAM use, allocator hooks, network buffer allocation, framebuffer memory sizing.
- Define `hal/hal_heap.h`: `hal_heap_size()`, `hal_heap_base()`, `hal_heap_psram_base()`, `hal_heap_psram_size()`.
- `Memory.c` becomes target-neutral; ports + `drivers/psram/` supply heap base + size at link time. Network buffer sizing uses `PORT_HAS_NETWORK`. Framebuffer sizing uses `hal_display_get_framebuffer_size()` or port-config.

**Exit gate:** Memory.c hardware-ifdef count → 0.

### Phase 11 — Sweep + remaining drivers + scope cleanup

- Pick up the smaller systems flagged earlier:
  - **Watchdog / reboot:** `drivers/watchdog_pico/` covers `cmd_watchdog`, `fun_restart`, `cmd_cpu`, `cmd_reset`. RP2040/RP2350 differences live inside the driver.
  - **GPS:** `drivers/gps_uart/`. Move GPS globals out of `host_peripheral_stubs.c`.
  - **Touch / mouse:** `drivers/goodix_touch/`, `drivers/mouse_serial/`. Optional drivers any port can pull in.
  - **GUICONTROLS:** `drivers/gui_controls/`. Pulled in by ports that have display + touch.
  - **CFunctions:** decide whether the embedded-native-code mechanism stays as a core MMBasic feature with a HAL hook (`hal_cfunc_resolve`) or becomes a per-port concern. Resolve the wasm-ld `CallCFunction` warning documented in web-host-plan.
  - **PICOCALC variant (12 blocks):** I²C keyboard selection, pin layout overrides, flash layout. All move into PicoCalc port-config + `drivers/i2c_picocalc_kbd/`.
  - **MM_Misc.c remaining (43 display blocks, 15 rp2350 blocks, 8 hardware-variant blocks):** whatever wasn't absorbed by Phases 5b/7/9. These are the OPTION output formatting blocks that print device identity strings, display option names, etc. Approach: `hal_board_get_option_string()` or similar — the HAL impl generates the board-specific option text.
  - **Commands.c remaining (19 rp2350 blocks, 4 display blocks, 6 network blocks):** PIO clock, COP control, extra RAM commands. rp2350 blocks → HAL or port-config. Display/network blocks should already be gone after Phases 7/9.
  - **Functions.c remaining (7 rp2350 blocks, 1 display, 5 network, 3 keyboard, 1 pin):** should mostly be gone after earlier phases. Sweep catches stragglers.
- `OP_BRIDGE_CMD` interaction: confirm that relocated `cmd_*` functions are still resolved by `commandtbl[].fptr` (they should be — link-time resolution doesn't care about source-tree location).
- Final pass: **every** scored core file must reach 0 hardware `#ifdef`s.

**Exit gate:** `tools/hal_scoreboard.sh` shows 0 for every column. `tools/check_hal_purity.sh` passes for the entire `core/` and `hal/` tree. Every device target builds clean. All 12 device targets boot to REPL on physical hardware (or accurate emulation).

### Phase 12 — Host + WASM relocation

- **Now** the device HAL contract is locked, observed across 12 targets.
- Move `host/host_*.c` files into `ports/host_native/` and `drivers/host_*/` per the layout in the architecture diagram. One commit per file moved, validated by `./run_tests.sh` after each.
- Move `host/host_wasm_*.c` likewise into `ports/host_wasm/` and `drivers/wasm_*/`.
- Subsume `host_fb.h`, `host_fs_hal.h`, `host_keys.h`, `host_terminal.h`, `host_time.h` into the corresponding `hal/*.h` files. Where the host took a shortcut that doesn't fit the device contract, the shortcut is examined — fixed if it's a real divergence, codified into the HAL only if it was right and the device contract was wrong.
- Retire the `host/` directory. Build scripts move to `ports/host_native/build.sh` and `ports/host_wasm/build.sh`. Test harnesses move to `tests/`.
- Top-level CMakeLists.txt files get rewritten as per-port CMake recipes.

**Exit gate:** No `host/` directory. `./run_tests.sh` (now under `tests/`) 192/192. WASM smoke harnesses green. All device targets still build clean (this phase touched no device source).

### Phase 12.5 — `mmbasic_stdio` pure-stdio executable (HAL litmus test)

- Land `ports/mmbasic_stdio/` with the layout above.
- `main.c`: parse `argv[1]` as a `.bas` file (or read stdin to EOF if no argv), tokenise, run, exit. Errors go to stderr. `PRINT` goes to stdout.
- HAL impls: write the minimal set listed above. Most reuse Phase 12 host_native impls (`hal_time`, `hal_filesystem` POSIX). New ones: `hal_keyboard_stdio` (read from stdin, blocking), `hal_display_null` (PRINT path goes to console; pixel/graphics ops error). All hardware-only HALs (`hal_pin`, `hal_audio`, `hal_multicore`, `hal_flash`, `hal_net`) link a `_hard_error` stub that calls MMBasic's `error()` if invoked.
- **Litmus criterion:** the link line for `mmbasic_stdio` must contain **no** files from `drivers/host_fb/`, `drivers/host_termios_kbd/`, `host/host_wasm_*`, `Editor.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, or any display driver. If the linker pulls those in due to undefined references, that's a HAL leak — fix the leak, don't link the file.
- Test harness: `tests/mmbasic_stdio/` runs a corpus of `.bas` programs through the stdio binary and diffs output against expected. Programs that touch hardware-only commands (`PIXEL`, `PLAY`, `PIN`) must produce the documented MMBasic error message via the hard-error stubs.

**Exit gate:** `mmbasic_stdio` builds. Stdio test corpus passes. Link line audit shows no display/REPL/editor files pulled in. The binary is small (target: under 500 KB stripped on x86_64 macOS, since it carries no graphics or filesystem-sim code).

### Phase 13 — Lock the contract

- Wire `tools/check_hal_purity.sh`, `tools/check_ram_baseline.sh`, and the perf microbench into `./run_tests.sh` and into `buildall.sh` so every commit is gated.
- Append "Superseded by `real-hal-plan.md` (Phase 13 complete)" to `bridge-restoration-plan.md`, `host-hal-plan.md`, `web-host-plan.md`. They remain in `docs/` as historical record but contributors know to follow this plan.
- Update MEMORY.md: replace project_host_is_its_own_port and related entries with a single pointer to this plan.
- Land `docs/adding-a-new-board.md`: 1-page guide to creating a new port directory.
- Land `drivers/CONTRIBUTING.md`: rules for new drivers (one peripheral, conformance test required, no cross-driver coupling, RAM-resident annotations honoured).

**Exit gate:** future contributors can't quietly re-introduce target spaghetti, and they have a paved path for adding a new board or driver.

## HAL conformance tests

Each driver implementing a HAL ships with a conformance test under `drivers/<name>/tests/`:

- `hal_display` drivers run `tests/hal_display_conformance.c` (init → put_pixel → blit → sync → scroll → close, plus boundary conditions).
- `hal_storage` drivers run `tests/hal_storage_conformance.c` (write/read round-trip across block boundaries, erase semantics, sync barrier).
- `hal_keyboard` drivers run `tests/hal_keyboard_conformance.c` against a deterministic input script.
- Etc.

The conformance test source lives in `tests/hal/`; each driver's CMake target links it. A driver passes the gate by passing its conformance suite. This catches "driver returns success when it should error" and "driver doesn't honour the HAL ordering contract" — bugs that grep can't find.

## Performance budget — sensitive enough to catch real regressions

`mand` is float-bound and won't catch IRQ jitter, DMA setup cost, or per-opcode regressions. The replacement battery (built in Phase 0):

- **Per-opcode VM microbench.** 10M iterations of a tight loop containing one opcode each. Run for OP_PUSH_NUM, OP_PIXEL, OP_DRAWBOX, OP_FRAMEBUFFER_WRITE, OP_OPEN, OP_INPUT_FILE, OP_BRIDGE_CMD. Report ns/iteration. Gate: per-opcode regression must be under 5%; aggregate `mand` regression under 1%.
- **On-device pixel fill.** 1024×768 `BOX 0,0 TO 1023,767 FILLED, RGB(...)` on host AND on at least one physical RP2040 (ILI9341) and one physical RP2350 (HDMI). Wall time within 2% of baseline.
- **IRQ jitter probe.** Timer IRQ toggles a GPIO pin; logic analyser captures jitter histogram. Baseline committed as `tools/perf_microbench/baselines/irq_jitter_<target>.txt`. Worst-case jitter must not grow >10% between phases.
- **FASTGFX SWAP rate.** `pico_blocks_tilemap` running for 30 s; SWAP rate must hold 50 Hz on web host (Phase 4 baseline) and within 5% of pre-refactor on physical RP2350.
- **Cold boot to REPL.** Under 200 ms on RP2040, under 250 ms on RP2350.

If a HAL boundary costs perf in a hot path, the fix is **not** to add a back-channel `#ifdef`; it's to (a) move the boundary up the call stack so the hot loop is inside the driver, (b) provide an inlinable specialisation in the HAL header per the Tier-B mechanism, or (c) in extremis, accept the regression with explicit documentation in `hal/CONTRACT.md` and a new baseline.

## Safety net (per phase)

1. `cd host/ && ./build.sh && ./run_tests.sh` — 192/192 in default mode (correctness gate, see `CLAUDE.md`).
2. `./buildall.sh` — every device target compiles.
3. `cd host/ && make -f Makefile.wasm` — WASM links (host/WASM untouched until Phase 12, so this is a regression check on the device-side HAL not breaking shared core code).
4. `tools/check_hal_purity.sh` — passes for files in scope this phase.
5. `tools/check_ram_baseline.sh` — passes for every device target (or baseline updated with explicit justification).
6. Performance battery for any phase touching hot paths (Phases 2, 3, 6, 7a-d, 8).
7. Smoke-boot at least one physical device after Phases 7a/7b/7c/7d, 5 (keyboard), 6 (audio). User-driven; document what was tested in the phase commit message.

If any of (1-6) fails, the phase doesn't merge. (7) is required for display/keyboard/audio phases.

## Open questions

1. **Naming.** `hal_display` vs `hal_lcd` vs `hal_video`? Going with `hal_display` — covers VGA/HDMI/LCD/canvas.
2. **Tier-B inlining mechanism.** Per-port inline header (decided in Phase 0, validated by prototype).
3. **Where do shared constants live?** `NBRPINS`, `STRINGSIZE`, `MAXVARS` are MMBasic-level, not HAL. Stay in `core/configuration.h`.
4. **Picomite `Option.PIN` and on-flash settings.** Goes through `hal_flash`. The persistent option block is HAL-storage; the *meaning* of the bytes is core/MMBasic. Watch the boundary — host-hal already had to fix `LoadOptions()` reading garbage from zero-fill flash (see MEMORY.md).
5. **CFunctions.** Defer the architectural decision to Phase 11; document the wasm-ld `CallCFunction` warning resolution there.

## Out of scope (for this plan)

- Adding new boards beyond the existing 12 device targets.
- Replacing FatFS or LFS with a different filesystem.
- Replacing the bytecode VM with a different backend.
- Cross-target binary releases (one binary per target, as today).
- Refactoring the BASIC dialect or the parser.

## Scoreboard (raw grep count — measured, not estimated)

Every row in this table must be measured by running `tools/hal_scoreboard.sh` after the phase lands. The scoreboard is a hard gate, not a trend indicator — if the number didn't go down, the ifdef elimination didn't happen.

```
Phase    Draw.c  MM_Misc  External  FileIO  Commands  Memory  Functions  Audio   Total
0        162     135      120       75      46        37      17         14      606  (measured baseline)
0.5      162     135      120       75      46        37      17         14      606  (state hoist, no #ifdef change)
1-5a     160     134      120       60      45        37      17         14      587  (MEASURED at tip — infra landed, ifdefs mostly unchanged)
─── ifdef elimination passes ───
3b         .       .        0        .       .         .       .          .        .  (External.c pin/PWM/ADC → HAL + port-config)
4b         .       .        .        0       .         .       .          .        .  (FileIO.c host/storage/flash → HAL)
5b         .       .        .        .       .         .       .          .        .  (MM_Misc.c USBKEYBOARD → HAL)
6          .       .        .        .       .         .       .          0        .  (Audio.c → HAL)
7a         .       .        .        .       .         .       .          .        .  (Draw.c ILI9341 → HAL)
7b         .       .        .        .       .         .       .          .        .  (Draw.c VGA → HAL)
7c         .       .        .        .       .         .       .          .        .  (Draw.c HDMI → HAL)
7d         0       .        .        .       .         .       .          .        .  (Draw.c SSD1963 → HAL)
8          .       .        .        .       .         .       .          .        .  (multicore)
9          .       .        0        .       .         .       .          .        .  (net — External.c + FileIO.c PICOMITEWEB)
10         .       .        .        .       .         0       .          .        .  (heap)
11         0       0        0        0       0         0       0          0        0  (sweep)
```

Dots (`.`) mean "not targeted by this phase — carry forward from previous." Zeros are the exit-gate targets. Each row gets filled with measured values when the phase lands.

## Summary

The host-HAL refactor showed the technique on one axis. The web-host port composed it onto a third target. This plan turns the existing pattern into the project's native architecture **for device targets first**, deferring the host/WASM relocation until the device HAL contract is locked across all 12 boards. The interpreter and VM stop knowing what hardware exists; drivers stop competing for the same `#ifdef` namespace; new boards become a directory, not a diff to the core.

Each phase stands alone (after Phase 0.5 hoists shared state), ships with green builds and benchmarks, and leaves the tree better than it found it. The riskiest phase (display) is split into four sub-phases, one per backend, so a stall on HDMI doesn't block ILI9341. Tooling lands in Phase 0 so regressions (RAM placement, IRQ jitter, target-macro leakage) are caught automatically rather than discovered weeks later.

The plan ends with a `mmbasic_stdio` executable (Phase 12.5) — a pure command-line MMBasic that reads a `.bas` file or stdin, prints to stdout, and links zero display/REPL/editor code. Its existence is the structural proof that the HAL contract is genuinely complete: if MMBasic-the-language can run as a Unix tool with nothing but stdio + libc, then the interpreter and VM are truly hardware-clean.
