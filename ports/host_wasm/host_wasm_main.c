/*
 * host_wasm_main.c -- WASM replacement for host_main.c.
 *
 * Has no CLI. emscripten's runtime calls our (empty) main(), then JS
 * calls _wasm_boot() explicitly once the module has resolved and the
 * canvas is wired. wasm_boot mirrors the native host_main.c flow for
 * MODE_REPL: flash_prog_buf setup, InitBasic, host_runtime_configure,
 * host_runtime_configure_keys, then run_repl (inlined here).
 *
 * Owns the two globals host_main.c used to provide — flash_prog_buf and
 * host_output_hook — because the WASM link drops host_main.o.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <setjmp.h>
#include <pthread.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern const uint8_t *flash_progmemory;
/* Sized to mirror the device program-flash region: MAX_PROG_SIZE +
 * the CFunction trailing area. Pre-refactor this was a hardcoded
 * 256 KB regardless of variant; now it tracks MAX_PROG_SIZE so wasm's
 * MEMORY command shows the same numbers as device. */
uint8_t flash_prog_buf[2 * MAX_PROG_SIZE];

/* Shared between host_runtime.c (MMPrintString / putConsole) and
 * host_main.c's capture mode. In WASM there is nothing to capture to —
 * console output already reaches the framebuffer via DisplayPutC — so
 * we leave the hook NULL permanently. */
void (*host_output_hook)(const char *text, int len) = NULL;

/* Declared in host_runtime.c; also referenced here to run the shared
 * REPL setup without going through host_main.c. */
extern char MMErrMsg[];
extern int MMerrno;
extern int host_repl_mode;
extern int bc_opt_level;
extern const char *host_sd_root;

void host_runtime_configure(int timeout_ms, const char *screenshot_path);
void host_runtime_configure_keys(const char *keys, int delay_ms);
void host_runtime_begin(void);
void host_runtime_finish(void);

void vm_host_fat_reset(void);
void vm_sys_file_reset(void);
void vm_sys_pin_reset(void);
void host_options_snapshot(void);

extern void MMBasic_PrintBanner(void);
extern void MMBasic_RunPromptLoop(void);

/* Set by wasm_break so CheckAbort / routinechecks sees the break and
 * longjmps back to the prompt. Polled opportunistically — full Ctrl-C
 * wiring is Phase 5 input-polish work. */
static volatile int wasm_break_requested = 0;

EMSCRIPTEN_KEEPALIVE
void wasm_break(void) {
    wasm_break_requested = 1;
    MMAbort = 1;
}

static int wasm_consume_break(void) {
    if (!wasm_break_requested) return 0;
    wasm_break_requested = 0;
    return 1;
}

/*
 * Live-adjustable throttle. Mirrors the native --slowdown CLI flag:
 * host_runtime_check_timeout sleeps host_sim_slowdown_us microseconds
 * per statement/poll-tick, and bc_vm_poll_interrupts does the same on
 * every backward branch. Safe to call at any time — a plain int store
 * takes effect on the next poll.
 */
/*
 * Runtime-selectable memory-constraint simulation. Sets the runtime
 * heap_memory_size (and therefore the TryGetMemory cap + MEMORY
 * command output) to the dropdown-selected profile — 128 KB simulates
 * RP2040, 300 KB simulates RP2350, 1/2/4/8 MB are larger "for fun"
 * profiles. Clamped to the compile-time 8 MB ceiling. Must be called
 * before InitBasic/ClearRuntime so the page bitmap is sized correctly.
 */
extern uint32_t heap_memory_size;

EMSCRIPTEN_KEEPALIVE
void wasm_set_heap_size(int bytes) {
    if (bytes < 32 * 1024) bytes = 32 * 1024;
    if (bytes > (int)HEAP_MEMORY_SIZE) bytes = HEAP_MEMORY_SIZE;
    bytes &= ~(PAGESIZE - 1);
    heap_memory_size = (uint32_t)bytes;
}

extern int host_sim_slowdown_us;

EMSCRIPTEN_KEEPALIVE
void wasm_set_slowdown_us(int us) {
    if (us < 0) us = 0;
    host_sim_slowdown_us = us;
}

