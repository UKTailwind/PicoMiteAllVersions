#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "audio_rp2_pwm_i2s.h"
#include "hal/hal_main_init.h"
#include "hal/hal_audio_control.h"
#include "hal/hal_audio_stream.h"
#include "synth_pcm.h"
#include "audio_play_common.h"
#include "drivers/audio_vs1053/audio_vs1053.h"
#include "drivers/vs1053/VS1053.h"

#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "PicoMiteI2S.pio.h"

#define sdi_send_buffer_local(a, b) sdi_send_buffer(a, b)
#define sendcount 64
#define sendstream 32

uint16_t AUDIO_CLK_PIN, AUDIO_MOSI_PIN, AUDIO_MISO_PIN, AUDIO_CS_PIN;
uint16_t AUDIO_RESET_PIN, AUDIO_DREQ_PIN, AUDIO_DCS_PIN, AUDIO_LDAC_PIN;
uint16_t AUDIO_L_PIN, AUDIO_R_PIN, AUDIO_SLICE;
uint16_t AUDIO_WRAP = 0;
uint16_t AUDIO_SPI;
uint16_t left = 0, right = 0;
volatile uint16_t VSbuffer = 0;
volatile int audio_shared_stream_active = 0;

static int audio_shared_acquired_target = 0;

PIO pioi2s;
uint8_t i2ssm;
extern uint I2SOff;
extern bool PIO2, PIO1, PIO0;

void __not_in_flash_func(DefaultAudio)(uint16_t left, uint16_t right) {
    pwm_set_both_levels(AUDIO_SLICE, (left * AUDIO_WRAP) >> 12, (right * AUDIO_WRAP) >> 12);
}

void __not_in_flash_func(SPIAudio)(uint16_t left, uint16_t right) {
    uint16_t l = 0x7000 | left, r = 0xF000 | right;
    gpio_put(AUDIO_CS_PIN, GPIO_PIN_RESET);
    spi_write16_blocking((AUDIO_SPI == 1 ? spi0 : spi1), &r, 1);
    gpio_put(AUDIO_CS_PIN, GPIO_PIN_SET);
    gpio_put(AUDIO_CS_PIN, GPIO_PIN_RESET);
    spi_write16_blocking((AUDIO_SPI == 1 ? spi0 : spi1), &l, 1);
    gpio_put(AUDIO_CS_PIN, GPIO_PIN_SET);
}

void (*AudioOutput)(uint16_t left, uint16_t right) = DefaultAudio;

void rp2_audio_enable_output_irq(void) {
    pwm_set_irq0_enabled(AUDIO_SLICE, true);
    pwm_set_enabled(AUDIO_SLICE, true);
}

void rp2_audio_disable_output_irq(void) {
    pwm_set_irq0_enabled(AUDIO_SLICE, false);
}

void rp2_audio_disable_output_irq_and_clear(void) {
    pwm_set_irq0_enabled(AUDIO_SLICE, false);
    pwm_clear_irq(AUDIO_SLICE);
}

void rp2_audio_output(uint16_t left, uint16_t right) {
    AudioOutput(left, right);
}

