/*
 * shared/net/mm_net_http_file.h - WEB TRANSMIT FILE shared sender.
 */

#ifndef MM_NET_HTTP_FILE_H
#define MM_NET_HTTP_FILE_H

#include <stddef.h>

#include "shared/net/mm_net_http.h"

#ifdef __cplusplus
extern "C" {
#endif

int mm_net_http_send_file(const char * fname, const char * content_type,
                          const char * server_name,
                          mm_net_http_send_fn send_fn, void * send_ctx);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_HTTP_FILE_H */
