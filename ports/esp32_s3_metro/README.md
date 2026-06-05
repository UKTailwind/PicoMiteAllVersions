# MMBasic Anywhere - Adafruit Metro ESP32-S3

ESP32-S3 port targeting the Adafruit Metro ESP32-S3 (#5500), currently verified on the N16R8 board variant: 16 MB flash and 8 MB embedded Octal PSRAM. PSRAM is owned by MMBasic via a fixed slab reserved from ESP-IDF at boot; `PSRAMsize` and the shared `RAM` command surface match Pico variants. The port runs an MMBasic stdio REPL over the ESP32-S3 native USB Serial/JTAG interface.

Plan: [docs/real-hal/esp32-s3-port.md](../../docs/real-hal/esp32-s3-port.md). Session log: [docs/real-hal/esp32-s3-port-log.md](../../docs/real-hal/esp32-s3-port-log.md).

## Prerequisites

- ESP-IDF 5.3 installed at `~/esp/esp-idf/`.
- Adafruit Metro ESP32-S3 connected over USB-C.
- `dfu-util` for recovery paths.
- `pyserial` if using `probe.py`.

Load the ESP-IDF environment before building:

```sh
. ~/esp/esp-idf/export.sh
```

## Build

From this directory:

```sh
idf.py set-target esp32s3   # one-time, or after idf.py fullclean
idf.py build
```

The firmware image is written under `build/`.

From the repo root, the opt-in helper runs the HAL purity gate first and then builds this port:

```sh
./buildesp32.sh
```

## Flash

```sh
idf.py -p /dev/cu.usbmodem* flash
```

If macOS hangs at `Connecting...`, put the board into ROM USB Direct mode:

1. Hold BOOT.
2. Press and release RESET.
3. Release BOOT.
4. Re-run the flash command once `/dev/cu.usbmodem*` reappears.

This is a USB CDC binding quirk, not a firmware build problem.

## Monitor And Probe

### Interactive REPL

The console is a full MMBasic REPL over the native USB Serial/JTAG interface
(115200 baud, nominal — it is USB CDC, not a real UART). For an interactive
terminal, use the `/dev/cu.*` node (not `/dev/tty.*` — `cu` does not assert DTR
on open, which avoids resetting the board into the bootloader):

```sh
screen /dev/cu.usbmodem* 115200      # exit: Ctrl-A then K, then y
```

`minicom -D /dev/cu.usbmodem* -b 115200` and `tio /dev/cu.usbmodem*` also work.
Only one program can hold the port at a time — close the terminal before running
`probe.py` / the smoke suites, and vice versa.

### Monitor and one-shot commands

Standard ESP-IDF monitor (firmware logs + console):

```sh
idf.py -p /dev/cu.usbmodem* monitor
```

Exit with Ctrl-]. For scripted single commands (no terminal):

```sh
python3 probe.py /dev/cu.usbmodem* --cmd 'PRINT MM.VER'
```

`probe.py` avoids the DTR pulse behavior that can reset the Metro into the wrong USB mode. Terminal programs such as `picocom` can work, but be careful with DTR/HUPCL reset behavior on open.

## Web Console

The port can serve a browser-based terminal + 320×240 framebuffer over WiFi.
From the REPL:

```basic
OPTION WIFI "your-ssid", "your-password"   ' stored in NVS, one-time
WEB CONNECT                                ' join the network
PRINT MM.INFO$(IP ADDRESS)                 ' note the address, e.g. 192.168.5.89
OPTION WEB CONSOLE ON                      ' enable it (reboots the board)
```

After it reboots it auto-rejoins WiFi (the credentials are persisted) and listens
on `Option.TCP_PORT` (default **80**). Then open this URL in a browser on the same
network — **note the `/__web_console/` path, the root `/` will not work**:

```
http://<device-ip>/__web_console/
```

The console UI is namespaced under `/__web_console/` on purpose, so it does not
collide with pages your own BASIC programs serve via `WEB TRANSMIT PAGE`. An
unhandled `GET /` is left for those programs and otherwise returns an empty reply.

## Audio

`PLAY TONE`, `PLAY SOUND` (all waveforms S/Q/T/W/P/N/U, the four SOUND slots),
and `PLAY NOTE` are synthesized by the shared software synthesizer
(`shared/audio/synth_pcm.c`, the same kernel the RP2 ports use). The ESP32
backend can output either to an external standard-I2S DAC or to the ESP32-S3
I2S PDM TX DAC-style two-line output.

Default audio is I2S using the pins in `port_config.h` (defaults avoid the
strapping, USB, and Octal-PSRAM GPIOs on the N16R8):

| Signal | Default GPIO | `port_config.h` macro |
|---|---|---|
| BCLK (bit clock) | 5 | `HAL_PORT_AUDIO_I2S_BCLK_PIN` |
| WS / LRCLK | 6 | `HAL_PORT_AUDIO_I2S_WS_PIN` |
| DOUT (serial data) | 7 | `HAL_PORT_AUDIO_I2S_DOUT_PIN` |

Audio configuration:

| Command | Backend | Pins |
|---|---|---|
| `OPTION AUDIO I2S bclk,data` | Standard I2S PCM for an external DAC/amp | `bclk`, inferred `ws = bclk + 1`, `data` |
| `OPTION AUDIO left,right` | ESP32-S3 I2S PDM TX DAC-style two-line output | left PDM output, right PDM output |
| `OPTION AUDIO PDM left,right` | Same as the bare two-pin form | left PDM output, right PDM output |
| `OPTION AUDIO DISABLE` | Audio off | none |

```
OPTION AUDIO I2S GP5,GP7
OPTION AUDIO GP12,GP13
OPTION AUDIO PDM GP12,GP13
OPTION AUDIO DISABLE
PRINT MM.INFO$(AUDIO)
```

