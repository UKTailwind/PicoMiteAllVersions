/* Stub for host build */
#ifndef _HARDWARE_FLASH_H
#define _HARDWARE_FLASH_H
#include <stdint.h>
#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096
/* XIP memory-map base. On device XIP_BASE is the address where flash is
 * memory-mapped (0x10000000). On host we have no XIP — flash is simulated
 * in flash_prog_buf at its real heap address — so XIP-relative arithmetic
 * inside FileIO.c (fs_flash_read, SaveOptions) collapses to plain buffer
 * offsets. Setting XIP_BASE to 0 keeps those expressions well-formed; the
 * code paths that actually matter on host (flash_range_*) are routed
 * through host_stubs_legacy.c which writes to flash_prog_buf directly. */
#define XIP_BASE 0
/* On the device these hit real flash. On host they simulate flash against
 * the flash_prog_buf in host_main.c, so NEW / SAVE / program-memory edits
 * actually take effect. Implementations live in host_stubs_legacy.c. */
void flash_range_erase(uint32_t off, uint32_t count);
void flash_range_program(uint32_t off, const uint8_t *data, uint32_t count);
/* flash_do_cmd: device-only SPI command to the flash chip (used by
 * ResetOptions to query FlashSize). No host equivalent — stub returns and
 * leaves the rxbuf alone; Option.FlashSize is set from a default elsewhere. */
static inline void flash_do_cmd(const uint8_t *tx, uint8_t *rx, uint32_t n) {
    (void)tx; (void)n;
    if (rx && n >= 4) { rx[0] = rx[1] = rx[2] = 0; rx[3] = 23; /* 8 MB */ }
}
#endif
