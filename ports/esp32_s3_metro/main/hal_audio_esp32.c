/*
 * hal_audio_esp32.c — ESP32 I2S/PDM backend for hal/hal_audio.h.
 *
 * PLAY TONE and PLAY SOUND are synthesized by the shared software
 * synthesizer (shared/audio/synth_pcm.c) and streamed as 16-bit stereo
 * PCM to an external I2S DAC (MAX98357A / PCM5102 / UDA1334 / ...) or
 * to the ESP32-S3 I2S PDM TX DAC-style two-line output. A dedicated
 * FreeRTOS task pulls frames from the synth; both sinks are DMA-paced I2S.
 *
 * The synth state this writes (PhaseM/PhaseAC/mono/vol for TONE, the
 * sound_* arrays for SOUND) is the same state the RP2 audio ISR drives,
 * so the two backends produce identical samples. File playback
 * (WAV/FLAC/MP3/MOD) is decoded by shared/audio/audio_stream.c and drained
 * from the streamed-sample ring below.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_audio.h"
#include "synth_pcm.h"
#include "audio_play_common.h"

#define AUDIO_RATE        HAL_PORT_AUDIO_SAMPLE_RATE
#define FRAMES_PER_CHUNK  256                       /* stereo frames per write */

typedef enum {
    AUDIO_BACKEND_NONE = 0,
    AUDIO_BACKEND_I2S,
    AUDIO_BACKEND_PDM
} audio_backend_t;

/* TONE/playback state owned by shared/audio/Audio.c (extern here). */
extern volatile e_CurrentlyPlaying CurrentlyPlaying;

static const char *TAG = "mmaudio";

static i2s_chan_handle_t s_tx;
static TaskHandle_t      s_task;
static volatile uint64_t s_tone_frames;   /* remaining TONE frames; UINT64_MAX = forever */
static volatile bool     s_paused;
static volatile bool     s_ready;
static audio_backend_t   s_backend;

/* Audio-DMA channel globals referenced by core SOUND/ADC bookkeeping.
 * The shared synth path does not use them, but the symbols must exist. */
uint32_t dma_tx_chan = 0, dma_rx_chan = 0, dma_tx_chan2 = 0, dma_rx_chan2 = 0;
uint32_t ADC_dma_chan = 0, ADC_dma_chan2 = 0;
bool ADCDualBuffering = 0;
bool dmarunning = 0;

/* --- streamed-sample ring (PLAY WAV/FLAC/MP3/MOD/ARRAY) ---
 * Single-producer (decode pump, BASIC service loop) / single-consumer
 * (audio_task) ring of 16-bit stereo frames in PSRAM. Free-running 32-bit
 * head/tail counters; no lock needed for one producer + one consumer. */
#define SAMPLE_RING_FRAMES 32768u            /* 128 KB in PSRAM, ~0.74s @ 44.1k */
static int16_t          *s_ring;             /* SAMPLE_RING_FRAMES * 2 int16 */
static volatile uint32_t s_ring_head, s_ring_tail;
static volatile uint32_t s_stream_inflight_frames;
static volatile uint32_t s_stream_drain_frames;
static volatile uint32_t s_stream_tail_hold_until;
static volatile bool     s_stream_eof_seen;
static int               s_stream_rate;

static int is_file_mode(int m) {
    return m == P_WAV || m == P_FLAC || m == P_MP3 ||
           m == P_MOD || m == P_ARRAY || m == P_STREAM;
}

static volatile int s_pending_rate;   /* requested by sample_begin/end */

static int default_audio_rate(void) {
    return AUDIO_RATE;
}

static int active_audio_rate(void) {
    return s_stream_rate > 0 ? s_stream_rate : default_audio_rate();
}

static bool stream_tail_hold_active(void) {
    uint32_t until = s_stream_tail_hold_until;
    if (!until) return false;
    return (int32_t)(until - (uint32_t)xTaskGetTickCount()) > 0;
}

