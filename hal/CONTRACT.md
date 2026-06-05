# HAL Contract

This document is the per-function contract for every Hardware Abstraction Layer (HAL) surface in `hal/*.h`. The plan that motivates these surfaces lives in [`docs/real-hal-plan.md`](../docs/real-hal-plan.md). This doc fills in the details the plan deferred.

**Status:** Phase 0 skeleton. Each HAL section starts as sketch-level and is finalised by the phase that defines that HAL. Sections marked *locked* cannot be loosened without an RFC amendment to this file.

## Global conventions (apply to every HAL)

1. **Return codes.** HAL functions that can fail return `int` with `0` = success, negative = errno-style error. Functions that cannot fail (e.g. `hal_time_us_64`) return the value directly.
2. **Ownership of buffers.** The caller owns every buffer passed into the HAL. HAL implementations do not allocate for the caller. Where a HAL needs internal scratch (bounce buffers, DMA alignment), it allocates once at `hal_<name>_init()` and reuses.
3. **Reentrancy.** HAL functions are **not** reentrant unless explicitly marked with the `_irq_safe` suffix. Calling a non-`_irq_safe` HAL function from an interrupt handler is undefined behaviour.
4. **Error reporting.** A HAL impl returns the error code and returns. It never calls MMBasic's `error()` (the longjmp-based error machinery) — that coupling is reserved to the caller. The *caller* (typically a `cmd_*`, `fun_*`, or VM syscall body) translates the error into the correct BASIC-visible message.
5. **Init order.** Every HAL has `hal_<name>_init()` called from `board_init.c` in this documented order: `time → flash → pin → storage → filesystem → display → keyboard → audio → multicore → net`. A HAL may not depend on a peer that is initialised later. Out-of-order init is a port bug.
6. **No hidden globals.** A HAL impl may own static state, but shared-with-core state (display dimensions, pin mode tables, Option block, audio voice slots) lives in `core/state/` — see `docs/real-hal-plan.md` § "Cross-cutting state — hoisted in Phase 0.5". A HAL impl reads these globals but does not *own* them.
7. **Performance.** Declarations in `hal/*.h` are extern by default (resolved at link time). Hot-path functions use the **Tier-B inline mechanism**: the HAL header ends with `#include "hal_<name>_inlines.h"` and each port directory supplies its own inline bodies. See Tier-B section below.

## Tier-B inline mechanism (locked in Phase 0)

Hot-path primitives — `hal_display_put_pixel`, `hal_display_get_pixel`, `hal_display_fill_rect_fast` — must inline across the HAL boundary or the pixel loop regresses. The mechanism:

- `hal/hal_display.h` declares the Tier-A (extern) API and ends with:
  ```c
  #include "hal_display_inlines.h"
  ```
- Each port directory provides `ports/<target>/hal_display_inlines.h` with `static inline` bodies.
- The port's CMakeLists.txt adds `ports/<target>/` to the include path *before* any driver directory. The first `hal_display_inlines.h` in the include order wins.
- For ports that inherit a shared impl (e.g. all RP2040 VGA ports using the same `vga_pio` driver), the inline header lives next to the driver: `drivers/vga_pio/hal_display_inlines.h`. The port's CMakeLists.txt adds that path.

If the inline mechanism costs >2% on `pico_blocks_tilemap` SWAP rate versus pre-refactor, fall back to per-port `.h`-included `.c` (header-only library style). This is re-measured in Phase 7a.

## `HAL_TIME_CRITICAL` and RAM-resident placement

`hal/hal_irq.h` defines:
```c
#ifdef HAL_PORT_PICO_SDK
#define HAL_TIME_CRITICAL __attribute__((section(".time_critical." __FILE__)))
#else
#define HAL_TIME_CRITICAL
#endif
```

Any function reachable from a PIO IRQ, DMA IRQ, timer IRQ, or the `bc_fastgfx_swap` critical path **must** carry `HAL_TIME_CRITICAL`. Omission is a contract violation, detected by `tools/check_ram_baseline.sh` which parses the linker map for each device target.

---

## `hal_time.h` — clock + sleep + RTC (Phase 2)

*Status: sketch; finalised in Phase 2.*

