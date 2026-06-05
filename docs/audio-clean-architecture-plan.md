# Clean Audio Architecture Plan

This plan captures the intended end state for audio across all ports: shared
command dispatch and runtime policy, shared synthesis where possible, a rational
HAL surface, and discrete backend drivers for each hardware audio target.

## Original State Before This Plan

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

## Implementation Status

Status as of 2026-06-05: the clean non-legacy architecture is implemented on
the PR branch. The ownership goal is complete even though the shared dispatcher
still lives in `shared/audio/Audio.c` rather than a renamed
`audio_cmd_play.c`.

Completed:

- Shared `PLAY` dispatch now owns common command syntax and policy in
  `shared/audio/Audio.c`.
- Shared runtime ownership lives in `shared/audio/audio_runtime.c`, including
  `CurrentlyPlaying`, `PlayingStr`, `WAVInterrupt`, `WAVcomplete`,
  `CloseAudio()`, `StopAudio()`, `audio_runtime_service()`, and
  `audio_interrupt_pending()`.
- Shared file decode remains in `shared/audio/audio_stream.c`; WAV, MP3, FLAC,
  and MODFILE decode to stereo PCM and feed the selected backend through the
  stream-sink HAL.
- RP PWM, SPI DAC, and PIO-I2S realtime transport moved to
  `drivers/audio_rp2_pwm_i2s/`.
- VS1053 file/codec/session behavior moved to `drivers/audio_vs1053/`.
- RP extension behavior is exposed through `shared/audio/audio_play_hooks.h`
  instead of common command parsing living in the backend.
- `drivers/pwm_synth/pwm_synth.c` was removed.
- `drivers/sd_spi/mmc_stm32.c` no longer owns audio IRQ/output/backend logic;
  it only invokes audio initialization from the port options path.
- WASM now has a real PCM stream backend in `ports/host_wasm/hal_audio_wasm.c`
  and `ports/host_wasm/web/ui/audio.js`; it no longer accepts and discards file
  playback samples.
- Host-style polling now calls `audio_runtime_service()` so streamed audio is
  continuously pumped while the REPL waits for input.

Validation completed:

- WASM build passes and `ports/host_wasm/web/smoke_audio.mjs` verifies
  `PLAY TONE`, `PLAY SOUND`, sustained `PLAY WAV`, sustained `PLAY MP3`, and
  sustained `PLAY MODFILE`.
- ESP32-S3 Metro builds and flashes. Hardware probe reported `MM.VER = 1.0006`
  and `MM.INFO$(AUDIO) = I2S`; an I2S tone/SOUND smoke accepted
  `PLAY TONE`, `PLAY SOUND`, `PLAY SOUND ... O`, and `PLAY STOP` without BASIC
  errors.
- PicoCalc RP2350 was rebuilt and flashed from the correct
  `build_picocalc_rp2350` board build directory.

Known follow-up:

- The HAL split is improved but not perfectly named: command/control and stream
  sink contracts now live in separate headers, while some candidate API names in
  this plan remain aspirational.
- Native host PCM file output is still a discard sink unless a native output
  device is added. WASM PCM output is implemented.
- pc386 file playback remains future work.
- Playlist `NEXT`/`PREVIOUS` portability still needs a product decision.

## Ownership Contract

This refactor is not just a file shuffle. The main invariant is that BASIC
policy and realtime transport state must have different owners.

- Shared command/runtime code owns BASIC syntax, accepted argument shapes,
  compatibility errors, playback modes, pause/resume/stop/close policy,
  `MM.INFO$(PLAYING)`, completion interrupt dispatch, and decoder lifecycle.
- Shared synth code owns software waveform tables, TONE/SOUND/NOTE voice state,
  user waveform registration, and frame rendering.
- Shared stream code owns file opening through the MMBasic file layer, format
  decode, decoder work memory, and feeding decoded stereo PCM to the backend.
- Backend code owns all hardware timing and publication details: IRQ/task/DMA
  state, PIO/I2S/PWM/SPI programming, ring or ping-pong buffers, rate changes,
  drain/tail timing, and codec DREQ/reset/volume handling.
- Shared code must not inspect or mutate backend realtime internals such as
  ring head/tail, `bcount[]`, DMA descriptors, PIO state machines, I2S tasks, or
  VS1053 DREQ state. It talks to the backend through explicit control, render,
  stream-sink, and capability APIs only.
- Backends must not parse common `PLAY` syntax. Non-common behavior is exposed
  through extension hooks with capability checks, so unsupported ports produce a
  deliberate error instead of silently drifting.

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

## Risks And Required Mitigations

- `CurrentlyPlaying`, `WAVcomplete`, `WAVInterrupt`, `SoundPlay`, and synth
  phase globals are read/written from ISR, task, and core paths. `volatile` is
  not enough on ESP32. Mitigation: move BASIC-visible runtime state behind
  shared runtime helpers, keep backend realtime state private, and use atomics
  or short critical sections only at backend publication points.
- RP ping-pong state must keep `bcount[target]` as the final publication write.
  Mitigation: the RP backend fills/converts a buffer first, then publishes the
  completed buffer by assigning `bcount[target]` last inside a short critical
  section. Do not disable IRQ while copying or converting whole buffers.
- `audio_stream_service()` has a static re-entry guard but not a cross-core
  lock. Mitigation: the decoder service has a single foreground owner per
  playback session. Audio ISR/task code consumes queued PCM only and must not
  enter the decoder.
- Tone duration semantics differ: RP rounds to waveform cycle counts; ESP32
  counts frames. Mitigation: decide before moving TONE whether RP rounding is
  compatibility. If yes, implement it in shared synth/runtime so every software
  backend matches it; if no, document the intentional behavior change.
- `usertable` points into BASIC array memory. Clear it on close/run/memory reset
  and guard against stale pointers. Mitigation: keep the pointer in shared synth
  state and clear it from all lifecycle paths that can invalidate BASIC arrays.
- MOD/MP3/FLAC allocations must stay out of small internal heaps where possible.
  ESP32 PSRAM fallback and RP PSRAM/XIP MOD behavior need explicit caps.
  Mitigation: treat `hal_audio_workmem_*` as decoder work memory only and make
  every failed-open, stop, close, EOF, and abort path release it.
- EOF completion must mean backend/DMA/I2S/IRQ drained, not just decoder EOF.
  Mitigation: shared stream code may mark decode EOF, but shared runtime should
  publish completion only after the backend reports its queue/ring/DMA/tail hold
  drained.

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
- Native host PCM file output can remain discard-only unless a native audio
  device is added. WASM PCM output is implemented through Web Audio.
- PC speaker cannot share the full synth meaningfully.
- Playlist directory support needs a product decision: portable BASIC behavior
  or Pico-only compatibility hook. Current shared path silently punts
  NEXT/PREVIOUS; that should become explicit.
