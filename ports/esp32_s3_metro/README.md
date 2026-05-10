# MMBasic Anywhere - Adafruit Metro ESP32-S3

ESP32-S3 port targeting the Adafruit Metro ESP32-S3 (#5500), currently verified on the N16R8 board variant: 16 MB flash and 8 MB embedded Octal PSRAM. The port runs an MMBasic stdio REPL over the ESP32-S3 native USB Serial/JTAG interface.

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

Still stubbed or incomplete:

- BASIC-visible GPIO DOUT/DIN/ARAW is hardware-smoked. PWM/servo are still explicit unsupported paths.
- Display, keyboard, audio, WiFi/BLE, OTA, and PSRAM-backed heap/display work are deferred.

## Build Shape

The ESP32 port owns its runtime/peripheral surface in `main/esp32_*.c` and `main/hal_*_esp32.c`. It no longer links the host-native runtime or peripheral stubs, and the link intentionally avoids `--wrap` and `--allow-multiple-definition`.

Known remaining cleanup:

- BASIC-visible GPIO uses ESP32-owned `vm_sys_pin_esp32.c` plus the Metro pin table. LEDC-backed PWM/servo remains future work.
- Legacy Pico SDK `hardware/*` compatibility headers still come from `ports/host_native/`.
- The build still defines `MMBASIC_HOST` as a temporary compile-mode compatibility tag.
