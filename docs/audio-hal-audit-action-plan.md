# Audio HAL Audit Action Plan

Date captured: 2026-06-04
Branch context: `audio-refactor-shared-pico-esp32`

This plan turns the audio HAL audit findings into implementation work. It is intentionally scoped around preserving Pico behavior while making the shared HAL path defensible for ESP32 and future ports.

## Goals

- Fix concrete regressions found in the shared/ESP32 audio path.
- Make the stream buffering contracts safe across ISR/task boundaries.
- Continue moving portable audio behavior out of Pico-only code where the behavior is not hardware-specific.
- Keep RP2 transport details, ESP32 I2S/PDM details, and VS1053/MIDI hardware behavior behind clear backend boundaries.
- Add enough verification that regressions in `PLAY TONE`, `PLAY SOUND`, and file playback are caught before hardware testing.

## Non-Goals

- Do not remove VS1053, MIDI, `PLAY STREAM`, or Pico playlist features in this pass.
- Do not force all ports to support every `PLAY` subcommand.
- Do not rewrite RP2 audio ISR timing logic unless required to make the shared sink contract correct.
- Do not change PicoCalc default audio behavior without an explicit product decision.

## Phase 1: Fix Immediate Shared/ESP32 Regressions

### 1. Reset ESP32 pause state on stop and stream begin

Problem:

- `hal_audio_pause()` sets `s_paused = true`.
- `hal_audio_stop()` does not clear it.
- After `PLAY PAUSE`, then `PLAY STOP` or `PLAY CLOSE`, later playback can stay silent because the ESP32 audio task continues forcing `mode = P_NOTHING`.

Files:

- `ports/esp32_s3_metro/main/hal_audio_esp32.c`

Implementation:

- In `hal_audio_stop()`, set `s_paused = false`.
- In `hal_audio_sample_begin()`, also set `s_paused = false` so a new file stream always starts unpaused.
- Consider setting `s_paused = false` in `hal_audio_tone()` and `hal_audio_sound()` if command semantics should be "new playback resumes output." Prefer this unless there is a compatibility reason not to.

Validation:

- ESP32 manual:
  - `PLAY TONE 440,440`
  - `PLAY PAUSE`
  - `PLAY STOP`
  - `PLAY TONE 660,660`
  - Expected: 660 Hz tone is audible.
- Host smoke should continue to pass:
  - `ports/host_native/build/mmbasic_test ports/host_native/tests/t115_play_tone_stop_native.bas`

### 2. Reset shared SOUND slot mask on stop/close

Problem:

- `shared/audio/Audio.c` tracks active `PLAY SOUND` slots using `s_sound_slot_mask`.
- `CloseAudio()` and `StopAudio()` stop the backend but do not clear the mask.
- Later `PLAY SOUND ...,O` commands can make shared state disagree with backend state.

Files:

- `shared/audio/Audio.c`

Implementation:

- Add a helper such as `shared_sound_clear_slots()`.
- Call it from `CloseAudio()` and `StopAudio()`.
- Consider calling it before starting a file stream, since file playback currently requires exclusive audio ownership.

Validation:

- Add or extend a host-native test:
  - `PLAY SOUND 1,B,S,440,20`
  - `PLAY STOP`
  - `PLAY SOUND 2,B,S,660,20`
  - `PLAY SOUND 2,B,O`
  - `PRINT MM.INFO$(PLAYING)`
  - Expected: `OFF` or equivalent stopped state.
- Run direct host smoke and full host tests if feasible.

### 3. Decide and fix shared `PLAY TONE` interrupt semantics

Problem:

- Pico's legacy `cmd_play()` wires the optional `PLAY TONE ..., interrupt` argument.
- The shared `cmd_play()` currently ignores it with a host-specific comment, but ESP32 now uses the shared path.

Files:

- `shared/audio/Audio.c`
- `ports/esp32_s3_metro/main/hal_audio_esp32.c`
- `runtime/runtime_interrupt.c`
- `core/state/audio_state.c`

Decision:

- Preferred: support the interrupt in shared command code and make backends report tone completion consistently.
- Fallback: explicitly document that ESP32/host shared audio does not support tone completion interrupts yet, and reject the argument instead of silently ignoring it.

