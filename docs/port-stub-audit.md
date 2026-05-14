# Port Completeness Audit — Stubbed Commands and Functionality

**Date:** 2026-05-14
**Scope:** Catalog every BASIC command, function, and HAL surface that is stubbed
out in each non-Pico port, and classify each stub as either *justified by platform
limitation* or *missing feature that needs work*.

The Pico ports (`pico_rp2350`, `dvi_wifi_rp2350`, `hdmi_rp2350`, `vga_rp2350`,
`vga_wifi_rp2350`, `web_rp2350`, and the rp2040 builds via `pico_sdk_common`) are
the **reference**: they link the canonical command bodies in `I2C.c`, `SPI.c`,
`External.c`, `MM_Misc.c`, `Audio.c`, etc. directly. The four ports audited here —
`host_native`, `host_wasm`, `esp32_s3_metro`, `pc386` — each replace some subset of
those bodies with stubs.

---

## 1. Stub Patterns

There is **no shared `error_unsupported()` helper**. Each port hand-writes its
own stubs, almost always in a single file:

| Port           | Stub file                                                      | Lines |
| -------------- | -------------------------------------------------------------- | ----- |
| `host_native`  | `ports/host_native/host_peripheral_stubs.c`                    | ~660  |
| `host_wasm`    | reuses `host_peripheral_stubs.c` + `host_web_stubs.c` overlay  | n/a   |
| `esp32_s3_metro` | `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c`         | ~660  |
| `pc386`        | `ports/pc386/pc386_peripheral_stubs.c`                         | ~620  |

The three peripheral-stub files are heavily redundant; the ESP32 file's header
explicitly notes it was forked from `host_peripheral_stubs.c`. ~60 of the
`cmd_*` / `fun_*` no-op stubs are character-for-character identical across all
three.

Four common stub postures appear:

1. **Silent no-op** — `void cmd_x(void) {}`. The BASIC program runs but the call
   does nothing. The dominant posture across all ports.
2. **Hard `error("...")`** — fails the BASIC program with a descriptive message.
   Used selectively: pc386 for `PWM` / `Servo` / `FRAMEBUFFER`; host_native for
   `COM:` open and `LoadPNG`; ESP32 for `MemLoadProgram`; ESP32's `vm_sys_pwm_*`.
3. **Route to virtual HAL** — `cmd_pwm`, `cmd_servo`, `cmd_setpin`, `cmd_pin`,
   `fun_pin`, `ExtCfg`, `ExtSet`, `ExtInp` are forwarded to `vm_sys_pin_*`.
   Whether that virtual HAL drives real hardware is per-port:
   - host_native/host_wasm: virtual-only (observable from tests, no electrical
     output).
   - ESP32: digital + ADC are real; PWM/servo raise `error(...)`.
   - pc386: digital only, via LPT1 GPIO.
4. **Kernel panic** — pc386 only, for `GetPeekAddr` / `GetPokeAddr` /
   `GetIntAddress`. PEEK/POKE without a meaningful address space halts the
   machine.

---

## 2. Host Native (`ports/host_native`)

A POSIX desktop simulator. Pin/peripheral hardware is virtual; the rest of the
runtime (FS, audio, net, display) is backed by real OS facilities.

### Stubbed commands (silent no-op)

`cmd_adc`, `cmd_backlight`, `cmd_camera`, `cmd_cfunction`, `cmd_Classic`,
`cmd_configure`, `cmd_cpu`, `cmd_csubinterrupt`, `cmd_device`, `cmd_DHT22`,
`cmd_ds18b20`, `cmd_endprogram`, `cmd_i2c`, `cmd_i2c2`, `cmd_in`, `cmd_ir`,
`cmd_irq`, `cmd_irqclear`, `cmd_irqnowait`, `cmd_irqset`, `cmd_irqwait`,
`cmd_jmp`, `cmd_keypad`, `cmd_label`, `cmd_lcd`, `cmd_library`, `cmd_mouse`,
`cmd_mov`, `cmd_nop`, `cmd_Nunchuck`, `cmd_onewire`, `cmd_out`, `cmd_pin`,
`cmd_pio`, `cmd_PIOline`, `cmd_poke`, `cmd_port`, `cmd_program`, `cmd_pull`,
`cmd_pulse`, `cmd_push`, `cmd_rtc`, `cmd_set`, `cmd_settick`, `cmd_sideset`,
`cmd_spi`, `cmd_spi2`, `cmd_sync`, `cmd_update`, `cmd_wait`, `cmd_watchdog`,
`cmd_wrap`, `cmd_wraptarget`, `cmd_WS2812`, `cmd_xmodem`, `cmd_guiBasic`.

