# Clean Audio Architecture Plan

This plan captures the intended end state for audio across all ports: shared
command dispatch and runtime policy, shared synthesis where possible, a rational
HAL surface, and discrete backend drivers for each hardware audio target.

## Current State

- Shared command path: `shared/audio/Audio.c` is used by host, WASM,
  stdio/ANSI, pc386, and ESP32. It owns `cmd_play()`, `CloseAudio()`,
  `StopAudio()`, `checkWAVinput()`, and completion handling for the shared path.
- RP command path: `drivers/pwm_synth/pwm_synth.c` owns a second `cmd_play()`
  for RP ports. It mixes BASIC parsing, global runtime state, file policy,
  VS1053, MIDI, STREAM, ARRAY, MOD legacy paths, and part of the sample sink.
- RP actual audio IRQ/output is split elsewhere: `drivers/sd_spi/mmc_stm32.c`
  contains `on_pwm_wrap()`, `DefaultAudio()`, `SPIAudio()`, `AudioOutput`,
  PWM/I2S/VS1053 send logic, and audio pin setup mixed into SD/SPI code.
- Shared synth exists in `shared/audio/synth_pcm.c`, with wavetables,
  `getsound()`, `setnoise()`, `synth_pcm_tone_frame()`, and
  `synth_pcm_sound_frame()`. Some TONE/SOUND globals remain duplicated across
  shared and RP paths.
- Shared decode exists in `shared/audio/audio_stream.c`, which decodes
  WAV/MP3/FLAC/MOD to stereo PCM and feeds `hal_audio_sample_*`.
- `hal/hal_audio.h` currently mixes BASIC-level operations (`tone`, `sound`)
  with low-level PCM sample queue operations.
- Completion interrupts are split: Pico-style paths still check
  `WAVInterrupt && WAVcomplete` directly in `core/mmbasic/MM_Misc.c`, while the
  shared runtime path calls `audio_interrupt_pending()`.
- Build split: RP top-level CMake links `drivers/pwm_synth/pwm_synth.c`,
  `audio_stream.c`, and `synth_pcm.c`; ESP32 and host-like ports link
  `shared/audio/Audio.c`, `audio_stream.c`, `synth_pcm.c`, and a port
  `hal_audio_*`.

## Target Architecture

- `shared/audio/audio_cmd_play.c`: the only shared BASIC `PLAY` dispatcher.
  Owns parsing, argument compatibility, state transitions, and common errors.
- `shared/audio/audio_runtime.c`: owns `CurrentlyPlaying`, `PlayingStr`,
  `WAVInterrupt`, `WAVcomplete`, `CloseAudio()`, `StopAudio()`,
  `audio_runtime_service()`, `audio_interrupt_pending()`, mode ownership,
  pause/resume, and completion.
- `shared/audio/synth_pcm.c`: owns all software synth state, including TONE
  globals, SOUND slot state, user waveform table pointer, and render functions.
- `shared/audio/audio_stream.c`: remains the shared file decode/service engine.
  It should transition playback state through audio runtime helpers rather than
  directly setting globals where possible.
- `shared/audio/audio_play_hooks.h`: a narrow extension hook table for
  non-common behavior: RP playlists, VS1053, MIDI/MIDIFILE, STREAM, ARRAY,
  MODSAMPLE, HALT/CONTINUE.
- `drivers/audio_rp2_pwm_i2s/`: RP PWM, SPI DAC, and PIO-I2S backend. Owns IRQ,
  sample-rate programming, ping-pong buffers, conversion, pin/output init, and
  `on_pwm_wrap()`.
- `drivers/audio_vs1053/`: VS1053 codec backend and RP-only extension hooks.
  Owns MIDI, MIDIFILE, STREAM, VS1053 MP3/FLAC/WAV feed, HALT/CONTINUE.
- Port HALs become backend drivers, not command owners.

## HAL Direction

