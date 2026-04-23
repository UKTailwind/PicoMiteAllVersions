/*
 * ports/pico_sdk_common/cmd_files_hooks.c — device-side implementations
 * of FileIO.c's command-level lifecycle hooks.
 *
 * On device:
 *   - cmd_files allocates ~76 KB for its sort buffer. When invoked from
 *     inside a running program (CurrentLinePtr != NULL) we save the
 *     interpreter's variable/heap state to flash (or PSRAM on RP2350)
 *     via SaveContext, wipe + re-init the heap, then RestoreContext on
 *     the way out. Host can't do this — bc_alloc backs both the heap
 *     and the live VMState.
 *   - The "PRESS ANY KEY" pump in cmd_files reads keys via the
 *     interrupt-filled ConsoleRxBuf; no extra polling needed. Host has
 *     no ISR and explicitly polls MMInkey + sleeps.
 *   - cmd_load returns to its caller normally; the host needs an extra
 *     longjmp because its SaveProgramToFlash stub clobbers tknbuf.
 *
 * Host implementations live in host/host_runtime.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ff.h"
#include "diskio.h"

extern void CloseAudio(int all);
extern void SaveContext(void);
extern void RestoreContext(bool keep);
extern void ErrorThrow(int e, int type);
extern struct uFileTable FileTable[];
extern hal_fs_fd_t hal_fds[];
extern FATFS FatFs;
extern int FatFSFileSystem;
extern volatile BYTE SDCardStat;

void cmd_files_save_program_context(void)
{
    if (!CurrentLinePtr) return;
    CloseAudio(1);
    SaveContext();
    ClearVars(0, false);
    InitHeap(false);
}

void cmd_files_restore_program_context(void)
{
    if (CurrentLinePtr) RestoreContext(false);
}

void cmd_files_pump_console_key(int *c)
{
    /* Device fills ConsoleRxBuf from the UART/USB ISR. The cmd_files loop
     * already drains it — no extra work here. */
    (void)c;
}

void cmd_load_post_cleanup(void)
{
    /* Device SaveProgramToFlash uses its own tokeniser buffer and doesn't
     * clobber the in-flight tknbuf; cmd_load returns normally. */
}

int port_mount_sd_drive(void)
{
    int i;
    ErrorThrow(0, NONEFILE);  /* reset mm.errno */
    if ((IsInvalidPin(Option.SD_CS) && !Option.CombinedCS) ||
        (IsInvalidPin(Option.SYSTEM_MOSI) && IsInvalidPin(Option.SD_MOSI_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_MISO) && IsInvalidPin(Option.SD_MISO_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_CLK)  && IsInvalidPin(Option.SD_CLK_PIN)))
        error("SDcard not configured");
    if (!(SDCardStat & STA_NOINIT))
        return 1;  /* already mounted */
    for (i = 0; i < MAXOPENFILES; i++)
        if (FileTable[i].com > MAXCOMPORTS && hal_fds[i] != 0)
            ForceFileClose(i);
    i = f_mount(&FatFs, "", 1);
    if (i) {
        FatFSFileSystem = 0;
        ErrorThrow(i, FATFSFILE);
        return 0;
    }
    return 2;
}
