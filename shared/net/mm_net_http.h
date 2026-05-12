/*
 * shared/net/mm_net_http.h - HTTP helpers for BASIC WEB surfaces.
 */

#ifndef MM_NET_HTTP_H
#define MM_NET_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t mm_net_http_extract_path(const void *request, size_t request_len,
                                char *out, size_t out_len);
const char *mm_net_http_mime_from_name(const char *name,
                                       const char *fallback);
const char *mm_net_http_status_reason(int status);
int mm_net_http_format_status_body(char *out, size_t out_len, int status,
                                   const char *reason);
int mm_net_http_format_response_header(char *out, size_t out_len, int status,
                                       const char *reason,
                                       const char *server,
                                       const char *content_type,
                                       size_t content_len);
typedef int (*mm_net_http_send_fn)(void *ctx, const void *buf, size_t len);
int mm_net_http_send_response(int status, const char *reason,
                              const char *content_type,
                              const void *body, size_t body_len,
                              const char *server_name,
                              mm_net_http_send_fn send_fn, void *send_ctx);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_HTTP_H */
