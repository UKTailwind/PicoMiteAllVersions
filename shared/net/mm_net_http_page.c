/*
 * shared/net/mm_net_http_page.c - WEB TRANSMIT PAGE template rendering.
 */

#include <stdint.h>
#include <string.h>

#include "hal/hal_filesystem.h"
#include "MMBasic_Includes.h"
#include "Memory.h"
#include "MATHS.h"
#include "shared/net/mm_net_http_page.h"

static void page_append(char * out, size_t cap, size_t * pos,
                        const void * src, size_t len) {
    if (*pos + len >= cap) error("Output buffer too small");
    memcpy(out + *pos, src, len);
    *pos += len;
    out[*pos] = 0;
}

int mm_net_http_render_page(const char * fname, int extra,
                            char ** out, size_t * out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!fname || !*fname || !out || !out_len || extra < 0) return -1;

    struct hal_stat st;
    if (hal_fs_stat(fname, &st) < 0 || !(st.mode & HAL_FS_S_IFREG)) return -1;

    hal_fs_fd_t fd;
    if (hal_fs_open(fname, HAL_FS_O_RDONLY, &fd) < 0) return -1;

    size_t cap = (size_t)st.size + (size_t)extra + 16;
    char * rendered = GetMemory((int)cap);
    size_t pos = 0;
    int ok = 1;

    while (ok) {
        char c;
        ssize_t n = hal_fs_read(fd, &c, 1);
        if (n < 0) {
            ok = 0;
            break;
        }
        if (n == 0) break;
        if (c == 26) continue;

        if (c != '{') {
            page_append(rendered, cap, &pos, &c, 1);
            continue;
        }

        char expr[MAXVARLEN];
        int exprp = 0;
        int literal_open = 0;
        while (1) {
            n = hal_fs_read(fd, &c, 1);
            if (n <= 0) break;
            if (exprp == 0 && c == '{') {
                literal_open = 1;
                break;
            }
            if (c == '}') break;
            if (exprp >= MAXVARLEN - 1) error("Variable name too long");
            expr[exprp++] = c;
        }
        if (literal_open) {
            c = '{';
            page_append(rendered, cap, &pos, &c, 1);
            continue;
        }
        if (n <= 0) {
            c = '{';
            page_append(rendered, cap, &pos, &c, 1);
            page_append(rendered, cap, &pos, expr, (size_t)exprp);
            break;
        }

        expr[exprp] = 0;
        MMFLOAT f;
        int64_t i64;
        unsigned char * s;
        int t = T_NOTYPE;
        unsigned char * saved_tknbuf = GetMemory(STRINGSIZE);
        unsigned char * eval_tkn = GetMemory(STRINGSIZE);
        char * valbuf = GetTempMemory(STRINGSIZE);

        strcpy((char *)saved_tknbuf, (char *)tknbuf);
        inpbuf[0] = 'r';
        inpbuf[1] = '=';
        strcpy((char *)inpbuf + 2, expr);
        tokenise(true);
        strcpy((char *)eval_tkn, (char *)(tknbuf + 2 + sizeof(CommandToken)));

        int saved_option_explicit = OptionExplicit;
        OptionExplicit = false;
        evaluate(eval_tkn, &f, &i64, &s, &t, false);
        OptionExplicit = saved_option_explicit;

        if (t & T_NBR) {
            FloatToStr(valbuf, f, 0, STR_AUTO_PRECISION, ' ');
            page_append(rendered, cap, &pos, valbuf, strlen(valbuf));
        } else if (t & T_INT) {
            IntToStr(valbuf, i64, 10);
            page_append(rendered, cap, &pos, valbuf, strlen(valbuf));
        } else if (t & T_STR) {
            page_append(rendered, cap, &pos, s + 1, s[0]);
        }

        strcpy((char *)tknbuf, (char *)saved_tknbuf);
        FreeMemory(saved_tknbuf);
        FreeMemory(eval_tkn);
    }

    hal_fs_close(fd);
    if (!ok) {
        FreeMemory((unsigned char *)rendered);
        return -2;
    }

    *out = rendered;
    *out_len = pos;
    return 0;
}
