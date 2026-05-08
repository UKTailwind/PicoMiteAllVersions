# MMBasic Anywhere — Adafruit Metro ESP32-S3

ESP32-S3 port targeting the [Adafruit Metro ESP32-S3](https://www.adafruit.com/product/5500) (#5500), N8R2 variant (8 MB flash, 2 MB Quad PSRAM).

Plan: [docs/real-hal/esp32-s3-port.md](../../docs/real-hal/esp32-s3-port.md). Session log: [docs/real-hal/esp32-s3-port-log.md](../../docs/real-hal/esp32-s3-port-log.md).

## Prerequisites

- ESP-IDF 5.3+ installed at `~/esp/esp-idf/`.
- Adafruit Metro ESP32-S3 plugged in via USB-C.
- `dfu-util` (for some boot-recovery paths). `cmake`, `ninja`, `python3` provided by IDF.

```sh
. ~/esp/esp-idf/export.sh
```

## Build / flash / monitor

From this directory:

```sh
idf.py set-target esp32s3   # one-time, on first build or after `fullclean`
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

Exit the monitor with Ctrl-]. The build artifact is at `build/mmbasic_anywhere_metro.bin` and is also written to flash by `idf.py flash`.

## Phase A scope

Toolchain bring-up. Blinks D13 at 1 Hz and prints `tick N` once per second over USB Serial/JTAG. Confirms board + toolchain + console path before any MMBasic linkage.

Phase B onward adds the MMBasic core source list to `main/CMakeLists.txt` and the HAL implementations to `main/hal_*_esp32.c`.
