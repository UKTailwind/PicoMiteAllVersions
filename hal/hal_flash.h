/*
 * hal/hal_flash.h — persistent flash HAL.
 *
 * Surface core code uses to read, erase, and program on-board flash
 * (Option block, tokenised program memory, CFunctions, saved variables,
 * Library, sample storage).
 *
 * Offsets throughout this API are absolute flash offsets, matching the
 * Pico SDK's `flash_range_program` / `flash_range_erase` semantics — i.e.
 * the offset from the start of the flash device, not from XIP_BASE.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md §"Global conventions"):
 *   0 = success; negative errno-style on failure. Caller owns all buffers.
 *   HAL impl never calls MMBasic's error() — caller translates errors.
 */

#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Erase `len` bytes of flash starting at `offset`.
 *
 * Alignment:
 *   - `offset` must be a multiple of the flash erase granularity (4096
 *     bytes on both RP2040 and RP2350).
 *   - `len`    must be a multiple of the erase granularity.
 *
 * Atomicity:
 *   - A power loss mid-erase leaves the affected sectors in indeterminate
 *     state. Callers that need atomic updates (e.g. Option block) must
 *     structure their on-flash layout for torn-write recovery.
 *
 * IRQ-safety: **not** IRQ-safe. On RP2040, flash ops disable XIP — holding
 * off IRQs is the caller's responsibility via `save_and_disable_interrupts`
 * at the port layer. The HAL impl handles that internally.
 */
int hal_flash_erase(uint32_t offset, size_t len);

/* Program `len` bytes at `offset`. The sector must be erased first.
 *
 * Alignment:
 *   - `offset` must be a multiple of 256 bytes (flash page size).
 *   - `len`    must be a multiple of 256 bytes.
 *   - `buf`    does not have an alignment requirement.
 *
 * Writing to an un-erased sector is undefined — flash can only go from
 * 1→0 per bit; programming over previously-programmed bytes yields the
 * bitwise-AND of old and new.
 */
int hal_flash_program(uint32_t offset, const void * buf, size_t len);

/* Fill `out[0..7]` with the 64-bit device unique ID.
 *
 * On Pico SDK ports this wraps `flash_get_unique_id()` (which actually
 * queries the flash chip's JEDEC unique-ID register, not the MCU).
 * On host, returns a stable-per-install 8-byte value.
 */
int hal_flash_unique_id(uint8_t out[8]);

/* Query the flash chip's JEDEC Read ID (command 0x9F).
 *
 * Returns 4 bytes from the SPI transaction in the same layout as the
 * Pico SDK's `flash_do_cmd` RX buffer for a 4-byte 0x9F command:
 *   out[0] = echo of the command (useless — don't rely on value)
 *   out[1] = manufacturer ID
 *   out[2] = memory type
 *   out[3] = capacity code (log2 of size in bytes — 23 means 8 MB)
 * MMBasic uses this at boot to set Option.FlashSize. On host, returns a
 * canned response matching an 8 MB chip. */
int hal_flash_read_jedec_id(uint8_t out[4]);

/* Optional batching hooks for callers that stream page writes through
 * hal_flash_program(). Pico SDK ports use these to hold off interrupts
 * and preserve PSRAM cache state across the batch; host/ESP32 stubs are
 * no-ops. */
void hal_flash_write_begin(void);
void hal_flash_write_end(void);
int hal_flash_write_active(void);

/* -----------------------------------------------------------------------
 * Option-block convenience helpers.
 *
 * The Option block is a `struct option_s` persisted at FLASH_TARGET_OFFSET.
 * Core code updates `Option` in RAM (core/state/option_state.c) and calls
 * SaveOptions() / LoadOptions(); those helpers are implemented in terms
 * of the primitives above. These convenience functions exist so callers
 * don't have to know the layout.
 *
 * Reads are synchronous pointer-like on device (XIP-cached flash); on
 * host, a RAM-backed buffer stands in. Writes go through erase + program.
 *
 * Freshness contract: a freshly initialised backing store (first boot;
 * or host's RAM buffer at startup) must read as all-zero from these
 * entry points. This is **different** from raw flash default state
 * (0xFF) — the HAL impl normalises. Violation causes Option.PIN (int)
 * to read as -1 (truthy) and trips the lockdown prompt.
 */
int hal_flash_read_options(void * buf, size_t len);
int hal_flash_write_options(const void * buf, size_t len);

/* Erase the entire on-flash program-memory area (tokenised BASIC). */
int hal_flash_erase_program_area(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
