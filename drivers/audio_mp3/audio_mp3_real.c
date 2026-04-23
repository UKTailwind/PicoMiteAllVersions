/*
 * drivers/audio_mp3/audio_mp3_real.c — dr_mp3 implementation bodies.
 *
 * Pulls in dr_mp3.h's amalgamated implementation (drmp3_init, drmp3_uninit,
 * drmp3_read_pcm_frames_s16, etc.). Linked only on ports with the RAM and
 * CPU budget to decode MP3 in software — RP2350 variants. RP2040 and host
 * link audio_mp3_stub.c instead, which provides no-op bodies for the same
 * symbols so Audio.c can include dr_mp3.h unconditionally.
 */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_ONLY_MP3
#define DR_MP3_NO_SIMD
#define DRMP3_DATA_CHUNK_SIZE 32768
#include "dr_mp3.h"
