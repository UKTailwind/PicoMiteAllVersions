/*
 * core/state/audio_state.c — hoisted audio globals referenced across TUs.
 *
 * See docs/real-hal-plan.md § "Cross-cutting state — hoisted in Phase 0.5".
 *
 * Phase 0.5 hoist: audio globals that appear in both the device and host
 * branches of Audio.c (gated by #ifndef MMBASIC_HOST) AND are referenced
 * from outside Audio.c. Consolidates the duplicated definitions into one
 * translation unit.
 *
 * Hoisted:
 *   - CurrentlyPlaying: audio state machine (MM.INFO, OPTION AUDIO, etc.
 *     read this from Commands.c / MM_Misc.c / PicoMite.c / Touch.c).
 *   - WAVInterrupt    : BASIC-visible interrupt hook (set from cmd_play,
 *     read from External.c's interrupt dispatcher).
 *   - WAVcomplete     : done-flag read by the same dispatcher.
 *
 * Deferred to Audio.c proper (device-only, not cross-cutting):
 *   - sound_v_left/right, sound_PhaseAC_*, sound_PhaseM_* (voice slot
 *     arrays — used only inside Audio.c's DMA + synth code).
 *   - bcount[3], swingbuf, nextbuf, playreadcomplete, ppos (sample-buffer
 *     bookkeeping — used only by Audio.c's PIO/DMA path).
 *   - WAV codec object pointers (mywav, myflac, mymp3) — local decoder
 *     state.
 *
 * When Phase 6 lands hal_audio, the codec/DMA/PWM machinery becomes a
 * driver-private concern; what remains as cross-cutting state (these
 * three) is what BASIC-visible commands poll.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

volatile e_CurrentlyPlaying CurrentlyPlaying = P_NOTHING;
char *WAVInterrupt = NULL;
bool WAVcomplete = 0;
