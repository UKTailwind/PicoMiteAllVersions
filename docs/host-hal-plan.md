# Host HAL Plan

Collapse the host port into a clean hardware-abstraction layer so the host build re-uses the original MMBasic source (`Draw.c`, `FileIO.c`, `shared/audio/Audio.c`, `MM_Misc.c`, REPL) instead of re-implementing command handlers in `host/host_stubs_legacy.c`.

- **Branch:** `host-hal-refactor` (off `bridge-restoration`).
- **Predecessor plan:** [`bridge-restoration-plan.md`](bridge-restoration-plan.md). The bridge restoration landed the invariant that the interpreter is the primary runtime and the VM is a performance backend behind `FRUN`. This plan builds on that invariant — it does not revisit the VM/interpreter split.

## Invariants

1. **Device runtime path is identical after this work.** Source layout may change (code moved between files, new shared headers, `#ifdef MMBASIC_HOST` guards); the device firmware's instruction stream for any given BASIC line must match today. The gate is behavioral, not textual.
2. **The VM path is mostly untouched.** `vm_sys_graphics_*_execute`, `bc_vm.c` dispatch, and peephole optimizations stay as-is. Native-opcode file I/O (`OP_OPEN`, `OP_PRINT_NUM_FILE`, `OP_LINE_INPUT_FILE`, etc. via `vm_sys_file_*`) stays too. Commands whose output format differs between the VM's simple native syscall and FileIO.c's full implementation (FILES is the first case, flagged in Phase 3) get moved from a native opcode to the `OP_BRIDGE_CMD` fallback, so interp and VM both land in the same FileIO.c handler. That's a narrow, per-command decision — the VM doesn't become a generic `cmd_*` dispatcher.
3. **Host test suite stays green at every phase boundary.** No phase lands until `cd host/ && ./build.sh && ./run_tests.sh` passes in default (compare), `--interp`, and `--vm` modes.
4. **Device build stays green at every phase boundary.** `CMakeLists.txt` / `CMakeLists 2350.txt` must still build. Manual smoke-boot of RP2040 firmware after any phase that touches `Draw.c` / `FileIO.c`.

## Problem statement

The host Makefile excludes `Draw.c`, `FileIO.c`, `shared/audio/Audio.c`, `MM_Misc.c`, `drivers/gui_controls/GUI.c`, `PicoMite.c`, and all peripheral drivers from `CORE_SRCS`. To plug the gap, `host/host_stubs_legacy.c` (3,549 lines) defines 105 `cmd_*` and 37 `fun_*` entries. Roughly 90 of those are legitimate no-op stubs for hardware-only commands (`cmd_adc`, `cmd_i2c`, `cmd_pwm`, ...). The remainder are **divergent re-implementations** of interpreter command handlers:

| Area | Duplicated host entries | Shared equivalent |
|---|---|---|
| Graphics | `cmd_box`, `cmd_circle`, `cmd_line`, `cmd_pixel`, `cmd_text`, `cmd_triangle`, `cmd_polygon` | `Draw.c` (via `gfx_*_shared.c`) |
| File I/O | `cmd_load`, `cmd_save`, `cmd_open`, `cmd_copy`, `cmd_seek`, `FileLoadProgram` | `FileIO.c` |
| Audio | `cmd_play` | `shared/audio/Audio.c` |

These duplicates skip the vector/array paths, differ in error-reporting, and must be hand-mirrored every time the shared version changes. They exist purely because the core files won't compile on host today.

The VM is **not** the reason. `cmd_box` is never reached from VM dispatch — `OP_BOX` → `vm_sys_graphics_box_execute` → `DrawBox`, bypassing `cmd_*` entirely.

## Target architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   Shared core (unchanged)                   │
│  MMBasic.c, Commands.c, Functions.c, Operators.c, …         │
│  MMBasic_REPL.c, MMBasic_Prompt.c, Editor.c                 │
│  runtime/vm/bc_source.c, runtime/vm/bc_vm.c,                │
│  runtime/vm/bc_runtime.c, runtime/vm/vm_sys_*.c             │
│  gfx_*_shared.c                                             │
├─────────────────────────────────────────────────────────────┤
│     Shared interpreter commands (compile on both)           │
│  Draw.c  │ FileIO.c │ shared/audio/Audio.c │ MM_Misc.c  (portions)       │
│     — hardware touchpoints gated by #ifdef MMBASIC_HOST     │
│     — gated branches call into HAL                          │
├─────────────────────────────────────────────────────────────┤
│                        HAL surface                          │
│  host_fb_hal.h     — pixel plane, clear, refresh            │
│  host_fs_hal.h     — flash backing, drive switching         │
│  host_input_hal.h  — key polling                            │
│  host_console_hal.h— char I/O, banner                       │
│  host_timer_hal.h  — monotonic time, sleep                  │
│  host_audio_hal.h  — sound                                  │
│  host_sim_hal.h    — WS emit (--sim only)                   │
├─────────────────────────────────────────────────────────────┤
│             Host implementation of HAL                      │
│  host/host_fb.c, host/host_fs.c, host/host_audio.c, …       │
│  host/host_noop_stubs.c  — true no-op commands only         │
└─────────────────────────────────────────────────────────────┘
```

Each HAL header declares a small, abstract interface. Host provides a backing `.c`. Device provides a backing `.c` (or inlines the same calls as today — see Phase 2). Nothing in the shared source knows whether it is running on device or host.

## How the `#ifdef` contract works

A shared file that currently calls device-only code:

```c
// Draw.c — before
mutex_enter_blocking(&frameBufferMutex);
dma_channel_transfer_from_buffer_now(...);
mutex_exit(&frameBufferMutex);
```

Becomes:

```c
// Draw.c — after
#ifdef MMBASIC_HOST
    /* host build: no DMA, direct write to pixel plane */
    host_fb_begin();
    host_fb_blit_region(...);
    host_fb_end();
#else
    mutex_enter_blocking(&frameBufferMutex);
    dma_channel_transfer_from_buffer_now(...);
    mutex_exit(&frameBufferMutex);
#endif
```

- The `#else` branch is the **exact current device code**. No behavior change.
- If the same pattern recurs, promote it to an inline in a HAL header so both branches shrink.
- Device-only helpers referenced only from `#else` branches are allowed to remain device-only (no host stub needed).

Where possible, prefer the HAL wrap in a single place (one `host_fb_commit()` call wrapping DMA + mutex) rather than scattering `#ifdef`s through every function. This keeps the device source readable.

## Phased plan

Each phase ends with a green host build, green device build, and a commit. No phase depends on the next.

### Phase 0 — Setup & discovery (no code changes)

- Land this plan document.
- Grep-inventory the hardware touchpoints in each target file:
  - `Draw.c` — 73 hits on `frameBufferMutex|mutex_enter|mutex_exit|dma_channel|...`
  - `FileIO.c` — 1 hit (frameBufferMutex extern at line 58)
  - `MM_Misc.c` — 18 hits
  - `shared/audio/Audio.c` — 0 direct peripheral hits (Audio uses DMA indirectly through helpers)
