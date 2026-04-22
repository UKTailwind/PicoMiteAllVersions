/*
 * ports/pico_sdk_common/hal_flash_pico.c — hal_flash over the Pico SDK.
 *
 * Shared across every RP2040/RP2350 device target; these primitives are
 * hardware-agnostic beyond "the device runs on a Pico SDK that provides
 * hardware_flash".
 *
 * The unique-board-id routine here queries the flash chip's JEDEC unique-ID
 * register via `flash_get_unique_id`; this matches what the existing
 * MM.DEVICE$(ID) path uses. Note that `pico_get_unique_board_id_string` is
 * a string-formatted wrapper over the same underlying bytes.
 */

#include <string.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"  /* XIP_BASE */

#include "configuration.h"             /* FLASH_TARGET_OFFSET, PROGSTART, MAX_PROG_SIZE, FLASH_ERASE_SIZE */
#include "hal/hal_flash.h"

int hal_flash_erase(uint32_t offset, size_t len)
{
    if (len == 0) return 0;
    if ((offset % FLASH_SECTOR_SIZE) != 0) return -EINVAL;
    if ((len    % FLASH_SECTOR_SIZE) != 0) return -EINVAL;

    uint32_t irqs = save_and_disable_interrupts();
    flash_range_erase(offset, len);
    restore_interrupts(irqs);
    return 0;
}

int hal_flash_program(uint32_t offset, const void *buf, size_t len)
{
    if (len == 0) return 0;
    if (buf == NULL)                    return -EINVAL;
    if ((offset % FLASH_PAGE_SIZE) != 0) return -EINVAL;
    if ((len    % FLASH_PAGE_SIZE) != 0) return -EINVAL;

    uint32_t irqs = save_and_disable_interrupts();
    flash_range_program(offset, (const uint8_t *)buf, len);
    restore_interrupts(irqs);
    return 0;
}

int hal_flash_unique_id(uint8_t out[8])
{
    if (out == NULL) return -EINVAL;
    flash_get_unique_id(out);
    return 0;
}

int hal_flash_read_options(void *buf, size_t len)
{
    if (buf == NULL) return -EINVAL;
    memcpy(buf, (const void *)(XIP_BASE + FLASH_TARGET_OFFSET), len);
    return 0;
}

int hal_flash_write_options(const void *buf, size_t len)
{
    if (buf == NULL) return -EINVAL;
    /* Erase one 4K sector at FLASH_TARGET_OFFSET, then program the buffer.
     * Current call sites use either sizeof(struct option_s) or a padded
     * 768 bytes; both are well under FLASH_ERASE_SIZE. Caller must have
     * rounded `len` up to a multiple of FLASH_PAGE_SIZE (256). */
    int rc = hal_flash_erase(FLASH_TARGET_OFFSET, FLASH_ERASE_SIZE);
    if (rc != 0) return rc;
    return hal_flash_program(FLASH_TARGET_OFFSET, buf, len);
}

int hal_flash_erase_program_area(void)
{
    return hal_flash_erase(PROGSTART, MAX_PROG_SIZE);
}
