#ifndef DRIVERS_AUDIO_RP2_PWM_I2S_H
#define DRIVERS_AUDIO_RP2_PWM_I2S_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint16_t VSbuffer;
extern volatile int audio_shared_stream_active;

void DefaultAudio(uint16_t left, uint16_t right);
void SPIAudio(uint16_t left, uint16_t right);
void on_pwm_wrap(void);

void start_i2s(int pio, int sm);
void rp2_audio_clear_sample_buffer_state(int complete);
void rp2_audio_enable_output_irq(void);
void rp2_audio_disable_output_irq(void);
void rp2_audio_disable_output_irq_and_clear(void);
void rp2_audio_output(uint16_t left, uint16_t right);
void setrate(int rate);

void iconvert(uint16_t * ibuff, int16_t * sbuff, int count);
void i2sconvert(int16_t * fbuff, int16_t * sbuff, int count);
void pico_audio_publish_initial_target(int target, int count);
void pico_audio_publish_refill_target(int target, int count);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AUDIO_RP2_PWM_I2S_H */
