/*
 * ports/pc386/hal_audio_pc386_sb16.c — Sound Blaster 16 audio HAL.
 *
 * Minimal SB16 backend for QEMU and real ISA-compatible hardware at the
 * conventional base address. PLAY TONE uses 8-bit unsigned stereo PCM over
 * DMA channel 1. PLAY SOUND uses the same generated PCM path.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_audio_control.h"
#include "hal/hal_audio_stream.h"
#include "hal/hal_time.h"
#include "io.h"

#define SB_MIXER_ADDR (sb_base + 0x04u)
#define SB_MIXER_DATA (sb_base + 0x05u)
#define SB_DSP_RESET (sb_base + 0x06u)
#define SB_DSP_READ (sb_base + 0x0Au)
#define SB_DSP_WRITE (sb_base + 0x0Cu)
#define SB_DSP_READ_STATUS (sb_base + 0x0Eu)
#define SB_DSP_ACK8 (sb_base + 0x0Eu)

#define DMA1_MASK 0x0Au
#define DMA1_MODE 0x0Bu
#define DMA1_CLEAR_FF 0x0Cu
#define DMA1_CH1_ADDR 0x02u
#define DMA1_CH1_COUNT 0x03u
#define DMA1_CH1_PAGE 0x83u

#define SB_RATE_HZ 22050u
#define SB_BUFFER_BYTES 65536u
#define SB_BUFFER_FRAMES (SB_BUFFER_BYTES / 2u)

static uint8_t sb_dma_buffer[SB_BUFFER_BYTES] __attribute__((aligned(65536)));
static uint16_t sb_base = 0x220u;
static uint8_t sb_irq = 5u;
static uint8_t sb_dma = 1u;
static uint8_t sb_dma16 = 5u;
static int sb_available;
static int sb_playing;
static int sb_paused;
static int sb_probe_attempted;
static uint32_t sb_noise_nonce;

typedef struct {
    uint8_t active;
    char wave;
    double hz;
    uint8_t volume;
} sb_voice_t;

static sb_voice_t sb_sound_left[4];
static sb_voice_t sb_sound_right[4];

static const uint16_t dma_addr_ports[4] = {0x00u, 0x02u, 0x04u, 0x06u};
static const uint16_t dma_count_ports[4] = {0x01u, 0x03u, 0x05u, 0x07u};
static const uint16_t dma_page_ports[4] = {0x87u, 0x83u, 0x81u, 0x82u};

static void sb_stop_dma(void);

static unsigned u_gcd(unsigned a, unsigned b) {
    while (b != 0) {
        unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static unsigned u_lcm_bounded(unsigned a, unsigned b, unsigned limit) {
    unsigned g = u_gcd(a, b);
    unsigned scaled = a / g;
    if (scaled > limit / b) return 0;
    unsigned out = scaled * b;
    return out <= limit ? out : 0;
}

static unsigned tone_hz_int(double hz) {
    if (hz <= 0.0) return 0;
    unsigned rounded = (unsigned)(hz + 0.5);
    double diff = hz - (double)rounded;
    if (diff < 0.0) diff = -diff;
    return diff < 0.001 ? rounded : 0;
}

static int dsp_read_ready(void) {
    return (inb(SB_DSP_READ_STATUS) & 0x80) != 0;
}

static int dsp_write_ready(void) {
    return (inb(SB_DSP_WRITE) & 0x80) == 0;
}

static int dsp_read_byte(uint8_t * out) {
    for (int i = 0; i < 100000; i++) {
        if (dsp_read_ready()) {
            *out = inb(SB_DSP_READ);
            return 1;
        }
    }
    return 0;
}

static int dsp_write_byte(uint8_t v) {
    for (int i = 0; i < 100000; i++) {
        if (dsp_write_ready()) {
            outb(SB_DSP_WRITE, v);
            return 1;
        }
    }
    return 0;
}

static int sb_reset(void) {
    outb(SB_DSP_RESET, 1);
    hal_time_sleep_us(100);
    outb(SB_DSP_RESET, 0);
    hal_time_sleep_us(100);

    uint8_t v = 0;
    return dsp_read_byte(&v) && v == 0xAA;
}

static void sb_speaker_on(void) {
    (void)dsp_write_byte(0xD1);
}

static void sb_speaker_off(void) {
    (void)dsp_write_byte(0xD3);
}

static int sb_probe(void) {
    if (sb_available) return 1;

    hal_time_us_64();
    sb_probe_attempted = 1;
    sb_available = sb_reset();
    if (sb_available) {
        outb(SB_MIXER_ADDR, 0x22); /* master volume */
        outb(SB_MIXER_DATA, 0xEE);
        sb_speaker_off();
    }
    return sb_available;
}

