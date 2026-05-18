#ifndef GFX_BOX_SHARED_H
#define GFX_BOX_SHARED_H

typedef int (*GfxBoxGetIntFn)(void *ctx, int index);

typedef struct {
    int present;
    int count; /* 0 = missing, 1 = scalar, >1 = vector */
    void *ctx;
    GfxBoxGetIntFn get_int;
} GfxBoxIntArg;

typedef void (*GfxBoxFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxBoxFailRangeFn)(void *ctx, const char *label, int value, int min, int max);

typedef struct {
    void *ctx;
    GfxBoxFailMsgFn fail_msg;
    GfxBoxFailRangeFn fail_range;
} GfxBoxErrorSink;

typedef enum {
    GFX_BOX_MODE_SCALAR = 0,
    GFX_BOX_MODE_VECTOR = 1,
} GfxBoxMode;

#define GFX_BOX_ARG_COUNT 7

void gfx_box_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                     const GfxBoxErrorSink *errors);

#endif
