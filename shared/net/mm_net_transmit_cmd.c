/*
 * shared/net/mm_net_transmit_cmd.c - shared WEB TRANSMIT BASIC parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "Memory.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_transmit_cmd.h"

static void clear_args(mm_net_transmit_args_t *out) {
    memset(out, 0, sizeof(*out));
    out->extra = 4096;
}

int mm_net_transmit_parse(unsigned char *cmd, int max_pcb,
                          mm_net_transmit_args_t *out) {
    unsigned char *arg;
    clear_args(out);

    arg = checkstring(cmd, (unsigned char *)"CODE");
    if (arg) {
        getargs(&arg, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        out->kind = MM_NET_TRANSMIT_CODE;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->status = getint(argv[2], 100, 999);
        return 1;
    }

    arg = checkstring(cmd, (unsigned char *)"FILE");
    if (arg) {
        getargs(&arg, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        out->kind = MM_NET_TRANSMIT_FILE;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->filename = (const char *)getCstring(argv[2]);
        out->content_type = (const char *)getCstring(argv[4]);
        return 1;
    }

    arg = checkstring(cmd, (unsigned char *)"PAGE");
    if (arg) {
        getargs(&arg, 5, (unsigned char *)",");
        if (!(argc == 3 || argc == 5)) error("Argument count");
        out->kind = MM_NET_TRANSMIT_PAGE;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->filename = (const char *)getCstring(argv[2]);
        if (argc == 5) out->extra = getint(argv[4], 0, heap_memory_size);
        return 1;
    }

    arg = checkstring(cmd, (unsigned char *)"CSS");
    if (arg) {
        getargs(&arg, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        out->kind = MM_NET_TRANSMIT_CSS;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->filename = (const char *)getCstring(argv[2]);
        out->content_type = "text/css";
        return 1;
    }

    arg = checkstring(cmd, (unsigned char *)"JAVASCRIPT");
    if (!arg) arg = checkstring(cmd, (unsigned char *)"JS");
    if (arg) {
        getargs(&arg, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        out->kind = MM_NET_TRANSMIT_JS;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->filename = (const char *)getCstring(argv[2]);
        out->content_type = "application/javascript";
        return 1;
    }

    arg = checkstring(cmd, (unsigned char *)"IMAGE");
    if (arg) {
        getargs(&arg, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        out->kind = MM_NET_TRANSMIT_IMAGE;
        out->pcb = getint(argv[0], 1, max_pcb) - 1;
        out->filename = (const char *)getCstring(argv[2]);
        out->content_type =
            mm_net_http_mime_from_name(out->filename,
                                       "application/octet-stream");
        return 1;
    }

    return 0;
}
