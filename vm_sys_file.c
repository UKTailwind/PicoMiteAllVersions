/*
 * vm_sys_file.c — device-side impl of the VM file syscalls.
 *
 * Drives FatFS (ff.c) over the SD-card HAL. Paired with
 * host/vm_sys_file_host.c (which uses the in-memory FAT simulator
 * from host/vm_host_fat.c); the build links exactly one of the two
 * per target. Shared helpers (vm_file_line_append, drive-spec
 * parser, path normaliser) are `static inline` in
 * vm_sys_file_internal.h.
 */

#include "vm_sys_file_internal.h"


typedef struct {
    unsigned char source;
    unsigned char mode;
    FIL *fat;
    lfs_file_t *flash;
} VMFileHandle;

static VMFileHandle vm_files[MAXOPENFILES + 1];

extern int FatFSFileSystem;
extern int FatFSFileSystemSave;
extern char filepath[2][FF_MAX_LFN];
extern int FSerror;

static void vm_file_check_number(int fnbr) {
    if (fnbr < 1 || fnbr > MAXOPENFILES) error("File number");
}

static void vm_file_error(int err, const char *msg) {
    FSerror = err;
    error((char *)msg);
}

static int vm_file_current_fs(void) {
    return FatFSFileSystemSave ? FatFSFileSystemSave : FatFSFileSystem;
}

/*
 * Resolve a filename to a full path and file system index.
 * Adapted from legacy getfullfilename / fullpath (FileIO.c).
 *
 * The legacy path model:
 *  - filepath[0] = "A:/" (flash/LFS), filepath[1] = "B:/" (SD/FatFS)
 *  - fullpathname[] stores resolved paths WITHOUT drive prefix (e.g. "/")
 *  - FatFS receives paths without volume prefix; the active file system
 *    is determined by FatFSFileSystem, not by the path string.
 *  - Drive prefixes like "B:" are stripped before passing to f_open/f_opendir.
 */
static void vm_file_resolve_path(const char *filename, int *fs_out, char *path) {
    int fs = vm_file_current_fs();
    const char *name = filename;

    /* Handle drive spec: "A:file" or "B:file" */
    if (vm_file_is_drive_spec(filename)) {
        fs = vm_file_drive_index(filename);
        name = filename + 2;  /* skip "X:" */
    }
    if (*name == '\0') {
        /* bare drive spec like "B:" → resolve to root */
        strcpy(path, "/");
        *fs_out = fs;
        return;
    }

    if (*name == '/') {
        /* Absolute path — use as-is (no drive prefix) */
        strncpy(path, name, FF_MAX_LFN - 1);
        path[FF_MAX_LFN - 1] = '\0';
    } else {
        /* Relative path — prepend CWD.
         * filepath[fs] has format "X:/subdir", strip the "X:" prefix
         * to get the bare path that FatFS/LFS expects. */
        const char *base = filepath[fs];
        /* Skip drive prefix if present */
        if (base[0] && base[1] == ':') base += 2;
        if (*base == '\0') base = "/";

        strncpy(path, base, FF_MAX_LFN - 1);
        path[FF_MAX_LFN - 1] = '\0';
        if (path[strlen(path) - 1] != '/')
            strncat(path, "/", FF_MAX_LFN - strlen(path) - 1);
        strncat(path, name, FF_MAX_LFN - strlen(path) - 1);
    }

    vm_file_normalize_resolved_path(path);
    *fs_out = fs;
}

static int vm_file_lfs_mode(int mode) {
    switch (mode) {
        case VM_FILE_MODE_INPUT:
            return LFS_O_RDONLY;
        case VM_FILE_MODE_OUTPUT:
            return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
        case VM_FILE_MODE_APPEND:
            return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
        case VM_FILE_MODE_RANDOM:
            return LFS_O_RDWR | LFS_O_CREAT;
        default:
            error("File access mode");
            return 0;
    }
}

static int vm_file_fat_mode(int mode) {
    switch (mode) {
        case VM_FILE_MODE_INPUT:
            return FA_READ;
        case VM_FILE_MODE_OUTPUT:
            return FA_WRITE | FA_CREATE_ALWAYS;
        case VM_FILE_MODE_APPEND:
            return FA_WRITE | FA_OPEN_APPEND;
        case VM_FILE_MODE_RANDOM:
            return FA_WRITE | FA_OPEN_APPEND | FA_READ;
        default:
            error("File access mode");
            return 0;
    }
}

