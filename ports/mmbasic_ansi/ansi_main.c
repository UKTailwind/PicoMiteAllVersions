/*
 * ansi_main.c — entry point for the ANSI half-block MMBasic port.
 *
 * Layout is a trimmed version of host_native/host_main.c's run_repl.
 * Forces MMBasic's console output through the framebuffer path
 * (Option.DISPLAY_CONSOLE = 1, OptionConsole = 2 "SCREEN only"), then
 * hands stdout exclusively to the render thread, which reads
 * host_framebuffer and paints Unicode ▀ half-block cells to the
 * terminal.
 *
 * Usage:
 *   mmbasic_ansi                    → interactive REPL
 *   mmbasic_ansi program.bas        → run .bas file via the VM
 *   mmbasic_ansi --interp prog.bas  → run via the legacy interpreter
 *   mmbasic_ansi --resolution WxH   → override framebuffer size
 *                                      (default 320x320)
 */

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* unistd.h conflicts with Draw.h's setmode(); host_main.c works around
 * this by forward-declaring the unistd symbols it needs. */
char    *getcwd(char *buf, size_t size);
unsigned sleep(unsigned seconds);

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "runtime/runtime.h"

#include "host_fb.h"
#include "host_terminal.h"
#include "ansi_terminal.h"
#include "ansi_display.h"

extern jmp_buf mark;

/* host_native symbols we rely on. */
extern void host_runtime_finish(void);
extern void host_runtime_configure(int timeout_ms, const char *screenshot_path);
extern void host_runtime_configure_keys(const char *keys, int delay_ms);
extern int  host_repl_mode;
extern const char *host_sd_root;
extern void MMBasic_PrintBanner(void);
extern unsigned char OptionConsole;
extern short gui_font_width, gui_font_height;

/* Framebuffer backing for programs loaded into ProgMemory. Mirrors
 * host_main.c's flash_prog_buf — first half zeroed (program), second
 * half 0xFF (erased flash). */
extern const uint8_t *flash_progmemory;
uint8_t flash_prog_buf[2 * MAX_PROG_SIZE];

extern void vm_host_fat_reset(void);
extern void vm_sys_file_reset(void);
extern void vm_sys_pin_reset(void);

extern void bc_run_source_string(const char *source, const char *source_name);
extern int  bc_opt_level;

/* Output hook defined by host_main.c in the host_native build; we
 * own this symbol here since we don't link host_main.c. Setting it
 * to ansi_swallow guarantees the stray fwrite(stdout) paths in
 * host_runtime.c (MMfputs to stdout, myprintf) can never leak text
 * into the render thread's ANSI stream. All real BASIC output
 * already goes through DisplayPutC → host_framebuffer because
 * OptionConsole = 2. */
void (*host_output_hook)(const char *text, int len) = NULL;
static void ansi_swallow(const char *text, int len) {
    (void)text; (void)len;
}

/* ----------------------------------------------------------------- */
/* Source loading */
/* ----------------------------------------------------------------- */

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = malloc((size_t)fsize + 1);
    if (!source) { fclose(f); return NULL; }
    fread(source, 1, (size_t)fsize, f);
    source[fsize] = '\0';
    fclose(f);
    return source;
}

/* ----------------------------------------------------------------- */
/* Display setup — mirrors the --sim block in host_main.c.           */
/* ----------------------------------------------------------------- */

static void configure_display(int width, int height) {
    extern void host_sim_set_framebuffer_size(int w, int h);
    host_sim_set_framebuffer_size(width, height);

    Option.DISPLAY_CONSOLE = 1;
    OptionConsole = 2;                /* SCREEN only — no UART/stdout. */

    gui_font = 0x01;
    gui_font_width = 8;
    gui_font_height = 12;
    Option.Width  = width  / gui_font_width;
    Option.Height = height / gui_font_height;
    Option.Tab    = 4;
    Option.DefaultFont = 0x01;
    Option.ColourCode = 1;

    /* PicoCalc green phosphor palette. */
    extern int gui_fcolour, gui_bcolour;
    gui_fcolour = 0x00FF00;
    gui_bcolour = 0x000000;
    PromptFC    = 0x00FF00;
    PromptBC    = 0x000000;
    Option.DefaultFC = 0x00FF00;
    Option.DefaultBC = 0x000000;
}

/* ----------------------------------------------------------------- */
/* Bring-up + tear-down shared between REPL and script modes.        */
/* ----------------------------------------------------------------- */

static void ansi_memory_backing_init(void) {
    memset(flash_prog_buf, 0, MAX_PROG_SIZE);
    memset(flash_prog_buf + MAX_PROG_SIZE, 0xFF, MAX_PROG_SIZE);
    flash_progmemory = flash_prog_buf;
}

static const mm_runtime_adapter ansi_boot_adapter = {
    .name = "mmbasic_ansi",
    .memory_backing_init = ansi_memory_backing_init,
};