Implementation if supporting:

- In shared `PLAY TONE`, when `argc == 7`, require `CurrentLinePtr`, store `WAVInterrupt`, clear `WAVcomplete`, and set `InterruptUsed`.
- Ensure the backend sets `CurrentlyPlaying = P_NOTHING` and `WAVcomplete = true` when timed tone frames drain.
- For ESP32, this likely happens in `audio_task()` when `mode == P_TONE && s_tone_frames == 0`.
- Confirm runtime interrupt service notices `WAVcomplete`.

Validation:

- BASIC program with a short tone and completion interrupt on ESP32.
- Existing Pico behavior must not change.

## Phase 2: Make Buffering Contracts Safe

### 4. Replace ESP32 volatile ring synchronization with a real concurrency primitive

Problem:

- ESP32 stream ring uses `volatile` `s_ring_head`, `s_ring_tail`, and related counters across the BASIC task and audio task.
- `volatile` does not provide atomicity, ordering, or inter-core visibility guarantees for the ring data publication contract.

Files:

- `ports/esp32_s3_metro/main/hal_audio_esp32.c`

Preferred implementation:

- Use C11 atomics for producer/consumer counters:
  - `_Atomic uint32_t s_ring_head`
  - `_Atomic uint32_t s_ring_tail`
  - Use release store after writing PCM frames.
  - Use acquire load before reading PCM frames.
- Keep ring data writes ordinary, but publish availability only after the data is written.
- Consumer should advance tail with release semantics after reading.
- Space/queued calculations should use acquire loads and clamp if inconsistent.

Alternative implementation:

- Use FreeRTOS critical sections around head/tail updates and ring reads/writes.
- This is simpler to reason about but may add audio-task jitter if the critical section copies too much data.
- If using critical sections, copy data outside the lock where possible and lock only publication/claim.

Acceptance criteria:

- Single producer remains the BASIC decode pump.
- Single consumer remains the ESP32 audio task.
- `hal_audio_sample_push()` never overwrites unread frames.
- `render_frame()` never reads unpublished frames.
- EOF/drain logic still reports queued frames until the I2S DMA write has had time to drain.

Validation:

- Add a stress BASIC program that plays several short WAV/MP3/FLAC/MOD files in sequence and interrupts with `PLAY STOP`.
- Run with ESP32 dual-core scheduling enabled.
- Watch for underruns, stuck `MM.INFO$(PLAYING)`, and crashes.

### 5. Tighten Pico sample sink ownership around ping-pong buffers

Problem:

- Pico shared streaming now writes into the legacy ping-pong buffer state through `hal_audio_sample_push()` and `hal_audio_sample_acquire/commit()`.
- ISR and foreground code coordinate through `volatile` counters such as `bcount`, `swingbuf`, `nextbuf`, and `ppos`.
- This is legacy ISR-style code and may be acceptable on RP2, but the new shared sink should document and protect its publication points.

Files:

- `drivers/pwm_synth/pwm_synth.c`
- `drivers/sd_spi/mmc_stm32.c`

Implementation:

- Around foreground buffer publication (`bcount[target]`, `swingbuf`, `nextbuf`), briefly disable the audio PWM IRQ or use RP2040 critical sections.
- Do not hold the IRQ disabled while copying or converting a whole buffer.
- Add comments documenting that `bcount[target]` is the publication flag for a completed buffer.
- Confirm direct decode acquire/commit cannot publish a partially filled buffer if decode returns zero or fails.

Validation:

- Pico/PicoCalc manual:
  - `PLAY WAV`
  - `PLAY MP3`
  - `PLAY FLAC`
  - `PLAY MODFILE`
  - Rapid `PLAY STOP` while the file is starting and while it is draining.
- Confirm there is no repeated stale buffer chunk after underrun recovery.

## Phase 3: Clarify What Stays Port-Specific

### 6. Split `PLAY` command behavior into shared core plus capability hooks

Problem:

- Pico still uses `drivers/pwm_synth/pwm_synth.c::cmd_play()`.
- ESP32 and host use `shared/audio/Audio.c::cmd_play()`.
- This creates semantic drift between ports for command parsing, pause/resume, completion interrupts, and slot state.

Files:

