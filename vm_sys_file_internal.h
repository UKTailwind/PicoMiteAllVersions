/*
 * vm_sys_file_internal.h — shared helpers used by both the device
 * body (vm_sys_file.c) and the simulator body
 * (ports/vm_sys_sim/vm_sys_file_sim.c).
 *
 * Same split pattern as vm_sys_pin_internal.h. The build system
 * links exactly one VM file syscall body per
 * target; each TU gets its own inline copies of the helpers, so
 * there are no cross-file linkage constraints.
 */

#ifndef VM_SYS_FILE_INTERNAL_H
#define VM_SYS_FILE_INTERNAL_H

#include <string.h>
#include <ctype.h>

#include "vm_sys_file.h"
#include "bc_alloc.h"
#include "vm_device_support.h"

static inline void vm_file_line_append(uint8_t *dest, int *len, int ch) {
    if (ch == '\t') {
        do {
            if (++(*len) > MAXSTRLEN) error("Line is too long");
            dest[*len] = ' ';
        } while (*len % 4);
        return;
    }
    if (++(*len) > MAXSTRLEN) error("Line is too long");
    dest[*len] = (uint8_t)ch;
}

static inline int vm_file_is_drive_spec(const char *s) {
    return s && ((s[0] == 'A' || s[0] == 'a' || s[0] == 'B' || s[0] == 'b') && s[1] == ':');
}

static inline int vm_file_drive_index(const char *s) {
    if (!vm_file_is_drive_spec(s)) error("Invalid disk");
    return (s[0] == 'B' || s[0] == 'b') ? 1 : 0;
}

static inline void vm_file_normalize_resolved_path(char *path) {
    char temp[FF_MAX_LFN] = {0};
    char *parts[64];
    int depth = 0;
    int absolute = 0;
    char *token;
    char *saveptr = NULL;

    if (!path || !*path) {
        strcpy(path, "/");
        return;
    }

    if (path[0] == '/') absolute = 1;
    strncpy(temp, path, sizeof(temp) - 1);
    token = strtok_r(temp, "/", &saveptr);
    while (token) {
        if (strcmp(token, ".") == 0) {
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) depth--;
        } else if (*token) {
            parts[depth++] = token;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    path[0] = '\0';
    if (absolute) strcat(path, "/");
    for (int i = 0; i < depth; ++i) {
        if (i > 0 || absolute) strcat(path, i == 0 && absolute ? "" : "/");
        strcat(path, parts[i]);
    }
    if (path[0] == '\0') strcpy(path, absolute ? "/" : ".");
}

#endif  /* VM_SYS_FILE_INTERNAL_H */
