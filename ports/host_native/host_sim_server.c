/*
 * host_sim_server.c -- Mongoose HTTP + WebSocket server for --sim mode.
 *
 * Runs on a background pthread. Serves static files from `web/` and
 * broadcasts the host framebuffer as RGBA binary WS frames at ~60fps
 * to every connected client on `/ws`.
 *
 * The MMBasic interpreter writes the framebuffer on the main thread
 * without any locking. The server reads it with memcpy under no lock;
 * a torn frame survives 16ms before the next broadcast overwrites it,
 * which is invisible in practice.
 *
 * Client → server messages (JSON) are decoded minimally here but are
 * not plumbed into the runtime until Phase 2 (keyboard).
 */

#include "vendor/mongoose.h"
#include "host_sim_server.h"
#include "host_sim_audio.h"
#include "drivers/web_console/web_console_protocol.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t host_sim_framebuffer_copy(uint32_t *dst, size_t dst_pixels);
extern void host_sim_framebuffer_dims(int *w, int *h);
extern void host_sim_push_key(int code);
extern size_t host_sim_cmd_drain(uint8_t **out_buf, size_t *out_cap);

/*
 * Per-connection state. New clients need one full-frame FRMB to bootstrap
 * the canvas; after that they just replay the shared CMDS stream the
 * server broadcasts every tick.
 */
typedef struct sim_client {
    int bootstrapped;
} sim_client;

struct sim_server {
    struct mg_mgr mgr;
    pthread_t thread;
    char listen_url[128];
    char web_root[1024];
    atomic_int running;
    uint64_t last_frame_ms;
    uint32_t *staging;
    size_t staging_capacity;
};

static struct sim_server g_server;

/*
 * Send one full-frame FRMB snapshot to a specific client. Used once per
 * client on first broadcast after connect, so the canvas starts with the
 * current framebuffer contents. After that the client just consumes the
 * CMDS stream.
 */
static void send_bootstrap_frame(struct mg_connection *c, struct sim_server *s,
                                 int w, int h) {
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > s->staging_capacity) {
        free(s->staging);
        s->staging = calloc(pixels, sizeof(uint32_t));
        if (!s->staging) { s->staging_capacity = 0; return; }
        s->staging_capacity = pixels;
    }
    host_sim_framebuffer_copy(s->staging, pixels);

    size_t msg_len = web_console_frmb_len(w, h);
    uint8_t *msg = malloc(msg_len);
    if (!msg) return;
    if (!web_console_pack_frmb(msg, msg_len, w, h, s->staging, pixels)) {
        free(msg);
        return;
    }
    mg_ws_send(c, msg, msg_len, WEBSOCKET_OP_BINARY);
    free(msg);
}

/*
 * Broadcast the queued-up graphics command stream to every connected WS
 * client as one "CMDS" message. Each CMDS message is:
 *   bytes 0..3 : magic "CMDS"
 *   bytes 4..5 : canvas width  (LE u16)
 *   bytes 6..7 : canvas height (LE u16)
 *   bytes 8..  : opaque opcode stream (see host_stubs_legacy.c for format)
 *
 * Fresh clients get one FRMB snapshot first so their canvas starts with
 * whatever's already been drawn.
 */