```c
uint64_t hal_time_us_64(void);
void     hal_time_sleep_us(uint32_t us);
uint32_t hal_time_ms_tick(void);
int      hal_time_rtc_get(struct hal_datetime *out);
int      hal_time_rtc_set(const struct hal_datetime *in);
```

**Guarantees to finalise in Phase 2:**
- `hal_time_us_64` is monotonic, never goes backward, wraps at 2^64 µs (≈584 500 years).
- `hal_time_sleep_us` blocks ≥ `us` microseconds. On cooperative ports (WASM), it must yield to the host scheduler at least once if `us > 0`. See `project_wasm_yield_hook` in MEMORY.md for the dedup contract with `host_runtime_check_timeout`.
- `hal_time_rtc_get`/`set` use a target-neutral `struct hal_datetime` (fields to be pinned in Phase 2). Callers convert to/from MMBasic `DATETIME$` strings.

**Hard part:** the WASM cooperative-yield hook. A naïve `hal_time_sleep_us(0)` that yields every call burns a cache miss per call. The contract: WASM `hal_time_sleep_us` dedups yields through the shared `wasm_last_yield_us` counter already established in web-host Phase 2.5 / Phase 4.

---

## `hal_random.h` — random source

*Status: active.*

```c
uint32_t hal_random_u32(void);
```

**Guarantees:**
- Returns a 32-bit random value suitable for BASIC `RND()` and non-cryptographic internal uses.
- Device ports should use hardware entropy when available.
- Host/test ports may use libc `rand()` so `RANDOMIZE` remains deterministic in tests.
- Core code must not call Pico SDK `get_rand_32()` / `get_rand_64()` directly.

---

## `hal_net.h` — network transport

*Status: skeleton; finalised by the network-core refactor.*

`hal_net.h` separates the BASIC-visible WEB command surface from platform
networking. Shared code parses BASIC syntax, owns interrupt/message state, and
handles long-string buffers. Backends provide WiFi association, DNS, sockets,
MQTT client operations, and event queues.

**Guarantees to finalise in the network-core refactor:**
- `hal_net_capabilities()` returns a bitmask describing the commands that can
  be honored on the current backend. Shared WEB command code must reject
  unsupported surfaces before performing partial work.
- `hal_net_poll()` is the interpreter-thread progress hook. It may be cheap on
  threaded backends, but polled backends such as Pico/CYW43 use it to advance
  lwIP and drain backend queues.
- Network callbacks/tasks must not call the MMBasic parser, evaluator,
  allocator, or `error()`. They copy packet/event data into backend-owned queues
  for the interpreter thread to consume.
- The stub backend advertises no capabilities, has no permanent network
  buffers, and returns `HAL_NET_UNSUPPORTED` for transport operations.
- **TCP server rebind contract.** `hal_net_tcp_server_open(port, …)` MUST
  succeed when called immediately after `hal_net_tcp_server_close` on the
  same port, even if peer connections from the previous listener are still
  draining (TIME_WAIT / lingering pcb state). The shared lifecycle drives
  back-to-back close/open whenever `OPTION TCP SERVER PORT` is reconfigured
  or restored; a backend that refuses rebind produces silent
  `ConnectionRefused` at the BASIC layer with no listener actually bound.
  - BSD socket backends: set `SO_REUSEADDR` via `setsockopt` before
    `bind()`. See `ports/esp32_s3_metro/main/hal_net_esp32.c`.
  - lwIP raw API backends: enable `LWIP_SO_REUSE`/`SO_REUSE` in `lwipopts.h`
    AND call `ip_set_option(pcb, SOF_REUSEADDR)` before `tcp_bind`. See
    `drivers/net_lwip_raw/hal_net_lwip.c`, `lwipopts.h`.
  - The header-level contract note lives at `hal/hal_net.h::hal_net_tcp_server_open`.

---

## `hal_flash.h` — persistent option block + device ID (Phase 1)

*Status: sketch; finalised in Phase 1.*

```c
int hal_flash_read_options(void *buf, size_t len);
int hal_flash_write_options(const void *buf, size_t len);
int hal_flash_unique_id(uint8_t out[8]);
int hal_flash_erase_program_area(void);
```

