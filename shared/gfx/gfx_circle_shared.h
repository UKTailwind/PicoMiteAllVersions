#ifndef GFX_CIRCLE_SHARED_H
#define GFX_CIRCLE_SHARED_H

typedef int (*GfxCircleGetIntFn)(void *ctx, int index);
typedef MMFLOAT (*GfxCircleGetFloatFn)(void *ctx, int index);

typedef struct {
    int present;
    int count; /* 0 = missing, 1 = scalar, >1 = vector */
    void *ctx;
    GfxCircleGetIntFn get_int;
    GfxCircleGetFloatFn get_float;
} GfxCircleArg;

typedef void (*GfxCircleFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxCircleFailRangeFn)(void *ctx, const char *label, int value, int min, int max);

typedef struct {
    void *ctx;
    GfxCircleFailMsgFn fail_msg;
    GfxCircleFailRangeFn fail_range;
} GfxCircleErrorSink;

typedef enum {
    GFX_CIRCLE_MODE_SCALAR = 0,
    GFX_CIRCLE_MODE_VECTOR = 1,
} GfxCircleMode;

#define GFX_CIRCLE_ARG_COUNT 7

void gfx_circle_execute(GfxCircleMode mode, const GfxCircleArg *args, int field_count,
                        const GfxCircleErrorSink *errors);

#endif
