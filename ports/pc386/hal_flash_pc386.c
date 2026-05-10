/*
 * ports/pc386/hal_flash_pc386.c — flash HAL over a BSS-resident
 * RAM buffer.
 *
 * The PC has no flash chip in the device-MCU sense; ProgMemory and
 * the Option block live in DRAM and lose state across reboot. The
 * BASIC `SAVE` command persists to FAT (A:\PROG.BAS etc.) — the
 * flash HAL is just for in-RAM staging.
 *
 * Placeholder for sub-stage 3a — every entry panics. Real impl in
 * 3c. The buffer layout will mirror host_native (program area
 * first, options block second, both inside a single static array).
 */

#include "hal/hal_flash.h"
#include "pc386_panic.h"

int hal_flash_erase(uint32_t offset, size_t len)
{
    (void)offset; (void)len;
    pc386_panic("hal_flash_erase not yet implemented (3c)");
}

int hal_flash_program(uint32_t offset, const void *buf, size_t len)
{
    (void)offset; (void)buf; (void)len;
    pc386_panic("hal_flash_program not yet implemented (3c)");
}

int hal_flash_unique_id(uint8_t out[8])
{
    (void)out;
    pc386_panic("hal_flash_unique_id not yet implemented (3c)");
}

int hal_flash_read_jedec_id(uint8_t out[4])
{
    (void)out;
    pc386_panic("hal_flash_read_jedec_id not yet implemented (3c)");
}

void hal_flash_write_begin(void) {}
void hal_flash_write_end  (void) {}
int  hal_flash_write_active(void) { return 0; }

int hal_flash_read_options(void *buf, size_t len)
{
    (void)buf; (void)len;
    pc386_panic("hal_flash_read_options not yet implemented (3c)");
}

int hal_flash_write_options(const void *buf, size_t len)
{
    (void)buf; (void)len;
    pc386_panic("hal_flash_write_options not yet implemented (3c)");
}

int hal_flash_erase_program_area(void)
{
    pc386_panic("hal_flash_erase_program_area not yet implemented (3c)");
}
