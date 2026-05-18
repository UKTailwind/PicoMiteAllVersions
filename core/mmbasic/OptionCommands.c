/***********************************************************************************************************************
PicoMite MMBasic

OptionCommands.c

Portable OPTION command handlers. Keep board/peripheral option setters
in port or driver code; this file handles language/runtime options that
do not know about Pico SDK hardware.
************************************************************************************************************************/

#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "FileIO.h"
#include "OptionCommands.h"

extern void setterminal(int height, int width);

static int option_named_colour(unsigned char *arg)
{
    if (checkstring(arg, (unsigned char *)"WHITE")) return WHITE;
    if (checkstring(arg, (unsigned char *)"YELLOW")) return YELLOW;
    if (checkstring(arg, (unsigned char *)"LILAC")) return LILAC;
    if (checkstring(arg, (unsigned char *)"BROWN")) return BROWN;
    if (checkstring(arg, (unsigned char *)"FUCHSIA")) return FUCHSIA;
    if (checkstring(arg, (unsigned char *)"RUST")) return RUST;
    if (checkstring(arg, (unsigned char *)"MAGENTA")) return MAGENTA;
    if (checkstring(arg, (unsigned char *)"RED")) return RED;
    if (checkstring(arg, (unsigned char *)"CYAN")) return CYAN;
    if (checkstring(arg, (unsigned char *)"GREEN")) return GREEN;
    if (checkstring(arg, (unsigned char *)"CERULEAN")) return CERULEAN;
    if (checkstring(arg, (unsigned char *)"MIDGREEN")) return MIDGREEN;
    if (checkstring(arg, (unsigned char *)"COBALT")) return COBALT;
    if (checkstring(arg, (unsigned char *)"MYRTLE")) return MYRTLE;
    if (checkstring(arg, (unsigned char *)"BLUE")) return BLUE;
    if (checkstring(arg, (unsigned char *)"BLACK")) return BLACK;
    error("Invalid colour: $", arg);
    return BLACK;
}

static void option_set_fkey(unsigned char *arg, unsigned char *dst, size_t dst_len, int limit)
{
    char text[STRINGSIZE];
    strcpy(text, (char *)getCstring(arg));
    if ((int)strlen(text) >= limit) error("Maximum % characters", limit - 1);
    if (strlen(text) >= dst_len) error("Maximum % characters", (int)dst_len - 1);
    strcpy((char *)dst, text);
    SaveOptions();
}

