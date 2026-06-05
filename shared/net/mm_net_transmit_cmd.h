/*
 * shared/net/mm_net_transmit_cmd.h - shared WEB TRANSMIT BASIC parsing.
 */

#ifndef MM_NET_TRANSMIT_CMD_H
#define MM_NET_TRANSMIT_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MM_NET_TRANSMIT_NONE = 0,
    MM_NET_TRANSMIT_CODE,
    MM_NET_TRANSMIT_FILE,
    MM_NET_TRANSMIT_PAGE,
    MM_NET_TRANSMIT_CSS,
    MM_NET_TRANSMIT_JS,
    MM_NET_TRANSMIT_IMAGE,
} mm_net_transmit_kind_t;

typedef struct {
    mm_net_transmit_kind_t kind;
    int pcb;
    int status;
    const char * filename;
    const char * content_type;
    int extra;
} mm_net_transmit_args_t;

int mm_net_transmit_parse(unsigned char * cmd, int max_pcb,
                          mm_net_transmit_args_t * out);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_TRANSMIT_CMD_H */
