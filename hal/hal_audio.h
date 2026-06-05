/*
 * hal/hal_audio.h - compatibility umbrella for the split audio HAL.
 *
 * New shared code should include hal_audio_control.h for PLAY command
 * primitives or hal_audio_stream.h for PCM stream/workmem primitives.
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include "hal_audio_control.h"
#include "hal_audio_stream.h"

#endif /* HAL_AUDIO_H */
