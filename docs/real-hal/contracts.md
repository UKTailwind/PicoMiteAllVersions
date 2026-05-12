# Real HAL — Contract sketches

Per-HAL surface shapes. Full per-function contracts live in `hal/CONTRACT.md` (landed in Phase 0). These sketches define the *shape* — function set, ownership of error reporting, IRQ-safety expectations — so the reader can judge whether the architecture is plausible before the code lands. Detailed signatures, parameter encodings, and edge-case behaviour are deferred to the phase that defines each HAL, because the implementation reveals constraints the plan can't predict.

## Conventions across all HALs

- **Return code:** `int` where `0` = success, negative = errno-style error. Functions returning a value (e.g. `hal_time_us_64`) cannot fail and return the value directly.
- **Ownership:** caller owns all buffers; HAL impls do not allocate. Where a HAL needs scratch (e.g. block I/O bounce buffer), it allocates at init and reuses.
- **Threading:** HAL functions are *not* reentrant unless explicitly marked `_irq_safe` in the header. Callers from IRQ context use `_irq_safe` variants only.
- **Error reporting:** the HAL returns the error code; translating to a BASIC-visible message is the *caller's* job (typically `error_throw_ex(code, "...")` in the interp). HAL impls do not call `error()` directly — that would couple them to MMBasic's longjmp-based error machinery.
- **Init order:** every HAL has `hal_<name>_init()` called from `board_init.c` in a documented order (time → flash → pin → storage → display → keyboard → audio → net). HALs may not depend on uninitialised peers.

## `hal_time.h`
```
uint64_t hal_time_us_64(void);                  // monotonic since boot; never wraps in practice
void     hal_time_sleep_us(uint32_t us);        // blocks ≥ us; may yield to cooperative scheduler (web host)
uint32_t hal_time_ms_tick(void);                // free-running ms counter for timeouts
int      hal_time_rtc_get(struct hal_datetime *out);
int      hal_time_rtc_set(const struct hal_datetime *in);
```
Hard part: web-host's cooperative-yield hook. `hal_time_sleep_us` on WASM must call `emscripten_sleep` for the cache-warm-friendly `wasm_last_yield_us` deduplication that web-host Phase 4 depends on.

## `hal_flash.h`
```
int hal_flash_read_options(void *buf, size_t len);
int hal_flash_write_options(const void *buf, size_t len);
int hal_flash_unique_id(uint8_t out[8]);
int hal_flash_erase_program_area(void);
```
Hard part: host's RAM-backed flash buffer must zero-fill on init or `Option.PIN` reads as -1 (truthy) and trips the lockdown prompt. The HAL impl on host owns this; the contract requires "freshly initialised flash reads as all-zero, not 0xFF."

## `hal_pin.h`
```
int hal_pin_set_mode(uint pin, hal_pin_mode_t mode);
int hal_pin_write(uint pin, bool high);
int hal_pin_read(uint pin, bool *out);
int hal_pin_pwm_start(uint pin, uint freq_hz, uint duty_pct);
int hal_pin_pwm_stop(uint pin);
int hal_pin_adc_read(uint channel, uint16_t *out);
int hal_pin_irq_attach(uint pin, hal_pin_edge_t edge, hal_pin_irq_cb cb, void *ctx);
int hal_pin_apply_clock_speed(uint khz);        // RP2350 accepts up to 250 MHz; RP2040 returns -EINVAL > 200 MHz
```
Hard part: `hal_pin_irq_attach` callback runs in IRQ context — the cb signature must be marked `HAL_TIME_CRITICAL` and document that it cannot allocate, longjmp, or call non-`_irq_safe` HAL functions.

## `hal_storage.h` (block-level)
```
int    hal_storage_init(hal_storage_dev_t dev);
size_t hal_storage_block_size(hal_storage_dev_t dev);
size_t hal_storage_block_count(hal_storage_dev_t dev);
int    hal_storage_read(hal_storage_dev_t dev, uint32_t lba, uint32_t count, void *buf);
int    hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf);
int    hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count);
int    hal_storage_sync(hal_storage_dev_t dev);
```
Hard part: SD-card removal between operations. The HAL must surface "media changed" as a distinct error so `cmd_mount` can re-init.