bool option_command_handle_common(unsigned char *cmdline, bool clear_display_on_default_colours)
{
    unsigned char *tp;

    tp = checkstring(cmdline, (unsigned char *)"NOCHECK");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"ON")) { OptionNoCheck = true; return true; }
        if (checkstring(tp, (unsigned char *)"OFF")) { OptionNoCheck = false; return true; }
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"BASE");
    if (tp) {
        if (g_DimUsed) error("Must be before DIM or LOCAL");
        g_OptionBase = getint(tp, 0, 1);
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"EXPLICIT");
    if (tp) {
        OptionExplicit = true;
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"ANGLE");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"DEGREES")) { optionangle = RADCONV; useoptionangle = true; return true; }
        if (checkstring(tp, (unsigned char *)"RADIANS")) { optionangle = 1.0; useoptionangle = false; return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"FAST AUDIO");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"OFF")) { optionfastaudio = 0; return true; }
        if (checkstring(tp, (unsigned char *)"ON")) { optionfastaudio = 1; return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"MILLISECONDS");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"OFF")) { optionfulltime = 0; return true; }
        if (checkstring(tp, (unsigned char *)"ON")) { optionfulltime = 1; return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"ESCAPE");
    if (tp) {
        OptionEscape = true;
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"CONSOLE");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"BOTH")) OptionConsole = 3;
        else if (checkstring(tp, (unsigned char *)"SERIAL")) OptionConsole = 1;
        else if (checkstring(tp, (unsigned char *)"SCREEN")) OptionConsole = 2;
        else if (checkstring(tp, (unsigned char *)"NONE")) OptionConsole = 0;
        else error("Syntax");
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"DEFAULT");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"INTEGER")) { DefaultType = T_INT; return true; }
        if (checkstring(tp, (unsigned char *)"FLOAT")) { DefaultType = T_NBR; return true; }
        if (checkstring(tp, (unsigned char *)"STRING")) { DefaultType = T_STR; return true; }
        if (checkstring(tp, (unsigned char *)"NONE")) { DefaultType = T_NOTYPE; return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"LOGGING");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"OFF")) { optionlogging = 0; return true; }
        if (checkstring(tp, (unsigned char *)"ON")) { optionlogging = 1; return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"BREAK");
    if (tp) {
        BreakKey = getinteger(tp);
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"F1");
    if (tp) { option_set_fkey(tp, Option.F1key, sizeof(Option.F1key), 64); return true; }
    tp = checkstring(cmdline, (unsigned char *)"F5");
    if (tp) { option_set_fkey(tp, Option.F5key, sizeof(Option.F5key), MAXKEYLEN); return true; }
    tp = checkstring(cmdline, (unsigned char *)"F6");
    if (tp) { option_set_fkey(tp, Option.F6key, sizeof(Option.F6key), MAXKEYLEN); return true; }
    tp = checkstring(cmdline, (unsigned char *)"F7");
    if (tp) { option_set_fkey(tp, Option.F7key, sizeof(Option.F7key), MAXKEYLEN); return true; }
    tp = checkstring(cmdline, (unsigned char *)"F8");
    if (tp) { option_set_fkey(tp, Option.F8key, sizeof(Option.F8key), MAXKEYLEN); return true; }
    tp = checkstring(cmdline, (unsigned char *)"F9");
    if (tp) { option_set_fkey(tp, Option.F9key, sizeof(Option.F9key), MAXKEYLEN); return true; }

    tp = checkstring(cmdline, (unsigned char *)"PLATFORM");
    if (tp) {
        char text[STRINGSIZE];
        strcpy(text, (char *)getCstring(tp));
        if (strlen(text) >= sizeof(Option.platform)) error("Maximum % characters", sizeof(Option.platform) - 1);
        if (checkstring((unsigned char *)text, (unsigned char *)"GAMEMITE")) strcpy((char *)Option.platform, "Game*Mite");
        else strcpy((char *)Option.platform, text);
        SaveOptions();
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"AUTORUN");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        Option.NoReset = 0;
        if (argc == 3) {
            if (checkstring(argv[2], (unsigned char *)"NORESET")) Option.NoReset = 1;
            else error("Syntax");
        }
        if (checkstring(argv[0], (unsigned char *)"OFF")) { Option.Autorun = 0; SaveOptions(); return true; }
        if (checkstring(argv[0], (unsigned char *)"ON")) { Option.Autorun = MAXFLASHSLOTS + 1; SaveOptions(); return true; }
        Option.Autorun = getint(argv[0], 0, MAXFLASHSLOTS);
        SaveOptions();
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLOURS");
    if (tp == NULL) tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLORS");
    if (tp) {
        int default_fc;
        int default_bc = BLACK;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 1 && argc != 3) error("Syntax");
        default_fc = option_named_colour(argv[0]);
        if (argc == 3) default_bc = option_named_colour(argv[2]);
        if (default_bc == default_fc) error("Foreground and Background colours are the same");
        Option.DefaultBC = default_bc;
        Option.DefaultFC = default_fc;
        SaveOptions();
        ResetDisplay();
        if (clear_display_on_default_colours && Option.DISPLAY_TYPE != SCREENMODE1) ClearScreen(gui_bcolour);
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"DISPLAY");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if (Option.DISPLAY_CONSOLE && argc > 0) error("Cannot change LCD console");
        if (argc >= 1) Option.Height = getint(argv[0], 5, 100);
        if (argc == 3) Option.Width = getint(argv[2], 37, 240);
        if (Option.DISPLAY_CONSOLE) {
            int height = (Option.Height > SCREENHEIGHT) ? Option.Height : SCREENHEIGHT;
            int width = (Option.Width > SCREENWIDTH) ? Option.Width : SCREENWIDTH;
            setterminal(height, width);
        } else {
            setterminal(Option.Height, Option.Width);
        }
        if (argc >= 1) SaveOptions();
        return true;
    }

    tp = checkstring(cmdline, (unsigned char *)"CASE");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"LOWER")) { Option.Listcase = CONFIG_LOWER; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"UPPER")) { Option.Listcase = CONFIG_UPPER; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"TITLE")) { Option.Listcase = CONFIG_TITLE; SaveOptions(); return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"TAB");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"2")) { Option.Tab = 2; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"3")) { Option.Tab = 3; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"4")) { Option.Tab = 4; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"8")) { Option.Tab = 8; SaveOptions(); return true; }
    }

    tp = checkstring(cmdline, (unsigned char *)"COLOURCODE");
    if (tp == NULL) tp = checkstring(cmdline, (unsigned char *)"COLORCODE");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"ON")) { Option.ColourCode = true; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"OFF")) { Option.ColourCode = false; SaveOptions(); return true; }
        error("Syntax");
    }

    tp = checkstring(cmdline, (unsigned char *)"CONTINUATION LINES");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"ENABLE")) { Option.continuation = '_'; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"DISABLE")) { Option.continuation = false; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"ON")) { Option.continuation = '_'; SaveOptions(); return true; }
        if (checkstring(tp, (unsigned char *)"OFF")) { Option.continuation = false; SaveOptions(); return true; }
        error("Syntax");
    }

    return false;
}