For I2S, WS/LRCLK is inferred as `BCLK + 1`, matching the RP2 `OPTION AUDIO I2S`
shape. The two-pin form configures the ESP32-S3 hardware PDM TX converter, not
LEDC PWM. The selected pins are the left/right PDM data outputs; no separate
PDM clock pin is exposed by the BASIC option. `MM.INFO$(AUDIO)` reports `I2S`,
`PDM`, or `OFF`.

PDM output can be filtered and amplified externally for analog audio. In
practice it also works directly into some high-impedance AUX inputs, but a
simple low-pass filter and amplifier remain the recommended hardware path.

Tone/SOUND synthesis runs at `HAL_PORT_AUDIO_SAMPLE_RATE` (44100). File playback
for `PLAY WAV`, `PLAY FLAC`, `PLAY MP3`, and `PLAY MODFILE` uses the shared
decoder path in `shared/audio/audio_stream.c`, feeding the selected backend with
16-bit stereo PCM. MIDI, ARRAY, and STREAM playback are not wired in this port.
The exhaustive `demos/sound/test_audio.bas` exercises the tone/SOUND surface.

## Current Status

Working on hardware:

- Interactive REPL over USB Serial/JTAG.
- `PRINT`, `FOR`/`NEXT`, `IF`/`ELSE`, `GOTO`/`GOSUB`, `LIST`, `EDIT`, `CLS`, `COLOUR`, and `CPU RESTART`.
- A: drive backed by LittleFS over an ESP-IDF flash partition.
- Bundled demos seeded to A:: `hello.bas`, `fizzbuzz.bas`, `sieve.bas`, and `mand.bas`.
- `FILES`, `LOAD`, `SAVE "file.bas"`, `RUN`, and `FRUN` for A: files.
- `RUN "mand.bas"` and `FRUN "mand.bas"` produce checksum `552868`; `FRUN` is currently about 24x faster on the Metro.
- B: drive rejects cleanly as not configured.
- `OPTION` persistence is backed by ESP-IDF NVS and has been hardware-smoked across reset/reflash.
- Default terminal colours survive errors and prompt recovery through shared MMBasic colour-state restoration.
- `FLASH SAVE 1`, reset, `FLASH LOAD 1`, `RUN` works on the dedicated `mmslots` partition.
- 48 KB WiFi-enabled MMBasic heap. ESP32 bytecode compiler scratch tables use ESP-IDF internal heap; VM runtime allocations still use the 48 KB MMBasic heap.
- ESP-IDF detects the onboard Octal PSRAM. The port reserves a fixed slab via `heap_caps_aligned_alloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` at boot and publishes it as `PSRAMbase` / `PSRAMsize`; `MM.INFO(PSRAM SIZE)` now returns the slab size and the shared `RAM` command (test / list / save / load / erase) works the same as on Pico variants. `RAM TEST NOCACHE` is Pico-only and errors on ESP32.
- `WEB CONNECT`, `WEB SCAN`, TCP server, TCP client request/stream, UDP send/receive, NTP, and plain-TCP MQTT are hardware-smoked.
- Bundled WEB demos seeded to A: include the small server demo and the multi-file website demo.
- Browser web console over WiFi at `http://<device-ip>/__web_console/` (see [Web Console](#web-console)).
- `PLAY TONE` / `PLAY SOUND` / `PLAY NOTE` over external I2S DAC or I2S PDM TX via the shared synthesizer (see [Audio](#audio)).
- `porttools/esp32_fs_vm_smoke.py` default smoke, opt-in flash/VAR persistence, and network conformance have passed on hardware.

Still stubbed or incomplete:

- BASIC-visible GPIO DOUT/DIN/ARAW is hardware-smoked. PWM/servo are still explicit unsupported paths.
- MQTT TLS/cert handling is not implemented; current MQTT support is plain TCP.
- MIDI, ARRAY, and STREAM playback are not wired.
- Display (local LCD/VGA), keyboard, BLE/Bluetooth, and OTA are deferred.

## Port Tools

Host-side smoke tooling lives in [`../../porttools`](../../porttools/README.md).
Use `basic_serial.py` for prompt-driven command checks and
`esp32_fs_vm_smoke.py` for the Stage G0 device smoke suite. The network suite
chains to `network_conformance.py`; `esp32_tcp_smoke.py` remains available for
narrow TCP client request/stream debugging.

Known-good quick checks:

```sh
python3.11 ../../porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --boot-wait 1 \
  --cmd 'PRINT "ESP32_PROMPT_OK"' \
  --expect ESP32_PROMPT_OK

python3.11 ../../porttools/esp32_tcp_smoke.py \
  --port /dev/cu.usbmodem101 \
  --host 192.168.4.23

python3.11 ../../porttools/esp32_fs_vm_smoke.py psram \
  --port /dev/cu.usbmodem2101 \
  --timeout 12 \
  --long-timeout 120
```

## Build Shape

The ESP32 port owns its runtime/peripheral surface in `main/esp32_*.c` and `main/hal_*_esp32.c`, while reusing the common runtime spine for shared source loading and abort/interrupt helpers where the sequencing matches. It no longer links the host-native runtime or peripheral stubs, and the link intentionally avoids `--wrap` and `--allow-multiple-definition`.

Known remaining cleanup:

- BASIC-visible GPIO uses ESP32-owned `vm_sys_pin_esp32.c` plus the Metro pin table. PWM/servo remains future work.
- Legacy Pico SDK `hardware/*` compatibility headers come from neutral `ports/pico_sdk_compat/`.
- The build defines `MMBASIC_ESP32` only; the temporary `MMBASIC_HOST` compile-mode tag has been removed.