Split the current mixed `hal_audio.h` contract into command/runtime-facing audio
core APIs and backend stream/sink APIs.

Example backend capabilities:

```c
typedef enum {
    AUDIO_CAP_PCM_OUT       = 1u << 0,
    AUDIO_CAP_PULL_RENDER   = 1u << 1,
    AUDIO_CAP_SAMPLE_QUEUE  = 1u << 2,
    AUDIO_CAP_TONE_DONE_IRQ = 1u << 3,
    AUDIO_CAP_MIDI          = 1u << 4,
    AUDIO_CAP_STREAM_BYTES  = 1u << 5,
} audio_backend_caps_t;
```

Core-facing API candidates:

- `audio_play_cmd_dispatch(cmdline)`
- `audio_runtime_stop(close_all)`
- `audio_runtime_pause()`
- `audio_runtime_resume()`
- `audio_runtime_service()`
- `audio_interrupt_pending(&target)`
- `audio_synth_tone_start(left, right, duration_ms)`
- `audio_synth_sound_set(slot, channel_mask, waveform, freq, volume)`
- `audio_synth_render_frame(&left, &right)`

Backend HAL candidates:

- `hal_audio_backend_init()`
- `hal_audio_backend_shutdown()`
- `hal_audio_backend_caps()`
- `hal_audio_backend_start_pcm(rate_hz)`
- `hal_audio_backend_stop_pcm()`
- `hal_audio_backend_pause(paused)`
- `hal_audio_backend_set_volume(left_pct, right_pct)`
- `hal_audio_backend_render_kick(mode)`
- Move `hal_audio_sample_begin/push/acquire/commit/space/queued/eof/end` to a
  stream-sink header so command control and PCM queues are separate contracts.
- Keep `hal_audio_workmem_alloc/realloc/free`, but document it as decoder work
  memory, not general audio state.

## Backend Responsibilities

- RP PWM/SPI DAC/PIO-I2S: transport only. Program PWM/PIO rate, own IRQ and
  ping-pong buffers, consume shared synth/file frames, publish tone completion.
  No `cmd_play()`.
- RP VS1053: byte-stream codec transport, MIDI/STREAM extension hooks, hardware
  volume, DREQ pacing, codec reset. It should not parse common `PLAY` commands.
- ESP32-S3: I2S/PDM task, ring buffer, PSRAM workmem, sample-rate reconfig,
  EOF tail drain. It should call shared synth render and not inspect BASIC
  syntax.
- Host native/WASM: event backend for TONE/SOUND/STOP/VOLUME/PAUSE/RESUME;
  sample stream can remain discard-only until PCM playback is required.
- pc386 PC speaker: limited mono tone backend; reports no full software synth or
  file capabilities.
- pc386 SB16: PCM backend should eventually use shared synth/file PCM. Current
  file sink behavior is debt.
- pc386 OPL3: hardware-synth backend; keep capability-specific behavior rather
  than forcing bit-identical shared PCM synth.

## Migration Phases

1. Consolidate runtime state.
   Move `PlayingStr`, duplicated TONE globals, `CloseAudio()`, `StopAudio()`,
   `audio_runtime_service()`, and `audio_interrupt_pending()` into shared audio
   runtime. Validate host tests `t115`, `t220`, `t221`, `t222`; RP builds; and
   `MM.INFO$(PLAYING)` on every linked target.

2. Extract RP backend code from mixed files.
   Move `on_pwm_wrap()`, `DefaultAudio()`, `SPIAudio()`, `AudioOutput`, rate
   setup, pin init, ping-pong publication, `iconvert()`, and `i2sconvert()` out
   of `drivers/sd_spi/mmc_stm32.c` and `drivers/pwm_synth/pwm_synth.c` into
   `drivers/audio_rp2_pwm_i2s/`. Validate RP PWM tone, PIO-I2S tone, WAV drain,
   and rapid STOP.