**Guarantees to finalise in Phase 1:**
- `hal_flash_read_options` reads `len` bytes of the persistent Option block. On a freshly initialised system, the returned buffer is **all zeros**, not 0xFF. This is different from physical flash default state; the HAL impl normalises it. Violation causes `Option.PIN` (int) to read as -1 (truthy) and trips the lockdown prompt — see MEMORY.md note.
- `hal_flash_write_options` commits `len` bytes atomically enough that a power loss mid-write leaves either the old or the new block, not a torn write.
- `hal_flash_unique_id` returns 8 bytes identifying the device. On host, a stable-per-install value (derived from a config file or `/etc/machine-id`).
- `hal_flash_erase_program_area` prepares the on-flash program area for a fresh tokenise+save cycle. On host, zeroes the RAM-backed buffer.

**Hard part:** host's RAM-backed flash must match device semantics exactly. The existing `host_options_snapshot` and zero-fill-on-init machinery stays; the HAL impl wraps it.

---

## `hal_pin.h` — GPIO + PWM + ADC + edge IRQ (Phase 3)

*Status: sketch; finalised in Phase 3.*

```c
int  hal_pin_set_mode(uint pin, hal_pin_mode_t mode);
int  hal_pin_write(uint pin, bool high);
int  hal_pin_read(uint pin, bool *out);
int  hal_pin_pwm_start(uint pin, uint freq_hz, uint duty_pct);
int  hal_pin_pwm_stop(uint pin);
int  hal_pin_adc_read(uint channel, uint16_t *out);
int  hal_pin_irq_attach(uint pin, hal_pin_edge_t edge, hal_pin_irq_cb cb, void *ctx);
int  hal_pin_apply_clock_speed(uint khz);
bool hal_pin_in_irq_context(void);
```

**Guarantees to finalise in Phase 3:**
- `hal_pin_apply_clock_speed`: RP2040 rejects > 200 MHz with `-EINVAL`, RP2350 accepts up to 250 MHz, host accepts any value as a no-op but records it for MM.INFO.
- `hal_pin_irq_attach` callback runs in IRQ context; signature is `void (*)(uint pin, hal_pin_edge_t edge, void *ctx)`; cb must be annotated `HAL_TIME_CRITICAL`; cb may not allocate, longjmp, call MMBasic `error()`, or call any non-`_irq_safe` HAL function.

**Hard part:** `PinDef[]` hoist (Phase 0.5) must land before this HAL lands; otherwise pin state ping-pongs between `External.c` and the HAL impl.

---

## `hal_storage.h` — block I/O (Phase 4)

*Status: sketch; finalised in Phase 4.*

```c
int    hal_storage_init(hal_storage_dev_t dev);
size_t hal_storage_block_size(hal_storage_dev_t dev);
size_t hal_storage_block_count(hal_storage_dev_t dev);
int    hal_storage_read(hal_storage_dev_t dev, uint32_t lba, uint32_t count, void *buf);
int    hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf);
int    hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count);
int    hal_storage_sync(hal_storage_dev_t dev);
```

**Hard part:** SD-card removal. Return a distinct `-EMEDIA` error so `cmd_mount` can re-init rather than treating it as a generic I/O failure.

---

## `hal_filesystem.h` — POSIX-style on top of storage (Phase 4)

*Status: sketch; finalised in Phase 4.*

```c
int     hal_fs_open(const char *path, int flags, int *fd);
int     hal_fs_close(int fd);
ssize_t hal_fs_read(int fd, void *buf, size_t n);
ssize_t hal_fs_write(int fd, const void *buf, size_t n);
off_t   hal_fs_seek(int fd, off_t off, int whence);
int     hal_fs_eof(int fd);
int     hal_fs_unlink(const char *path);
int     hal_fs_rename(const char *from, const char *to);
int     hal_fs_mkdir(const char *path);
int     hal_fs_rmdir(const char *path);
int     hal_fs_chdir(const char *path);
char   *hal_fs_getcwd(char *buf, size_t n);
int     hal_fs_dir_open(const char *path, hal_dir_t *out);
int     hal_fs_dir_next(hal_dir_t *dir, struct hal_dirent *out);
int     hal_fs_dir_close(hal_dir_t *dir);
int     hal_fs_stat(const char *path, struct hal_stat *out);
```

