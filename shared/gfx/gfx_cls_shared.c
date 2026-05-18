#include "vm_device_support.h"
#include "gfx_cls_shared.h"

void gfx_cls_execute(int has_arg, const GfxClsArg *arg, const GfxClsOps *ops) {
    int use_default = 1;
    int colour = 0;
    const int max_colour = (int)WHITE;

    if (has_arg) {
        if (!arg || !arg->get_int) {
            if (ops && ops->fail_msg) ops->fail_msg(ops->ctx, "Argument count");
            return;
        }
        colour = arg->get_int(arg->ctx);
        if (colour < 0 || colour > max_colour) {
            if (ops && ops->fail_range) ops->fail_range(ops->ctx, colour, 0, max_colour);
            return;
        }
        use_default = 0;
    }

    if (ops && ops->do_clear) ops->do_clear(ops->ctx, use_default, colour);
    CurrentX = 0;
    CurrentY = 0;
}
