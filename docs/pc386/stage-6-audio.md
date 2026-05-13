# Stage 6 - PC Audio

## Goal

Provide pc386 audio over the default Sound Blaster 16 backend, with the
original PC speaker retained as a smaller opt-in backend.

## Current State

Two audio HAL backends are selectable at build time:

```sh
make -C ports/pc386                     # default SB16 build
make -C ports/pc386 PC386_AUDIO=pcspk
```

Use the same value when launching QEMU so the expected emulated hardware is
attached:

```sh
ports/pc386/run.sh
PC386_AUDIO=sb16 PC386_SB_BASE=0x240 ports/pc386/run.sh
```

### PC Speaker

- `PLAY TONE left, right [, duration]` programs PIT channel 2 in mode 3
  square-wave mode.
- Because the hardware is mono, pc386 collapses left/right tone arguments to
  one square-wave pitch. If both are non-zero and different, one pitch is
  selected; the command still succeeds, but it cannot sound like stereo
  PicoMite output.
- `PLAY STOP`, `PLAY PAUSE`, and `PLAY RESUME` control indefinite tones.
- Finite-duration tones block until the duration expires, then stop the
  speaker. This avoids pretending we have asynchronous audio expiry before a
  periodic timer interrupt is wired into pc386.
- `PLAY SOUND` remains unsupported; the PC speaker path is one square-wave
  voice, not the multi-slot waveform synthesizer used by PicoMite.

### Sound Blaster 16

- `PC386_AUDIO=sb16` builds `hal_audio_pc386_sb16.c`; this is the default.
- QEMU launch attaches `-device sb16` with the selected host audio backend.
- The BASIC command `SB16` prints the current base/IRQ/DMA settings and probe
  status. `SB16 base[, irq[, dma[, dma16]]]` changes the runtime address,
  saves it, and reprobes the card, for example `SB16 &H240, 5, 1, 5`.
- `OPTION SB16 base[, irq[, dma[, dma16]]]` is the persistent form. pc386
  stores options in `C:/OPTIONS.INI` as editable key/value overrides, and the
  file only contains values that differ from pc386 defaults.
- `PLAY TONE left, right [, duration]` outputs generated 8-bit unsigned
  stereo PCM through SB16 DMA channel 1 at 22050 Hz.
- For integer tone frequencies, the DMA loop length is chosen to contain an
  exact whole number of cycles for both channels when that fits in the 64 KB
  DMA page. This avoids an audible click each time the auto-init buffer wraps.
- `PLAY STOP`, `PLAY PAUSE`, `PLAY RESUME`, and `PLAY VOLUME` are wired for
  the SB16 backend.
- `PLAY SOUND slot, channel, waveform[, frequency[, volume]]` is implemented
  for four generated PCM voices using the same SB16 DMA path. File/stream
  audio remains unsupported for now.

## QEMU

The interactive runner enables the selected QEMU audio device when possible:

```sh
ports/pc386/run.sh
PC386_AUDIO=pcspk ports/pc386/run.sh
```

On macOS the script selects QEMU's `coreaudio` backend. If that backend is not
available it still attaches the requested device with a silent `none` backend,
so the same kernel path is exercised.

## Tests

`ports/pc386/tests/repl_expect.py audio` verifies that `PLAY TONE`,
`PLAY PAUSE`, `PLAY RESUME`, and `PLAY STOP` return to the prompt without
falling through to the old Stage 3 error stub. The same test can be run
against the default SB16 build with:

```sh
make -C ports/pc386
python3 ports/pc386/tests/repl_expect.py audio
python3 ports/pc386/tests/repl_expect.py sb16
python3 ports/pc386/tests/repl_expect.py options_ini options_persist sb16_sound
```
