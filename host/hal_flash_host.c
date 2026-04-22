/*
 * host/hal_flash_host.c — hal_flash over the host's RAM-backed flash shim.
 *
 * The native-host build has no real flash; it simulates one with the
 * backing buffers in host_fs_shims.c (flash_prog_buf, host_flash_target_buf,
 * host_flash_option_buf). This TU wraps those buffers behind the hal_flash
 * contract so core code stops calling the SDK-shaped flash_range_* shims
 * directly.
 *
 * Erase on host = 0xFF-fill, matching device erase semantics; program =
 * memcpy. Option reads come from the RAM snapshot populated at boot by
 * host_options_snapshot(). The freshness contract (Option buffer reads as
 * all-zero before anything is written) is honoured by host_fs_shims.c's
 * constructor zero-fill of host_flash_option_buf.
 */

#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_flash.h"

/* Existing host shims (defined in host_fs_shims.c). These have Pico-SDK
 * signatures so core code that still calls flash_range_erase /
 * flash_range_program keeps compiling; we route through them so there's a
 * single source of truth for the backing-buffer semantics. */
extern void flash_range_erase  (uint32_t off, uint32_t count);
extern void flash_range_program(uint32_t off, const uint8_t *data, uint32_t count);
extern const uint8_t *flash_option_contents;
extern void host_options_snapshot(void);

int hal_flash_erase(uint32_t offset, size_t len)
{
    if (len == 0) return 0;
    flash_range_erase(offset, (uint32_t)len);
    return 0;
}

int hal_flash_program(uint32_t offset, const void *buf, size_t len)
{
    if (len == 0) return 0;
    if (buf == NULL) return -EINVAL;
    flash_range_program(offset, (const uint8_t *)buf, (uint32_t)len);
    return 0;
}

int hal_flash_unique_id(uint8_t out[8])
{
    if (out == NULL) return -EINVAL;
    /* Host has no hardware unique ID. Return a fixed recognisable value;
     * if a caller ever needs a stable-per-install ID, this is the place
     * to derive one (e.g. hash of /etc/machine-id). */
    static const uint8_t host_id[8] = { 'H','O','S','T','B','U','I','L' };
    memcpy(out, host_id, 8);
    return 0;
}

int hal_flash_read_options(void *buf, size_t len)
{
    if (buf == NULL) return -EINVAL;
    memcpy(buf, flash_option_contents, len);
    return 0;
}

int hal_flash_write_options(const void *buf, size_t len)
{
    if (buf == NULL) return -EINVAL;
    /* The host keeps Option and its flash-backing buffer in sync via
     * host_options_snapshot(). When core calls SaveOptions() after
     * mutating Option, we just refresh the snapshot — no erase / program
     * dance is needed since reads come from the same RAM buffer. */
    (void)len;
    host_options_snapshot();
    return 0;
}

int hal_flash_erase_program_area(void)
{
    /* Program area on host starts at offset 0 in flash_prog_buf and runs
     * for MAX_PROG_SIZE bytes — matching host_main.c's initial erase. */
    flash_range_erase(0, MAX_PROG_SIZE);
    return 0;
}