void MIPS16 __not_in_flash_func(on_pwm_wrap)(void) {
    static int noisedwellleft[MAXSOUNDS] = {0}, noisedwellright[MAXSOUNDS] = {0};
    static uint32_t noiseleft[MAXSOUNDS] = {0}, noiseright[MAXSOUNDS] = {0};
    static int repeatcount = 1;
    __dsb();
    pwm_clear_irq(AUDIO_SLICE);
    if (Option.audio_i2s_bclk) {
        if ((pioi2s->flevel & (0xf << (i2ssm * 8))) > (0x6 << (i2ssm * 8))) return;
        static int32_t left = 0, right = 0;
        if (CurrentlyPlaying == P_TONE) {
            if (!SoundPlay) {
                StopAudio();
                WAVcomplete = true;
            } else {
                while ((pioi2s->flevel & (0xf << (i2ssm * 8))) < (0x6 << (i2ssm * 8))) {
                    SoundPlay--;
                    synth_pcm_tone_frame(&left, &right);
                    pio_sm_put_blocking(pioi2s, i2ssm, left);
                    pio_sm_put_blocking(pioi2s, i2ssm, right);
                }
            }
            return;
        } else if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_MP3 || CurrentlyPlaying == P_ARRAY) {
            while ((pioi2s->flevel & (0xf << (i2ssm * 8))) < (0x6 << (i2ssm * 8))) {
                if (bcount[1] == 0 && bcount[2] == 0 && playreadcomplete == 1) {
                    pwm_set_irq_enabled(AUDIO_SLICE, false);
                    left = right = 0;
                    return;
                }
                if (!swingbuf || bcount[swingbuf] == 0) {
                    left = right = 0;
                    pio_sm_put(pioi2s, i2ssm, (uint32_t)left);
                    pio_sm_put(pioi2s, i2ssm, (uint32_t)right);
                    continue;
                }
                if (--repeatcount) {
                    pio_sm_put(pioi2s, i2ssm, left);
                    pio_sm_put(pioi2s, i2ssm, right);
                } else {
                    repeatcount = audiorepeat;
                    if (swingbuf) {
                        if (swingbuf == 1)
                            uplaybuff = g_buff1;
                        else
                            uplaybuff = g_buff2;
                        if ((CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MP3) && mono) {
                            left = right = (uplaybuff[ppos] << 16);
                            ppos++;
                        } else {
                            if (ppos < bcount[swingbuf]) {
                                left = uplaybuff[ppos] << 16;
                                right = uplaybuff[ppos + 1] << 16;
                                ppos += 2;
                            }
                        }
                        pio_sm_put(pioi2s, i2ssm, (uint32_t)(left));
                        pio_sm_put(pioi2s, i2ssm, (uint32_t)(right));
                        if (ppos == bcount[swingbuf]) {
                            int psave = ppos;
                            bcount[swingbuf] = 0;
                            ppos = 0;
                            if (swingbuf == 1)
                                swingbuf = 2;
                            else
                                swingbuf = 1;
                            if (bcount[swingbuf] == 0 && !playreadcomplete) {
                                if (swingbuf == 1) {
                                    swingbuf = 2;
                                    nextbuf = 1;
                                } else {
                                    swingbuf = 1;
                                    nextbuf = 2;
                                }
                                bcount[swingbuf] = psave;
                                ppos = 0;
                            }
                        }
                    }
                }
            }
            return;
        } else if (CurrentlyPlaying == P_SOUND) {
            while ((pioi2s->flevel & (0xf << (i2ssm * 8))) < (0x6 << (i2ssm * 8))) {
                int32_t leftv, rightv;
                synth_pcm_sound_frame(&leftv, &rightv);
                pio_sm_put_blocking(pioi2s, i2ssm, leftv);
                pio_sm_put_blocking(pioi2s, i2ssm, rightv);
            }
            return;
        } else if (CurrentlyPlaying == P_STOP) {
            while ((pioi2s->flevel & (0xf << (i2ssm * 8))) < (0x6 << (i2ssm * 8))) {
                pio_sm_put(pioi2s, i2ssm, left);
                pio_sm_put(pioi2s, i2ssm, right);
            }
            return;
        } else {
            while ((pioi2s->flevel & (0xf << (i2ssm * 8))) < (0x6 << (i2ssm * 8))) {
                pio_sm_put(pioi2s, i2ssm, left);
                pio_sm_put(pioi2s, i2ssm, right);
            }
            return;
        }
    }
    if (Option.AUDIO_MISO_PIN) {
        int32_t left = 0, right = 0;
        if (!(gpio_get(PinDef[Option.AUDIO_DREQ_PIN].GPno))) return;
        if (!(CurrentlyPlaying == P_TONE || CurrentlyPlaying == P_SOUND)) {
            VSbuffer = VS1053free();
            if (VSbuffer > 1023 - (CurrentlyPlaying == P_STREAM ? sendstream : sendcount)) return;
        }
        if (CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_MP3 || CurrentlyPlaying == P_MIDI || CurrentlyPlaying == P_ARRAY || CurrentlyPlaying == P_MOD) {
            if (bcount[1] == 0 && bcount[2] == 0 && playreadcomplete == 1) {
                return;
            }
            if (swingbuf) {
                int sendlen = ((bcount[swingbuf] - ppos) >= sendcount ? sendcount : bcount[swingbuf] - ppos);
                if (swingbuf == 1)
                    sdi_send_buffer_local((uint8_t *)&sbuff1[ppos], sendlen);
                else
                    sdi_send_buffer_local((uint8_t *)&sbuff2[ppos], sendlen);
                ppos += sendlen;
                if (ppos == bcount[swingbuf]) {
                    bcount[swingbuf] = 0;
                    ppos = 0;
                    if (swingbuf == 1)
                        swingbuf = 2;
                    else
                        swingbuf = 1;
                }
            }
        } else if (CurrentlyPlaying == P_STREAM) {
            int rp = *streamreadpointer, wp = *streamwritepointer;
            if (rp == wp) return;
            int i = wp - rp;
            if (i < 0) i += streamsize;
            if (i > sendstream) {
                if (streamsize - rp > sendcount) {
                    sdi_send_buffer((uint8_t *)&streambuffer[rp], sendstream);
                    rp += sendstream;
                } else {
                    char buff[sendstream];
                    int j = 0;
                    while (j < sendstream) {
                        buff[j++] = streambuffer[rp];
                        rp = (rp + 1) % streamsize;
                    }
                    sdi_send_buffer((uint8_t *)buff, sendstream);
                }
            }
            *streamreadpointer = rp;
        } else if (CurrentlyPlaying == P_SOUND) {
            int i, j;
            int leftv = 0, rightv = 0, Lcount = 0, Rcount = 0;
            for (i = 0; i < MAXSOUNDS; i++) {
                Lcount++;
                if (sound_mode_left[i] != whitenoise) {
                    sound_PhaseAC_left[i] = sound_PhaseAC_left[i] + sound_PhaseM_left[i];
                    if (sound_PhaseAC_left[i] >= 4096.0) sound_PhaseAC_left[i] -= 4096.0;
                    leftv += getsound(i, 2);
                } else {
                    if (noisedwellleft[i] <= 0) {
                        noisedwellleft[i] = sound_PhaseM_left[i];
                        noiseleft[i] = rand() % 3800 + 100;
                    }
                    if (noisedwellleft[i]) noisedwellleft[i]--;
                    j = (int)noiseleft[i];
                    leftv += j;
                }
                Rcount++;
                if (sound_mode_right[i] != whitenoise) {
                    sound_PhaseAC_right[i] = sound_PhaseAC_right[i] + sound_PhaseM_right[i];
                    if (sound_PhaseAC_right[i] >= 4096.0) sound_PhaseAC_right[i] -= 4096.0;
                    rightv += getsound(i, 3);
                } else {
                    if (noisedwellright[i] <= 0) {
                        noisedwellright[i] = sound_PhaseM_right[i];
                        noiseright[i] = rand() % 3800 + 100;
                    }
                    if (noisedwellright[i]) noisedwellright[i]--;
                    j = (int)noiseright[i];
                    rightv += j;
                }
            }
            left = ((leftv / Lcount) - 2000) * 16;
            right = ((rightv / Rcount) - 2000) * 16;
            sdi_send_buffer((uint8_t *)&left, 2);
            sdi_send_buffer((uint8_t *)&right, 2);
        } else if (CurrentlyPlaying == P_TONE) {
            if (!SoundPlay) {
                StopAudio();
                WAVcomplete = true;
            } else {
                SoundPlay--;
                if (mono) {
                    left = ((((int)SineTable[(int)PhaseAC_left]) - 2000) * 16);
                    PhaseAC_left = PhaseAC_left + PhaseM_left;
                    if (PhaseAC_left >= 4096.0) PhaseAC_left -= 4096.0;
                    right = left;
                } else {
                    left = (((SineTable[(int)PhaseAC_left]) - 2000) * 16);
                    right = (((SineTable[(int)PhaseAC_right]) - 2000) * 16);
                    PhaseAC_left = PhaseAC_left + PhaseM_left;
                    PhaseAC_right = PhaseAC_right + PhaseM_right;
                    if (PhaseAC_left >= 4096.0) PhaseAC_left -= 4096.0;
                    if (PhaseAC_right >= 4096.0) PhaseAC_right -= 4096.0;
                }
                sdi_send_buffer((uint8_t *)&left, 2);
                sdi_send_buffer((uint8_t *)&right, 2);
            }
        }
    } else {
        if (CurrentlyPlaying == P_TONE) {
            if (!SoundPlay) {
                StopAudio();
                WAVcomplete = true;
                return;
            } else {
                int32_t left_frame, right_frame;
                SoundPlay--;
                synth_pcm_tone_frame(&left_frame, &right_frame);
                left = (uint16_t)((left_frame / (2000 * 512)) + 2000);
                right = (uint16_t)((right_frame / (2000 * 512)) + 2000);
            }
        } else if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_ARRAY || CurrentlyPlaying == P_MP3) {
            if (--repeatcount) return;
            repeatcount = audiorepeat;
            if (bcount[1] == 0 && bcount[2] == 0 && playreadcomplete == 1) {
                pwm_set_irq_enabled(AUDIO_SLICE, false);
                return;
            }
            if (swingbuf) {
                if (swingbuf == 1)
                    playbuff = (uint16_t *)sbuff1;
                else
                    playbuff = (uint16_t *)sbuff2;
                if ((CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MP3) && mono) {
                    left = right = playbuff[ppos];
                    ppos++;
                } else {
                    if (ppos < bcount[swingbuf]) {
                        left = playbuff[ppos];
                        right = playbuff[ppos + 1];
                        ppos += 2;
                    }
                }
                if (ppos == bcount[swingbuf]) {
                    int psave = ppos;
                    bcount[swingbuf] = 0;
                    ppos = 0;
                    if (swingbuf == 1)
                        swingbuf = 2;
                    else
                        swingbuf = 1;
                    if (bcount[swingbuf] == 0 && !playreadcomplete) {
                        if (swingbuf == 1) {
                            swingbuf = 2;
                            nextbuf = 1;
                        } else {
                            swingbuf = 1;
                            nextbuf = 2;
                        }
                        bcount[swingbuf] = psave;
                        ppos = 0;
                    }
                }
            }
        } else if (CurrentlyPlaying == P_SOUND) {
            int leftv, rightv;
            synth_pcm_sound_sample(&leftv, &rightv);
            left = leftv + 2000;
            right = rightv + 2000;
        } else if (CurrentlyPlaying <= P_STOP) {
            return;
        } else {
            if (Option.AUDIO_MISO_PIN) return;
            left = right = 2000;
        }
        AudioOutput(left, right);
    }
}

