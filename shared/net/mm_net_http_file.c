/*
 * shared/net/mm_net_http_file.c - WEB TRANSMIT FILE shared sender.
 */

#include <stdint.h>

#include "hal/hal_filesystem.h"
#include "shared/net/mm_net_http.h"
#include "shared/net/mm_net_http_file.h"

int mm_net_http_send_file(const char * fname, const char * content_type,
                          const char * server_name,
                          mm_net_http_send_fn send_fn, void * send_ctx) {
    if (!fname || !*fname || !send_fn) return -1;

    struct hal_stat st;
    if (hal_fs_stat(fname, &st) < 0 || !(st.mode & HAL_FS_S_IFREG)) return -1;

    hal_fs_fd_t fd;
    if (hal_fs_open(fname, HAL_FS_O_RDONLY, &fd) < 0) return -1;

    if (!content_type || !*content_type) {
        content_type = mm_net_http_mime_from_name(fname, "application/octet-stream");
    }

    int rc = mm_net_http_send_response(200, NULL, content_type, NULL,
                                       (size_t)st.size,
                                       server_name, send_fn, send_ctx);
    if (rc != 0) {
        hal_fs_close(fd);
        return rc;
    }

    uint8_t buf[1024];
    while (1) {
        ssize_t n = hal_fs_read(fd, buf, sizeof buf);
        if (n < 0) {
            hal_fs_close(fd);
            return -2;
        }
        if (n == 0) break;
        if (!send_fn(send_ctx, buf, (size_t)n)) {
            hal_fs_close(fd);
            return -2;
        }
    }

    hal_fs_close(fd);
    return 0;
}
