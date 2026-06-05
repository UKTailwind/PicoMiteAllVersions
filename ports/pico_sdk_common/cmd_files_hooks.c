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
#include "hal/hal_fatfs_dispatch.h"
#include "hal/hal_keyboard.h"

extern void ErrorThrow(int e, int type);
extern struct uFileTable FileTable[];
extern hal_fs_fd_t hal_fds[];
extern FATFS FatFs;
extern int FatFSFileSystem;
extern volatile BYTE SDCardStat;

/* The historical save/restore dance (SaveContext + ClearVars +
 * InitHeap on entry, RestoreContext on exit) existed to free heap for
 * cmd_files's transient ~76 KB flist[] sort buffer. The multi-pass
 * selection sort that replaced the buffer is O(1) memory, so the
 * dance is no longer needed and would actively harm by wiping the
 * caller's BASIC variables across a `FILES` call from inside a
 * program. */
void cmd_files_save_program_context(void) {}
void cmd_files_restore_program_context(void) {}

void cmd_files_pump_console_key(int * c) {
    /* Device fills ConsoleRxBuf from the UART/USB ISR. The cmd_files loop
     * already drains it — no extra work here. */
    (void)c;
}

void cmd_load_post_cleanup(void) {
    /* Device SaveProgramToFlash uses its own tokeniser buffer and doesn't
     * clobber the in-flight tknbuf; cmd_load returns normally. */
}

void port_drive_check(char drive) {
    if (drive != 'A' && drive != 'B') error("Invalid disk");
    /* Both A: (LFS-on-flash) and B: (FatFS-on-SD) exist on device. A:
     * needs no validation — flash is always present. B: requires the
     * SD-card chip-select pin to be configured (SD_CS or CombinedCS). */
    if (drive == 'B') {
        if (!(Option.SD_CS || Option.CombinedCS))
            error("B: drive not enabled");
    }
}

/* port_drivecheck_remap (identity) + port_filesystem_prefix ("A:"/"B:")
 * live in runtime/runtime_filesystem_defaults.c — shared with host +
 * ESP32. pc386 (FatFs on every volume, no LFS) provides real overrides
 * and doesn't link the shared TU. */

/* hal_ff_* directory + path ops on device just forward to the vendored
 * FatFS in ff.c. Host impls in host/host_fs_shims.c (host_f_*) handle
 * the vm_host_fat / POSIX dispatch. */
FRESULT hal_ff_findfirst(DIR * dp, FILINFO * fi, const TCHAR * path,
                         const TCHAR * pattern) {
    return f_findfirst(dp, fi, path, pattern);
}
FRESULT hal_ff_findnext(DIR * dp, FILINFO * fi) {
    return f_findnext(dp, fi);
}
FRESULT hal_ff_closedir(DIR * dp) {
    return f_closedir(dp);
}
FRESULT hal_ff_unlink(const TCHAR * path) {
    return f_unlink(path);
}
FRESULT hal_ff_chdir(const TCHAR * path) {
    return f_chdir(path);
}
FRESULT hal_ff_getcwd(TCHAR * buf, UINT len) {
    return f_getcwd(buf, len);
}