static int vm_file_lfs_exists_dir(const char *path) {
    lfs_dir_t dir;
    int rc = lfs_dir_open(&lfs, &dir, path);
    if (rc < 0) return 0;
    lfs_dir_close(&lfs, &dir);
    return 1;
}

static void vm_file_set_current_path(int fs, const char *path) {
    strncpy(filepath[fs], path, FF_MAX_LFN - 1);
    filepath[fs][FF_MAX_LFN - 1] = '\0';
}

void vm_sys_file_open(const char *filename, int fnbr, int mode) {
    char path[FF_MAX_LFN] = {0};
    int fs;

    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source != NONEFILE) error("File number already open");
    vm_file_resolve_path(filename, &fs, path);

    if (fs) {
        if (!InitSDCard()) error("SD Card not found");
        vm_files[fnbr].fat = (FIL *)BC_ALLOC(sizeof(FIL));
        if (!vm_files[fnbr].fat) error("NEM[file:fil_fat] want=%", (int)sizeof(FIL));
        FSerror = f_open(vm_files[fnbr].fat, path, (BYTE)vm_file_fat_mode(mode));
        if (FSerror) {
            BC_FREE(vm_files[fnbr].fat);
            memset(&vm_files[fnbr], 0, sizeof(vm_files[fnbr]));
            vm_file_error(FSerror, "File error");
        }
        vm_files[fnbr].source = FATFSFILE;
    } else {
        vm_files[fnbr].flash = (lfs_file_t *)BC_ALLOC(sizeof(lfs_file_t));
        if (!vm_files[fnbr].flash) error("NEM[file:fil_lfs] want=%", (int)sizeof(lfs_file_t));
        FSerror = lfs_file_open(&lfs, vm_files[fnbr].flash, path, vm_file_lfs_mode(mode));
        if (FSerror < 0) {
            BC_FREE(vm_files[fnbr].flash);
            memset(&vm_files[fnbr], 0, sizeof(vm_files[fnbr]));
            vm_file_error(FSerror, "File error");
        }
        if (mode == VM_FILE_MODE_APPEND)
            lfs_file_seek(&lfs, vm_files[fnbr].flash,
                          lfs_file_size(&lfs, vm_files[fnbr].flash),
                          LFS_SEEK_SET);
        vm_files[fnbr].source = FLASHFILE;
    }
    vm_files[fnbr].mode = (unsigned char)mode;
}

void vm_sys_file_close(int fnbr) {
    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source == NONEFILE) error("File number is not open");
    if (vm_files[fnbr].source == FATFSFILE) {
        FSerror = f_close(vm_files[fnbr].fat);
        BC_FREE(vm_files[fnbr].fat);
        if (FSerror) {
            memset(&vm_files[fnbr], 0, sizeof(vm_files[fnbr]));
            vm_file_error(FSerror, "File error");
        }
    } else {
        FSerror = lfs_file_close(&lfs, vm_files[fnbr].flash);
        BC_FREE(vm_files[fnbr].flash);
        if (FSerror < 0) {
            memset(&vm_files[fnbr], 0, sizeof(vm_files[fnbr]));
            vm_file_error(FSerror, "File error");
        }
    }
    memset(&vm_files[fnbr], 0, sizeof(vm_files[fnbr]));
}

void vm_sys_file_reset(void) {
    for (int i = 1; i <= MAXOPENFILES; i++) {
        if (vm_files[i].source != NONEFILE) {
            if (vm_files[i].source == FATFSFILE) {
                f_close(vm_files[i].fat);
                BC_FREE(vm_files[i].fat);
            } else {
                lfs_file_close(&lfs, vm_files[i].flash);
                BC_FREE(vm_files[i].flash);
            }
            memset(&vm_files[i], 0, sizeof(vm_files[i]));
        }
    }
}

void vm_sys_file_print_buf(int fnbr, const char *buf, int len) {
    unsigned int wrote = 0;
    int rc;

    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source == NONEFILE) error("File number is not open");
    if (len <= 0) return;

    if (vm_files[fnbr].source == FATFSFILE) {
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_write(vm_files[fnbr].fat, buf, (UINT)len, &wrote);
        if (FSerror || wrote != (unsigned int)len) vm_file_error(FSerror, "File error");
        diskchecktimer = DISKCHECKRATE;
    } else {
        rc = lfs_file_write(&lfs, vm_files[fnbr].flash, buf, len);
        if (rc != len) vm_file_error(rc < 0 ? rc : -5, "File error");
    }
}

void vm_sys_file_print_str(int fnbr, const uint8_t *mstr) {
    vm_sys_file_print_buf(fnbr, (const char *)mstr + 1, mstr[0]);
}

