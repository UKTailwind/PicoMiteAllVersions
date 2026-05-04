# Real HAL Plan — Index

Promote the implicit hardware-abstraction layer that emerged from the host port (`host-hal-plan.md`) into a **first-class HAL spanning every device target** — RP2040 and RP2350 in all 12 board variants — so the BASIC interpreter and bytecode VM compile with **zero references to hardware target macros**, and target variants are selected by directory composition, not by preprocessor surgery.

This file is the index. Topic detail lives under `real-hal/` and `real-hal-fixup-plan.md`. Read this page plus the one sub-doc you're touching; don't page through the whole set.

- **Branch:** `real-hal` (off `main`).
- **Predecessor plans:** `bridge-restoration-plan.md`, `host-hal-plan.md`, `web-host-plan.md`. Locked invariants from those plans are not revisited here.
- **Active fixup:** `real-hal-fixup-plan.md` — the correction for the Phase 3b ifdef-rename episode. Read this before touching any HAL work.

## The standard (non-negotiable)

A core file is "HAL-clean" when **all four** hold:

1. **Zero `#if` / `#ifdef` / `#ifndef` / `#elif` lines referencing any target macro** — `rp2350`, `PICOMITE`, `PICOMITEVGA`, `PICOMITEWEB`, `MMB4L`, `USBKEYBOARD`, `MMBASIC_HOST`, `MMBASIC_WASM`, or any successor name for the same concept.
2. **Zero `#if` / `#ifdef` / `#ifndef` lines referencing port-config macros** (`HAL_PORT_*`, `PORT_*`). Port-config constants are allowed as *values* in C expressions and array sizes; they are not allowed as preprocessor gates. Renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` does not count as elimination.
3. **Zero `if (HAL_PORT_*)` / `if (!HAL_PORT_*)` runtime folds** in C statements (with case-by-case exceptions, see below). Replacing `#if HAL_PORT_X` with `if (HAL_PORT_X) { ... }` is gaming the rule, not eliminating the gate — the source still hard-codes a port-shape branch and the compiler folding it to dead code doesn't make the code portable. Acceptable elimination strategies:
   - HAL hooks with real-impl + stub driver pair (mutually-exclusive linkage).
   - File relocation (move the divergent body into a per-port-shape driver TU).
   - Universal struct-field guards (`if (Option.X)`) where the field exists on every port.
   - Per-port palette macros / per-port `port_*.h` files included by name.

   Runtime checks on `Option.*` fields are fine — those reflect user-configurable state, not port shape. Pico SDK target macros (`#ifdef rp2350`) and host-build wrappers (`#if defined(MMBASIC_HOST)`) remain allowed per the existing scope rules.

   **Exception clause:** rule 3 admits case-by-case exceptions where modularizing the site is genuinely impractical — e.g. a two-line divergence where a hook would be overkill, or a per-port split that would invent load-bearing helpers nowhere else needs. When taking an exception, leave a one-line `/* port-shape inline: <reason> */` comment so a future drain pass can re-evaluate. The default is elimination; the exception is the rare site where elimination harms readability.
4. **Conditional bodies live in HAL impl files or drivers**, not in headers that core includes. `hal/*.h` is pure contract — no target macros, no port-config ifdefs, no feature gates.

A phase closes only when its targeted core files pass that standard. "Infrastructure landed" (HAL header + call sites migrated) is not a phase-exit state. Either finish the ifdef elimination or leave the phase marked 🔧.

The strict purity gate (`tools/check_hal_purity.sh`) currently catches rules 1, 2, and 4. Rule 3 is enforced by review until the gate is extended — running `grep -nE "if \(!?HAL_PORT_" path/to/file.c` is the manual check.

## Goals (unchanged from the original plan)

