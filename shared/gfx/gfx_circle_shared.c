#include "vm_device_support.h"
#include "gfx_circle_shared.h"

static int gfx_circle_arg_int(const GfxCircleArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static MMFLOAT gfx_circle_arg_float(const GfxCircleArg *arg, int index) {
    if (!arg->present || arg->count <= 0) return 0;
    if (arg->get_float != NULL) return arg->get_float(arg->ctx, (arg->count > 1) ? index : 0);
    if (arg->get_int != NULL) return (MMFLOAT)arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
    return 0;
}

static void gfx_circle_fail_msg(const GfxCircleErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void gfx_circle_fail_range(const GfxCircleErrorSink *errors, const char *label,
                                  int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

void gfx_circle_execute(GfxCircleMode mode, const GfxCircleArg *args, int field_count,
                        const GfxCircleErrorSink *errors) {
    int x = 0, y = 0, r = 0, w = 1, c = gui_fcolour, f = -1;
    MMFLOAT a = 1;
    const int max_colour = (int)WHITE;

    if (field_count < 3 || field_count > GFX_CIRCLE_ARG_COUNT) {
        gfx_circle_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_CIRCLE_MODE_SCALAR) {
        int save_refresh;

        if (!args[0].present || !args[1].present || !args[2].present) {
            gfx_circle_fail_msg(errors, "Argument count");
            return;
        }
        if (args[0].count != 1 || args[1].count != 1 || args[2].count != 1) {
            gfx_circle_fail_msg(errors, "Invalid variable");
            return;
        }

        x = gfx_circle_arg_int(&args[0], 0);
        y = gfx_circle_arg_int(&args[1], 0);
        r = gfx_circle_arg_int(&args[2], 0);

        if (field_count > 3 && args[3].present) {
            if (args[3].count != 1) {
                gfx_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            w = gfx_circle_arg_int(&args[3], 0);
            if (w < 0 || w > 100) {
                gfx_circle_fail_range(errors, NULL, w, 0, 100);
                return;
            }
        }
        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                gfx_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            a = gfx_circle_arg_float(&args[4], 0);
        }
        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                gfx_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            c = gfx_circle_arg_int(&args[5], 0);
            if (c < 0 || c > max_colour) {
                gfx_circle_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }
        if (field_count > 6 && args[6].present) {
            if (args[6].count != 1) {
                gfx_circle_fail_msg(errors, "Invalid variable");
                return;
            }
            f = gfx_circle_arg_int(&args[6], 0);
            if (f < -1 || f > max_colour) {
                gfx_circle_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }

        save_refresh = Option.Refresh;
        Option.Refresh = 0;
        DrawCircle(x, y, r, w, c, f, a);
        Option.Refresh = save_refresh;
        return;
    }

    {
        int i;
        int n;
        int nw = 0, na = 0, nc = 0, nf = 0;
        int save_refresh;

        if (!args[0].present || !args[1].present || !args[2].present ||
            args[0].count <= 1 || args[1].count <= 1 || args[2].count <= 1) {
            gfx_circle_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;
        if (args[2].count < n) n = args[2].count;

        if (field_count > 3 && args[3].present) {
            if (args[3].count == 1) {
                w = gfx_circle_arg_int(&args[3], 0);
                if (w < 0 || w > 100) {
                    gfx_circle_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[3].count;
                if (nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    int value = gfx_circle_arg_int(&args[3], i);
                    if (value < 0 || value > 100) {
                        gfx_circle_fail_range(errors, NULL, value, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                a = gfx_circle_arg_float(&args[4], 0);
                na = 1;
            } else {
                na = args[4].count;
                if (na < n) n = na;
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = gfx_circle_arg_int(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    gfx_circle_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    int value = gfx_circle_arg_int(&args[5], i);
                    if (value < 0 || value > max_colour) {
                        gfx_circle_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count > 6 && args[6].present) {
            if (args[6].count == 1) {
                f = gfx_circle_arg_int(&args[6], 0);
                if (f < -1 || f > max_colour) {
                    gfx_circle_fail_range(errors, NULL, f, -1, max_colour);
                    return;
                }
                nf = 1;
            } else {
                nf = args[6].count;
                if (nf < n) n = nf;
                for (i = 0; i < nf; i++) {
                    int value = gfx_circle_arg_int(&args[6], i);
                    if (value < 0 || value > max_colour) {
                        gfx_circle_fail_range(errors, NULL, value, 0, max_colour);
                        return;
                    }
                }
            }
        }

        save_refresh = Option.Refresh;
        Option.Refresh = 0;
        for (i = 0; i < n; i++) {
            x = gfx_circle_arg_int(&args[0], i);
            y = gfx_circle_arg_int(&args[1], i);
            r = gfx_circle_arg_int(&args[2], i) - 1;
            if (nw > 1) w = gfx_circle_arg_int(&args[3], i);
            if (na > 1) a = gfx_circle_arg_float(&args[4], i);
            if (nc > 1) c = gfx_circle_arg_int(&args[5], i);
            if (nf > 1) f = gfx_circle_arg_int(&args[6], i);
            DrawCircle(x, y, r, w, c, f, a);
        }
        Option.Refresh = save_refresh;
    }
}
