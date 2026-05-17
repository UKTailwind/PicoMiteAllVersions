/*
 * hal_audio_esp32_stub.c — Phase B stub for hal/hal_audio.h.
 * No-op bodies. Phase E+ wires I2S DAC.
 */

#include <stdint.h>
#include "hal/hal_audio.h"

/* ADC / I2S DMA channel state. Audio path uses two DMA channels (one
 * for output buffer, one for the descriptor ring). Stdio scope has no
 * audio output; channels stay at 0 = unconfigured. ADC variants are
 * for SOUND ADC INPUT pin reads, also unwired here. */
uint32_t dma_tx_chan = 0;
uint32_t dma_rx_chan = 0;
uint32_t dma_tx_chan2 = 0;
uint32_t dma_rx_chan2 = 0;
uint32_t ADC_dma_chan = 0;
uint32_t ADC_dma_chan2 = 0;

/* SOUND ADC dual-buffer flag and "DMA in flight" sentinel. Both stay
 * false on a port without active audio. */
#include <stdbool.h>
bool ADCDualBuffering = 0;
bool dmarunning = 0;

void hal_audio_init(void) {}
void hal_audio_tone(double l, double r, int hd, int64_t d) { (void)l; (void)r; (void)hd; (void)d; }
void hal_audio_sound(int s, const char *c, const char *t, double f, int v) {
    (void)s; (void)c; (void)t; (void)f; (void)v;
}
void hal_audio_stop(void) {}
void hal_audio_volume(int l, int r) { (void)l; (void)r; }
void hal_audio_pause(void) {}
void hal_audio_resume(void) {}
