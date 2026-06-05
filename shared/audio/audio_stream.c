/*
 * shared/audio/audio_stream.c — portable file-decode / streaming engine.
 *
 * One decode path for WAV / FLAC / MP3 / MOD, shared by every backend.
 * Decoders read through the MMBasic file HAL (hal_fds / hal_fs_read) and
 * decoded 16-bit interleaved-stereo PCM is fed to the backend through
 * hal_audio_sample_push(). Decoder state, decode buffers, and the MOD
 * file buffer all come from hal_audio_workmem_alloc (PSRAM on memory-tight
 * targets) so nothing large lands in BSS.
 *
 * dr_wav and dr_flac are compiled here; dr_mp3's implementation is linked
 * separately (drivers/audio_mp3/audio_mp3_real.c) and hxcmod from
 * third_party/hxcmod.
 */

#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_audio_stream.h"
#include "audio_stream.h"

#define DRWAV_COPY_MEMORY(dst, src, sz) memcpy((dst), (src), (sz))
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_SIMD
#include "dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_CRC
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

#define DR_MP3_NO_STDIO /* implementation lives in audio_mp3_real.c */
#include "dr_mp3.h"

#include "hxcmod.h"

/* File HAL (core/mmbasic/FileIO.c) */
extern hal_fs_fd_t hal_fds[];
extern int FindFreeFileNbr(void);
extern int BasicFileOpen(char * fname, int fnbr, int mode);
extern int ForceFileClose(int fnbr);

#define DECODE_FRAMES 1024
#define MOD_RENDER_RATE 22050
#define MOD_OUTPUT_RATE (MOD_RENDER_RATE * 2)

static int s_fnbr;
static int s_active = P_NOTHING; /* P_NOTHING / P_WAV / P_MP3 / P_FLAC / P_MOD */
static int s_channels;

/* All large state lives in work memory (PSRAM on the ESP32), pointed to
 * from here so BSS stays tiny. */
static int16_t * s_stereo; /* DECODE_FRAMES * 2 */
static int16_t * s_mono;   /* DECODE_FRAMES */
static drwav * s_wav;
static drmp3 * s_mp3;
static drflac * s_flac;
static modcontext * s_modctx;
static void * s_modbuf;
static int s_pending_offset;
static int s_pending_frames;
static int s_eof_pending;
static int s_mod_noloop;

/* Decoder allocations route to PSRAM-capable working memory. */
static void * dec_malloc(size_t sz, void * ud) {
    (void)ud;
    return hal_audio_workmem_alloc(sz);
}
static void * dec_realloc(void * p, size_t sz, void * ud) {
    (void)ud;
    return hal_audio_workmem_realloc(p, sz);
}
static void dec_free(void * p, void * ud) {
    (void)ud;
    if (p) hal_audio_workmem_free(p);
}

static void stream_close_file(void) {
    if (s_fnbr > 0) ForceFileClose(s_fnbr);
    if (WAV_fnbr == s_fnbr) WAV_fnbr = 0;
    s_fnbr = 0;
}

static size_t stream_read(void * ud, void * buf, size_t n) {
    (void)ud;
    ssize_t r = hal_fs_read(hal_fds[s_fnbr], buf, n);
    if (r < 0) {
        FSerror = FR_DISK_ERR;
        return 0;
    }
    FSerror = 0;
    return (size_t)r;
}

static drwav_bool32 stream_seek(void * ud, int offset, drwav_seek_origin origin) {
    (void)ud;
    int whence = (origin == drwav_seek_origin_start) ? HAL_FS_SEEK_SET : HAL_FS_SEEK_CUR;
    return hal_fs_seek(hal_fds[s_fnbr], offset, whence) < 0 ? 0 : 1;
}

/* Decode scratch buffers, allocated per stream. */
static int ensure_buffers(void) {
    if (!s_stereo) s_stereo = hal_audio_workmem_alloc(DECODE_FRAMES * 2 * sizeof(int16_t));
    if (!s_mono) s_mono = hal_audio_workmem_alloc(DECODE_FRAMES * sizeof(int16_t));
    return (s_stereo && s_mono) ? 0 : -1;
}