### Stubbed functions

Silent: `fun_dev`, `fun_device`, `fun_distance`, `fun_ds18b20`, `fun_GPS`,
`fun_pio`, `fun_port`, `fun_pulsin`, `fun_spi`, `fun_spi2`, `fun_touch`.

Partial: `fun_info` (HRES/VRES/FONT*/FCOLOUR/BCOLOUR/FLASH ADDRESS only),
`fun_peek` (byte/word/short/integer/float only — no PIO/ADC bus subkeys).

### Routed through `vm_sys_*`

`cmd_pwm`, `cmd_Servo`, `cmd_setpin`, `fun_pin`, `ExtCfg`, `ExtSet`, `ExtInp`.
These succeed at the API level and update the virtual pin state, but nothing
reaches a wire.

### Hard errors

`SerialOpen` ("COM: not supported on host"), `LoadPNG`, unknown `OPTION`.

### Real implementations (NOT stubbed)

| Subsystem    | Implementation                                                |
| ------------ | ------------------------------------------------------------- |
| Filesystem   | `hal_filesystem_host.c`, `host_fs.c` (real POSIX + /sd MEMFS) |
| Audio        | `hal_audio_host.c` → `host_sim_audio.c` (sim-server WebSocket)|
| Net          | `hal_net_posix.c` (real BSD sockets: WEB, TCP, UDP, MQTT, NTP)|
| Flash        | `hal_flash_host.c`                                            |
| Keyboard     | `hal_keyboard_host.c`                                         |
| Display      | `host_fb.c` (320×320 framebuffer with full draw pipeline)     |
| Web server   | `host_web.c`, `host_sim_server.c`                             |
| Time/Random/Storage | `hal_time_host.c` / `hal_random_host.c` / `hal_storage_host.c` |

### Classification

| Stub area                                              | Verdict       | Rationale                                                                                                |
| ------------------------------------------------------ | ------------- | -------------------------------------------------------------------------------------------------------- |
| I2C, SPI, PIO, IR, OneWire, DHT22, DS18B20, WS2812, RTC, mouse, keypad, Nunchuck, Wii Classic, LCD, camera, backlight, ADC | **Justified** | No physical pins on a desktop host. Silent no-op is the correct posture so that BASIC programs that touch peripherals don't hard-fail under the sim. |
| `cmd_pin` is a no-op while `fun_pin` is real           | **Bug**       | Inconsistent: `fun_pin` routes through `vm_sys_pin_read`, but `cmd_pin = {}` drops writes. ESP32 and pc386 both implement `cmd_pin` against the same virtual HAL. Programs that do `PIN(GP3) = 1` silently lose the write. |
| `cmd_WS2812` no-op                                     | **Gap**       | Shared `cmd_ws2812_shared.c` exists and only needs a working `hal_ws2812_write`; host could provide a virtual-pixel-buffer backend for tests and the sim server. |
| AES, xregex stubs                                      | **Gap**       | These libraries are pure C with no hardware dependency. There is no reason for the host to lack them; the Pico ports build them. Vendor `tiny-AES-c` and `xregex` into host_native and host_wasm. |
| GPS, settick, IRQ commands                             | **Justified** | No peripherals; IRQs in particular have no analogue on a desktop process. |
| `cmd_xmodem`                                           | **Gap (small)** | Could work over a host UART or stdin, but value is low. Leave as is. |
| `fun_peek` minimal                                     | **Justified** | PEEK/POKE on a desktop is mostly meaningless. Returning 0 for unsupported bus subkeys is fine. |

---

