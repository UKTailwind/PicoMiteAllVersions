/*
 * shared/net/mm_net_udp_cmd.c - shared UDP BASIC command parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "shared/net/mm_net_udp_cmd.h"

char *mm_net_udp_parse_interrupt(unsigned char *arg) {
    getargs(&arg, 1, (unsigned char *)",");
    if (argc != 1) error("Syntax");
    return (char *)GetIntAddress(argv[0]);
}

void mm_net_udp_parse_send(unsigned char *arg, mm_net_udp_send_args_t *out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 5, (unsigned char *)",");
    if (argc != 5) error("Syntax");
    out->host = (char *)getCstring(argv[0]);
    out->port = getint(argv[2], 1, 65535);
    unsigned char *data = getstring(argv[4]);
    out->payload = data + 1;
    out->payload_len = data[0];
}