static void broadcast_frame(struct sim_server *s) {
    int w = 0, h = 0;
    host_sim_framebuffer_dims(&w, &h);
    if (w <= 0 || h <= 0) return;

    /* Drain all queued commands once. */
    uint8_t *cmd_bytes = NULL;
    size_t cmd_cap = 0;
    size_t cmd_len = host_sim_cmd_drain(&cmd_bytes, &cmd_cap);

    /* Bootstrap any new connections with a full frame; then broadcast the
     * drained command stream (if non-empty) to everyone. */
    struct mg_connection *c;
    for (c = s->mgr.conns; c != NULL; c = c->next) {
        if (!c->is_websocket) continue;
        sim_client *cs = (sim_client *)c->data;
        _Static_assert(sizeof(sim_client) <= MG_DATA_SIZE, "sim_client too big for c->data");
        if (!cs->bootstrapped) {
            send_bootstrap_frame(c, s, w, h);
            cs->bootstrapped = 1;
        }
    }

    if (cmd_len == 0) {
        free(cmd_bytes);
        return;
    }

    size_t msg_len = web_console_cmds_len(cmd_len);
    uint8_t *msg = malloc(msg_len);
    if (!msg) { free(cmd_bytes); return; }
    if (!web_console_pack_cmds(msg, msg_len, w, h, cmd_bytes, cmd_len)) {
        free(cmd_bytes);
        free(msg);
        return;
    }
    free(cmd_bytes);

    for (c = s->mgr.conns; c != NULL; c = c->next) {
        if (!c->is_websocket) continue;
        mg_ws_send(c, msg, msg_len, WEBSOCKET_OP_BINARY);
    }
    free(msg);
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    struct sim_server *s = (struct sim_server *)c->fn_data;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
            mg_ws_upgrade(c, hm, NULL);
        } else {
            struct mg_http_serve_opts opts = {0};
            opts.root_dir = s->web_root;
            mg_http_serve_dir(c, hm, &opts);
        }
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        /* Only text frames carry our JSON protocol. */
        if ((wm->flags & 0x0f) == WEBSOCKET_OP_TEXT) {
            int code = -1;
            if (web_console_parse_key_json(wm->data.buf, wm->data.len, &code)) {
                host_sim_push_key(code);
            }
        }
    } else if (ev == MG_EV_POLL) {
        /* No throttle: every poll iteration drains whatever commands the
         * MMBasic thread has produced and broadcasts them. With
         * mg_mgr_poll(mgr, 1) below, that's ~1ms latency end-to-end —
         * indistinguishable from immediate. broadcast_frame() is a no-op
         * when the queue is empty. */
        broadcast_frame(s);

        /* Drain audio events (JSON TEXT frames) and forward to every
         * connected WS client. Audio volume is low, so one message per
         * TEXT frame is fine — no batching needed. */
        char **audio_msgs = NULL;
        int audio_count = 0;
        host_sim_audio_drain(&audio_msgs, &audio_count);
        if (audio_count > 0) {
            for (int i = 0; i < audio_count; ++i) {
                const char *msg = audio_msgs[i];
                if (!msg) continue;
                size_t len = strlen(msg);
                for (struct mg_connection *wc = s->mgr.conns; wc; wc = wc->next) {
                    if (wc->is_websocket)
                        mg_ws_send(wc, msg, len, WEBSOCKET_OP_TEXT);
                }
            }
            host_sim_audio_free_drain(audio_msgs, audio_count);
        }
        (void)ev_data;
    }
}

static void sim_log_sink(char ch, void *param) {
    (void)ch;
    (void)param;  /* swallow all Mongoose log output — the REPL owns stdout/stderr */
}

static void *server_thread(void *arg) {
    struct sim_server *s = (struct sim_server *)arg;
    mg_log_set(MG_LL_NONE);
    mg_log_set_fn(sim_log_sink, NULL);
    mg_mgr_init(&s->mgr);
    struct mg_connection *lc = mg_http_listen(&s->mgr, s->listen_url, ev_handler, s);
    if (!lc) {
        atomic_store(&s->running, 0);
        mg_mgr_free(&s->mgr);
        return NULL;
    }
    while (atomic_load(&s->running)) {
        mg_mgr_poll(&s->mgr, 1);
    }
    mg_mgr_free(&s->mgr);
    return NULL;
}

int host_sim_server_start(const char *listen_addr, int port, const char *web_root) {
    memset(&g_server, 0, sizeof(g_server));
    snprintf(g_server.listen_url, sizeof(g_server.listen_url),
             "http://%s:%d", listen_addr ? listen_addr : "127.0.0.1", port);
    snprintf(g_server.web_root, sizeof(g_server.web_root), "%s",
             web_root ? web_root : "web");
    atomic_store(&g_server.running, 1);
    if (pthread_create(&g_server.thread, NULL, server_thread, &g_server) != 0) {
        perror("pthread_create");
        atomic_store(&g_server.running, 0);
        return -1;
    }
    return 0;
}

void host_sim_server_stop(void) {
    if (!atomic_load(&g_server.running)) return;
    atomic_store(&g_server.running, 0);
    pthread_join(g_server.thread, NULL);
    free(g_server.staging);
    g_server.staging = NULL;
    g_server.staging_capacity = 0;
}

/* ------------------------------------------------------------------------
 * 1ms tick thread — synthesizes the device's timer_callback (PicoMite.c:826)
 * ------------------------------------------------------------------------ */

/* These MMBasic globals are defined in host_stubs_legacy.c. Declared here
 * rather than pulled from MMBasic_Includes.h to keep this file free of the
 * full interpreter header tree. */
