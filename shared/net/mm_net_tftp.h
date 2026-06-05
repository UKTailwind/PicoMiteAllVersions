/*
 * shared/net/mm_net_tftp.h - small TFTP server protocol core.
 */

#ifndef MM_NET_TFTP_H
#define MM_NET_TFTP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bytes[16];
    uint8_t family; /* 4 or 6 */
    uint16_t port;
} mm_net_tftp_peer_t;

typedef struct {
    int (*open)(void * ctx, const char * filename, int write, void ** handle);
    ssize_t (*read)(void * ctx, void * handle, void * buf, size_t len);
    ssize_t (*write)(void * ctx, void * handle, const void * buf, size_t len);
    void (*close)(void * ctx, void * handle);
    int (*send)(void * ctx, const mm_net_tftp_peer_t * peer,
                const void * buf, size_t len);
} mm_net_tftp_ops_t;

typedef struct {
    int active;
    int write_mode;
    int last_data;
    uint16_t block;
    uint16_t block_size;
    mm_net_tftp_peer_t peer;
    void * handle;
    const mm_net_tftp_ops_t * ops;
    void * ctx;
} mm_net_tftp_session_t;

void mm_net_tftp_init(mm_net_tftp_session_t * session,
                      const mm_net_tftp_ops_t * ops, void * ctx);
void mm_net_tftp_close(mm_net_tftp_session_t * session);
void mm_net_tftp_handle_packet(mm_net_tftp_session_t * session,
                               const mm_net_tftp_peer_t * peer,
                               const void * packet, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_TFTP_H */
