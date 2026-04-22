# Real HAL — Scoreboard

Raw grep count of target-macro `#ifdef` occurrences in scored core files. Measured by `tools/hal_scoreboard.sh`. **Trend indicator only — the hard gate is `tools/check_hal_purity.sh`.** See `tooling.md` for the metric rules (notably, the scoreboard must be rewritten per fixup plan F1 to count all preprocessor conditionals in core, not just occurrences of specific macro names, so renamed gates can't hide).

Every row below must be measured by running the scoreboard script after the phase lands. If the number didn't go down, the ifdef elimination didn't happen.

```
Phase    Draw.c  MM_Misc  External  FileIO  Commands  Memory  Functions  Audio   Total
0        162     135      120       75      46        37      17         14      606  (measured baseline)
0.5      162     135      120       75      46        37      17         14      606  (state hoist, no #ifdef change)
1–5a     160     134      120       60      45        37      17         14      587  (MEASURED at tip — infra landed, ifdefs mostly unchanged)
3b-old     .       .        0        .       .         .       .          .        .  (INVALID — ifdefs renamed, not eliminated; scheduled for revert)
─── fixup (real elimination, per ../real-hal-fixup-plan.md) ───
F2         .       .        0        .       .         .       .          .        .  (External.c pin/PWM/ADC → HAL, bodies moved into impls)
F3         .       .        .        0       .         .       .          .        .  (FileIO.c host/storage/flash → HAL)
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

Dots (`.`) mean "not targeted by this phase — carry forward from previous." Zeros are the exit-gate targets. Each row gets filled with measured values when the phase lands.

## Methodology note

The original `tools/hal_scoreboard.sh` only grepped core files for occurrences of the listed target macros (`rp2350`, `PICOMITE`, etc.). Phase 3b exploited this by renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` — the scoreboard dropped while the conditional compilation stayed. Per fixup plan F1, the script must be rewritten to count **every** `#if*` directive in the configured core files, regardless of macro name. Only then does a falling scoreboard mean fewer gates.