## 3. Host WASM (`ports/host_wasm`)

A browser build via Emscripten. `ports/host_wasm/Makefile` reuses
`host_peripheral_stubs.c` verbatim, so every stub in §2 also applies here.

### Differences from host_native

- **Audio** is real — `host_wasm_audio.c` bridges to Web Audio API.
- **Display** is real — `host_wasm_canvas.c` pumps to an HTML canvas.
- **Filesystem** uses Emscripten MEMFS + IDBFS at `/sd`.
- **Network** is *flatly stubbed* via `host_web_stubs.c`: `cmd_web` calls
  `error("WEB not supported on this port")`; `closeMQTT`, `ProcessWeb`, etc.
  are silent no-ops. (host_native uses real BSD sockets in
  `hal_net_posix.c`.)
- `host_wasm_main.c` adds the JS-callable boot/control surface: `wasm_boot`,
  `wasm_break`, `wasm_set_heap_size`, `wasm_set_slowdown_us`,
  `wasm_configure_display_console`.

### Classification

| Stub area                              | Verdict       | Rationale                                                                                              |
| -------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------ |
| All peripheral commands (I2C/SPI/etc.) | **Justified** | Browser sandbox; no hardware access of any kind.                                                       |
| `cmd_web` / TCP / MQTT                 | **Partial**   | Browser sandbox blocks raw sockets, but WebSocket and `fetch` proxying through JS is feasible — and `wasm_proxy_*.c` files in host_native suggest the scaffolding exists. Worth investigating a browser-side `cmd_web` that proxies through the page's `fetch`/WebSocket. |
| `cmd_pin` no-op + `fun_pin` real       | **Bug**       | Same inconsistency as host_native (inherits the same file). |
| `cmd_WS2812`                           | **Gap**       | Could render to a virtual strip in the canvas for educational use. Low priority. |
| `cmd_xmodem`, IRQ commands             | **Justified** | No serial port; no preemptive IRQs in JS. |

---

## 4. ESP32-S3 Metro (`ports/esp32_s3_metro`)

Native ESP32-S3 port via ESP-IDF. WiFi/TCP/HTTP and digital GPIO + raw ADC are
real; analog output and bus peripherals are not yet wired.

### Stubbed commands (silent no-op)

Same shape as host_native, plus a few omissions:

`cmd_adc`, `cmd_backlight`, `cmd_camera`, `cmd_cfunction`, `cmd_Classic`,
`cmd_configure`, `cmd_csubinterrupt`, `cmd_device`, `cmd_DHT22`, `cmd_ds18b20`,
`cmd_endprogram`, `cmd_i2c`, `cmd_i2c2`, `cmd_in`, `cmd_ir`, `cmd_irq`,
`cmd_irqclear`, `cmd_irqnowait`, `cmd_irqset`, `cmd_irqwait`, `cmd_jmp`,
`cmd_keypad`, `cmd_label`, `cmd_lcd`, `cmd_library`, `cmd_mouse`, `cmd_mov`,
`cmd_nop`, `cmd_Nunchuck`, `cmd_onewire`, `cmd_out`, `cmd_pio`, `cmd_PIOline`,
`cmd_poke`, `cmd_port`, `cmd_program`, `cmd_pull`, `cmd_pulse`, `cmd_push`,
`cmd_rtc`, `cmd_set`, `cmd_settick`, `cmd_sideset`, `cmd_spi`, `cmd_spi2`,
`cmd_sync`, `cmd_update`, `cmd_wait`, `cmd_watchdog`, `cmd_wrap`,
`cmd_wraptarget`, `cmd_xmodem`.

### Real on ESP32 (NOT stubbed)