- `shared/audio/Audio.c`
- `drivers/pwm_synth/pwm_synth.c`
- `hal/hal_audio.h`
- New optional file, for example `shared/audio/audio_capabilities.h`

Implementation approach:

- Keep a shared parser for common commands:
  - `PLAY STOP`
  - `PLAY CLOSE`
  - `PLAY PAUSE`
  - `PLAY RESUME`
  - `PLAY VOLUME`
  - `PLAY TONE`
  - `PLAY SOUND`
  - non-MIDI `PLAY NOTE`
  - `PLAY WAV`
  - `PLAY MP3`
  - `PLAY FLAC`
  - non-VS1053 `PLAY MODFILE`
- Add backend capability hooks or weak functions for non-common commands:
  - `PLAY ARRAY`
  - `PLAY STREAM`
  - `PLAY MIDI`
  - `PLAY MIDIFILE`
  - `PLAY HALT`
  - `PLAY CONTINUE`
  - `PLAY MODSAMPLE`
  - directory playlist `NEXT`/`PREVIOUS`
  - VS1053 paths
- Pico can implement these hooks in `pwm_synth.c` or a new `audio_pico_legacy.c`.
- Shared code should produce consistent "unsupported" errors when a port lacks a capability.

Acceptance criteria:

- Pico command coverage is unchanged.
- ESP32 gets the shared common behavior without accidentally inheriting VS1053-only commands.
- Host behavior remains compatible with simulator expectations.

### 7. Move portable playlist/file-open policy where practical

Problem:

- Pico still owns directory playlist handling and path normalization for WAV/FLAC/MP3/MIDIFILE.
- ESP32 shared path opens only a single file through `audio_stream_play_*`.

Decision:

- Decide whether directory playlist support should be portable MMBasic behavior or Pico-only behavior.

If portable:

- Extract shared helpers for:
  - extension append
  - full filename normalization
  - directory scan by extension
  - `PLAY NEXT`
  - `PLAY PREVIOUS`
- Keep VS1053 and MIDI playback hooks backend-specific.

If Pico-only:

- Document it as a Pico capability.
- Make shared `PLAY NEXT/PREVIOUS` return a clear unsupported error rather than silently doing nothing.

## Phase 4: Improve `OPTION AUDIO`

### 8. Keep option application per-port, share common helpers

Why per-port code is appropriate:

- `OPTION AUDIO` validates physical pin capabilities.
- It reserves boot pins.
- It chooses transport-specific resources: PWM slices, PIO state machines, ESP32 I2S/PDM GPIOs, SPI channels, VS1053 control pins.
- It persists options and resets using port-specific mechanisms.

Files:

- `core/mmbasic/MM_Misc.c`
- `ports/esp32_s3_metro/main/esp32_audio_options.c`
- Potential new shared helper:
  - `shared/audio/audio_option_common.c`
  - `shared/audio/audio_option_common.h`

Shared helper candidates:

- Parse `GPnn` versus MMBasic pin names.
- Check whether a pin is currently owned by the active audio backend.
- Clear common `Option.AUDIO_*` and `Option.audio_i2s_*` fields.
- Print common option forms.
- Return `MM.INFO$(AUDIO)` strings through a small capability table.

Keep per-port:

- PWM slice selection.
- PIO allocation.
- ESP32 WS/LRCLK derivation and GPIO validation.
- VS1053 support.
- Reset mechanism: `SoftReset()` versus `esp_restart()`.

### 9. Align ESP32 audio reconfiguration behavior with Pico or document the difference

Problem:

- Pico now allows reconfiguring over the currently owned audio pins, then disables and replaces the backend.
- ESP32 still errors with `Audio already configured`.

Files:

- `ports/esp32_s3_metro/main/esp32_audio_options.c`

Decision:

- Preferred: match Pico and allow reconfiguration when the requested pins are either free or currently owned by audio.
- Fallback: document ESP32 as requiring `OPTION AUDIO DISABLE` first.

Implementation if matching Pico:

- Add an ESP32 equivalent of `is_current_audio_pin()`.
- Replace strict `ExtCurrentConfig[pin] == EXT_NOT_CONFIG` checks with "free or current audio pin" checks.
- Call clear/disable before storing the new backend.
- Save options and restart.

