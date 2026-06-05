# Manual Audio Test Suite

`test_audio.bas` is a prompt-driven acceptance suite for one already-configured
audio device. It does not run `OPTION AUDIO`, `OPTION AUDIO DISABLE`, or any
command that should reboot the board.

Run it from this directory or copy it with the sample files to the device:

```basic
RUN "test_audio"
```

At each section prompt, press any key to run, `S` to skip, or `Q` to quit. After
a section runs, record `P` pass, `F` fail, or `S` unsupported/skipped. Use
headphones when checking left/right routing on mono-speaker devices.

## Required Sample Assets

Place these known-good files next to `test_audio.bas`:

- `mono.wav`: mono PCM WAV, 1.0-1.2 seconds.
- `stereo.wav`: stereo PCM WAV, 1.0-1.2 seconds, with clearly different
  left/right content.
- `short.mp3`: MP3, 1.0-1.2 seconds.
- `short.flac`: FLAC, 1.0-1.2 seconds.
- `short.mod`: ProTracker-compatible MOD that can loop normally, but whose
  no-loop playback reaches song end within 6 seconds.

Optional:

- `long.wav`: longer WAV, 10 seconds or more, for underrun and keyboard/display
  activity stress.

The exact audio content is not important, but the short WAV/MP3/FLAC files
should stay inside the documented duration window. The suite's near-end STOP
checks run at about 1.0 seconds, before the expected EOF, and its immediate-next
checks wait at least 1.8 seconds so EOF should already have occurred. The stereo
WAV should make channel swaps or mono collapse obvious.

The MOD no-loop interrupt check is optional. On a backend that supports the
second `PLAY MODFILE` argument as no-loop completion interrupt, `short.mod`
should report one interrupt within the suite's 7.5 second wait. If the backend
loops MODFILE or does not support that interrupt form, mark that section
unsupported/skipped.

Low-free-memory stress is intentionally omitted. It is not practical to create a
repeatable constrained-memory setup inside this one-device manual suite without
making assumptions about the operator's firmware, PSRAM, display mode, and loaded
programs. Run a separate constrained-memory setup if that behavior needs manual
validation.

## Coverage

The suite covers `PLAY TONE`, `PLAY SOUND`, `PLAY NOTE`, `PLAY WAV`, `PLAY MP3`,
`PLAY FLAC`, `PLAY MODFILE`, `PLAY PAUSE`, `PLAY RESUME`, `PLAY STOP`,
`PLAY CLOSE`, `PLAY VOLUME`, and `PLAY LOAD SOUND`.

File and interrupt cases skip cleanly on ports that do not support that codec or
completion interrupt. Record the active backend from the opening `Audio` line
and the failing section number when reporting bugs.
