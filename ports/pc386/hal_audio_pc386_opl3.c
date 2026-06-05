/*
 * ports/pc386/hal_audio_pc386_opl3.c - Yamaha OPL3 / AdLib audio HAL.
 *
 * The Pocket386 exposes a Yamaha YMF262/OPL3-compatible FM synth at the
 * standard AdLib ports 0x388/0x389. This backend supports PLAY TONE and
 * PLAY SOUND by programming two-operator OPL voices directly. It does not
 * attempt PCM playback; OPL3 is an FM synthesizer, not a DAC.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_audio.h"
#include "hal/hal_time.h"
#include "io.h"

#define OPL_ADDR 0x388u
#define OPL_DATA 0x389u
#define OPL3_ADDR 0x38Au
#define OPL3_DATA 0x38Bu

#define OPL_TONE_LEFT_CH 4
#define OPL_TONE_RIGHT_CH 5
#define OPL_TONE_VOICES 2
#define OPL_SOUND_VOICES 4

typedef struct {
    uint8_t active;
    uint8_t ch;
    uint8_t pan;
    uint8_t type;
    uint8_t volume;
    double hz;
} opl_voice_t;

static opl_voice_t sound_voice[OPL_SOUND_VOICES];
static opl_voice_t tone_voice[OPL_TONE_VOICES];
static int opl_ready;
static int opl_paused;
static int master_left = 100;
static int master_right = 100;

static const uint8_t opl_mod_offset[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

static void opl_delay_addr(void) {
    for (int i = 0; i < 6; i++) (void)inb(OPL_ADDR);
}

static void opl_delay_data(void) {
    for (int i = 0; i < 35; i++) (void)inb(OPL_ADDR);
}

static void opl_write_bank(uint16_t addr_port, uint16_t data_port, uint8_t reg, uint8_t val) {
    outb(addr_port, reg);
    opl_delay_addr();
    outb(data_port, val);
    opl_delay_data();
}

static void opl_write(uint8_t reg, uint8_t val) {
    opl_write_bank(OPL_ADDR, OPL_DATA, reg, val);
}

static void opl_write3(uint8_t reg, uint8_t val) {
    opl_write_bank(OPL3_ADDR, OPL3_DATA, reg, val);
}

static uint8_t carrier_for(uint8_t ch) {
    return (uint8_t)(opl_mod_offset[ch] + 3u);
}

static uint8_t attenuation_for(uint8_t volume, uint8_t pan) {
    int v = volume;
    if (v < 0) v = 0;
    if (v > 25) v = 25;

    int master = 0;
    if (pan == 0x10)
        master = master_left;
    else if (pan == 0x20)
        master = master_right;
    else
        master = (master_left + master_right) / 2;

    int scaled = v * master;
    int level = 63 - (scaled * 63 + 1250) / 2500;
    if (level < 0) level = 0;
    if (level > 63) level = 63;
    return (uint8_t)level;
}

static uint8_t wave_for(uint8_t type) {
    switch (type) {
    case 'Q':
        return 1; /* clipped/half sine: square-ish */
    case 'T':
        return 6; /* OPL3 waveform, triangle-ish */
    case 'W':
        return 4; /* double-speed sine: brighter saw-ish */
    case 'P':
    case 'N':
        return 7; /* harshest OPL3 waveform; not true noise */
    case 'S':
    default:
        return 0;
    }
}

