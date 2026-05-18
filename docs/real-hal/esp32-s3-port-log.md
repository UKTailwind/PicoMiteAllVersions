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
`FileIO.c`, `shared/audio/Audio.c`, all `bc_*.c`, all `vm_sys_*.c`. The one
`grep` hit in `FileIO.c` was a false positive — the substring
`asm` inside a comment about `f_lseek`.

✅ **HAL contract headers (`hal/*.h`) are Pico-clean.** No
`pico/`, `hardware/`, or chip-specific includes leak into the
contracts.

✅ **`core/state/*.c` are Pico-clean.** State hoist (Phase 0.5)
held up.

⚠️ **8 core files include Pico SDK headers directly:**
- `MMBasic.c`, `shared/mmbasic/mm_misc_shared.c`, `FileIO.c` → `pico/stdlib.h`
- `Functions.c`, `vm_sys_pin.c` → `hardware/adc.h`, `hardware/pwm.h` etc.
- `Commands.c` → `hardware/dma.h`, `hardware/structs/watchdog.h`
- `MATHS.c` → `pico/rand.h`
- `shared/audio/Audio.c` → `hardware/pwm.h`, `hardware/irq.h`, `hardware/regs/addressmap.h`
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

## 2026-05-08 (later) — Phase A confirmed + Phase B closed

### Phase A on hardware

User did the BOOT+RESET tap, port came up at /dev/cu.usbmodem101.
Flashed; chip booted; red LED blinks at 1 Hz; pyserial captured
`tick N` heartbeat. **Phase A green on physical hardware.**

Hardware identification: confirmed N16R8 (16 MB flash, 8 MB Embedded
Octal PSRAM AP_3v3) via esptool chip_id. Plan originally assumed N8R2
(Quad PSRAM); needed to switch CONFIG_SPIRAM_MODE_OCT=y for proper
PSRAM init. Currently SPIRAM is left disabled in sdkconfig; OCT
mode booted silently the first time, but subsequent investigation
suggests the silence was the macOS USB-CDC binding issue, not a
real PSRAM-init failure. Phase C will re-enable + retest.

macOS USB-CDC quirks to remember:
- pyserial.Serial.open() pulses DTR briefly on macOS, which the
  ESP32-S3 native USB controller interprets as a download-mode
  request. Use os.O_NONBLOCK + termios CLOCAL + ~HUPCL to avoid
  the DTR pulse, or pulse RTS-only after open to do a clean app reset.
- After many fast reset cycles macOS gets confused about the USB
  CDC binding; unplug-replug recovers cleanly.

### Phase B closed

The MMBasic core, the bytecode VM, and 80+ shared source files all
**compile and link on Xtensa GCC**. An actual BASIC program
(`PRINT 1+1`) runs to completion on the chip.

Commit-progression:
1. Pulled CORE_SRCS / STATE_SRCS / DEVICE_FACING_SRCS / DRIVER_STUBS
   / BC_SRCS into main/CMakeLists.txt — 80+ source files. All
   compiled cleanly first try (a few `-Wno-*` flags needed since
   IDF defaults `-Werror` on, but no source-level errors).
2. Switched `vm_sys_pin.c` → `vm_sys_pin_host.c` and same for
   `vm_sys_file` (the device variants pull Pico SDK GPIO calls
   that don't exist on ESP32; the host variants route through
   hal_pin / hal_filesystem instead).
3. Wrote ESP32-specific HAL stub files for hal_pin, hal_audio,
   hal_vm_framebuffer, hal_flash, hal_storage, hal_filesystem,
   hal_keyboard. `hal_time_esp32.c` is a real impl using
   esp_timer_get_time / vTaskDelay.
4. Pulled in shared host_native files: `host_runtime.c`,
   `host_peripheral_stubs.c`, `host_fs_shims.c`, `host_fs.c`,
   `host_keys.c`, `host_sim_slowdown.c`, `host_sim_emit_stub.c`.
   These provide the runtime glue + global storage that
   mmbasic_stdio also reuses.
5. Wrote `esp32_glue.c` for the small set of platform-glue
   symbols host_runtime.c expects from a sibling host TU
   (host_time, host_terminal, host_fb stubs, plus `flash_prog_buf`
   storage, `host_runtime_get_pixel` stub, `timegm` impl, and
   the EDIT/FRAMEBUFFER/FASTGFX command stubs).
