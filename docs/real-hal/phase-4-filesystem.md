# Real HAL — Phase 4: `hal_storage` + `hal_filesystem` ✅ (F3 closed)

**Status:** infrastructure landed (4a); F3 ifdef elimination drove FileIO.c target-macro ifdefs from 60 → 0 across fifteen sub-steps. FileIO.c is in `STRICT_FILES`. See the step table below and the rows in `scoreboard.md`.

## What landed (Phase 4a)

- `hal/hal_storage.h` + `hal/hal_filesystem.h` contracts.
- `hal_storage_pico.c`, `hal_filesystem_pico.c`, `hal_filesystem_host.c` impls.
- `hal_fs_open`/`close`/`read`/`write`/`seek`/`tell`/`eof`/`sync` working on device and host.
- `BasicFileOpen`/`FileGetChar`/`FilePutChar`/`FilePutStr`/`FileEOF`/`FileClose`/`ForceFileClose`/`positionfile`/`cmd_seek`/`cmd_flush`/`fun_loc`/`fun_lof` + copy variants all HAL-routed.
- BMP/JPEG/WAV/TFTP/HTTP callers migrated.
- `FileTable` collapsed to `{ com }`.
- `host_fs_posix_*` shim family deleted.
- Drivers relocated: `mmc_stm32.c` → `drivers/sd_spi/`, `fs_flash_*` → `drivers/pico_flash/`.

**Ifdef count:** FileIO.c went from 75 → 60, not the target of <10.

**Commits:** `293c98d`, `b929139`, `d171e87`, `0d7940d`, `25d6bd7`, `1ca8347`, `581a1db`, `20a8629`, `2d76e16`, `f0e83c9`, `1848318` (Phase 0 baseline script).

## First ifdef-elimination attempt (partial revert required)

Commit `61cb08e` claimed to eliminate 14 `rp2350` ifdefs from FileIO.c by converting them to PSRAM/UPNG/DEFINES/GPIO_COUNT port-config gates. The blocks were renamed to `#if HAL_PORT_*`, not actually moved out of core. Revert per fixup plan F1; redo per F3.

## F3 sub-step summary (landed)