void vm_sys_file_print_newline(int fnbr) {
    vm_sys_file_print_buf(fnbr, "\r\n", 2);
}

int vm_sys_file_eof(int fnbr) {
    if (vm_files[fnbr].source == FATFSFILE)
        return f_eof(vm_files[fnbr].fat);
    return lfs_file_tell(&lfs, vm_files[fnbr].flash) >=
           lfs_file_size(&lfs, vm_files[fnbr].flash);
}

int vm_sys_file_lof(int fnbr) {
    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source == NONEFILE) return 0;
    if (vm_files[fnbr].source == FATFSFILE)
        return (int)f_size(vm_files[fnbr].fat);
    return (int)lfs_file_size(&lfs, vm_files[fnbr].flash);
}

int vm_sys_file_getc(int fnbr) {
    char ch = 0;
    unsigned int read = 0;
    int rc;

    if (vm_files[fnbr].source == FATFSFILE) {
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_read(vm_files[fnbr].fat, &ch, 1, &read);
        if (FSerror) vm_file_error(FSerror, "File error");
        diskchecktimer = DISKCHECKRATE;
        return read == 1 ? (unsigned char)ch : -1;
    }

    rc = lfs_file_read(&lfs, vm_files[fnbr].flash, &ch, 1);
    if (rc < 0) vm_file_error(rc, "File error");
    return rc == 1 ? (unsigned char)ch : -1;
}

void vm_sys_file_line_input(int fnbr, uint8_t *dest) {
    int len = 0;
    int ch;
    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source == NONEFILE) error("File number is not open");
    while (len < MAXSTRLEN && !vm_sys_file_eof(fnbr)) {
        ch = vm_sys_file_getc(fnbr);
        if (ch < 0) break;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        vm_file_line_append(dest, &len, ch);
    }
    dest[0] = (uint8_t)len;
}

void vm_sys_file_drive(const char *drive) {
    FatFSFileSystem = vm_file_drive_index(drive);
}

void vm_sys_file_seek(int fnbr, int position) {
    vm_file_check_number(fnbr);
    if (vm_files[fnbr].source == NONEFILE) error("File number is not open");
    if (position < 1) position = 1;
    if (vm_files[fnbr].source == FATFSFILE) {
        FSerror = f_lseek(vm_files[fnbr].fat, (FSIZE_t)(position - 1));
        if (FSerror) vm_file_error(FSerror, "File error");
    } else {
        FSerror = lfs_file_seek(&lfs, vm_files[fnbr].flash, position - 1, LFS_SEEK_SET);
        if (FSerror < 0) vm_file_error(FSerror, "File error");
    }
}

void vm_sys_file_mkdir(const char *path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    vm_file_resolve_path(path, &fs, full);
    if (fs) {
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_mkdir(full);
        if (FSerror) vm_file_error(FSerror, "File error");
    } else {
        FSerror = lfs_mkdir(&lfs, full);
        if (FSerror < 0) vm_file_error(FSerror, "File error");
    }
}

void vm_sys_file_chdir(const char *path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    vm_file_resolve_path(path, &fs, full);
    if (fs) {
        DIR dj;
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_opendir(&dj, full);
        if (FSerror) vm_file_error(FSerror, "File error");
        f_closedir(&dj);
    } else {
        if (!vm_file_lfs_exists_dir(full)) vm_file_error(-1, "File error");
    }
    FatFSFileSystem = fs;
    vm_file_set_current_path(fs, full);
}

void vm_sys_file_rmdir(const char *path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    vm_file_resolve_path(path, &fs, full);
    if (fs) {
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_unlink(full);
        if (FSerror) vm_file_error(FSerror, "File error");
    } else {
        FSerror = lfs_remove(&lfs, full);
        if (FSerror < 0) vm_file_error(FSerror, "File error");
    }
}

void vm_sys_file_kill(const char *path) {
    vm_sys_file_rmdir(path);
}

void vm_sys_file_rename(const char *old_name, const char *new_name) {
    int old_fs, new_fs;
    char old_full[FF_MAX_LFN] = {0};
    char new_full[FF_MAX_LFN] = {0};
    vm_file_resolve_path(old_name, &old_fs, old_full);
    vm_file_resolve_path(new_name, &new_fs, new_full);
    if (old_fs != new_fs) error("Only valid on current drive");
    if (old_fs) {
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_rename(old_full, new_full);
        if (FSerror) vm_file_error(FSerror, "File error");
    } else {
        FSerror = lfs_rename(&lfs, old_full, new_full);
        if (FSerror < 0) vm_file_error(FSerror, "File error");
    }
}