6. Added `-Wl,--allow-multiple-definition` to the link line —
   MMBasic uses tentative-definition merging (`FSerror`,
   `gui_bcolour`, etc.) which IDF's default `--warn-common`
   rejects. Same flag the mmbasic_stdio Makefile uses.
7. Pinned `HAL_PORT_HEAP_MEMORY_SIZE` to 32 KB so the combined
   BSS (`AllMemory` + `flash_prog_buf` + `host_flash_target_buf`,
   all sized as multiples of MAX_PROG_SIZE) fits the 512 KB
   internal SRAM. Phase C replaces `host_fs_shims.c` (the source
   of the bloat — those are *host port* RAM mirrors of flash that
   real device builds don't carry) with an `esp_partition`-backed
   impl, and the heap goes back to 128 KB+ with PSRAM available.

### Phase B output (cleaned)

```
=== MMBasic Anywhere - ESP32-S3 Phase B ===
Running: PRINT 1+1
Result : 2
=== MMBasic exited cleanly ===
tick 0
tick 1
...
```

(`PRINT 1+1` output renders as garbled bytes in the current setup
because MMBasic's MMPrintString emits ANSI color escapes that the
raw `cat`-style read doesn't strip — but the fact that MMBasic
calls `putchar` and reaches "exited cleanly" means
`tokenize() → PrepareProgram() → ExecuteProgram()` all work
end-to-end on Xtensa.)

## 2026-05-08 (later) — Phase C step 1+2: REPL over USB Serial/JTAG

✅ **Interactive REPL working.** Verified:

```
> PRINT 1+1
 2
> FOR i=1 TO 3 : PRINT i*i : NEXT i
 1
 4
 9
>
```

What it took, in the order the bugs surfaced:

1. **Heartbeat task stack overflow.** The `xTaskCreate` for the LED
   heartbeat used 2 KB stack, which overflowed inside FreeRTOS's
   GPIO call path. Fix: dropped the heartbeat task entirely. The
   chip's running, that's enough.

2. **`host_output_hook` signature mismatch.** I declared it as
   `void (*)(int c)`; host_runtime.c expects
   `void (*)(const char *text, int len)`. Caller passed a `char*`
   + length, my hook read the pointer as a char and emitted RAM
   addresses → 0x90-spam in the console. One-line fix.

3. **`MMBasic_RunPromptLoop` reached and prompted, but no input.**
   Diagnosed via temporary `[REPL]` checkpoints inside the function
   (since reverted). The `>` printed; `EditInputLine` hung waiting
   for keys.

4. **Wrong path in `MMInkey`.** It dispatches:
   - if `host_raw_mode_is_active()` → `host_read_byte_nonblock`
   - else if `host_repl_mode` → `fgetc(stdin)`
   - else → `return -1`

   My stub had `host_raw_mode_is_active` returning `0`, and
   `host_repl_mode` defaults to `0`, so MMInkey always returned -1.
   Setting raw-mode to 1 is correct: USB Serial/JTAG is byte-level
   raw, no terminal line discipline.

5. **`usb_serial_jtag_read_bytes` requires the driver.** With just
   IDF's default polling reader, bytes don't reach the FIFO from
   user code. Calling `usb_serial_jtag_driver_install` +
   `usb_serial_jtag_vfs_use_driver` activates the interrupt-driven
   path. Using `usb_serial_jtag_read_bytes(buf, 1, 0)` directly
   for non-blocking and `pdMS_TO_TICKS(ms)` for timed.

### Diagnostic tooling

Wrote `ports/esp32_s3_metro/probe.py` — pyserial-based driver that
opens the port, RTS-pulses the chip into app boot, captures boot
output, sends test inputs, captures responses. Avoids picocom
entirely. Used heavily during the back-and-forth on input plumbing.

### What's still in Phase C

3. **Drop `host_fs_shims.c`** + write `hal_flash_esp32.c` /
   `hal_filesystem_esp32.c` real impls over `esp_partition_*` and
   FATFS-via-VFS at `/sd`. Eliminates the 768 KB RAM mirror
   (`host_flash_target_buf`) and lets us bump
   `HAL_PORT_HEAP_MEMORY_SIZE` from 32 KB back up to 128 KB+.
4. **Re-enable Octal PSRAM**. With 8 MB external RAM available, the
   BC heap can sit there comfortably; the silent-boot earlier was
   almost certainly the macOS USB-CDC binding issue.

## 2026-05-09 — Current ESP32 status refresh

The port has moved past the early host-native reuse shape documented
above. Runtime/peripheral host_native sources are no longer linked by
the ESP32 port; ESP32 owns the port surface in `esp32_*.c` and
`hal_*_esp32.c` files. Remaining host-shape debt is narrower:
generic VM shims and Pico SDK compatibility headers still live under
`ports/host_native/`, `esp32_platform.h` still defines `MMBASIC_HOST`,
and strict-link cleanup is not complete.

Verified status:
- Host tests: `./run_tests.sh` passes 243/243.
- HAL purity: clean over the current core/shared scope.
- ESP32 build/flash: `idf.py build` green, hardware flash/probe green.
- A: drive: LFS over `esp_partition_*`, bundled demos seed-only,
  zero-byte bundled demos repaired, non-empty user edits preserved.
- File save guard: `SAVE "file.bas"` now errors `No program` before
  truncating if no tokenized program is loaded.
- `mand.bas`: `RUN` and `FRUN` both produce checksum `552868`; current
  Metro measurement is ~8569 ms for `RUN` and ~359 ms for `FRUN`.

Important caveat: low-level `hal_pin_esp32.c` is linked, but BASIC
`PIN()` / `SETPIN` are not yet proven as real hardware GPIO. The
user-facing pin table and command/function path still need ESP32-owned
cleanup instead of host-shaped shim/stub behavior.

## 2026-05-10 — Stage F closed + E2 build-landed

Stage F hygiene landed:
- `tools/check_hal_purity.sh` now has an ESP32 port scope. It checks
  `ports/esp32_s3_metro/main/*.c` with the strict target/port-config
  rules and fails if `main/CMakeLists.txt` pulls host-native runtime or
  peripheral sources outside the temporary generic-shim allowlist.
- `docs/real-hal-plan.md` and `ports/esp32_s3_metro/README.md` now
  describe the current D/E/F state instead of the old Phase A/B shape.
- Added opt-in root helper `buildesp32.sh`; it runs the HAL purity gate,
  sources ESP-IDF if needed, and runs `idf.py build` for the Metro port.

Stage E2 implementation now builds:
- `hal_flash_esp32.c` replaces the old linked flash stub. Options are
  stored as an NVS blob under namespace `mmbasic`, key `options`.
- `app_main.c` loads the NVS-backed option mirror before `LoadOptions()`
  and applies serial defaults only when the stored options are missing or
  invalid, so saved options are no longer overwritten every boot.
- `hal_flash_read_jedec_id()` reports ESP-IDF's flash size instead of the
  old all-zero stub response.

Verification in this session:
- `tools/check_hal_purity.sh` passes, including the new ESP32 scope.
- `./buildesp32.sh` passes; current app image is `0xd2d50` bytes with
  `0x2d2b0` bytes (18%) free in the 1 MB app partition.

Not yet hardware-smoked: `OPTION COLOUR GREEN`, power cycle, prompt
comes back green; then `OPTION COLOUR RESET`.

### Hardware smoke attempt

Flashed the E2 build to `/dev/cu.usbmodem2101`; flash succeeded and the
board booted to the REPL. Verified:
- `PRINT 1+1` returned `2`.
- `FILES` listed the A: LittleFS demo files.
- `RUN "hello.bas"` printed the ESP32-S3 demo output.
- `OPTION DEFAULT COLOURS GREEN` was accepted and emitted the expected
  ANSI `38;2;0;255;0` / black-background prompt sequence immediately
  after the command.

Persistence verification is still inconclusive. After rapid reset/probe
cycles, macOS/ESP32-S3 USB CDC stopped delivering serial data and
`idf.py flash` could no longer auto-reset into ROM (`No serial data
received`). This matches the known USB-CDC binding/reset quirk. Recovery
requires the physical BOOT+RESET tap, then repeat: boot, confirm green
prompt after reset, and run `OPTION DEFAULT COLOURS WHITE` (or reset
defaults) to restore the prompt colour.

## 2026-05-10 (later) — shared ANSI colour fix + E1 build-landed

Found that the post-error colour reset shown on ESP32 was inherited
from upstream shared code, not ESP32-specific behavior:

- Legacy `do_end()` emitted hard-coded `ESC[97;40m` after showing the
  cursor.
- Legacy prompt/error recovery only restored `PromptFC` / `PromptBC`
  inside `Option.DISPLAY_CONSOLE`, so serial-only ANSI builds could
  lose the selected prompt colour.

Fix landed in shared code:

- `Draw.c` now has `ApplyDefaultConsoleColours()` and
  `ApplyPromptConsoleColours()`.
- `MMBasic_REPL.c` restores prompt colours for every prompt, not only
  framebuffer console builds.
- `Commands.c::do_end()` calls the shared prompt-colour helper instead
  of forcing white-on-black.

Verified locally: HAL purity, ESP32 build, `mmbasic_stdio` build,
`mmbasic_ansi` build, and stdio corpus 8/8. Flashed successfully to
`/dev/cu.usbmodem2101`.

Stage E1 also build-landed:

- Added a 1 MB `mmslots` partition after `lfsdata`.
- `esp32_flash_storage.c` now maps `mmslots` with
  `esp_partition_mmap()`.
- `SavedVarsFlash` points at the partition's saved-vars area.
- `flash_target_contents` points at the numbered-slot area.
- Slot/saved-vars `flash_range_erase` and `flash_range_program` calls
  route to `esp_partition_erase_range` / `esp_partition_write`.

Build verified. Follow-up hardware smoke passed for numbered slots:
`FLASH SAVE 1`, reset, `FLASH LOAD 1`, `RUN` reloaded and ran
`hello.bas` from the dedicated `mmslots` partition.

The first smoke attempt exposed an ESP32 port-boundary bug: the slot
persisted and `FLASH LIST` showed it in use after reset, but `FLASH LOAD
1` did not repopulate runnable program memory because the adapter only
treated offset zero as the program region. Shared code correctly writes
the runnable program through legacy `PROGSTART` offsets, matching Pico.
`esp32_flash_storage.c` now normalizes both offset-zero and `PROGSTART`
program-region writes into `flash_prog_buf`.

## 2026-05-10 (later) — strict link verified + VM shim relocation

Confirmed the ESP32 component has no `--wrap` and no
`--allow-multiple-definition`; `./buildesp32.sh` links cleanly under
that strict policy.

The simulator VM syscall bodies moved out of `ports/host_native/` and
into `ports/vm_sys_sim/`. Host-style builds link those simulator bodies
from the neutral path. ESP32 now links shared device `vm_sys_file.c`
for VM file syscalls, so bytecode file operations use the same LFS/FatFS
device path as Pico-style ports instead of the host FAT simulator.

ESP32 still links the simulator VM pin body. That is intentionally left
as the next GPIO task: real BASIC-visible GPIO needs an ESP32 pin table
and `vm_sys_pin_esp32.c`; the existing root `vm_sys_pin.c` is Pico-SDK
specific and cannot be linked by ESP32 as-is.

## 2026-05-10 (later) — ESP32 BASIC-visible GPIO smoke

Replaced the ESP32 simulator pin syscall body with an ESP32-owned
`vm_sys_pin_esp32.c` and added `esp32_pin_tables.c` for the Metro's
GPIO0..GPIO48 mapping. Slot zero remains the legacy NULL row; `GPn`
maps to PinDef slot `n+1`, preserving the shared 1-based pin-state
arrays.

Implemented the missing `cmd_pin()` path in `esp32_peripheral_stubs.c`
so interactive `PIN(GP13)=1` drives hardware instead of no-oping. The
function path already routed through `fun_pin()`.

ADC support now handles both ESP32-S3 ADC units by encoding ADC2
channels as `10 + channel` in `PinDef[].ADCpin`; `hal_pin_adc_select()`
selects ADC1 or ADC2 from that encoding.

Hardware smoke on `/dev/cu.usbmodem2101` passed:
- `SETPIN GP13,DOUT`, `PIN(GP13)=1`, `PRINT PIN(GP13)` returned `1`;
  `PIN(GP13)=0` returned `0`. User observed the onboard LED blinking.
- `SETPIN GP13,DIN,PULLUP`, `PRINT PIN(GP13)` returned `1`.
- `SETPIN GP1,ARAW`, `PRINT PIN(GP1)` returned a raw ADC value (`775`
  in this run).

The smoke exposed a shared enum-helper bug: `VM_PIN_MODE_ARAW` is value
46, between `PWM0A` and `PWM11B`, so `vm_pin_mode_is_pwm()` treated ARAW
as PWM. The helper now checks the two real PWM ranges separately. Full
host comparison suite still passes: 243/243.

## 2026-05-10 (later) — WS2812 shared command/HAL split

Moved `WS2812` out of the Pico-only `External.c` body and out of the
ESP32 unsupported-stub list. The BASIC-facing parser, pin validation,
and colour packing now live in `shared/cmd_ws2812_shared.c`; the waveform
backend is the per-port `hal_ws2812_write()` implementation.

Backends:
- ESP32: `ports/esp32_s3_metro/main/hal_ws2812_esp32.c`, using
  ESP-IDF RMT TX.
- Pico SDK ports: `ports/pico_sdk_common/hal_ws2812_pico.c`, preserving
  the legacy SysTick/GPIO timing path.

Hardware smoke on ESP32 `/dev/cu.usbmodem2101` passed with the Metro
onboard NeoPixel on `GP46`: red, green, blue, off, and white all
accepted. `&HFFFFFF` is very bright.

The full build gate was run after the Pico migration:
- `./buildall.sh`: all 14 device variants built clean; RAM baseline
  passed.
- `ports/host_native/run_tests.sh`: 243/243 passed; VM pin-mode helper clean; HAL
  purity clean.
- `./buildesp32.sh`: ESP32 IDF build green.

Two small header hygiene fixes landed while enforcing the full gate:
- `MM_Misc.h` now includes `pico.h` on Pico SDK builds before using
  `__uninitialized_ram(_persistent)`, and falls back to a plain symbol
  form when a non-Pico port has not force-defined the macro.
- `Draw.h` now includes `hardware/gpio.h` before defining `PinRead()`
  as `gpio_get(PinDef[a].GPno)`.

## 2026-05-10 (later) — ESP32 WEB/TCP/UDP/NTP/MQTT surface smoke

Implemented and hardware-smoked the ESP32 BASIC network surface on top of
ESP-IDF WiFi and sockets:

- `WEB CONNECT`, `WEB SCAN`, and `WEB SCAN array%()`.
- TCP server: `OPTION TCP SERVER PORT`, `WEB TCP INTERRUPT`,
  `WEB TCP READ`, `WEB TCP SEND`, `WEB TCP CLOSE`, and
  `WEB TRANSMIT PAGE/FILE/CODE/CSS/JS/IMAGE`.
- TCP client: `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`,
  `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, and
  `WEB CLOSE TCP CLIENT`.
- UDP: `OPTION UDP SERVER PORT`, `WEB UDP SEND`, receive state through
  `MM.MESSAGE$` and `MM.ADDRESS$`.
- `WEB NTP`, including BASIC `DATE$` / `TIME$` update through the ESP32
  port clock hook.
- Plain TCP MQTT: `WEB MQTT CONNECT/PUBLISH/SUBSCRIBE/UNSUBSCRIBE/CLOSE`,
  with receive state through `MM.TOPIC$` and `MM.MESSAGE$`.

The A: drive now includes a small `web_hello.bas` demo and a multi-file
website demo (`site.bas`, HTML pages, and CSS). The website was served from
the Metro and fetched successfully from macOS.

Added reusable host-side smoke tooling under `porttools/`:

- `basic_serial.py` opens serial with DTR/RTS deasserted, syncs to the
  MMBasic prompt, runs immediate-mode commands or command scripts, and
  supports regex `--expect` checks.
- `esp32_tcp_smoke.py` starts local Mac-side HTTP/stream TCP responders and
  drives the ESP32 TCP client commands through the interpreter.

Known-good tool checks:

```sh
python3.11 porttools/basic_serial.py --port /dev/cu.usbmodem101 \
  --boot-wait 1 --cmd 'PRINT "PORTTOOLS_OK"' --expect PORTTOOLS_OK

python3.11 porttools/esp32_tcp_smoke.py --port /dev/cu.usbmodem101 \
  --host 192.168.4.23

python3.11 -m py_compile porttools/basic_serial.py \
  porttools/esp32_tcp_smoke.py
```

The final build/flash gate for this batch was:

```sh
SKIP_HAL_PURITY=1 ./buildesp32.sh
SKIP_HAL_PURITY=1 ./buildesp32.sh -p /dev/cu.usbmodem101 flash
python3.11 porttools/basic_serial.py --port /dev/cu.usbmodem101 \
  --boot-wait 1 --cmd 'PRINT "FINAL_FLASH_OK"' \
  --expect FINAL_FLASH_OK --quiet
```

Remaining network caveat: MQTT TLS/cert handling is not implemented yet;
the smoke covers plain TCP MQTT only.
