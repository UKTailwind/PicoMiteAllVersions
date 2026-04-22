# drivers/vs1053 — VS1053 MP3 codec SPI driver

Drives a VS1053 / VS1053b chip (Ogg Vorbis / MP3 / AAC / WMA / FLAC /
MIDI hardware codec) attached via SPI. The chip decodes compressed
audio in hardware so PicoMite can play streams that wouldn't fit
software decoding on an RP2040.

Origin: `baldram` / `edzelf` / `MagicCube` / `maniacbug` open-source
driver (GPLv3). Adapted for the Pico SDK. The flac patch blob in
`vs1053b-patches.h` ships pre-assembled.

Exposes:
- `VS1053(cs, dcs, dreq, reset)` — init the chip with the given SPI pins.
- `VS1053reset(reset_pin)` — hard reset cycle.
- SCI register control (volume, tone, chipID, balance, etc.) — internal.

Linked unconditionally (via `PICOMITE_BASE_SOURCES` in the root
CMakeLists.txt) — every target builds `VS1053.c` whether or not it is
a Web variant, because `Audio.c` references the `VS1053*` symbols
conditionally at runtime based on `Option.AUDIO_MISO_PIN`. A Web build
that sets the pin enables VS1053 playback; other builds leave it
dormant.

Header `VS1053.h` stays at repo root because it's the interface header
read by `Audio.c`; same pattern as `drivers/sd_spi/` leaving `diskio.h`
and `drivers/ps2_matrix/` leaving `PS2Keyboard.h` at repo root.

## Lifted from

`VS1053.c` + `vs1053b-patches.h` (repo root, pre-Phase-6 refactor).
No behavioural change — source relocated and the CMake reference
updated. `VS1053.h` stayed at root.

## Future work

- Implement `hal_audio` via this driver for Web variants (`hal_audio_tone`
  on VS1053 → `playimmediatevs1053(P_TONE)`, sample_push → streaming
  SDI). Today the chip is driven directly from `Audio.c`.
- Driver conformance tests per `docs/real-hal-plan.md`.