/*
 * Mirrors the MODE_REPL branch of host_main.c: set up the flash buffer,
 * InitBasic, configure runtime/keys, point host_sd_root at MEMFS, then
 * run_repl (inlined — host_main.c's run_repl isn't linked under WASM).
 *
 * Does not return under normal operation. MMBasic_RunPromptLoop longjmps
 * back to its own setjmp on every command; if it ever exits cleanly we
 * fall through to host_runtime_finish and then return to JS, at which
 * point the app is idle.
 */
/*
 * Framebuffer-console state. Must be set BEFORE MMBasic_PrintBanner fires,
 * because the banner is the first thing that emits characters through
 * putConsole → DisplayPutC. Mirrors host_main.c's --sim branch verbatim —
 * same flags, same font, same green-phosphor palette.
 */
static void wasm_configure_display_console(void) {
    extern unsigned char OptionConsole;
    extern short gui_font_width, gui_font_height;

    Option.DISPLAY_CONSOLE = 1;
    OptionConsole = 2;  /* screen only — no UART in the browser */

    /* 0x01 = font 0 (the built-in 8x12 font1) at 1x scale. Scale MUST
     * be nonzero; on error, MMBasic.c calls SetFont(Option.DefaultFont)
     * during recovery, and a scale of 0 zeroes out gui_font_width/
     * height and strands the cursor at column 0 forever. */
    gui_font = 0x01;
    gui_font_width  = 8;
    gui_font_height = 12;
    Option.Tab         = 4;
    Option.DefaultFont = 0x01;
    Option.ColourCode  = 1;

    /* PicoCalc-style green phosphor. Mirrored into Default* so
     * ResetDisplay (error path) restores the same look. */
    gui_fcolour       = 0x00FF00;
    gui_bcolour       = 0x000000;
    PromptFC          = 0x00FF00;
    PromptBC          = 0x000000;
    Option.DefaultFC  = 0x00FF00;
    Option.DefaultBC  = 0x000000;
}

static void *wasm_runtime_thread(void *arg) {
    (void)arg;

    /* Zero first half, 0xFF second half — same erased-flash simulation
     * host_main.c does. CFunction scan loops rely on the 0xFF to find
     * their terminator immediately. */
    memset(flash_prog_buf, 0x00, sizeof(flash_prog_buf) / 2);
    memset(flash_prog_buf + sizeof(flash_prog_buf) / 2, 0xFF,
           sizeof(flash_prog_buf) / 2);
    flash_progmemory = flash_prog_buf;

    InitBasic();
    bc_opt_level = 1;
    host_runtime_configure(0, NULL);
    host_runtime_configure_keys(NULL, 0);

    /* /sd is the emscripten MEMFS mount point — the worker's JS side
     * mounts IDBFS there on startup. */
    host_sd_root = "/sd";

    host_repl_mode = 1;
    vm_host_fat_reset();
    vm_sys_file_reset();
    vm_sys_pin_reset();
    ClearRuntime(true);
    MMErrMsg[0] = '\0';
    MMerrno = 0;

    host_runtime_begin();

    wasm_configure_display_console();
    Option.Width  = HRes / gui_font_width;   /* 320/8 = 40 cols */
    Option.Height = VRes / gui_font_height;  /* 320/12 = 26 rows */

    /* Re-snapshot Option into flash_option_buf AFTER the display
     * console is configured. host_runtime_begin took its snapshot
     * before our overrides; if we don't re-snapshot, the LoadOptions()
     * call inside error() (MMBasic.c:2835) reverts DISPLAY_CONSOLE to
     * 0 — which trips LCD_error and paints a bright-red overlay in the
     * middle of the screen on every BASIC error. */
    host_options_snapshot();

    MMBasic_PrintBanner();
    MMBasic_RunPromptLoop();  /* never returns under normal operation */

    host_runtime_finish();
    (void)wasm_consume_break;
    return NULL;
}

/*
 * Spawn the interpreter on a dedicated pthread and return immediately
 * so the worker's JS event loop stays responsive. The pthread owns
 * the long-running MMBasic runtime; its real (Atomics.wait-backed)
 * sleeps park just that thread, leaving postMessage handlers free to
 * process FS round-trips on the worker's main thread.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_boot(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, wasm_runtime_thread, NULL) != 0) {
        /* pthread_create shouldn't fail with PTHREAD_POOL_SIZE=1,
         * but if it does we're dead in the water — log and bail. */
        fprintf(stderr, "pthread_create for wasm runtime failed\n");
        return;
    }
    pthread_detach(tid);
}

