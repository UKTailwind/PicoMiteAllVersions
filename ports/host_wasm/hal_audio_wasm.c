#include <stdlib.h>

#include "host_sim_audio.h"
#include "hal/hal_audio_control.h"
#include "hal/hal_audio_stream.h"

#ifdef MMBASIC_WASM
#include <emscripten.h>
#endif

#define WASM_AUDIO_STREAM_CAPACITY_FRAMES 32768

static int stream_active;
static int stream_rate_hz;
static double stream_queued_frames;
static double stream_last_ms;

static double wasm_audio_now_ms(void) {
#ifdef MMBASIC_WASM
    return emscripten_get_now();
#else
    return 0.0;
#endif
}

static void stream_update(void) {
    if (!stream_active || stream_rate_hz <= 0) return;
    double now = wasm_audio_now_ms();
    if (stream_last_ms <= 0.0) {
        stream_last_ms = now;
        return;
    }
    double elapsed = now - stream_last_ms;
    if (elapsed > 0.0) {
        stream_queued_frames -= elapsed * (double)stream_rate_hz / 1000.0;
        if (stream_queued_frames < 0.0) stream_queued_frames = 0.0;
        stream_last_ms = now;
    }
}

void hal_audio_init(void) {
}

void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, int64_t duration_ms) {
    host_sim_audio_tone(left_hz, right_hz, has_duration, duration_ms);
}

void hal_audio_sound(int slot, const char * ch, const char * type,
                     double freq_hz, int volume) {
    host_sim_audio_sound(slot, ch, type, freq_hz, volume);
}

void hal_audio_stop(void) {
    host_sim_audio_stop();
}

void hal_audio_volume(int left_pct, int right_pct) {
    host_sim_audio_volume(left_pct, right_pct);
}

void hal_audio_pause(void) {
    host_sim_audio_pause();
}

void hal_audio_resume(void) {
    host_sim_audio_resume();
}

int hal_audio_sample_begin(int sample_rate_hz) {
    if (sample_rate_hz <= 0) return -1;
    stream_active = 1;
    stream_rate_hz = sample_rate_hz;
    stream_queued_frames = 0.0;
    stream_last_ms = wasm_audio_now_ms();
#ifdef MMBASIC_WASM
    // clang-format off
    MAIN_THREAD_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.streamBegin($0);
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'streamBegin', args: [$0] });
        }
    }, sample_rate_hz);
    // clang-format on
#endif
    return 0;
}

void hal_audio_sample_end(void) {
    stream_active = 0;
    stream_rate_hz = 0;
    stream_queued_frames = 0.0;
    stream_last_ms = 0.0;
#ifdef MMBASIC_WASM
    // clang-format off
    MAIN_THREAD_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.streamEnd();
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'streamEnd', args: [] });
        }
    });
    // clang-format on
#endif
}

void hal_audio_sample_eof(void) {
#ifdef MMBASIC_WASM
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.streamEof();
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'streamEof', args: [] });
        }
    });
    // clang-format on
#endif
}

int hal_audio_sample_space(void) {
    stream_update();
    if (!stream_active) return 0;
    int queued = (int)(stream_queued_frames + 0.5);
    int space = WASM_AUDIO_STREAM_CAPACITY_FRAMES - queued;
    return space > 0 ? space : 0;
}

int hal_audio_sample_queued(void) {
    stream_update();
    if (!stream_active) return 0;
    return (int)(stream_queued_frames + 0.5);
}

int hal_audio_sample_push(const int16_t * frames, int frame_count) {
    if (!stream_active || !frames || frame_count <= 0) return 0;
    int space = hal_audio_sample_space();
    if (space <= 0) return 0;
    int accepted = frame_count < space ? frame_count : space;
    stream_queued_frames += accepted;
#ifdef MMBASIC_WASM
    // clang-format off
    MAIN_THREAD_EM_ASM({
        const ptr = $0;
        const frames = $1;
        const rate = $2;
        const byteLen = frames * 4;
        const data = HEAPU8.slice(ptr, ptr + byteLen);
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.streamSamples(rate, data);
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'streamSamples', args: [rate, data.buffer] }, [data.buffer]);
        }
    }, frames, accepted, stream_rate_hz);
    // clang-format on
#endif
    return accepted;
}

int hal_audio_sample_acquire(int16_t ** frames, int * frame_capacity) {
    (void)frames;
    (void)frame_capacity;
    return 0;
}

void hal_audio_sample_commit(int frame_count) {
    (void)frame_count;
}

void * hal_audio_workmem_alloc(unsigned long bytes) {
    return malloc((size_t)bytes);
}

void * hal_audio_workmem_realloc(void * p, unsigned long bytes) {
    return realloc(p, (size_t)bytes);
}

void hal_audio_workmem_free(void * p) {
    free(p);
}