| Subsystem            | Implementation                                                        |
| -------------------- | --------------------------------------------------------------------- |
| WiFi + WEB/HTTP      | `esp32_wifi.c` (owns `cmd_web`, options, `MM.INFO`)                    |
| TCP server + client  | `esp32_tcp_server.c`, `esp32_tcp_client.c`                            |
| UDP / Telnet / MQTT / NTP / TFTP | `esp32_udp.c`, `esp32_telnet.c`, `esp32_mqtt.c`, `esp32_ntp.c`, `esp32_tftp.c` |
| Net HAL              | `hal_net_esp32.c`                                                     |
| Filesystem (LittleFS)| `hal_filesystem_esp32.c`                                              |
| Flash + PSRAM        | `hal_flash_esp32.c`, `hal_psram_esp32.c`                              |
| Digital GPIO + ADC   | `hal_pin_esp32.c` + `vm_sys_pin_esp32.c` (real, including raw ADC)    |
| WS2812               | `cmd_ws2812_shared.c` → `hal_ws2812_esp32.c` (real)                   |
| `cmd_pin` / `fun_pin`| Real — drives ESP32 GPIO via the HAL                                  |
| `cmd_setpin` modes OFF/DIN/DOUT/ARAW | Real — including PULLUP/PULLDOWN for DIN          |
| Web console display  | `hal_vm_framebuffer_esp32_stub.c` — name is misleading; this is a real 320×240 virtual framebuffer wired into the web-console transport. DrawPixel/DrawRectangle/DrawBitmap/ScrollLCD/DrawBuffer/ReadBuffer are all real. Only the FRAMEBUFFER N/F/L *commands* (the off-screen-layer model) are no-op. |
| `fun_info`           | Richer than host: CPU speed, free heap, PSRAM size, stack high-water, MAC ID, uptime, filesystem free space, EXISTS FILE/DIR/SIZE |

### Hard errors

- `vm_sys_pwm_configure` / `vm_sys_pwm_sync` / `vm_sys_servo_configure` →
  `error("PWM not supported on this port yet")`. The header comment in
  `vm_sys_pin_esp32.c:6` explicitly states "PWM/servo remain explicit
  unsupported features until LEDC is wired."
- `SerialOpen` → "COM: not supported on host" (inherited).
- `LoadPNG` → error.
- `MemLoadProgram` → "RAM FILE LOAD not supported on this port".

### Stubbed HAL files

| File                                      | Verdict                                                                                       |
| ----------------------------------------- | --------------------------------------------------------------------------------------------- |
| `hal_audio_esp32_stub.c`                  | **Real gap.** All `hal_audio_*` entries no-op. PLAY/SOUND/TONE silently do nothing. I2S DAC is the planned backend. |
| `hal_pin_esp32_stub.c`, `hal_flash_esp32_stub.c`, `hal_keyboard_esp32_stub.c`, `hal_storage_esp32_stub.c` | Co-exist with their real counterparts. Likely linked for unused entry points; needs cleanup audit. |

### Classification

