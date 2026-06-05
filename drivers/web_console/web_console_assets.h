/*
 * drivers/web_console/web_console_assets.h
 *
 * Tiny built-in browser assets for firmware-hosted web-console smoke.
 */

#ifndef WEB_CONSOLE_ASSETS_H
#define WEB_CONSOLE_ASSETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char * path;
    const char * content_type;
    const char * data;
    size_t len;
} web_console_asset_t;

const web_console_asset_t * web_console_asset_find(const char * path);

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_ASSETS_H */