static uint32_t stream_tail_hold_ticks(void) {
    int rate = active_audio_rate();
    int ms = (int)(((FRAMES_PER_CHUNK * 3u) * 1000u) / (uint32_t)(rate > 0 ? rate : AUDIO_RATE));
    if (ms < 20) ms = 20;
    return (uint32_t)pdMS_TO_TICKS(ms);
}

static int16_t pcm16_from_synth_frame(int32_t frame) {
    int32_t sample = frame >> 16;
    if (sample > INT16_MAX) return INT16_MAX;
    if (sample < INT16_MIN) return INT16_MIN;
    return (int16_t)sample;
}

static int option_gpio(int pin, int fallback_gpio) {
    if (pin > 0 && pin <= NBRPINS && !(PinDef[pin].mode & UNUSED))
        return PinDef[pin].GPno;
    return fallback_gpio;
}

static int audio_pin_invalid(int pin) {
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

static audio_backend_t select_backend(void) {
    if (Option.AUDIO_L && Option.AUDIO_R) return AUDIO_BACKEND_PDM;
    if (Option.audio_i2s_bclk && Option.audio_i2s_data) return AUDIO_BACKEND_I2S;
    return AUDIO_BACKEND_NONE;
}

void esp32_audio_reserve_option_pins(void) {
    if (Option.AUDIO_L && Option.AUDIO_L <= NBRPINS)
        ExtCurrentConfig[Option.AUDIO_L] = EXT_BOOT_RESERVED;
    if (Option.AUDIO_R && Option.AUDIO_R <= NBRPINS)
        ExtCurrentConfig[Option.AUDIO_R] = EXT_BOOT_RESERVED;
    if (Option.audio_i2s_bclk && Option.audio_i2s_bclk <= NBRPINS) {
        int ws_gpio = PinDef[Option.audio_i2s_bclk].GPno + 1;
        ExtCurrentConfig[Option.audio_i2s_bclk] = EXT_BOOT_RESERVED;
        if (ws_gpio >= 0 && ws_gpio < HAL_PORT_GPIO_COUNT) {
            int ws_pin = codemap(ws_gpio);
            if (!audio_pin_invalid(ws_pin)) ExtCurrentConfig[ws_pin] = EXT_BOOT_RESERVED;
        }
    }
    if (Option.audio_i2s_data && Option.audio_i2s_data <= NBRPINS)
        ExtCurrentConfig[Option.audio_i2s_data] = EXT_BOOT_RESERVED;
}

/* Record a desired rate; the audio task applies it (see apply_pending_rate).
 * Reconfiguring from the BASIC thread would call i2s_channel_disable while
 * the task is blocked in i2s_channel_write(portMAX_DELAY) — a deadlock. */
static void stream_set_rate(int rate) {
    if (rate > 0) s_pending_rate = rate;
}

/* Apply a pending rate change. Runs only on the audio task, between writes,
 * so the channel is never disabled while a write is in flight. */
static void apply_pending_rate(void) {
    int rate = s_pending_rate;
    if (!s_tx || rate <= 0 || rate == s_stream_rate) return;
    i2s_channel_disable(s_tx);
    if (s_backend == AUDIO_BACKEND_PDM) {
        i2s_pdm_tx_clk_config_t clk = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG((uint32_t)rate);
        i2s_channel_reconfig_pdm_tx_clock(s_tx, &clk);
    } else {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)rate);
        i2s_channel_reconfig_std_clock(s_tx, &clk);
    }
    i2s_channel_enable(s_tx);
    s_stream_rate = rate;
}

int hal_audio_sample_begin(int sample_rate_hz) {
    hal_audio_init();                        /* ensure I2S + task are up */
    if (s_backend == AUDIO_BACKEND_NONE) return -1;
    if (!s_ring) {
        size_t bytes = (size_t)SAMPLE_RING_FRAMES * 2u * sizeof(int16_t);
        s_ring = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ring) s_ring = malloc(bytes);
        if (!s_ring) return -1;
    }
    s_ring_head = s_ring_tail = 0;
    s_stream_inflight_frames = 0;
    s_stream_drain_frames = 0;
    s_stream_tail_hold_until = 0;
    s_stream_eof_seen = false;
    stream_set_rate(sample_rate_hz);
    return 0;
}