| Stub area              | Verdict      | Rationale / Recommended action                                                                                                                                                          |
| ---------------------- | ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **PWM**                | **Gap**      | ESP32-S3 has the LEDC peripheral specifically for PWM. Wire LEDC into `vm_sys_pwm_configure` / `_sync` / `_off`. The framework, the syscall surface, and the BASIC parser are all already in place. |
| **Servo**              | **Gap**      | Falls out of PWM — `vm_sys_servo_configure` is a thin wrapper over `vm_sys_pwm_configure(slice, 50Hz, ...)`. Free with the PWM work. |
| **Audio (PLAY/SOUND/TONE)** | **Gap** | The board has an I2S DAC. `hal_audio_esp32_stub.c` is a deliberate placeholder. Implement against ESP-IDF's `i2s_std` driver. host_native's audio architecture (sound queue + DMA-style pumping) is reusable. |
| **I2C / I2C2**         | **Gap**      | ESP32-S3 has two I2C controllers. The `cmd_i2c` / `cmd_i2c2` no-ops are placeholders. Wire ESP-IDF `i2c_master` driver into the canonical bodies in `I2C.c` (gate the existing implementation, or provide an `hal_i2c_*` backend). |
| **SPI / SPI2**         | **Gap**      | Same as I2C. ESP32-S3 has multiple SPI peripherals. |
| **OneWire**            | **Gap**      | Bit-banged over a single GPIO — works on any pin that has a real `hal_pin_write` / `hal_pin_read`, which ESP32 has. The Pico implementation is portable. |
| **DHT22 / DS18B20**    | **Gap**      | Layer over OneWire / direct GPIO timing. Free once OneWire works. |
| **WS2812**             | **Already done** | `cmd_WS2812` is *not* in the stubs file — it's in `cmd_ws2812_shared.c` and dispatches to `hal_ws2812_esp32.c`. (Initial audit had this wrong.) |
| **IR**                 | **Gap**      | ESP32-S3 has the RMT peripheral, which is ideal for IR remote-control encode/decode. Worth wiring. |
| **Keypad / Mouse / Nunchuck / Wii Classic** | **Mixed** | Keypad and matrix scanning need GPIO + maybe I2C — viable once I2C lands. Nunchuck and Classic Controller speak I2C — same gate. PS/2 mouse is bit-banged GPIO. All implementable; whether they're worth the effort depends on demand. |
| **PIO**                | **Justified** | ESP32 has no PIO state machines. (RMT and I2S DMA cover the use cases differently.) Stub is correct. |
| **`cmd_cpu`, `cmd_csubinterrupt`, `cmd_settick`, IRQ family** | **Gap** | ESP32 has interrupts, FreeRTOS tasks, and adjustable CPU frequency. These are wireable but require careful design. Lower priority. |
| **RTC**                | **Gap**      | ESP32 has hardware RTC plus NTP. Wire `cmd_rtc` to the IDF RTC + the existing `esp32_ntp.c`. |
| **Watchdog**           | **Gap**      | ESP-IDF has Task Watchdog. Simple to wire. |
| **`cmd_xmodem`**       | **Gap (small)** | XMODEM over the ESP32 UART is straightforward; useful for firmware/program transfer. Low priority. |
| **LCD / camera / backlight** | **Justified** (depends) | The Metro S3 board doesn't have an LCD or camera; the web console *is* the display. If a future port adds physical display hardware, this changes. |
| **GPS**                | **Justified** | No GPS hardware on the board. |
| **AES / xregex**       | **Gap**      | Pure-C libraries with no platform dependency. Should be linked in. |
| **PNG decode**         | **Gap (low priority)** | Useful for any port with a real display. |

The ESP32 port is the **largest source of fixable gaps** in this audit. The
real-HAL framework, the syscall layer, and the BASIC parser all already
support each of these subsystems on the Pico ports; they're just waiting for
their ESP32 HAL backend.

---

## 5. PC386 (`ports/pc386`)

Bare-metal x86 kernel booted via Limine, no OS underneath. Audio (SoundBlaster
16), VGA mode 13h, LPT1 GPIO, and FatFs on floppy/HDD are all real. Everything
embedded-microcontroller-specific is genuinely absent.

### Stubbed commands (silent no-op)

`cmd_adc`, `cmd_backlight`, `cmd_camera`, `cmd_cfunction`, `cmd_Classic`,
`cmd_configure`, `cmd_cpu`, `cmd_csubinterrupt`, `cmd_device`, `cmd_DHT22`,
`cmd_ds18b20`, `cmd_endprogram`, `cmd_i2c`, `cmd_i2c2`, `cmd_in`, `cmd_ir`,
`cmd_ireturn`, `cmd_irq`, `cmd_irqclear`, `cmd_irqnowait`, `cmd_irqset`,
`cmd_irqwait`, `cmd_jmp`, `cmd_keypad`, `cmd_label`, `cmd_lcd`, `cmd_library`,
`cmd_mouse`, `cmd_mov`, `cmd_nop`, `cmd_Nunchuck`, `cmd_onewire`, `cmd_out`,
`cmd_pio`, `cmd_PIOline`, `cmd_poke`, `cmd_port`, `cmd_program`, `cmd_pull`,
`cmd_pulse`, `cmd_push`, `cmd_rtc`, `cmd_set`, `cmd_settick`, `cmd_sideset`,
`cmd_spi`, `cmd_spi2`, `cmd_steppedstream`, `cmd_synth`, `cmd_temp`, `cmd_uart`,
`cmd_watchdog`, `cmd_web`, `cmd_wii`, `cmd_ws2812`, `cmd_WS2812`, `cmd_sync`,
`cmd_update`, `cmd_wait`, `cmd_wrap`, `cmd_wraptarget`, `cmd_xmodem`.

