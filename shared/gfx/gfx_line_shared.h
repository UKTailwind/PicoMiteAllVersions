#ifndef GFX_LINE_SHARED_H
#define GFX_LINE_SHARED_H

typedef int (*GfxLineGetIntFn)(void *ctx, int index);

typedef struct {
    int present;
    int count; /* 0 = missing, 1 = scalar, >1 = vector */
    void *ctx;
    GfxLineGetIntFn get_int;
} GfxLineArg;

typedef void (*GfxLineFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxLineFailRangeFn)(void *ctx, const char *label, int value, int min, int max);

typedef struct {
    void *ctx;
    GfxLineFailMsgFn fail_msg;
    GfxLineFailRangeFn fail_range;
} GfxLineErrorSink;

typedef enum {
    GFX_LINE_MODE_SCALAR = 0,
    GFX_LINE_MODE_VECTOR = 1,
} GfxLineMode;

#define GFX_LINE_ARG_COUNT 6

void gfx_line_execute(GfxLineMode mode, const GfxLineArg *args, int field_count,
                      const GfxLineErrorSink *errors);

#endif