**Hard part:** on host, `chdir` shadows libc (fixed as `mmbasic_chdir` in host-hal). The HAL name `hal_fs_chdir` does not clash. Multi-drive ports (A: SD, B: flash) need mount-point management — deferred to a Phase 4 sub-section.

---

## `hal_keyboard.h` — pump backend into ConsoleRxBuf (Phase 5)

*Status: Phase 5 closed — service + clear_repeat_state + init + keydown + lock_state + set_layout all landed.*

```c
void     hal_keyboard_service(void);
void     hal_keyboard_clear_repeat_state(void);
void     hal_keyboard_init(void);
int      hal_keyboard_keydown_count(void);
int      hal_keyboard_keydown_slot(int slot);  /* slot=1..6 */
uint32_t hal_keyboard_lock_state(void);
int      hal_keyboard_set_layout(int layout);  /* HAL_KBD_LAYOUT_* */
```

The surface shrank during Phase 5 once the landscape was mapped: every keyboard backend (PS/2 matrix, USB host HID, generic I²C, PicoCalc I²C, host stdin) already feeds MMBasic's existing `ConsoleRxBuf` ring buffer. Core reads characters via `getConsole()` / `MMInkey()`. The HAL therefore does not return characters for the streaming path — it only pumps the hardware so the side-effect writes into the ring buffer can happen. `fun_keydown()`'s one-shot snapshot surface (KeyDown[] slot / lock-state bitmap) is exposed separately through `hal_keyboard_keydown_*` + `hal_keyboard_lock_state()`.

**Call sites in core (all migrated):**

- `MM_Misc.c::check_interrupt` — `hal_keyboard_service()` replaces `#ifndef USBKEYBOARD if(Option.KeyboardConfig) CheckKeyboard();`.
- `MMBasic.c::ClearExternalIO` / `Editor.c` x4 / `Commands.c` list-pager — `hal_keyboard_clear_repeat_state()` replaces `#ifdef USBKEYBOARD clearrepeat();`.
- `vm_sys_input.c::fun_keydown` — unified BASIC `KEYDOWN()` handler. Dispatches `n=0` to `hal_keyboard_keydown_count()`, `n=1..6` to `hal_keyboard_keydown_slot()`, `n=8` to `hal_keyboard_lock_state()`. Removes the prior USBKEYBOARD/PICOCALC/MMBASIC_HOST fan-out.
- `MM_Misc.c::OPTION KEYBOARD` layout ladder — picks a `HAL_KBD_LAYOUT_*` code then calls `hal_keyboard_set_layout()`. The USB backend rejects BE/BR/I2C; PS/2 accepts all. Repeat-timing parameter ranges still live inside an `#ifdef USBKEYBOARD` block because the semantics diverge (PS/2 packs a rate register; USB stores ms values).
- `PicoMite.c` — `hal_keyboard_init()` replaces both the PS/2 `initKeyboard()` call and the USB `hcd_port_reset` / `tuh_init` / HID reset block. Early-vs-late call siting is preserved via the existing `#ifdef USBKEYBOARD` guards, so the PS/2 path still initialises before banner-print and the USB path still initialises after. The per-backend ordering is inside `ports/pico_sdk_common/hal_keyboard_pico.c` now.

**Runtime dispatch inside the impl.** On non-USB builds `Option.KeyboardConfig` selects PS/2 vs I²C at run time; the HAL impl internally dispatches `CheckKeyboard()` vs `CheckI2CKeyboard()`. Core code does not know.

**IRQ-safety:** not IRQ-safe. TinyUSB's `tuh_task()` must run at thread priority; PS/2 and I²C backends call into I²C/PIO drivers that also expect thread context.

**Hard part:** USB host keyboard (TinyUSB) expects ≥1 kHz poll — the existing routinechecks timer provides that. The driver decides what "service" means (TinyUSB `tuh_task()`+`hid_app_task()` on USB ports, PS/2 scan poll on PICOMITEVGA/PicoMite, I²C read drive on PicoCalc, no-op on host).

**Residuals deferred to later phases:** `routinechecks`'s 1 kHz USB pump / I²C poll block in `PicoMite.c` still uses local `#ifdef USBKEYBOARD`; that's a board-file concern that migrates together with the display/touch service loop in Phase 7.

---

## `hal_audio.h` — audio command control + PCM stream sink (Phase 6)

