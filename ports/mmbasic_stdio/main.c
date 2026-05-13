/*
 * ports/mmbasic_stdio/main.c — pure-stdio MMBasic entry.
 *
 * argv[1] = .bas file to run (or read stdin to EOF if missing).
 * PRINT goes to stdout; errors to stderr.  No REPL, no editor,
 * no graphics, no filesystem sim.
 *
 * The binary that emerges from linking this file is the HAL litmus:
 * if the MMBasic core is hardware-clean, we get a working stdio
 * interpreter without pulling in Editor.c / MMBasic_REPL.c /
 * MMBasic_Prompt.c / any display driver.  Undefined symbols below
 * indicate HAL leaks — fix them in core, don't add more files here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "runtime/runtime.h"

/* Read an entire file (or stdin if path is "-" or NULL) into a newly
 * malloc'd, NUL-terminated buffer.  Caller frees. */
static char *read_all(const char *path) {
    FILE *fp;
    if (path == NULL || strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "mmbasic_stdio: cannot open %s: %s\n",
                    path, strerror(errno));
            return NULL;
        }
    }
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { if (fp != stdin) fclose(fp); return NULL; }
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); if (fp != stdin) fclose(fp); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    if (fp != stdin) fclose(fp);
    return buf;
}

static void stdio_memory_backing_init(void) {
    extern unsigned char flash_prog_buf[];
    extern const uint8_t *flash_progmemory;
    memset(flash_prog_buf, 0, MAX_PROG_SIZE);
    memset(flash_prog_buf + MAX_PROG_SIZE, 0xFF, MAX_PROG_SIZE);
    flash_progmemory = flash_prog_buf;
}

static void stdio_reset_port_state(void) {
    extern void vm_host_fat_reset(void);
    extern void vm_sys_file_reset(void);
    extern void vm_sys_pin_reset(void);
    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
}

static const mm_runtime_adapter stdio_runtime_adapter = {
    .name = "mmbasic_stdio",
    .memory_backing_init = stdio_memory_backing_init,
    .display_console_init = mmbasic_runtime_port_begin,
    .after_load_program = stdio_reset_port_state,
};

int main(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : NULL;
    char *source = read_all(path);
    if (!source) return 2;

    if (mmbasic_runtime_init_common(&stdio_runtime_adapter,
            MMBASIC_RUNTIME_INIT_FLAG_LOAD_OPTIONS |
            MMBASIC_RUNTIME_INIT_FLAG_INIT_BASIC |
            MMBASIC_RUNTIME_INIT_FLAG_INIT_HEAP |
            MMBASIC_RUNTIME_INIT_FLAG_CLEAR_ERROR) != 0) {
        fprintf(stderr, "mmbasic_stdio: failed to initialise runtime\n");
        free(source);
        return 1;
    }

    int rc = mmbasic_runtime_run_source(&stdio_runtime_adapter, source,
        MMBASIC_SOURCE_FLAGS_BATCH_LOAD |
        MMBASIC_RUNTIME_RUN_FLAG_CLEAR_RUNTIME |
        MMBASIC_RUNTIME_RUN_FLAG_PREPARE_PROGRAM);
    if (rc != 0 && !MMErrMsg[0]) {
        fprintf(stderr, "mmbasic_stdio: failed to tokenise input\n");
    }
    if (MMErrMsg[0]) fprintf(stderr, "Error: %s\n", MMErrMsg);

    free(source);
    return rc;
}
