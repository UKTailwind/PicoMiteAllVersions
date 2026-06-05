# Audio Refactor Summary

Date captured: 2026-06-04

This document records the current in-progress audio refactor so a follow-up audit can start from concrete state instead of reconstructed chat context.

## Purpose

The refactor is moving audio toward a shared architecture:

- Decode file formats once in shared code.
- Synthesize `PLAY TONE`, `PLAY SOUND`, and non-MIDI `PLAY NOTE` once in shared code.
- Keep only the physical transport in each port: RP2 PWM/I2S/VS1053 details, ESP32 I2S/PDM details, host no-op/simulated sinks.
- Avoid regressions in PicoCalc defaults and existing PicoMite command semantics while adding ESP32 audio support.

MIDI and `PLAY STREAM` are intentionally out of scope for this pass.

## Current Shared Code

### File Playback

`shared/audio/audio_stream.c` and `shared/audio/audio_stream.h` are the new shared decode/service path for:

- `PLAY WAV`
- `PLAY MP3`
- `PLAY FLAC`
- `PLAY MODFILE`

The shared stream code opens files through the MMBasic file layer (`FindFreeFileNbr`, `BasicFileOpen`, `hal_fds`, `hal_fs_read`, `hal_fs_seek`) and decodes to interleaved stereo `int16_t` PCM. Decoded frames are fed to the backend through the sample sink in `hal/hal_audio.h`:

- `hal_audio_sample_begin(sample_rate_hz)`
- `hal_audio_sample_push(frames, frame_count)`
- `hal_audio_sample_acquire()` / `hal_audio_sample_commit()` for optional zero-copy buffers
- `hal_audio_sample_space()`
- `hal_audio_sample_queued()`
- `hal_audio_sample_eof()`
- `hal_audio_sample_end()`
- `hal_audio_workmem_alloc/realloc/free()`

WAV, MP3, and FLAC can use the zero-copy path when the backend exposes a discrete writable buffer and the source is already stereo. Mono sources and MOD playback use scratch buffers. MOD files are loaded into backend work memory and rendered through hxcmod at 22050 Hz, then doubled to a 44100 Hz sink rate.

End-of-file is now explicit. The shared decoder marks EOF with `hal_audio_sample_eof()` after all decoded frames are queued, then waits for `hal_audio_sample_queued()` to drain before `audio_stream_finish()` clears `CurrentlyPlaying` and sets `WAVcomplete`.

### Synth Playback

`shared/audio/synth_pcm.c` and `shared/audio/synth_pcm.h` hold shared tone/sound state, wave tables, volume mapping, and frame mixers:

- `synth_pcm_tone_frame()`
- `synth_pcm_sound_sample()`
- `synth_pcm_sound_frame()`
- `getsound()`
- `setnoise()`

`PLAY NOTE` is implemented as a non-MIDI adapter on top of the existing four `SOUND` slots. It maps note number to frequency and velocity to synth volume. This is not full MIDI channel support; it is a four-slot synth path for non-MIDI backends.

### HAL Surface

`hal/hal_audio.h` now covers both command-level synth controls and file/sample streaming. MIDI remains outside this shared audio HAL.

Host and PC backends provide no-op sample-sink implementations so shared code links. ESP32 and Pico provide real sinks.

## Pico / RP2 Status

The top-level CMake audio device source set now includes:

- `drivers/pwm_synth/pwm_synth.c`
- `shared/audio/audio_stream.c`
- `shared/audio/synth_pcm.c`

Pico file playback is partly shared now:

- `pwm_synth.c` calls `audio_stream_play_wav()`, `audio_stream_play_mp3()`, `audio_stream_play_flac()`, and the non-VS1053 MOD path calls `audio_stream_play_mod_noloop()`.
- The old local dr_wav/dr_flac decode bodies are no longer the primary Pico decode path.
- `pwm_synth.c` implements the Pico sample sink with its existing ping-pong buffers. `hal_audio_sample_acquire/commit()` allows direct decode into those buffers for stereo WAV/MP3/FLAC.
- `drivers/sd_spi/mmc_stm32.c` uses the shared synth mixers for ISR playback and has EOF handling for file drains.

Pico command parsing is not fully shared:

- Pico still owns its main `cmd_play()` in `drivers/pwm_synth/pwm_synth.c`.
- `shared/audio/Audio.c` owns the host/ESP32-style `cmd_play()` surface.
- Pico-specific features still live locally: VS1053, MIDI/MIDIFILE, STREAM, ARRAY, playlist `NEXT`/`PREVIOUS`, and several legacy buffer paths.

Pico I2S still exists:

- `OPTION AUDIO I2S bclk,data` uses `bclk` as BCLK, `bclk + 1` as LRCLK/WS, and `data` as DIN/DOUT-to-DAC.
- For PicoCalc external pins, a practical test configuration is `OPTION AUDIO I2S GP2, GP4`; this means GP2=BCLK, GP3=LRCLK/WS, GP4=DATA.
- PicoCalc still defaults to PWM on GP26/GP27 when no audio backend is saved,
  unless the saved options contain the explicit disabled-audio sentinel.
- Pico `OPTION AUDIO ...` validation now allows reconfiguring over the currently-owned audio pins, then disables the old backend and saves the new one.
- `OPTION AUDIO DISABLE` persists a disabled PicoCalc state using an invalid
  `AUDIO_SLICE` sentinel. A factory/default reset still restores board-default
  PWM because it uses the ordinary unset sentinel.

Pico MP3 behavior:

- The 200 MHz guard is restored for MP3 playback without VS1053.
- MP3 no longer requires PSRAM, but performance is file-dependent and may still stutter, especially near 200 MHz.
- `RUN` now calls `CloseAudio(1)` before loading a new program to avoid stale decoder/buffer state across repeated runs.

## ESP32-S3 Status

The old ESP32 audio stub is deleted and replaced by `ports/esp32_s3_metro/main/hal_audio_esp32.c`.

Implemented output modes:

- I2S standard output for external DAC/amp devices such as MAX98357.
- ESP32-S3 I2S PDM TX output on two GPIOs for filterless/simple-DAC-style output.

ESP32 option parsing lives in `ports/esp32_s3_metro/main/esp32_audio_options.c`:

- `OPTION AUDIO I2S bclk,data`; WS/LRCLK is inferred as `bclk + 1`.
- `OPTION AUDIO left,right`; configures PDM.
- `OPTION AUDIO PDM left,right`; explicit PDM form.
- `OPTION AUDIO DISABLE`.
- `MM.INFO$(AUDIO)` returns `I2S`, `PDM`, or `OFF`.

ESP32 audio can now be reconfigured over pins owned by the active audio
backend. Pins owned by other peripherals still fail validation. The old audio
backend options are cleared before the new I2S/PDM backend is saved and the
board restarts.

ESP32 backend details:

- A dedicated FreeRTOS audio task writes 256-frame chunks to I2S/PDM.
- File playback uses a 32768-frame stereo ring, allocated from PSRAM when possible with a malloc fallback.
- File stream sample rate changes are applied inside the audio task between writes.
- EOF handling now drains at least one zero chunk and uses a short tail hold before reporting the queue empty, intended to reduce end-of-file clicks/glitches on I2S amps.

Known ESP32 issue needing retest: there was still a small end-of-file glitch on I2S/MAX after streamed WAV/MP3/FLAC. The tail-hold change was added after that report; hardware confirmation is still pending.

## User-Visible Command Scope

Currently expected in this refactor:

- `PLAY TONE`
- `PLAY SOUND`
- `PLAY NOTE ON/OFF` over synth slots
- `PLAY WAV`
- `PLAY MP3`
- `PLAY FLAC`
- `PLAY MODFILE`
- `PLAY STOP`, `PLAY CLOSE`, `PLAY PAUSE`, `PLAY RESUME`, `PLAY VOLUME`

Intentionally not completed in the shared software path:

- MIDI/MIDIFILE
- `PLAY STREAM`
- `PLAY ARRAY`
- VS1053-specific behavior
- Playlist controls outside the Pico legacy path

## Planned `cmd_play()` Split

The full Pico `cmd_play()` migration should be a staged extraction, not a
single rewrite. This section is a planning artifact only; do not implement the
large parser refactor in the current pass. The next implementation pass should
introduce a shared parser for common `PLAY` forms and a small capability table
for backend-specific extensions.

Recommended structure:

