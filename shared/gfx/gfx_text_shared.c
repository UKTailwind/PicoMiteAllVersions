#include "vm_device_support.h"
#include "gfx_text_shared.h"

static void gfx_text_fail_msg(const GfxTextOps *ops, const char *msg) {
    if (ops && ops->fail_msg) ops->fail_msg(ops->ctx, msg);
}

static void gfx_text_fail_range(const GfxTextOps *ops, int value, int min, int max) {
    if (ops && ops->fail_range) ops->fail_range(ops->ctx, value, min, max);
}

void gfx_text_execute(const GfxTextArg *args, int field_count, const GfxTextOps *ops) {
    int x, y, font, scale, fc, bc;
    int jh = 0, jv = 0, jo = 0;
    const int max_colour = (int)WHITE;
    char *text;
    char *just;

    if (field_count < 3 || field_count > GFX_TEXT_ARG_COUNT) {
        gfx_text_fail_msg(ops, "Argument count");
        return;
    }
    if (!args[0].present || !args[1].present || !args[2].present ||
        !args[0].get_int || !args[1].get_int || !args[2].get_str) {
        gfx_text_fail_msg(ops, "Argument count");
        return;
    }

    x = args[0].get_int(args[0].ctx);
    y = args[1].get_int(args[1].ctx);
    text = args[2].get_str(args[2].ctx);

    if (field_count > 3 && args[3].present) {
        if (!args[3].get_str) {
            gfx_text_fail_msg(ops, "TEXT requires string arguments");
            return;
        }
        just = args[3].get_str(args[3].ctx);
        if (!GetJustification((char *)just, &jh, &jv, &jo)) {
            gfx_text_fail_msg(ops, "Justification");
            return;
        }
    }

    if (ops && ops->get_defaults) ops->get_defaults(ops->ctx, &font, &scale, &fc, &bc);
    else {
        font = (gui_font >> 4) + 1;
        scale = gui_font & 0x0F;
        fc = gui_fcolour;
        bc = gui_bcolour;
    }

    if (field_count > 4 && args[4].present) {
        if (!args[4].get_int) {
            gfx_text_fail_msg(ops, "Argument count");
            return;
        }
        font = args[4].get_int(args[4].ctx);
        if (font < 1 || font > FONT_TABLE_SIZE) {
            gfx_text_fail_range(ops, font, 1, FONT_TABLE_SIZE);
            return;
        }
    }
    if (ops && ops->font_valid && !ops->font_valid(ops->ctx, font)) {
        gfx_text_fail_msg(ops, "Invalid font");
        return;
    }

    if (field_count > 5 && args[5].present) {
        if (!args[5].get_int) {
            gfx_text_fail_msg(ops, "Argument count");
            return;
        }
        scale = args[5].get_int(args[5].ctx);
        if (scale < 1 || scale > 15) {
            gfx_text_fail_range(ops, scale, 1, 15);
            return;
        }
    }
    if (field_count > 6 && args[6].present) {
        if (!args[6].get_int) {
            gfx_text_fail_msg(ops, "Argument count");
            return;
        }
        fc = args[6].get_int(args[6].ctx);
        if (fc < 0 || fc > max_colour) {
            gfx_text_fail_range(ops, fc, 0, max_colour);
            return;
        }
    }
    if (field_count > 7 && args[7].present) {
        if (!args[7].get_int) {
            gfx_text_fail_msg(ops, "Argument count");
            return;
        }
        bc = args[7].get_int(args[7].ctx);
        if (bc < -1 || bc > max_colour) {
            gfx_text_fail_range(ops, bc, -1, max_colour);
            return;
        }
    }

    if (ops && ops->render) ops->render(ops->ctx, x, y, font, scale, jh, jv, jo, fc, bc, text);
}
