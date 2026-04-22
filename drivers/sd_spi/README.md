# drivers/sd_spi — SD/MMC SPI driver

Implements FatFS's `disk_*` diskio contract against the SD card wired
up on the PicoMite's SPI pins. Backs `hal_storage` via the SDCARD
device ID (`ports/pico_sdk_common/hal_storage_pico.c` forwards
`hal_storage_read/write/erase/sync/present` to this file's `disk_*`
entry points).

No host impl — the host port runs its file I/O against real POSIX
(when `host_sd_root` is set) or against `vm_host_fat.c`'s RAM disk
(test harness).

## Lifted from

`mmc_stm32.c` (repo root, pre-real-hal refactor). No behavioural change
on the move — source file was relocated and the CMake reference
updated.

## Future work

The HAL contract expects a `drivers/<name>/tests/` conformance suite
(see `docs/real-hal-plan.md`). Not yet authored.