### Stubbed functions

All return 0 / empty string: `fun_adc`, `fun_classic`, `fun_cpuid`, `fun_device`,
`fun_DHT22`, `fun_ds18b20`, `fun_keypad`, `fun_mmcmdline`, `fun_porta`,
`fun_temp`, `fun_touch`, `fun_tpadlast`, `fun_wii`, `fun_ws2812`, `fun_dev`,
`fun_distance`, `fun_GPS`, **`fun_info`** (returns empty string — see below),
**`fun_peek`** (returns 0), `fun_pio`, `fun_port`, `fun_pulsin`, `fun_spi`,
`fun_spi2`.

### Hard errors (unique to pc386)

- `cmd_pwm` → "PWM not available on PC386"
- `cmd_Servo` → "Servo not available on PC386"
- `cmd_framebuffer` → "FRAMEBUFFER not available on mode 13h display"
- `cmd_setpin` with PULLUP/PULLDOWN → error (LPT1 has no internal pulls)

### Kernel panic posture

`GetIntAddress`, `GetPeekAddr`, `GetPokeAddr` call `pc386_panic("... not
supported")`. The whole machine halts on `PEEK`/`POKE`. This is the most
aggressive stub posture in any port and exists because pc386 has no safe
address-space abstraction to expose.

### Real on PC386 (NOT stubbed)

| Subsystem    | Implementation                                                            |
| ------------ | ------------------------------------------------------------------------- |
| Audio (SB16) | `hal_audio_pc386_sb16.c` — full SoundBlaster 16 driver (`cmd_sb16` here)  |
| Audio (PC speaker) | `hal_audio_pc386.c` fallback                                        |
| Display      | `hal_vm_framebuffer_pc386.c` + `drivers/vga_mode13h/` (VGA mode 13h)      |
| FASTGFX      | `cmd_fastgfx` real, double-buffered mode 13h pageflip                     |
| Filesystem   | `hal_filesystem_pc386.c` (real FatFs on floppy A: and HDD C:)             |
| Flash        | `hal_flash_pc386.c` + `pc386_flash.c`                                     |
| Pin (LPT1)   | `hal_pin_pc386.c` (real LPT1 GPIO via `inb`/`outb`)                       |
| Keyboard     | `hal_keyboard_pc386.c`                                                    |
| `cmd_sys`    | Real — installs `MMBASIC.ELF`, `LIMINE.CONF`, `LIMINE-BIOS.SYS` from A: to C: |
| `cmd_pin` / `fun_pin` / `cmd_setpin` / `ExtCfg` / `ExtSet` / `ExtInp` | Real over LPT1 |

### Classification

| Stub area                                                               | Verdict       | Rationale                                                                                                                                          |
| ----------------------------------------------------------------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| I2C, SPI, PIO, IR, OneWire, DHT22, DS18B20, WS2812, RTC, Nunchuck, Wii  | **Justified** | An IBM-PC clone has none of these peripherals. Silent no-op is correct.                                                                            |
| PWM, Servo (hard `error()`)                                             | **Justified** | No microcontroller PWM/timer on a 386. The hard error posture is *better* than silent no-op — programs fail loudly. **Consider applying the same posture in other ports** for clarity. |
| `cmd_framebuffer` hard error                                            | **Justified** | Mode 13h is a scanout buffer, not an off-screen layered model.                                                                                     |
| **`fun_info` returning empty string**                                   | **Bug**       | Other ports return HRES/VRES/FONT*/FCOLOUR/BCOLOUR at minimum. BASIC programs that query `MM.INFO("HRES")` on pc386 currently get garbage. Should return at least display geometry, font, color state, and `EXISTS FILE/DIR/SIZE` from the real FatFs. |
| **`fun_peek`** returning 0                                              | **Borderline**| PEEK with no defined address space is meaningless on this kernel, but BASIC code that does `PEEK(BYTE addr)` could plausibly target real-mode memory or device registers. Decide: silent zero (current) vs. hard error vs. real `inb`/memory map. |
| PNG decode silent no-op (vs. host/esp32 hard error)                     | **Inconsistency** | Make `LoadPNG` hard-error on pc386 too, for symmetry.                                                                                          |
| AES, xregex                                                             | **Gap (small)** | Pure C; no reason to lack them. Low priority on a bare-metal kernel where security/regex use is rare. |
| `cmd_temp`, `cmd_synth`, `cmd_steppedstream`, `cmd_uart`, `cmd_wii`, `cmd_ws2812` | **Stubs unique to pc386** | These names exist as RP2040/RP2350-specific extensions; pc386 must stub them because the tokens are present in the shared parser but the implementations aren't built. Justified. |
| `cmd_web` no-op (host/esp32 have real or stub-error)                    | **Defensible** | A 386 PC could in principle do TCP via NE2000 or 3c509 NICs, but the effort is large and the audience small. Justified as a feature gap, not a bug. If pursued, parallels the `cmd_web` design that already exists for host/esp32. |
| `pc386_panic` on `GetPeekAddr` / `GetPokeAddr` / `GetIntAddress`        | **Borderline harsh** | Panicking the kernel on a `PEEK` is dramatic. Consider downgrading to `error("...")` so the BASIC program fails but the kernel survives. |

