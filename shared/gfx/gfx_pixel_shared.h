#ifndef GFX_PIXEL_SHARED_H
#define GFX_PIXEL_SHARED_H

typedef int (*GfxPixelGetIntFn)(void *ctx, int index);

typedef struct {
    int present;
    int count; /* 0 = missing, 1 = scalar, >1 = vector */
    void *ctx;
    GfxPixelGetIntFn get_int;
} GfxPixelArg;

typedef void (*GfxPixelFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxPixelFailRangeFn)(void *ctx, const char *label, int value, int min, int max);

typedef struct {
    void *ctx;
    GfxPixelFailMsgFn fail_msg;
    GfxPixelFailRangeFn fail_range;
} GfxPixelErrorSink;

typedef enum {
    GFX_PIXEL_MODE_SCALAR = 0,
    GFX_PIXEL_MODE_VECTOR = 1,
} GfxPixelMode;

#define GFX_PIXEL_ARG_COUNT 3

void gfx_pixel_execute(GfxPixelMode mode, const GfxPixelArg *args, int field_count,
                       const GfxPixelErrorSink *errors);

#endif
