/*
 * host_fastgfx.c — host implementations of FASTGFX and FRAMEBUFFER commands.
 *
 * Device uses DMA + dual-core to double-buffer against an LCD; host
 * keeps a back plane in RAM and copies it to host_framebuffer on SWAP.
 * FRAMEBUFFER CREATE/LAYER/WRITE/SYNC/WAIT/COPY/MERGE/CLOSE delegate
 * directly to host_fb.c's layer bookkeeping.
 */

#include <setjmp.h>
#include <string.h>

#ifdef MMBASIC_WASM
#include <emscripten.h>
#endif

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "host_fb.h"
#include "host_sim_server.h"
#include "host_time.h"

static int host_fastgfx_active = 0;
static int host_fastgfx_fps = 0;
static uint64_t host_fastgfx_next_sync_us = 0;

#ifdef MMBASIC_WASM
/* Written from JS every requestAnimationFrame — kept exported for
 * smoke tests that want to observe the main-thread rAF cadence from
 * wasm's vantage point. bc_fastgfx_sync no longer spins on it. */
volatile uint32_t wasm_vsync_counter = 0;
#endif

void host_fastgfx_reset_state(void) {
    host_fastgfx_active = 0;
    host_fastgfx_fps = 0;
    host_fastgfx_next_sync_us = 0;
}

void bc_fastgfx_swap(void) {
    /* Present the back buffer: copy it into host_framebuffer so local
     * snapshots stay correct, and emit one BLIT command so browsers
     * get a single full-frame update. Unlike the per-primitive stream
     * this is one message per visible frame — browsers never see a
     * torn or in-progress frame. */
    host_fb_ensure();
    if (!host_fastgfx_active || !host_fastgfx_back || !host_framebuffer) return;
    size_t pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    memcpy(host_framebuffer, host_fastgfx_back, pixels * sizeof(uint32_t));
    /* Tell the WASM rAF loop there's a new frame to paint. */
    host_fb_bump_generation();
#ifdef MMBASIC_SIM
    host_sim_emit_blit(0, 0, host_fb_width, host_fb_height, host_fastgfx_back);
#endif
}

void bc_fastgfx_sync(void) {
    if (!host_fastgfx_active || host_fastgfx_fps <= 0) return;
    uint64_t frame_us = 1000000ULL / (uint64_t)host_fastgfx_fps;
    uint64_t now = host_time_us_64();
    if (host_fastgfx_next_sync_us == 0) host_fastgfx_next_sync_us = now + frame_us;
    if (now < host_fastgfx_next_sync_us) host_sleep_us(host_fastgfx_next_sync_us - now);
    host_fastgfx_next_sync_us += frame_us;
#ifdef MMBASIC_WASM
    /* If we fell more than two frames behind the nominal deadline
     * (e.g. a one-off GC pause or tab visibility change), resync to
     * wall clock so we don't spend the rest of the session spinning
     * to catch up. */
    now = host_time_us_64();
    if (host_fastgfx_next_sync_us + 2 * frame_us < now) {
        host_fastgfx_next_sync_us = now + frame_us;
    }
    (void)wasm_vsync_counter;
#endif
}

void bc_fastgfx_create(void) {
    host_fb_ensure();
    size_t pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    if (!host_fastgfx_back) {
        host_fastgfx_back = calloc(pixels, sizeof(uint32_t));
        if (!host_fastgfx_back) error("Not enough memory");
    }
    /* Seed the back buffer with the current front contents so existing
     * text/graphics aren't wiped out at the start of the first frame. */
    if (host_framebuffer) memcpy(host_fastgfx_back, host_framebuffer, pixels * sizeof(uint32_t));
    /* Point the graphics WriteBuf at the back buffer so every primitive
     * draws into it until CLOSE. host_fb_current_target looks at
     * WriteBuf to decide the destination. */
    WriteBuf = (unsigned char *)host_fastgfx_back;
    host_fastgfx_active = 1;
    host_fastgfx_next_sync_us = 0;
}

void bc_fastgfx_close(void) {
    if (!host_fastgfx_active) error("FASTGFX not active");
    /* Flip the final back contents out to the front so the last frame
     * the game drew stays visible after CLOSE. */
    if (host_fastgfx_back && host_framebuffer) {
        size_t pixels = (size_t)host_fb_width * (size_t)host_fb_height;
        memcpy(host_framebuffer, host_fastgfx_back, pixels * sizeof(uint32_t));
    }
    free(host_fastgfx_back);
    host_fastgfx_back = NULL;
    WriteBuf = NULL;
    host_fastgfx_active = 0;
    host_fastgfx_next_sync_us = 0;
}

void bc_fastgfx_reset(void) {
    free(host_fastgfx_back);
    host_fastgfx_back = NULL;
    WriteBuf = NULL;
    host_fastgfx_active = 0;
    host_fastgfx_next_sync_us = 0;
}

