#include "vm_device_support.h"
#include "gfx_line_shared.h"

static int gfx_line_arg_value(const GfxLineArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void gfx_line_fail_msg(const GfxLineErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void gfx_line_fail_range(const GfxLineErrorSink *errors, const char *label,
                                int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

void gfx_line_execute(GfxLineMode mode, const GfxLineArg *args, int field_count,
                      const GfxLineErrorSink *errors) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 1, c = gui_fcolour;
    const int max_colour = (int)WHITE;

    if (field_count < 2 || field_count > GFX_LINE_ARG_COUNT) {
        gfx_line_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_LINE_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || args[0].count != 1 || args[1].count != 1) {
            gfx_line_fail_msg(errors, "Argument count");
            return;
        }

        x1 = gfx_line_arg_value(&args[0], 0);
        y1 = gfx_line_arg_value(&args[1], 0);

        if (field_count > 2 && args[2].present) {
            if (args[2].count != 1) {
                gfx_line_fail_msg(errors, "Argument count");
                return;
            }
            x2 = gfx_line_arg_value(&args[2], 0);
        } else {
            x2 = CurrentX;
            CurrentX = x1;
        }

        if (field_count > 3 && args[3].present) {
            if (args[3].count != 1) {
                gfx_line_fail_msg(errors, "Argument count");
                return;
            }
            y2 = gfx_line_arg_value(&args[3], 0);
        } else {
            y2 = CurrentY;
            CurrentY = y1;
        }

        if (x1 == CurrentX && y1 == CurrentY) {
            CurrentX = x2;
            CurrentY = y2;
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                gfx_line_fail_msg(errors, "Argument count");
                return;
            }
            w = gfx_line_arg_value(&args[4], 0);
            if (w < -100 || w > 100) {
                gfx_line_fail_range(errors, NULL, w, 0, 100);
                return;
            }
            if (!w) return;
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                gfx_line_fail_msg(errors, "Argument count");
                return;
            }
            c = gfx_line_arg_value(&args[5], 0);
            if (c < 0 || c > max_colour) {
                gfx_line_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }

        DrawLine(x1, y1, x2, y2, w, c);
        return;
    }

    {
        int i;
        int n;
        int nw = 0, nc = 0;

        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present ||
            args[0].count <= 1 || args[1].count <= 1 || args[2].count <= 1 || args[3].count <= 1) {
            gfx_line_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;
        if (args[2].count < n) n = args[2].count;
        if (args[3].count < n) n = args[3].count;

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                w = gfx_line_arg_value(&args[4], 0);
                if (w < -100 || w > 100) {
                    gfx_line_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[4].count;
                if (nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    int value = gfx_line_arg_value(&args[4], i);
                    if (value < -100 || value > 100) {
                        gfx_line_fail_range(errors, NULL, value, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = gfx_line_arg_value(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    gfx_line_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = gfx_line_arg_value(&args[5], i);
                    if (value < 0 || value > max_colour) {
                        gfx_line_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x1 = gfx_line_arg_value(&args[0], i);
            y1 = gfx_line_arg_value(&args[1], i);
            x2 = gfx_line_arg_value(&args[2], i);
            y2 = gfx_line_arg_value(&args[3], i);
            if (nw > 1) w = gfx_line_arg_value(&args[4], i);
            if (nc > 1) c = gfx_line_arg_value(&args[5], i);
            if (w) DrawLine(x1, y1, x2, y2, w, c);
        }
    }
}