Validation:

- ESP32 manual:
  - Configure I2S.
  - Configure PDM without first disabling.
  - Configure I2S again.
  - Confirm `OPTION LIST` and `MM.INFO$(AUDIO)` match the active backend.

### 10. Decide PicoCalc `OPTION AUDIO DISABLE` semantics

Problem:

- PicoCalc load overrides restore default PWM if no saved backend exists.
- Therefore `OPTION AUDIO DISABLE` may not persistently disable audio on PicoCalc.

Files:

- `ports/pico_sdk_common/port_load_overrides_picocalc.c`
- `core/mmbasic/MM_Misc.c`

Decision:

- Option A: keep current "disable means reset to board default on next boot" behavior and document it.
- Option B: add a persistent sentinel meaning "user explicitly disabled audio" and make PicoCalc load overrides respect it.
- Selected: Option B. `AUDIO_SLICE == 98` is the explicit-disabled sentinel; ordinary no-audio/default-reset state remains `AUDIO_SLICE == 99`.

Preferred:

- Option B if users expect `DISABLE` to persist.
- Option A if PicoCalc must always boot with working board audio unless another backend is configured.

Validation:

- `OPTION AUDIO DISABLE`
- Reboot.
- Confirm expected `OPTION LIST` and `MM.INFO$(AUDIO)`.

## Phase 5: Testing and Tooling

### 11. Build a comprehensive manual audio test suite for one configured device

Problem:

- `demos/sound/test_audio.bas` is a useful starting point, but it is not yet a complete acceptance suite for the areas touched by this refactor.
- The refactor spans command parsing, shared synth state, shared file decode, backend buffering, and pause/resume/stop semantics. A single tone/sound demo cannot catch those regressions.
- The manual suite should be runnable on one already-configured device and should not change persistent options, reboot the board, or depend on hardware that may not be present.

Files:

- `demos/sound/test_audio.bas`
- Potential new files:
  - `demos/sound/test_audio_core.bas`
  - `demos/sound/test_audio_files.bas`
  - `demos/sound/README.md`

Implementation:

- Keep the suite manual and prompt-driven, because audio quality, stereo routing, clipping, underruns, and backend output still need a listener.
- Treat the active device configuration as pre-existing. The suite may print `MM.INFO$(AUDIO)` or related state for operator context, but it must not run `OPTION AUDIO`, `OPTION AUDIO DISABLE`, or any command that causes a reboot.
- Split the suite into sections that can be run independently. Long file-playback and stress tests should not block quick synth checks.
- Add clear PASS/FAIL prompts after each section, but do not rely only on printed output. The operator should confirm what was heard.
- Include a short setup page or README with required sample files and the assumption that audio has already been configured for the device under test.
- Require known-good sample assets for each decoder:
  - short mono WAV
  - short stereo WAV
  - short MP3
  - short FLAC
  - short MOD
  - optional larger/longer file for underrun checks

Coverage to add beyond current `test_audio.bas`:

- `PLAY TONE`
  - mono and stereo frequencies
  - zero-frequency left/right silence
  - finite duration completion
  - interrupt completion where supported
  - pause, resume, stop, close, then new playback
- `PLAY SOUND`
  - all waveforms
  - slot independence across all four slots
  - left/right/both routing
  - `M` alias behavior
  - user waveform `U`
  - all-off state after stopping each slot
  - `PLAY STOP` followed by new slot playback, to catch stale slot-mask state
- `PLAY NOTE`
  - note on/off
  - velocity zero as note off
  - four-slot channel mapping
  - invalid channel behavior
- File playback
  - WAV mono and stereo
  - MP3
  - FLAC
  - MOD looping and no-loop interrupt behavior
  - pause/resume during file playback
  - stop during startup, middle, and drain
  - immediate next file after EOF
  - repeated open/close cycles to catch stale decoder/buffer state
- Buffer stress
  - rapid `PLAY STOP` during decode
  - small files in sequence
  - longer file under display/keyboard activity
  - low-free-memory scenario if feasible
- Exclusions for this manual suite
  - no `OPTION AUDIO` commands
  - no rebooting option tests
  - no VS1053-only commands
  - no MIDI, `PLAY STREAM`, `PLAY HALT`, or `PLAY CONTINUE`
  - no platform-specific features such as Pico `PLAY ARRAY` or playlist `NEXT`/`PREVIOUS`