3. Make RP use shared common `cmd_play()`.
   Route RP `cmd_play()` to the shared dispatcher for
   STOP/CLOSE/PAUSE/RESUME/VOLUME/TONE/SOUND/LOAD SOUND/NOTE/WAV/MP3/FLAC and
   software MODFILE. Keep extension-hook fallback for legacy-only branches.
   Validate accepted arities/defaults/errors against old RP behavior.

4. Split extension hooks.
   Move VS1053/MIDI/STREAM/HALT/CONTINUE/MODSAMPLE/ARRAY/playlist
   NEXT/PREVIOUS behind the hook table. The shared dispatcher calls the hook or
   errors consistently. Validate RP VS1053 manually and host/ESP32 unsupported
   feature tests.

5. Clean HAL headers and build files.
   Split `hal_audio.h` into shared runtime/control headers and backend stream
   sink headers. Update CMake/Makefiles so every port links the same shared
   audio core plus exactly one backend. Validate all RP CMake ports, ESP32 IDF,
   host native, WASM, and pc386 variants.

6. Remove legacy driver-command shape.
   Delete `drivers/pwm_synth/pwm_synth.c::cmd_play()` and shrink or delete the
   file. Remaining code should live in `audio_rp2_*` and `audio_vs1053_*`.

## Risks

- `CurrentlyPlaying`, `WAVcomplete`, `WAVInterrupt`, `SoundPlay`, and synth
  phase globals are read/written from ISR, task, and core paths. `volatile` is
  not enough on ESP32; use atomics or short critical sections around
  publication.
- RP ping-pong state must keep `bcount[target]` as the final publication write.
  Do not disable IRQ while copying or converting whole buffers.
- `audio_stream_service()` has a static re-entry guard but not a cross-core
  lock. ESP32 should ensure only one task enters it.
- Tone duration semantics differ: RP rounds to waveform cycle counts; ESP32
  counts frames. Decide whether exact RP rounding is compatibility or an
  implementation detail.
- `usertable` points into BASIC array memory. Clear it on close/run/memory reset
  and guard against stale pointers.
- MOD/MP3/FLAC allocations must stay out of small internal heaps where possible.
  ESP32 PSRAM fallback and RP PSRAM/XIP MOD behavior need explicit caps.
- EOF completion must mean backend/DMA/I2S/IRQ drained, not just decoder EOF.

## Specific Moves And Deletes

- Move from `pwm_synth.c`: common `cmd_play()` branches, `pico_synth_note()`,
  `rampvolume()` behavior, `CloseAudio()`, `StopAudio()`, `checkWAVinput()`,
  `audio_checks()`, `hal_audio_sample_*`, and `hal_audio_workmem_*`.
- Move from `sd_spi/mmc_stm32.c`: `on_pwm_wrap()`, `DefaultAudio()`,
  `SPIAudio()`, `AudioOutput`, audio init blocks around `AUDIO_*`, and VS1053
  send path.
- Move VS1053 helpers from `pwm_synth.c`: `playvs1053()`,
  `playimmediatevs1053()`, `midicallback()`, VS1053 STREAM, MIDI, MIDIFILE,
  HALT, and CONTINUE handling.
- Move `PlayingStr[]` to `core/state/audio_state.c` or
  `shared/audio/audio_runtime.c`.
- Delete after migration: RP-owned `cmd_play()` in `drivers/pwm_synth`; audio
  ISR/output code from `drivers/sd_spi/mmc_stm32.c`; command-level
  `hal_audio_tone/sound` dependency for ports that should use shared synth
  directly.

## Explicit Punts

- Keep VS1053, MIDI, STREAM, ARRAY, MODSAMPLE as RP-only hooks initially.
- Host/WASM PCM file output can remain discard-only unless Web Audio PCM
  streaming is explicitly required.
- PC speaker cannot share the full synth meaningfully.
- Playlist directory support needs a product decision: portable BASIC behavior
  or Pico-only compatibility hook. Current shared path silently punts
  NEXT/PREVIOUS; that should become explicit.
