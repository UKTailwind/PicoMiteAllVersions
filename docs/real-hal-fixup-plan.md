# Real HAL Fixup Plan

Short, self-contained correction plan for the `real-hal` branch. Written 2026-04-22 after an audit found Phase 3b was gaming the ifdef scoreboard by renaming conditionals instead of eliminating them. This doc is the standard and the active work queue. `docs/real-hal-plan.md` is the slim index and `docs/real-hal/` holds per-topic detail; their claims for Phases 3/4/5 and 3b are superseded by F1–F5 below.

## The standard (non-negotiable)

A core file is "HAL-clean" when **all three** hold:

1. **Zero `#if` / `#ifdef` / `#ifndef` / `#elif` lines referencing any target macro** — `rp2350`, `PICOMITE`, `PICOMITEVGA`, `PICOMITEWEB`, `MMB4L`, `USBKEYBOARD`, `MMBASIC_HOST`, `MMBASIC_WASM`, or any successor name for the same concept.
2. **Zero `#if` / `#ifdef` / `#ifndef` lines referencing port-config macros** (`HAL_PORT_*`, `PORT_*`, etc.). Port-config constants are allowed as *values* in expressions and array sizes; they are not allowed as preprocessor gates. Renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` does not count as elimination.
3. **Conditional bodies live in HAL impl files or drivers**, not in headers that core includes. `hal/*.h` is pure contract — no target macros, no port-config ifdefs, no feature gates.

If a block of code can't meet (1)+(2)+(3), the fix is to push its body behind a HAL function call, not to rename the gate.

## What to revert

Two commits introduced the renamed-gate pattern and must be undone before new work lands:

- `2c034d7` — "Phase 3b: port-config mechanism + External.c rp2350 ifdef elimination"
- `61cb08e` — "Phase 3b: FileIO.c rp2350 ifdef elimination + plan status update"

Revert via `git revert` (not reset — keep history honest). This restores External.c and FileIO.c to their explicit `#ifdef rp2350` state and removes the leaky `hal/hal_port_config.h`.

## Fixup phases

### F1. Revert and instrument (1 commit)

- `git revert 61cb08e 2c034d7`
- Rewrite `tools/hal_scoreboard.sh` to count **every** `#if*` directive in a configured list of core files, regardless of macro name. The metric is "preprocessor conditionals in core," not "occurrences of `rp2350`." Record new baseline.
- Extend `tools/check_hal_purity.sh` to fail on any `#if*` in `hal/*.h` — no target macros, no port-config macros, no exceptions. HAL headers are pure C declarations.
- Run both gates in CI-equivalent fashion locally. Both must pass (purity) or be explicitly waived with a recorded number (scoreboard) before any F2+ commit lands.

Exit: scoreboard measures the real thing; purity gate is honest; baseline is re-captured.

### F2. External.c — Phase 3 redo, done right

Target: External.c contains **zero preprocessor conditionals on target or port macros.** Allowed: plain `#include`, function-body `if (...)` runtime checks against a global like `rp2350a`.

Approach:
- For each remaining `#ifdef rp2350` / `#ifdef PICOMITEVGA` / `#ifdef PICOMITEWEB` block, identify whether the body is (a) hardware setup, (b) pin-table data, (c) feature gating.
  - (a) → move the body into a new or existing `hal_pin_*.c` function (e.g. `hal_pin_init_extended_pwm()`); core calls it unconditionally.
  - (b) → port owns its own pin table in a port-specific `.c`; core reads through `hal_pin_table()` or a `port_pin_count` constant. Constants are fine as *values*; conditional compilation isn't.
  - (c) → if a feature is genuinely absent on a port, the HAL function is a no-op on that port. Core still calls it.
- No `hal_port_config.h` with `#ifdef` inside. If a constant needs to differ by port, each port's own `.c` (or a port-specific header that only *that port* includes) defines it. Core sees a single value through a HAL accessor.

Exit: `grep -E '^\s*#\s*(if|ifdef|ifndef|elif)' External.c` returns zero target-macro and zero port-config hits. Scoreboard drop is real.

### F3. FileIO.c — Phase 4 redo

Same approach as F2. Filesystem-specific branches (rp2350 flash geometry, VGA SD wiring, web MEMFS) push into `hal_filesystem_*.c` impls. Core sees `hal_fs_*()` calls.

Exit: FileIO.c has zero target/port-macro ifdefs.

### F4. MM_Misc.c — Phase 5 redo

USBKEYBOARD blocks and remaining target ifdefs in MM_Misc.c route through `hal_keyboard_*()`. Devices without USB keyboard get no-op impls.

Exit: MM_Misc.c has zero target/port-macro ifdefs.

### F5. Sweep and gate

- Re-run all 12 device builds + host + WASM. `./run_tests.sh` must be 239/239 (or current number, whichever is higher — never lower).
- Purity gate green. Scoreboard shows the real reduction against F1's honest baseline.
- Doc updates (in order):
  - `docs/real-hal/phase-3-pin.md`, `phase-4-filesystem.md`, `phase-5-keyboard.md` — mark 🔧 → ✅, drop the "what remains" sections, cite the F2/F3/F4 commits.
  - `docs/real-hal/scoreboard.md` — fill the F2/F3/F4 rows with measured values.
  - `docs/real-hal-plan.md` — flip the status-table rows for Phases 3/4/5 from 🔧 to ✅.
  - `docs/real-hal/retrospective.md` — append an F5 closure note.

Only after F5 is clean does Phase 6 resume.

## Rules for all future HAL phases

- A phase closes only when its targeted core files pass the "zero ifdefs" standard above, not when the HAL header + call sites land.
- "Infrastructure landed" is not a phase-exit state. It's halfway. Either finish the elimination or don't claim the phase.
- Scoreboard deltas must be measured by the real gate, not by grepping for old macro names.
- No new `hal_port_config.h`-style shims. If you feel tempted to write one, re-read F2.

## Doc hygiene

This fixup doc stays short on purpose. ~120 lines is the target ceiling for any doc read every session. If it grows past that, split it. The same rule applies to per-phase docs under `docs/real-hal/` — if one of them needs to grow past ~150 lines, carve an appendix rather than letting the phase doc bloat.
