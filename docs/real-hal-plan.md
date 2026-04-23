# Real HAL Plan — Index

Promote the implicit hardware-abstraction layer that emerged from the host port (`host-hal-plan.md`) into a **first-class HAL spanning every device target** — RP2040 and RP2350 in all 12 board variants — so the BASIC interpreter and bytecode VM compile with **zero references to hardware target macros**, and target variants are selected by directory composition, not by preprocessor surgery.

This file is the index. Topic detail lives under `real-hal/` and `real-hal-fixup-plan.md`. Read this page plus the one sub-doc you're touching; don't page through the whole set.

- **Branch:** `real-hal` (off `main`).
- **Predecessor plans:** `bridge-restoration-plan.md`, `host-hal-plan.md`, `web-host-plan.md`. Locked invariants from those plans are not revisited here.
- **Active fixup:** `real-hal-fixup-plan.md` — the correction for the Phase 3b ifdef-rename episode. Read this before touching any HAL work.

## The standard (non-negotiable)

A core file is "HAL-clean" when **all three** hold:

1. **Zero `#if` / `#ifdef` / `#ifndef` / `#elif` lines referencing any target macro** — `rp2350`, `PICOMITE`, `PICOMITEVGA`, `PICOMITEWEB`, `MMB4L`, `USBKEYBOARD`, `MMBASIC_HOST`, `MMBASIC_WASM`, or any successor name for the same concept.
2. **Zero `#if` / `#ifdef` / `#ifndef` lines referencing port-config macros** (`HAL_PORT_*`, `PORT_*`). Port-config constants are allowed as *values* in C expressions and array sizes; they are not allowed as preprocessor gates. Renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` does not count as elimination.
3. **Conditional bodies live in HAL impl files or drivers**, not in headers that core includes. `hal/*.h` is pure contract — no target macros, no port-config ifdefs, no feature gates.

A phase closes only when its targeted core files pass that standard. "Infrastructure landed" (HAL header + call sites migrated) is not a phase-exit state. Either finish the ifdef elimination or leave the phase marked 🔧.

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
| [3 — hal_pin](real-hal/phase-3-pin.md) | 🔧 | 3a (HAL + 107 call sites) done; 3b attempt was an ifdef rename, to be reverted and redone per fixup F2 |
| [4 — hal_storage + hal_filesystem](real-hal/phase-4-filesystem.md) | ✅ | 4a + F3 done; FileIO.c is in STRICT_FILES (zero target/port-config ifdefs) |
| [5 — hal_keyboard](real-hal/phase-5-keyboard.md) | 🔧 | 5a done; MM_Misc.c `USBKEYBOARD` elimination redo per fixup F4 |
| [6 — hal_audio](real-hal/phase-6-audio.md) | 🟡 | 6a (host arm + VS1053 relocation) done; device arm (6b) pending |
| [7 — hal_display](real-hal/phase-7-display.md) | ⏳ | four sub-phases (ILI9341 / VGA / HDMI / SSD1963) not started |
| [8 — hal_multicore](real-hal/phases-8-to-13.md#phase-8--hal_multicoreh) | ⏳ | not started |
| [9 — hal_net](real-hal/phases-8-to-13.md#phase-9--hal_neth) | ⏳ | not started; 67 `PICOMITEWEB` blocks to clear |
| [10 — hal_heap + Memory.c](real-hal/phases-8-to-13.md#phase-10--hal_heaph--memoryc-cleanup) | ⏳ | not started |
| [11 — sweep](real-hal/phases-8-to-13.md#phase-11--sweep--remaining-drivers--scope-cleanup) | ⏳ | not started |
| [12 — host + WASM relocation](real-hal/phases-8-to-13.md#phase-12--host--wasm-relocation) | ⏳ | not started |
| [12.5 — mmbasic_stdio](real-hal/phases-8-to-13.md#phase-125--mmbasic_stdio-pure-stdio-executable-hal-litmus-test) | ⏳ | not started; the HAL litmus test |
| [13 — lock contract](real-hal/phases-8-to-13.md#phase-13--lock-the-contract) | ⏳ | not started |

Tests 239/239 on `real-hal` tip. All 12 device CMake variants green. WASM link green.

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