Acceptance criteria:

- The manual suite can be run as a checklist on one configured device without editing the program for every section.
- Each section states expected audible behavior.
- The suite covers all common audio commands and file types touched by the shared refactor without requiring optional hardware.
- Bugs found during the suite are reproducible with a small section number and the active backend name.

### 12. Add command-level host tests for shared behavior

Files:

- `ports/host_native/tests/`

Tests to add:

- `PLAY PAUSE`/`PLAY RESUME` state transitions.
- `PLAY SOUND` slot mask cleanup after `PLAY STOP`.
- `PLAY NOTE ON/OFF` mapping to SOUND slots.
- `PLAY WAV/MP3/FLAC/MODFILE` unsupported or supported behavior according to host policy.

Run:

- `./ports/host_native/build.sh`
- `ports/host_native/build/mmbasic_test ports/host_native/tests/t115_play_tone_stop_native.bas`
- Use `./ports/host_native/run_tests.sh tests/<name>.bas` from `ports/host_native` or pass a path that the script sees as an existing file from its working directory.

### 13. Add shared decoder/backend unit-style tests where host can simulate a sink

Problem:

- Current host `hal_audio_sample_push()` accepts and discards frames, so stream EOF/drain behavior is barely tested.

Implementation:

- Add a test-only host audio backend or compile-time mode that stores queued frames in a small ring.
- Exercise:
  - partial pushes
  - EOF after pending frames
  - `audio_stream_stop()` during pending frames
  - direct acquire/commit path with zero-frame commit

Acceptance:

- No decoder reentrancy.
- EOF only completes after queued frames drain.
- Stop releases decoder/file/work memory.

### 14. Hardware smoke matrix

Pico/PicoCalc:

- Default PWM boot audio.
- `OPTION AUDIO I2S GP2, GP4`, then tone/sound/WAV/MP3/FLAC/MOD.
- `PLAY SOUND` all waveforms including `U`.
- `PLAY ARRAY`.
- VS1053 if available:
  - MP3
  - MIDI/MIDIFILE
  - STREAM
  - HALT/CONTINUE

ESP32-S3:

- `OPTION AUDIO I2S bclk,data`
- `OPTION AUDIO PDM left,right`
- `PLAY TONE`
- `PLAY SOUND`
- `PLAY NOTE`
- `PLAY WAV`
- `PLAY MP3`
- `PLAY FLAC`
- `PLAY MODFILE`
- Pause/stop/resume edge cases.
- Reconfigure audio backend if Phase 4 implements it.

Host/WASM:

- `PLAY TONE`
- `PLAY SOUND`
- `PLAY STOP`
- `PLAY PAUSE`
- `PLAY RESUME`
- Confirm file playback policy is explicit.

## Suggested Work Order

1. Fix `s_paused` reset in ESP32 backend.
2. Fix shared SOUND slot mask cleanup.
3. Decide and fix shared tone interrupt behavior.
4. Expand `demos/sound/test_audio.bas` into the comprehensive single-device manual suite or split it into the proposed focused manual test files.
5. Add focused host tests for the two shared-state fixes.
6. Replace or harden ESP32 ring synchronization.
7. Add RP2 ping-pong publication guards/comments.
8. Extract common `OPTION AUDIO` helpers.
9. Align ESP32 audio reconfiguration.
10. Decide PicoCalc persistent disable behavior.
11. Plan the larger `cmd_play()` split into shared common parser plus backend capability hooks.

## Completion Criteria

- Host build passes.
- HAL purity check passes.
- New host audio state tests pass.
- ESP32 pause/stop/new-playback manual smoke passes.
- ESP32 file stream stress test does not hang, crash, or leave stale `MM.INFO$(PLAYING)`.
- Comprehensive manual audio suite exists, documents required sample assets, avoids option/reboot and platform-specific commands, and covers every common command/file/sound-generation behavior touched by the refactor.
- Pico/PicoCalc existing audio demos still work.
- Documentation states which `PLAY` and `OPTION AUDIO` behaviors are shared, port-specific, or unsupported.
