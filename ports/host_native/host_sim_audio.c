/*
 * host_sim_audio.c -- JSON audio event emitter for the simulator.
 *
 * PLAY TONE / PLAY SOUND / PLAY STOP (and friends) are translated into
 * one-line JSON messages and enqueued. The Mongoose server thread in
 * host_sim_server.c drains the queue every poll iteration and pushes
 * each string as a WebSocket TEXT frame to every client. The browser
 * in web/audio.js owns the actual WebAudio graph.
 *
 * When MMBASIC_SIM is not defined (e.g. the mmbasic_test harness build)
 * everything no-ops so the same cmd_play / vm_sys_audio_* sources link
 * against both host variants.
 */

#include "host_sim_audio.h"
#include "drivers/web_console/web_console_protocol.h"

#include <stdlib.h>
#include <string.h>

#ifdef MMBASIC_SIM

#include <pthread.h>

extern int host_sim_active;

/*
 * One-writer (MMBasic thread) / one-reader (server thread) queue of
 * malloc'd JSON strings. The buffer grows on demand; drain empties it.
 */
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static char ** audio_msgs = NULL;
static int audio_count = 0;
static int audio_cap = 0;

static void push_msg(const char * msg) {
    if (!host_sim_active || !msg) return;
    pthread_mutex_lock(&audio_lock);
    if (audio_count == audio_cap) {
        int new_cap = audio_cap ? audio_cap * 2 : 16;
        char ** nb = realloc(audio_msgs, (size_t)new_cap * sizeof(char *));
        if (!nb) {
            pthread_mutex_unlock(&audio_lock);
            return;
        }
        audio_msgs = nb;
        audio_cap = new_cap;
    }
    audio_msgs[audio_count++] = strdup(msg);
    pthread_mutex_unlock(&audio_lock);
}

/*
 * %.6g is tight enough for frequencies and durations — PicoMite's own
 * PLAY TONE input range tops out at 20 kHz, well inside single precision.
 */
void host_sim_audio_tone(double left_hz, double right_hz,
                         int has_duration, int64_t duration_ms) {
    char buf[128];
    if (web_console_audio_build_tone(buf, sizeof(buf), left_hz, right_hz,
                                     has_duration, duration_ms) >= 0)
        push_msg(buf);
}

void host_sim_audio_stop(void) {
    char buf[32];
    if (web_console_audio_build_stop(buf, sizeof(buf)) >= 0)
        push_msg(buf);
}

void host_sim_audio_sound(int slot, const char * ch, const char * type,
                          double freq_hz, int volume) {
    char buf[160];
    if (web_console_audio_build_sound(buf, sizeof(buf), slot, ch, type,
                                      freq_hz, volume) >= 0)
        push_msg(buf);
}

void host_sim_audio_volume(int left, int right) {
    char buf[80];
    if (web_console_audio_build_volume(buf, sizeof(buf), left, right) >= 0)
        push_msg(buf);
}

void host_sim_audio_pause(void) {
    char buf[32];
    if (web_console_audio_build_pause(buf, sizeof(buf)) >= 0)
        push_msg(buf);
}

void host_sim_audio_resume(void) {
    char buf[32];
    if (web_console_audio_build_resume(buf, sizeof(buf)) >= 0)
        push_msg(buf);
}

size_t host_sim_audio_drain(char *** out_msgs, int * out_count) {
    pthread_mutex_lock(&audio_lock);
    char ** msgs = audio_msgs;
    int count = audio_count;
    audio_msgs = NULL;
    audio_count = 0;
    audio_cap = 0;
    pthread_mutex_unlock(&audio_lock);

    *out_msgs = msgs;
    *out_count = count;
    return (size_t)count;
}

void host_sim_audio_free_drain(char ** msgs, int count) {
    if (!msgs) return;
    for (int i = 0; i < count; ++i) free(msgs[i]);
    free(msgs);
}

#else /* !MMBASIC_SIM */

void host_sim_audio_tone(double l, double r, int h, int64_t m) {
    (void)l;
    (void)r;
    (void)h;
    (void)m;
}
void host_sim_audio_stop(void) {}
void host_sim_audio_sound(int s, const char * c, const char * t, double f, int v) {
    (void)s;
    (void)c;
    (void)t;
    (void)f;
    (void)v;
}
void host_sim_audio_volume(int l, int r) {
    (void)l;
    (void)r;
}
void host_sim_audio_pause(void) {}
void host_sim_audio_resume(void) {}

size_t host_sim_audio_drain(char *** m, int * c) {
    *m = NULL;
    *c = 0;
    return 0;
}
void host_sim_audio_free_drain(char ** m, int c) {
    (void)m;
    (void)c;
}

#endif
