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

### Phase A status (autonomous run, 2026-05-08)

- **Build: ✅ green.** `idf.py build` produces a 178 KB
  `mmbasic_anywhere_metro.bin` (83% of the 1 MB app partition free).
- **Flash: ⏳ blocked on physical access.** When `idf.py flash`
  triggered DTR/RTS auto-reset, the Metro flipped from CircuitPython
  (USB VID `0x239a:0x8145`) to the chip's USB JTAG/serial debug unit
  (`0x303a:0x1001`) — the right state in principle, but **macOS does
  not auto-bind a CDC driver for that PID**. esptool can't find a
  `/dev/cu.*` for it, and OpenOCD over USB JTAG hits
  `esp_usb_jtag: could not find or open device!` at
  `libusb_open()` — Apple's CDC driver appears to claim the
  interface without exposing it.

  The standard recovery is to **enter ROM USB Direct mode** by
  holding BOOT while pressing RESET (which enumerates as
  `0x303a:0x0002`, a stable CDC ACM that macOS does bind). Requires
  physical button presses on the board.

  Resume path when next at the keyboard:
    1. Hold BOOT button, press and release RESET, release BOOT.
    2. `ls /dev/cu.usbmodem*` should show a fresh port.
    3. `cd ports/esp32_s3_metro && idf.py -p /dev/cu.usbmodem* flash monitor`.
    4. Confirm `tick N` heartbeat lines + LED blink.

  This is unrelated to the build itself; the binary is correct.
  The hold-up is purely a macOS USB-CDC-binding quirk specific to
  ESP32-S3's `0x1001` PID.

### Continuing autonomously into Phase B

Phase A's build pipeline is validated end-to-end (target set,
sdkconfig, blink + heartbeat firmware compiles and links cleanly on
Xtensa GCC). Phase B doesn't depend on physical flash — it's about
getting the MMBasic core to **link** on Xtensa. Moving forward to
that work; Phase A flash gets confirmed by user when they return.

### Phase B portability audit (2026-05-08)

Grepped the core / VM / shared graphics / state files that the
`mmbasic_stdio` port pulls in, looking for things that would break
on Xtensa GCC. Findings:

✅ **Zero ARM intrinsics** in any core or VM file. `__builtin_arm`,
`__ARM*`, ARM-specific inline asm — none. The HAL refactor cleaned
these out via `tools/check_hal_purity.sh`'s purity gate, but the
audit confirms it for the Xtensa concern specifically.

✅ **Zero real inline asm** in `MMBasic.c`, `Commands.c`,
`Functions.c`, `Operators.c`, `MATHS.c`, `Memory.c`, `Draw.c`,
`FileIO.c`, `Audio.c`, all `bc_*.c`, all `vm_sys_*.c`. The one
`grep` hit in `FileIO.c` was a false positive — the substring
`asm` inside a comment about `f_lseek`.

✅ **HAL contract headers (`hal/*.h`) are Pico-clean.** No
`pico/`, `hardware/`, or chip-specific includes leak into the
contracts.

✅ **`core/state/*.c` are Pico-clean.** State hoist (Phase 0.5)
held up.

⚠️ **8 core files include Pico SDK headers directly:**
- `MMBasic.c`, `mm_misc_shared.c`, `FileIO.c` → `pico/stdlib.h`
- `Functions.c`, `vm_sys_pin.c` → `hardware/adc.h`, `hardware/pwm.h` etc.
- `Commands.c` → `hardware/dma.h`, `hardware/structs/watchdog.h`
- `MATHS.c` → `pico/rand.h`
- `Audio.c` → `hardware/pwm.h`, `hardware/irq.h`, `hardware/regs/addressmap.h`
- `FileIO.c` → 11 `hardware/*` headers (the worst offender)

These are Xtensa blockers if compiled raw, **but** —

✅ **`ports/host_native/` already provides clean shim headers**
for every Pico SDK header that appears in core. The shims live in
`ports/host_native/pico/` and `ports/host_native/hardware/`,
expose the same APIs as no-op `static inline` stubs, and route
time-related calls through `host_time_us_64()`. The WASM port
already uses this pattern (its `port_config.h` does
`#include "../host_native/port_config.h"`, and the build line
adds `ports/host_native/` to `-I`).

### Phase B strategy (next session)

Reuse the host_native shim approach — same as WASM. Concretely:

1. **`ports/esp32_s3_metro/port_config.h`**: include
   `../host_native/port_config.h`, override `HAL_PORT_HEAP_MEMORY_SIZE`
   to ~1.5 MB (PSRAM-backed).
2. **`main/esp32_platform.h`** (force-included): set
   `MMBASIC_HOST=1`, `MMBASIC_ESP32=1`, stub Pico SDK section
   attributes (`__not_in_flash_func`, etc.) — adapt from
   `ports/host_native/host_platform.h`.
3. **`main/CMakeLists.txt`**: add `../../host_native` and the repo
   root to `INCLUDE_DIRS` (shim header resolution + access to
   `MMBasic.h`, `configuration.h`, etc.). Add the core MMBasic +
   BC source list via `SRCS` with `../../../*.c` paths. Set
   `target_compile_definitions(... PUBLIC MMBASIC_HOST MMBASIC_ESP32
   FF_MAX_LFN_LARGE BC_SIM_RP2040)`.
4. **HAL stubs** in `main/hal_*_esp32.c`: empty bodies returning
   defaults for `hal_pin_*`, `hal_audio_*`, `hal_vm_framebuffer_*`.
5. **HAL real impls**: `hal_time_esp32.c` (route to
   `esp_timer_get_time()`), `hal_flash_esp32.c` (`esp_partition_*`),
   `hal_filesystem_esp32.c` (FATFS via VFS at `/sd`),
   `hal_storage_esp32.c` (NVS-backed Options).
6. **Replace `host_runtime.c` reuse** with an ESP32-specific
   `esp32_runtime.c` because POSIX/pthread assumptions in
   `host_runtime.c` won't fit. Use the IDF VFS APIs instead.
7. **`esp32_console.c`**: USB Serial/JTAG init dance per the plan
   (the 5 IDF calls — `usb_serial_jtag_driver_install`,
   `esp_vfs_usb_serial_jtag_use_driver`, etc.).
8. Iterate on `idf.py build` until link succeeds.

The audit shows this is a tractable amount of work — the heavy
structural lifting (HAL refactor) is already done. Phase B is
mostly platform glue, and the glue surface is well-defined by the
host_native pattern.

### Stopping point

Stopping autonomous work here. Phase A build is verified; Phase A
flash needs a physical BOOT+RESET tap (documented above). Phase B
strategy is documented and ready to execute in the next session.

When user resumes:
- 5 minutes: tap BOOT+RESET, flash + monitor Phase A, confirm
  blink + heartbeat.
- Then start Phase B step 1 (port_config.h + esp32_platform.h).
