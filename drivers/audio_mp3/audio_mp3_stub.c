/*
 * drivers/audio_mp3/audio_mp3_stub.c — no-op stubs for targets without MP3.
 *
 * RP2040 and host builds link this instead of audio_mp3_real.c. Audio.c
 * includes dr_mp3.h unconditionally to get types and declarations, but the
 * stub bodies here make link succeed on targets where dr_mp3's amalgamated
 * implementation (~100 KB of decoder code) doesn't fit the budget.
 *
 * Audio.c rejects PLAY MP3 at parse time on targets without hardware support
 * (see cmd_play: requires AUDIO_MISO_PIN i.e. VS1053 when !HAL_PORT_HAS_MP3).
 * So these stubs are unreachable during normal play flow. If they do get
 * called, drmp3_init returns DRMP3_FALSE (which Audio.c treats as an error),
 * and the decoder entry points return zero frames.
 */
#include <string.h>

#include "dr_mp3.h"

DRMP3_API drmp3_bool32 drmp3_init(drmp3 * pMP3,
                                  drmp3_read_proc onRead,
                                  drmp3_seek_proc onSeek,
                                  void * pUserData,
                                  const drmp3_allocation_callbacks * pAllocationCallbacks) {
    (void)onRead;
    (void)onSeek;
    (void)pUserData;
    (void)pAllocationCallbacks;
    if (pMP3) memset(pMP3, 0, sizeof(*pMP3));
    return DRMP3_FALSE;
}

DRMP3_API void drmp3_uninit(drmp3 * pMP3) {
    (void)pMP3;
}

DRMP3_API drmp3_uint64 drmp3_read_pcm_frames_s16(drmp3 * pMP3,
                                                 drmp3_uint64 framesToRead,
                                                 drmp3_int16 * pBufferOut) {
    (void)pMP3;
    (void)framesToRead;
    (void)pBufferOut;
    return 0;
}
