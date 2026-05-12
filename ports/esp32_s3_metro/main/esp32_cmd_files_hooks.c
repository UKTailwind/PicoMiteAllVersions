/*
 * esp32_cmd_files_hooks.c — A:-only filesystem hooks for cmd_files.c.
 *
 * Three port_* hooks core code calls when handling drive-letter ops:
 *
 *   port_drive_check(drive)       — error if the drive isn't valid here.
 *   port_mount_sd_drive(void)     — mount B: (SD-card). N/A on this board.
 *   port_apply_load_overrides()   — board-specific Option fixups after
 *                                   LoadOptions().
 *
 * These are sole strong definitions in the ESP32 source list. Do not
 * reintroduce --wrap or link-order overrides for these hooks.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Drive availability. A: is LittleFS on internal flash (always present
 * on a booted board). B: would be FatFs on SD; the Adafruit Metro
 * doesn't expose an SD slot, so reject it explicitly with a familiar
 * error message rather than letting cmd_files probe a non-existent
 * mount and produce a confusing "Could not find file" later. */
void port_drive_check(char drive) {
    if (drive != 'A' && drive != 'B') error("Invalid disk");
    if (drive == 'B') error("B: drive not configured on this board");
}

/* Default drivecheck remap: identity. Pc386 (which has FatFs on every
 * volume and no LFS) overrides this. ESP32 keeps A:=LFS / B:=FatFs. */
int port_drivecheck_remap(int t) { return t; }

const char *port_filesystem_prefix(int filesystem) {
    return filesystem ? "B:" : "A:";
}

/* Mount B:. No-op success on ESP32 stdio scope — there's nothing to
 * mount. cmd_drive's typical contract is "return 0 on success"; the
 * actual SD code on pico returns 0 too. */
int port_mount_sd_drive(void) {
    return 0;
}

/* Per-board Option fixups after LoadOptions() reads the saved blob.
 * Pico ports use this to force PicoCalc / VGA / HDMI display defaults
 * before the runtime starts drawing. ESP32 stdio has no display
 * configuration; the LoadOptions read into Option is enough. */
void port_apply_load_overrides(void) {}