static int ansi_boot(int width, int height) {
    if (mmbasic_runtime_init_common(&ansi_boot_adapter,
            MMBASIC_RUNTIME_INIT_FLAG_LOAD_OPTIONS |
            MMBASIC_RUNTIME_INIT_FLAG_INIT_BASIC |
            MMBASIC_RUNTIME_INIT_FLAG_INIT_HEAP |
            MMBASIC_RUNTIME_INIT_FLAG_CLEAR_ERROR) != 0) {
        return -1;
    }

    configure_display(width, height);

    mmbasic_runtime_port_begin();

    /* The banner paints glyphs into host_framebuffer through
     * DrawBitmap. Do it before we enter the alt screen so
     * host_fb_generation is nonzero when the render thread starts,
     * guaranteeing an initial paint. */
    MMBasic_PrintBanner();

    /* Now swallow any stray stdout — the render thread owns stdout
     * from this point on. */
    host_output_hook = ansi_swallow;

    if (ansi_terminal_enter() != 0) return -1;
    if (ansi_display_start() != 0) return -1;
    return 0;
}

static void ansi_shutdown(void) {
    ansi_display_stop();
    /* ansi_terminal_exit is atexit-registered; it fires on return
     * from main() or on exit(). Nothing to do here. */
}

/* ----------------------------------------------------------------- */
/* Script mode.                                                      */
/* ----------------------------------------------------------------- */

static int run_script(const char *filename, int use_interpreter) {
    char *source = read_file(filename);
    if (!source) {
        fprintf(stderr, "mmbasic_ansi: cannot read %s: %s\n",
                filename, strerror(errno));
        return 2;
    }

    int rc = 0;
    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
    ClearRuntime(true);

    if (use_interpreter) {
        rc = mmbasic_runtime_run_source(NULL, source,
            MMBASIC_SOURCE_FLAGS_BATCH_LOAD |
            MMBASIC_RUNTIME_RUN_FLAG_CLEAR_RUNTIME |
            MMBASIC_RUNTIME_RUN_FLAG_PREPARE_PROGRAM);
    } else {
        if (setjmp(mark) == 0) {
            bc_run_source_string(source, filename);
        } else {
            rc = MMErrMsg[0] ? 1 : 0;
        }
    }

    free(source);
    host_runtime_finish();
    return rc;
}

/* ----------------------------------------------------------------- */
/* REPL mode.                                                        */
/* ----------------------------------------------------------------- */

static int run_repl(void) {
    static char sd_root[4096];
    if (getcwd(sd_root, sizeof(sd_root)) == NULL) strcpy(sd_root, ".");
    host_sd_root = sd_root;

    host_repl_mode = 1;

    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
    ClearRuntime(true);

    mmbasic_runtime_enter_repl(NULL, 0);   /* does not return under normal use */
    host_runtime_finish();
    return 0;
}

/* ----------------------------------------------------------------- */
/* Entry.                                                            */
/* ----------------------------------------------------------------- */

/* Minimum usable size. The 8×12 font needs at least 12 pixel rows
 * to show one row of glyphs, so 80×40 cells (80×80 pixels) is the
 * floor where the REPL is usable at all. */
#define ANSI_MIN_COLS 80
#define ANSI_MIN_ROWS 40

int main(int argc, char **argv) {
    int width = 0, height = 0;            /* 0 = auto-fit terminal */
    int use_interpreter = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                width = w;
                height = h;
            } else {
                fprintf(stderr, "Bad --resolution (expected WxH, e.g. 320x320)\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--interp") == 0) {
            use_interpreter = 1;
        } else if (strcmp(argv[i], "--vm") == 0) {
            use_interpreter = 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--resolution WxH] [--interp|--vm] [program.bas]\n",
                   argv[0]);
            printf("  No file given → interactive REPL.\n");
            printf("  Default resolution is auto-sized to the terminal.\n");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        } else if (!filename) {
            filename = argv[i];
        } else {
            fprintf(stderr, "Extra positional arg: %s\n", argv[i]);
            return 2;
        }
    }

    /* Read terminal size up front. We always need it, either to
     * auto-size the framebuffer or to sanity-check an explicit
     * --resolution. */
    int rows = 0, cols = 0;
    if (ansi_terminal_get_size(&rows, &cols) != 0) {
        fprintf(stderr, "mmbasic_ansi: can't read terminal size (TIOCGWINSZ)\n");
        return 2;
    }

    if (cols < ANSI_MIN_COLS || rows < ANSI_MIN_ROWS) {
        fprintf(stderr,
                "mmbasic_ansi: terminal is %dx%d cells; need at least %dx%d.\n"
                "Shrink your font (Cmd-minus) or enlarge the window.\n",
                cols, rows, ANSI_MIN_COLS, ANSI_MIN_ROWS);
        return 2;
    }

    /* Auto-size: one framebuffer pixel per terminal column, two
     * framebuffer pixel rows per terminal cell row. Cap at 320×320
     * so big terminals don't allocate a huge buffer the user can't
     * use. */
    if (width == 0 || height == 0) {
        width  = cols;
        height = rows * 2;
        if (width  > 320) width  = 320;
        if (height > 320) height = 320;
    } else {
        /* Explicit --resolution: warn if it won't fit (letterboxed). */
        int need_rows = height / 2;
        if (rows < need_rows || cols < width) {
            fprintf(stderr,
                    "mmbasic_ansi: terminal is %dx%d cells, need %dx%d "
                    "for %dx%d framebuffer. Output will be letterboxed.\n",
                    cols, rows, width, need_rows, width, height);
            sleep(1);
        }
    }

    if (ansi_boot(width, height) != 0) return 1;

    int rc;
    if (filename) {
        rc = run_script(filename, use_interpreter);
    } else {
        rc = run_repl();
    }

    ansi_shutdown();
    return rc;
}