void port_audio_init_from_options(void) {
    if (!(Option.AUDIO_L || Option.AUDIO_CLK_PIN)) return;

    if (Option.AUDIO_L) {
        ExtCfg(Option.AUDIO_L, EXT_BOOT_RESERVED, 0);
        ExtCfg(Option.AUDIO_R, EXT_BOOT_RESERVED, 0);
        AUDIO_L_PIN = PinDef[Option.AUDIO_L].GPno;
        AUDIO_R_PIN = PinDef[Option.AUDIO_R].GPno;
        gpio_set_function(AUDIO_L_PIN, GPIO_FUNC_PWM);
        gpio_set_function(AUDIO_R_PIN, GPIO_FUNC_PWM);
        gpio_set_slew_rate(AUDIO_L_PIN, GPIO_SLEW_RATE_SLOW);
        gpio_set_slew_rate(AUDIO_R_PIN, GPIO_SLEW_RATE_SLOW);
    } else {
        ExtCfg(Option.AUDIO_CS_PIN, EXT_BOOT_RESERVED, 0);
        AUDIO_CS_PIN = PinDef[Option.AUDIO_CS_PIN].GPno;
        gpio_init(AUDIO_CS_PIN);
        gpio_set_drive_strength(AUDIO_CS_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(AUDIO_CS_PIN, GPIO_PIN_SET);
        gpio_set_dir(AUDIO_CS_PIN, GPIO_OUT);
        gpio_set_slew_rate(AUDIO_CS_PIN, GPIO_SLEW_RATE_SLOW);

        AUDIO_CLK_PIN = PinDef[Option.AUDIO_CLK_PIN].GPno;
        ExtCfg(Option.AUDIO_CLK_PIN, EXT_BOOT_RESERVED, 0);
        AUDIO_MOSI_PIN = PinDef[Option.AUDIO_MOSI_PIN].GPno;
        ExtCfg(Option.AUDIO_MOSI_PIN, EXT_BOOT_RESERVED, 0);
        if (PinDef[Option.AUDIO_CLK_PIN].mode & SPI0SCK && PinDef[Option.AUDIO_MOSI_PIN].mode & SPI0TX) {
            SPI0locked = 1;
            AUDIO_SPI = 1;
        } else if (PinDef[Option.AUDIO_CLK_PIN].mode & SPI1SCK && PinDef[Option.AUDIO_MOSI_PIN].mode & SPI1TX) {
            SPI1locked = 1;
            AUDIO_SPI = 2;
        }
        gpio_init(AUDIO_CLK_PIN);
        gpio_set_drive_strength(AUDIO_CLK_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(AUDIO_CLK_PIN, GPIO_PIN_RESET);
        gpio_set_dir(AUDIO_CLK_PIN, GPIO_OUT);
        gpio_set_slew_rate(AUDIO_CLK_PIN, GPIO_SLEW_RATE_FAST);
        gpio_set_function(AUDIO_CLK_PIN, GPIO_FUNC_SPI);
        gpio_set_drive_strength(AUDIO_MOSI_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(AUDIO_MOSI_PIN, GPIO_PIN_RESET);
        gpio_set_dir(AUDIO_MOSI_PIN, GPIO_OUT);
        gpio_set_slew_rate(AUDIO_MOSI_PIN, GPIO_SLEW_RATE_FAST);
        gpio_set_function(AUDIO_MOSI_PIN, GPIO_FUNC_SPI);
        if (Option.AUDIO_MISO_PIN) {
            AUDIO_MISO_PIN = PinDef[Option.AUDIO_MISO_PIN].GPno;
            ExtCfg(Option.AUDIO_MISO_PIN, EXT_BOOT_RESERVED, 0);
            gpio_set_function(AUDIO_MISO_PIN, GPIO_FUNC_SPI);
            gpio_set_input_hysteresis_enabled(AUDIO_MISO_PIN, true);
            spi_init((AUDIO_SPI == 1 ? spi0 : spi1), 200000);
            spi_set_format((AUDIO_SPI == 1 ? spi0 : spi1), 8, false, false, SPI_MSB_FIRST);
        } else {
            spi_init((AUDIO_SPI == 1 ? spi0 : spi1), 16000000);
            spi_set_format((AUDIO_SPI == 1 ? spi0 : spi1), 16, true, true, SPI_MSB_FIRST);
        }
    }
    if (!Option.AUDIO_DCS_PIN) {
        AUDIO_SLICE = Option.AUDIO_SLICE;
        AUDIO_WRAP = (Option.CPU_Speed * 10) / 441 - 1;
        pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
        if (Option.AUDIO_L) {
            pwm_set_chan_level(AUDIO_SLICE, PWM_CHAN_A, AUDIO_WRAP >> 1);
            pwm_set_chan_level(AUDIO_SLICE, PWM_CHAN_B, AUDIO_WRAP >> 1);
            AudioOutput = DefaultAudio;
        } else {
            AudioOutput = SPIAudio;
        }
        AudioOutput(2000, 2000);
        pwm_clear_irq(AUDIO_SLICE);
        irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
        irq_set_enabled(PWM_IRQ_WRAP, true);
        irq_set_priority(PWM_IRQ_WRAP, 255);
        pwm_set_enabled(AUDIO_SLICE, true);
    } else {
        AUDIO_DREQ_PIN = PinDef[Option.AUDIO_DREQ_PIN].GPno;
        ExtCfg(Option.AUDIO_DREQ_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(AUDIO_DREQ_PIN);
        gpio_set_dir(AUDIO_DREQ_PIN, GPIO_IN);
        gpio_set_input_hysteresis_enabled(AUDIO_DREQ_PIN, true);

        AUDIO_DCS_PIN = PinDef[Option.AUDIO_DCS_PIN].GPno;
        ExtCfg(Option.AUDIO_DCS_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(AUDIO_DCS_PIN);
        gpio_set_drive_strength(AUDIO_DCS_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(AUDIO_DCS_PIN, GPIO_PIN_SET);
        gpio_set_dir(AUDIO_DCS_PIN, GPIO_OUT);
        gpio_set_slew_rate(AUDIO_DCS_PIN, GPIO_SLEW_RATE_SLOW);

        AUDIO_RESET_PIN = PinDef[Option.AUDIO_RESET_PIN].GPno;
        ExtCfg(Option.AUDIO_RESET_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(AUDIO_RESET_PIN);
        gpio_set_drive_strength(AUDIO_RESET_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(AUDIO_RESET_PIN, GPIO_PIN_RESET);
        gpio_set_dir(AUDIO_RESET_PIN, GPIO_OUT);
        gpio_set_slew_rate(AUDIO_RESET_PIN, GPIO_SLEW_RATE_SLOW);
        AUDIO_SLICE = Option.AUDIO_SLICE;
        AUDIO_WRAP = (Option.CPU_Speed * 10) / 441 - 1;
        pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
        pwm_clear_irq(AUDIO_SLICE);
        irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
        irq_set_enabled(PWM_IRQ_WRAP, true);
        irq_set_priority(PWM_IRQ_WRAP, 255);
    }
}

void start_i2s(int pior, int sm) {
    if (!Option.audio_i2s_bclk) return;
    i2ssm = sm;
#ifdef rp2350
    pioi2s = (pior == 0 ? pio0 : (pior == 1 ? pio1 : pio2));
#else
    pioi2s = (pior == 0 ? pio0 : pio1);
#endif
    port_audio_i2s_pio_add_program(pioi2s);
    gpio_set_input_enabled(PinDef[Option.audio_i2s_data].GPno, true);
    gpio_set_input_enabled(PinDef[Option.audio_i2s_bclk].GPno, true);
    gpio_set_input_enabled(PinDef[Option.audio_i2s_bclk].GPno + 1, true);
#ifdef rp2350
    gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno, pior == 0 ? GPIO_FUNC_PIO0 : (pior == 1 ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
    gpio_set_function(PinDef[Option.audio_i2s_data].GPno, pior == 0 ? GPIO_FUNC_PIO0 : (pior == 1 ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
    gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno + 1, pior == 0 ? GPIO_FUNC_PIO0 : (pior == 1 ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
#else
    gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno, pior == 0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    gpio_set_function(PinDef[Option.audio_i2s_data].GPno, pior == 0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno + 1, pior == 0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
#endif
    ExtCfg(Option.audio_i2s_bclk, EXT_BOOT_RESERVED, 0);
    ExtCfg(Option.audio_i2s_data, EXT_BOOT_RESERVED, 0);
    ExtCfg(PINMAP[PinDef[Option.audio_i2s_bclk].GPno + 1], EXT_BOOT_RESERVED, 0);

    pio_sm_config cfg = i2s_program_get_default_config(I2SOff);
    sm_config_set_out_pins(&cfg, PinDef[Option.audio_i2s_data].GPno, 1);
    sm_config_set_sideset_pins(&cfg, PinDef[Option.audio_i2s_bclk].GPno);
    sm_config_set_sideset(&cfg, 2, false, false);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg, (Option.CPU_Speed * 1000.0f) / (float)(44100 * 128));
    sm_config_set_out_shift(&cfg, false, false, false);
    sm_config_set_in_shift(&cfg, false, false, false);

    pio_sm_init(pioi2s, sm, I2SOff, &cfg);
    pio_sm_set_consecutive_pindirs(pioi2s, sm, PinDef[Option.audio_i2s_data].GPno, 1, true);
    pio_sm_set_consecutive_pindirs(pioi2s, sm, PinDef[Option.audio_i2s_bclk].GPno, 2, true);
    pio_sm_set_enabled(pioi2s, sm, true);

    AUDIO_SLICE = Option.AUDIO_SLICE;
    AUDIO_WRAP = (Option.CPU_Speed * 10) / 441 - 1;
    pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
    pwm_clear_irq(AUDIO_SLICE);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    irq_set_priority(PWM_IRQ_WRAP, 255);
    pwm_set_enabled(AUDIO_SLICE, true);
    if (pior == 2)
        PIO2 = false;
    else if (pior == 1)
        PIO1 = false;
    else
        PIO0 = false;
}

void rp2_audio_clear_sample_buffer_state(int complete) {
    uint32_t save = save_and_disable_interrupts();
    bcount[1] = bcount[2] = 0;
    wav_filesize = 0;
    swingbuf = nextbuf = 0;
    audio_shared_acquired_target = 0;
    ppos = 0;
    playreadcomplete = complete;
    restore_interrupts(save);
}

static const unsigned short * rp2_audio_table_for(char type) {
    switch (type) {
    case 'O':
    case 'o':
        return nulltable;
    case 'Q':
    case 'q':
        return squaretable;
    case 'T':
    case 't':
        return triangletable;
    case 'W':
    case 'w':
        return sawtable;
    case 'S':
    case 's':
        return SineTable;
    case 'P':
    case 'p':
        setnoise();
        return noisetable;
    case 'N':
    case 'n':
        return whitenoise;
    case 'U':
    case 'u':
        return usertable;
    default:
        return NULL;
    }
}

static int rp2_audio_sound_slots_off(void) {
    for (int i = 0; i < MAXSOUNDS; i++) {
        if (sound_mode_left[i] != (unsigned short *)nulltable) return 0;
        if (sound_mode_right[i] != (unsigned short *)nulltable) return 0;
    }
    return 1;
}

static void rp2_audio_rampvolume(int l, int r, int channel, int target) {
    if (optionfastaudio) {
        if (l) sound_v_left[channel] = target;
        if (r) sound_v_right[channel] = target;
        return;
    }
    int ramptime = PWM_FREQ > 0 ? 1000000 / PWM_FREQ + 2 : 2;
    if (l && r) {
        int start = sound_v_left[channel];
        int step = start > target ? -1 : 1;
        for (int i = start + step; step > 0 ? i <= target : i >= target; i += step) {
            sound_v_left[channel] = i;
            sound_v_right[channel] = i;
            uSec(ramptime);
        }
    } else if (l) {
        int start = sound_v_left[channel];
        int step = start > target ? -1 : 1;
        for (int i = start + step; step > 0 ? i <= target : i >= target; i += step) {
            sound_v_left[channel] = i;
            uSec(ramptime);
        }
    } else if (r) {
        int start = sound_v_right[channel];
        int step = start > target ? -1 : 1;
        for (int i = start + step; step > 0 ? i <= target : i >= target; i += step) {
            sound_v_right[channel] = i;
            uSec(ramptime);
        }
    }
}

void hal_audio_init(void) {
}

int hal_audio_tone_interrupt_supported(void) {
    return 1;
}

void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, int64_t duration_ms) {
    SoundPlay = 1000;
    double duration = has_duration ? ((double)duration_ms / 1000.0) : 1.0;
    uint64_t play_duration = UINT64_MAX;
    if (has_duration) {
        if (duration_ms == 0) return;
        if (left_hz >= 10.0) {
            double hw = (double)PWM_FREQ / left_hz;
            double samples = duration * (double)PWM_FREQ;
            play_duration = (uint64_t)(samples / hw) * (uint64_t)hw;
        } else {
            play_duration = (uint64_t)(duration * (double)PWM_FREQ);
        }
    }

    mono = (left_hz == right_hz && vol_left == vol_right) ? 1 : 0;
    rp2_audio_disable_output_irq();
    PhaseM_left = (float)(left_hz / (double)PWM_FREQ * 4096.0);
    PhaseM_right = (float)(right_hz / (double)PWM_FREQ * 4096.0);
    WAV_fnbr = 0;
    SoundPlay = play_duration;
    if (!(CurrentlyPlaying == P_PAUSE_TONE || CurrentlyPlaying == P_TONE)) {
        setrate(PWM_FREQ);
        PhaseAC_right = 0.0f;
        PhaseAC_left = 0.0f;
        if (Option.AUDIO_MISO_PIN) audio_vs1053_play_immediate(P_TONE);
    }
    rp2_audio_enable_output_irq();
}

void hal_audio_sound(int slot, const char * ch, const char * type,
                     double freq_hz, int volume) {
    int channel = slot - 1;
    if (channel < 0 || channel >= MAXSOUNDS || ch == NULL || type == NULL) return;
    const unsigned short * tbl = rp2_audio_table_for(type[0]);
    if (!tbl) return;

    int left_ch = (ch[0] == 'L' || ch[0] == 'l' || ch[0] == 'B' || ch[0] == 'b' ||
                   ch[0] == 'M' || ch[0] == 'm');
    int right_ch = (ch[0] == 'R' || ch[0] == 'r' || ch[0] == 'B' || ch[0] == 'b' ||
                    ch[0] == 'M' || ch[0] == 'm');
    int target_volume = audio_play_volume_to_synth(volume);
    uint16_t * lastleft = (uint16_t *)sound_mode_left[channel];
    uint16_t * lastright = (uint16_t *)sound_mode_right[channel];
    float phase = (tbl == whitenoise) ? (float)freq_hz
                                      : (float)(freq_hz / (double)PWM_FREQ * 4096.0);

    WAV_fnbr = 0;
    if (left_ch) {
        if (lastleft != (uint16_t *)tbl) {
            if (!Option.AUDIO_MISO_PIN) rp2_audio_rampvolume(1, 0, channel, 0);
            sound_PhaseAC_left[channel] = 0.0f;
        }
        sound_PhaseM_left[channel] = phase;
    }
    if (right_ch) {
        if (lastright != (uint16_t *)tbl) {
            if (!Option.AUDIO_MISO_PIN) rp2_audio_rampvolume(0, 1, channel, 0);
            sound_PhaseAC_right[channel] = 0.0f;
        }
        sound_PhaseM_right[channel] = phase;
    }
    if (left_ch && right_ch &&
        sound_v_left[channel] == sound_v_right[channel] &&
        !Option.AUDIO_MISO_PIN) {
        rp2_audio_rampvolume(1, 1, channel, target_volume);
    } else {
        if (left_ch && !Option.AUDIO_MISO_PIN) rp2_audio_rampvolume(1, 0, channel, target_volume);
        if (right_ch && !Option.AUDIO_MISO_PIN) rp2_audio_rampvolume(0, 1, channel, target_volume);
    }
    if (Option.AUDIO_MISO_PIN) {
        if (left_ch) sound_v_left[channel] = target_volume;
        if (right_ch) sound_v_right[channel] = target_volume;
    }
    if (left_ch) sound_mode_left[channel] = (unsigned short *)tbl;
    if (right_ch) sound_mode_right[channel] = (unsigned short *)tbl;
    if (rp2_audio_sound_slots_off()) {
        hal_audio_stop();
        return;
    }
    if (!(CurrentlyPlaying == P_SOUND || CurrentlyPlaying == P_PAUSE_SOUND)) {
        setrate(PWM_FREQ);
        if (Option.AUDIO_MISO_PIN) audio_vs1053_play_immediate(P_SOUND);
        rp2_audio_enable_output_irq();
    }
}

void hal_audio_stop(void) {
    for (int i = 0; i < MAXSOUNDS; i++) {
        sound_PhaseM_left[i] = 0.0f;
        sound_PhaseM_right[i] = 0.0f;
        sound_PhaseAC_left[i] = 0.0f;
        sound_PhaseAC_right[i] = 0.0f;
        sound_mode_left[i] = (unsigned short *)nulltable;
        sound_mode_right[i] = (unsigned short *)nulltable;
    }
    SoundPlay = 0;
    rp2_audio_disable_output_irq_and_clear();
}

void hal_audio_volume(int left_pct, int right_pct) {
    vol_left = left_pct;
    vol_right = right_pct;
    if (CurrentlyPlaying == P_TONE && vol_left != vol_right && mono) mono = 0;
    if (Option.AUDIO_MISO_PIN && CurrentlyPlaying != P_NOTHING) {
        rp2_audio_disable_output_irq();
        setVolumes(vol_left, vol_right);
        rp2_audio_enable_output_irq();
    }
}

void hal_audio_pause(void) {
}

void hal_audio_resume(void) {
}

void setrate(int rate) {
    static int lastrate = 0;
    if (rate == lastrate) return;
    lastrate = rate;
    AUDIO_WRAP = (Option.CPU_Speed * 1000) / rate - 1;
    pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
    if (Option.AUDIO_L) {
        pwm_set_both_levels(AUDIO_SLICE, (int)(((AUDIO_WRAP >> 1) * 4000) / 4096), (int)(((AUDIO_WRAP >> 1) * 4000) / 4096));
    }
    pwm_clear_irq(AUDIO_SLICE);
    if (Option.audio_i2s_bclk) {
        float clockdiv = (Option.CPU_Speed * 1000.0f) / (float)(rate * 128);
        pio_sm_set_clkdiv(pioi2s, i2ssm, clockdiv);
    }
}

void __not_in_flash_func(iconvert)(uint16_t * ibuff, int16_t * sbuff, int count) {
    int i;
    for (i = 0; i < (count); i += 2) {
        ibuff[i] = (uint16_t)((((int)sbuff[i] * mapping[vol_left] / 2000 + 32768)) >> 4);
        ibuff[i + 1] = (uint16_t)((((int)sbuff[i + 1] * mapping[vol_right] / 2000 + 32768)) >> 4);
    }
}

void MIPS64 __not_in_flash_func(i2sconvert)(int16_t * fbuff, int16_t * sbuff, int count) {
    int i;
    for (i = 0; i < (count); i += 2) {
        sbuff[i] = (int16_t)((int)(fbuff[i]) * mapping[vol_left] / 2000);
        sbuff[i + 1] = (int16_t)((int)(fbuff[i + 1]) * mapping[vol_right] / 2000);
    }
}

static int pico_audio_stream_buffer_capacity_frames(void) {
    return WAV_BUFFER_SIZE / (int)(sizeof(int16_t) * 2);
}

static int pico_audio_stream_claim_target(void) {
    int target = 0;
    uint32_t save = save_and_disable_interrupts();
    if (audio_shared_stream_active && !audio_shared_acquired_target) {
        if (swingbuf == 0 && bcount[1] == 0 && bcount[2] == 0) {
            target = 1;
        } else if (swingbuf != nextbuf) {
            target = (swingbuf == 2) ? 1 : 2;
            if (bcount[target] != 0) target = 0;
        }
    }
    restore_interrupts(save);
    return target;
}

static void pico_audio_stream_convert_buffer(char * dst, int samples) {
    if (Option.audio_i2s_bclk) {
        i2sconvert((int16_t *)dst, (int16_t *)dst, samples);
    } else {
        iconvert((uint16_t *)dst, (int16_t *)dst, samples);
    }
}

static int pico_audio_stream_publish_target(int target, int samples) {
    if (target < 1 || target > 2 || samples <= 0) return 0;
    int published = 0;
    uint32_t save = save_and_disable_interrupts();
    if (audio_shared_stream_active && bcount[target] == 0) {
        wav_filesize = samples;
        if (swingbuf == 0) {
            swingbuf = target;
            nextbuf = (target == 1) ? 2 : 1;
            ppos = 0;
        } else {
            nextbuf = swingbuf;
        }
        bcount[target] = samples;
        if (swingbuf == target) {
            pwm_set_irq0_enabled(AUDIO_SLICE, true);
            pwm_set_enabled(AUDIO_SLICE, true);
        }
        published = 1;
    }
    restore_interrupts(save);
    return published;
}

void pico_audio_publish_initial_target(int target, int count) {
    if (target < 1 || target > 2) return;
    int other = (target == 1) ? 2 : 1;
    uint32_t save = save_and_disable_interrupts();
    bcount[other] = 0;
    wav_filesize = count;
    swingbuf = target;
    nextbuf = other;
    ppos = 0;
    bcount[target] = count;
    restore_interrupts(save);
}

void pico_audio_publish_refill_target(int target, int count) {
    if (target < 1 || target > 2) return;
    uint32_t save = save_and_disable_interrupts();
    wav_filesize = count;
    nextbuf = swingbuf;
    bcount[target] = count;
    restore_interrupts(save);
}

int hal_audio_sample_begin(int sample_rate_hz) {
    if (Option.AUDIO_MISO_PIN) return -1;
    if (!(Option.AUDIO_L || Option.AUDIO_CLK_PIN || Option.audio_i2s_bclk)) return -1;
    FreeMemorySafe((void **)&sbuff1);
    FreeMemorySafe((void **)&sbuff2);
    sbuff1 = GetMemory(WAV_BUFFER_SIZE);
    sbuff2 = GetMemory(WAV_BUFFER_SIZE);
    ubuff1 = (uint16_t *)sbuff1;
    ubuff2 = (uint16_t *)sbuff2;
    g_buff1 = (int16_t *)sbuff1;
    g_buff2 = (int16_t *)sbuff2;
    rp2_audio_clear_sample_buffer_state(0);
    mono = 0;
    audiorepeat = 1;
    int actualrate = sample_rate_hz;
    if (Option.AUDIO_L) {
        while (actualrate < 32000) {
            actualrate += sample_rate_hz;
            audiorepeat++;
        }
    }
    setrate(actualrate);
    audio_shared_stream_active = 1;
    return 0;
}

void hal_audio_sample_end(void) {
    audio_shared_stream_active = 0;
    rp2_audio_clear_sample_buffer_state(1);
}

void hal_audio_sample_eof(void) {
    playreadcomplete = 1;
}

int hal_audio_sample_space(void) {
    int capacity = pico_audio_stream_buffer_capacity_frames();
    if (!audio_shared_stream_active) return 0;
    if (swingbuf == 0 && bcount[1] == 0 && bcount[2] == 0) return capacity;
    if (swingbuf != nextbuf) {
        int target = (swingbuf == 2) ? 1 : 2;
        if (bcount[target] == 0) return capacity;
    }
    return 0;
}

int hal_audio_sample_queued(void) {
    int samples = 0;
    if (bcount[1] > 0) samples += bcount[1];
    if (bcount[2] > 0) samples += bcount[2];
    if (swingbuf == 1 && ppos < bcount[1])
        samples -= ppos;
    else if (swingbuf == 2 && ppos < bcount[2])
        samples -= ppos;
    if (samples < 0) samples = 0;
    return samples / 2;
}

int hal_audio_sample_push(const int16_t * frames, int frame_count) {
    if (!audio_shared_stream_active || frame_count <= 0 || frames == NULL) return 0;
    int capacity = pico_audio_stream_buffer_capacity_frames();
    int frames_to_copy = frame_count > capacity ? capacity : frame_count;
    int target = pico_audio_stream_claim_target();
    if (!target) return 0;
    char * dst = (target == 1) ? sbuff1 : sbuff2;
    if (!dst) return 0;
    memcpy(dst, frames, (size_t)frames_to_copy * 2u * sizeof(int16_t));
    int samples = frames_to_copy * 2;
    pico_audio_stream_convert_buffer(dst, samples);
    if (!pico_audio_stream_publish_target(target, samples)) return 0;
    return frames_to_copy;
}

int hal_audio_sample_acquire(int16_t ** frames, int * frame_capacity) {
    if (!audio_shared_stream_active || frames == NULL || frame_capacity == NULL) return 0;
    int target = pico_audio_stream_claim_target();
    if (!target) return 0;
    char * dst = (target == 1) ? sbuff1 : sbuff2;
    if (!dst) return 0;
    uint32_t save = save_and_disable_interrupts();
    if (audio_shared_stream_active && !audio_shared_acquired_target && bcount[target] == 0) {
        audio_shared_acquired_target = target;
    } else {
        target = 0;
    }
    restore_interrupts(save);
    if (!target) return 0;
    *frames = (int16_t *)dst;
    *frame_capacity = pico_audio_stream_buffer_capacity_frames();
    return 1;
}

void hal_audio_sample_commit(int frame_count) {
    int target = audio_shared_acquired_target;
    if (!target || frame_count <= 0 || !audio_shared_stream_active) {
        audio_shared_acquired_target = 0;
        return;
    }
    int capacity = pico_audio_stream_buffer_capacity_frames();
    int frames_to_commit = frame_count > capacity ? capacity : frame_count;
    char * dst = (target == 1) ? sbuff1 : sbuff2;
    if (!dst) {
        audio_shared_acquired_target = 0;
        return;
    }
    int samples = frames_to_commit * 2;
    pico_audio_stream_convert_buffer(dst, samples);
    (void)pico_audio_stream_publish_target(target, samples);
    audio_shared_acquired_target = 0;
}

void * hal_audio_workmem_alloc(unsigned long bytes) {
    return GetMemory((int)bytes);
}

void * hal_audio_workmem_realloc(void * p, unsigned long bytes) {
    return ReAllocMemory(p, (int)bytes);
}

void hal_audio_workmem_free(void * p) {
    if (p) FreeMemory(p);
}