static void opl_patch_voice(uint8_t ch, uint8_t type, uint8_t volume, uint8_t pan) {
    uint8_t mod = opl_mod_offset[ch];
    uint8_t car = carrier_for(ch);
    uint8_t wave = wave_for(type);
    uint8_t carrier_level = attenuation_for(volume, pan);

    uint8_t mod_level = 0x16;
    uint8_t feedback = 0x08; /* FM connection with light feedback for audible tones. */
    uint8_t mod_env = 0xF0;
    uint8_t car_env = 0xF0;
    uint8_t mod_sr = 0x05;
    uint8_t car_sr = 0x05;

    if (type == 'Q') {
        mod_level = 0x12;
        feedback = 0x0E;
        car_env = 0xF3;
    } else if (type == 'T') {
        mod_level = 0x30;
        feedback = 0x04;
        car_env = 0xE4;
    } else if (type == 'W') {
        mod_level = 0x08;
        feedback = 0x0A;
        car_env = 0xF2;
    } else if (type == 'P' || type == 'N') {
        mod_level = 0x00;
        feedback = 0x0E;
        mod_env = 0xFF;
        car_env = 0xFF;
        mod_sr = 0x11;
        car_sr = 0x11;
    }

    opl_write((uint8_t)(0x20u + mod), 0x21); /* modulator mult, sustain */
    opl_write((uint8_t)(0x20u + car), 0x21); /* carrier mult, sustain */
    opl_write((uint8_t)(0x40u + mod), mod_level);
    opl_write((uint8_t)(0x40u + car), carrier_level);
    opl_write((uint8_t)(0x60u + mod), mod_env);
    opl_write((uint8_t)(0x60u + car), car_env);
    opl_write((uint8_t)(0x80u + mod), mod_sr);
    opl_write((uint8_t)(0x80u + car), car_sr);
    opl_write((uint8_t)(0xE0u + mod), wave);
    opl_write((uint8_t)(0xE0u + car), wave);
    opl_write((uint8_t)(0xC0u + ch), (uint8_t)(pan | feedback));
}

static unsigned opl_fnum_for(double hz, uint8_t * block_out) {
    if (hz < 1.0) hz = 1.0;
    if (hz > 6208.0) hz = 6208.0;

    for (int block = 0; block < 7; block++) {
        double scale = (double)(1u << (20 - block));
        unsigned fnum = (unsigned)(hz * scale / 49716.0 + 0.5);
        if (fnum <= 1023u) {
            *block_out = (uint8_t)block;
            return fnum < 1u ? 1u : fnum;
        }
    }
    *block_out = 7;
    unsigned fnum = (unsigned)(hz * 8192.0 / 49716.0 + 0.5);
    if (fnum < 1u) fnum = 1u;
    if (fnum > 1023u) fnum = 1023u;
    return fnum;
}

static void opl_key_off(uint8_t ch) {
    opl_write((uint8_t)(0xB0u + ch), 0x00);
}

static void opl_key_on(uint8_t ch, double hz) {
    uint8_t block = 0;
    unsigned fnum = opl_fnum_for(hz, &block);
    opl_write((uint8_t)(0xA0u + ch), (uint8_t)(fnum & 0xFFu));
    opl_write((uint8_t)(0xB0u + ch), (uint8_t)(0x20u | ((block & 7u) << 2) | ((fnum >> 8) & 3u)));
}

static void opl_reset(void) {
    for (unsigned r = 0; r < 256; r++) opl_write((uint8_t)r, 0);
    for (unsigned r = 0; r < 256; r++) opl_write3((uint8_t)r, 0);

    opl_write3(0x05, 0x01); /* OPL3 mode, enables stereo pan bits. */
    opl_write(0x01, 0x20);  /* Waveform select enable. */
    opl_write(0x08, 0x00);
    opl_write(0xBD, 0x00); /* Rhythm mode off. */

    for (uint8_t ch = 0; ch < 9; ch++) {
        opl_patch_voice(ch, 'S', 0, 0x30);
        opl_key_off(ch);
    }
}

void hal_audio_init(void) {
    master_left = 100;
    master_right = 100;
    opl_paused = 0;
    memset(sound_voice, 0, sizeof(sound_voice));
    memset(tone_voice, 0, sizeof(tone_voice));
    opl_reset();
    opl_ready = 1;
}

static void ensure_ready(void) {
    if (!opl_ready) hal_audio_init();
}

