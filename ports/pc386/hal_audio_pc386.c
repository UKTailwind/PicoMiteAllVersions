/*
 * ports/pc386/hal_audio_pc386.c — audio HAL.
 *
 * No audio hardware is wired in stage 3. PIT-channel-2 PC speaker
 * support arrives in stage 6. Until then, every entry panics so
 * any BASIC program that hits PLAY surfaces the missing surface
 * loudly rather than silently.
 */

#include "hal/hal_audio.h"
#include "pc386_panic.h"

void hal_audio_init(void)
{
    /* Called unconditionally from boot — must not panic just because
     * the speaker isn't wired yet. The actual sound entries below do
     * the talking. */
}

void hal_audio_tone(double left_hz, double right_hz, int has_duration, long long duration_ms)
{
    (void)left_hz; (void)right_hz; (void)has_duration; (void)duration_ms;
    pc386_panic("PLAY TONE not available until stage 6 (PC speaker)");
}

void hal_audio_sound(int slot, const char *ch, const char *type, double freq_hz, int volume)
{
    (void)slot; (void)ch; (void)type; (void)freq_hz; (void)volume;
    pc386_panic("PLAY SOUND not available until stage 6 (PC speaker)");
}

void hal_audio_stop(void) {}
void hal_audio_volume(int left_pct, int right_pct) { (void)left_pct; (void)right_pct; }
void hal_audio_pause(void) {}
void hal_audio_resume(void) {}
