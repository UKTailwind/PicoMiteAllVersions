#include "vm_device_support.h"
#include "gfx_pixel_shared.h"

static int gfx_pixel_arg_value(const GfxPixelArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void gfx_pixel_fail_msg(const GfxPixelErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void gfx_pixel_fail_range(const GfxPixelErrorSink *errors, const char *label,
                                 int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

void gfx_pixel_execute(GfxPixelMode mode, const GfxPixelArg *args, int field_count,
                       const GfxPixelErrorSink *errors) {
    int x = 0, y = 0, c = gui_fcolour;
    const int max_colour = (int)WHITE;

    if (field_count < 2 || field_count > GFX_PIXEL_ARG_COUNT) {
        gfx_pixel_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_PIXEL_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || args[0].count != 1 || args[1].count != 1) {
            gfx_pixel_fail_msg(errors, "Argument count");
            return;
        }

        x = gfx_pixel_arg_value(&args[0], 0);
        y = gfx_pixel_arg_value(&args[1], 0);
        if (field_count > 2 && args[2].present) {
            if (args[2].count != 1) {
                gfx_pixel_fail_msg(errors, "Argument count");
                return;
            }
            c = gfx_pixel_arg_value(&args[2], 0);
            if (c < -1 || c > max_colour) {
                gfx_pixel_fail_range(errors, NULL, c, -1, max_colour);
                return;
            }
        }

        if (c != -1) DrawPixel(x, y, c);
        else {
            CurrentX = x;
            CurrentY = y;
        }
        return;
    }

    {
        int i;
        int n;
        int nc = 0;

        if (!args[0].present || !args[1].present || args[0].count <= 1 || args[1].count <= 1) {
            gfx_pixel_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;

        if (field_count > 2 && args[2].present) {
            if (args[2].count == 1) {
                c = gfx_pixel_arg_value(&args[2], 0);
                if (c < 0 || c > max_colour) {
                    gfx_pixel_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[2].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = gfx_pixel_arg_value(&args[2], i);
                    if (value < 0 || value > max_colour) {
                        gfx_pixel_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x = gfx_pixel_arg_value(&args[0], i);
            y = gfx_pixel_arg_value(&args[1], i);
            if (nc > 1) c = gfx_pixel_arg_value(&args[2], i);
            DrawPixel(x, y, c);
        }
    }
}