| step | commit | what | Δ FileIO target |
|------|--------|------|-------|
| F3 step 1 | `e56b1f5` | PSRAM cache (`mmbasic_save/restore_psram_settings`) → `ports/pico_sdk_common/psram_cache.c`; `mergedread` / `a_dlist` / `MemLoadProgram` decl unconditional | −10 |
| F3 step 2 | `fa0b252` | `disable/enable_interrupts_pico` MMBASIC_HOST gate removed; host stubs | −2 |
| F3 step 3 | `b26145b` | `ResetOptions` heartbeat `rp2350a` runtime; LoadPNG call under `HAL_PORT_HAS_UPNG` | −2 |
| F3 step 4 | `7c1d2a9` | `ProcessWeb()` stub on non-WEB; 4 PICOMITEWEB gates in FileIO.c removed | −4 |
| F3 step 5 | `ee4ebba` | `tcp_free_recv_buffers()` / `tcp_realloc_recv_buffers()` helpers replace 4 TCPstate blocks; `closeall3d` WEB stub | −5 |
| F3 step 6 | `e9ea76b` | `ResetOptions` board-defaults (CPU_Speed / USB / HDMI / VGA / touch) → 7 per-port `port_defaults.c` files | −7 |
| F3 step 7 | `353038b` | `cmd_psram` (178 lines) → `ports/pico_sdk_common/cmd_psram.c` | −1 |
| F3 step 8 | `3d3ae10` | DEFINES loader + `MemLoadProgram` + `FileLoadCMM2Program` (353 lines) → `ports/pico_sdk_common/defines_loader.c`; `a_dlist` typedef to FileIO.h | −1 |
| F3 step 9 | `2875acd` | `MemWriteBlock` + CMM2 helpers (418 lines) → `ports/pico_sdk_common/mem_writeblock.c`; `LoadPNG` (88 lines) → `ports/pico_sdk_common/load_png.c`; `union u_flash` + `MemWord` + `mi8p` to FileIO.h | −2 |
| F3 fixup  | `2fdce89` | Restore host build: add `host/port_config.h` (ports/<board>/port_config.h was on every device include path but host had none); add `rp2350a` / `port_set_default_options` / `closeMQTT` / `ProcessWeb` / `tcp_*_recv_buffers` host stubs (the F3 step 5/6 extractions removed their device-side definitions from host's link surface) | 0 |
| F3 step 10 | `79f96aa` | SD-sector read-cache moves from FileIO.c arrays (`SDbuffer[fnbr]`, `buffpointer`, `bw[fnbr]`, `lastfptr`, `fmode`) into `pico_fs_slot_t`. New HAL primitives `hal_fs_getc` / `hal_fs_putc`; `hal_fs_seek` / `tell` / `eof` are now cache-aware. `FileGetChar` / `FilePutChar` / `FileEOF` / `positionfile` / `BasicFileOpen` / `ForceFileClose` / `FilePutStr` / `fun_loc` collapse to single HAL calls. `diskchecktimer` poke moves into the device backend. Host backend gets trivial single-byte fread/fwrite | −11 |
| F3 step 11 | `9c29781` | Four command-level lifecycle hooks (`cmd_files_save_program_context`, `cmd_files_restore_program_context`, `cmd_files_pump_console_key`, `cmd_load_post_cleanup`) split host vs device behaviour for the SaveContext+InitHeap dance, the PRESS-ANY-KEY MMInkey poll, and cmd_load's tknbuf-clobber bounce. Real impls in `host/host_runtime.c`; device no-ops in new `ports/pico_sdk_common/cmd_files_hooks.c` | −5 |
| F3 step 12 | `e6adb08` | Three small wins: (a) delete dead `FileLoadSourceProgramVM` + its `bc_alloc.h` / `bc_run_diag.h` includes (function added in `e27f8d4` but never called); (b) `MAXFILES` becomes `HAL_PORT_FILES_MAX` in each port_config.h (256 host, 1000 device); (c) `InitSDCard` body splits into `port_mount_sd_drive()` (device pin check + f_mount; host vm_host_fat_mount + SDCardStat clear) | −4 |
| F3 step 13 | `c3e7481` | Drop dead `#ifdef PICOMITE` multicore.h include + unused `frameBufferMutex` extern; `cmd_disk` A:/B: rejection collapses to `port_drive_check(char)` HAL call; LoadOptions's 47-line PICOCALC override block moves to new `ports/pico_sdk_common/port_load_overrides.c` exposing `port_apply_load_overrides()` (host stub no-ops) | −4 |
| F3 step 14 | `7e2d6c8` | FatFS directory + path dispatch through new `hal/hal_fatfs_dispatch.h` (`hal_ff_findfirst` / `findnext` / `closedir` / `unlink` / `chdir` / `getcwd`). Device impls in `cmd_files_hooks.c` forward to FatFS f_*; host impls in `host_fs_shims.c` forward to `host_f_*` | −2 |
| F3 step 15 | `7e2d6c8` | `cmd_LoadJPGImage` body is unconditionally compiled — picojpeg.c added to host's CORE_SRCS so the symbols link. The host stub goes away | 0 (no scoreboard delta — the leftover 2 are `#ifndef max/min`) |
| F3 close   | `7e2d6c8` | `FileIO.c` added to `STRICT_FILES`; gate now fails on any future target-macro or port-config #ifdef in the file | — |
| **total**  | | | **−60 (60 → 0 target ifdefs)** |

F3 CLOSED. Promote-to-STRICT lands in the same commit as steps 14+15.

## Exit gate

- FileIO.c: zero `#if*` directives on any target OR port-config macro.
- Every target-specific file-system behaviour lives in a HAL impl, not in FileIO.c.
- `tools/check_hal_purity.sh` passes for FileIO.c and `hal/hal_filesystem.h`.

After 4b + 7 + 9, FileIO.c is clean; only then may Phase 11 sweep verify.
