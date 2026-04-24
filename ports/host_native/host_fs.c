/*
 * host_fs.c — POSIX filesystem helpers for the host REPL.
 *
 * Isolated from MMBasic_Includes.h because both FatFS (ff.h) and POSIX
 * (dirent.h) declare DIR, and POSIX unistd.h's setmode() collides with
 * MMBasic's graphics setmode(). Keeping this code here means those
 * headers never meet.
 */

#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#include "host_fs.h"

/* Tiny glob matcher — handles '*' and '?'. Case-insensitive. */
static int host_fs_match(const char *pattern, const char *name) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            while (*name) {
                if (host_fs_match(pattern, name)) return 1;
                name++;
            }
            return 0;
        }
        if (*pattern == '?') {
            if (!*name) return 0;
            pattern++;
            name++;
            continue;
        }
        if (toupper((unsigned char)*pattern) != toupper((unsigned char)*name))
            return 0;
        pattern++;
        name++;
    }
    return *name == '\0';
}

int host_fs_list_dir(const char *dir, const char *pattern, host_fs_emit_line emit) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    const char *pat = (pattern && *pattern) ? pattern : "*";
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;  /* hide dotfiles and . / .. */
        if (!host_fs_match(pat, de->d_name)) continue;
        emit(de->d_name);
    }
    closedir(d);
    return 0;
}

struct host_fs_walker {
    DIR *d;
    char dir[4096];
    char pattern[256];
};

host_fs_walker_t *host_fs_walk_open(const char *dir, const char *pattern) {
    DIR *d = opendir(dir);
    if (!d) return NULL;
    host_fs_walker_t *w = calloc(1, sizeof(*w));
    if (!w) { closedir(d); return NULL; }
    w->d = d;
    snprintf(w->dir, sizeof(w->dir), "%s", dir);
    snprintf(w->pattern, sizeof(w->pattern), "%s",
             (pattern && *pattern) ? pattern : "*");
    return w;
}

int host_fs_walk_next(host_fs_walker_t *w,
                      char *name_out, int name_cap,
                      int *is_dir_out,
                      unsigned long long *size_out,
                      long long *mtime_epoch_out) {
    if (!w || !w->d) return 0;
    struct dirent *de;
    char path[4096];
    while ((de = readdir(w->d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!host_fs_match(w->pattern, de->d_name)) continue;
        snprintf(path, sizeof(path), "%s/%s", w->dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (name_out && name_cap > 0) {
            snprintf(name_out, name_cap, "%s", de->d_name);
        }
        if (is_dir_out) *is_dir_out = S_ISDIR(st.st_mode) ? 1 : 0;
        if (size_out) *size_out = (unsigned long long)st.st_size;
        if (mtime_epoch_out) *mtime_epoch_out = (long long)st.st_mtime;
        return 1;
    }
    return 0;
}

void host_fs_walk_close(host_fs_walker_t *w) {
    if (!w) return;
    if (w->d) closedir(w->d);
    free(w);
}

int host_fs_unlink(const char *path) { return unlink(path) == 0 ? 0 : -1; }
int host_fs_rename(const char *from, const char *to) { return rename(from, to) == 0 ? 0 : -1; }
int host_fs_mkdir(const char *path) { return mkdir(path, 0755) == 0 ? 0 : -1; }
int host_fs_rmdir(const char *path) { return rmdir(path) == 0 ? 0 : -1; }
int host_fs_chdir(const char *path) { return chdir(path) == 0 ? 0 : -1; }
int host_fs_getcwd(char *out, int out_cap) { return getcwd(out, (size_t)out_cap) ? 0 : -1; }
