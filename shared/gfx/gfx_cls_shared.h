#ifndef GFX_CLS_SHARED_H
#define GFX_CLS_SHARED_H

typedef int (*GfxClsGetIntFn)(void *ctx);
typedef void (*GfxClsDoClearFn)(void *ctx, int use_default, int colour);
typedef void (*GfxClsFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxClsFailRangeFn)(void *ctx, int value, int min, int max);

typedef struct {
    void *ctx;
    GfxClsGetIntFn get_int;
} GfxClsArg;

typedef struct {
    void *ctx;
    GfxClsDoClearFn do_clear;
    GfxClsFailMsgFn fail_msg;
    GfxClsFailRangeFn fail_range;
} GfxClsOps;

void gfx_cls_execute(int has_arg, const GfxClsArg *arg, const GfxClsOps *ops);

#endif