static void release_buffers(void) {
    if (s_stereo) {
        hal_audio_workmem_free(s_stereo);
        s_stereo = NULL;
    }
    if (s_mono) {
        hal_audio_workmem_free(s_mono);
        s_mono = NULL;
    }
}

int audio_stream_active(void) {
    return s_active != P_NOTHING;
}

static int audio_stream_paused(void) {
    switch (s_active) {
    case P_WAV:
        return CurrentlyPlaying == P_PAUSE_WAV;
    case P_FLAC:
        return CurrentlyPlaying == P_PAUSE_FLAC;
    case P_MP3:
        return CurrentlyPlaying == P_PAUSE_MP3;
    case P_MOD:
        return CurrentlyPlaying == P_PAUSE_MOD;
    default:
        return 0;
    }
}

void audio_stream_stop(void) {
    int active = s_active;
    s_pending_offset = 0;
    s_pending_frames = 0;
    s_eof_pending = 0;
    s_mod_noloop = 0;
    switch (active) {
    case P_WAV:
        drwav_uninit(s_wav);
        hal_audio_workmem_free(s_wav);
        s_wav = NULL;
        break;
    case P_MP3:
        drmp3_uninit(s_mp3);
        hal_audio_workmem_free(s_mp3);
        s_mp3 = NULL;
        break;
    case P_FLAC:
        if (s_flac) {
            drflac_close(s_flac);
            s_flac = NULL;
        }
        break;
    case P_MOD:
        hal_audio_workmem_free(s_modctx);
        s_modctx = NULL;
        hal_audio_workmem_free(s_modbuf);
        s_modbuf = NULL;
        break;
    default:
        stream_close_file();
        release_buffers();
        return;
    }
    if (active != P_MOD) stream_close_file(); /* MOD closes its file at load */
    hal_audio_sample_end();
    release_buffers();
    s_active = P_NOTHING;
    WAV_fnbr = 0;
}

static void audio_stream_finish(void) {
    audio_stream_stop();
    CurrentlyPlaying = P_NOTHING;
    WAVcomplete = true;
}

static void audio_stream_mark_eof(void) {
    s_eof_pending = 1;
    if (s_pending_frames <= 0) hal_audio_sample_eof();
}

/* Common open for the dr_* stream decoders: append `ext`, open the file. */
static int stream_open_file(char * fname, const char * ext) {
    if (strchr(fname, '.') == NULL) strcat(fname, ext);
    audio_stream_stop();
    s_fnbr = FindFreeFileNbr();
    if (!BasicFileOpen(fname, s_fnbr, FA_READ)) {
        s_fnbr = 0;
        return -1;
    }
    WAV_fnbr = s_fnbr;
    return 0;
}