void hal_audio_sample_end(void) {
    s_ring_head = s_ring_tail = 0;
    s_stream_inflight_frames = 0;
    s_stream_drain_frames = 0;
    s_stream_tail_hold_until = 0;
    s_stream_eof_seen = false;
    stream_set_rate(default_audio_rate());   /* restore the synth rate */
}

void hal_audio_sample_eof(void) {
    if (!s_stream_eof_seen) {
        s_stream_eof_seen = true;
        s_stream_drain_frames = FRAMES_PER_CHUNK;
    }
}

int hal_audio_sample_space(void) {
    if (!s_ring) return 0;
    return (int)(SAMPLE_RING_FRAMES - (s_ring_head - s_ring_tail));
}

int hal_audio_sample_queued(void) {
    if (!s_ring) return 0;
    return (int)((s_ring_head - s_ring_tail) +
                 s_stream_inflight_frames +
                 s_stream_drain_frames +
                 (stream_tail_hold_active() ? 1u : 0u));
}

/* Decoder + MOD-file working memory from PSRAM (the 36 KB internal heap
 * is too small for MP3/FLAC/MOD); falls back to internal heap if PSRAM is
 * unavailable. */
void *hal_audio_workmem_alloc(unsigned long bytes) {
    void *p = heap_caps_malloc((size_t)bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc((size_t)bytes);
}
void *hal_audio_workmem_realloc(void *p, unsigned long bytes) {
    void *n = heap_caps_realloc(p, (size_t)bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return n ? n : realloc(p, (size_t)bytes);
}
void hal_audio_workmem_free(void *p) { heap_caps_free(p); }

int hal_audio_sample_push(const int16_t *frames, int frame_count) {
    if (!s_ring || frame_count <= 0) return 0;
    int space = (int)(SAMPLE_RING_FRAMES - (s_ring_head - s_ring_tail));
    if (frame_count > space) frame_count = space;
    for (int i = 0; i < frame_count; i++) {
        uint32_t idx = (s_ring_head & (SAMPLE_RING_FRAMES - 1u)) * 2u;
        s_ring[idx]     = frames[2 * i];
        s_ring[idx + 1] = frames[2 * i + 1];
        s_ring_head++;
    }
    return frame_count;
}

int hal_audio_sample_acquire(int16_t **frames, int *frame_capacity) {
    (void)frames;
    (void)frame_capacity;
    return 0;
}

void hal_audio_sample_commit(int frame_count) {
    (void)frame_count;
}

/* Map a PLAY SOUND waveform letter to its synth wavetable. */
static const unsigned short *audio_table_for(char type) {
    switch (type) {
        case 'O': case 'o': return nulltable;
        case 'Q': case 'q': return squaretable;
        case 'T': case 't': return triangletable;
        case 'W': case 'w': return sawtable;
        case 'S': case 's': return SineTable;
        case 'P': case 'p': setnoise(); return noisetable;
        case 'N': case 'n': return whitenoise;
        case 'U': case 'u': return usertable;
        default:            return NULL;
    }
}

static void render_frame(int mode, int16_t *left, int16_t *right) {
    if (mode == P_TONE) {
        int32_t l, r;
        if (s_tone_frames == 0) {
            *left = *right = 0;
            return;
        }
        synth_pcm_tone_frame(&l, &r);
        if (s_tone_frames != UINT64_MAX) s_tone_frames--;
        *left = pcm16_from_synth_frame(l);
        *right = pcm16_from_synth_frame(r);
        return;
    }
    if (mode == P_SOUND) {
        int32_t l, r;
        synth_pcm_sound_frame(&l, &r);
        *left = pcm16_from_synth_frame(l);
        *right = pcm16_from_synth_frame(r);
        return;
    }
    if (is_file_mode(mode) && s_ring && (s_ring_head - s_ring_tail) > 0) {
        uint32_t idx = (s_ring_tail & (SAMPLE_RING_FRAMES - 1u)) * 2u;
        *left = s_ring[idx];
        *right = s_ring[idx + 1];
        s_ring_tail++;
        return;
    }
    *left = *right = 0;
}

static void audio_task(void *arg) {
    (void)arg;
    int16_t buf[FRAMES_PER_CHUNK * 2];
    for (;;) {
        apply_pending_rate();          /* safe rate switch between writes */
        int mode = s_paused ? P_NOTHING : (int)CurrentlyPlaying;
        if (s_backend != AUDIO_BACKEND_I2S && s_backend != AUDIO_BACKEND_PDM) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (is_file_mode(mode)) s_stream_inflight_frames = 0;
        for (int n = 0; n < FRAMES_PER_CHUNK; n++) {
            if (is_file_mode(mode) && s_ring && (s_ring_head - s_ring_tail) > 0) {
                s_stream_inflight_frames++;
            } else if (is_file_mode(mode) && s_stream_drain_frames > 0) {
                s_stream_inflight_frames++;
                s_stream_drain_frames--;
            }
            render_frame(mode, &buf[2 * n], &buf[2 * n + 1]);
        }
        size_t wr;
        i2s_channel_write(s_tx, buf, sizeof(buf), &wr, portMAX_DELAY);
        if (is_file_mode(mode) && s_stream_eof_seen &&
            s_stream_drain_frames == 0 && s_ring &&
            (s_ring_head - s_ring_tail) == 0 &&
            s_stream_tail_hold_until == 0) {
            s_stream_tail_hold_until = (uint32_t)xTaskGetTickCount() + stream_tail_hold_ticks();
        }
        if (is_file_mode(mode)) s_stream_inflight_frames = 0;
        if (mode == P_TONE && s_tone_frames == 0) CurrentlyPlaying = P_NOTHING;
    }
}

void hal_audio_init(void) {
    int i;
    if (s_ready) return;
    s_backend = select_backend();
    int default_rate = default_audio_rate();
    if (s_backend == AUDIO_BACKEND_NONE) {
        ESP_LOGW(TAG, "audio not configured; use OPTION AUDIO I2S or OPTION AUDIO");
    }

    /* All SOUND slots start silent; the synth dereferences the table
     * pointer per frame, so every slot must point at nulltable. */
    for (i = 0; i < MAXSOUNDS; i++) {
        sound_mode_left[i]  = (unsigned short *)nulltable;
        sound_mode_right[i] = (unsigned short *)nulltable;
        sound_PhaseAC_left[i] = sound_PhaseAC_right[i] = 0.0f;
        sound_PhaseM_left[i]  = sound_PhaseM_right[i]  = 0.0f;
    }

    if (s_backend == AUDIO_BACKEND_I2S) {
        int bclk_gpio = option_gpio(Option.audio_i2s_bclk, HAL_PORT_AUDIO_I2S_BCLK_PIN);
        int data_gpio = option_gpio(Option.audio_i2s_data, HAL_PORT_AUDIO_I2S_DOUT_PIN);
        int ws_gpio = bclk_gpio + 1;
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&chan_cfg, &s_tx, NULL) != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel failed");
            return;
        }
        i2s_std_config_t std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(default_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                            I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = bclk_gpio,
                .ws   = ws_gpio,
                .dout = data_gpio,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
            },
        };
        if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
            return;
        }
        i2s_channel_enable(s_tx);
    } else if (s_backend == AUDIO_BACKEND_PDM) {
        int left_gpio = PinDef[Option.AUDIO_L].GPno;
        int right_gpio = PinDef[Option.AUDIO_R].GPno;
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&chan_cfg, &s_tx, NULL) != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel failed");
            return;
        }
        i2s_pdm_tx_config_t pdm_cfg = {
            .clk_cfg  = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(default_rate),
            .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                           I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .clk = I2S_GPIO_UNUSED,
                .dout = left_gpio,
#if SOC_I2S_PDM_MAX_TX_LINES > 1
                .dout2 = right_gpio,
#endif
                .invert_flags = { .clk_inv = false },
            },
        };
        if (i2s_channel_init_pdm_tx_mode(s_tx, &pdm_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_pdm_tx_mode failed");
            return;
        }
        i2s_channel_enable(s_tx);
    }
    s_stream_rate = default_rate;       /* channel starts at the synth rate */
    s_pending_rate = default_rate;

    if (xTaskCreate(audio_task, "mmaudio", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        return;
    }
    s_ready = true;
}

