# Real HAL — Phase 6: `hal_audio` 🟡

**Status:** 6a landed, 6b pending.

`Audio.c` is 115 KB but mostly dialect logic. The hardware-dependent parts are PWM tone generation on PicoMite, VS1053 codec on Web variants, and sample DMA for WAV/MP3/FLAC/MOD streaming.

## What landed (Phase 6a)

- `hal/hal_audio.h` contract: tone/sound/stop/volume/pause/resume; init is no-op on host.
- `host/hal_audio_host.c` forwarding to the existing `host_sim_audio_*` family.
- `Audio.c` host arm migrated to call `hal_audio_*` instead of `host_sim_audio_*` directly.
- VS1053 relocated to `drivers/vs1053/`.
- Device arm untouched (~2000 lines of PWM/DMA/codec).

**Commits:** `5232a46`, `7e733d6`.

## Phase 6b — device arm

- Move ~2000 lines of PWM/DMA/codec code from Audio.c into `drivers/pwm_synth/` (device impl).
- `hal_audio_sample_push()` for WAV/MP3/FLAC/MOD streaming.
- Audio.c becomes pure BASIC dialect logic (parsing PLAY command syntax, managing voice slots) with zero hardware `#ifdef`s — all hardware dispatch goes through `hal_audio_*` impls linked per target.
- A `mmbasic_stdio` port links `hal_audio_hard_error.c` — any PLAY statement returns a BASIC error.

## Exit gate

- Audio.c: zero `#if*` directives on target OR port-config macros.
- Audio bench (`PLAY TONE` 1 kHz for 5 s, capture buffer underruns) shows zero regressions.
- `tools/check_hal_purity.sh` passes for Audio.c and `hal/hal_audio.h`.
- Commit-count target: 2–3 commits for 6b.
