#ifndef GFX_TEXT_SHARED_H
#define GFX_TEXT_SHARED_H

typedef int (*GfxTextGetIntFn)(void *ctx);
typedef char *(*GfxTextGetStrFn)(void *ctx);
typedef void (*GfxTextGetDefaultsFn)(void *ctx, int *font, int *scale, int *fc, int *bc);
typedef int (*GfxTextFontValidFn)(void *ctx, int font);
typedef void (*GfxTextRenderFn)(void *ctx, int x, int y, int font, int scale,
                                int jh, int jv, int jo, int fc, int bc, char *s);
typedef void (*GfxTextFailMsgFn)(void *ctx, const char *msg);
typedef void (*GfxTextFailRangeFn)(void *ctx, int value, int min, int max);

typedef struct {
    int present;
    void *ctx;
    GfxTextGetIntFn get_int;
    GfxTextGetStrFn get_str;
} GfxTextArg;

typedef struct {
    void *ctx;
    GfxTextGetDefaultsFn get_defaults;
    GfxTextFontValidFn font_valid;
    GfxTextRenderFn render;
    GfxTextFailMsgFn fail_msg;
    GfxTextFailRangeFn fail_range;
} GfxTextOps;

#define GFX_TEXT_ARG_COUNT 8

void gfx_text_execute(const GfxTextArg *args, int field_count, const GfxTextOps *ops);

#endif
