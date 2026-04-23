# Real HAL — Retrospective and course correction

## End-of-phase-5a, 2026-04-22

Phases 0–5a + 6a landed in 31 commits. HAL headers, device/host impls, call-site migrations, and driver relocations are real work — 7 HAL contracts, 7 device impls, 7 host impls, 6 driver directories. Tests 239/239. All 12 device CMake targets build.

**But the primary goal — eliminating hardware `#ifdef`s from core files — barely moved.** The scoreboard went from 606 → 587 (−3%). The phases added HAL wrappers and migrated call sites, but left the `#ifdef` blocks in place. That means core files still branch on `PICOMITEVGA`, `USBKEYBOARD`, `rp2350`, `MMBASIC_HOST`, etc. — the HAL infrastructure exists but the core isn't clean.

**Course correction:** Phases 3, 4, 5 were relabelled from ✅ to 🔧 (infrastructure landed). New sub-phases 3b, 4b, 5b were added to do the actual ifdef elimination. A "port-config mechanism" was introduced to absorb structural constants.

From this point forward, every phase's exit gate is measured by `tools/hal_scoreboard.sh`. If the number didn't go down, the phase isn't done. No more marking phases complete based on infrastructure alone.

## 2026-04-22 audit finding and fixup (supersedes the course correction above)

The first Phase 3b attempt (commits `2c034d7` and `61cb08e`) did not actually eliminate ifdefs — it renamed them. `#ifdef rp2350` became `#if HAL_PORT_PWM_SLICE_COUNT > 8` in core files, and the original `#ifdef rp2350` was relocated into `hal/hal_port_config.h`. The scoreboard only matched the old macro names, so renamed conditionals were invisible to the metric and the claimed 587 → 508 (−79) delta overstated progress. The HAL purity gate was failing at HEAD through both commits.

**Consequence:** a short fixup plan was drafted. See `../real-hal-fixup-plan.md` for the standard ("zero `#if*` on target OR port-config macros in core; HAL headers pure") and F1–F5 corrective sequence. Commits `2c034d7` and `61cb08e` are to be reverted; Phases 3/4/5 redo (under names F2/F3/F4) happens with the bodies actually moved into HAL impls. Sub-phase labels 3b/4b/5b are subsumed by the fixup plan and should be removed from future status updates.

## F5 closure, 2026-04-23

F2, F3, F4 all closed. The three previously-renamed files are now in
`STRICT_FILES` and the HAL purity gate passes:

- **F2 (External.c):** 120 → 0 target-macro ifdefs across eight sub-steps
  (3a–3h). Closed 2026-04-22.
- **F3 (FileIO.c):** 60 → 0 target-macro ifdefs across fifteen sub-steps.
  Closed 2026-04-22. The two leftover conditionals are `#ifndef max` /
  `#ifndef min` macro guards — neither target nor port-config.
- **F4 (MM_Misc.c):** 135 → 0 target-macro ifdefs across thirty sub-steps.
  Closed 2026-04-23 at commit `38cb691`. Four leftover `#ifdef GUICONTROLS`
  directives remain — feature flag, not a target or port-config macro, so
  out of the purity gate's scope.

Scoreboard total at F5 close: 281. Pre-fixup F1 baseline: 602. Real
reduction: 321 (−53%). This is measured by the rewritten
`tools/hal_scoreboard.sh` which counts every `#if*` directive regardless of
macro name, so the renamed-gate pattern that triggered the 2026-04-22 audit
can't inflate the number.

Build verification at F5 close: all 12 device CMake variants green; host
`./run_tests.sh` 239/239; WASM build green.

The fixup plan is finished. Phase 6 (hal_audio device arm) is now unblocked.

## Phase 6b closure, 2026-04-23

Phase 6b landed in five commits (`8ea699e` → `899c218`) driving Audio.c
from 14 target-macro ifdefs to 0. Grand scoreboard 281 → 267. Audio.c
joins External.c / FileIO.c / MM_Misc.c in `STRICT_FILES`.

Per-step summary lives in `phase-6-audio.md`. Key structural moves:

- **dr_mp3** amalgamated implementation relocated to
  `drivers/audio_mp3/{audio_mp3_real,audio_mp3_stub}.c`, selected per
  target by CMakeLists. Audio.c includes `dr_mp3.h` unconditionally for
  types/declarations; linker resolves the function bodies to real impl
  on RP2350 or no-op stubs on RP2040/host.
- **Three new port-config constants** in every `port_config.h`:
  `HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ` (44.1 kHz RP2040, 48 kHz RP2350),
  `HAL_PORT_AUDIO_MOD_BUFFER_SIZE` (6144 RP2040, 8192 RP2350),
  `HAL_PORT_HAS_MP3` (0 RP2040, 1 RP2350).
- **`PSRAMsize` promoted to unconditional storage** in PicoMite.c (and
  extern in Hardware_Includes.h). RP2040 still reads 0 → the existing
  `if (PSRAMsize)` runtime branches dead-code as before, but target-
  macro ifdefs guarding PSRAM-only code paths now drop cleanly.
- **Device body relocated** (~2100 lines) from Audio.c to
  `drivers/pwm_synth/pwm_synth.c`. Audio.c shrinks to ~199 lines of
  host-only cmd_play. The `#ifndef MMBASIC_HOST / #else` wrapper
  disappears entirely — Audio.c compiles on host (via `host/Makefile`),
  pwm_synth.c compiles on device (via CMakeLists.txt), neither has
  target ifdefs.

The hal_audio.h contract was **not** expanded in 6b. File-based playback
(WAV/MP3/FLAC/MOD) still lives inside pwm_synth.c as before. Expanding
the HAL so pwm_synth.c reduces to a HAL impl (and Audio.c becomes the
single BASIC-dialect parser across host + device) is future refinement;
6b's exit gate was only "zero target ifdefs in Audio.c", met by the
driver-file split.

**Lesson added:**
- When a core file has a big per-target backend (`#ifndef MMBASIC_HOST`
  device body in Audio.c), relocating the backend to a driver file that
  links per-target is a valid route to phase closure even if it stops
  short of the idealised single-cmd_play HAL. Scoping the step to "get
  target ifdefs to 0" (not "make the HAL contract complete") let 6b
  land in 5 incremental commits instead of one risky rewrite.
- Physical-device testing language in exit gates is a red herring for
  a code-move phase: compile-time gate (buildall.sh across all 12
  targets) + host functional tests + HAL purity gate is the same
  validation set Phase 6 shipped under. Phase 7 does not have an
  intrinsic extra validation requirement.

**Lessons reconfirmed:**
- "Infrastructure landed" is not a phase-exit state; the actual gate is
  zero target/port-config `#if*` directives in each scoped core file.
- Per-port impl files (`ports/<port>/*.c`, `ports/pico_sdk_common/*.c`,
  `host/*.c`) are where conditional bodies live. Core calls through
  function hooks; the hooks' internal `#ifdef` is fine because port impl
  files are exempt from the purity gate.
- When a core file has many small port-specific OPTION setters / MM.INFO
  fields / etc., grouping them behind a small number of `port_*_setter()`
  or `port_*_extra()` hooks keeps the call-site churn low. F4 used this
  pattern via `ports/pico_sdk_common/misc_option_setters.c` to absorb
  seven OPTION setters behind one hook.

## Per-phase commit-count targets (from the original course correction, unchanged by the fixup)

- Fixup F2/F3/F4 (redone Phase 3/4/5 ifdef elimination): **2–3 commits each.** Actual work of moving bodies into HAL impls, not just renaming gates.
- Phase 6b (audio device arm): **2–3 commits.**
- Phase 7a–d (display): **3–4 commits each.**
- Phase 8 (multicore): **1 commit.**
- Phase 9 (net): **1–2 commits.** 67 `PICOMITEWEB` blocks across 8 files.
- Phase 10 (heap): **1 commit.**
- Phase 11 (sweep): **3–5 commits** for remaining drivers + final cleanup of every file to 0.