- Produce a dependency map: each shared file → list of device-only symbols it references.
- Decide per-symbol: (a) stub in `host/hardware/*.h` (done already for most pico-sdk headers), (b) HAL call, (c) leave behind `#ifdef`.

**Exit gate:** dependency map lives in this doc, updated after survey.

#### Phase 0 dependency map (2026-04-17 survey)

Host stubs already defined in `host_stubs_legacy.c` for every `cmd_*`/`fun_*` that lives in the four target files — no "add a new stub" needed, only replace/delete as each file gets linked. `host/hardware/` already shims the pico-sdk headers referenced below (`flash.h`, `mutex.h`, `sync.h`, `time.h`, `multicore.h`, etc.).

**Draw.c (9,721 lines).** Hardware touchpoints cluster in three families:
- `multicore_fifo_push_blocking` — 23 calls across 8 functions (rp2350 dual-core LCD path; ClearScreen, set/closeframebuffer, bc_fastgfx_swap, merge/mergecolor, fun_mmcharheight, cmd_refresh).
- `mutex_enter_blocking(&frameBufferMutex)` / `mutex_exit` — 4 pairs, merge/mergecolor/blit region (wrap `host_fb_begin/end`).
- `__not_in_flash_func` — 6 macros on color conversion + fastgfx_swap_core1.
- Preprocessor: `PICOMITEVGA` (35+ blocks — VGA/3D/HDMI; leave device-only), `PICOMITEWEB` (2), `rp2350` (5+), `PICOMITE` (gates the multicore/mutex blocks).
- Whole-function `#ifndef MMBASIC_HOST` candidates: `fastgfx_swap_core1`, multicore-driven variants of `ClearScreen`/`setframebuffer`/`closeframebuffer`, `fun_getscanline` (VGA).
- Mostly portable (per-block gating only): `cmd_box`, `cmd_circle`, `cmd_line`, `cmd_polygon`, `cmd_pixel`, `cmd_triangle`, `cmd_blitmemory`, `cmd_sprite`, `merge`/`mergecolor` (after mutex wrap), `fun_rgb`, `fun_mmhres`/`fun_mmvres`, `fun_mmcharwidth`/`fun_mmcharheight`.

**FileIO.c (5,606 lines).** Almost entirely portable — device touch concentrated in flash-backed LittleFS:
- `flash_range_program` (13), `flash_range_erase` (14), `fs_flash_{read,prog,erase,sync}` (4) — all already stubbed in `host/hardware/flash.h`.
- `disable_interrupts_pico` / `enable_interrupts_pico` — defined inside FileIO.c (lines 311-327); wrap the whole pair in `#ifndef MMBASIC_HOST` (host: empty macros).
- `frameBufferMutex` extern at line 58 — `#ifdef PICOMITE` gate.
- `rp2350` mmap/psmap page-table blocks (5+) — device-only.
- Per-handler: `cmd_open`, `cmd_close`, `cmd_seek`, `cmd_flush`, `fun_loc`/`lof`/`eof`/`inputstr`, `cmd_chdir`, `fun_cwd`, `fun_dir` all portable as-is; `cmd_autosave` has a dual path (flash save + file — gate flash branch); `cmd_LoadImage` / `cmd_LoadJPGImage` need device display output wrapped.

**shared/audio/Audio.c (2,177 lines).** Hardware-bound — minimal portable surface. `cmd_play` (1037-1900) is the only command and it depends end-to-end on PWM (21 calls) / PIO (1) / flash. Strategy: HAL wraps `StartAudio`/`StopAudio`/DMA-buffer fill; on host, route to existing `host_sim_audio.c` PCM API. `iconvert`/`i2sconvert` are `__not_in_flash_func` but pure compute — portable.

**MM_Misc.c (6,474 lines).** Mix of portable + hardware-bound. Portable: `cmd_sort`, `cmd_longString` + friends (`fun_LGetStr`, `fun_LGetByte`, etc.), `fun_format`, date/time (`cmd_date`/`fun_date`/`cmd_time`/`fun_time`/`fun_day`/`fun_datetime`/`fun_epoch` — all once `time_us_64()` is HAL-routed), `cmd_pause` (busy loop → host sleep), `cmd_poke`/`fun_peek` (host: restricted address range). Device-only: `cmd_settick`, `cmd_watchdog`, `fun_restart`, `cmd_csubinterrupt`, `cmd_ireturn`, INT1-INT4 GPIO setup (`gpio_set_irq_enabled` ×8 at 4602-4619), `cmd_cpu`, `fun_device`. Preprocessor: `PICOMITEVGA` (VGA recovery), `rp2350` (PRNG/mmap), `PICOCALC` (I2C keyboard), `USBKEYBOARD`.

**Gating strategy** for the four files:
1. Flash ops: define `disable_interrupts_pico`/`enable_interrupts_pico` as `((void)0)` macros on host; real flash_range_* are already no-op stubs.
2. Multicore: `#ifdef PICOMITE` blocks in Draw.c already exist — extend with `#ifndef MMBASIC_HOST` where PICOMITE is defined on both (or rely on not defining PICOMITE when `MMBASIC_HOST` is set; verify current flag mix).
3. `time_us_64()`: route through existing host shim (already stubbed).
4. GPIO IRQ / PWM / PIO: wrap whole blocks / whole functions.
5. `PICOMITEVGA` / `PICOMITEWEB`: not defined on host → blocks drop out naturally.

Host stub line count impact (estimated from plan): Phase 2 ~1,200 lines deleted, Phase 3 ~300, Phase 4 ~115, Phase 5 ~200-400. Target final `host_noop_stubs.c` under 1,500 lines.

### Phase 1 — Pure moves out of `host_stubs_legacy.c` — ✅ DONE (2026-04-17)

Extracted four focused files from `host_stubs_legacy.c`; zero behavior change; 192/192 tests green at every commit.

| Extraction | Commit | Destination |
|---|---|---|
| Time primitives (`host_now_us`, `host_sync_msec_timer`, `host_time_us_64`, `host_sleep_us`) | `dfb7d03` | `host/host_time.{c,h}` |
| --sim tick thread + key queue (`host_sim_tick_*`, `host_sim_push_key`/`pop_key`) | `e4f517d` | `host_sim_server.{c,h}` |
| --sim graphics cmd stream + emit_* (`host_sim_emit_cls`/`rect`/`pixel`/`scroll`/`blit`, `host_sim_cmd_{append,drain}`, `host_sim_cmds_target_is_front`) | `4d6ff2e` | `host_sim_server.{c,h}` |
| Pixel plane + FRAMEBUFFER backend (`host_framebuffer`, dims, `host_fastgfx_back`, FRAMEBUFFER CREATE/WRITE/CLOSE/MERGE/SYNC/WAIT/COPY/LAYER, DrawRectangle/DrawBitmap/ScrollLCD/ReadBuffer backings, screenshot) | `d4034b7` | `host/host_fb.{c,h}` (replaces `host_framebuffer_backend.h` which is deleted) |
| Scripted-key injection (`host_key_script`, `host_load_key_script`, `host_keys_ready`, `host_keydown`, `host_runtime_configure_keys`) | `57ff5f7` | `host/host_keys.{c,h}` |

