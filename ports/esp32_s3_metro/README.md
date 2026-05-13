# MMBasic Anywhere - Adafruit Metro ESP32-S3

ESP32-S3 port targeting the Adafruit Metro ESP32-S3 (#5500), currently verified on the N16R8 board variant: 16 MB flash and 8 MB embedded Octal PSRAM. PSRAM is enabled through ESP-IDF as caps-only external RAM for explicit port allocations; it is not part of MMBasic `AllMemory`. The port runs an MMBasic stdio REPL over the ESP32-S3 native USB Serial/JTAG interface.

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

Standard monitor:

```sh
idf.py -p /dev/cu.usbmodem* monitor
```

Exit with Ctrl-].

For automated smoke tests, prefer the repo-local probe:

```sh
python3 probe.py /dev/cu.usbmodem*
```

`probe.py` avoids the DTR pulse behavior that can reset the Metro into the wrong USB mode. Terminal programs such as `picocom` can work, but be careful with DTR/HUPCL reset behavior on open.

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
- ESP-IDF detects the onboard Octal PSRAM. `MM.INFO(PSRAM SIZE)` intentionally remains 0 and `MM.INFO(HEAP)` stays internal-only; ESP32-specific `MM.INFO(ESP32 PSRAM SIZE/FREE/LARGEST)` reports heap_caps-visible SPIRAM without enabling generic BASIC PSRAM allocation.
- `WEB CONNECT`, `WEB SCAN`, TCP server, TCP client request/stream, UDP send/receive, NTP, and plain-TCP MQTT are hardware-smoked.
- Bundled WEB demos seeded to A: include the small server demo and the multi-file website demo.
- `porttools/esp32_fs_vm_smoke.py` default smoke, opt-in flash/VAR persistence, and network conformance have passed on hardware.

Still stubbed or incomplete:

- BASIC-visible GPIO DOUT/DIN/ARAW is hardware-smoked. PWM/servo are still explicit unsupported paths.
- MQTT TLS/cert handling is not implemented; current MQTT support is plain TCP.
- Display, keyboard, audio, BLE/Bluetooth, OTA, and PSRAM-backed BASIC heap/display work are deferred.

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

- BASIC-visible GPIO uses ESP32-owned `vm_sys_pin_esp32.c` plus the Metro pin table. LEDC-backed PWM/servo remains future work.
- Legacy Pico SDK `hardware/*` compatibility headers come from neutral `ports/pico_sdk_compat/`.
- The build defines `MMBASIC_ESP32` only; the temporary `MMBASIC_HOST` compile-mode tag has been removed.
