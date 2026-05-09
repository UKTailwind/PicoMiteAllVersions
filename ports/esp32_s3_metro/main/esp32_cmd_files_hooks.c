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
 * Currently linked alongside host_runtime.c's same-named symbols via
 * --allow-multiple-definition + --wrap=port_drive_check. After
 * D-decouple Step C drops host_native from the link, the wrap goes
 * away and these become the sole strong definitions.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Drive availability. A: is LittleFS on internal flash (always present
 * on a booted board). B: would be FatFs on SD; the Adafruit Metro
 * doesn't expose an SD slot, so reject it explicitly with a familiar
 * error message rather than letting cmd_files probe a non-existent
 * mount and produce a confusing "Could not find file" later. */
void port_drive_check(char drive) {
    if (drive == 'B') error("B: drive not configured on this board");
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
