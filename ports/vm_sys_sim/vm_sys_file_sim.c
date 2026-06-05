/*
 * ports/vm_sys_sim/vm_sys_file_sim.c — simulator impl of the VM's file syscalls.
 *
 * Backs OPEN / CLOSE / INPUT / PRINT / LINE INPUT / EOF / CHR$ etc.
 * with the in-memory FAT simulator from runtime/vm/vm_host_fat.c.
 * Paired with runtime/vm/vm_sys_file.c (device-side FatFS/LFS impl); the build
 * links exactly one implementation body per target.
 *
 * Shared helpers (vm_file_line_append, vm_file_is_drive_spec,
 * vm_file_drive_index, vm_file_normalize_resolved_path) live as
 * `static inline` in vm_sys_file_internal.h so both TUs get their
 * own copies.
 */

#include "vm_sys_file_internal.h"
#include "vm_host_fat.h"

static FIL * vm_files[MAXOPENFILES + 1];
static int vm_current_drive = 1;
static char vm_cwd[2][FF_MAX_LFN] = {"/", "/"};

static void vm_file_check_number(int fnbr) {
    if (fnbr < 1 || fnbr > MAXOPENFILES) error("File number");
}

static void vm_file_resolve_path(const char * filename, int * fs_out, char * path) {
    const char * name = filename;
    int fs = vm_current_drive;

    if (!filename || !*filename) error("File name");
    if (vm_file_is_drive_spec(name)) {
        fs = vm_file_drive_index(name);
        name += 2;
    }
    if (*name == '\0') {
        strcpy(path, vm_cwd[fs]);
        *fs_out = fs;
        return;
    }

    if (*name == '/') {
        strncpy(path, name, FF_MAX_LFN - 1);
        path[FF_MAX_LFN - 1] = '\0';
    } else {
        strncpy(path, vm_cwd[fs], FF_MAX_LFN - 1);
        path[FF_MAX_LFN - 1] = '\0';
        if (strcmp(path, "/") != 0)
            strncat(path, "/", FF_MAX_LFN - strlen(path) - 1);
        strncat(path, name, FF_MAX_LFN - strlen(path) - 1);
    }
    vm_file_normalize_resolved_path(path);
    *fs_out = fs;
}

void vm_sys_file_host_resolve_path(const char * filename, char * path, int path_size) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    (void)path_size;
    vm_file_resolve_path(filename, &fs, full);
    (void)fs;
    strncpy(path, full, FF_MAX_LFN - 1);
    path[FF_MAX_LFN - 1] = '\0';
}

static void vm_file_ensure_parent_exists(const char * path) {
    char dir[FF_MAX_LFN] = {0};
    char * slash;
    FRESULT res;
    DIR dj;

    strncpy(dir, path, sizeof(dir) - 1);
    slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = '\0';
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_opendir(&dj, dir);
    if (res != FR_OK) error("Directory does not exist");
    f_closedir(&dj);
}

void vm_sys_file_open(const char * filename, int fnbr, int mode) {
    BYTE fmode = 0;
    FRESULT res;
    int fs;
    char path[FF_MAX_LFN] = {0};

    vm_file_check_number(fnbr);
    if (vm_files[fnbr]) error("File number already open");

    switch (mode) {
    case VM_FILE_MODE_INPUT:
        fmode = FA_READ;
        break;
    case VM_FILE_MODE_OUTPUT:
        fmode = FA_WRITE | FA_CREATE_ALWAYS;
        break;
    case VM_FILE_MODE_APPEND:
        fmode = FA_WRITE | FA_OPEN_APPEND;
        break;
    case VM_FILE_MODE_RANDOM:
        fmode = FA_WRITE | FA_OPEN_APPEND | FA_READ;
        break;
    default:
        error("File access mode");
    }

    vm_file_resolve_path(filename, &fs, path);
    (void)fs;
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    if (mode != VM_FILE_MODE_INPUT) vm_file_ensure_parent_exists(path);

    vm_files[fnbr] = (FIL *)BC_ALLOC(sizeof(FIL));
    if (!vm_files[fnbr]) error("NEM[file:fil_host] want=%", (int)sizeof(FIL));
    res = f_open(vm_files[fnbr], vm_host_fat_path(path), fmode);
    if (res != FR_OK) {
        BC_FREE(vm_files[fnbr]);
        vm_files[fnbr] = NULL;
        error("File error");
    }
}

void vm_sys_file_close(int fnbr) {
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");
    FRESULT res = f_close(vm_files[fnbr]);
    BC_FREE(vm_files[fnbr]);
    vm_files[fnbr] = NULL;
    if (res != FR_OK) error("File error");
}

void vm_sys_file_reset(void) {
    for (int i = 1; i <= MAXOPENFILES; i++) {
        if (vm_files[i]) {
            f_close(vm_files[i]);
            BC_FREE(vm_files[i]);
            vm_files[i] = NULL;
        }
    }
    vm_current_drive = 1;
    strcpy(vm_cwd[0], "/");
    strcpy(vm_cwd[1], "/");
}