int port_mount_sd_drive(void) {
    int i;
    ErrorThrow(0, NONEFILE); /* reset mm.errno */
    if ((IsInvalidPin(Option.SD_CS) && !Option.CombinedCS) ||
        (IsInvalidPin(Option.SYSTEM_MOSI) && IsInvalidPin(Option.SD_MOSI_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_MISO) && IsInvalidPin(Option.SD_MISO_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_CLK) && IsInvalidPin(Option.SD_CLK_PIN)))
        error("SDcard not configured");
    if (!(SDCardStat & STA_NOINIT))
        return 1; /* already mounted */
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

/* OPTION KEYBOARD setter — USB and PS/2 builds parse different
 * arg lists and Option fields. Real impl lives here with #ifdef
 * HAL_PORT_KEYBOARD_USB_HOST inside (port impl files allow target gates).  Returns 1
 * if matched (call typically never returns — SoftReset). */
int port_keyboard_option_setter(unsigned char * cmdline) {
    unsigned char * tp = checkstring(cmdline, (unsigned char *)"KEYBOARD");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");
#if !HAL_PORT_KEYBOARD_USB_HOST
    if (checkstring(tp, (unsigned char *)"DISABLE")) {
        Option.KeyboardConfig = NO_KEYBOARD;
        Option.capslock = 0;
        Option.numlock = 0;
        Option.KEYBOARD_CLOCK = 0;
        Option.KEYBOARD_DATA = 0;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    getargs(&tp, 9, (unsigned char *)",");
#if !HAL_PORT_KEYBOARD_USB_HOST
    if (!Option.KEYBOARD_CLOCK) {
        Option.KEYBOARD_CLOCK = KEYBOARDCLOCK;
        Option.KEYBOARD_DATA = KEYBOARDDATA;
    }
    if (ExtCurrentConfig[Option.KEYBOARD_CLOCK] != EXT_NOT_CONFIG && Option.KeyboardConfig == NO_KEYBOARD)
        error("Pin %/| is in use", Option.KEYBOARD_CLOCK, Option.KEYBOARD_CLOCK);
    if (ExtCurrentConfig[Option.KEYBOARD_DATA] != EXT_NOT_CONFIG && Option.KeyboardConfig == NO_KEYBOARD)
        error("Pin %/| is in use", Option.KEYBOARD_DATA, Option.KEYBOARD_DATA);
#endif
    int hal_kbd_layout = -1;
    if (checkstring(argv[0], (unsigned char *)"US"))
        hal_kbd_layout = HAL_KBD_LAYOUT_US;
    else if (checkstring(argv[0], (unsigned char *)"FR"))
        hal_kbd_layout = HAL_KBD_LAYOUT_FR;
    else if (checkstring(argv[0], (unsigned char *)"GR"))
        hal_kbd_layout = HAL_KBD_LAYOUT_GR;
    else if (checkstring(argv[0], (unsigned char *)"IT"))
        hal_kbd_layout = HAL_KBD_LAYOUT_IT;
    else if (checkstring(argv[0], (unsigned char *)"UK"))
        hal_kbd_layout = HAL_KBD_LAYOUT_UK;
    else if (checkstring(argv[0], (unsigned char *)"ES"))
        hal_kbd_layout = HAL_KBD_LAYOUT_ES;
    else if (checkstring(argv[0], (unsigned char *)"BE"))
        hal_kbd_layout = HAL_KBD_LAYOUT_BE;
    else if (checkstring(argv[0], (unsigned char *)"BR"))
        hal_kbd_layout = HAL_KBD_LAYOUT_BR;
    else if (checkstring(argv[0], (unsigned char *)"I2C"))
        hal_kbd_layout = HAL_KBD_LAYOUT_I2C;
    if (hal_kbd_layout < 0 || hal_keyboard_set_layout(hal_kbd_layout) != 0) error("Syntax");
    Option.capslock = 0;
    Option.numlock = 1;
#if !HAL_PORT_KEYBOARD_USB_HOST
    int rs = 0b00100000;
    int rr = 0b00001100;
    if (Option.KeyboardConfig != CONFIG_I2C) {
        if (argc >= 3 && *argv[2]) Option.capslock = getint(argv[2], 0, 1);
        if (argc >= 5 && *argv[4]) Option.numlock = getint(argv[4], 0, 1);
        if (argc >= 7 && *argv[6]) rs = getint(argv[6], 0, 3) << 5;
        if (argc == 9 && *argv[8]) rr = getint(argv[8], 0, 31);
        Option.repeat = rs | rr;
    } else {
        if (!Option.SYSTEM_I2C_SCL) error("Option System I2C not set");
    }
#else
    if (argc >= 3 && *argv[2]) Option.capslock = getint(argv[2], 0, 1);
    if (argc >= 5 && *argv[4]) Option.numlock = getint(argv[4], 0, 1);
    if (argc >= 7 && *argv[6]) Option.RepeatStart = getint(argv[6], 100, 2000);
    if (argc >= 9 && *argv[8]) Option.RepeatRate = getint(argv[8], 25, 2000);
#endif
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return 1;
}

#if !HAL_PORT_KEYBOARD_USB_HOST
/* `Update Firmware` jumps to BOOTSEL on non-USB-keyboard device builds.
 * USB-keyboard variants register `Gamepad` in the same commandtbl slot
 * (AllCommands.h gates the entry), so cmd_update is never referenced
 * and doesn't need to exist. */
#include "pico/bootrom.h"
#include "pico/stdio_usb.h"
#include "pico/stdio_usb/reset_interface.h"
void MIPS16 cmd_update(void) {
    uint gpio_mask = 0u;
    reset_usb_boot(gpio_mask, PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK);
}
#endif
