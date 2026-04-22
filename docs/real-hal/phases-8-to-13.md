# Real HAL — Phases 8 through 13

Sketches for the later phases. These are not started and their details will firm up as earlier phases complete.

## Phase 8 — `hal_multicore.h`

23 `multicore_fifo_push_blocking` calls in Draw.c (RP2350 PicoMite + Web). This isn't a peripheral — it's a *cross-core protocol* for scrolling/clearing.

- Define `hal/hal_multicore.h`: `hal_multicore_post(channel, payload)`, `hal_multicore_recv()`, `hal_multicore_init()`, with channel IDs in `hal/hal_multicore_channels.h`.
- Implementations: `ports/pico_sdk_common/hal_multicore_pico.c` (real FIFO), `ports/host_native/hal_multicore_stub.c` (single-core no-op for host).
- Drivers that use multicore (HDMI in particular) call the HAL.

**Exit gate:** no direct `multicore_fifo_*` calls outside `ports/pico_sdk_common/` and drivers. Zero multicore-related ifdefs in scored core files.

**Commit-count target:** 1 commit.

## Phase 9 — `hal_net.h`

PICOMITEWEB only. 67 `PICOMITEWEB` blocks across 8 files.

- Define `hal/hal_net.h` (TCP listen/accept, UDP send/recv, DNS, HTTP server hook).
- `drivers/cyw43/` lifted from current Pico W network code.
- Move all `PICOMITEWEB` conditional bodies out of External.c (13 blocks), MM_Misc.c (15 blocks), Memory.c (12 blocks), FileIO.c (10 blocks), Commands.c (6 blocks), Functions.c (5 blocks), Draw.c (5 blocks), Audio.c (1 block) into `hal_net` impls, `drivers/cyw43/`, or port-config (`PORT_HAS_NETWORK`).
- Non-network ports (including host, mmbasic_stdio) link `hal_net_hard_error.c` — network commands return a BASIC error.

**Exit gate:** zero `PICOMITEWEB` references in any scored core file. All network dispatch lives in HAL impls or drivers.

**Commit-count target:** 1–2 commits.

## Phase 10 — `hal_heap.h` + Memory.c cleanup

Memory.c's 37 hardware `#ifdef`s: heap-size choice, PSRAM use, allocator hooks, network buffer allocation, framebuffer memory sizing.

- Define `hal/hal_heap.h`: `hal_heap_size()`, `hal_heap_base()`, `hal_heap_psram_base()`, `hal_heap_psram_size()`.
- Memory.c becomes target-neutral; ports + `drivers/psram/` supply heap base + size at link time. Network buffer sizing uses `PORT_HAS_NETWORK`. Framebuffer sizing uses `hal_display_get_framebuffer_size()` or port-config.

**Exit gate:** Memory.c hardware-ifdef count → 0.

**Commit-count target:** 1 commit.

## Phase 11 — Sweep + remaining drivers + scope cleanup

Pick up the smaller systems:

- **Watchdog / reboot:** `drivers/watchdog_pico/` covers `cmd_watchdog`, `fun_restart`, `cmd_cpu`, `cmd_reset`. RP2040/RP2350 differences live inside the driver.
- **GPS:** `drivers/gps_uart/`. Move GPS globals out of `host_peripheral_stubs.c`.
- **Touch / mouse:** `drivers/goodix_touch/`, `drivers/mouse_serial/`. Optional drivers any port can pull in.
- **GUICONTROLS:** `drivers/gui_controls/`. Pulled in by ports that have display + touch.
- **CFunctions:** decide whether the embedded-native-code mechanism stays as a core MMBasic feature with a HAL hook (`hal_cfunc_resolve`) or becomes a per-port concern. Resolve the wasm-ld `CallCFunction` warning documented in web-host-plan.
- **PICOCALC variant (12 blocks):** I²C keyboard selection, pin layout overrides, flash layout. All move into PicoCalc port-config + `drivers/i2c_picocalc_kbd/`.
- **MM_Misc.c remaining:** OPTION output formatting blocks that print device identity strings, display option names, etc. Approach: `hal_board_get_option_string()` or similar — the HAL impl generates the board-specific option text.
- **Commands.c remaining:** PIO clock, COP control, extra RAM commands. rp2350 blocks → HAL or port-config. Display/network blocks should already be gone after Phases 7/9.
- **Functions.c remaining:** should mostly be gone after earlier phases. Sweep catches stragglers.