1. **Hardware-clean core.** `MMBasic.c`, `Operators.c`, `Functions.c`, `Commands.c`, `bc_*.c`, and the VM syscall layer reference no target macros.
2. **HAL-clean device files.** `Draw.c`, `FileIO.c`, `Audio.c`, `MM_Misc.c`, `External.c`, `Memory.c` call HAL entry points instead of branching on target macros. Algorithmic dispatch moves into `gfx_*_shared.c` or driver files; what's left in the core file is BASIC dialect logic.
3. **Composable directory layout.** A device target = a port directory + a list of HAL implementations to link.
4. **No performance regression.** HAL is **compile-time-bound** (link-time symbol selection, `static inline` in headers where hot). No vtables, no function pointers in pixel-write or sample-output paths. RAM-resident hot-path placement is treated as a load-bearing contract, not a hint.
5. **Incremental and safe.** Every commit: `./run_tests.sh` full-pass green, every device target in `buildall.sh` builds clean, host build passes, WASM build links. Performance gates run on phases that touch hot paths.
6. **Pure-stdio MMBasic executable.** Phase 12.5 delivers `mmbasic` — argv-driven, no display, no REPL, no editor. It's the litmus test that the HAL contract is genuinely complete.

## Non-goals

- Runtime polymorphism. Target selection happens at build time, not runtime.
- API neutrality across the BASIC dialect. Programs see the same MMBasic.
- Rewriting drivers. Existing drivers move into per-driver directories with no behavioural change.
- Reorganising the host/WASM ports. They stay in `host/` until Phase 12.

## Phase status (one line each; follow the link for detail)