static void sb_config_from_options(void) {
    uint16_t base = Option.pc386_sb_base ? Option.pc386_sb_base : 0x220u;
    uint8_t irq = Option.pc386_sb_irq ? Option.pc386_sb_irq : 5u;
    uint8_t dma = Option.pc386_sb_dma;
    uint8_t dma16 = Option.pc386_sb_dma16 ? Option.pc386_sb_dma16 : 5u;
    if (dma == 0 || dma > 3 || dma == 2) dma = 1;
    if (base == sb_base && irq == sb_irq && dma == sb_dma && dma16 == sb_dma16) return;
    sb_stop_dma();
    sb_base = base;
    sb_irq = irq;
    sb_dma = dma;
    sb_dma16 = dma16;
    sb_available = 0;
    sb_probe_attempted = 0;
}

void pc386_audio_apply_options(void) {
    sb_config_from_options();
}

static void sb_stop_dma(void) {
    if (!sb_available) return;
    (void)dsp_write_byte(0xD0); /* pause 8-bit DMA */
    (void)dsp_write_byte(0xDA); /* exit 8-bit auto-init DMA */
    (void)inb(SB_DSP_ACK8);
    sb_speaker_off();
    outb(DMA1_MASK, 0x04u | sb_dma);
    sb_playing = 0;
    sb_paused = 0;
}

static void sb_clear_sounds(void) {
    memset(sb_sound_left, 0, sizeof(sb_sound_left));
    memset(sb_sound_right, 0, sizeof(sb_sound_right));
}

static void sb_program_dma(uintptr_t addr, unsigned count) {
    outb(DMA1_MASK, 0x04u | sb_dma);
    outb(DMA1_CLEAR_FF, 0);
    outb(DMA1_MODE, (uint8_t)(0x58u | sb_dma)); /* auto-init, read, single */

    outb(dma_addr_ports[sb_dma], (uint8_t)(addr & 0xFF));
    outb(dma_addr_ports[sb_dma], (uint8_t)((addr >> 8) & 0xFF));
    outb(dma_page_ports[sb_dma], (uint8_t)((addr >> 16) & 0xFF));

    unsigned last = count - 1u;
    outb(dma_count_ports[sb_dma], (uint8_t)(last & 0xFF));
    outb(dma_count_ports[sb_dma], (uint8_t)((last >> 8) & 0xFF));
    outb(DMA1_MASK, sb_dma);
}

static uint8_t tone_sample(double hz, unsigned frame) {
    if (hz <= 0.0) return 128;
    double pos = (double)frame * hz / (double)SB_RATE_HZ;
    unsigned whole = (unsigned)pos;
    double frac = pos - (double)whole;
    return frac < 0.5 ? 224 : 32;
}

static unsigned tone_period_frames(double hz) {
    unsigned hz_int = tone_hz_int(hz);
    if (hz_int == 0) return hz <= 0.0 ? 1u : 0u;
    unsigned g = u_gcd(SB_RATE_HZ, hz_int);
    return SB_RATE_HZ / g;
}

static unsigned sb_tone_frames(double left_hz, double right_hz) {
    unsigned left_period = tone_period_frames(left_hz);
    unsigned right_period = tone_period_frames(right_hz);
    if (left_period != 0 && right_period != 0) {
        unsigned frames = u_lcm_bounded(left_period, right_period, SB_BUFFER_FRAMES);
        if (frames != 0) return frames;
    }
    return SB_BUFFER_FRAMES;
}

static unsigned sb_fill_tone(double left_hz, double right_hz) {
    unsigned frames = sb_tone_frames(left_hz, right_hz);
    for (unsigned frame = 0; frame < frames; frame++) {
        sb_dma_buffer[frame * 2u + 0u] = tone_sample(left_hz, frame);
        sb_dma_buffer[frame * 2u + 1u] = tone_sample(right_hz, frame);
    }
    return frames;
}

static int voice_active(void) {
    for (int i = 0; i < 4; i++) {
        if (sb_sound_left[i].active || sb_sound_right[i].active) return 1;
    }
    return 0;
}