/* ------------------------------------------------------------------------
 * Source-file loaders.
 *
 * host_main.c owns these on the native build, but we can't link host_main.c
 * under WASM. bc_runtime.c (shared VM code) and host_fs_shims.c both call
 * them, so we duplicate the implementations here. They are pure POSIX +
 * MMBasic-tokeniser plumbing — emscripten's MEMFS provides fopen/fread/
 * ftell, and tokenise() is unchanged shared code.
 *
 * A later phase can de-duplicate by moving these into a shared host file
 * (host_fs_shims.c would be the natural home). For Phase 1 we prefer zero
 * risk to the native build over DRY.
 * ------------------------------------------------------------------------ */

extern unsigned char *ProgMemory;
extern int PSize;
extern unsigned char inpbuf[];
extern unsigned char tknbuf[];
extern void tokenise(int console);
extern void flash_range_erase(uint32_t offset, size_t count);

#ifndef MAX_PROG_SIZE
#define MAX_PROG_SIZE (128 * 1024)
#endif

char *read_basic_source_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", filename);
        return NULL;
    }

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

static void host_update_continuation_setting(const char *line, unsigned char *continuation) {
    const char *p = line;
    if (!continuation) return;
    while (*p == ' ' || *p == '\t') p++;
    if (*p >= '0' && *p <= '9') {
        while (*p >= '0' && *p <= '9') p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    if (strncasecmp(p, "OPTION CONTINUATION LINES ON", 28) == 0 ||
        strncasecmp(p, "OPTION CONTINUATION LINES ENABLE", 32) == 0) {
        *continuation = '_';
    } else if (strncasecmp(p, "OPTION CONTINUATION LINES OFF", 29) == 0 ||
               strncasecmp(p, "OPTION CONTINUATION LINES DISABLE", 33) == 0) {
        *continuation = 0;
    }
}

static int host_read_logical_line(const char **linep, char *out, size_t out_cap,
                                  int *physical_line_io, int *line_no_out,
                                  unsigned char *continuation) {
    const char *line = *linep;
    size_t out_len = 0;
    int line_no = *physical_line_io;

    if (*line == '\0') return 0;
    out[0] = '\0';

    while (*line) {
        const char *eol = strchr(line, '\n');
        int len = eol ? (int)(eol - line) : (int)strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (out_len + (size_t)len > out_cap - 1) len = (int)((out_cap - 1) - out_len);
        memcpy(out + out_len, line, (size_t)len);
        out_len += (size_t)len;
        out[out_len] = '\0';

        (*physical_line_io)++;
        line = eol ? eol + 1 : line + strlen(line);

        if (*continuation && out_len >= 2 &&
            out[out_len - 2] == ' ' && out[out_len - 1] == *continuation) {
            out_len -= 2;
            out[out_len] = '\0';
            continue;
        }
        break;
    }

    *linep = line;
    *line_no_out = line_no;
    host_update_continuation_setting(out, continuation);
    return 1;
}

int load_basic_source(const char *source) {
    flash_range_erase(0, MAX_PROG_SIZE);
    unsigned char *pm = ProgMemory;
    const char *line = source;
    int physical_line = 1;
    unsigned char continuation = Option.continuation;

    while (*line) {
        char logical[STRINGSIZE + 1];
        int lineno = 0;
        if (!host_read_logical_line(&line, logical, sizeof(logical),
                                    &physical_line, &lineno, &continuation))
            break;
        int len = (int)strlen(logical);

        if (len > 0) {
            memcpy(inpbuf, logical, (size_t)len);
            inpbuf[len] = '\0';

            tokenise(0);

            unsigned char *tp = tknbuf;
            while (!(tp[0] == 0 && tp[1] == 0)) {
                *pm++ = *tp++;
            }
            *pm++ = 0;
        }
    }

    *pm++ = 0;
    *pm++ = 0;
    PSize = (int)(pm - ProgMemory);

    return 0;
}

/* emscripten's runtime still invokes main once the module resolves. We
 * do nothing here — JS calls _wasm_boot() explicitly after the canvas
 * is set up. -sINVOKE_RUN=0 would let us drop main entirely, but
 * leaving it in place costs nothing and keeps the libc initialisers
 * (__wasm_call_ctors, etc.) on their default path. */
int main(void) {
    return 0;
}