- Keep `shared/audio/Audio.c` as the owner of common command semantics:
  `STOP`, `CLOSE`, `PAUSE`, `RESUME`, `VOLUME`, `TONE`, `SOUND`, non-MIDI
  `NOTE`, `WAV`, `MP3`, `FLAC`, and software `MODFILE`.
- Add a capability/hook header for non-common commands. Pico can provide hooks
  for `ARRAY`, `STREAM`, MIDI/MIDIFILE, VS1053 paths, MOD samples, and
  playlist `NEXT`/`PREVIOUS`. ESP32/host should return consistent unsupported
  errors when those hooks are absent.
- Keep file extension normalization and playlist directory scanning behind
  explicit policy. If playlist behavior remains Pico-only, the shared parser
  should say so through the capability hook instead of silently no-oping.
- Do not move VS1053, MIDI, `PLAY STREAM`, or `PLAY ARRAY` into the base shared
  parser. They require transport-specific state, ISR behavior, or legacy buffer
  contracts that should stay in Pico hook code until separately audited.

Risk areas to preserve explicitly:

- Argument compatibility: accepted arities, default values, optional interrupt
  arguments, type coercion, and exact error messages where BASIC programs rely
  on them.
- Runtime state transitions: completion interrupts, `WAVcomplete`,
  `CurrentlyPlaying`, SOUND slot cleanup, pause/resume after stop/close, and
  EOF/drain behavior before another file starts.
- File command policy: extension inference and normalization, unsupported
  extension errors, playlist directory scans, and `NEXT`/`PREVIOUS` behavior.
- Backend-only behavior: VS1053, MIDI/MIDIFILE, `PLAY STREAM`, `PLAY ARRAY`,
  MOD samples, legacy MOD/file buffer paths, and any ISR or transport-specific
  state they depend on.

Staged acceptance criteria and testing expectations:

- Stage 1 extracts only common state commands: `STOP`, `CLOSE`, `PAUSE`,
  `RESUME`, and `VOLUME`. Pico, ESP32, and host behavior must match existing
  behavior, including repeated calls and unsupported-backend errors.
- Stage 2 extracts software synth commands: `TONE`, `SOUND`, and non-MIDI
  `NOTE`. Host tests should cover argument compatibility, tone interrupts where
  supported, SOUND slot cleanup, and pause/resume/stop interactions.
- Stage 3 extracts common file commands: `WAV`, `MP3`, `FLAC`, and software
  `MODFILE`. Tests should cover extension handling, failed opens, EOF/drain,
  immediate next-file playback, pause/resume, and stop during startup and drain.
- Stage 4 wires Pico-only hooks without moving them into the shared parser:
  VS1053, MIDI/MIDIFILE, `PLAY STREAM`, `PLAY ARRAY`, MOD samples, playlist
  `NEXT`/`PREVIOUS`, and legacy buffer paths. Hardware/manual testing is
  required for each hooked feature.
- Every stage should keep Pico command coverage and error behavior unchanged
  for migrated branches, while ESP32 and host expose only the shared
  software-audio surface plus explicit unsupported-feature errors.
- Each stage should land with focused host tests where possible and an updated
  manual hardware checklist for Pico and ESP32 behavior that cannot be covered
  by host tests.

## Bugs Fixed During This Pass

- ESP32 now has real audio output instead of stubs.
- ESP32 PWM output was removed in favor of hardware PDM TX.
- `OPTION AUDIO left,right` on ESP32 configures PDM output.
- `PLAY NOTE` works over the software synth path without MIDI.
- PicoCalc no longer overwrites a saved I2S backend with default PWM on every boot.
- PicoCalc now persists explicit `OPTION AUDIO DISABLE` instead of restoring
  default PWM on the next boot.
- Pico `OPTION AUDIO` can reconfigure an already configured backend.
- ESP32 `OPTION AUDIO` can reconfigure over pins already owned by audio.
- Pico file playback no longer leaves junk noise after WAV/FLAC EOF in the tested case.
- Pico `test_audio.bas` file-playback step no longer skips because stale `P_SOUND` state is left after all SOUND slots are off.
- Repeated `RUN "mp3.bas"` no longer keeps stale audio decoder state across program loads.
- The Pico MP3 CPU speed guard was restored.
- Shared decoder scratch buffers are released per stream.

## Build And Hardware Status

Builds that have been exercised during this refactor:

