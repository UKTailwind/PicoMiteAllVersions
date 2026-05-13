#include "runtime/runtime.h"

#include <string.h>

#include "MMBasic_Includes.h"
#include "FileIO.h"

extern void flash_range_erase(uint32_t off, uint32_t count);

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int ascii_starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (ascii_lower(*s++) != ascii_lower(*prefix++)) return 0;
    }
    return 1;
}

static void update_continuation_setting(const char *line, unsigned char *continuation)
{
    const char *p = line;
    if (!continuation) return;
    while (*p == ' ' || *p == '\t') p++;
    if (*p >= '0' && *p <= '9') {
        while (*p >= '0' && *p <= '9') p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    if (ascii_starts_with_ci(p, "OPTION CONTINUATION LINES ON") ||
        ascii_starts_with_ci(p, "OPTION CONTINUATION LINES ENABLE")) {
        *continuation = '_';
    } else if (ascii_starts_with_ci(p, "OPTION CONTINUATION LINES OFF") ||
               ascii_starts_with_ci(p, "OPTION CONTINUATION LINES DISABLE")) {
        *continuation = 0;
    }
}

static const char *find_line_end(const char *line)
{
    while (*line && *line != '\r' && *line != '\n') line++;
    return line;
}

static const char *skip_line_end(const char *line)
{
    if (*line == '\r') line++;
    if (*line == '\n') line++;
    return line;
}

static int read_logical_line(const char **linep, char *out, size_t out_cap,
                             unsigned char *continuation)
{
    const char *line = *linep;
    size_t out_len = 0;

    if (*line == '\0') return 0;
    out[0] = '\0';

    while (*line) {
        const char *eol = find_line_end(line);
        size_t len = (size_t)(eol - line);
        if (out_len + len > out_cap - 1) len = (out_cap - 1) - out_len;
        memcpy(out + out_len, line, len);
        out_len += len;
        out[out_len] = '\0';

        line = skip_line_end(eol);

        if (*continuation && out_len >= 2 &&
            out[out_len - 2] == ' ' && out[out_len - 1] == *continuation) {
            out_len -= 2;
            out[out_len] = '\0';
            continue;
        }
        break;
    }

    *linep = line;
    update_continuation_setting(out, continuation);
    return 1;
}

static int read_batch_line(const char **linep, char *out, size_t out_cap)
{
    const char *line = *linep;
    const char *eol;
    size_t len;

    if (*line == '\0') return 0;
    eol = find_line_end(line);
    len = (size_t)(eol - line);
    if (len > out_cap - 1) len = out_cap - 1;
    memcpy(out, line, len);
    out[len] = '\0';
    *linep = skip_line_end(eol);
    return 1;
}

static void append_tokenised_line(unsigned char **pm)
{
    unsigned char *tp = tknbuf;
    while (!(tp[0] == 0 && tp[1] == 0)) {
        *(*pm)++ = *tp++;
    }
    *(*pm)++ = 0;
}

int mmbasic_tokenise_source_to_progmem(const char *source, unsigned flags)
{
    unsigned char *pm;
    const char *line;
    unsigned char continuation = 0;

    if (!source) return -1;
    if (flags & MMBASIC_SOURCE_FLAG_CLEAR_PROGMEM) {
        flash_range_erase(0, MAX_PROG_SIZE);
    }

    pm = ProgMemory;
    line = source;
    if (flags & MMBASIC_SOURCE_FLAG_CONTINUATION_LINES) {
        continuation = Option.continuation;
    }

    while (*line) {
        char logical[STRINGSIZE + 1];
        int has_line;
        size_t len;

        if (flags & MMBASIC_SOURCE_FLAG_CONTINUATION_LINES) {
            has_line = read_logical_line(&line, logical, sizeof(logical),
                                         &continuation);
        } else {
            has_line = read_batch_line(&line, logical, sizeof(logical));
        }
        if (!has_line) break;

        len = strlen(logical);
        if (len == 0) continue;
        memcpy(inpbuf, logical, len);
        inpbuf[len] = '\0';
        tokenise(0);
        append_tokenised_line(&pm);
    }

    *pm++ = 0;
    *pm++ = 0;
    PSize = (int)(pm - ProgMemory);
    (void)(flags & MMBASIC_SOURCE_FLAG_ERASED_TAIL_SENTINEL);
    return 0;
}

int mmbasic_save_loaded_source(const char *source, unsigned flags)
{
    int rc = mmbasic_tokenise_source_to_progmem(source, flags);
    if (rc != 0) return rc;
    PrepareProgram(false);
    return 0;
}
