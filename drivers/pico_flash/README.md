# drivers/pico_flash — internal flash as LFS backing (A: drive)

Provides the four `fs_flash_*` callbacks + static buffers that fill out
`pico_lfs_cfg` for littlefs. A: on device is littlefs running on XIP
flash above `TOP_OF_SYSTEM_FLASH`; this file is the block-level
adapter between LFS and the flash.

Flash writes go through `hal_flash_program` / `hal_flash_erase` wrapped
in `disable_interrupts_pico` / `enable_interrupts_pico` (serialises
PSRAM setting save/restore and pins the handler itself into SRAM via
`__not_in_flash_func` — flash is unreachable while being programmed).

Reached from `ports/pico_sdk_common/hal_storage_pico.c` for block
geometry introspection (`hal_storage_block_size` /
`hal_storage_block_count` for INTERNAL_FLASH read `pico_lfs_cfg`), and
from FileIO.c via direct `lfs_mount` against `pico_lfs_cfg`.

## Lifted from

FileIO.c (repo root, pre-real-hal refactor). Lines 133-160 (buffers,
cfg) and 403-439 (fs_flash_* bodies) moved verbatim. FileIO.c keeps
only an `extern struct lfs_config pico_lfs_cfg;`.

## Host

No host impl — A: on host is stubbed / emscripten MEMFS via the host
port's existing FileIO.c dispatch; the LFS backing doesn't exist off
device.

## Future work

Conformance test under `drivers/pico_flash/tests/` (see
`docs/real-hal-plan.md` "HAL conformance tests"). Not yet authored.