- PicoCalc RP2350 via `./build_picocalc_firmware.sh rp2350`.
- ESP32-S3 Metro via `./buildesp32.sh`.
- Host native via `make -C ports/host_native -j4`.

Hardware tests reported during the session:

- ESP32 PDM output worked filterless into a cheap radio aux input.
- ESP32 I2S output to MAX amp works, with a remaining EOF-glitch retest pending.
- PicoCalc default PWM works on GP26/GP27.
- PicoCalc I2S should be testable on external GPIOs with `OPTION AUDIO I2S GP2, GP4`.
- Pico MOD playback works without PSRAM.
- Pico MP3 playback works without PSRAM but may stutter depending on file and CPU speed.

Not all Pico variants or all host/PC/WASM targets have been rebuilt after every latest edit. Treat cross-target build coverage as incomplete until audited.

## Audit Checklist

Highest priority:

- Execute the planned `cmd_play()` split in small stages, starting with common
  command branches and adding backend capability hooks for Pico-only commands.
- Compare Pico and shared command behavior for every `PLAY` subcommand, especially argument counts, interrupts, pause/resume, file extension handling, and state transitions.
- Audit `shared/audio/audio_stream.c` EOF behavior across WAV/MP3/FLAC/MOD and both Pico and ESP32 sinks.
- Retest ESP32 I2S streamed-file EOF after the tail-hold change.
- Hardware-test ESP32 `OPTION AUDIO` reconfiguration across I2S and PDM.

Pico-specific:

- Audit the remaining legacy playback paths in `drivers/pwm_synth/pwm_synth.c`: VS1053, MIDI, STREAM, ARRAY, playlist handling, and old MOD/file buffer branches.
- Verify Pico I2S EOF logic in `drivers/sd_spi/mmc_stm32.c` for both file playback and synth playback.
- Hardware-test PicoCalc `OPTION AUDIO DISABLE` persistence across reboot.
- Review pin validation/reconfiguration in `core/mmbasic/MM_Misc.c`, including `PINMAP[PinDef[pin].GPno + 1]` use for inferred I2S LRCLK.
- Re-test MP3 performance against the previous Pico code path and confirm whether any extra copies remain.

ESP32-specific:

- Audit ring-buffer counters and EOF tail-hold state for races between the decoder service loop and the FreeRTOS audio task.
- Confirm PDM and I2S sample-rate switching is click-free enough when returning from file playback to synth rate.
- Decide whether ESP32 should implement the same audio reconfiguration behavior as Pico.
- Confirm `OPTION AUDIO I2S bclk,data` documentation consistently states WS/LRCLK is `bclk + 1`.

Shared code:

- Ensure `CloseAudio()` and `StopAudio()` reset all shared synth and SOUND-slot bookkeeping in both command paths.
- Confirm `audio_stream_stop()` is safe from every caller, including failed opens, repeated `RUN`, `PLAY STOP`, and program abort paths.
- Confirm MOD work memory is released on every failure and stop path.
- Confirm zero-copy decode is used only when the backend buffer format is exactly interleaved stereo `int16_t`.
- Decide whether `demos/sound/test_audio.bas` should be tracked and whether its comments need updating now that WAV/MP3/FLAC/MOD are wired on ESP32.

Cross-build:

- Rebuild all PicoCalc/Pico/DVI/HDMI/WiFi variants that include the audio source set.
- Rebuild ESP32, host native, host WASM, stdio/ANSI, and PC386 variants after the refactor settles.
- Run any HAL purity or port duplication checks that are expected for shared code.

## Quick Test Commands

PicoCalc I2S external test:

```basic
OPTION AUDIO I2S GP2, GP4
CPU RESTART
PRINT MM.INFO$(AUDIO)
PLAY TONE 440, 400
PLAY WAV "chime.wav"
PLAY MP3 "chime.mp3"
PLAY FLAC "chime.flac"
PLAY MODFILE "laamaa.mod"
RUN "test_audio.bas"
```

ESP32 I2S:

```basic
OPTION AUDIO I2S GP5, GP7
```

This means GP5=BCLK, GP6=WS/LRCLK, GP7=DATA.

ESP32 PDM:

```basic
OPTION AUDIO GP12, GP13
OPTION AUDIO PDM GP12, GP13
```
