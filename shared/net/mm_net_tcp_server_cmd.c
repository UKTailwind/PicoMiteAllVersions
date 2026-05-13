/*
 * shared/net/mm_net_tcp_server_cmd.c - shared TCP server BASIC parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "MATHS.h"
#include "shared/net/mm_net_tcp_server_cmd.h"

char *mm_net_tcp_server_parse_interrupt(unsigned char *arg) {
    getargs(&arg, 1, (unsigned char *)",");
    if (argc != 1) error("Syntax");
    return (char *)GetIntAddress(argv[0]);
}

void mm_net_tcp_server_parse_slot(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_slot_args_t *out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 1, (unsigned char *)",");
    if (argc != 1) error("Syntax");
    out->pcb = getint(argv[0], 1, max_pcb) - 1;
}

void mm_net_tcp_server_parse_read(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_read_args_t *out) {
    memset(out, 0, sizeof(*out));
    void *ptr = NULL;
    getargs(&arg, 3, (unsigned char *)",");
    if (argc != 3) error("Syntax");
    out->pcb = getint(argv[0], 1, max_pcb) - 1;
    ptr = findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
    if (!(g_vartbl[g_VarIndex].type & T_INT)) error("Argument 2 must be integer array");
    if (g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0) error("Argument 2 must be integer array");
    out->array_bytes = (g_vartbl[g_VarIndex].dims[0] - g_OptionBase + 1) * 8;
    out->payload_capacity = out->array_bytes - 8;
    out->dest = (int64_t *)ptr;
    out->dest[0] = 0;
    out->buffer = (uint8_t *)&out->dest[1];
}

void mm_net_tcp_server_parse_send(unsigned char *arg, int max_pcb,
                                  mm_net_tcp_server_send_args_t *out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 3, (unsigned char *)",");
    if (argc != 3) error("Argument count");
    out->pcb = getint(argv[0], 1, max_pcb) - 1;

    int64_t *src = NULL;
    parseintegerarray(argv[2], &src, 2, 1, NULL, false);
    if (src[0] < 0) error("Syntax");
    out->payload = (const uint8_t *)&src[1];
    out->payload_len = (size_t)src[0];
}