extern volatile int64_t mSecTimer;
extern volatile unsigned int AHRSTimer;
extern volatile unsigned int InkeyTimer;
extern volatile unsigned int PauseTimer;
extern volatile unsigned int IntPauseTimer;
extern volatile int ds18b20Timer;
extern volatile unsigned int GPSTimer;
extern volatile unsigned int I2CTimer;
extern volatile unsigned int MouseTimer;
extern volatile unsigned int clocktimer;
extern volatile unsigned int Timer1, Timer2, Timer3, Timer4, Timer5;
extern volatile int CursorTimer;
extern volatile unsigned int ScrewUpTimer;
extern volatile unsigned int WDTimer;
#ifndef NBRSETTICKS
#define NBRSETTICKS 4
#endif
#ifndef CURSOR_ON
#define CURSOR_ON 400
#endif
#ifndef CURSOR_OFF
#define CURSOR_OFF 250
#endif
extern volatile int TickTimer[NBRSETTICKS];
extern volatile unsigned char TickActive[NBRSETTICKS];

static pthread_t host_sim_tick_thread;
static atomic_int host_sim_tick_running;

static void *host_sim_tick_body(void *unused) {
    (void)unused;
    struct timespec req = { 0, 1 * 1000 * 1000 };   /* 1 ms */
    while (atomic_load(&host_sim_tick_running)) {
        nanosleep(&req, NULL);
        mSecTimer++;
        AHRSTimer++;
        InkeyTimer++;
        PauseTimer++;
        IntPauseTimer++;
        ds18b20Timer++;
        GPSTimer++;
        I2CTimer++;
        MouseTimer++;
        if (clocktimer) clocktimer--;
        if (Timer5) Timer5--;
        if (Timer4) Timer4--;
        if (Timer3) Timer3--;
        if (Timer2) Timer2--;
        if (Timer1) Timer1--;
        if (++CursorTimer > CURSOR_OFF + CURSOR_ON) CursorTimer = 0;
        if (ScrewUpTimer) ScrewUpTimer--;
        if (WDTimer) WDTimer--;   /* on device triggers watchdog; here just counts */
        for (int i = 0; i < NBRSETTICKS; ++i) if (TickActive[i]) TickTimer[i]++;
    }
    return NULL;
}

void host_sim_tick_start(void) {
    if (atomic_load(&host_sim_tick_running)) return;
    atomic_store(&host_sim_tick_running, 1);
    if (pthread_create(&host_sim_tick_thread, NULL, host_sim_tick_body, NULL) != 0) {
        atomic_store(&host_sim_tick_running, 0);
    }
}

void host_sim_tick_stop(void) {
    if (!atomic_load(&host_sim_tick_running)) return;
    atomic_store(&host_sim_tick_running, 0);
    pthread_join(host_sim_tick_thread, NULL);
}

/* ------------------------------------------------------------------------
 * Key queue — single-producer (WS thread), single-consumer (main thread).
 * ------------------------------------------------------------------------ */

#define HOST_SIM_KEYQ_LEN 128
static struct {
    pthread_mutex_t lock;
    uint8_t buf[HOST_SIM_KEYQ_LEN];
    int head, tail;
} host_sim_keyq = { .lock = PTHREAD_MUTEX_INITIALIZER };

void host_sim_push_key(int code) {
    if (code < 0 || code > 0xff) return;
    pthread_mutex_lock(&host_sim_keyq.lock);
    int next = (host_sim_keyq.head + 1) % HOST_SIM_KEYQ_LEN;
    if (next != host_sim_keyq.tail) {
        host_sim_keyq.buf[host_sim_keyq.head] = (uint8_t)code;
        host_sim_keyq.head = next;
    }
    pthread_mutex_unlock(&host_sim_keyq.lock);
}

int host_sim_pop_key(void) {
    int c = -1;
    pthread_mutex_lock(&host_sim_keyq.lock);
    if (host_sim_keyq.head != host_sim_keyq.tail) {
        c = host_sim_keyq.buf[host_sim_keyq.tail];
        host_sim_keyq.tail = (host_sim_keyq.tail + 1) % HOST_SIM_KEYQ_LEN;
    }
    pthread_mutex_unlock(&host_sim_keyq.lock);
    return c;
}

/* ------------------------------------------------------------------------
 * Graphics command stream — recorded by draw primitives, drained by the
 * broadcast loop and forwarded as one "CMDS" WebSocket message per tick.
 *
 * Protocol (all fields little-endian):
 *   OP_CLS    = 0x01 : u32 color
 *   OP_RECT   = 0x02 : i16 x, i16 y, u16 w, u16 h, u32 color
 *   OP_PIXEL  = 0x03 : i16 x, i16 y, u32 color
 *   OP_SCROLL = 0x04 : i16 lines, u32 bg
 *   OP_BLIT   = 0x05 : i16 x, i16 y, u16 w, u16 h, [RGBA rows]
 *
 * FASTGFX back-buffer draws bypass the queue; the SWAP-time memcpy into
 * the front buffer is enqueued as a single BLIT op.
 * ------------------------------------------------------------------------ */

