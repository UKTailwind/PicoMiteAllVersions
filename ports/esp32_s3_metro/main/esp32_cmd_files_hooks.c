/*
 * esp32_cmd_files_hooks.c — ESP32 filesystem hooks for cmd_files.c.
 *
 * Three port_* hooks core code calls when handling drive-letter ops:
 *
 *   port_drive_check(drive)       — error if the drive isn't valid here.
 *   port_mount_sd_drive(void)     — mount B: (Metro onboard microSD).
 *   port_apply_load_overrides()   — board-specific Option fixups after
 *                                   LoadOptions().
 *
 * These are sole strong definitions in the ESP32 source list. Do not
 * reintroduce --wrap or link-order overrides for these hooks.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "FileIO.h"
#include "diskio.h"
#include "ff.h"

extern FATFS FatFs;
extern int FatFSFileSystem;
extern volatile BYTE SDCardStat;
extern hal_fs_fd_t hal_fds[];
extern void ErrorThrow(int e, int type);

/* Drive availability. A: is LittleFS on internal flash; B: is FatFs on
 * the Adafruit Metro ESP32-S3 onboard microSD slot. */
void port_drive_check(char drive) {
    (void)drive;
}

int port_mount_sd_drive(void) {
    ErrorThrow(0, NONEFILE);  /* reset mm.errno */
    if (!(SDCardStat & STA_NOINIT))
        return 1;  /* already mounted */
    for (int i = 0; i < MAXOPENFILES; i++)
        if (FileTable[i].com > MAXCOMPORTS && hal_fds[i] != 0)
            ForceFileClose(i);
    FRESULT r = f_mount(&FatFs, "", 1);
    if (r != FR_OK) {
        FatFSFileSystem = 0;
        ErrorThrow(r, FATFSFILE);
        return 0;
    }
    return 2;
}

/* Per-board Option fixups after LoadOptions() reads the saved blob.
 * Pico ports use this to force PicoCalc / VGA / HDMI display defaults
 * before the runtime starts drawing. ESP32 stdio has no display
 * configuration; the LoadOptions read into Option is enough. */
void port_apply_load_overrides(void) {}