void bc_fastgfx_set_fps(int fps) {
    if (fps < 1 || fps > 1000) error("Number out of bounds");
    host_fastgfx_fps = fps;
    host_fastgfx_next_sync_us = 0;
}

void cmd_fastgfx(void) {
    unsigned char *p;

    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        checkend(p);
        bc_fastgfx_create();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        checkend(p);
        bc_fastgfx_close();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SWAP"))) {
        checkend(p);
        bc_fastgfx_swap();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        checkend(p);
        bc_fastgfx_sync();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"FPS"))) {
        bc_fastgfx_set_fps(getint(p, 1, 1000));
        return;
    }

    error("Syntax");
}

void cmd_framebuffer(void) {
    unsigned char *p = NULL;

    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        if (checkstring(p, (unsigned char *)"FAST")) {
            /* FAST flag accepted but no-op on host (no DMA) */
        } else {
            checkend(p);
        }
        host_framebuffer_create();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"LAYER"))) {
        getargs(&p, 1, (unsigned char *)",");
        if (argc == 0) {
            host_framebuffer_layer(0, 0);
            return;
        }
        if (argc != 1) error("Syntax");
        host_framebuffer_layer(1, getint(argv[0], 0, 0xFFFFFF));
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"WRITE"))) {
        if (checkstring(p, (unsigned char *)"N")) {
            host_framebuffer_write('N');
            return;
        }
        if (checkstring(p, (unsigned char *)"F")) {
            host_framebuffer_write('F');
            return;
        }
        if (checkstring(p, (unsigned char *)"L")) {
            host_framebuffer_write('L');
            return;
        }
        getargs(&p, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax");
        {
            char *q = (char *)getCstring(argv[0]);
            if (strcasecmp(q, "N") == 0) host_framebuffer_write('N');
            else if (strcasecmp(q, "F") == 0) host_framebuffer_write('F');
            else if (strcasecmp(q, "L") == 0) host_framebuffer_write('L');
            else error("Syntax");
        }
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        checkend(p);
        host_framebuffer_sync();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"WAIT"))) {
        checkend(p);
        host_framebuffer_wait();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"COPY"))) {
        char from = 0;
        char to = 0;
        int background = 0;
        getargs(&p, 5, (unsigned char *)",");
        if (!(argc == 3 || argc == 5)) error("Syntax");
        if (checkstring(argv[0], (unsigned char *)"N")) from = 'N';
        else if (checkstring(argv[0], (unsigned char *)"F")) from = 'F';
        else if (checkstring(argv[0], (unsigned char *)"L")) from = 'L';
        else {
            char *q = (char *)getCstring(argv[0]);
            if (strcasecmp(q, "N") == 0) from = 'N';
            else if (strcasecmp(q, "F") == 0) from = 'F';
            else if (strcasecmp(q, "L") == 0) from = 'L';
            else error("Syntax");
        }
        if (checkstring(argv[2], (unsigned char *)"N")) to = 'N';
        else if (checkstring(argv[2], (unsigned char *)"F")) to = 'F';
        else if (checkstring(argv[2], (unsigned char *)"L")) to = 'L';
        else {
            char *q = (char *)getCstring(argv[2]);
            if (strcasecmp(q, "N") == 0) to = 'N';
            else if (strcasecmp(q, "F") == 0) to = 'F';
            else if (strcasecmp(q, "L") == 0) to = 'L';
            else error("Syntax");
        }
        if (argc == 5) {
            if (!checkstring(argv[4], (unsigned char *)"B")) error("Syntax");
            background = 1;
        }
        host_framebuffer_copy(from, to, background);
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"MERGE"))) {
        int colour = 0;
        int has_colour = 0;
        int mode = BC_FB_MERGE_MODE_NOW;
        int has_rate = 0;
        int rate_ms = 0;
        getargs(&p, 5, (unsigned char *)",");
        if (argc >= 1 && *argv[0]) {
            colour = getint(argv[0], 0, 0xFFFFFF);
            has_colour = 1;
        }
        if (argc >= 3 && *argv[2]) {
            if (checkstring(argv[2], (unsigned char *)"B")) mode = BC_FB_MERGE_MODE_B;
            else if (checkstring(argv[2], (unsigned char *)"R")) mode = BC_FB_MERGE_MODE_R;
            else if (checkstring(argv[2], (unsigned char *)"A")) mode = BC_FB_MERGE_MODE_A;
            else error("Syntax");
        }
        if (argc == 5 && *argv[4]) {
            rate_ms = getint(argv[4], 0, 600000);
            has_rate = 1;
        }
        host_framebuffer_merge(has_colour, colour, mode, has_rate, rate_ms);
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        if (checkstring(p, (unsigned char *)"F")) {
            host_framebuffer_close('F');
            return;
        }
        if (checkstring(p, (unsigned char *)"L")) {
            host_framebuffer_close('L');
            return;
        }
        checkend(p);
        host_framebuffer_close('A');
        return;
    }

    error("Syntax");
}