extern int host_sim_active;

static pthread_mutex_t host_sim_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *host_sim_cmd_buf = NULL;
static size_t host_sim_cmd_cap = 0;
static size_t host_sim_cmd_len = 0;

int host_sim_cmds_target_is_front(void) {
    /* Only record draws that land on the visible framebuffer. FASTGFX
     * and FRAMEBUFFER/LAYER back buffers are invisible until a merge
     * or copy, which come through a separate BLIT op. */
    extern unsigned char *WriteBuf, *DisplayBuf;
    return (WriteBuf == NULL || WriteBuf == DisplayBuf);
}

static void host_sim_cmd_append(const void *bytes, size_t len) {
    if (!host_sim_active) return;
    pthread_mutex_lock(&host_sim_cmd_lock);
    if (host_sim_cmd_len + len > host_sim_cmd_cap) {
        size_t new_cap = host_sim_cmd_cap ? host_sim_cmd_cap * 2 : 4096;
        while (new_cap < host_sim_cmd_len + len) new_cap *= 2;
        uint8_t *nb = realloc(host_sim_cmd_buf, new_cap);
        if (!nb) { pthread_mutex_unlock(&host_sim_cmd_lock); return; }
        host_sim_cmd_buf = nb;
        host_sim_cmd_cap = new_cap;
    }
    memcpy(host_sim_cmd_buf + host_sim_cmd_len, bytes, len);
    host_sim_cmd_len += len;
    pthread_mutex_unlock(&host_sim_cmd_lock);
}

size_t host_sim_cmd_drain(uint8_t **out_buf, size_t *out_cap) {
    pthread_mutex_lock(&host_sim_cmd_lock);
    *out_buf = host_sim_cmd_buf;
    *out_cap = host_sim_cmd_cap;
    size_t n = host_sim_cmd_len;
    host_sim_cmd_buf = NULL;
    host_sim_cmd_cap = 0;
    host_sim_cmd_len = 0;
    pthread_mutex_unlock(&host_sim_cmd_lock);
    return n;
}

void host_sim_emit_cls(int colour) {
    if (!host_sim_cmds_target_is_front()) return;
    uint8_t buf[5];
    size_t len = web_console_pack_cmd_cls(buf, sizeof(buf), colour);
    host_sim_cmd_append(buf, len);
}

void host_sim_emit_rect(int x1, int y1, int x2, int y2, int colour) {
    if (!host_sim_cmds_target_is_front()) return;
    uint8_t buf[13];
    size_t len = web_console_pack_cmd_rect(buf, sizeof(buf),
                                           x1, y1, x2, y2, colour);
    host_sim_cmd_append(buf, len);
}

void host_sim_emit_pixel(int x, int y, int colour) {
    if (!host_sim_cmds_target_is_front()) return;
    uint8_t buf[9];
    size_t len = web_console_pack_cmd_pixel(buf, sizeof(buf), x, y, colour);
    host_sim_cmd_append(buf, len);
}

void host_sim_emit_scroll(int lines, int bg) {
    /* ScrollLCD operates on the front buffer unconditionally. */
    uint8_t buf[7];
    size_t len = web_console_pack_cmd_scroll(buf, sizeof(buf), lines, bg);
    host_sim_cmd_append(buf, len);
}

void host_sim_emit_blit(int x, int y, int w, int h, const uint32_t *pixels) {
    if (w <= 0 || h <= 0 || !pixels) return;
    pthread_mutex_lock(&host_sim_cmd_lock);
    size_t body_len = (size_t)w * (size_t)h * 4;
    size_t total = 9 + body_len;
    if (host_sim_cmd_len + total > host_sim_cmd_cap) {
        size_t new_cap = host_sim_cmd_cap ? host_sim_cmd_cap * 2 : 4096;
        while (new_cap < host_sim_cmd_len + total) new_cap *= 2;
        uint8_t *nb = realloc(host_sim_cmd_buf, new_cap);
        if (!nb) { pthread_mutex_unlock(&host_sim_cmd_lock); return; }
        host_sim_cmd_buf = nb;
        host_sim_cmd_cap = new_cap;
    }
    if (!web_console_pack_cmd_blit(host_sim_cmd_buf + host_sim_cmd_len,
                                   total, x, y, w, h, pixels)) {
        pthread_mutex_unlock(&host_sim_cmd_lock);
        return;
    }
    host_sim_cmd_len += total;
    pthread_mutex_unlock(&host_sim_cmd_lock);
}
