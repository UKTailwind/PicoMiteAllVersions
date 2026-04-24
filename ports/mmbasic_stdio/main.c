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
#include <setjmp.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Declared in MMBasic.c — the jump target error() / END longjmp back to. */
extern jmp_buf mark;

/* flash_range_erase lives in hal_flash_host.c (also hal_flash HAL
 * surface) but has no public header.  Declare here. */
extern void flash_range_erase(uint32_t off, uint32_t count);

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

/* Tokenise `source` line-by-line into ProgMemory.  Minimal version of
 * host_main.c's load_basic_source — no continuation-line handling, no
 * REPL-aware numbering, just split on '\n' and feed each line through
 * tokenise(). */
int load_source(const char *source) {
    flash_range_erase(0, MAX_PROG_SIZE);
    unsigned char *pm = ProgMemory;
    const char *line = source;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            if (len >= STRINGSIZE) len = STRINGSIZE - 1;
            memcpy(inpbuf, line, len);
            inpbuf[len] = '\0';
            tokenise(0);
            unsigned char *tp = tknbuf;
            while (!(tp[0] == 0 && tp[1] == 0)) *pm++ = *tp++;
            *pm++ = 0;
        }
        line = eol ? eol + 1 : line + strlen(line);
    }
    *pm++ = 0;
    *pm++ = 0;
    PSize = (int)(pm - ProgMemory);
    return 0;
}

int main(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : NULL;
    char *source = read_all(path);
    if (!source) return 2;

    /* Minimal runtime bring-up mirroring host_main.c's run_interpreter. */
    InitHeap(true);
    MMerrno = 0;
    MMErrMsg[0] = '\0';

    if (load_source(source) != 0) {
        fprintf(stderr, "mmbasic_stdio: failed to tokenise input\n");
        free(source);
        return 1;
    }

    ClearRuntime(true);
    PrepareProgram(1);

    int rc = 0;
    if (setjmp(mark) == 0) {
        ExecuteProgram(ProgMemory);
    } else {
        rc = MMErrMsg[0] ? 1 : 0;
    }
    if (MMErrMsg[0]) fprintf(stderr, "Error: %s\n", MMErrMsg);

    free(source);
    return rc;
}