void hal_audio_tone(double left_hz, double right_hz, int has_duration, int64_t duration_ms) {
    ensure_ready();
    hal_audio_stop();

    if (left_hz <= 0.0 && right_hz <= 0.0) return;

    if (left_hz > 0.0) {
        tone_voice[0] = (opl_voice_t){1, OPL_TONE_LEFT_CH, 0x10, 'S', 25, left_hz};
        opl_patch_voice(tone_voice[0].ch, tone_voice[0].type, tone_voice[0].volume, tone_voice[0].pan);
        opl_key_on(tone_voice[0].ch, tone_voice[0].hz);
    }
    if (right_hz > 0.0) {
        tone_voice[1] = (opl_voice_t){1, OPL_TONE_RIGHT_CH, 0x20, 'S', 25, right_hz};
        opl_patch_voice(tone_voice[1].ch, tone_voice[1].type, tone_voice[1].volume, tone_voice[1].pan);
        opl_key_on(tone_voice[1].ch, tone_voice[1].hz);
    }

    if (has_duration) {
        uint64_t remaining_us = (uint64_t)duration_ms * 1000ull;
        while (remaining_us > 0) {
            uint32_t chunk = remaining_us > 1000000ull ? 1000000u : (uint32_t)remaining_us;
            hal_time_sleep_us(chunk);
            remaining_us -= chunk;
        }
        for (int i = 0; i < OPL_TONE_VOICES; i++) {
            if (tone_voice[i].active) {
                opl_key_off(tone_voice[i].ch);
                tone_voice[i].active = 0;
            }
        }
    }
}

void hal_audio_sound(int slot, const char * ch, const char * type, double freq_hz, int volume) {
    ensure_ready();
    if (slot < 1 || slot > OPL_SOUND_VOICES) return;

    for (int i = 0; i < OPL_TONE_VOICES; i++) {
        tone_voice[i].active = 0;
        opl_key_off(tone_voice[i].ch ? tone_voice[i].ch : (uint8_t)(OPL_TONE_LEFT_CH + i));
    }

    int idx = slot - 1;
    uint8_t voice_ch = (uint8_t)idx;
    uint8_t wave = type && type[0] ? (uint8_t)mytoupper(type[0]) : 'O';
    if (wave == 'O' || volume <= 0) {
        opl_key_off(voice_ch);
        memset(&sound_voice[idx], 0, sizeof(sound_voice[idx]));
        return;
    }

    uint8_t pan = 0x30;
    if (ch && mytoupper(ch[0]) == 'L')
        pan = 0x10;
    else if (ch && mytoupper(ch[0]) == 'R')
        pan = 0x20;

    if (volume > 25) volume = 25;
    sound_voice[idx] = (opl_voice_t){1, voice_ch, pan, wave, (uint8_t)volume, freq_hz};
    opl_patch_voice(voice_ch, wave, (uint8_t)volume, pan);
    opl_key_on(voice_ch, freq_hz);
}

void hal_audio_stop(void) {
    ensure_ready();
    for (uint8_t ch = 0; ch < 9; ch++) opl_key_off(ch);
    memset(sound_voice, 0, sizeof(sound_voice));
    memset(tone_voice, 0, sizeof(tone_voice));
    opl_paused = 0;
}

void hal_audio_volume(int left_pct, int right_pct) {
    ensure_ready();
    if (left_pct < 0) left_pct = 0;
    if (left_pct > 100) left_pct = 100;
    if (right_pct < 0) right_pct = 0;
    if (right_pct > 100) right_pct = 100;
    master_left = left_pct;
    master_right = right_pct;

    for (int i = 0; i < OPL_TONE_VOICES; i++) {
        if (tone_voice[i].active)
            opl_patch_voice(tone_voice[i].ch, tone_voice[i].type, tone_voice[i].volume, tone_voice[i].pan);
    }
    for (int i = 0; i < OPL_SOUND_VOICES; i++) {
        if (sound_voice[i].active)
            opl_patch_voice(sound_voice[i].ch, sound_voice[i].type, sound_voice[i].volume, sound_voice[i].pan);
    }
}

void hal_audio_pause(void) {
    ensure_ready();
    if (opl_paused) return;
    for (uint8_t ch = 0; ch < 9; ch++) opl_key_off(ch);
    opl_paused = 1;
}

void hal_audio_resume(void) {
    ensure_ready();
    if (!opl_paused) return;
    opl_paused = 0;
    for (int i = 0; i < OPL_TONE_VOICES; i++) {
        if (tone_voice[i].active) opl_key_on(tone_voice[i].ch, tone_voice[i].hz);
    }
    for (int i = 0; i < OPL_SOUND_VOICES; i++) {
        if (sound_voice[i].active) opl_key_on(sound_voice[i].ch, sound_voice[i].hz);
    }
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