*Status: Phase 6 landed — `shared/audio/Audio.c` owns BASIC `PLAY` dispatch across host, ESP32, pc386, stdio/ANSI, and RP targets. Runtime state lives in `shared/audio/audio_runtime.c`; PCM streaming lives behind `hal/hal_audio_stream.h`; RP transport code lives in `drivers/audio_rp2_pwm_i2s/`; VS1053 PLAY extensions live in `drivers/audio_vs1053/` and use the low-level codec driver in `drivers/vs1053/`.*

```c
void hal_audio_init(void);
void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, long long duration_ms);
void hal_audio_sound(int slot, const char *ch, const char *type,
                     double freq_hz, int volume);
void hal_audio_stop(void);
void hal_audio_volume(int left_pct, int right_pct);
void hal_audio_pause(void);
void hal_audio_resume(void);
```

The real signatures grew to match the actual BASIC semantics: independent L/R frequencies for TONE, 1..4 slot numbering with "L"/"R"/"B" channel / "S"|"Q"|"T"|"W"|"O"|"P"|"N" waveform strings for SOUND. Passing channel/waveform as `const char *` keeps the impl from having to re-parse and matches the host_sim_audio function set that the host impl already speaks.

Return type dropped to `void` — the earlier sketch returned `int` but neither backend has a meaningful error to surface at this level (arg validation happens in cmd_play before the HAL call; internal backend failures are diagnostic only).

**Call sites in core (migrated):**

- `shared/audio/Audio.c` — shared `cmd_play`, `CloseAudio`, and `StopAudio` route through the audio control HAL and optional PLAY extension hooks.
- `shared/audio/audio_stream.c` — WAV/MP3/FLAC/MOD decoding feeds the selected PCM stream sink.

**RP layout:**

- `drivers/audio_rp2_pwm_i2s/audio_rp2_pwm_i2s.c` owns PWM/SPI DAC/PIO-I2S transport, sample publication, conversion, and IRQ handling.
- `drivers/audio_rp2_pwm_i2s/audio_rp2_play.c` owns RP-only PLAY hooks such as ARRAY, MOD sample handling, playlist advance, and routing file playback to software decode or VS1053.
- `drivers/audio_vs1053/audio_vs1053.c` owns VS1053 MIDI/STREAM/HALT/CONTINUE/session PLAY extensions.

**Hard part:** WASM's gesture-armed AudioContext (web-host Phase 3). `hal_audio_resume` must be idempotent and callable from any input event path. Both host impls (host_sim_audio.c and host_wasm_audio.c) already honour this; the HAL forwards to them unchanged.

---

## `hal_display.h` — framebuffer + pixel + sync (Phase 7a)

*Status: sketch; Tier-A finalised in Phase 7a; Tier-B inline bodies finalised per-port.*

**Tier A (extern, slow path):**
```c
int  hal_display_init(void);
int  hal_display_set_mode(hal_display_mode_t mode);
void hal_display_get_dimensions(int *w, int *h);
int  hal_display_sync(void);
int  hal_display_vsync_wait(void);
int  hal_display_scroll(int dx, int dy);
int  hal_display_blit_rect(int x, int y, int w, int h, const void *pixels, hal_pixfmt_t fmt);
int  hal_display_layer_select(uint layer);
int  hal_display_layer_merge(uint dst_layer, uint src_layer, hal_blend_t blend);
int  hal_display_close(void);
```

**Tier B (per-port inline):**
```c
static inline void     hal_display_put_pixel(int x, int y, uint32_t rgb);
static inline uint32_t hal_display_get_pixel(int x, int y);
static inline void     hal_display_fill_rect_fast(int x, int y, int w, int h, uint32_t rgb);
```

**Hard part:** `__not_in_flash_func` placement — Tier-B inlines must not pull in flash-resident helpers when called from FASTGFX swap. The conformance test exercises `put_pixel` from a tight loop and measures throughput; a regression is a HAL design bug, not a driver bug.

**Mode dispatch:** SCREENMODE1/2/3 (VGA) and SCREENMODE4/5 (HDMI) are handled *inside* the driver. Core code calls `hal_display_set_mode(mode_enum)` and the driver dispatches internally. Local `#ifdef HDMI` inside `drivers/vga_pio/` or `drivers/hdmi/` is allowed by the "local-to-driver" exemption.