## `hal_filesystem.h` (POSIX-style on top of storage)
```
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
Hard part: `chdir` shadows libc on host (renamed to `mmbasic_chdir`). Mount-point management for multi-drive ports (A: SD, B: flash). The function set is large because FILES/LOAD/SAVE/COPY/SEEK/OPEN all need it — but the surface is well-trodden POSIX, no novelty.

## `hal_keyboard.h`
```
int      hal_keyboard_service(void);            // pump hardware; called from main loop
int      hal_keyboard_get(uint16_t *out);       // non-blocking; 0 = no key, 1 = key returned
int      hal_keyboard_peek(uint16_t *out);      // non-destructive
uint32_t hal_keyboard_modifiers(void);          // shift/ctrl/alt/meta bitmap
int      hal_keyboard_set_layout(hal_kbd_layout_t layout);
int      hal_keyboard_paste(const char *utf8, size_t len);
```
Hard part: USB host keyboard runs on TinyUSB which expects to be polled at ≥1 kHz; PS/2 matrix scan runs from a timer IRQ. `hal_keyboard_service` is the single callsite the interpreter knows about; the driver decides what "service" means.

## `hal_audio.h`
```
int hal_audio_init(void);
int hal_audio_tone(uint channel, uint freq_hz, uint8_t vol_pct);
int hal_audio_sound(uint slot, uint freq_hz, uint8_t vol_pct, hal_sound_wave_t wave);  // SOUND command
int hal_audio_sample_push(const int16_t *samples, size_t frames);                       // streaming
int hal_audio_stop(uint channel_or_slot);
int hal_audio_set_master_volume(uint8_t vol_pct);
int hal_audio_pause(void);
int hal_audio_resume(void);
```
Hard part: web host's gesture-armed AudioContext (must resume on first input; requires the JS bridge — see web-host-plan Phase 3). VS1053 codec stream vs PWM tone share the channel namespace.

## `hal_display.h`

Two tiers, per the Tier-B inlining mechanism (see architecture.md):

**Tier A (extern, slow path):**
```
int  hal_display_init(void);
int  hal_display_set_mode(hal_display_mode_t mode);
void hal_display_get_dimensions(int *w, int *h);
int  hal_display_sync(void);                                              // FASTGFX SWAP
int  hal_display_vsync_wait(void);
int  hal_display_scroll(int dx, int dy);
int  hal_display_blit_rect(int x, int y, int w, int h, const void *pixels, hal_pixfmt_t fmt);
int  hal_display_layer_select(uint layer);
int  hal_display_layer_merge(uint dst_layer, uint src_layer, hal_blend_t blend);
int  hal_display_close(void);
```

**Tier B (inline, hot path) — body in per-port `hal_display_inlines.h`:**
```
static inline void     hal_display_put_pixel(int x, int y, uint32_t rgb);
static inline uint32_t hal_display_get_pixel(int x, int y);
static inline void     hal_display_fill_rect_fast(int x, int y, int w, int h, uint32_t rgb);
```

Hard part: `__not_in_flash_func` placement — Tier B inlines must not pull flash-resident helpers when called from FASTGFX swap. The conformance test exercises `put_pixel` from a tight loop and measures throughput; a regression here is a HAL design bug, not a driver bug.

Multi-mode displays (VGA SCREENMODE1/2/3, HDMI SCREENMODE4/5) are handled by the *driver*: `hal_display_set_mode` takes a mode enum, and the driver internally dispatches to the right pixel format. Core never branches on display mode.

## `hal_multicore.h`
```
int hal_multicore_init(void);
int hal_multicore_post(uint channel, uint32_t payload);                   // non-blocking
int hal_multicore_post_blocking(uint channel, uint32_t payload);
int hal_multicore_recv(uint channel, uint32_t *out);                      // non-blocking
int hal_multicore_recv_blocking(uint channel, uint32_t *out);
```
Channel IDs are project-wide and live in `hal/hal_multicore_channels.h`: `HAL_MC_CH_DISPLAY_SCROLL`, `HAL_MC_CH_DISPLAY_CLEAR`, `HAL_MC_CH_FASTGFX_SWAP`, etc. RP2040 single-core ports get a no-op impl (`post` runs the work synchronously); host gets a stub.

Hard part: this isn't a peripheral — it's a *protocol*. The channel-ID list is the actual contract. Adding a new cross-core message means adding a channel ID and documenting the payload format in `hal_multicore_channels.h`, not editing the HAL.

## `hal_net.h`
```
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
Hard part: WASM has no raw TCP — the WASM impl maps `hal_net_http_register` to `fetch()` callbacks and rejects raw TCP/UDP with `-ENOSYS`. The contract must allow this (functions can return "not supported on this port").