| Phase | Status | One-line state |
|-------|--------|----------------|
| [0 — tooling](real-hal/phases-0-to-2.md) | ✅ partial | scaffolding + purity gate + scoreboard landed; RAM-baseline + perf microbench + Tier-B prototype need physical device |
| [0.5 — state hoist](real-hal/phases-0-to-2.md) | ✅ | display/option/audio state moved into `core/state/`; `PinDef[]` corrected to stay in board files |
| [1 — hal_flash](real-hal/phases-0-to-2.md) | ✅ | 52 flash call sites routed through HAL; `Include.h` still pulls `hardware/flash.h` — clean in fixup F2 |
| [2 — hal_time](real-hal/phases-0-to-2.md) | ✅ | 42 core call sites migrated; `pico_blocks_tilemap` 50 Hz invariant held |
| [3 — hal_pin](real-hal/phase-3-pin.md) | ✅ | 3a + F2 done; External.c is in STRICT_FILES (zero target/port-config ifdefs) |
| [4 — hal_storage + hal_filesystem](real-hal/phase-4-filesystem.md) | ✅ | 4a + F3 done; FileIO.c is in STRICT_FILES (zero target/port-config ifdefs) |
| [5 — hal_keyboard](real-hal/phase-5-keyboard.md) | ✅ | 5a + F4 done; MM_Misc.c is in STRICT_FILES (zero target/port-config ifdefs) |
| [6 — hal_audio](real-hal/phase-6-audio.md) | ✅ | 6a + 6b done; Audio.c in STRICT_FILES; device body in drivers/pwm_synth/ |
| [7 — hal_display](real-hal/phase-7-display.md) | ✅ | 7a + 7b + 7c closed — Draw.c 164 → 3, STRICT_FILES; `drivers/hdmi/` owns modes + scanout; 7d (SSD1963) optional refinement remains |
| [8 — hal_multicore](real-hal/phases-8-to-13.md#phase-8--hal_multicoreh) | ✅ | 8 steps 1–3 done; no direct multicore_fifo_* outside drivers; driver-owned pattern (no generic HAL contract) |
| [9 — hal_net](real-hal/phases-8-to-13.md#phase-9--hal_neth) | ✅ | PICOMITEWEB references across core files 30 → 0 (5 steps); routed through port hooks / runtime PSRAMsize / port-config macros; no `hal/hal_net.h` contract introduced |
| [10 — hal_heap + Memory.c](real-hal/phases-8-to-13.md#phase-10--hal_heaph--memoryc-cleanup) | ✅ | Memory.c 39 → 1 (#ifdef GUICONTROLS feature flag only); promoted to STRICT_FILES; PSRAM bitmap + allocator in drivers/psram_heap/; VGA framebuffer + tile state in drivers/vga_pio/vga_memory.c; unified AllMemory layout via `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES` + `HAL_PORT_ALLMEMORY_ALIGN` port-config macros. No `hal/hal_heap.h` contract introduced. |
| [11 — sweep](real-hal/phases-8-to-13.md#phase-11--sweep--remaining-drivers--scope-cleanup) | ✅ partial | All 8 main-tracked core files in STRICT_FILES at zero target-macro ifdefs (Commands + Functions promoted this phase). Target-macro references across all scored core files: 0. INFO-tracked VM files still have some ifdefs (bc_runtime, MMBasic, vm_sys_graphics, vm_sys_file, vm_sys_time, vm_sys_pin, bc_debug). |
| [12 — host + WASM relocation](real-hal/phases-8-to-13.md#phase-12--host--wasm-relocation) | ✅ | source-code retirement closed (steps 1–13); every host/*.c + host/*.h relocated under ports/host_{native,wasm}/; canonical builds are ports/host_native/Makefile + ports/host_wasm/Makefile |
| 12.5 — mmbasic_stdio | ✅ functional | ports/mmbasic_stdio/ runs BASIC via stdin/stdout; link-line audit clean (no Editor/REPL/Prompt/display objects); exit-gate polish (test corpus + --gc-sections size) bundled with Phase 13 |
| 12.6 — host/ full retirement | ❌ skipped | cosmetic move of remaining user-facing tooling (run_tests.sh, demos/, web/, README.md). Architectural goal already met by Phase 12; churn (CI, buildall.sh, serve.py, ~20 doc URLs) outweighs benefit. Do opportunistically if a third native port ever lands. |
| [13 — lock contract](real-hal/phases-8-to-13.md#phase-13--lock-the-contract) | ⏳ | not started |

Tests 239/239 on `real-hal` tip. All 12 device CMake variants green. WASM link green. Grand scoreboard at phase 6 close (`899c218`): **267**.

**Validation model (all phases, 0–13):** a phase closes when its scoped
core file is in `STRICT_FILES` with zero target-macro and zero port-
config-macro ifdefs, `buildall.sh` is clean across all 12 device CMake
variants, host `./run_tests.sh` is 239/239 (or whatever the current
number is — never lower), and `tools/check_hal_purity.sh` is green.
Physical-device smoke-boot / FPS / RAM-baseline checks listed in some
phase docs are desirable post-merge verifications but not blockers —
they were written before Phase 6 closed and held to the same triple
(compile + host tests + purity). Don't skip or reorder phases on the
basis of "this one needs hardware testing" — they all have the same
exit gate in practice.

## Topic reference (shared across phases)

- [architecture.md](real-hal/architecture.md) — directory layout, composition example, Tier-B inlining mechanism, host-deferred rationale, Phase 0 baseline survey.
- [contracts.md](real-hal/contracts.md) — HAL contract sketches for all 10 HAL headers plus `hal_irq` and stub-for-stdio.
- [port-config.md](real-hal/port-config.md) — port-config mechanism (with the failure mode to avoid), combinatorial driver-local ifdefs, cross-cutting state hoist.
- [tooling.md](real-hal/tooling.md) — scoreboard metric, RAM-resident contract, conformance tests, performance budget, safety net, session-start rituals.
- [retrospective.md](real-hal/retrospective.md) — end-of-phase-5a course correction plus the 2026-04-22 audit finding that spawned the fixup plan.
- [scoreboard.md](real-hal/scoreboard.md) — raw ifdef counts per file per phase, baseline + targets.

## How to work a phase

1. Read this index + the one phase file you're touching + the fixup plan.
2. If the phase has a 🔧 marker: the HAL surface exists, the remaining work is ifdef elimination per the fixup-plan standard. Do not ship a rename.
3. Run `./host/build.sh && ./host/run_tests.sh` *before* your first edit. If the `Results:` line shows failures, fix them first — regardless of the task you were going to do (see tooling.md session rituals).
4. When closing a phase: run `tools/check_hal_purity.sh` and the rewritten `tools/hal_scoreboard.sh` (per fixup F1). Both must be green. Update the phase file's status and the scoreboard row. Commit.

## Summary

The host-HAL refactor showed the technique on one axis. The web-host port composed it onto a third target. This plan turns the existing pattern into the project's native architecture **for device targets first**, deferring the host/WASM relocation until the device HAL contract is locked across all 12 boards. The riskiest phase (display) is split into four sub-phases, one per backend, so a stall on HDMI doesn't block ILI9341. Tooling lands in Phase 0 so regressions (RAM placement, IRQ jitter, target-macro leakage) are caught automatically rather than discovered weeks later.
