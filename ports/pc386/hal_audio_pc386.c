/*
 * ports/pc386/hal_audio_pc386.c — PC speaker audio HAL.
 *
 * The original PC speaker is PIT channel 2 gated through port 0x61.
 * This gives one square-wave voice, so PLAY TONE maps left/right to a
 * single mono frequency and PLAY SOUND remains unsupported until a real
 * multi-voice backend exists.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_audio.h"
#include "hal/hal_time.h"
#include "io.h"

#define PIT_FREQ_HZ 1193182u
#define PIT_CHAN2_DATA 0x42
#define PIT_MODE_REG 0x43
#define SPEAKER_CTRL_PORT 0x61

static int audio_paused;
static uint16_t audio_reload;
static uint8_t audio_saved_gate;

static void pc_speaker_off(void) {
    uint8_t gate = inb(SPEAKER_CTRL_PORT);
    outb(SPEAKER_CTRL_PORT, gate & (uint8_t)~0x03);
}

static void pc_speaker_program(uint16_t reload) {
    audio_reload = reload;

    /* Channel 2, lobyte/hibyte, mode 3 square wave, binary counter. */
    outb(PIT_MODE_REG, 0xB6);
    outb(PIT_CHAN2_DATA, (uint8_t)(reload & 0xFF));
    outb(PIT_CHAN2_DATA, (uint8_t)(reload >> 8));

    audio_saved_gate = inb(SPEAKER_CTRL_PORT);
    outb(SPEAKER_CTRL_PORT, audio_saved_gate | 0x03);
}

void hal_audio_init(void) {
    audio_paused = 0;
    audio_reload = 0;
    audio_saved_gate = inb(SPEAKER_CTRL_PORT);
    pc_speaker_off();
}

void hal_audio_tone(double left_hz, double right_hz, int has_duration, int64_t duration_ms) {
    double hz = left_hz > 0.0 ? left_hz : right_hz;
    if (right_hz > 0.0 && (left_hz <= 0.0 || right_hz < hz)) hz = right_hz;

    hal_time_us_64(); /* Force TSC calibration before we take PIT ch2. */
    pc_speaker_off();
    audio_paused = 0;
    audio_reload = 0;

    if (hz <= 0.0) return;
    if (hz < 18.3) hz = 18.3; /* 16-bit PIT divisor floor. */

    unsigned divisor = (unsigned)((double)PIT_FREQ_HZ / hz + 0.5);
    if (divisor < 1) divisor = 1;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;
    pc_speaker_program((uint16_t)divisor);

    if (has_duration) {
        uint64_t remaining_us = (uint64_t)duration_ms * 1000ull;
        while (remaining_us > 0) {
            uint32_t chunk = remaining_us > 1000000ull ? 1000000u : (uint32_t)remaining_us;
            hal_time_sleep_us(chunk);
            remaining_us -= chunk;
        }
        pc_speaker_off();
        audio_reload = 0;
    }
}

void hal_audio_sound(int slot, const char * ch, const char * type, double freq_hz, int volume) {
    (void)slot;
    (void)ch;
    (void)type;
    (void)freq_hz;
    (void)volume;
    error("PLAY SOUND not available until stage 6 (PC speaker)");
}

void hal_audio_stop(void) {
    pc_speaker_off();
    audio_paused = 0;
    audio_reload = 0;
}
void hal_audio_volume(int left_pct, int right_pct) {
    (void)left_pct;
    (void)right_pct;
}
void hal_audio_pause(void) {
    if (audio_reload == 0 || audio_paused) return;
    audio_saved_gate = inb(SPEAKER_CTRL_PORT);
    pc_speaker_off();
    audio_paused = 1;
}

void hal_audio_resume(void) {
    if (audio_reload == 0 || !audio_paused) return;
    pc_speaker_program(audio_reload);
    audio_paused = 0;
}

void pc386_audio_apply_options(void) {}

void cmd_sb16(void) {
    error("SB16 audio backend not built; rebuild with PC386_AUDIO=sb16");
}

/* Streamed-sample sink: file-stream PCM is not routed to this backend
 * yet; report "always room, never queued" so a decode runs to completion
 * without stalling. */
int hal_audio_sample_begin(int sample_rate_hz) {
    (void)sample_rate_hz;
    return 0;
}
void hal_audio_sample_end(void) {}
void hal_audio_sample_eof(void) {}
int hal_audio_sample_space(void) {
    return 4096;
}
int hal_audio_sample_queued(void) {
    return 0;
}
int hal_audio_sample_push(const int16_t * frames, int n) {
    (void)frames;
    return n;
}
int hal_audio_sample_acquire(int16_t ** frames, int * frame_capacity) {
    (void)frames;
    (void)frame_capacity;
    return 0;
}
void hal_audio_sample_commit(int frame_count) {
    (void)frame_count;
}

#include <stdlib.h>
void * hal_audio_workmem_alloc(unsigned long bytes) {
    return malloc((size_t)bytes);
}
void * hal_audio_workmem_realloc(void * p, unsigned long bytes) {
    return realloc(p, (size_t)bytes);
}
void hal_audio_workmem_free(void * p) {
    free(p);
}