`OP_BRIDGE_CMD` interaction: confirm that relocated `cmd_*` functions are still resolved by `commandtbl[].fptr` (they should be — link-time resolution doesn't care about source-tree location).

Final pass: **every** scored core file must reach 0 hardware `#ifdef`s (and, per the fixup plan, 0 port-config `#if*` gates too).

**Exit gate:** `tools/hal_scoreboard.sh` shows 0 for every column. `tools/check_hal_purity.sh` passes for the entire `core/` and `hal/` tree. Every device target builds clean. All 12 device targets boot to REPL on physical hardware (or accurate emulation).

**Commit-count target:** 3–5 commits.

## Phase 12 — Host + WASM relocation

Now the device HAL contract is locked, observed across 12 targets.

- Move `host/host_*.c` files into `ports/host_native/` and `drivers/host_*/`. One commit per file moved, validated by `./run_tests.sh` after each.
- Move `host/host_wasm_*.c` likewise into `ports/host_wasm/` and `drivers/wasm_*/`.
- Subsume `host_fb.h`, `host_fs_hal.h`, `host_keys.h`, `host_terminal.h`, `host_time.h` into the corresponding `hal/*.h` files. Where the host took a shortcut that doesn't fit the device contract, the shortcut is examined — fixed if it's a real divergence, codified into the HAL only if it was right and the device contract was wrong.
- Retire the `host/` directory. Build scripts move to `ports/host_native/build.sh` and `ports/host_wasm/build.sh`. Test harnesses move to `tests/`.
- Top-level CMakeLists.txt files get rewritten as per-port CMake recipes.

**Exit gate:** no `host/` directory. `./run_tests.sh` (now under `tests/`) 192/192. WASM smoke harnesses green. All device targets still build clean (this phase touched no device source).

## Phase 12.5 — `mmbasic_stdio` pure-stdio executable (HAL litmus test)

- Land `ports/mmbasic_stdio/` with the layout from `architecture.md`.
- `main.c`: parse `argv[1]` as a `.bas` file (or read stdin to EOF if no argv), tokenise, run, exit. Errors go to stderr. `PRINT` goes to stdout.
- HAL impls: write the minimal set listed in `architecture.md`. Most reuse Phase 12 host_native impls (`hal_time`, `hal_filesystem` POSIX). New ones: `hal_keyboard_stdio` (read from stdin, blocking), `hal_display_null` (PRINT path goes to console; pixel/graphics ops error). All hardware-only HALs (`hal_pin`, `hal_audio`, `hal_multicore`, `hal_flash`, `hal_net`) link a `_hard_error` stub that calls MMBasic's `error()` if invoked.
- **Litmus criterion:** the link line for `mmbasic_stdio` must contain **no** files from `drivers/host_fb/`, `drivers/host_termios_kbd/`, `host/host_wasm_*`, `Editor.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, or any display driver. If the linker pulls those in due to undefined references, that's a HAL leak — fix the leak, don't link the file.
- Test harness: `tests/mmbasic_stdio/` runs a corpus of `.bas` programs through the stdio binary and diffs output against expected. Programs that touch hardware-only commands (`PIXEL`, `PLAY`, `PIN`) must produce the documented MMBasic error message via the hard-error stubs.

**Exit gate:** `mmbasic_stdio` builds. Stdio test corpus passes. Link line audit shows no display/REPL/editor files pulled in. The binary is small (target: under 500 KB stripped on x86_64 macOS, since it carries no graphics or filesystem-sim code).

## Phase 13 — Lock the contract

- Wire `tools/check_hal_purity.sh`, `tools/check_ram_baseline.sh`, and the perf microbench into `./run_tests.sh` and into `buildall.sh` so every commit is gated.
- Append "Superseded by `real-hal-plan.md` (Phase 13 complete)" to `bridge-restoration-plan.md`, `host-hal-plan.md`, `web-host-plan.md`. They remain in `docs/` as historical record but contributors know to follow this plan.
- Update MEMORY.md: replace project_host_is_its_own_port and related entries with a single pointer to this plan.
- Land `docs/adding-a-new-board.md`: 1-page guide to creating a new port directory.
- Land `drivers/CONTRIBUTING.md`: rules for new drivers (one peripheral, conformance test required, no cross-driver coupling, RAM-resident annotations honoured).

**Exit gate:** future contributors can't quietly re-introduce target spaghetti, and they have a paved path for adding a new board or driver.

## Open questions (resolved as phases land)

1. **Naming.** `hal_display` vs `hal_lcd` vs `hal_video`? Going with `hal_display` — covers VGA/HDMI/LCD/canvas.
2. **Tier-B inlining mechanism.** Per-port inline header (decided in Phase 0, validated by prototype).
3. **Where do shared constants live?** `NBRPINS`, `STRINGSIZE`, `MAXVARS` are MMBasic-level, not HAL. Stay in `core/configuration.h`.
4. **Picomite `Option.PIN` and on-flash settings.** Goes through `hal_flash`. The persistent option block is HAL-storage; the *meaning* of the bytes is core/MMBasic.
5. **CFunctions.** Defer the architectural decision to Phase 11; document the wasm-ld `CallCFunction` warning resolution there.

## Out of scope (for this plan)

- Adding new boards beyond the existing 12 device targets.
- Replacing FatFS or LFS with a different filesystem.
- Replacing the bytecode VM with a different backend.
- Cross-target binary releases (one binary per target, as today).
- Refactoring the BASIC dialect or the parser.