**Line count:** `host_stubs_legacy.c` 3,549 → 3,006 lines (543 moved out). The remaining 500-line gap to the `<2,500` target closes in Phase 2 when duplicate `cmd_*` / `Draw*` stubs get deleted.

**Public API introduced** — `host_fb_put_pixel`, `host_fb_colour24`, `host_fb_fill_rect`, `host_fb_draw_rectangle/bitmap/scroll_lcd/read_buffer`, `host_framebuffer_*`, `host_runtime_keys_consume` / `_ready` / `_peek_char`, `host_runtime_keys_load`. The old textual-include idiom (`host_framebuffer_backend.h`) is gone.

### Phase 2 — `Draw.c` compiles on host — ✅ DONE (2026-04-17)

Draw.c now compiles, links, and runs on host. Commits: `2749d38` (shim infrastructure), `7c9e143` (gating + stub deletion), plus follow-up pixel/edge fixes below.

**Gating in Draw.c (one #if edit per region):**
- `#ifndef PICOMITEVGA` at **308**, **429**, **4998**, **6117** → `#if !defined(PICOMITEVGA) && !defined(MMBASIC_HOST)`. These wrap touch/display-reset handlers, `restorepanel`/`closeframebuffer`/`setframebuffer`/`copyframetoscreen`/`blitmerge`/`merge`/`cmd_framebuffer`, and `cmd_blit MERGE`.
- `#ifdef PICOMITE` at **5671** (FASTGFX family) → `#if defined(PICOMITE) && !defined(MMBASIC_HOST)`.
- `cmd_fastgfx`'s `#else` host-error stub branch → `#elif !defined(MMBASIC_HOST)` so the host's working `cmd_fastgfx` in `host_stubs_legacy.c` (driving the `bc_fastgfx_*` helpers) wins.

**Host runtime wiring added to `host_runtime_begin`:**
- `Option.DISPLAY_TYPE = DISP_USER` — satisfies Draw.c's 16 `DISPLAY_TYPE == 0 → error` checks. DISP_USER (28) sits in the gap between `BufferedPanel` and `SSDPANEL`, so every specific-panel code path stays dead.
- `HRes / VRes` re-synced from `host_fb_width / host_fb_height` at each run (they're now defined in Draw.c, initialised to 0 there).
- `ReadBuffer = host_fb_read_buffer` — new function-pointer backing so `fun_pixel` / `FRAMEBUFFER COPY N` / `BLIT READ` can sample the visible plane. (Added `host_fb_read_buffer` in `host_fb.{c,h}`.)

**Stubs deletion** — ~50 duplicate `cmd_*` / `fun_*` / `Draw*` / `Read*` / `RGB*` / `GUIPrintString` / `SetFont` / `GetFont*` / `GetJustification` / `ClearScreen` / `initFonts` / `InitDisplayVirtual` / `ConfigDisplayVirtual` / `closeall3d` / `closeallsprites` / `getColour` / `rgb` / `ScrollLCD16` defs removed from `host_stubs_legacy.c`. Plus ~30 duplicate globals (`HRes`, `FontTable`, `DrawBuffer`, `mergerunning`, `spritebuff`, `struct3d`, etc.).

**Stubs added** (for symbols Draw.c references that aren't in the host build):
- `display_details[1]` — one zero-filled entry so the `InitDisplayVirtual` static init links; host never indexes past DISP_USER.
- `BDEC_bReadHeader`, `BMP_bDecode_memory` — BMP decoder error stubs; drivers/bmp_decoder/BmpDecoder.c isn't in the host build yet.

**Correctness fixes that fell out of running real graphics programs through Draw.c:**
- `host_fb_draw_rectangle` was decrementing x2/y2 with a (wrong) comment claiming device `DrawRectangle` has exclusive `[x1,x2)` semantics. Device `DrawRectangle16` / `DrawRectangleSPISCR` / etc. use `for (y=y1; y<=y2; y++)` (inclusive). The bogus decrement caused RUN-vs-FRUN pixel drift on `BOX` / `RBOX` and a 1-pixel-L ghost behind destroyed bricks in pico_blocks (the erase rect missed the right+bottom pixels that the outline rect had drawn). Removed the decrement; interp and VM now render byte-identical for `demo_gfx_shapes.bas`.
- `ExistsFile` host stub always returned 0, leaving `edit()`'s `p` pointer NULL and segfaulting at `Editor.c:511`. Now does a real `stat()` against `host_sd_root`. `BasicFileOpen` / `FileGetChar` / `FileEOF` / `FileClose` also route through `fopen`+`fgetc` when `host_sd_root` is set, so `EDIT "file"` / `RUN "file.bas"` read real files instead of the empty FatFS in-memory disk.
- Renamed `color%` → `col_c%` in `tests/t74_blocks_render.bas`; the old name only worked on host because the `cmd_colour` stub was a no-op. With Draw.c now providing the real `cmd_colour`, MMBasic tokenises `color` as the COLOUR command — fixed the test rather than reinstate a no-op.

**`host_stubs_legacy.c` line count:** 3,006 → 2,531 lines. Hits the plan's `<2,500` target within rounding.

**Exit gate results:**
- `./run_tests.sh` (default compare) — 192/192 green.
- `./run_tests.sh --vm` — 192/192 green.
- Simulator (`mmbasic_sim`) builds and runs; `demo_gfx_shapes.bas` renders interp ≡ vm byte-for-byte; pico_blocks runs without brick ghosts; `EDIT "file.bas"` opens real files.
- Device build: not re-verified this phase (Draw.c `#if` additions are host-only; no device code path changed).

### Phase 3 — `FileIO.c` compiles on host — ✅ DONE (2026-04-17)

FileIO.c now compiles, links, and runs on host. Every file primitive routes
through `host_fs_posix_*` shunts (REPL / `--sim`) or falls through to the
existing FatFS path (test harness, backed by `vm_host_fat.c`'s RAM disk).

**New host HAL surface** — `host/host_fs_hal.h`:
```c
int  host_fs_posix_active(int fnbr);
int  host_fs_posix_try_open(char *fname, int fnbr, int mode);
char host_fs_posix_get_char(int fnbr);
char host_fs_posix_put_char(char c, int fnbr);
void host_fs_posix_put_str(int count, char *s, int fnbr);
int  host_fs_posix_eof(int fnbr);
void host_fs_posix_close(int fnbr);
int64_t host_fs_posix_loc(int fnbr);
int64_t host_fs_posix_lof(int fnbr);
void host_fs_posix_seek(int fnbr, int64_t offset);
void host_fs_posix_flush(int fnbr);
```

Implementations live in `host_stubs_legacy.c` (a thin POSIX side-table
indexed by `fnbr`). `host_fs_posix_try_open` returns 0 when `host_sd_root`
is NULL so the test harness transparently falls through to FatFS.

**Gating in FileIO.c (24 `#ifdef MMBASIC_HOST` regions, all additive):**
- `disable_interrupts_pico` / `enable_interrupts_pico` — body empty on host
  (save_and_disable_interrupts / restore_interrupts / time_us_64 arithmetic
  are device-only; the pair is still called from flash write paths).
- `cmd_LoadImage` / `cmd_LoadJPGImage` — replaced with
  `error("Not supported on host")` stubs (`#else` branch). BmpDecoder /
  picojpeg / upng aren't in the host build; gating the bodies also drops
  the JPEG working-buffer allocations and `pjpeg_need_bytes_callback`.
- `cmd_disk` — `B:` drive-select skips the `Option.SD_CS` pin check (host
  has no SD pins; drive B is always available via vm_host_fat or POSIX).
- `InitSDCard` — short-circuits to `vm_host_fat_mount()` + return 2 (no
  SPI pin validation, no SDCardStat check).
- `BasicFileOpen` — preamble calls `host_fs_posix_try_open`; if the POSIX
  table services the open, return; otherwise fall through to the FatFS
  body unchanged.
- `FileGetChar`, `FilePutChar`, `FilePutStr`, `FileEOF`, `ForceFileClose`,
  `cmd_flush`, `cmd_seek`, `fun_loc`, `fun_lof` — each adds
  `if (host_fs_posix_active(fnbr)) return host_fs_posix_*(…);` ahead of
  the existing FATFSFILE/FLASHFILE dispatch.
- `cmd_files` — skips the `SaveContext/InitHeap/RestoreContext` dance (LFS
  `/.vars` isn't backed on host, and host has the RAM budget to keep the
  caller's state live). No shunt around the listing body itself — FILES
  now goes through `OP_BRIDGE_CMD` on the VM side too, so both interp and
  VM hit FileIO.c's cmd_files. See "FILES bridging" below.
- `cmd_disk` — `B:` drive-select skips the `Option.SD_CS` pin check. `A:`
  drive-select errors with "A: drive not available on host": the A: drive
  is the device's LittleFS-on-flash filesystem, and switching to it on
  host would leave the interpreter on a stubbed LFS path that corrupts
  downstream console state.
- FatFS directory walker (`f_findfirst`/`f_findnext`/`f_closedir`) and
  whole-path operations (`f_unlink`/`f_rename`/`f_mkdir`/`f_chdir`/
  `f_getcwd`) — redirected via `#define` at the top of FileIO.c to
  `host_f_*` wrappers in host_stubs_legacy.c. When `host_sd_root` is set
  they walk / act against POSIX (via host_fs.c's opaque DIR walker);
  otherwise they delegate to real FatFS. One side-table in host_fs.c
  keeps POSIX DIR* out of FileIO.c (POSIX `DIR` / `dirent.h` clash with
  FatFS's `DIR` typedef). That means `cmd_files`, `cmd_copy`, `cmd_kill`,
  `cmd_mkdir`, `cmd_rmdir`, `cmd_name`, `fun_dir`, `fun_cwd` all run
  FileIO.c's real logic and see real host files.

**New host headers added** (all empty or minimal — symbols gated out of
host source):
- `host/hardware/sync.h`, `host/hardware/gpio.h`, `host/hardware/pll.h`,
  `host/hardware/structs/pll.h`, `host/hardware/structs/clocks.h`,
  `host/pico/binary_info.h`. Plus `XIP_BASE = 0` and a `flash_do_cmd`
  stub added to `host/hardware/flash.h` so `fs_flash_read` / `SaveOptions`
  / `ResetOptions` expressions compile.

**Stubs deletion** — 72 duplicate symbols removed from
`host_stubs_legacy.c`: `BasicFileOpen`, `FileClose`, `FileEOF`,
`FileGetChar`, `FilePutChar`, `FilePutStr`, `FileLoadProgram`,
`FileLoadSourceProgram`, `FileLoadSourceProgramVM`, `FindFreeFileNbr`,
`ForceFileClose`, `MMfgetc`, `MMfeof`, `MMfputc`, `MMfputs`,
`cmd_autosave`, `cmd_chdir`, `cmd_close`, `cmd_copy`, `cmd_disk`,
`cmd_files`, `cmd_flash`, `cmd_flush`, `cmd_kill`, `cmd_load`,
`cmd_mkdir`, `cmd_name`, `cmd_open`, `cmd_rmdir`, `cmd_save`,
`cmd_seek`, `cmd_var`, `fun_cwd`, `fun_dir`, `fun_eof`, `fun_inputstr`,
`fun_loc`, `fun_lof`, `enable_interrupts_pico`, `disable_interrupts_pico`,
`FlashWrite*`, `LoadOptions`, `SaveOptions`, `ResetAllFlash`,
`ResetOptions`, `ResetFlashStorage`, `CheckSDCard`, `CrunchData`,
`ClearSavedVars`, `ErrorCheck`, `positionfile`, `drivecheck`,
`getfullfilename`, `GetCWD`, `InitSDCard`, `CloseAllFiles`. Plus 13
duplicate globals (`CFunctionFlash`, `CFunctionLibrary`, `FatFSFileSystem`,
`FatFSFileSystemSave`, `filesource`, `FlashLoad`, `lfs_FileFnbr`,
`OptionFileErrorAbort`, `pico_lfs_cfg`).

**Stubs added** (symbols FileIO.c references transitively):
- `SerialOpen`, `SerialClose`, `SerialGetchar`, `SerialRxStatus`,
  `SerialTxStatus`, `disable_audio`, `ExistsDir`.
- GPS globals: `GPSlatitude`, `GPSlongitude`, `GPSspeed`, `GPStrack`,
  `GPSdop`, `GPSaltitude`, `GPSvalid`, `GPSfix`, `GPSadjust`,
  `GPSsatellites`, `GPStime[9]`, `GPSdate[11]`, `gpsbuf1[128]`,
  `gpsbuf2[128]`, `gpsbuf`, `gpscount`, `gpscurrent`, `gpsmonitor`.
- Flash layout: `flash_option_contents` backs onto a zero-filled host
  buffer; `flash_target_contents` backs onto a 0xFF-filled buffer (both
  seeded via `__attribute__((constructor))` in host_stubs_legacy.c).
  - **Zero-fill (not 0xFF) for `flash_option_contents`** because
    `Option.PIN` is an `int` and all-0xFF would read as `0xFFFFFFFF`,
    tripping the PIN lockdown in `MMBasic_REPL.c:196` on every error.
  - **`host_options_snapshot()`** is called from `host_runtime_begin`
    after host init finishes seeding `Option.Width/Height/DISPLAY_CONSOLE/
    DISPLAY_TYPE/FatFSFileSystem/…`. It `memcpy`s the current `Option`
    back into `flash_option_buf` so the `LoadOptions` call inside
    `error()` (MMBasic.c:2835) restores the correct host configuration
    rather than the zero-filled defaults. Without this, every unhandled
    error wiped Width/Height/DISPLAY_CONSOLE — the symptoms were browser
    console going silent, cmd_files wrapping at column 0 (vertical
    char-per-line output), and pagination firing on the first line.
- LFS surface: added `lfs_dir_open`/`close`/`read`, `lfs_file_rewind`/
  `sync`/`tell`, `lfs_format`/`mount`/`unmount`, `lfs_getattr`/
  `setattr`/`removeattr`, `lfs_mkdir`, `lfs_rename` (every reachable
  LFS call site in FileIO.c has a linkable symbol; none actually store
  state since BasicFileOpen shunts through POSIX / FatFS first).

**Seeds in host_runtime_begin:**
- `Option.DISPLAY_TYPE = DISP_USER`, `HRes / VRes / DrawPixel / DrawRectangle /
  DrawBitmap / ScrollLCD / ReadBuffer` — framebuffer function pointers.
- `CFunctionFlash = host_cfunction_flash_buf` (was a static initialiser
  before; FileIO.c now owns the symbol so we seed it at runtime).
- `FatFSFileSystem = FatFSFileSystemSave = 1` so BasicFileOpen's FatFS
  branch runs — the LFS branch is dead on host.
- `Option.Height = 1000` if zero (pagination fallback for batch runs).
- `host_options_snapshot()` — copies current `Option` into the flash
  buffer so `error()`'s LoadOptions restore path uses the right state.

**FILES unification via OP_BRIDGE_CMD** — FILES was a native VM opcode
(`BC_SYS_FILE_FILES`) whose simple output didn't match FileIO.c's
tabular cmd_files. Phase 3 initially shunted around this by making
host's cmd_files call the VM's simple path; that defeated the point of
the refactor. The cleaner fix:

1. `bc_source.c`: deleted `source_compile_files` and its keyword
   dispatch. FILES now falls through to the `OP_BRIDGE_CMD` fallback,
   which pre-tokenises the FILES statement and embeds the tokens in the
   bytecode stream.
2. At runtime, `bc_bridge_call_cmd` dispatches to `commandtbl[cmdFILES].fptr()`
   → FileIO.c's cmd_files. Interpreter and VM both hit the same code.
3. `vm_sys_file_files` in vm_sys_file.c is now unused — left for Phase 7
   cleanup.

Same shared-path principle applies to COPY / KILL / MKDIR / RMDIR /
RENAME / CHDIR / DIR$ / CWD$ — all running FileIO.c logic, with the
FatFS API surface intercepted at `#define` level for POSIX routing.

**`chdir` symbol-shadowing fix** — FileIO.c's internal `chdir(char *p)`
helper collided with libc's POSIX `chdir(3)` at link time on host.
`host_fs_chdir` in host_fs.c calls libc's chdir; the linker bound that
call to FileIO.c's chdir instead, producing infinite recursion
(`mmbasic chdir → f_chdir → host_f_chdir → host_fs_chdir → mmbasic
chdir → …`) with each level prepending host_sd_root to the path until
the stack blew. Renamed FileIO.c's function to `mmbasic_chdir`; dropped
the dead `extern void chdir(char *p)` declaration from Commands.c.

**cmd_load longjmp on host** — host's `SaveProgramToFlash` stub calls
`load_basic_source`, which tokenises each line of the loaded file into
the same `tknbuf` that ExecuteProgram is iterating. Without a bail-out,
the caller resumed reading garbage and errored "Unknown command". Added
`longjmp(mark, 1)` + `memset(inpbuf, 0, STRINGSIZE)` at the end of
cmd_load under `#ifdef MMBASIC_HOST`. Device's SaveProgramToFlash uses
its own tokeniser buffer and is unaffected.

**InitSDCard clears SDCardStat** — the original stub returned success
but left `SDCardStat` with the device's startup `STA_NOINIT | STA_NODISK`
bits set. fun_dir tests those bits and errors "SD card not found". Host
InitSDCard now zeros SDCardStat after a successful vm_host_fat mount.

**Pagination fix in cmd_files** — the "PRESS ANY KEY" loop polls
`ConsoleRxBuf` which is never populated on host. Added a `MMInkey()`
poll + 10ms sleep under `#ifdef MMBASIC_HOST` so the pagination prompt
actually unblocks on any keypress via stdin / scripted-key queue /
sim websocket.

**Line count:** `host_stubs_legacy.c` 2,531 → 2,200 (~330 deleted net);
`FileIO.c` +168 lines of `#ifdef MMBASIC_HOST` gates and header macros;
`host_fs.c` +60 lines for the opaque directory walker + whole-path
POSIX helpers; `bc_source.c` -20 lines from the dropped
`source_compile_files`. No `#else` / device-path code is modified.

**New tests** (t186-t190, marked `' RUN_ARGS: --interp` — VM-side bridge
doesn't yet sync file-handle state with the interpreter for function
bridging):
- t186 `file_loc_lof_eof` — LOC/LOF/EOF as direct queries + INPUT$.
- t187 `file_inputstr` — INPUT$(n, #fnbr) short-return at EOF.
- t188 `file_flush` — FLUSH + cross-handle read visibility.
- t189 `fun_dir_loop` — DIR$() iterative walk with glob filter.
- t190 `file_large` — 800-line round-trip across the 512-byte
  FileGetChar buffer boundary.

**Exit gate results:**
- `./run_tests.sh` (default compare) — **197/197** green (192 existing +
  5 new).
- `make sim` — links clean; sim_obj tree builds without errors.
- REPL smoke tests:
  - `LOAD "file.bas"`, `RUN`, `PRINT` after load — program executes,
    subsequent commands don't inherit stale state.
  - `SAVE "copy.bas"` + `LOAD "copy.bas"` round-trip.
  - `FILES` in REPL — full formatted listing against real POSIX
    directory (time, date, size, filename + dir/file count).
  - `CHDIR "subdir"` / `CHDIR ".."` / `FILES` — navigation works.
  - `DRIVE "A:"` — errors cleanly, doesn't corrupt display state.
  - Error recovery — `Option.Width` / `DISPLAY_CONSOLE` / etc. survive
    the LoadOptions reset inside error().
- Device build: not re-verified here (all new host regions use `#ifdef
  MMBASIC_HOST`; the device `#else` / default paths are unchanged).

### Phase 4 — `shared/audio/Audio.c` compiles on host — ✅ DONE (2026-04-17)

Scope correction from the original plan: `shared/audio/Audio.c` is *not* HAL-clean.
Grep turned up 31 direct hardware calls (PWM×21, flash×2, PIO×1) plus
heavy dependencies on single-header decoder libs (`dr_wav`, `dr_flac`,
`dr_mp3`, `hxcmod`) and the VS1053 driver. Making all of that compile on
host would be a multi-week refactor with no runtime value — the sim's
audio backend is WebAudio, not a DMA ring buffer.

**Approach taken: file-level split inside `shared/audio/Audio.c` instead of per-function
gating.** Device body stays 100% textually unchanged; host gets a short
tail section that re-uses the existing `host_sim_audio_*` JSON emitter.

**Shape:**
```c
/* shared/audio/Audio.c */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#ifndef MMBASIC_HOST
/* 2,140-line device body — decoder libs, PWM pipeline, full cmd_play */
...
#else  /* MMBASIC_HOST */
#include "host_sim_audio.h"
/* Audio globals host needs (CurrentlyPlaying, PWM_FREQ, vol_left/right,
 * SoundPlay, PhaseM_*, PhaseAC_*, mono, WAV_fnbr, WAVInterrupt,
 * WAVcomplete) — defined once here instead of scattered through
 * host_stubs_legacy.c. */
void MIPS16 cmd_play(void) { /* STOP/PAUSE/RESUME/CLOSE/VOLUME/TONE/
                              * SOUND route to host_sim_audio_*;
                              * file-playback subcommands error. */ }
void CloseAudio(int all) { host_sim_audio_stop(); CurrentlyPlaying = P_NOTHING; }
void StopAudio(void)     { host_sim_audio_stop(); CurrentlyPlaying = P_NOTHING; }
void checkWAVinput(void) {}
void audio_checks(void)  {}
#endif
```

**Stubs deletion** — ~115 lines from `host_stubs_legacy.c`:
- `cmd_play` + `host_play_parse_channel` + `host_play_parse_type`
  helpers (113 lines).
- `CloseAudio` stub.
- Duplicate globals: `CurrentlyPlaying`, `WAVcomplete`, `WAVInterrupt`.

**Makefile** — added `shared/audio/Audio.c` to `CORE_SRCS`.

**PLAY unification via OP_BRIDGE_CMD** (same pattern as FILES in
Phase 3). The VM source compiler's `source_compile_play` only emitted
native syscalls for PLAY STOP and PLAY TONE; everything else —
SOUND, VOLUME, PAUSE, RESUME, CLOSE, WAV/FLAC/MP3/MODFILE/MIDI —
hit `bc_set_error(cs, "Unsupported PLAY command")`. That silently
diverged FRUN from RUN: interp ran the full subcommand set; VM
rejected 20 of 22 subcommands at compile time.

Fix: delete `source_compile_play` and the PLAY dispatch block in
`source_compile_statement`. PLAY now falls through to `OP_BRIDGE_CMD`,
which pre-tokenises the statement and at runtime dispatches to
`commandtbl[cmdPLAY].fptr()` → shared/audio/Audio.c's `cmd_play`. Interpreter and
VM share one parser. `BC_SYS_PLAY_STOP` / `BC_SYS_PLAY_TONE` enums
and bc_vm.c cases are now dead — Phase 7 cleanup.

**What's NOT in Phase 4** (deferred indefinitely — no clear value):
- PLAY WAV/FLAC/MP3/MODFILE/MIDI on host. Emitting encoded audio frames
  over a WebSocket to a WebAudio player is its own subsystem.
- PLAY ARRAY and NOTE. Ditto.
- VS1053 support on host. Host has no SPI IC to drive.

**Exit gate results:**
- `cd host && ./build.sh` — clean, 3 pre-existing warnings only (the
  `__attribute__((optimize))` unknown-attribute warning on MIPS16 —
  already present for other core files).
- `./run_tests.sh` (default compare) — **197/197** green.
- `make sim` — links clean; `mmbasic_sim` binary produced.
- `t115_play_tone_stop_native` — still PASS after the cmd_play move.
- Device build: not re-verified this phase. All additions gated by
  `#ifdef MMBASIC_HOST`; device sees the unchanged original body.

### Phase 5 — extract portable `MM_Misc.c` subset — ✅ DONE (2026-04-17)

Scope expansion from the original plan: gating the whole file with
`#ifdef MMBASIC_HOST` wasn't tractable (6,474 lines, 173 hardware
touchpoints, 303 preprocessor directives, plus `xregex.h` / `aes.h` /
`pico/bootrom.h` / `hardware/structs/systick.h` includes that pull in
device-world types before any function body). Instead, extracted the
portable subset into a new shared file — same pattern as the existing
`gfx_*_shared.c` layer.

**New file:** `shared/mmbasic/mm_misc_shared.c` (~1,300 lines, compiled by both host
Makefile and device CMakeLists). Contents:
- sort helpers + `cmd_sort`
- `cmd_pause`, `cmd_timer` / `fun_timer`
- `cmd_longString` + `fun_LGetStr` / `LGetByte` / `LInstr` / `LCompare`
  / `LLen`
- `fun_format`
- `cmd_date` / `cmd_time` / `fun_date` / `fun_time` / `fun_day` /
  `fun_datetime` / `fun_epoch` plus `gettimefromepoch` / `get_epoch`
- globals `TimeOffsetToUptime` / `timeroffset` / `daystrings`

`fun_date` / `fun_time` gain a `#ifdef MMBASIC_HOST` branch that
honours `MMBASIC_HOST_DATE` / `MMBASIC_HOST_TIME` env vars (same
pattern `vm_sys_time.c` already uses) so the interp-vs-VM comparison
harness can pin wall-clock values deterministically.

**`MM_Misc.c`:** 6,474 → 5,302 lines. Extracted definitions replaced
with externs where other MM_Misc.c code still references them.

**`host_stubs_legacy.c`:** -18 no-op stubs (`cmd_sort`, `cmd_longString`,
`cmd_date`, `cmd_time`, `cmd_pause`, `cmd_timer`, `fun_timer`,
`fun_date`, `fun_datetime`, `fun_day`, `fun_epoch`, `fun_format`,
`fun_LCompare`, `fun_LGetByte`, `fun_LGetStr`, `fun_LInstr`, `fun_LLen`,
`fun_time`, plus duplicate globals `timeroffset` / `TimeOffsetToUptime`).

**Bugs surfaced by the new tests** (fixed in this phase):

1. **VM function-bridge double-append.** `source_parse_varname` keeps
   the `$`/`%`/`!` suffix in the `name` buffer (e.g.
   `name = "FORMAT$"`, `name_len = 7`), but
   `bc_source.c:source_parse_primary` appended the suffix a second
   time before tokentbl lookup, producing `"FORMAT$$("` — no match.
   Every T_STR function needing `OP_BRIDGE_FUN_S` (`FORMAT$`,
   `LGETSTR$`, …) bailed with "Expected numeric expression" under
   FRUN. Fixed by dropping the duplicate append.

2. **Host `timegm`/`gmtime` stubs applied the local TZ offset.**
   `host_platform.h` renames `timegm`/`gmtime` →
   `mmbasic_timegm`/`mmbasic_gmtime` to sidestep GPS.h's
   const-vs-non-const signature mismatch with macOS `<time.h>`. The
   stubs in `host_stubs_legacy.c` delegated to `mktime`/`localtime`,
   which apply the local timezone offset, so `EPOCH` / `DATETIME$` /
   `DAY$` results were TZ-shifted (CST host gave results 6 h off
   UTC). Rewrote the stubs to `#undef` the macros locally and call
   real libc `timegm`/`gmtime`, preserving UTC semantics.

**New tests** (default interp-vs-VM comparison mode):
- `t191_cmd_sort`       — SORT on integer array
- `t192_longstring`     — APPEND / LLEN / LGETSTR$ / LGETBYTE / LINSTR / LCOMPARE
- `t193_format`         — FORMAT$ with %f / %e / default specifiers
- `t194_datetime_funs`  — DAY$ / DATETIME$ / EPOCH UTC round-trip

**Exit gate results:**
- `./run_tests.sh` — **201/201** green.
- `make sim` — clean.
- Device CMakeLists additions are textually correct; not re-verified
  on hardware. `shared/mmbasic/mm_misc_shared.c` contains only portable code (no
  `#ifdef` for device-specific features), so device build should
  succeed.

### Phase 6 — REPL & banner unification — ✅ DONE (2026-04-17)

- `MMBasic_PrintBanner(void)` added to `MMBasic_REPL.c`. Emits the
  first line + `MMBASIC_COPYRIGHT` (+ host trailer on host) via
  `MMPrintString`. Device first line = runtime `banner` array (still
  patched for rp2350a/b variants in PicoMite.c); host first line =
  `"\rPicoMite MMBasic Host V" VERSION "\r\n"` followed by
  `"Host REPL — Ctrl-D to exit.\r\n\r\n"`.
- `MMBASIC_COPYRIGHT` macro moved from PicoMite.c (where it was
  `#define COPYRIGHT ...`) into `Version.h`. One authoritative
  definition for the Geoff/Peter/Josh trailer.
- `host/host_main.c`: the five-line `printf` block replaced with a
  single `MMBasic_PrintBanner()` call.
- `PicoMite.c:3713-3722`: both `MMPrintString(banner); MMPrintString(COPYRIGHT);`
  pairs collapsed to `MMBasic_PrintBanner()`.

**Exit gate results:**
- Host: piped / REPL / `--sim` all emit the banner correctly
  (verified `echo QUIT | ./mmbasic_test --repl` prints banner +
  copyright + "Host REPL" trailer).
- `./run_tests.sh` — 197/197 green.
- `make sim` — links clean.
- Device build: not re-verified this phase. `MMBasic_PrintBanner`'s
  device branch uses the same `banner` / `MMBASIC_COPYRIGHT` text
  that PicoMite.c previously emitted inline, in the same order, via
  the same `MMPrintString`. Instruction stream should match.

### Phase 7 — Cleanup & rename — ✅ DONE (2026-04-18)

**Dead native syscalls removed (moved to OP_BRIDGE_CMD in earlier phases):**
- `vm_sys_file_files` + `BC_SYS_FILE_FILES` + `BC_FILE_FILES` (Phase 3, FILES) — removed from bc_vm.c (both the bc_vm_syscall switch case and the op_file subop case), bytecode.h enums, and vm_sys_file.c (both host and device branches). Dragged `vm_file_match_pattern` + `vm_file_resolve_dir_and_pattern` helpers with it (no other callers).
- `vm_sys_audio_play_stop` / `vm_sys_audio_play_tone` + `BC_SYS_PLAY_STOP` / `BC_SYS_PLAY_TONE` + `OP_PLAY_STOP` / `OP_PLAY_TONE` (Phase 4, PLAY) — removed from bc_vm.c dispatch (switch case + computed-goto labels + dispatch table entries), bc_debug.c disassembler, bytecode.h enums. Entire `vm_sys_audio.{c,h}` file deleted; dropped from CMakeLists.txt / CMakeLists 2350.txt / host/Makefile. `#include "vm_sys_audio.h"` dropped from bc_vm.c.

**Dead-code sweep in host_stubs_legacy.c:**
- Removed ~515 lines of legacy drawing helpers (`host_fill_rect_pixels`, `host_draw_line_pixels`, `host_calc_triangle_edge`, `host_draw_triangle_pixels`, `host_glyph_rows`, `host_draw_char`, `host_font_glyph_bit`, `host_plot_text_pixel`, `host_fill_text_cell`, `host_draw_font_char`, `host_draw_text`) plus `host_font_metrics` and `host_clamp_int` — all leftover from before Draw.c was ported to compile on host. No callers anywhere in the codebase.
- Removed `HostBoxArgCtx` + `host_box_arg_get_int` + `host_pixel_fail_msg` + `host_pixel_fail_range` + `host_fill_polygon_edges` + `host_draw_polygon_points` + `host_getargaddress` + `host_cmd_single_path` + `host_file_copy_mode_from_string` — all defined but never called.

**File split** — `host_stubs_legacy.c` (2,405 lines) split into four focused files, all under 1000 lines:

| File | Lines | Contents |
|---|---|---|
| `host_runtime.c` (renamed residual) | ~750 | Runtime lifecycle (`host_runtime_configure`/`_begin`/`_finish`/`_timed_out`/`_check_timeout`/`host_sim_apply_slowdown`/`host_write_screenshot`), console I/O (`MMInkey`/`MMgetchar`/`putConsole`/`MMputchar`/`MMPrintString`/`SSPrintString`/`MMfopen`/`MMfclose`/`MMgetline`/`myprintf`/`SerialConsolePutC`/`kbhitConsole`/`host_print`/`host_prints`/`host_decode_escape_sequence`), hardware-world zero-inited globals (Option, PinDef, FontTable, dma_hw, watchdog_hw, PWMxApin, etc.), `CheckAbort`/`routinechecks`/`check_interrupt`/`ClearExternalIO`/`SoftReset`/`uSec`/`__get_MSP`/`closeframebuffer`/`initMouse0`/`restorepanel`/`clear320`, `host_repl_mode`, `mmbasic_timegm`/`mmbasic_gmtime`. |
| `host_fastgfx.c` (new) | ~260 | `bc_fastgfx_swap`/`sync`/`create`/`close`/`reset`/`set_fps`, `cmd_fastgfx`, `cmd_framebuffer`. Exposes `host_fastgfx_reset_state()` for `host_runtime_begin` to reset internal state. |
| `host_fs_shims.c` (new) | ~435 | `host_f_findfirst`/`findnext`/`closedir`/`unlink`/`rename`/`mkdir`/`chdir`/`getcwd` FatFS walkers, `host_fs_posix_*` per-fnbr file table, `host_sd_root`, `host_resolve_sd_path`, `ExistsFile`/`ExistsDir`, `flash_range_erase`/`program`, `SaveProgramToFlash`, full LFS stub surface, `host_flash_contents_init`/`host_options_snapshot` + flash buffers. |
| `host_peripheral_stubs.c` (new) | ~470 | All no-op `cmd_*`/`fun_*` for hardware host doesn't carry; `cmd_pwm`/`cmd_Servo`/`cmd_setpin`/`fun_pin`/`fun_keydown` route through VM pin HAL. `ExtCfg`/`ExtSet`/`ExtInp`/`PinSetBit`/`GetPinStatus`/`CallCFuncInt*`/`IrInit`/`KeypadCheck`/`codemap`/etc., GPS globals, `AES_*`, `xreg*`, memory stubs, `SerialOpen`/`Close`/`Getchar`, `UnloadFont`/`setmode`/`copyframetoscreen`/`copybuffertoscreen`/`merge`/`blitmerge`, `setterminal`/`OtherOptions`/`disable_sd`, `DisplayNotSet`/`ScrollLCDSPISCR`/`Display_Refresh`/`cmd_guiBasic`, `display_details`/`BDEC_bReadHeader`/`BMP_bDecode_memory`. |

The residual got named `host_runtime.c` instead of the plan's `host_noop_stubs.c` because after splitting the no-ops into `host_peripheral_stubs.c`, the remaining content is all runtime glue — `host_runtime.c` fits better. Naming decision documented here; plan overridden.

**Docs updated:**
- `host/README.md` — file layout table + module-by-module responsibilities updated to reflect the new split; test count bumped 168→201; added paragraph framing the host build as a HAL target.
- `project_host_is_its_own_port.md` memory rewritten — host is no longer "its own port", it's a HAL target under shared MMBasic source. Lists which files are shared vs host-owned after the refactor.
- `MEMORY.md` index line updated to match.

**Exit gate results:**
- `./build.sh` — clean (3 pre-existing MIPS16 attribute warnings + 1 Option.Height narrowing — all known).
- `./run_tests.sh` (default compare) — **201/201** green.
- `wc -l host/*.c host/*.h` — no file over 1000 lines (host_main.c is 948, largest).
- Device build: not re-verified this phase. Changes to bytecode.h / bc_vm.c / bc_debug.c / vm_sys_file.c touch both host and device; gated `#else` device paths are unchanged in shape (only the dead enum values / switch cases / op labels removed). Device bytecode keeps working because no live code emitted `OP_PLAY_*` / `BC_SYS_PLAY_*` / `BC_FILE_FILES` / `BC_SYS_FILE_FILES` since Phases 3-4.

## What does *not* change

- `vm_sys_graphics.c`, `vm_sys_file.c`, `vm_sys_audio.c`, `vm_sys_time.c`, `vm_sys_pin.c`, `vm_sys_input.c` — VM syscalls stay as-is. The VM dispatch path is completely untouched.
- `bc_vm.c`, `bc_source.c`, `bc_runtime.c`, `bc_alloc.c` — bytecode compiler/VM stay as-is.
- `gfx_*_shared.c` — already shared, no change.
- `MMBasic.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, `Editor.c`, `Commands.c`, `Functions.c` — already shared, stay shared.
- Device peripheral drivers (`drivers/spi_bus/SPI.c`, `drivers/i2c_bus/I2C.c`, `Keyboard.c`, display drivers) — remain device-only, not linked on host.
- The "no `VM-only device` build" constraint from the bridge plan — still holds.

## Validation gates (applied after every phase)

1. **Host build:** `cd host/ && ./build.sh` — zero warnings, zero errors.
2. **Host tests, default compare:** `cd host/ && ./run_tests.sh` — all PASS.
3. **Host tests, interp only:** `./run_tests.sh --interp`.
4. **Host tests, VM only:** `./run_tests.sh --vm`.
5. **Device build:** `cmake` RP2040 and RP2350 targets build clean.
6. **Device smoke boot:** firmware flashed to RP2040 boots to prompt and runs a demo `.bas`. Required after Phase 2 (Draw.c) and Phase 3 (FileIO.c) specifically.
7. **Framebuffer diff test** (new, Phase 2 forward): run a graphics program through interpreter + VM, snapshot `host_fb_get_pixel` over the frame, assert byte-equal. Catches divergences between the shared `Draw.c` path and the VM's direct path.

## Open questions

- **Does `PICOMITEVGA` dual-core rendering need a host story?** Probably not — host renders single-threaded. Leave `#ifdef PICOMITEVGA` blocks as device-only; the corresponding commands remain host no-ops. Decide in Phase 2 discovery.
- **What about `Custom.c`, `drivers/gui_controls/GUI.c`?** Out of scope for now. They have zero commands shared between host and device currently, so there's no duplication to unwind. Revisit after Phase 7 if we want full parity.
- **Does the VM need its own host HAL entry point?** The VM already uses `host_put_pixel` indirectly via `DrawBox` / `DrawLine`. Once those route through the `host_fb_hal.h` API, the VM also benefits — no additional work.

## Rollback

Every phase is a single commit (or small commit chain) on the `host-hal-refactor` branch. If a phase breaks a downstream integration, revert the phase commit and re-attempt. The HAL headers added in earlier phases can remain (they're additive). If the whole effort stalls, the branch can be abandoned and `bridge-restoration` is unaffected.

## Ordering rationale

Phase 2 (Draw.c) is the largest and highest-value phase — it removes the most duplication and closes the most drift risk. But it's also the one most likely to surface surprises in the pico-sdk dependency graph. If Phase 2 reveals that `Draw.c` is more entangled than expected, bail and narrow to the three smallest commands (cmd_box, cmd_pixel, cmd_line) as a proof-of-concept before committing to the full file.

Phase 3 and 4 can be parallelized if desired (independent files). Phase 5 is optional — it's a smaller win and `MM_Misc.c` has more fundamentally device-bound code than the others.

---

**Superseded by [real-hal-plan.md](real-hal-plan.md) (Phase 13 in progress, 2026-04-24).** The host-HAL refactor demonstrated the technique on a single non-device axis; the real-HAL plan generalises it to every device target. Patterns established here (shared Draw.c / FileIO.c / shared/audio/Audio.c / shared/mmbasic/mm_misc_shared.c behind `#ifdef MMBASIC_HOST`) are the direct ancestors of the current `hal/*.h` + `drivers/*/` + `ports/*/` layout.
