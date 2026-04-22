# Real HAL — Phase 4: `hal_storage` + `hal_filesystem` 🔧

**Status:** infrastructure landed (4a). Ifdef elimination is the remaining work — see the fixup plan (`../real-hal-fixup-plan.md`, F3).

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

## What remains (corrected)

FileIO.c must have zero `#if*` directives on target OR port-config macros. Current blocks to migrate:

- **24 `MMBASIC_HOST` blocks:** SD card init, flash erase geometry, POSIX-vs-FatFS dispatch. Bodies move into `hal_filesystem_pico.c` / `hal_filesystem_host.c`. Core calls a single `hal_fs_*` function and lets the impl dispatch.
- **8 `rp2350` blocks:** flash sector sizes, PSRAM bank switching. Bodies move into `hal_flash_pico.c` or, where a constant suffices, each port's `port_config.h` defines a `#define` consumed as a value (never as a `#if` operand).
- **8 `PICOMITEVGA` blocks:** display-specific image load paths. Delegate to Phase 7 (display HAL) or a display-aware file helper. FileIO.c itself calls `hal_display_*` unconditionally; the display impl is a no-op on non-VGA ports.
- **10 `PICOMITEWEB` blocks:** MQTT config, network file ops. Delegate to Phase 9 (`hal_net`) or a network-aware file helper.
- **1 `PICOCALC` + `rp2350` block:** flash layout. Port-config constant.

## Exit gate

- FileIO.c: zero `#if*` directives on any target OR port-config macro.
- Every target-specific file-system behaviour lives in a HAL impl, not in FileIO.c.
- `tools/check_hal_purity.sh` passes for FileIO.c and `hal/hal_filesystem.h`.

After 4b + 7 + 9, FileIO.c is clean; only then may Phase 11 sweep verify.