---

## `hal_multicore.h` — cross-core IPC (Phase 8)

*Status: sketch; finalised in Phase 8.*

```c
int hal_multicore_init(void);
int hal_multicore_post(uint channel, uint32_t payload);
int hal_multicore_post_blocking(uint channel, uint32_t payload);
int hal_multicore_recv(uint channel, uint32_t *out);
int hal_multicore_recv_blocking(uint channel, uint32_t *out);
```

Channel IDs in `hal/hal_multicore_channels.h` (Phase 8). The ID list is the contract; adding a cross-core message means adding a channel ID and documenting the payload format. No `hal_multicore_custom_*` escape hatches.

Single-core and host ports get a no-op impl that runs the "work" synchronously on post — so `HAL_MC_CH_DISPLAY_SCROLL` on a single-core port is literally just `scroll_impl(payload)`.

---

## `hal_net.h` — TCP + UDP + DNS + HTTP (Phase 9)

*Status: sketch; finalised in Phase 9.*

```c
int hal_net_init(void);
int hal_net_tcp_listen(uint16_t port, hal_tcp_handle_t *out);
int hal_net_tcp_accept(hal_tcp_handle_t listener, hal_tcp_handle_t *conn);
int hal_net_tcp_send(hal_tcp_handle_t conn, const void *buf, size_t n);
int hal_net_tcp_recv(hal_tcp_handle_t conn, void *buf, size_t n);
int hal_net_tcp_close(hal_tcp_handle_t conn);
int hal_net_udp_bind(uint16_t port, hal_udp_handle_t *out);
int hal_net_udp_send(hal_udp_handle_t sock, const struct hal_addr *to, const void *buf, size_t n);
int hal_net_udp_recv(hal_udp_handle_t sock, struct hal_addr *from, void *buf, size_t n);
int hal_net_dns_resolve(const char *host, struct hal_addr *out);
int hal_net_http_register(uint16_t port, hal_http_cb cb, void *ctx);
```

**Hard part:** WASM has no raw TCP. `hal_net_http_register` maps to `fetch()` on WASM; `hal_net_tcp_*` / `hal_net_udp_*` return `-ENOSYS`. The contract explicitly allows "not supported on this port" as a return.

---

## `hal_heap.h` — heap base + size + PSRAM (Phase 10)

*Status: sketch; finalised in Phase 10.*

```c
size_t hal_heap_size(void);
void  *hal_heap_base(void);
void  *hal_heap_psram_base(void);
size_t hal_heap_psram_size(void);
```

Values are supplied at link time. RP2040 reports `psram_size() == 0`; RP2350 with external RAM reports the configured size; host reports `HEAP_MEMORY_SIZE`.

---

## `hal_irq.h` — macros + helpers (Phase 7a; locks `HAL_TIME_CRITICAL`)

```c
#define HAL_TIME_CRITICAL  /* port-supplied */
#define HAL_IRQ_SAFE       /* annotation for reviewers and tooling */

bool hal_in_irq_context(void);

typedef struct hal_critical { /* port-supplied */ } hal_critical_t;
void hal_critical_section_enter(hal_critical_t *cs);
void hal_critical_section_exit(hal_critical_t *cs);
```

---

## Stub HAL impls for the pure-stdio port (Phase 12.5)

Every hardware-only HAL has a `hal_<name>_hard_error.c` stub in `ports/mmbasic_stdio/stubs/`:

```c
int hal_pin_set_mode(uint pin, hal_pin_mode_t mode) { return -ENOSYS; }
int hal_audio_tone(uint c, uint f, uint8_t v)       { return -ENOSYS; }
/* ... etc ... */
```

The callers translate `-ENOSYS` to the documented MMBasic error text.

---

## Amendments

Any change to a *locked* function signature, return code, or guarantee requires:
1. A note in the relevant section explaining the change and the motivation.
2. A follow-up pass through every port that implements the HAL to confirm compliance.
3. Bumping `HAL_CONTRACT_VERSION` in `hal/hal_version.h` (landed in Phase 0).

Pre-Phase-N sections are "sketch" and not amendment-protected — they land in final form *with* the phase that defines them.
