/*
 * shared/net/mm_net_tcp_client_cmd.c - shared TCP client BASIC parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "MATHS.h"
#include "shared/net/mm_net_tcp_client_cmd.h"

void mm_net_tcp_client_parse_open(unsigned char * arg,
                                  mm_net_tcp_client_open_args_t * out) {
    memset(out, 0, sizeof(*out));
    out->timeout_ms = 5000;
    getargs(&arg, 5, (unsigned char *)",");
    if (!(argc == 3 || argc == 5)) error("Syntax");
    out->host = (char *)getCstring(argv[0]);
    out->port = getint(argv[2], 1, 65535);
    if (argc == 5) out->timeout_ms = getint(argv[4], 1, 100000);
}

void mm_net_tcp_client_parse_request(unsigned char * arg,
                                     mm_net_tcp_client_request_args_t * out) {
    memset(out, 0, sizeof(*out));
    out->timeout_ms = 5000;
    getargs(&arg, 5, (unsigned char *)",");
    if (!(argc == 3 || argc == 5)) error("Syntax");
    unsigned char * request = getstring(argv[0]);
    out->request = request + 1;
    out->request_len = request[0];
    out->array_bytes = parseintegerarray(argv[2], &out->dest, 2, 1, NULL, true) * 8;
    out->payload_capacity = out->array_bytes - 8;
    if (out->payload_capacity <= 0) error("array too small");
    out->dest[0] = 0;
    out->buffer = (uint8_t *)&out->dest[1];
    if (argc == 5) out->timeout_ms = getint(argv[4], 1, 100000);
}

static int64_t * parse_scalar_integer(unsigned char * expr, const char * arg_name) {
    void * ptr = findvar(expr, V_FIND | V_NOFIND_ERR);
    if (!(g_vartbl[g_VarIndex].type & T_INT) || g_vartbl[g_VarIndex].dims[0] != 0)
        error("%s must be an integer", arg_name);
    return (int64_t *)ptr;
}

void mm_net_tcp_client_parse_stream(unsigned char * arg,
                                    mm_net_tcp_client_stream_args_t * out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 7, (unsigned char *)",");
    if (argc != 7) error("Syntax");
    unsigned char * request = getstring(argv[0]);
    out->request = request + 1;
    out->request_len = request[0];
    out->array_bytes = parseintegerarray(argv[2], &out->dest, 2, 1, NULL, true) * 8;
    out->payload_capacity = out->array_bytes - 8;
    if (out->payload_capacity <= 1) error("array too small");
    out->dest[0] = 0;
    out->buffer = (uint8_t *)&out->dest[1];
    out->read_pos = parse_scalar_integer(argv[4], "Argument 3");
    out->write_pos = parse_scalar_integer(argv[6], "Argument 4");
}

void mm_net_tcp_client_stream_append(uint8_t * buffer, int capacity,
                                     int64_t * read_pos, int64_t * write_pos,
                                     const uint8_t * data, size_t len) {
    if (!buffer || capacity <= 1 || !read_pos || !write_pos || !data) return;
    int64_t r = *read_pos;
    int64_t w = *write_pos;
    if (r < 0 || r >= capacity) r = 0;
    if (w < 0 || w >= capacity) w = r;
    for (size_t i = 0; i < len; ++i) {
        buffer[w] = data[i];
        w = (w + 1) % capacity;
        if (w == r) r = (r + 1) % capacity;
    }
    *read_pos = r;
    *write_pos = w;
}