void vm_sys_file_print_buf(int fnbr, const char * buf, int len) {
    UINT wrote = 0;
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");
    if (len <= 0) return;
    if (f_write(vm_files[fnbr], buf, (UINT)len, &wrote) != FR_OK ||
        wrote != (UINT)len) {
        error("File error");
    }
}

void vm_sys_file_print_str(int fnbr, const uint8_t * mstr) {
    vm_sys_file_print_buf(fnbr, (const char *)mstr + 1, mstr[0]);
}

void vm_sys_file_print_newline(int fnbr) {
    vm_sys_file_print_buf(fnbr, "\r\n", 2);
}

void vm_sys_file_line_input(int fnbr, uint8_t * dest) {
    int len = 0;
    int ch;
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");

    while (len < MAXSTRLEN && !f_eof(vm_files[fnbr])) {
        unsigned char c = 0;
        UINT read = 0;
        if (f_read(vm_files[fnbr], &c, 1, &read) != FR_OK || read == 0) break;
        ch = c;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        vm_file_line_append(dest, &len, ch);
    }
    dest[0] = (uint8_t)len;
}

int vm_sys_file_eof(int fnbr) {
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");
    return f_eof(vm_files[fnbr]);
}

int vm_sys_file_lof(int fnbr) {
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) return 0;
    return (int)f_size(vm_files[fnbr]);
}

int vm_sys_file_getc(int fnbr) {
    unsigned char c = 0;
    UINT read = 0;
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");
    if (f_read(vm_files[fnbr], &c, 1, &read) != FR_OK) error("File error");
    return read == 1 ? (int)c : -1;
}

void vm_sys_file_drive(const char * drive) {
    vm_current_drive = vm_file_drive_index(drive);
}

void vm_sys_file_seek(int fnbr, int position) {
    FRESULT res;
    vm_file_check_number(fnbr);
    if (!vm_files[fnbr]) error("File number is not open");
    if (position < 1) position = 1;
    res = f_lseek(vm_files[fnbr], (FSIZE_t)(position - 1));
    if (res != FR_OK) error("File error");
}

void vm_sys_file_mkdir(const char * path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    FRESULT res;
    vm_file_resolve_path(path, &fs, full);
    (void)fs;
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_mkdir(vm_host_fat_path(full));
    if (res != FR_OK) error("File error");
}

void vm_sys_file_chdir(const char * path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    DIR dj;
    FRESULT res;

    vm_file_resolve_path(path, &fs, full);
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_opendir(&dj, vm_host_fat_path(full));
    if (res != FR_OK) error("File error");
    f_closedir(&dj);
    vm_current_drive = fs;
    strncpy(vm_cwd[fs], full, FF_MAX_LFN - 1);
    vm_cwd[fs][FF_MAX_LFN - 1] = '\0';
}

void vm_sys_file_rmdir(const char * path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    FRESULT res;
    vm_file_resolve_path(path, &fs, full);
    (void)fs;
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_unlink(vm_host_fat_path(full));
    if (res != FR_OK) error("File error");
}

void vm_sys_file_kill(const char * path) {
    int fs;
    char full[FF_MAX_LFN] = {0};
    FRESULT res;
    vm_file_resolve_path(path, &fs, full);
    (void)fs;
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_unlink(vm_host_fat_path(full));
    if (res != FR_OK) error("File error");
}

void vm_sys_file_rename(const char * old_name, const char * new_name) {
    int old_fs, new_fs;
    char old_full[FF_MAX_LFN] = {0};
    char new_full[FF_MAX_LFN] = {0};
    FRESULT res;

    vm_file_resolve_path(old_name, &old_fs, old_full);
    vm_file_resolve_path(new_name, &new_fs, new_full);
    if (old_fs != new_fs) error("Only valid on current drive");
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    res = f_rename(old_full, new_full);
    if (res != FR_OK) error("File error");
}

void vm_sys_file_copy(const char * from_name, const char * to_name, int mode) {
    FIL src, dst;
    char from_full[FF_MAX_LFN] = {0};
    char to_full[FF_MAX_LFN] = {0};
    int from_fs, to_fs;
    FRESULT res;
    BYTE buffer[512];

    (void)mode;
    vm_file_resolve_path(from_name, &from_fs, from_full);
    vm_file_resolve_path(to_name, &to_fs, to_full);
    (void)from_fs;
    (void)to_fs;
    res = vm_host_fat_mount();
    if (res != FR_OK) error("Host FAT init failed");
    vm_file_ensure_parent_exists(to_full);
    res = f_open(&src, vm_host_fat_path(from_full), FA_READ);
    if (res != FR_OK) error("File error");
    res = f_open(&dst, vm_host_fat_path(to_full), FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        f_close(&src);
        error("File error");
    }

    while (1) {
        UINT read = 0, wrote = 0;
        res = f_read(&src, buffer, sizeof(buffer), &read);
        if (res != FR_OK) break;
        if (read == 0) break;
        res = f_write(&dst, buffer, read, &wrote);
        if (res != FR_OK || wrote != read) break;
    }
    f_close(&src);
    f_close(&dst);
    if (res != FR_OK) error("File error");
}