### Network lifecycle

The network HAL owns transport primitives only. BASIC-visible network lifecycle
policy is shared code, not a port/backend decision.

This is a single shared implementation requirement, not a parity-by-copying
requirement. Ports may expose backend primitives, capability bits, and minimal
adapter callbacks; they should not each reimplement option application,
listener preservation, runtime cleanup, or unsupported-feature policy.

- Durable network options are `Option.TCP_PORT`, `Option.UDP_PORT`,
  `Option.Telnet`, `Option.disabletftp`, `OPTION WIFI` credentials, and
  `OPTION WEB MESSAGES`. Port code may validate hardware-specific details, but
  it must not decide different BASIC semantics for these options.
- Once a backend reaches an IP-ready state, shared lifecycle code opens every
  configured and supported service: TCP server, UDP server, TFTP, and Telnet.
  Reconnect uses the same path as boot. Port open callbacks receive the
  already-selected configured port; they should bind that port and not
  rederive lifecycle policy from `Option.*`.
- Runtime polling uses the shared lifecycle poll entry. Ports supply narrow
  callbacks for backend event pumps such as UDP, TFTP, TCP client stream, MQTT,
  TCP server, and Telnet; the shared lifecycle layer owns the service poll
  order and network-ready gate.
- `RUN` and normal runtime cleanup close active sessions only: accepted TCP
  slots, TCP clients/streams, MQTT sessions, pending UDP/message state, TFTP
  transfer sessions, and active Telnet console connections. Configured
  listeners remain open or are reopened before control returns to BASIC.
- Setting a service option to disabled is the explicit way to close its
  configured listener. Network-down handling may also close listeners, but
  network-ready handling must reopen the durable configured services.
- `NEW` and hard runtime reset must call the shared lifecycle reset entry with
  an explicit cleanup level. Ports must not independently choose whether to
  preserve or tear down configured network listeners.
- Unsupported network features are selected through `hal_net_capabilities()`.
  Browser/WASM raw TCP server, TCP stream, UDP, TFTP, and Telnet paths should
  fail through the same shared capability checks used by other ports, rather
  than through hand-written parallel policy.
- If a backend truly cannot apply a network option without rebooting, it
  reports that as a shared lifecycle result. The common caller is responsible
  for the user-visible reset/reporting behavior.

## `hal_irq.h`

Macros and helpers, no functions:
```
#define HAL_TIME_CRITICAL                       /* port-supplied: __not_in_flash_func or empty */
#define HAL_IRQ_SAFE                            /* annotation for review/grep */
bool hal_in_irq_context(void);                  /* for assertions */
void hal_critical_section_enter(hal_critical_t *cs);
void hal_critical_section_exit(hal_critical_t *cs);
```

## Stub HAL impls for the pure-stdio port (Phase 12.5)

Every hardware-only HAL has a `hal_<name>_hard_error.c` stub in `ports/mmbasic_stdio/stubs/`:
```
int hal_pin_set_mode(uint pin, hal_pin_mode_t mode) { return -ENOSYS; }
int hal_audio_tone(uint c, uint f, uint8_t v)       { return -ENOSYS; }
... etc
```
These exist so the linker is satisfied and any BASIC program calling `PIN(0)` or `PLAY TONE` gets the documented MMBasic error. The pure-stdio port links these instead of any peripheral driver.

---

These sketches don't pin every signature — that happens in the phase that defines each HAL. They pin the *shape*: function count per HAL, who owns errors, where IRQ context lives, what's inline vs extern. If a sketch turns out to be wrong during Phase N, this doc is updated as part of that phase's commit.
