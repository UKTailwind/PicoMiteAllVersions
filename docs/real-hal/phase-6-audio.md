# Real HAL — Phase 6: `hal_audio` ✅

**Status:** 6a + 6b landed. shared/audio/Audio.c is in STRICT_FILES (zero target-macro
and zero port-config ifdefs); device body now lives in
`drivers/pwm_synth/pwm_synth.c`, linked per-target by CMakeLists. The
full hal_audio.h expansion (WAV/MP3/FLAC/MOD file playback via HAL
instead of through the pwm_synth driver) remains as future refinement
and is not required for this phase's exit.

`shared/audio/Audio.c` is 115 KB but mostly dialect logic. The hardware-dependent parts are PWM tone generation on PicoMite, VS1053 codec on Web variants, and sample DMA for WAV/MP3/FLAC/MOD streaming.

## What landed (Phase 6a)

- `hal/hal_audio.h` contract: tone/sound/stop/volume/pause/resume; init is no-op on host.
- `host/hal_audio_host.c` forwarding to the existing `host_sim_audio_*` family.
- `shared/audio/Audio.c` host arm migrated to call `hal_audio_*` instead of `host_sim_audio_*` directly.
- VS1053 relocated to `drivers/vs1053/`.
- Device arm untouched (~2000 lines of PWM/DMA/codec).

**Commits:** `5232a46`, `7e733d6`.

## What landed (Phase 6b)

Five incremental commits drove shared/audio/Audio.c from 14 target-macro ifdefs to 0
and closed the phase:

- **step 1** (`8ea699e`): drop PICOMITEWEB guard around `ProcessWeb(1)`
  in the PLAY MODFILE wait loop — F3's stub makes the guard dead weight.
- **step 2** (`60c34c8`): FLAC max sample-rate cap → per-port
  `HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ` constant (44.1 kHz on RP2040, 48 kHz
  on RP2350).
- **step 3a** (`fd51053`): dr_mp3 amalgamated implementation →
  `drivers/audio_mp3/audio_mp3_real.c` (linked on RP2350) +
  `audio_mp3_stub.c` (linked on RP2040 + host). dr_mp3.h becomes
  unconditional in shared/audio/Audio.c; `MOD_BUFFER_SIZE` +
  `HAL_PORT_HAS_MP3` added to port_config.h.
- **step 3b** (`e4d8fe1`): drop 6 rp2350 guards around MP3 code —
  `drmp3 *mymp3` decl, `CloseAudio` MP3+PSRAM cleanup, mp3callback body,
  PLAY MP3 precondition → `HAL_PORT_HAS_MP3` runtime branch,
  checkWAVinput pump, audio_checks cleanup. PSRAMsize definition
  promoted to unconditional in PicoMite.c so the remaining PSRAM checks
  work everywhere.
- **step 4** (`f88ba0a`): drop 4 rp2350 guards around PSRAM MOD buffer
  code — runtime `PSRAMsize` check (0 on RP2040) collapses the branches
  cleanly without preprocessor gating.
- **step 5** (this commit): relocate device body (~2100 lines) into
  `drivers/pwm_synth/pwm_synth.c`; shared/audio/Audio.c shrinks to ~199 lines of
  host-only cmd_play. The `#ifndef MMBASIC_HOST / #else` wrapper
  disappears entirely — shared/audio/Audio.c compiles on host (via `host/Makefile`),
  pwm_synth.c compiles on device (via CMakeLists.txt), neither has
  target ifdefs.

A future refinement (out of scope for phase 6b) will expand hal_audio.h
to cover file-based playback (WAV/MP3/FLAC/MOD) so pwm_synth.c reduces
to a HAL implementation rather than owning BASIC-dialect parsing. For
phase 6b's exit gate, the "zero target ifdefs in shared/audio/Audio.c" standard is
satisfied by the driver-file split.

## Exit gate (met)

- shared/audio/Audio.c: zero `#if*` directives on target OR port-config macros. ✅
- `tools/check_hal_purity.sh` passes with shared/audio/Audio.c in STRICT_FILES. ✅
- All 12 device variants + host (239/239) + WASM green on every commit. ✅
