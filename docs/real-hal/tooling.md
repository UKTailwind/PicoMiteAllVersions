# Real HAL — Tooling, RAM contract, scoreboard, perf budget, safety net

## Scoreboard metric (replaces naive grep count)

Counting `#ifdef` occurrences with grep is gameable: a phase that introduces `#define HAL_FOR_RP2350(x) x` and uses `HAL_FOR_RP2350(multicore_fifo_push_blocking(7))` reduces the textual count to zero while preserving every target-specific code path. Likewise, renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` reduces the rp2350 count to zero without eliminating any conditional compilation. Both are gaming; neither counts as elimination.

**Real metric, enforced by `tools/check_hal_purity.sh`:**

1. **Zero references** to target macros (`PICOMITE`, `PICOMITEVGA`, `HDMI`, `rp2350`, `MMBASIC_HOST`, `MMBASIC_WASM`, `USBKEYBOARD`, `PICOMITEWEB`) in any file under `core/`, `hal/`, or `gfx_*_shared.c`. Detection runs against preprocessor-expanded source (`gcc -E -P -dD`) so macro hiding is caught.
2. **Zero conditional compilation directives** (`#if`, `#ifdef`, `#ifndef`, `#elif defined(...)`) in core files referencing those macros, before or after expansion. **Also zero** conditional compilation directives referencing port-config macros (`HAL_PORT_*`, `PORT_*`). Port-config constants are allowed only as *values* in C expressions; using them as preprocessor gates defeats the abstraction.
3. **All HAL functions called from core have a declaration in `hal/*.h`.** No accidental "core file calls a driver-private symbol" leaks.
4. **HAL headers (`hal/*.h`) contain zero conditional compilation on target or port-config macros.** HAL headers are pure C declarations; the impl file is the place for conditional logic.

The numeric scoreboard (see `scoreboard.md`) tracks the *raw grep count* as a progress metric, but the *gate* is `tools/check_hal_purity.sh`. A phase passes when the gate passes; the scoreboard shows the trend.

`tools/hal_scoreboard.sh` must count **every** `#if*` directive in the configured list of core files, regardless of macro name. The metric is "preprocessor conditionals in core," not "occurrences of `rp2350`." (See fixup plan F1 for the corrective rework.)

## RAM-resident code contract

`__not_in_flash_func(NAME)` is `__attribute__((section(".time_critical." #NAME)))` on Pico SDK targets — it places a function in SRAM so XIP cache misses don't add 5–10× latency on hot paths. RP2040 has 264 KB SRAM total; both *aggressive use* and *aggressive removal* matter. There are 6 in `Draw.c` and 210 project-wide.

**Contract:**

- Any function called from a PIO/DMA/scanline IRQ context, or from the FASTGFX SWAP critical path, must be marked `HAL_TIME_CRITICAL`.
- `HAL_TIME_CRITICAL` expands to `__not_in_flash_func` on Pico SDK ports, to nothing on host/WASM, and to a section attribute on any future MCU port.
- A driver that calls into a HAL function from such a context must document that requirement in its README (`drivers/<name>/README.md`).
- **CI gate:** a script parses the linker map for each device target, lists all functions placed in `.text` (flash) over a size threshold (1 KB) that are reachable from a known IRQ root (PIO IRQs, DMA IRQs, `bc_fastgfx_swap`). If the list grows between commits without an explicit baseline update, the phase fails. The baseline lives in `tools/ram_baseline_<target>.txt` and is updated as part of any phase that intentionally moves code in or out of SRAM.

This is not a Phase 10 cleanup — it's enforced from Phase 0 so a regression can't accumulate silently.

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
6. Performance battery for any phase touching hot paths (Phases 2, 3, 6, 7a–d, 8).
7. Smoke-boot at least one physical device after Phases 7a/7b/7c/7d, 5 (keyboard), 6 (audio). User-driven; document what was tested in the phase commit message.

If any of (1–6) fails, the phase doesn't merge. (7) is required for display/keyboard/audio phases.

## Verification rituals

Copied from the top of the original plan, unchanged:

- **There are no pre-existing failures.** Every commit on `real-hal` must leave `./run_tests.sh` at the full pass count. If a test fails, it's a regression you caused — fix it, do not dismiss it, do not move on. This applies to commits already landed on `real-hal`: if tip shows failures, the last migration commit that "landed green" actually didn't, and those failures are owned by this plan until they're fixed.

- **Never `git stash` + check out an earlier commit to prove a failure is "pre-existing."** The exercise is wasted motion — any failure seen at tip is one to fix now, on the branch, in the next commit. Do not investigate blame, investigate the root cause.

- **When you fix something, rebuild every artefact the user might be running.** The source tree is not the deliverable. `mmbasic_test`, `mmbasic_sim`, and `host/web/picomite.{mjs,wasm,data}` are three distinct binaries — fixing FileIO.c and only rebuilding the one you happened to run tests against leaves the user on stale bytes. Before reporting a fix as landed: rebuild `./host/build.sh`, `./host/build_sim.sh`, and `./host/build_wasm.sh` (when emcc is available). If a fix touches shared core code, all three must be regenerated in the same step.

- **Commit-message pass counts must be scraped, not recalled.** Any commit that states a pass count ("Host N/N", "239/239 green", etc.) must derive that number from the `Results: X passed, Y failed` line of a fresh `./run_tests.sh` invocation run against the built binary immediately before `git commit`. Not from an earlier session, not from `--interp`-only, not from a single-file run. If `Y != 0`, the commit does not go out — fix first. Commit `20a8629` ("Host 239/239") actually shipped at 237/239; the two failures (t195, t197) sat on `real-hal` tip for two commits before they were noticed. This rule exists because of that.

- **Verify-baseline ritual at session start.** Before the first code edit of any new session on `real-hal`, run `./host/build.sh && ./host/run_tests.sh` and report the verbatim `Results:` line. If `Y != 0`, the first task of the session is to fix it — no matter what the user asked for. This catches regressions the previous session shipped but didn't notice.

- **`run_tests.sh` single-file mode must honour `RUN_ARGS:`.** Single-file invocations (`./run_tests.sh tests/t195_save_image_posix.bas --interp`) today pass `$MODE` but ignore the test file's `RUN_ARGS:` header, so `--sd-root=/tmp/mmbasic-t195` never reaches the binary. That's why spot-checking a named failing test returned PASS while the full suite was red — the POSIX-routing path isn't exercised in single-file mode. Fix: parse RUN_ARGS in the single-file branch the same way `run_one_test()` already does.
