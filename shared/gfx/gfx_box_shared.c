#include "vm_device_support.h"
#include "gfx_box_shared.h"

static int gfx_box_arg_value(const GfxBoxIntArg *arg, int index) {
    if (!arg->present || arg->count <= 0 || arg->get_int == NULL) return 0;
    return arg->get_int(arg->ctx, (arg->count > 1) ? index : 0);
}

static void gfx_box_fail_msg(const GfxBoxErrorSink *errors, const char *msg) {
    if (errors && errors->fail_msg) errors->fail_msg(errors->ctx, msg);
}

static void gfx_box_fail_range(const GfxBoxErrorSink *errors, const char *label,
                               int value, int min, int max) {
    if (errors && errors->fail_range) errors->fail_range(errors->ctx, label, value, min, max);
}

void gfx_box_execute(GfxBoxMode mode, const GfxBoxIntArg *args, int field_count,
                     const GfxBoxErrorSink *errors) {
    int x1 = 0, y1 = 0, width = 0, height = 0, w = 1, c = gui_fcolour, f = -1;
    int wmod = 0, hmod = 0;
    const int max_colour = (int)WHITE;

    if (field_count < 4 || field_count > GFX_BOX_ARG_COUNT) {
        gfx_box_fail_msg(errors, "Argument count");
        return;
    }

    if (mode == GFX_BOX_MODE_SCALAR) {
        if (!args[0].present || !args[1].present || !args[2].present || !args[3].present) {
            gfx_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[0].count != 1 || args[1].count != 1 || args[2].count != 1 || args[3].count != 1) {
            gfx_box_fail_msg(errors, "Invalid variable");
            return;
        }

        c = gui_fcolour;
        w = 1;
        f = -1;
        x1 = gfx_box_arg_value(&args[0], 0);
        y1 = gfx_box_arg_value(&args[1], 0);
        width = gfx_box_arg_value(&args[2], 0);
        height = gfx_box_arg_value(&args[3], 0);
        wmod = (width > 0) ? -1 : 1;
        hmod = (height > 0) ? -1 : 1;

        if (field_count > 4 && args[4].present) {
            if (args[4].count != 1) {
                gfx_box_fail_msg(errors, "Invalid variable");
                return;
            }
            w = gfx_box_arg_value(&args[4], 0);
            if (w < 0 || w > 100) {
                gfx_box_fail_range(errors, NULL, w, 0, 100);
                return;
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count != 1) {
                gfx_box_fail_msg(errors, "Invalid variable");
                return;
            }
            c = gfx_box_arg_value(&args[5], 0);
            if (c < 0 || c > max_colour) {
                gfx_box_fail_range(errors, NULL, c, 0, max_colour);
                return;
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count != 1) {
                gfx_box_fail_msg(errors, "Invalid variable");
                return;
            }
            f = gfx_box_arg_value(&args[6], 0);
            if (f < -1 || f > max_colour) {
                gfx_box_fail_range(errors, NULL, f, -1, max_colour);
                return;
            }
        }

        if (width != 0 && height != 0) {
            DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
        }
        return;
    }

    {
        int i;
        int n;
        int nwidth = 0, nheight = 0, nw = 0, nc = 0, nf = 0;

        if (!args[0].present || !args[1].present || args[0].count <= 1 || args[1].count <= 1) {
            gfx_box_fail_msg(errors, "Argument count");
            return;
        }

        n = args[0].count;
        if (args[1].count < n) n = args[1].count;

        if (!args[2].present) {
            gfx_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[2].count == 1) {
            width = gfx_box_arg_value(&args[2], 0);
            if (width < 1 || width > HRes) {
                gfx_box_fail_range(errors, "Width", width, 1, HRes);
                return;
            }
            nwidth = 1;
        } else {
            nwidth = args[2].count;
            if (nwidth > 1 && nwidth < n) n = nwidth;
            for (i = 0; i < nwidth; i++) {
                width = gfx_box_arg_value(&args[2], i);
                if (width < 1 || width > HRes) {
                    gfx_box_fail_range(errors, "Width", width, 1, HRes);
                    return;
                }
            }
        }

        if (!args[3].present) {
            gfx_box_fail_msg(errors, "Argument count");
            return;
        }
        if (args[3].count == 1) {
            height = gfx_box_arg_value(&args[3], 0);
            if (height < 1 || height > VRes) {
                gfx_box_fail_range(errors, "Height", height, 1, VRes);
                return;
            }
            nheight = 1;
        } else {
            nheight = args[3].count;
            if (nheight > 1 && nheight < n) n = nheight;
            for (i = 0; i < nheight; i++) {
                height = gfx_box_arg_value(&args[3], i);
                if (height < 1 || height > VRes) {
                    gfx_box_fail_range(errors, "Height", height, 1, VRes);
                    return;
                }
            }
        }

        c = gui_fcolour;
        w = 1;
        f = -1;

        if (field_count > 4 && args[4].present) {
            if (args[4].count == 1) {
                w = gfx_box_arg_value(&args[4], 0);
                if (w < 0 || w > 100) {
                    gfx_box_fail_range(errors, NULL, w, 0, 100);
                    return;
                }
                nw = 1;
            } else {
                nw = args[4].count;
                if (nw > 1 && nw < n) n = nw;
                for (i = 0; i < nw; i++) {
                    w = gfx_box_arg_value(&args[4], i);
                    if (w < 0 || w > 100) {
                        gfx_box_fail_range(errors, NULL, w, 0, 100);
                        return;
                    }
                }
            }
        }

        if (field_count > 5 && args[5].present) {
            if (args[5].count == 1) {
                c = gfx_box_arg_value(&args[5], 0);
                if (c < 0 || c > max_colour) {
                    gfx_box_fail_range(errors, NULL, c, 0, max_colour);
                    return;
                }
                nc = 1;
            } else {
                nc = args[5].count;
                if (nc > 1 && nc < n) n = nc;
                for (i = 0; i < nc; i++) {
                    c = gfx_box_arg_value(&args[5], i);
                    if (c < 0 || c > max_colour) {
                        gfx_box_fail_range(errors, NULL, c, 0, max_colour);
                        return;
                    }
                }
            }
        }

        if (field_count == 7 && args[6].present) {
            if (args[6].count == 1) {
                f = gfx_box_arg_value(&args[6], 0);
                if (f < 0 || f > max_colour) {
                    gfx_box_fail_range(errors, NULL, f, 0, max_colour);
                    return;
                }
                nf = 1;
            } else {
                nf = args[6].count;
                if (nf > 1 && nf < n) n = nf;
                for (i = 0; i < nf; i++) {
                    f = gfx_box_arg_value(&args[6], i);
                    if (f < -1 || f > max_colour) {
                        gfx_box_fail_range(errors, NULL, f, -1, max_colour);
                        return;
                    }
                }
            }
        }

        for (i = 0; i < n; i++) {
            x1 = gfx_box_arg_value(&args[0], i);
            y1 = gfx_box_arg_value(&args[1], i);
            if (nwidth > 1) width = gfx_box_arg_value(&args[2], i);
            if (nheight > 1) height = gfx_box_arg_value(&args[3], i);
            wmod = (width > 0) ? -1 : 1;
            hmod = (height > 0) ? -1 : 1;
            if (nw > 1) w = gfx_box_arg_value(&args[4], i);
            if (nc > 1) c = gfx_box_arg_value(&args[5], i);
            if (nf > 1) f = gfx_box_arg_value(&args[6], i);
            if (width != 0 && height != 0) {
                DrawBox(x1, y1, x1 + width + wmod, y1 + height + hmod, w, c, f);
            }
        }
    }
}