int audio_stream_play_wav(char * fname) {
    if (stream_open_file(fname, ".wav") != 0) return -1;
    s_wav = hal_audio_workmem_alloc(sizeof(drwav));
    drwav_allocation_callbacks ac = {NULL, dec_malloc, dec_realloc, dec_free};
    if (!s_wav || !drwav_init(s_wav, stream_read, stream_seek, NULL, &ac)) {
        hal_audio_workmem_free(s_wav);
        s_wav = NULL;
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_channels = (int)s_wav->channels;
    if (s_channels < 1 || s_channels > 2 || hal_audio_sample_begin((int)s_wav->sampleRate) != 0) {
        drwav_uninit(s_wav);
        hal_audio_workmem_free(s_wav);
        s_wav = NULL;
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_active = P_WAV;
    CurrentlyPlaying = P_WAV;
    return 0;
}

int audio_stream_play_mp3(char * fname) {
    if (stream_open_file(fname, ".mp3") != 0) return -1;
    s_mp3 = hal_audio_workmem_alloc(sizeof(drmp3));
    drmp3_allocation_callbacks ac = {NULL, dec_malloc, dec_realloc, dec_free};
    if (!s_mp3 || !drmp3_init(s_mp3, (drmp3_read_proc)stream_read,
                              (drmp3_seek_proc)stream_seek, NULL, &ac)) {
        hal_audio_workmem_free(s_mp3);
        s_mp3 = NULL;
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_channels = (int)s_mp3->channels;
    if (s_channels < 1 || s_channels > 2 || hal_audio_sample_begin((int)s_mp3->sampleRate) != 0) {
        drmp3_uninit(s_mp3);
        hal_audio_workmem_free(s_mp3);
        s_mp3 = NULL;
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_active = P_MP3;
    CurrentlyPlaying = P_MP3;
    return 0;
}

int audio_stream_play_flac(char * fname) {
    if (stream_open_file(fname, ".flac") != 0) return -1;
    drflac_allocation_callbacks ac = {NULL, dec_malloc, dec_realloc, dec_free};
    s_flac = drflac_open((drflac_read_proc)stream_read, (drflac_seek_proc)stream_seek, NULL, &ac);
    if (!s_flac) {
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_channels = (int)s_flac->channels;
    if (s_channels < 1 || s_channels > 2 || hal_audio_sample_begin((int)s_flac->sampleRate) != 0) {
        drflac_close(s_flac);
        s_flac = NULL;
        stream_close_file();
        release_buffers();
        return -1;
    }
    s_active = P_FLAC;
    CurrentlyPlaying = P_FLAC;
    return 0;
}

int audio_stream_play_mod(char * fname) {
    return audio_stream_play_mod_noloop(fname, 0);
}

int audio_stream_play_mod_noloop(char * fname, int noloop) {
    if (stream_open_file(fname, ".mod") != 0) return -1;
    if (ensure_buffers() != 0) goto fail;

    long size = (long)hal_fs_seek(hal_fds[s_fnbr], 0, HAL_FS_SEEK_END);
    hal_fs_seek(hal_fds[s_fnbr], 0, HAL_FS_SEEK_SET);
    if (size <= 0) {
        stream_close_file();
        return -1;
    }

    s_modbuf = hal_audio_workmem_alloc((unsigned long)size);
    s_modctx = hal_audio_workmem_alloc(sizeof(modcontext));
    if (!s_modbuf || !s_modctx) goto fail;

    long off = 0;
    while (off < size) {
        ssize_t r = hal_fs_read(hal_fds[s_fnbr], (char *)s_modbuf + off, (size_t)(size - off));
        if (r <= 0) break;
        off += r;
    }
    stream_close_file();
    if (off != size) goto fail;

    hxcmod_init(s_modctx);
    hxcmod_setcfg(s_modctx, MOD_RENDER_RATE, 1, 1);
    if (!hxcmod_load(s_modctx, s_modbuf, (int)size)) goto fail;
    if (hal_audio_sample_begin(MOD_OUTPUT_RATE) != 0) goto fail;

    s_channels = 2;
    s_mod_noloop = noloop ? 1 : 0;
    s_active = P_MOD;
    CurrentlyPlaying = P_MOD;
    return 0;

fail:
    stream_close_file();
    hal_audio_workmem_free(s_modbuf);
    s_modbuf = NULL;
    hal_audio_workmem_free(s_modctx);
    s_modctx = NULL;
    release_buffers();
    return -1;
}

/* Decode up to `want` stereo frames into s_stereo. Returns frames produced.
 * Mono sources are expanded to dual-mono. */
static int decode_chunk(int want) {
    int got;
    if (ensure_buffers() != 0) return 0;
    switch (s_active) {
    case P_WAV:
        if (s_channels >= 2) return (int)drwav_read_pcm_frames_s16(s_wav, want, s_stereo);
        got = (int)drwav_read_pcm_frames_s16(s_wav, want, s_mono);
        break;
    case P_MP3:
        if (s_channels >= 2) return (int)drmp3_read_pcm_frames_s16(s_mp3, want, s_stereo);
        got = (int)drmp3_read_pcm_frames_s16(s_mp3, want, s_mono);
        break;
    case P_FLAC:
        if (s_channels >= 2) return (int)drflac_read_pcm_frames_s16(s_flac, want, s_stereo);
        got = (int)drflac_read_pcm_frames_s16(s_flac, want, s_mono);
        break;
    case P_MOD:
        /* hxcmod renders at 22050 Hz; legacy device playback runs the
             * sink at 44100 Hz and repeats each stereo frame. PDM DAC mode is
             * much happier with the 44.1 kHz sink clock as well. */
        got = (want + 1) / 2;
        if (hxcmod_fillbuffer(s_modctx, (msample *)s_stereo,
                              (unsigned long)got, NULL, s_mod_noloop)) {
            s_eof_pending = 1;
        }
        for (int i = got - 1; i >= 0; i--) {
            int16_t left = s_stereo[2 * i];
            int16_t right = s_stereo[2 * i + 1];
            int out = 2 * i;
            s_stereo[2 * out] = left;
            s_stereo[2 * out + 1] = right;
            if (out + 1 < want) {
                s_stereo[2 * (out + 1)] = left;
                s_stereo[2 * (out + 1) + 1] = right;
            }
        }
        return want;
    default:
        return 0;
    }
    for (int i = 0; i < got; i++) {
        s_stereo[2 * i] = s_mono[i];
        s_stereo[2 * i + 1] = s_mono[i];
    }
    return got;
}

static int decode_direct(int want, int16_t * dst) {
    if (!dst || s_channels < 2) return 0;
    switch (s_active) {
    case P_WAV:
        return (int)drwav_read_pcm_frames_s16(s_wav, want, dst);
    case P_MP3:
        return (int)drmp3_read_pcm_frames_s16(s_mp3, want, dst);
    case P_FLAC:
        return (int)drflac_read_pcm_frames_s16(s_flac, want, dst);
    default:
        return 0;
    }
}

static int direct_decode_supported(void) {
    return s_channels >= 2 &&
           (s_active == P_WAV || s_active == P_MP3 || s_active == P_FLAC);
}

static int push_pending(void) {
    if (s_pending_frames <= 0) return 1;
    int pushed = hal_audio_sample_push(s_stereo + (2 * s_pending_offset), s_pending_frames);
    if (pushed <= 0) return 0;
    s_pending_offset += pushed;
    s_pending_frames -= pushed;
    if (s_pending_frames == 0) s_pending_offset = 0;
    return s_pending_frames == 0;
}

void audio_stream_service(void) {
    /* Pumped from several idle paths (PAUSE/CheckAbort, MMInkey, MMgetchar);
     * a non-re-entrant decoder must never be entered twice. */
    static int in_service;
    if (in_service || s_active == P_NOTHING || audio_stream_paused()) return;
    in_service = 1;

    if (!push_pending()) {
        in_service = 0;
        return;
    }
    if (s_eof_pending && s_pending_frames <= 0) hal_audio_sample_eof();
    if (s_eof_pending) {
        if (hal_audio_sample_queued() == 0) audio_stream_finish();
        in_service = 0;
        return;
    }

    int space = hal_audio_sample_space();
    while (space >= 256) {
        int got = 0;
        int want = 0;
        int16_t * direct = NULL;
        int direct_capacity = 0;

        int have_direct = direct_decode_supported() &&
                          hal_audio_sample_acquire(&direct, &direct_capacity);
        if (have_direct && direct_capacity >= 256) {
            want = direct_capacity;
            got = decode_direct(want, direct);
            hal_audio_sample_commit(got);
        } else {
            if (have_direct) hal_audio_sample_commit(0);
            want = space < DECODE_FRAMES ? space : DECODE_FRAMES;
            got = decode_chunk(want);
            if (got > 0) {
                int pushed = hal_audio_sample_push(s_stereo, got);
                if (pushed < got) {
                    s_pending_offset = pushed > 0 ? pushed : 0;
                    s_pending_frames = got - s_pending_offset;
                }
            }
        }
        if (s_eof_pending) {
            if (s_pending_frames <= 0) hal_audio_sample_eof();
            break;
        }
        if (got < want) { /* decoder exhausted */
            if (s_pending_frames > 0)
                audio_stream_mark_eof();
            else if (hal_audio_sample_queued() == 0)
                audio_stream_finish();
            else
                audio_stream_mark_eof();
            break;
        }
        if (s_pending_frames > 0) break;
        space = hal_audio_sample_space();
    }
    in_service = 0;
}