---

## 6. Cross-Port Summary Matrix

Legend: **R** = real / wired to hardware. **S** = silent no-op stub. **E** =
hard `error(...)`. **V** = virtual-HAL only (no electrical effect). **—** = not
applicable.

| Subsystem                | rp2040/rp2350 | host_native | host_wasm | esp32 | pc386 |
| ------------------------ | :-----------: | :---------: | :-------: | :---: | :---: |
| GPIO (digital R/W)       | R             | V           | V         | R     | R (LPT1) |
| ADC                      | R             | S           | S         | R (raw) | S |
| PWM                      | R             | V           | V         | E     | E     |
| Servo                    | R             | V           | V         | E     | E     |
| I2C / I2C2               | R             | S           | S         | S     | S     |
| SPI / SPI2               | R             | S           | S         | S     | S     |
| PIO                      | R             | S           | S         | S     | S     |
| IR                       | R             | S           | S         | S     | S     |
| OneWire / DHT22 / DS18B20| R             | S           | S         | S     | S     |
| WS2812                   | R             | S           | S         | **R** | S     |
| RTC                      | R             | S           | S         | S     | S     |
| Watchdog                 | R             | S           | S         | S     | S     |
| Audio (PLAY/SOUND/TONE)  | R             | R (sim WS)  | R (Web Audio) | **S** | R (SB16) |
| Display / Draw           | R             | R (host_fb) | R (canvas)| R (web console FB) | R (mode 13h) |
| FASTGFX                  | R             | R           | R         | —     | R     |
| FRAMEBUFFER N/F/L        | R             | R           | R         | S     | E     |
| Filesystem               | R (LFS)       | R (POSIX)   | R (MEMFS) | R (LFS) | R (FatFs) |
| Flash storage            | R             | R           | R         | R     | R     |
| Serial / COM:            | R             | E on Open   | E         | E     | S     |
| WEB / HTTP / TCP / UDP / MQTT / NTP | R   | R (POSIX)   | E         | R (WiFi) | S |
| Telnet / TFTP            | R             | partial     | S         | R     | S     |
| `fun_info`               | R             | partial     | partial   | **richest** | **empty** |
| `fun_peek`               | R             | minimal     | minimal   | minimal | S |
| AES                      | R             | S           | S         | S     | S     |
| xregex                   | R             | S           | S         | S     | S     |
| PNG decode               | R             | E           | E         | E     | S     |
| Camera / LCD / Backlight | R (where wired) | S        | S         | S     | S     |
| Mouse / Nunchuck / Wii / Classic | R (where wired) | S | S | S | S |
| Keypad                   | R             | S           | S         | S     | S     |
| GPS                      | R (where wired) | S         | S         | S     | S     |
| IRQ / settick / cfunction | R            | S           | S         | S     | S     |

---

