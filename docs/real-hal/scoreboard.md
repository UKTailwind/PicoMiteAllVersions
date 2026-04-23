# Real HAL — Scoreboard

Per-file count of **every** preprocessor conditional (`#if` / `#ifdef` / `#ifndef` / `#elif`) in the tracked core files. Measured by `tools/hal_scoreboard.sh`. **Trend indicator only — the hard gate is `tools/check_hal_purity.sh`.**

The old script grepped for specific macro names (`rp2350`, `PICOMITE`, …), which made renamed gates (`#ifdef rp2350` → `#if HAL_PORT_PWM_SLICE_COUNT > 8`) invisible. The rewrite counts all conditional-compilation directives regardless of macro name, so renaming can't help the score — only moving bodies out of the core file does. See `../real-hal-fixup-plan.md` for the standard and corrective sequence.

Every row below must be measured by running the scoreboard script after the phase lands. If the number didn't go down, the ifdef elimination didn't happen.

```
Phase    Draw    MM_Misc  External  FileIO  Commands  Memory  Functions  Audio   Total
F1       164     139      123       62      45        38      17         14      602  (honest baseline — revert of 2c034d7+61cb08e)
─── fixup (real elimination, per ../real-hal-fixup-plan.md) ───
F2a step 1 164     139     107      62      45        38      17         14      586  (per-port port_config.h + first cull; −16)
F2a step 2 164     139      58      62      45        38      17         14      537  (unconditional globals + case arms; −49)
F2a step 3a 164    139      50      62      45        38      17         14      529  (hal_fast_timer + HEARTBEAT; −8)
F2a step 3b 164    139      38      62      45        38      17         14      517  (keyboard HAL + ADC_CHANNEL_MAX; −12)
F2a step 3c 164    139      27      62      45        38      17         14      506  (rp2350a runtime split + camera stubs; −11)
F2a step 3d 164    139      22      62      45        38      17         14      501  (PINMAP + codemap to per-port .c; −5)
F2a step 3e 164    139      19      62      45        38      17         14      498  (ADC OPEN rp2350a + MOUSE stub; −3)
F2a step 3f 164    140      16      62      45        38      17         14      496  (MQTT + Collision unconditional; −2)
F2a step 3g 164    140      10      62      45        38      17         14      490  (setBacklight unified + NEXTGEN; −6)
F2a step 3h 164    140       2      62      39        38      17         14      476  (F2 close: MATHS.c dims widening + KEYPAD unification + PicoCalc keymap per-port; −14)
            External.c: 0 target-macro ifdefs (only 2 #ifdef GUICONTROLS
            remain — not in scope). Promoted to STRICT_FILES in the purity
            gate. F2 CLOSED.
─── F3 (FileIO.c) ───
F3 step 1  164    140       2      52      39        38      17         14      466  (PSRAM cache→port file + mergedread/a_dlist/MemLoadProgram uncond; −10)
F3 step 2  164    140       2      50      39        38      17         14      464  (disable/enable_interrupts_pico MMBASIC_HOST gate off; −2)
F3 step 3  164    140       2      48      39        38      17         14      462  (heartbeat rp2350a runtime + LoadPNG HAS_UPNG; −2)
F3 step 4  164    140       2      44      39        38      17         14      458  (ProcessWeb stub + 4 PICOMITEWEB sites; −4)
F3 step 5  164    140       2      39      39        38      17         14      453  (TCP recv-buffer helpers + closeall3d stub; −5)
F3 step 6  164    140       2      32      39        38      17         14      446  (ResetOptions → 7 per-port port_defaults.c; −7)
F3 step 7  164    140       2      31      39        38      17         14      445  (cmd_psram → ports/pico_sdk_common/cmd_psram.c; −1)
F3 step 8  164    140       2      30      39        38      17         14      444  (DEFINES loader → ports/pico_sdk_common/defines_loader.c; −1)
F3 step 9  164    140       2      28      39        38      17         14      442  (MemWriteBlock + LoadPNG → port files; −2)
            FileIO.c: 60 → 26 target ifdefs (57% reduction). ~20 remaining
            are MMBASIC_HOST gates around the SD read/write cache — genuine
            backend distinction between device (FatFS+SD) and host
            (POSIX/RAM-disk). See phase-4-filesystem.md for options.
F4         .       .        .        .       .         .       .          .        .  (MM_Misc.c USBKEYBOARD → HAL)
─── post-fixup phases ───
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

Dots (`.`) mean "not targeted by this phase — carry forward from previous." Zeros are the exit-gate targets.

## F1 baseline breakdown (per `tools/hal_scoreboard.sh --breakdown`)

| file | total | target-macro | port-config | other |
|------|-------|--------------|-------------|-------|
| Draw.c      | 164 | 160 | 0 | 4 |
| MM_Misc.c   | 139 | 134 | 0 | 5 |
| External.c  | 123 | 120 | 0 | 3 |
| FileIO.c    |  62 |  60 | 0 | 2 |
| Commands.c  |  45 |  45 | 0 | 0 |
| Memory.c    |  38 |  37 | 0 | 1 |
| Functions.c |  17 |  17 | 0 | 0 |
| Audio.c     |  14 |  14 | 0 | 0 |
| **total**   | **602** | **587** | **0** | **15** |

The "other" column counts conditionals that reference neither a target macro nor a port-config macro (e.g. `#ifdef EXTERNAL_INCLUDES`, `#if defined(__GNUC__)`). Those are out of scope for the HAL-clean standard — the goal is zero target-macro ifdefs and zero port-config-macro ifdefs. A phase closes by driving its file's target+port columns to 0, not by touching the "other" column.

## Historical (for context)

Pre-F1 measurement used the old name-based metric:

- Phase 0 baseline: 606 total (target macros only)
- 1–5a tip: 587 (infrastructure landed, ifdefs mostly unchanged)
- Post 2c034d7+61cb08e: claimed 508; the 79-gate drop was a rename, not elimination; reverted in F1

The F1 total (602) is higher than "5a" (587) by 15 because the new script also counts non-target conditionals. The target-only subtotal in the F1 breakdown (587) matches the pre-revert "5a" figure exactly — i.e. nothing was lost or gained in the revert at the level of target-macro ifdefs. The additional 15 were always there; the old script simply didn't see them.

## Methodology note

The scoreboard is a trend indicator. The gate is `tools/check_hal_purity.sh`. The gate enforces two things:

1. `hal/*.h` has zero preprocessor conditionals apart from the file's own include-guard `#ifndef` and the `#ifdef __cplusplus` `extern "C"` wrapper.
2. Any file in `STRICT_FILES` has zero target-macro ifdefs AND zero port-config-macro ifdefs.

As each phase lands, it promotes its targeted file(s) into `STRICT_FILES`. A phase closes only when the gate passes with that file promoted. Renaming a `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` now fails the gate at both (1) and (2).
