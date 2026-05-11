/*
 * ports/pc386/hal_audio_pc386.c — audio HAL.
 *
 * No audio hardware until stage 6 (PIT-channel-2 PC speaker). Until
 * then, every PLAY entry returns to the BASIC prompt with a clear
 * error rather than halting the kernel — so a user typing PLAY TONE
 * sees a familiar error message and can continue.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_audio.h"

void hal_audio_init(void) {
    /* Called unconditionally from boot — must not error just because
     * the speaker isn't wired yet. */
}

void hal_audio_tone(double left_hz, double right_hz, int has_duration, long long duration_ms)
{
    (void)left_hz; (void)right_hz; (void)has_duration; (void)duration_ms;
    error("PLAY TONE not available until stage 6 (PC speaker)");
}

void hal_audio_sound(int slot, const char *ch, const char *type, double freq_hz, int volume)
{
    (void)slot; (void)ch; (void)type; (void)freq_hz; (void)volume;
    error("PLAY SOUND not available until stage 6 (PC speaker)");
}

void hal_audio_stop(void) {}
void hal_audio_volume(int left_pct, int right_pct) { (void)left_pct; (void)right_pct; }
void hal_audio_pause(void) {}
void hal_audio_resume(void) {}
