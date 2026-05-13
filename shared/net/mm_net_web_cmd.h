/*
 * shared/net/mm_net_web_cmd.h - shared top-level WEB command dispatch.
 */

#ifndef MM_NET_WEB_CMD_H
#define MM_NET_WEB_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*connect)(unsigned char *arg);
    void (*scan)(unsigned char *arg);
    int (*is_connected)(void);
    const char *not_connected_error;
    int (*mqtt)(unsigned char *line);
    int (*tcp_client)(unsigned char *line);
    int (*transmit)(unsigned char *arg);
    int (*tcp_server)(unsigned char *arg);
    void (*ntp)(unsigned char *arg);
    int (*udp)(unsigned char *arg);
} mm_net_web_dispatch_t;

void mm_net_web_dispatch(unsigned char *line,
                         const mm_net_web_dispatch_t *dispatch);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_WEB_CMD_H */