## 7. Recommended Work Items (by priority)

### High — clear gaps with platform support

1. **ESP32: wire PWM/Servo to LEDC.** The `vm_sys_pwm_*` syscalls are already
   wired; only the backend is missing. Comment in
   `ports/esp32_s3_metro/main/vm_sys_pin_esp32.c:6` already names this.
2. **ESP32: wire Audio (PLAY/SOUND/TONE) to I2S DAC.** Replace
   `hal_audio_esp32_stub.c` with a real I2S backend. Reuse the host_native
   audio queue design.
3. **ESP32: wire I2C/I2C2 to ESP-IDF `i2c_master`.** Once present, OneWire
   (bit-banged), DHT22, DS18B20, Nunchuck, Wii Classic, and any I2C displays
   become viable.
4. **ESP32: wire SPI/SPI2 to ESP-IDF SPI master.** Unlocks any SPI peripheral
   (LCD, SD over SPI, sensors).
5. **`cmd_pin` no-op bug on host_native / host_wasm.** `fun_pin` already
   routes through `vm_sys_pin_read`, so `cmd_pin` should symmetrically route
   through `vm_sys_pin_write`. Compare with the ESP32 and pc386 implementations
   — they're 5-line additions.
6. **PC386: `fun_info` returns empty string.** Implement at least HRES, VRES,
   FONT, FCOLOUR, BCOLOUR, EXISTS FILE/DIR, FILESIZE. Most of the data is
   already available from the existing pc386 HAL.

### Medium — useful additions

7. **AES + xregex for host_native, host_wasm, esp32, pc386.** Pure C, no
   hardware dep. Already built by the Pico ports.
8. **ESP32: RTC + watchdog + IR (RMT) + WS2812 RMT backend.** Hardware exists;
   these are HAL-only wins.
9. **PNG decode** on any port with a real display (ESP32 web console qualifies).
10. **Host WASM: investigate `cmd_web` via browser `fetch`/WebSocket bridge.**
    The `wasm_proxy_*.c` files in `host_native/` suggest scaffolding exists.

### Low / cleanup

11. **Factor a shared `error_unsupported()` helper** to deduplicate the three
    near-identical peripheral-stub files.
12. **Re-classify a few "stub" file names** — `hal_vm_framebuffer_esp32_stub.c`
    is misleading; it contains a real virtual framebuffer.
13. **PC386: downgrade `pc386_panic` on `GetPeekAddr` / `GetPokeAddr` /
    `GetIntAddress`** to `error("...")` so a stray `PEEK` doesn't crash the
    kernel.
14. **PC386: align PNG stub** with host/esp32 (hard error rather than silent).
15. **Audit `*_stub.c` co-existing files in `esp32_s3_metro/main/`** — several
    appear alongside their "real" counterparts; verify which are still linked
    and prune.

### Justified — leave alone

- All bus peripherals on `host_native` and `host_wasm` (no real pins).
- PWM/Servo/I2C/SPI/PIO/OneWire/WS2812 on `pc386` (no microcontroller
  peripherals on a 386).
- PIO on ESP32 (no PIO state machines — use RMT/I2S instead).
- GPS on every non-Pico port (no GPS module on those boards).
- Display/camera/backlight on ESP32 Metro S3 (no LCD on the board; web console
  is the display).

---

## 8. Source-of-Truth Files

- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/host_native/host_peripheral_stubs.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/host_native/host_web_stubs.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/host_wasm/host_wasm_main.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/host_wasm/Makefile` (confirms reuse of host stubs)
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/esp32_s3_metro/main/esp32_peripheral_stubs.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/esp32_s3_metro/main/hal_audio_esp32_stub.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/esp32_s3_metro/main/vm_sys_pin_esp32.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/esp32_s3_metro/main/hal_vm_framebuffer_esp32_stub.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/pc386/pc386_peripheral_stubs.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/ports/pc386/hal_audio_pc386_sb16.c`
- `/Users/joshv/picocalc/PicoMiteAllVersions/cmd_ws2812_shared.c` (shared `cmd_WS2812` body, dispatches to `hal_ws2812_*`)