static unsigned sb_sound_frames(void) {
    unsigned frames = 1;
    for (int i = 0; i < 4; i++) {
        sb_voice_t * voices[2] = {&sb_sound_left[i], &sb_sound_right[i]};
        for (int c = 0; c < 2; c++) {
            sb_voice_t * v = voices[c];
            if (!v->active) continue;
            if (v->wave == 'N' || v->wave == 'P') return SB_BUFFER_FRAMES;
            unsigned period = tone_period_frames(v->hz);
            if (period == 0) return SB_BUFFER_FRAMES;
            frames = u_lcm_bounded(frames, period, SB_BUFFER_FRAMES);
            if (frames == 0) return SB_BUFFER_FRAMES;
        }
    }
    return frames < 2 ? 2 : frames;
}

static uint32_t noise_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static int noise_value(uint32_t bucket, unsigned seed) {
    uint32_t x = noise_hash(bucket + seed * 0x9e3779b9u);
    return (int)((x >> 24) & 0xFF) - 128;
}

static int wave_sample(const sb_voice_t * v, unsigned frame, unsigned channel_seed) {
    if (!v->active || v->volume == 0) return 0;
    if (v->wave == 'N') {
        unsigned dwell = v->hz > 1.0 ? (unsigned)(v->hz + 0.5) : 1u;
        int raw = noise_value(frame / dwell, channel_seed);
        return raw * (int)v->volume / 25;
    }
    if (v->wave == 'P') {
        double pos = (double)frame * v->hz / (double)SB_RATE_HZ;
        double frac = pos - (double)(unsigned)pos;
        int raw = noise_value((uint32_t)(frac * 4096.0), channel_seed);
        return raw * (int)v->volume / 25;
    }

    double pos = (double)frame * v->hz / (double)SB_RATE_HZ;
    unsigned whole = (unsigned)pos;
    double frac = pos - (double)whole;
    int raw;
    switch (v->wave) {
    case 'Q':
        raw = frac < 0.5 ? 96 : -96;
        break;
    case 'W':
        raw = (int)(frac * 192.0) - 96;
        break;
    case 'T':
        raw = frac < 0.5 ? (int)(frac * 384.0) - 96 : 288 - (int)(frac * 384.0);
        break;
    case 'S':
    default: {
        double tri = frac < 0.5 ? frac * 4.0 - 1.0 : 3.0 - frac * 4.0;
        double smooth = tri * (1.5 - 0.5 * tri * tri);
        raw = (int)(smooth * 96.0);
        break;
    }
    }
    return raw * (int)v->volume / 25;
}

static uint8_t mix_channel(unsigned frame, sb_voice_t voices[4], unsigned seed) {
    int mix = 128;
    for (int i = 0; i < 4; i++) mix += wave_sample(&voices[i], frame, seed + (unsigned)i);
    if (mix < 0) mix = 0;
    if (mix > 255) mix = 255;
    return (uint8_t)mix;
}

static unsigned sb_fill_sound(void) {
    unsigned frames = sb_sound_frames();
    unsigned seed_base = ++sb_noise_nonce;
    for (unsigned frame = 0; frame < frames; frame++) {
        sb_dma_buffer[frame * 2u + 0u] = mix_channel(frame, sb_sound_left, seed_base + 1u);
        sb_dma_buffer[frame * 2u + 1u] = mix_channel(frame, sb_sound_right, seed_base + 17u);
    }
    return frames;
}

static void sb_start_stereo_dma(unsigned bytes) {
    sb_program_dma((uintptr_t)sb_dma_buffer, bytes);

    (void)dsp_write_byte(0x41); /* output sample rate */
    (void)dsp_write_byte((uint8_t)(SB_RATE_HZ >> 8));
    (void)dsp_write_byte((uint8_t)(SB_RATE_HZ & 0xFF));

    sb_speaker_on();
    (void)dsp_write_byte(0xC6); /* 8-bit auto-init DMA output */
    (void)dsp_write_byte(0x20); /* unsigned stereo */
    unsigned last = bytes - 1u;
    (void)dsp_write_byte((uint8_t)(last & 0xFF));
    (void)dsp_write_byte((uint8_t)((last >> 8) & 0xFF));

    sb_playing = 1;
    sb_paused = 0;
}

void hal_audio_init(void) {
    sb_config_from_options();
    (void)sb_probe();
    sb_playing = 0;
    sb_paused = 0;
}

