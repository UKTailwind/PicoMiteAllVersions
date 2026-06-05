/*
 * drivers/web_console/web_console_transport.h
 *
 * Target-clean transport contract for the web console shared driver.
 * Backends own HTTP/WebSocket integration and provide only nonblocking
 * frame send/close hooks to the shared protocol/display/audio code.
 */

#ifndef WEB_CONSOLE_TRANSPORT_H
#define WEB_CONSOLE_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_CONSOLE_WS_PATH "/__web_console/ws"

typedef enum {
    WEB_CONSOLE_TRANSPORT_OK = 0,
    WEB_CONSOLE_TRANSPORT_CLOSED = -1,
    WEB_CONSOLE_TRANSPORT_BACKPRESSURE = -2,
    WEB_CONSOLE_TRANSPORT_ERROR = -3,
} web_console_transport_result_t;

typedef void (*web_console_transport_text_rx_fn)(void * ctx,
                                                 const char * text,
                                                 size_t len);

typedef struct web_console_transport {
    void * ctx;
    int (*send_binary)(void * ctx, const void * data, size_t len);
    int (*send_text)(void * ctx, const char * text, size_t len);
    int (*can_send)(void * ctx);
    void (*close)(void * ctx);
} web_console_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_TRANSPORT_H */