void vm_sys_file_copy(const char *from_name, const char *to_name, int mode) {
    int from_fs, to_fs;
    char from_full[FF_MAX_LFN] = {0};
    char to_full[FF_MAX_LFN] = {0};
    FIL fat_src, fat_dst;
    lfs_file_t lfs_src, lfs_dst;
    BYTE buffer[512];
    int lrc;

    (void)mode;
    vm_file_resolve_path(from_name, &from_fs, from_full);
    vm_file_resolve_path(to_name, &to_fs, to_full);

    if (from_fs && to_fs) {  /* both SD */
        if (!InitSDCard()) error("SD Card not found");
        FSerror = f_open(&fat_src, from_full, FA_READ);
        if (FSerror) vm_file_error(FSerror, "File error");
        FSerror = f_open(&fat_dst, to_full, FA_WRITE | FA_CREATE_ALWAYS);
        if (FSerror) {
            f_close(&fat_src);
            vm_file_error(FSerror, "File error");
        }
        while (1) {
            UINT read = 0, wrote = 0;
            FSerror = f_read(&fat_src, buffer, sizeof(buffer), &read);
            if (FSerror || read == 0) break;
            FSerror = f_write(&fat_dst, buffer, read, &wrote);
            if (FSerror || wrote != read) break;
        }
        f_close(&fat_src);
        f_close(&fat_dst);
        if (FSerror) vm_file_error(FSerror, "File error");
        return;
    }

    if (!from_fs && !to_fs) {  /* both flash */
        lrc = lfs_file_open(&lfs, &lfs_src, from_full, LFS_O_RDONLY);
        if (lrc < 0) vm_file_error(lrc, "File error");
        lrc = lfs_file_open(&lfs, &lfs_dst, to_full, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (lrc < 0) {
            lfs_file_close(&lfs, &lfs_src);
            vm_file_error(lrc, "File error");
        }
        while (1) {
            lrc = lfs_file_read(&lfs, &lfs_src, buffer, sizeof(buffer));
            if (lrc < 0) break;
            if (lrc == 0) break;
            if (lfs_file_write(&lfs, &lfs_dst, buffer, lrc) != lrc) {
                lrc = -5;
                break;
            }
        }
        lfs_file_close(&lfs, &lfs_src);
        lfs_file_close(&lfs, &lfs_dst);
        if (lrc < 0) vm_file_error(lrc, "File error");
        return;
    }

    if (!from_fs && to_fs) {  /* flash → SD */
        if (!InitSDCard()) error("SD Card not found");
        lrc = lfs_file_open(&lfs, &lfs_src, from_full, LFS_O_RDONLY);
        if (lrc < 0) vm_file_error(lrc, "File error");
        FSerror = f_open(&fat_dst, to_full, FA_WRITE | FA_CREATE_ALWAYS);
        if (FSerror) {
            lfs_file_close(&lfs, &lfs_src);
            vm_file_error(FSerror, "File error");
        }
        while (1) {
            lrc = lfs_file_read(&lfs, &lfs_src, buffer, sizeof(buffer));
            if (lrc < 0) break;
            if (lrc == 0) break;
            UINT wrote = 0;
            FSerror = f_write(&fat_dst, buffer, (UINT)lrc, &wrote);
            if (FSerror || wrote != (UINT)lrc) break;
        }
        lfs_file_close(&lfs, &lfs_src);
        f_close(&fat_dst);
        if (lrc < 0 || FSerror) vm_file_error(lrc < 0 ? lrc : FSerror, "File error");
        return;
    }

    if (!InitSDCard()) error("SD Card not found");
    FSerror = f_open(&fat_src, from_full, FA_READ);
    if (FSerror) vm_file_error(FSerror, "File error");
    lrc = lfs_file_open(&lfs, &lfs_dst, to_full, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (lrc < 0) {
        f_close(&fat_src);
        vm_file_error(lrc, "File error");
    }
    while (1) {
        UINT read = 0;
        FSerror = f_read(&fat_src, buffer, sizeof(buffer), &read);
        if (FSerror || read == 0) break;
        if (lfs_file_write(&lfs, &lfs_dst, buffer, read) != (int)read) {
            lrc = -5;
            break;
        }
    }
    f_close(&fat_src);
    lfs_file_close(&lfs, &lfs_dst);
    if (lrc < 0 || FSerror) vm_file_error(lrc < 0 ? lrc : FSerror, "File error");
}

