# Real HAL — Phase 4: `hal_storage` + `hal_filesystem` 🔧 (57% — F3 in progress)

**Status:** infrastructure landed (4a); F3 ifdef elimination is driving FileIO.c target-macro ifdefs from 60 → 26 across nine sub-steps, with MMBASIC_HOST SD-cache gates remaining as the architectural holdout. See the step table below and the rows in `scoreboard.md`. `../real-hal-fixup-plan.md` (F3) is the standard.

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
| **total** | | | **−34 (60 → 26)** |

## What remains (~26 ifdefs)

- **~20 `MMBASIC_HOST` gates:** SD read/write cache optimization (`SDbuffer[fnbr]`, `buffpointer`, `bw[fnbr]`, sector-seek in `positionfile`). Host's POSIX/RAM-disk path doesn't need the per-fnbr cache — `fread` buffers natively and FatFS-on-RAM is zero-cost. Clean migration requires either pushing the SD read cache into `hal_filesystem_pico.c` (so `hal_fs_read` does the caching on device and `hal_fs_host` doesn't need to) or accepting the gates as a hal_backend-distinction and exempting MMBASIC_HOST from the FileIO.c strict gate.
- **~3 `rp2350`-specific** smaller blocks still in FileIO.c beyond the big extracted ones — likely flash geometry constants.
- **2 `MMBASIC_HOST` includes** at the top of the file (vm_host_fat.h + host FatFS shims). Hard to unconditionalize without pulling host-only symbols onto device.
- **1 `PICOCALC` + `rp2350`** block at line 75 — `bc_alloc.h` / `bc_run_diag.h` include for FileLoadSourceProgramVM. Policy gate.
- **1 `PICOMITE`** block for `pico/multicore.h` + `frameBufferMutex` extern (line 91).

Remaining work is either (a) introducing a HAL layer for SD-cache management (real architectural change) or (b) calling F3 done-at-architectural-boundary and promoting FileIO.c to STRICT_FILES with an MMBASIC_HOST exemption line in `check_hal_purity.sh`.

## Exit gate

- FileIO.c: zero `#if*` directives on any target OR port-config macro.
- Every target-specific file-system behaviour lives in a HAL impl, not in FileIO.c.
- `tools/check_hal_purity.sh` passes for FileIO.c and `hal/hal_filesystem.h`.

After 4b + 7 + 9, FileIO.c is clean; only then may Phase 11 sweep verify.
