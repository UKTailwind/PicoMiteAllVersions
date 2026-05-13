/*
 * shared/net/mm_net_http_page.h - WEB TRANSMIT PAGE template rendering.
 */

#ifndef MM_NET_HTTP_PAGE_H
#define MM_NET_HTTP_PAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int mm_net_http_render_page(const char *fname, int extra,
                            char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_HTTP_PAGE_H */
