# ESP32-S3 Metro Port — Session Log

Chronological record of decisions made during the bring-up. Pairs with
the plan at [esp32-s3-port.md](esp32-s3-port.md). The plan describes
intent; this log describes what actually happened.

## 2026-05-08 — Phase A start

Branched `esp32-port` off `main` at `696e7f5` (the post-rebrand tip with
SDK 2.1.1 firmware CI pin). Hardware: Adafruit Metro ESP32-S3 (#5500),
plugged in over USB-C. Currently running factory CircuitPython (USB VID
0x239a:0x8145).

### Decisions

- **Port directory: `ports/esp32_s3_metro/`** as the plan calls for.
  Reads `<chip>_<board>` so future ESP32-S3 boards (DevKitC, Feather S3)
  would each get their own `ports/esp32_s3_<board>/` rather than
  trying to share one port directory across board variants.
- **ESP-IDF release/v5.3.** Plan said "≥ 5.0"; pinned to 5.3 because
  it's the most recent LTS-flavoured release branch with stable
  ESP32-S3 support and predictable behaviour. Shallow clone
  (`--depth 1`) to save disk — full history adds ~1 GB and we don't
  need it.
- **Brew deps installed:** `dfu-util` (added; `cmake` / `ninja` /
  `git` / `python3` already present).
- **Disk pressure noted:** 11 GB free at session start. ESP-IDF +
  toolchains will land at roughly 2.5 GB. Acceptable but worth
  flagging if the install runs longer than expected.

### Phase A scope

Per the plan: "Empty ESP-IDF project. LED blink + 'hello' line over
USB Serial/JTAG. No MMBasic linkage yet."

Only the files needed for an empty IDF project go in this commit:
top-level `CMakeLists.txt`, `sdkconfig.defaults`, `main/CMakeLists.txt`,
`main/app_main.c`, `README.md`. The rest of the layout (the HAL impl
files) lands with Phase B.

### Notes

- The Metro's user LED on D13 maps to **GPIO13** per Adafruit's
  pinout. Used for the blink in `app_main.c`.
- Console output goes to USB Serial/JTAG (the chip's built-in
  controller), not UART0. `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in
  `sdkconfig.defaults`. No external probe needed.