void hal_audio_tone(double left_hz, double right_hz, int has_duration, int64_t duration_ms) {
    sb_config_from_options();
    if (!sb_probe()) error("Sound Blaster 16 not detected");

    sb_stop_dma();
    sb_clear_sounds();
    if (left_hz <= 0.0 && right_hz <= 0.0) return;

    unsigned frames = sb_fill_tone(left_hz, right_hz);
    sb_start_stereo_dma(frames * 2u);

    if (has_duration) {
        uint64_t remaining_us = (uint64_t)duration_ms * 1000ull;
        while (remaining_us > 0) {
            uint32_t chunk = remaining_us > 1000000ull ? 1000000u : (uint32_t)remaining_us;
            hal_time_sleep_us(chunk);
            remaining_us -= chunk;
        }
        sb_stop_dma();
    }
}

void hal_audio_sound(int slot, const char * ch, const char * type, double freq_hz, int volume) {
    sb_config_from_options();
    if (!sb_probe()) error("Sound Blaster 16 not detected");

    int idx = slot - 1;
    if (idx < 0 || idx >= 4) error("Sound number");
    char wave = type && type[0] ? (char)mytoupper(type[0]) : 'O';
    int set_left = ch && (mytoupper(ch[0]) == 'L' || mytoupper(ch[0]) == 'B');
    int set_right = ch && (mytoupper(ch[0]) == 'R' || mytoupper(ch[0]) == 'B');
    if (!set_left && !set_right) error("Position must be L, R, or B");
    if (volume < 0) volume = 0;
    if (volume > 25) volume = 25;

    sb_voice_t v = {
        .active = wave != 'O',
        .wave = wave,
        .hz = freq_hz,
        .volume = (uint8_t)volume,
    };
    if (set_left) sb_sound_left[idx] = v;
    if (set_right) sb_sound_right[idx] = v;

    sb_stop_dma();
    if (!voice_active()) return;
    unsigned frames = sb_fill_sound();
    sb_start_stereo_dma(frames * 2u);
}

void hal_audio_stop(void) {
    sb_stop_dma();
    sb_clear_sounds();
}

void hal_audio_volume(int left_pct, int right_pct) {
    if (!sb_probe()) return;
    if (left_pct < 0) left_pct = 0;
    if (left_pct > 100) left_pct = 100;
    if (right_pct < 0) right_pct = 0;
    if (right_pct > 100) right_pct = 100;
    uint8_t l = (uint8_t)(left_pct * 15 / 100);
    uint8_t r = (uint8_t)(right_pct * 15 / 100);
    outb(SB_MIXER_ADDR, 0x22);
    outb(SB_MIXER_DATA, (uint8_t)((l << 4) | r));
}

void hal_audio_pause(void) {
    if (!sb_probe() || !sb_playing || sb_paused) return;
    (void)dsp_write_byte(0xD0);
    sb_paused = 1;
}

void hal_audio_resume(void) {
    if (!sb_probe() || !sb_playing || !sb_paused) return;
    (void)dsp_write_byte(0xD4);
    sb_paused = 0;
}

void cmd_sb16(void) {
    sb_config_from_options();
    if (cmdline == NULL || *cmdline == 0) {
        if (!sb_probe_attempted) (void)sb_probe();
        MMPrintString("SB16 &H");
        PIntH(sb_base);
        MMPrintString(", IRQ ");
        PInt(sb_irq);
        MMPrintString(", DMA ");
        PInt(sb_dma);
        MMPrintString(", DMA16 ");
        PInt(sb_dma16);
        MMPrintString(sb_available ? " detected\r\n" : " not detected\r\n");
        return;
    }

    unsigned char * p = cmdline;
    getargs(&p, 7, (unsigned char *)",");
    if (!(argc == 1 || argc == 3 || argc == 5 || argc == 7)) error("Argument count");

    int base = getint(argv[0], 0x200, 0x280);
    int irq = sb_irq;
    int dma = sb_dma;
    int dma16 = sb_dma16;
    if (argc >= 3) irq = getint(argv[2], 2, 15);
    if (argc >= 5) dma = getint(argv[4], 0, 3);
    if (argc >= 7) dma16 = getint(argv[6], 5, 7);
    if (dma == 2) error("DMA channel 2 is reserved for cascade");

    sb_stop_dma();
    sb_base = (uint16_t)base;
    sb_irq = (uint8_t)irq;
    sb_dma = (uint8_t)dma;
    sb_dma16 = (uint8_t)dma16;
    Option.pc386_sb_base = sb_base;
    Option.pc386_sb_irq = sb_irq;
    Option.pc386_sb_dma = sb_dma;
    Option.pc386_sb_dma16 = sb_dma16;
    SaveOptions();
    sb_available = 0;
    sb_probe_attempted = 0;
    if (!sb_probe()) error("Sound Blaster 16 not detected");
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