void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, int64_t duration_ms) {
    hal_audio_init();
    int rate = active_audio_rate();
    mono = (left_hz == right_hz && vol_left == vol_right) ? 1 : 0;
    PhaseM_left  = (float)(left_hz  / (double)rate * 4096.0);
    PhaseM_right = (float)(right_hz / (double)rate * 4096.0);
    if (CurrentlyPlaying != P_TONE) {          /* fresh start: reset phase */
        PhaseAC_left = 0.0f;
        PhaseAC_right = 0.0f;
    }
    s_tone_frames = has_duration
        ? (uint64_t)((double)duration_ms / 1000.0 * (double)rate)
        : UINT64_MAX;
}

void hal_audio_sound(int slot, const char *ch, const char *type,
                     double freq_hz, int volume) {
    int channel = slot - 1;
    if (channel < 0 || channel >= MAXSOUNDS) return;
    const unsigned short *tbl = audio_table_for(type[0]);
    if (!tbl) return;

    hal_audio_init();
    int rate = active_audio_rate();

    int left  = (ch[0] == 'L' || ch[0] == 'l' || ch[0] == 'B' || ch[0] == 'b' ||
                 ch[0] == 'M' || ch[0] == 'm');
    int right = (ch[0] == 'R' || ch[0] == 'r' || ch[0] == 'B' || ch[0] == 'b' ||
                 ch[0] == 'M' || ch[0] == 'm');

    /* white-noise stores the dwell count in PhaseM; tonal waveforms
     * store the phase increment. Volume 0..25 scales to the 0..41 range
     * the synth's mapping[] lookup expects (matches the RP2 path). */
    float pm = (tbl == whitenoise) ? (float)freq_hz
                                   : (float)(freq_hz / (double)rate * 4096.0);
    int v = audio_play_volume_to_synth(volume);

    if (left) {
        if (sound_mode_left[channel] != (unsigned short *)tbl)
            sound_PhaseAC_left[channel] = 0.0f;
        sound_PhaseM_left[channel] = pm;
        sound_v_left[channel] = v;
        sound_mode_left[channel] = (unsigned short *)tbl;
    }
    if (right) {
        if (sound_mode_right[channel] != (unsigned short *)tbl)
            sound_PhaseAC_right[channel] = 0.0f;
        sound_PhaseM_right[channel] = pm;
        sound_v_right[channel] = v;
        sound_mode_right[channel] = (unsigned short *)tbl;
    }
}

void hal_audio_stop(void) {
    int i;
    s_tone_frames = 0;
    for (i = 0; i < MAXSOUNDS; i++) {
        sound_mode_left[i]  = (unsigned short *)nulltable;
        sound_mode_right[i] = (unsigned short *)nulltable;
    }
}

void hal_audio_volume(int left_pct, int right_pct) {
    vol_left = left_pct;
    vol_right = right_pct;
    if (CurrentlyPlaying == P_TONE && vol_left != vol_right) mono = 0;
}

void hal_audio_pause(void)  { s_paused = true; }
void hal_audio_resume(void) { s_paused = false; }
