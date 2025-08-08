/***********************************************************************************************************************
PicoMite MMBasic

mmc_stm32.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/


/*------------------------------------------------------------------------/
/  MMCv3/SDv1/SDv2 (in SPI mode) control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2014, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------*/

#include "Hardware_Includes.h"
#include <stddef.h>
#include "diskio.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "MM_Misc.h"
#ifdef PICOMITEWEB
#include "pico/cyw43_arch.h"
#endif
#ifdef PICOMITEVGA
#include "Include.h"
#endif
#include "VS1053.h"
#ifndef PICOMITEWEB
#include "pico/multicore.h"
#endif
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
//#include "integer.h"
int SPISpeed=0xFF;
//#define SD_CS_PIN Option.SD_CS
//#define SPI_MOSI_PIN Option.SYSTEM_MOSI
//#define SPI_CLK_PIN Option.SYSTEM_CLK
//define SPI_MISO_PIN Option.SYSTEM_MISO
uint16_t SPI_CLK_PIN,SPI_MOSI_PIN,SPI_MISO_PIN;
#if defined(PICOMITE) && defined(rp2350)
uint16_t LCD_CLK_PIN,LCD_MOSI_PIN,LCD_MISO_PIN;
#endif
uint16_t SD_CLK_PIN,SD_MOSI_PIN,SD_MISO_PIN, SD_CS_PIN;
uint16_t AUDIO_CLK_PIN,AUDIO_MOSI_PIN,AUDIO_MISO_PIN, AUDIO_CS_PIN, AUDIO_RESET_PIN, AUDIO_DREQ_PIN, AUDIO_DCS_PIN, AUDIO_LDAC_PIN;
uint16_t AUDIO_L_PIN, AUDIO_R_PIN, AUDIO_SLICE;
#define CD	MDD_SDSPI_CardDetectState()	/* Card detected   (yes:true, no:false, default:true) */
#define WP	MDD_SDSPI_WriteProtectState()		/* Write protected (yes:true, no:false, default:false) */
/* SPI bit rate controls */
int SD_SPI_SPEED=SD_SLOW_SPI_SPEED;
//#define	FCLK_SLOW()		{SD_SPI_SPEED=SD_SLOW_SPI_SPEED;}	/* Set slow clock (100k-400k) */
//#define	FCLK_FAST()		{SD_SPI_SPEED=SD_FAST_SPI_SPEED;}/* Set fast clock (depends on the CSD) */
extern volatile BYTE SDCardStat;
volatile int diskcheckrate=0;
uint16_t left=0,right=0;
const uint8_t high[512]={[0 ... 511]=0xFF};
int CurrentSPISpeed=NONE_SPI_SPEED;
#define slow_clock 200000
#define SPI_FAST 18000000
uint16_t AUDIO_WRAP=0;
BYTE (*xchg_byte)(BYTE data_out)= NULL;
void (*xmit_byte_multi)(const BYTE *buff, int cnt)= NULL;
void (*rcvr_byte_multi)(BYTE *buff, int cnt)= NULL;
int (*SET_SPI_CLK)(int speed, int polarity, int edge)=NULL;
#if defined(PICOMITE) && defined(rp2350)
void (*lcd_xmit_byte_multi)(const BYTE *buff, int cnt)= NULL;
void (*lcd_rcvr_byte_multi)(BYTE *buff, int cnt)= NULL;
int (*LCD_SET_SPI_CLK)(int speed, int polarity, int edge)=NULL;
#endif
extern const uint8_t PINMAP[];
const int mapping[101]={
	0,4,11,18,25,33,41,49,57,66,75,
	84,93,103,113,123,134,145,156,167,179,
	191,203,216,228,241,255,268,282,296,311,
	325,340,355,371,387,403,419,436,453,470,
	487,505,523,541,560,578,597,617,636,656,
	676,697,718,738,760,781,803,825,847,870,
	893,916,940,963,987,1012,1036,1061,1086,1111,
	1137,1163,1189,1216,1242,1269,1297,1324,1352,1380,
	1408,1437,1466,1495,1525,1554,1584,1615,1645,1676,
	1707,1739,1770,1802,1834,1867,1900,1933,1966,2000
	};
uint8_t I2C0locked=0;
uint8_t I2C1locked=0;
uint8_t SPI0locked=0;
uint8_t SPI1locked=0;
int BacklightSlice=-1;
int BacklightChannel=-1;
#if defined(PICOMITE) && defined(rp2350)
int KeyboardlightSlice=-1;
int KeyboardlightChannel=-1;
#endif
extern const unsigned short whitenoise[2];
uint16_t AUDIO_SPI;
volatile uint16_t VSbuffer=0;
void __not_in_flash_func(DefaultAudio)(uint16_t left, uint16_t right){
	pwm_set_both_levels(AUDIO_SLICE,(left*AUDIO_WRAP)>>12,(right*AUDIO_WRAP)>>12);
}
void __not_in_flash_func(SPIAudio)(uint16_t left, uint16_t right){
	uint16_t l=0x7000 | left, r=0xF000 | right;
	gpio_put(AUDIO_CS_PIN,GPIO_PIN_RESET);
	spi_write16_blocking((AUDIO_SPI==1 ? spi0 : spi1),&r,1);
	gpio_put(AUDIO_CS_PIN,GPIO_PIN_SET);
	gpio_put(AUDIO_CS_PIN,GPIO_PIN_RESET);
	spi_write16_blocking((AUDIO_SPI==1 ? spi0 : spi1),&l,1);
	gpio_put(AUDIO_CS_PIN,GPIO_PIN_SET);

	
}
void (*AudioOutput)(uint16_t left, uint16_t right) = (void (*)(uint16_t, uint16_t))DefaultAudio;

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* Definitions for MMC/SDC command */
#define CMD0   (0)			/* GO_IDLE_STATE */
#define CMD1   (1)			/* SEND_OP_COND */
#define ACMD41 (41|0x80)	/* SEND_OP_COND (SDC) */
#define CMD6   (6)			/* SEND_IF_COND */
#define CMD8   (8)			/* SEND_IF_COND */
#define CMD9   (9)			/* SEND_CSD */
#define CMD10  (10)			/* SEND_CID */
#define CMD12  (12)			/* STOP_TRANSMISSION */
#define ACMD13 (13|0x80)	/* SD_STATUS (SDC) */
#define CMD16  (16)			/* SET_BLOCKLEN */
#define CMD17  (17)			/* READ_SINGLE_BLOCK */
#define CMD18  (18)			/* READ_MULTIPLE_BLOCK */
#define CMD23  (23)			/* SET_BLOCK_COUNT */
#define ACMD23 (23|0x80)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24  (24)			/* WRITE_BLOCK */
#define CMD25  (25)			/* WRITE_MULTIPLE_BLOCK */
#define CMD41  (41)			/* SEND_OP_COND (ACMD) */
#define CMD55  (55)			/* APP_CMD */
#define CMD58  (58)			/* READ_OCR */
unsigned char CRC7(const unsigned char message[], const unsigned int length) {
  const unsigned char poly = 0b10001001;
  unsigned char crc = 0;
  for (unsigned i = 0; i < length; i++) {
     crc ^= message[i];
     for (int j = 0; j < 8; j++) {
      crc = (crc & 0x80u) ? ((crc << 1) ^ (poly << 1)) : (crc << 1);
    }
  }
  return crc >> 1;
}

static
UINT CardType;
BYTE MDD_SDSPI_CardDetectState(void){
            return 1;
}
BYTE MDD_SDSPI_WriteProtectState(void)
{
	return 0;
}
int __not_in_flash_func(getsound)(int i,int mode){
	int j=0,phase;
	if(mode % 2){
		phase=(int)sound_PhaseAC_right[i];
		if(sound_mode_right[i][0]==97){j = 2000; mode+=6;}
		else if(sound_mode_right[i][0]==99){j = (phase > 2047 ? 3900 : 100); mode+=4;}
		else if(mode==1 && sound_mode_right[i][0]==98){
			mode+=4;
			j=phase*3800/4096+100;
		}
	} else {
		phase=(int)sound_PhaseAC_left[i];
		if(sound_mode_left[i][0]==97){j = 2000; mode+=6;}
		else if(sound_mode_left[i][0]==99){j = (phase > 2047 ? 3900 : 100); mode+=4;}
		else if(sound_mode_left[i][0]==98){
			mode+=4;
			j=phase*3800/4096+100;
		}
	}
	switch(mode){
		case 0:
			j = (int)sound_mode_left[i][phase];
		case 4:
			return (j-2000)*mapping[sound_v_left[i]]/2000;
		case 1:
			j = (int)sound_mode_right[i][phase];
		case 5:
			return (j-2000)*mapping[sound_v_right[i]]/2000;
		case 2:
			return (int)sound_mode_left[i][phase];
		case 3:
			return (int)sound_mode_right[i][phase];
		case 6:
		case 7:
		    return j;
	}
	return 0;
}
#define sdi_send_buffer_local(a,b) sdi_send_buffer(a,b)
#define sendcount 64
#define sendstream 32
extern PIO pioi2s;
extern uint8_t i2ssm;
void MIPS16 __not_in_flash_func(on_pwm_wrap)(void) {
	static int noisedwellleft[MAXSOUNDS]={0}, noisedwellright[MAXSOUNDS]={0};
	static uint32_t noiseleft[MAXSOUNDS]={0}, noiseright[MAXSOUNDS]={0};
	static int repeatcount=1;
    // play a tone
#ifndef PICOMITEWEB
	__dsb();
#endif
    pwm_clear_irq(AUDIO_SLICE);
	if(Option.audio_i2s_bclk){
		if((pioi2s->flevel & (0xf<<(i2ssm*8))) > (0x6<<(i2ssm*8)))return;
		static int32_t left=0, right=0;
		if(CurrentlyPlaying == P_TONE){
			if(!SoundPlay){
				StopAudio();
				WAVcomplete = true;
			} else {
				while((pioi2s->flevel & (0xf<<(i2ssm*8))) < (0x6<<(i2ssm*8))){
					SoundPlay--;
					if(mono){
						left=(((((SineTable[(int)PhaseAC_left]-2000)  * mapping[vol_left])))*512);
						PhaseAC_left = PhaseAC_left + PhaseM_left;
						PhaseAC_right=PhaseAC_left;
						if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
						right=left;
					} else {
						left=(((((SineTable[(int)PhaseAC_left]-2000)  * mapping[vol_left])))*512);
						right=(((((SineTable[(int)PhaseAC_right]-2000)  * mapping[vol_right])))*512);
						PhaseAC_left = PhaseAC_left + PhaseM_left;
						PhaseAC_right = PhaseAC_right + PhaseM_right;
						if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
						if(PhaseAC_right>=4096.0)PhaseAC_right-=4096.0;
					}
					pio_sm_put_blocking(pioi2s, i2ssm, left);
					pio_sm_put_blocking(pioi2s, i2ssm, right);
				}
			}
			return;
		} else if(CurrentlyPlaying == P_WAV  || CurrentlyPlaying == P_FLAC  || CurrentlyPlaying == P_MOD  || CurrentlyPlaying == P_MP3  || CurrentlyPlaying == P_ARRAY) {
			while((pioi2s->flevel & (0xf<<(i2ssm*8))) < (0x6<<(i2ssm*8))){
				if(--repeatcount){
					pio_sm_put(pioi2s, i2ssm, left);
					pio_sm_put(pioi2s, i2ssm, right);
				} else {
					repeatcount=audiorepeat;
					if(bcount[1]==0 && bcount[2]==0 && playreadcomplete==1){
						pwm_set_irq_enabled(AUDIO_SLICE, false);
					}
					if(swingbuf){ //buffer is primed
						if(swingbuf==1)uplaybuff=g_buff1;
						else uplaybuff=g_buff2;
						if((CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MP3) && mono){
							left=right=(uplaybuff[ppos]<<16);
							ppos++;
						} else {
							if(ppos<bcount[swingbuf]){
								left=uplaybuff[ppos]<<16;
								right=uplaybuff[ppos+1]<<16;
								ppos+=2;
							}
						}
						pio_sm_put(pioi2s, i2ssm, (uint32_t)(left));
						pio_sm_put(pioi2s, i2ssm, (uint32_t)(right));
						if(ppos==bcount[swingbuf]){
							int psave=ppos;
							bcount[swingbuf]=0;
							ppos=0;
							if(swingbuf==1)swingbuf=2;
							else swingbuf=1;
							if(bcount[swingbuf]==0 && !playreadcomplete){ //nothing ready yet so flip back
								if(swingbuf==1){
									swingbuf=2;
									nextbuf=1;
								}
								else {
									swingbuf=1;
									nextbuf=2;
								}
								bcount[swingbuf]=psave;
								ppos=0;
							}
						}
					}
				}
			}
			return;
		} else if(CurrentlyPlaying == P_SOUND) {
			while((pioi2s->flevel & (0xf<<(i2ssm*8))) < (0x6<<(i2ssm*8))){
				int i,j;
				int leftv=0, rightv=0;
				for(i=0;i<MAXSOUNDS;i++){ //first update the 8 sound pointers
					if(sound_mode_left[i]!=nulltable){
						if(sound_mode_left[i]!=whitenoise){
							sound_PhaseAC_left[i] = sound_PhaseAC_left[i] + sound_PhaseM_left[i];
							if(sound_PhaseAC_left[i]>=4096.0)sound_PhaseAC_left[i]-=4096.0;
							leftv+=getsound(i,0);
						} else {
							if(noisedwellleft[i]<=0){
								noisedwellleft[i]=sound_PhaseM_left[i];
								noiseleft[i]=rand() % 3800+100;
							}
							if(noisedwellleft[i])noisedwellleft[i]--;
							j = (int)noiseleft[i];
							j= (j-2000)*mapping[sound_v_left[i]]/2000;
							leftv+=j;
						}
					}
					if(sound_mode_right[i]!=nulltable){
						if(sound_mode_right[i]!=whitenoise){
							sound_PhaseAC_right[i] = sound_PhaseAC_right[i] + sound_PhaseM_right[i];
							if(sound_PhaseAC_right[i]>=4096.0)sound_PhaseAC_right[i]-=4096.0;
							rightv += getsound(i,1);
						}  else {
							if(noisedwellright[i]<=0){
								noisedwellright[i]=sound_PhaseM_right[i];
								noiseright[i]=rand() % 3800+100;
							}
							if(noisedwellright[i])noisedwellright[i]--;
							j = (int)noiseright[i];
							j= (j-2000)*mapping[sound_v_right[i]]/2000;
							rightv+=j;
						}
					}
				}
				pio_sm_put_blocking(pioi2s, i2ssm,leftv*2000*512);
				pio_sm_put_blocking(pioi2s, i2ssm,rightv*2000*512);
			}
			return;
		} else if(CurrentlyPlaying == P_STOP) {
			while((pioi2s->flevel & (0xf<<(i2ssm*8))) < (0x6<<(i2ssm*8))){
					pio_sm_put(pioi2s, i2ssm, left);
					pio_sm_put(pioi2s, i2ssm, right);
			}
			return;
		} else {
			while((pioi2s->flevel & (0xf<<(i2ssm*8))) < (0x6<<(i2ssm*8))){
					pio_sm_put(pioi2s, i2ssm, left);
					pio_sm_put(pioi2s, i2ssm, right);
			}
			return;
		}
 	}
	if(Option.AUDIO_MISO_PIN){
		int32_t left=0, right=0;
		if(!(gpio_get(PinDef[Option.AUDIO_DREQ_PIN].GPno)))return;
		if(!(CurrentlyPlaying == P_TONE || CurrentlyPlaying == P_SOUND)){
			VSbuffer=VS1053free();
			if(VSbuffer>1023-(CurrentlyPlaying == P_STREAM ? sendstream :sendcount))return;
		}
    	if(CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV ||CurrentlyPlaying == P_MP3 || CurrentlyPlaying == P_MIDI || CurrentlyPlaying==P_ARRAY || CurrentlyPlaying==P_MOD) {
			if(bcount[1]==0 && bcount[2]==0 && playreadcomplete==1){
//				pwm_set_irq_enabled(AUDIO_SLICE, false);
				return;
			}
			if(swingbuf){ //buffer is primed
				int sendlen=((bcount[swingbuf]-ppos)>=sendcount ? sendcount : bcount[swingbuf]-ppos);
				if(swingbuf==1)sdi_send_buffer_local((uint8_t *)&sbuff1[ppos],sendlen);
				else sdi_send_buffer_local((uint8_t *)&sbuff2[ppos],sendlen);
				ppos+=sendlen;
				if(ppos==bcount[swingbuf]){
					bcount[swingbuf]=0;
					ppos=0;
					if(swingbuf==1)swingbuf=2;
					else swingbuf=1;
				}
			}
		} else if(CurrentlyPlaying == P_STREAM){ 
			int rp=*streamreadpointer,wp=*streamwritepointer;
			if(rp==wp)return;
			int i = wp - rp;
			if(i < 0) i += streamsize;
			if(i>sendstream){
				if(streamsize-rp>sendcount){
					sdi_send_buffer((uint8_t *)&streambuffer[rp],sendstream);
					rp+=sendstream;
				} else {
					char buff[sendstream];
					int j=0;
					while(j<sendstream){
						buff[j++]=streambuffer[rp];
						rp = (rp + 1) % streamsize;
					}
					sdi_send_buffer((uint8_t *)buff,sendstream);
				}
			}
			*streamreadpointer=rp;
		} else if(CurrentlyPlaying == P_SOUND) {
			int i,j;
			int leftv=0, rightv=0, Lcount=0, Rcount=0;
			for(i=0;i<MAXSOUNDS;i++){ //first update the 8 sound pointers
//					if(sound_mode_left[i]!=nulltable){
						Lcount++;
						if(sound_mode_left[i]!=whitenoise){
							sound_PhaseAC_left[i] = sound_PhaseAC_left[i] + sound_PhaseM_left[i];
							if(sound_PhaseAC_left[i]>=4096.0)sound_PhaseAC_left[i]-=4096.0;
							leftv+=getsound(i,2);
						} else {
							if(noisedwellleft[i]<=0){
								noisedwellleft[i]=sound_PhaseM_left[i];
								noiseleft[i]=rand() % 3800+100;
							}
							if(noisedwellleft[i])noisedwellleft[i]--;
							j = (int)noiseleft[i];
							leftv+=j;
						}
//					}
//					if(sound_mode_right[i]!=nulltable){
						Rcount++;
						if(sound_mode_right[i]!=whitenoise){
							sound_PhaseAC_right[i] = sound_PhaseAC_right[i] + sound_PhaseM_right[i];
							if(sound_PhaseAC_right[i]>=4096.0)sound_PhaseAC_right[i]-=4096.0;
							rightv += getsound(i,3);
						}  else {
							if(noisedwellright[i]<=0){
								noisedwellright[i]=sound_PhaseM_right[i];
								noiseright[i]=rand() % 3800+100;
							}
							if(noisedwellright[i])noisedwellright[i]--;
							j = (int)noiseright[i];
							rightv+=j;
						}
					}
//			}
			left=((leftv/Lcount)-2000)*16;
			right=((rightv/Rcount)-2000)*16;
			sdi_send_buffer((uint8_t *)&left,2);
			sdi_send_buffer((uint8_t *)&right,2);
		} else if(CurrentlyPlaying == P_TONE){
			if(!SoundPlay){
				StopAudio();
				WAVcomplete = true;
			} else {
				SoundPlay--;
				if(mono){
					left=((((int)SineTable[(int)PhaseAC_left])-2000)  * 16 );
					PhaseAC_left = PhaseAC_left + PhaseM_left;
					if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
					right=left;
				} else {
					left=(((SineTable[(int)PhaseAC_left])-2000) * 16);
					right=(((SineTable[(int)PhaseAC_right])-2000) * 16);
					PhaseAC_left = PhaseAC_left + PhaseM_left;
					PhaseAC_right = PhaseAC_right + PhaseM_right;
					if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
					if(PhaseAC_right>=4096.0)PhaseAC_right-=4096.0;
				}
				sdi_send_buffer((uint8_t *)&left,2);
				sdi_send_buffer((uint8_t *)&right,2);
			}
		}
	} else {
		if(CurrentlyPlaying == P_TONE){
			if(!SoundPlay){
				StopAudio();
				WAVcomplete = true;
				return;
			} else {
				SoundPlay--;
				if(mono){
					left=(((((SineTable[(int)PhaseAC_left]-2000)  * mapping[vol_left]) / 2000)+2000));
					PhaseAC_left = PhaseAC_left + PhaseM_left;
					PhaseAC_right=PhaseAC_left;
					if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
					right=left;
				} else {
					left=(((((SineTable[(int)PhaseAC_left]-2000)  * mapping[vol_left]) / 2000)+2000));
					right=(((((SineTable[(int)PhaseAC_right]-2000)  * mapping[vol_right]) / 2000)+2000));
					PhaseAC_left = PhaseAC_left + PhaseM_left;
					PhaseAC_right = PhaseAC_right + PhaseM_right;
					if(PhaseAC_left>=4096.0)PhaseAC_left-=4096.0;
					if(PhaseAC_right>=4096.0)PhaseAC_right-=4096.0;
				}
			}
		} else if(CurrentlyPlaying == P_WAV  || CurrentlyPlaying == P_FLAC  || CurrentlyPlaying == P_MOD   || CurrentlyPlaying==P_ARRAY || CurrentlyPlaying == P_MP3) {
			if(--repeatcount)return;
			repeatcount=audiorepeat;
			if(bcount[1]==0 && bcount[2]==0 && playreadcomplete==1){
				pwm_set_irq_enabled(AUDIO_SLICE, false);
				return;
			}
			if(swingbuf){ //buffer is primed
				if(swingbuf==1)playbuff=(uint16_t *)sbuff1;
				else playbuff=(uint16_t *)sbuff2;
				if((CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MP3) && mono){
					left=right=playbuff[ppos];
					ppos++;
				} else {
					if(ppos<bcount[swingbuf]){
						left=playbuff[ppos];
						right=playbuff[ppos+1];
						ppos+=2;
					}
				}
				if(ppos==bcount[swingbuf]){
					int psave=ppos;
					bcount[swingbuf]=0;
					ppos=0;
					if(swingbuf==1)swingbuf=2;
					else swingbuf=1;
					if(bcount[swingbuf]==0 && !playreadcomplete){ //nothing ready yet so flip back
						if(swingbuf==1){
							swingbuf=2;
							nextbuf=1;
						}
						else {
							swingbuf=1;
							nextbuf=2;
						}
						bcount[swingbuf]=psave;
						ppos=0;
					}
				}
			}
		} else if(CurrentlyPlaying == P_SOUND) {
			int i,j;
			int leftv=0, rightv=0;
			for(i=0;i<MAXSOUNDS;i++){ //first update the 8 sound pointers
					if(sound_mode_left[i]!=nulltable){
						if(sound_mode_left[i]!=whitenoise){
							sound_PhaseAC_left[i] = sound_PhaseAC_left[i] + sound_PhaseM_left[i];
							if(sound_PhaseAC_left[i]>=4096.0)sound_PhaseAC_left[i]-=4096.0;
							leftv+=getsound(i,0);
						} else {
							if(noisedwellleft[i]<=0){
								noisedwellleft[i]=sound_PhaseM_left[i];
								noiseleft[i]=rand() % 3800+100;
							}
							if(noisedwellleft[i])noisedwellleft[i]--;
							j = (int)noiseleft[i];
							j= (j-2000)*mapping[sound_v_left[i]]/2000;
							leftv+=j;
						}
					}
					if(sound_mode_right[i]!=nulltable){
						if(sound_mode_right[i]!=whitenoise){
							sound_PhaseAC_right[i] = sound_PhaseAC_right[i] + sound_PhaseM_right[i];
							if(sound_PhaseAC_right[i]>=4096.0)sound_PhaseAC_right[i]-=4096.0;
							rightv += getsound(i,1);
						}  else {
							if(noisedwellright[i]<=0){
								noisedwellright[i]=sound_PhaseM_right[i];
								noiseright[i]=rand() % 3800+100;
							}
							if(noisedwellright[i])noisedwellright[i]--;
							j = (int)noiseright[i];
							j= (j-2000)*mapping[sound_v_right[i]]/2000;
							rightv+=j;
						}
					}
			}
			left=leftv+2000;
			right=rightv+2000;
		} else if(CurrentlyPlaying <= P_STOP ) {
			return;
		} else {
			if(Option.AUDIO_MISO_PIN)return;
			left=right=2000;
		}
	AudioOutput(left,right);
	}
}

void BitBangSendSPI(const BYTE *buff, int cnt){
	int i, SPICount;
	BYTE SPIData;
    if(SD_SPI_SPEED==SD_SLOW_SPI_SPEED){
    	for(i=0;i<cnt;i++){
    		SPIData=buff[i];
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock out the Address byte
    		{
    			if (SPIData & 0x80)                                 // Check for a 1
    				gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
    			else
    				gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
    			busy_wait_us_32(20);
    			gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
    			busy_wait_us_32(20);
    			gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
    			SPIData <<= 1;                                      // Rotate to get the next bit
    		}  // and loop back to send the next bit
    	}
    } else if(Option.CPU_Speed<=slow_clock){
    	for(i=0;i<cnt;i++){
    		SPIData=buff[i];
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock out the Address byte
    		{
    			if (SPIData & 0x80) gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
    			else gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
				asm("NOP");
    			gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
    			SPIData <<= 1;                                      // Rotate to get the next bit
    			gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
    		}  // and loop back to send the next bit
    	}
	} else {
    	for(i=0;i<cnt;i++){
    		SPIData=buff[i];
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock out the Address byte
    		{
    			if (SPIData & 0x80) gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
    			else gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
				asm("NOP");asm("NOP");asm("NOP");
    			gpio_put(SD_CLK_PIN, GPIO_PIN_SET);
    			SPIData <<= 1;   asm("NOP");                                   // Rotate to get the next bit
    			gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
    		}  // and loop back to send the next bit
    	}
	}
}
void BitBangReadSPI(BYTE *buff, int cnt){
	int i, SPICount;
	BYTE SPIData;
	gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
    if(SD_SPI_SPEED==SD_SLOW_SPI_SPEED){
    	for(i=0;i<cnt;i++){
    		SPIData = 0;
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
    		{
    			SPIData <<=1;                                       // Rotate the data
    			gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
    			busy_wait_us_32(20);
    			SPIData += gpio_get(SD_MISO_PIN);                       // Read the data bit
    			gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
    			busy_wait_us_32(20);
    		}                                                     // and loop back
    		buff[i]=SPIData;
    	}
    } else if(Option.CPU_Speed<=slow_clock){
    	for(i=0;i<cnt;i++){
    		SPIData = 0;
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
    		{
    			SPIData <<=1;                                       // Rotate the data
    			gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
				asm("NOP");
    			SPIData += gpio_get(SD_MISO_PIN);                       // Read the data bit
   				gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
				asm("NOP");
     		}                                                     // and loop back
    		buff[i]=SPIData;
    	}
    } else {
    	for(i=0;i<cnt;i++){
    		SPIData = 0;
    		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
    		{
    			SPIData <<=1;                                       // Rotate the data
    			gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
				asm("NOP");asm("NOP");asm("NOP");
    			SPIData += gpio_get(SD_MISO_PIN);                       // Read the data bit
   				gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
				asm("NOP");asm("NOP");asm("NOP");
     		}                                                     // and loop back
    		buff[i]=SPIData;
    	}
    }
}

BYTE BitBangSwapSPI(BYTE data_out){
	BYTE data_in=0;
	int SPICount;
	if(SD_SPI_SPEED==SD_SLOW_SPI_SPEED){
		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
		{
			if (data_out & 0x80)                                 // Check for a 1
				gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
			else
				gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
	    	busy_wait_us_32(20);
	    	data_in <<=1;                                       // Rotate the data
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
	    	busy_wait_us_32(20);
	    	data_in += gpio_get(SD_MISO_PIN);                       // Read the data bit
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
	    	data_out <<= 1;
		}                                                     // and loop back
    } else if(Option.CPU_Speed<=slow_clock){
		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
		{
			if (data_out & 0x80)                                 // Check for a 1
				gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
			else
				gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
			asm("NOP");
	    	data_in <<=1;                                       // Rotate the data
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
	    	data_out <<= 1;
	    	data_in += gpio_get(SD_MISO_PIN);                       // Read the data bit
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
		}                                                     // and loop back
	} else {
		for (SPICount = 0; SPICount < 8; SPICount++)          // Prepare to clock in the data to be fread
		{
			if (data_out & 0x80)                                 // Check for a 1
				gpio_put(SD_MOSI_PIN,GPIO_PIN_SET);
			else
				gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
			asm("NOP");asm("NOP");
	    	data_in <<=1;                                       // Rotate the data
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_SET);
	    	data_out <<= 1;asm("NOP");
	    	data_in += gpio_get(SD_MISO_PIN);                       // Read the data bit
	    	gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
		}                                                     // and loop back
	}
	return data_in;
}
int MIPS16 BitBangSetClk(int speed, int polarity, int edge){
	gpio_init(SD_CLK_PIN);
	gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
	gpio_set_dir(SD_CLK_PIN, GPIO_OUT);
	gpio_init(SD_MOSI_PIN);
	gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
	gpio_set_dir(SD_MOSI_PIN, GPIO_OUT);
	gpio_init(SD_MISO_PIN);
	gpio_set_pulls(SD_MISO_PIN,true,false);
	gpio_set_dir(SD_MISO_PIN, GPIO_IN);
	gpio_set_drive_strength(SD_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_drive_strength(SD_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_input_hysteresis_enabled(SD_MISO_PIN,true);
	SD_SPI_SPEED=speed;
	return speed;
}
BYTE __not_in_flash_func(HW0SwapSPI)(BYTE data_out){
	BYTE data_in=0;
	spi_write_read_blocking(spi0,&data_out,&data_in,1);
	return data_in;
}
void __not_in_flash_func(HW0SendSPI)(const BYTE *buff, int cnt){
	spi_write_blocking(spi0,buff,cnt);
}
void __not_in_flash_func(HW0ReadSPI)(BYTE *buff, int cnt){
	spi_read_blocking(spi0,0xff,buff,cnt);

}
int HW0Clk(int speed, int polarity, int edge){
	xchg_byte= HW0SwapSPI;
	xmit_byte_multi=HW0SendSPI;
	rcvr_byte_multi=HW0ReadSPI;
	gpio_set_function(SPI_CLK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SPI_MISO_PIN, GPIO_FUNC_SPI);
	spi_init(spi0, speed);
	spi_set_format(spi0, 8, polarity,edge, SPI_MSB_FIRST);
	gpio_set_drive_strength(SPI_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_drive_strength(SPI_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_input_hysteresis_enabled(SPI_MISO_PIN,true);
	return spi_get_baudrate(spi0);
}
BYTE __not_in_flash_func(HW1SwapSPI)(BYTE data_out){
	BYTE data_in=0;
	spi_write_read_blocking(spi1,&data_out,&data_in,1);
	return data_in;
}
void __not_in_flash_func(HW1SendSPI)(const BYTE *buff, int cnt){
	spi_write_blocking(spi1,buff,cnt);
}
void __not_in_flash_func(HW1ReadSPI)(BYTE *buff, int cnt){
	spi_read_blocking(spi1,0xff,buff,cnt);
}
int HW1Clk(int speed, int polarity, int edge){
	xchg_byte= HW1SwapSPI;
	xmit_byte_multi=HW1SendSPI;
	rcvr_byte_multi=HW1ReadSPI;
	gpio_set_function(SPI_CLK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SPI_MISO_PIN, GPIO_FUNC_SPI);
	spi_init(spi1, speed);
	spi_set_format(spi1, 8, polarity,edge, SPI_MSB_FIRST);
	gpio_set_drive_strength(SPI_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_drive_strength(SPI_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
	gpio_set_input_hysteresis_enabled(SPI_MISO_PIN,true);
	return spi_get_baudrate(spi1);
}


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (void)
{
	BYTE d;

	Timer2 = 500;	/* Wait for ready in timeout of 500ms */
	do {
		d = xchg_byte(0xFF);
        busy_wait_us_32(5);
	} while ((d != 0xFF) && Timer2);

	return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/

static void __not_in_flash_func(deselect)(void)
{
#ifndef PICOMITEVGA
	if(Option.CombinedCS){
		gpio_set_dir(TOUCH_CS_PIN, GPIO_IN);
	} else {
#endif
		gpio_put(SD_CS_PIN,GPIO_PIN_SET);
#ifndef PICOMITEVGA
	}
	nop;nop;nop;nop;nop;
#endif
	xchg_byte(0xFF);		/* Dummy clock (force DO hi-z for multiple slave SPI) */
}



/*-----------------------------------------------------------------------*/
/* Select the card and wait ready                                        */
/*-----------------------------------------------------------------------*/


static
int __not_in_flash_func(selectSD)(void)	/* 1:Successful, 0:Timeout */
{
	if(SD_SPI_SPEED==SD_SLOW_SPI_SPEED)	SPISpeedSet(SDSLOW);
	else SPISpeedSet(SDFAST);
#ifndef PICOMITEVGA
	if(Option.CombinedCS){
		gpio_put(TOUCH_CS_PIN,GPIO_PIN_RESET);
		gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);
	} else gpio_put(SD_CS_PIN,GPIO_PIN_RESET);
#else
	gpio_put(SD_CS_PIN,GPIO_PIN_RESET);
#endif
    busy_wait_us_32(5);
    xchg_byte(0xFF);		/* Dummy clock (force DO enabled) */

	if (wait_ready()) return 1;	/* Wait for card ready */

	deselect();
	return 0;	/* Timeout */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/


static
int __not_in_flash_func(rcvr_datablock)(	/* 1:OK, 0:Failed */
	BYTE *buff,			/* Data buffer to store received data */
	UINT btr			/* Byte count (must be multiple of 4) */
)
{
	BYTE token;


	Timer1 = 100;
	do {							/* Wait for data packet in timeout of 100ms */
		token = xchg_byte(0xFF);
	} while ((token == 0xFF) && Timer1);

	if(token != 0xFE) return 0;		/* If not valid data token, retutn with error */

	rcvr_byte_multi(buff, btr);		/* Receive the data block into buffer */
	xchg_byte(0xFF);					/* Discard CRC */
	xchg_byte(0xFF);
	diskcheckrate=1; //successful read so reset check
	return 1;						/* Return with success */
}


/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/



static
int xmit_datablock(	/* 1:OK, 0:Failed */
	const BYTE *buff,	/* 512 byte data block to be transmitted */
	BYTE token			/* Data token */
)
{
	BYTE resp;


	if (!wait_ready()) return 0;

	xchg_byte(token);		/* Xmit a token */
	if (token != 0xFD) {	/* Not StopTran token */
		xmit_byte_multi(buff, 512);	/* Xmit the data block to the MMC */
		xchg_byte(0xFF);				/* CRC (Dummy) */
		xchg_byte(0xFF);
		resp = xchg_byte(0xFF);		/* Receive a data response */
		if ((resp & 0x1F) != 0x05)	/* If not accepted, return with error */
			return 0;
	}
	diskcheckrate=1; //successful write so reset check

	return 1;
}


/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/
BYTE __not_in_flash_func(send_cmd)(
	BYTE cmd,		/* Command byte */
	DWORD arg		/* Argument */
)
{
	BYTE n, res;

	if (cmd & 0x80) {	/* ACMD<n> is the command sequense of CMD55-CMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12) {
		deselect();
		if (!selectSD()){
            return 0xFF;
        }
	}
	/* Send command packet */
		BYTE command[6];
		command[0]=(0x40 | cmd);			// Start + Command index
		command[1]=(arg >> 24);	// Argument[31..24]
		command[2]=(arg >> 16);	// Argument[23..16]
		command[3]=(arg >> 8);		// Argument[15..8]
		command[4]=(arg);			// Argument[7..0]
		command[5]=(CRC7(command, 5)<<1) | 1;
/*		xchg_byte(command[0]);			// Start + Command index 
		xchg_byte(command[1]);	// Argument[31..24] 
		xchg_byte(command[2]);	// Argument[23..16] 
		xchg_byte(command[3]);		// Argument[15..8] 
		xchg_byte(command[4]);			// Argument[7..0] 
		xchg_byte(command[5]);*/
		xmit_byte_multi(command,6);
	/* Receive command response */
	if (cmd == CMD12) xchg_byte(0xFF);	/* Skip a stuff byte on stop to read */
	n = 100;							/* Wait for a valid response in timeout of 10 attempts */
	do
		res = xchg_byte(0xFF);
	while ((res & 0x80) && --n);

	return res;			/* Return with the response value */
}



/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber (0) */
)
{
	if (pdrv != 0) return STA_NOINIT;	/* Supports only single drive */

	return SDCardStat;
}

/*int CMD0send(void){
    char response,trys=100, responsetrys=10;
    do {
    	deselect();
		gpio_put(SD_CS_PIN,GPIO_PIN_RESET);
		asm("NOP");asm("NOP");asm("NOP");
        trys--;
        xchg_byte(0x40);
        xchg_byte(0x0);
        xchg_byte(0x0);
        xchg_byte(0x0);
        xchg_byte(0x0);
        xchg_byte(0x95);
        do{
            response=xchg_byte(0xFF);
            responsetrys--;
        } while((responsetrys !=0) && (response !=1));
    } while((trys !=0) && (response !=1));
    return response;
}*/

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv		/* Physical drive nmuber (0) */
)
{
	BYTE n, cmd, ty, ocr[4];


	if (pdrv != 0) return STA_NOINIT;	/* Supports only single drive */
//	if (SDCardStat & STA_NODISK) return SDCardStat;	/* No card in the socket */
	SD_SPI_SPEED=SD_SLOW_SPI_SPEED;
	SPISpeedSet(SDSLOW);
	deselect();							/* Initialize memory card interface */
	for (n = 10; n; n--) xchg_byte(0xFF);	/* 80 dummy clocks */
	ty = 0;

	if (send_cmd(CMD0, 0) == 1) {			/* Enter Idle state */
		Timer1 = 1000;						/* Initialization timeout of 1000 msec */
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDv2? */
//			MMPrintString("sdv2\r\n");
			for (n = 0; n < 4; n++) ocr[n] = xchg_byte(0xFF);			/* Get trailing return value of R7 resp */

			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {				/* The card can work at vdd range of 2.7-3.6V */
				while (Timer1 && send_cmd(ACMD41, 0x40000000));	/* Wait for leaving idle state (ACMD41 with HCS bit) */
				if (Timer1 && send_cmd(CMD58, 0) == 0) {			/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = xchg_byte(0xFF);
					ty = (ocr[0] & 0x40) ? CT_SD2|CT_BLOCK : CT_SD2;	/* SDv2 */
				}
			}
		} else {							/* SDv1 or MMCv3 */
//			MMPrintString("sdv1\r\n");
			if (send_cmd(ACMD41, 0) <= 1) 	{
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 */
			}
			while (Timer1 && send_cmd(cmd, 0));		/* Wait for leaving idle state */
			if (!Timer1 || send_cmd(CMD16, 512) != 0)	/* Set read/write block length to 512 */
				ty = 0;
		}
	}
	CardType = ty;
	deselect();
	if (ty) {		/* Function succeded */
		SDCardStat &= ~STA_NOINIT;	/* Clear STA_NOINIT */
		SD_SPI_SPEED=Option.SDspeed*1000000;
		SPISpeedSet(SDFAST);
//		SET_SPI_CLK(SD_SPI_SPEED, false, false);
	}

	return SDCardStat;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT __not_in_flash_func(disk_read)(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv || !count) return RES_PARERR;
	if (SDCardStat & STA_NOINIT) return RES_NOTRDY;

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert to byte address if needed */

	if (count == 1) {		/* Single block read */
		if ((send_cmd(CMD17, sector) == 0)	/* READ_SINGLE_BLOCK */
			&& rcvr_datablock(buff, 512))
			count = 0;
	}
	else {				/* Multiple block read */
		if (send_cmd(CMD18, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (!rcvr_datablock(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/


DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv || !count) return RES_PARERR;
	if (SDCardStat & STA_NOINIT) return RES_NOTRDY;
	if (SDCardStat & STA_PROTECT) return RES_WRPRT;

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert to byte address if needed */

	if (count == 1) {		/* Single block write */
		if ((send_cmd(CMD24, sector) == 0)	/* WRITE_BLOCK */
			&& xmit_datablock(buff, 0xFE))
			count = 0;
	}
	else {				/* Multiple block write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);
		if (send_cmd(CMD25, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!xmit_datablock(buff, 0xFC)) break;
				buff += 512;
			} while (--count);
			if (!xmit_datablock(0, 0xFD))	/* STOP_TRAN token */
				count = 1;
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;
}




/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/


DRESULT disk_ioctl(
	BYTE pdrv,		/* Physical drive nmuber (0) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive data block */
)
{
	DRESULT res;
	BYTE n, csd[16], *ptr = buff;
	DWORD csz;

	if (pdrv) return RES_PARERR;
	if (SDCardStat & STA_NOINIT) return RES_NOTRDY;

	res = RES_ERROR;
	switch (cmd) {
	case CTRL_SYNC :	/* Flush write-back cache, Wait for end of internal process */
		if (selectSD()) res = RES_OK;
		break;

	case GET_SECTOR_COUNT :	/* Get number of sectors on the disk (WORD) */
		if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
			if ((csd[0] >> 6) == 1) {	/* SDv2? */
				csz = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
				*(DWORD*)buff = csz << 10;
			} else {					/* SDv1 or MMCv3 */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csz = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
				*(DWORD*)buff = csz << (n - 9);
			}
			res = RES_OK;
		}
		break;

	case GET_BLOCK_SIZE :	/* Get erase block size in unit of sectors (DWORD) */
		if (CardType & CT_SD2) {	/* SDv2? */
			if (send_cmd(ACMD13, 0) == 0) {		/* Read SD status */
				xchg_byte(0xFF);
				if (rcvr_datablock(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_byte(0xFF);	/* Purge trailing data */
					*(DWORD*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		} else {					/* SDv1 or MMCv3 */
			if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDv1 */
					*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				} else {					/* MMCv3 */
					*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case MMC_GET_TYPE :		/* Get card type flags (1 byte) */
		*ptr = CardType;
		res = RES_OK;
		break;

	case MMC_GET_CSD :	/* Receive CSD as a data block (16 bytes) */
		if ((send_cmd(CMD9, 0) == 0)	/* READ_CSD */
			&& rcvr_datablock(buff, 16))
			res = RES_OK;
		break;

	case MMC_GET_CID :	/* Receive CID as a data block (16 bytes) */
		if ((send_cmd(CMD10, 0) == 0)	/* READ_CID */
			&& rcvr_datablock(buff, 16))
			res = RES_OK;
		break;

	case MMC_GET_OCR :	/* Receive OCR as an R3 resp (4 bytes) */
		if (send_cmd(CMD58, 0) == 0) {	/* READ_OCR */
			for (n = 0; n < 4; n++)
				*((BYTE*)buff+n) = xchg_byte(0xFF);
			res = RES_OK;
		}
		break;

	case MMC_GET_SDSTAT :	/* Receive SD statsu as a data block (64 bytes) */
		if ((CardType & CT_SD2) && send_cmd(ACMD13, 0) == 0) {	/* SD_STATUS */
			xchg_byte(0xFF);
			if (rcvr_datablock(buff, 64))
				res = RES_OK;
		}
		break;

	case CTRL_POWER :	/* Power off */
		SDCardStat |= STA_NOINIT;
		res = RES_OK;
		break;

	default:
		res = RES_PARERR;
	}

	deselect();

	return res;
}




/*-----------------------------------------------------------------------*/
/* Device Timer Driven Procedure                                         */
/*-----------------------------------------------------------------------*/
/* This function must be called by timer interrupt in period of 1ms      */


DWORD get_fattime(void){
    DWORD i;
    int year, month, day, hour, minute, second;
    gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    i = ((year-1980) & 0x7F)<<25;
    i |= (month & 0xF)<<21;
    i |= (day & 0x1F)<<16;
    i |= (hour & 0x1F)<<11;
    i |= (minute & 0x3F)<<5;
    i |= (second/2 & 0x1F);
    return i;
}
int getslice(int pin){
	int slice=0;
	if(PinDef[pin].mode & PWM0A){PWM0Apin=pin;slice=0;}
	else if(PinDef[pin].mode & PWM0B){PWM0Bpin=pin;slice=0;}
	else if(PinDef[pin].mode & PWM1A){PWM1Apin=pin;slice=1;}
	else if(PinDef[pin].mode & PWM1B){PWM1Bpin=pin;slice=1;}
	else if(PinDef[pin].mode & PWM2A){PWM2Apin=pin;slice=2;}
	else if(PinDef[pin].mode & PWM2B){PWM2Bpin=pin;slice=2;}
	else if(PinDef[pin].mode & PWM3A){PWM3Apin=pin;slice=3;}
	else if(PinDef[pin].mode & PWM3B){PWM3Bpin=pin;slice=3;}
	else if(PinDef[pin].mode & PWM4A){PWM4Apin=pin;slice=4;}
	else if(PinDef[pin].mode & PWM4B){PWM4Bpin=pin;slice=4;}
	else if(PinDef[pin].mode & PWM5A){PWM5Apin=pin;slice=5;}
	else if(PinDef[pin].mode & PWM5B){PWM5Bpin=pin;slice=5;}
	else if(PinDef[pin].mode & PWM6A){PWM6Apin=pin;slice=6;}
	else if(PinDef[pin].mode & PWM6B){PWM6Bpin=pin;slice=6;}
	else if(PinDef[pin].mode & PWM7A){PWM7Apin=pin;slice=7;}
	else if(PinDef[pin].mode & PWM7B){PWM7Bpin=pin;slice=7;}
#ifdef rp2350
	else if(PinDef[pin].mode & PWM8A){PWM8Apin=pin;slice=8;}
	else if(PinDef[pin].mode & PWM8B){PWM8Bpin=pin;slice=8;}
	else if(PinDef[pin].mode & PWM9A){PWM9Apin=pin;slice=9;}
	else if(PinDef[pin].mode & PWM9B){PWM9Bpin=pin;slice=9;}
	else if(PinDef[pin].mode & PWM10A){PWM10Apin=pin;slice=10;}
	else if(PinDef[pin].mode & PWM10B){PWM10Bpin=pin;slice=10;}
	else if(PinDef[pin].mode & PWM11A){PWM11Apin=pin;slice=11;}
	else if(PinDef[pin].mode & PWM11B){PWM11Bpin=pin;slice=11;}
#endif
	return slice;
}
void setpwm(int pin, int *PWMChannel, int *PWMSlice, MMFLOAT frequency, MMFLOAT duty){
	int slice=getslice(pin);
	gpio_init(PinDef[pin].GPno); 
	gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PWM);
	int wrap=(Option.CPU_Speed*1000)/frequency;
	int high=(int)((MMFLOAT)Option.CPU_Speed/frequency*duty*10.0);
	int div=1;
	while(wrap>65535){
		wrap>>=1;
		if(duty>=0.0)high>>=1;
		div<<=1;
	}
	wrap--;
	if(div!=1)pwm_set_clkdiv(slice,(float)div);
	pwm_set_wrap(slice, wrap);
	*PWMSlice=slice;
	if(slice==0 && PWM0Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==0 && PWM0Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==1 && PWM1Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==1 && PWM1Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==2 && PWM2Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==2 && PWM2Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==3 && PWM3Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==3 && PWM3Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==4 && PWM4Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==4 && PWM4Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==5 && PWM5Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==5 && PWM5Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==6 && PWM6Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==6 && PWM6Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	if(slice==7 && PWM7Apin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_A, high);
		*PWMChannel=PWM_CHAN_A;
	}
	if(slice==7 && PWM7Bpin!=99){
		pwm_set_chan_level(slice, PWM_CHAN_B, high);
		*PWMChannel=PWM_CHAN_B;
	}
	#ifdef rp2350
	if(!rp2350a){
		if(slice==8 && PWM8Apin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_A, high);
			*PWMChannel=PWM_CHAN_A;
		}
		if(slice==8 && PWM8Bpin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_B, high);
			*PWMChannel=PWM_CHAN_B;
		}
		if(slice==8 && PWM9Apin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_A, high);
			*PWMChannel=PWM_CHAN_A;
		}
		if(slice==9 && PWM9Bpin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_B, high);
			*PWMChannel=PWM_CHAN_B;
		}
		if(slice==10 && PWM10Apin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_A, high);
			*PWMChannel=PWM_CHAN_A;
		}
		if(slice==10 && PWM10Bpin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_B, high);
			*PWMChannel=PWM_CHAN_B;
		}
		if(slice==11 && PWM11Apin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_A, high);
			*PWMChannel=PWM_CHAN_A;
		}
		if(slice==11 && PWM11Bpin!=99){
			pwm_set_chan_level(slice, PWM_CHAN_B, high);
			*PWMChannel=PWM_CHAN_B;
		}
	}
	#endif

	if(slice==0){
		pwm_set_enabled(slice, true);
	}
	if(slice==1){
		pwm_set_enabled(slice, true);
	}
	if(slice==2){
		pwm_set_enabled(slice, true);
	}
	if(slice==3){
		pwm_set_enabled(slice, true);
	}
	if(slice==4){
		pwm_set_enabled(slice, true);
	}
	if(slice==5){
		pwm_set_enabled(slice, true);
	}
	if(slice==6){
		pwm_set_enabled(slice, true);
	}
	if(slice==7){
		pwm_set_enabled(slice, true);
	}
#ifdef rp2350
	if(!rp2350a){
		if(slice==8){
			pwm_set_enabled(slice, true);
		}
		if(slice==9){
			pwm_set_enabled(slice, true);
		}
		if(slice==10){
			pwm_set_enabled(slice, true);
		}
		if(slice==11){
			pwm_set_enabled(slice, true);
		}
	}
#endif

}
void dobacklight(void){
	if(Option.DISPLAY_BL){
		ExtCfg(Option.DISPLAY_BL, EXT_BOOT_RESERVED, 0);
		int pin=Option.DISPLAY_BL;
		setpwm(pin, &BacklightChannel, &BacklightSlice, Option.DISPLAY_TYPE==ILI9488W ? 1000.0 : 50000.0, Option.BackLightLevel);
	}
}
void InitReservedIO(void) {
#ifdef rp2350
	if(Option.PSRAM_CS_PIN){
		ExtCfg(Option.PSRAM_CS_PIN, EXT_BOOT_RESERVED, 0);
	}
#if defined(PICOMITE)
	if(Option.LOCAL_KEYBOARD){
		ExtCfg(PINMAP[47],EXT_ANA_IN,0);
		ExtCfg(PINMAP[44],EXT_DIG_OUT,0);
//		ExtCfg(PINMAP[45],EXT_DIG_OUT,0);
//		ExtCfg(PINMAP[46],EXT_DIG_OUT,0);
		setpwm(PINMAP[43], &KeyboardlightChannel, &KeyboardlightSlice, 50000.0, Option.KeyboardBrightness);

		for(int i=24;i<48;i++){
			if(i==25)continue;
			if(i<43){
				ExtCfg(PINMAP[i], EXT_DIG_IN, ODCSET);
			}
			if(!(i==45 || i==46))ExtCfg(PINMAP[i], EXT_BOOT_RESERVED, 0);
		}
	}
#endif
#endif
#ifdef PICOMITEVGA
#ifndef HDMI
	VGArecovery(0);
#endif
#else
	if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL){
		ExtCfg(SSD1963_DC_PIN, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_DC_GPPIN);gpio_put(SSD1963_DC_GPPIN,GPIO_PIN_SET);gpio_set_dir(SSD1963_DC_GPPIN, GPIO_OUT);
		if(Option.SSD_RESET!=-1){
			ExtCfg(SSD1963_RESET_PIN, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_RESET_GPPIN);gpio_put(SSD1963_RESET_GPPIN,GPIO_PIN_SET);gpio_set_dir(SSD1963_RESET_GPPIN, GPIO_OUT);
		}
		ExtCfg(SSD1963_WR_PIN, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_WR_GPPIN);gpio_put(SSD1963_WR_GPPIN,GPIO_PIN_SET);gpio_set_dir(SSD1963_WR_GPPIN, GPIO_OUT);
		ExtCfg(SSD1963_RD_PIN, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_RD_GPPIN);gpio_put(SSD1963_RD_GPPIN,GPIO_PIN_SET);gpio_set_dir(SSD1963_RD_GPPIN, GPIO_OUT);
		ExtCfg(SSD1963_DAT1, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT1);gpio_put(SSD1963_GPDAT1,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT1, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT1, true);
		ExtCfg(SSD1963_DAT2, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT2);gpio_put(SSD1963_GPDAT2,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT2, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT2, true);
		ExtCfg(SSD1963_DAT3, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT3);gpio_put(SSD1963_GPDAT3,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT3, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT3, true);
		ExtCfg(SSD1963_DAT4, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT4);gpio_put(SSD1963_GPDAT4,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT4, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT4, true);
		ExtCfg(SSD1963_DAT5, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT5);gpio_put(SSD1963_GPDAT5,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT5, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT5, true);
		ExtCfg(SSD1963_DAT6, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT6);gpio_put(SSD1963_GPDAT6,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT6, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT6, true);
		ExtCfg(SSD1963_DAT7, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT7);gpio_put(SSD1963_GPDAT7,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT7, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT7, true);
		ExtCfg(SSD1963_DAT8, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT8);gpio_put(SSD1963_GPDAT8,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT8, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT8, true);
        if(Option.DISPLAY_TYPE>SSD_PANEL_8){
			ExtCfg(SSD1963_DAT9, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT9);gpio_put(SSD1963_GPDAT9,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT9, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT9, true);
			ExtCfg(SSD1963_DAT10, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT10);gpio_put(SSD1963_GPDAT10,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT10, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT10, true);
			ExtCfg(SSD1963_DAT11, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT11);gpio_put(SSD1963_GPDAT11,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT11, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT11, true);
			ExtCfg(SSD1963_DAT12, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT12);gpio_put(SSD1963_GPDAT12,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT12, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT12, true);
			ExtCfg(SSD1963_DAT13, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT13);gpio_put(SSD1963_GPDAT13,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT13, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT13, true);
			ExtCfg(SSD1963_DAT14, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT14);gpio_put(SSD1963_GPDAT14,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT14, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT14, true);
			ExtCfg(SSD1963_DAT15, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT15);gpio_put(SSD1963_GPDAT15,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT15, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT15, true);
			ExtCfg(SSD1963_DAT16, EXT_BOOT_RESERVED, 0);gpio_init(SSD1963_GPDAT16);gpio_put(SSD1963_GPDAT16,GPIO_PIN_SET);gpio_set_dir(SSD1963_GPDAT16, GPIO_OUT);gpio_set_input_enabled(SSD1963_GPDAT16, true);
 		}
		dobacklight();
	}
	if(Option.LCD_CD){
		ExtCfg(Option.LCD_CD, EXT_BOOT_RESERVED, 0);
		if(!(Option.DISPLAY_TYPE==ST7920))ExtCfg(Option.LCD_CS, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.LCD_Reset, EXT_BOOT_RESERVED, 0);
		LCD_CD_PIN=PinDef[Option.LCD_CD].GPno;
		LCD_CS_PIN=PinDef[Option.LCD_CS].GPno;
		LCD_Reset_PIN=PinDef[Option.LCD_Reset].GPno;
		gpio_init(LCD_CD_PIN);
		gpio_put(LCD_CD_PIN,Option.DISPLAY_TYPE!=ST7920 ? GPIO_PIN_SET : GPIO_PIN_RESET);
		gpio_set_dir(LCD_CD_PIN, GPIO_OUT);
		gpio_init(LCD_CS_PIN);
		gpio_set_drive_strength(LCD_CS_PIN,GPIO_DRIVE_STRENGTH_8MA);
		if(!(Option.DISPLAY_TYPE==ST7920)){
			gpio_put(LCD_CS_PIN,GPIO_PIN_SET);
			gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
		}
		gpio_init(LCD_Reset_PIN);
		gpio_put(LCD_Reset_PIN,GPIO_PIN_RESET);
		gpio_set_dir(LCD_Reset_PIN, GPIO_OUT);
		CurrentSPISpeed=NONE_SPI_SPEED;
		dobacklight();
	}
	if(Option.TOUCH_CS || Option.TOUCH_IRQ){
		if(Option.TOUCH_CS){
			ExtCfg(Option.TOUCH_CS, EXT_BOOT_RESERVED, 0);
			TOUCH_CS_PIN=PinDef[Option.TOUCH_CS].GPno;
			gpio_init(TOUCH_CS_PIN);
			gpio_set_drive_strength(TOUCH_CS_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_set_slew_rate(TOUCH_CS_PIN, GPIO_SLEW_RATE_SLOW);
			gpio_put(TOUCH_CS_PIN,GPIO_PIN_SET);
			if(Option.CombinedCS)gpio_set_dir(TOUCH_CS_PIN, GPIO_IN);
			else gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);
		}
		ExtCfg(Option.TOUCH_IRQ, EXT_BOOT_RESERVED, 0);
		TOUCH_IRQ_PIN=PinDef[Option.TOUCH_IRQ].GPno;
		gpio_init(TOUCH_IRQ_PIN);
		gpio_pull_up(TOUCH_IRQ_PIN);
		gpio_set_dir(TOUCH_IRQ_PIN, GPIO_IN);
		gpio_set_input_hysteresis_enabled(TOUCH_IRQ_PIN,true);
		if(Option.TOUCH_Click){
			ExtCfg(Option.TOUCH_Click, EXT_BOOT_RESERVED, 0);
			TOUCH_Click_PIN=PinDef[Option.TOUCH_Click].GPno;
			gpio_init(TOUCH_Click_PIN);
			gpio_put(TOUCH_Click_PIN,GPIO_PIN_RESET);
			gpio_set_dir(TOUCH_Click_PIN, GPIO_OUT);
		}
	}
#endif
	if(Option.SYSTEM_I2C_SDA){
		ExtCfg(Option.SYSTEM_I2C_SCL, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.SYSTEM_I2C_SDA, EXT_BOOT_RESERVED, 0);
		gpio_set_function(PinDef[Option.SYSTEM_I2C_SCL].GPno, GPIO_FUNC_I2C);
		gpio_set_function(PinDef[Option.SYSTEM_I2C_SDA].GPno, GPIO_FUNC_I2C);
		if(PinDef[Option.SYSTEM_I2C_SDA].mode & I2C0SDA){
			I2C0locked=1;
			i2c_init(i2c0,(Option.SYSTEM_I2C_SLOW ? 100000:400000));
			gpio_pull_up(PinDef[Option.SYSTEM_I2C_SCL].GPno);
			gpio_pull_up(PinDef[Option.SYSTEM_I2C_SDA].GPno);
			I2C_enabled=1;
			I2C0SDApin=Option.SYSTEM_I2C_SDA;
			I2C0SCLpin=Option.SYSTEM_I2C_SCL;
			I2C_Timeout=SystemI2CTimeout;
		} else {
			I2C1locked=1;
			i2c_init(i2c1,(Option.SYSTEM_I2C_SLOW ? 100000:400000));
			gpio_pull_up(PinDef[Option.SYSTEM_I2C_SCL].GPno);
			gpio_pull_up(PinDef[Option.SYSTEM_I2C_SDA].GPno);
			I2C2_enabled=1;	
			I2C1SDApin=Option.SYSTEM_I2C_SDA;
			I2C1SCLpin=Option.SYSTEM_I2C_SCL;
			I2C2_Timeout=SystemI2CTimeout;
		}
		if(Option.RTC)RtcGetTime(1);
#ifndef USBKEYBOARD
		if(Option.KeyboardConfig==CONFIG_I2C){
			CheckI2CKeyboard(1,0);
			uSec(2000);
			CheckI2CKeyboard(1,1);
			uSec(2000);
		}
#endif	
	}
#if defined(PICOMITE) && defined(rp2350)
	if(Option.LCD_CLK && !(Option.LCD_CLK==Option.SYSTEM_CLK)){
		LCD_CLK_PIN=PinDef[Option.LCD_CLK].GPno;
		LCD_MOSI_PIN=PinDef[Option.LCD_MOSI].GPno;
		LCD_MISO_PIN=PinDef[Option.LCD_MISO].GPno;
		ExtCfg(Option.LCD_CLK, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.LCD_MOSI, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.LCD_MISO, EXT_BOOT_RESERVED, 0);
		if(PinDef[Option.LCD_CLK].mode & SPI0SCK && PinDef[Option.LCD_MOSI].mode & SPI0TX  && PinDef[Option.LCD_MISO].mode & SPI0RX  ){
			SET_SPI_CLK=HW0Clk;
			SPI0locked=1;
		} else if(PinDef[Option.LCD_CLK].mode & SPI1SCK && PinDef[Option.LCD_MOSI].mode & SPI1TX  && PinDef[Option.LCD_MISO].mode & SPI1RX  ){
			SET_SPI_CLK=HW1Clk;
			SPI1locked=1;
		}
		gpio_init(LCD_CLK_PIN);
		gpio_set_drive_strength(LCD_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
		gpio_put(LCD_CLK_PIN,GPIO_PIN_RESET);
		gpio_set_dir(LCD_CLK_PIN, GPIO_OUT);
		gpio_set_slew_rate(LCD_CLK_PIN, GPIO_SLEW_RATE_FAST);
		gpio_init(LCD_MOSI_PIN);
		gpio_set_drive_strength(LCD_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
		gpio_put(LCD_MOSI_PIN,GPIO_PIN_RESET);
		gpio_set_dir(LCD_MOSI_PIN, GPIO_OUT);
		gpio_set_slew_rate(LCD_MOSI_PIN, GPIO_SLEW_RATE_FAST);
		gpio_init(LCD_MISO_PIN);
		gpio_set_pulls(LCD_MISO_PIN,true,false);
		gpio_set_dir(LCD_MISO_PIN, GPIO_IN);
		gpio_set_input_hysteresis_enabled(LCD_MISO_PIN,true);
/*		xchg_byte= BitBangSwapSPI;
		xmit_byte_multi=BitBangSendSPI;
		rcvr_byte_multi=BitBangReadSPI;*/
	}
#endif
	if(Option.SYSTEM_CLK){
		SPI_CLK_PIN=PinDef[Option.SYSTEM_CLK].GPno;
		SPI_MOSI_PIN=PinDef[Option.SYSTEM_MOSI].GPno;
		SPI_MISO_PIN=PinDef[Option.SYSTEM_MISO].GPno;
		ExtCfg(Option.SYSTEM_CLK, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.SYSTEM_MOSI, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.SYSTEM_MISO, EXT_BOOT_RESERVED, 0);
		if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK && PinDef[Option.SYSTEM_MOSI].mode & SPI0TX  && PinDef[Option.SYSTEM_MISO].mode & SPI0RX  ){
			SET_SPI_CLK=HW0Clk;
			SPI0locked=1;
		} else if(PinDef[Option.SYSTEM_CLK].mode & SPI1SCK && PinDef[Option.SYSTEM_MOSI].mode & SPI1TX  && PinDef[Option.SYSTEM_MISO].mode & SPI1RX  ){
			SET_SPI_CLK=HW1Clk;
			SPI1locked=1;
		} else {
			SET_SPI_CLK=BitBangSetClk; 
		}
		gpio_init(SPI_CLK_PIN);
		gpio_set_drive_strength(SPI_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
		gpio_put(SPI_CLK_PIN,GPIO_PIN_RESET);
		gpio_set_dir(SPI_CLK_PIN, GPIO_OUT);
		gpio_set_slew_rate(SPI_CLK_PIN, GPIO_SLEW_RATE_FAST);
		gpio_init(SPI_MOSI_PIN);
		gpio_set_drive_strength(SPI_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
		gpio_put(SPI_MOSI_PIN,GPIO_PIN_RESET);
		gpio_set_dir(SPI_MOSI_PIN, GPIO_OUT);
		gpio_set_slew_rate(SPI_MOSI_PIN, GPIO_SLEW_RATE_FAST);
		gpio_init(SPI_MISO_PIN);
		gpio_set_pulls(SPI_MISO_PIN,true,false);
		gpio_set_dir(SPI_MISO_PIN, GPIO_IN);
		gpio_set_input_hysteresis_enabled(SPI_MISO_PIN,true);
		xchg_byte= BitBangSwapSPI;
		xmit_byte_multi=BitBangSendSPI;
		rcvr_byte_multi=BitBangReadSPI;
	}
	if(Option.SD_CS || Option.CombinedCS){
		if(!Option.CombinedCS){
			ExtCfg(Option.SD_CS, EXT_BOOT_RESERVED, 0);
			SD_CS_PIN=PinDef[Option.SD_CS].GPno;
			gpio_init(SD_CS_PIN);
			gpio_set_drive_strength(SD_CS_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(SD_CS_PIN,GPIO_PIN_SET);
			gpio_set_dir(SD_CS_PIN, GPIO_OUT);
			gpio_set_slew_rate(SD_CS_PIN, GPIO_SLEW_RATE_SLOW);
		}
		CurrentSPISpeed=NONE_SPI_SPEED;
		if(Option.SD_CLK_PIN){
			SD_CLK_PIN=PinDef[Option.SD_CLK_PIN].GPno;
			SD_MOSI_PIN=PinDef[Option.SD_MOSI_PIN].GPno;
			SD_MISO_PIN=PinDef[Option.SD_MISO_PIN].GPno;
			ExtCfg(Option.SD_CLK_PIN, EXT_BOOT_RESERVED, 0);
			ExtCfg(Option.SD_MOSI_PIN, EXT_BOOT_RESERVED, 0);
			ExtCfg(Option.SD_MISO_PIN, EXT_BOOT_RESERVED, 0);
			gpio_init(SD_CLK_PIN);
			gpio_set_drive_strength(SD_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(SD_CLK_PIN,GPIO_PIN_RESET);
			gpio_set_dir(SD_CLK_PIN, GPIO_OUT);
			gpio_set_slew_rate(SD_CLK_PIN, GPIO_SLEW_RATE_FAST);
			gpio_init(SD_MOSI_PIN);
			gpio_set_drive_strength(SD_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(SD_MOSI_PIN,GPIO_PIN_RESET);
			gpio_set_dir(SD_MOSI_PIN, GPIO_OUT);
			gpio_set_slew_rate(SD_MOSI_PIN, GPIO_SLEW_RATE_FAST);
			gpio_init(SD_MISO_PIN);
			gpio_set_pulls(SD_MISO_PIN,true,false);
			gpio_set_dir(SD_MISO_PIN, GPIO_IN);
			gpio_set_input_hysteresis_enabled(SD_MISO_PIN,true);
			xchg_byte= BitBangSwapSPI;
			xmit_byte_multi=BitBangSendSPI;
			rcvr_byte_multi=BitBangReadSPI;
		} else {
			SD_CLK_PIN=SPI_CLK_PIN;
			SD_MOSI_PIN=SPI_MOSI_PIN;
			SD_MISO_PIN=SPI_MISO_PIN;
		}
	}
	if(Option.AUDIO_L || Option.AUDIO_CLK_PIN){ //enable the audio system
		if(Option.AUDIO_L){ // Normal PWM audio
			ExtCfg(Option.AUDIO_L, EXT_BOOT_RESERVED, 0);
			ExtCfg(Option.AUDIO_R, EXT_BOOT_RESERVED, 0);
			AUDIO_L_PIN=PinDef[Option.AUDIO_L].GPno;
			AUDIO_R_PIN=PinDef[Option.AUDIO_R].GPno;
			gpio_set_function(AUDIO_L_PIN, GPIO_FUNC_PWM);
			gpio_set_function(AUDIO_R_PIN, GPIO_FUNC_PWM);
			gpio_set_slew_rate(AUDIO_L_PIN, GPIO_SLEW_RATE_SLOW);
			gpio_set_slew_rate(AUDIO_R_PIN, GPIO_SLEW_RATE_SLOW);
		} else { //SPI Audio (DAC or VS1053)
			ExtCfg(Option.AUDIO_CS_PIN, EXT_BOOT_RESERVED, 0);
			AUDIO_CS_PIN=PinDef[Option.AUDIO_CS_PIN].GPno;
//
			gpio_init(AUDIO_CS_PIN);
			gpio_set_drive_strength(AUDIO_CS_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(AUDIO_CS_PIN,GPIO_PIN_SET);
			gpio_set_dir(AUDIO_CS_PIN, GPIO_OUT);
			gpio_set_slew_rate(AUDIO_CS_PIN, GPIO_SLEW_RATE_SLOW);
//
			AUDIO_CLK_PIN=PinDef[Option.AUDIO_CLK_PIN].GPno;
			ExtCfg(Option.AUDIO_CLK_PIN, EXT_BOOT_RESERVED, 0);
			AUDIO_MOSI_PIN=PinDef[Option.AUDIO_MOSI_PIN].GPno;
			ExtCfg(Option.AUDIO_MOSI_PIN, EXT_BOOT_RESERVED, 0);
			if(PinDef[Option.AUDIO_CLK_PIN].mode & SPI0SCK && PinDef[Option.AUDIO_MOSI_PIN].mode & SPI0TX){
				SPI0locked=1;
				AUDIO_SPI=1;
			} else if(PinDef[Option.AUDIO_CLK_PIN].mode & SPI1SCK && PinDef[Option.AUDIO_MOSI_PIN].mode & SPI1TX){
				SPI1locked=1;
				AUDIO_SPI=2;
			} 
			gpio_init(AUDIO_CLK_PIN);
			gpio_set_drive_strength(AUDIO_CLK_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(AUDIO_CLK_PIN,GPIO_PIN_RESET);
			gpio_set_dir(AUDIO_CLK_PIN, GPIO_OUT);
			gpio_set_slew_rate(AUDIO_CLK_PIN, GPIO_SLEW_RATE_FAST);
			gpio_set_function(AUDIO_CLK_PIN, GPIO_FUNC_SPI);
			gpio_set_drive_strength(AUDIO_MOSI_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(AUDIO_MOSI_PIN,GPIO_PIN_RESET);
			gpio_set_dir(AUDIO_MOSI_PIN, GPIO_OUT);
			gpio_set_slew_rate(AUDIO_MOSI_PIN, GPIO_SLEW_RATE_FAST);
			gpio_set_function(AUDIO_MOSI_PIN, GPIO_FUNC_SPI);
			if(Option.AUDIO_MISO_PIN){ //VS1053 audio needs the MISO pin
				AUDIO_MISO_PIN=PinDef[Option.AUDIO_MISO_PIN].GPno;
				ExtCfg(Option.AUDIO_MISO_PIN, EXT_BOOT_RESERVED, 0);
				gpio_set_function(AUDIO_MISO_PIN, GPIO_FUNC_SPI);
				gpio_set_input_hysteresis_enabled(AUDIO_MISO_PIN,true);
        		spi_init((AUDIO_SPI==1 ? spi0 : spi1), 200000);
				spi_set_format((AUDIO_SPI==1 ? spi0 : spi1), 8, false, false, SPI_MSB_FIRST);
			} else { //DAC audio
				spi_init((AUDIO_SPI==1 ? spi0 : spi1), 16000000);
				spi_set_format((AUDIO_SPI==1 ? spi0 : spi1), 16, true, true, SPI_MSB_FIRST);
			}
		}
		if(!Option.AUDIO_DCS_PIN){ //PWM or DAC audio
			AUDIO_SLICE=Option.AUDIO_SLICE;
			AUDIO_WRAP=(Option.CPU_Speed*10)/441  - 1 ;
			pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
			if(Option.AUDIO_L){
				pwm_set_chan_level(AUDIO_SLICE, PWM_CHAN_A, AUDIO_WRAP>>1);
				pwm_set_chan_level(AUDIO_SLICE, PWM_CHAN_B, AUDIO_WRAP>>1);
				AudioOutput=DefaultAudio;
			} else {
				AudioOutput=SPIAudio;
			}
			AudioOutput(2000,2000);
			pwm_clear_irq(AUDIO_SLICE);
			irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
			irq_set_enabled(PWM_IRQ_WRAP, true);
			irq_set_priority(PWM_IRQ_WRAP,255);
			pwm_set_enabled(AUDIO_SLICE, true);
		} else { //VS1053 audio
			AUDIO_DREQ_PIN=PinDef[Option.AUDIO_DREQ_PIN].GPno;
			ExtCfg(Option.AUDIO_DREQ_PIN, EXT_BOOT_RESERVED, 0);
			gpio_init(AUDIO_DREQ_PIN); 
			gpio_set_dir(AUDIO_DREQ_PIN, GPIO_IN);
			gpio_set_input_hysteresis_enabled(AUDIO_DREQ_PIN,true);

			AUDIO_DCS_PIN=PinDef[Option.AUDIO_DCS_PIN].GPno;
			ExtCfg(Option.AUDIO_DCS_PIN, EXT_BOOT_RESERVED, 0);
			gpio_init(AUDIO_DCS_PIN);
			gpio_set_drive_strength(AUDIO_DCS_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(AUDIO_DCS_PIN,GPIO_PIN_SET);
			gpio_set_dir(AUDIO_DCS_PIN, GPIO_OUT);
			gpio_set_slew_rate(AUDIO_DCS_PIN, GPIO_SLEW_RATE_SLOW);

			AUDIO_RESET_PIN=PinDef[Option.AUDIO_RESET_PIN].GPno;
			ExtCfg(Option.AUDIO_RESET_PIN, EXT_BOOT_RESERVED, 0);
			gpio_init(AUDIO_RESET_PIN);
			gpio_set_drive_strength(AUDIO_RESET_PIN,GPIO_DRIVE_STRENGTH_8MA);
			gpio_put(AUDIO_RESET_PIN,GPIO_PIN_RESET);
			gpio_set_dir(AUDIO_RESET_PIN, GPIO_OUT);
			gpio_set_slew_rate(AUDIO_RESET_PIN, GPIO_SLEW_RATE_SLOW);
			AUDIO_SLICE=Option.AUDIO_SLICE;
			AUDIO_WRAP=(Option.CPU_Speed*10)/441  - 1 ;
			pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
			pwm_clear_irq(AUDIO_SLICE);
			irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
			irq_set_enabled(PWM_IRQ_WRAP, true);
			irq_set_priority(PWM_IRQ_WRAP,255);
 		}
	}

#ifndef PICOMITEWEB
#ifdef rp2350
	if(rp2350a){
#endif
	if(!Option.AllPins){
		if(Option.PWM){
			if(CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)){
				gpio_init(23);
				gpio_put(23,GPIO_PIN_SET);
				gpio_set_dir(23, GPIO_OUT);
			}
		} else {
			if(CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)){
				gpio_init(23);
				gpio_put(23,GPIO_PIN_RESET);
				gpio_set_dir(23, GPIO_OUT);
			}
		}
	}
#ifdef rp2350
	} 
#endif
#endif
	if(Option.SerialConsole){
		ExtCfg(Option.SerialTX, EXT_BOOT_RESERVED, 0);
		ExtCfg(Option.SerialRX, EXT_BOOT_RESERVED, 0);
		gpio_set_function(PinDef[Option.SerialTX].GPno, GPIO_FUNC_UART);
		gpio_set_function(PinDef[Option.SerialRX].GPno, GPIO_FUNC_UART);		
		uart_init((Option.SerialConsole & 3)==1 ? uart0: uart1, Option.Baudrate);
		uart_set_hw_flow((Option.SerialConsole & 3)==1 ? uart0: uart1, false, false);
		uart_set_format((Option.SerialConsole & 3)==1  ? uart0: uart1, 8, 1, UART_PARITY_NONE);
		uart_set_fifo_enabled((Option.SerialConsole & 3)==1 ? uart0: uart1,  false);
		irq_set_exclusive_handler((Option.SerialConsole & 3)==1 ? UART0_IRQ : UART1_IRQ, (Option.SerialConsole & 3)==1 ? on_uart_irq0 : on_uart_irq1);
		irq_set_enabled((Option.SerialConsole & 3)==1  ? UART0_IRQ : UART1_IRQ, true);
		uart_set_irq_enables((Option.SerialConsole & 3)==1  ? uart0: uart1, true, false);
	}
#ifndef USBKEYBOARD
	if(!(Option.KeyboardConfig==NO_KEYBOARD || Option.KeyboardConfig==CONFIG_I2C)){
		ExtCfg(Option.KEYBOARD_CLOCK, EXT_BOOT_RESERVED, 0);
    	ExtCfg(Option.KEYBOARD_DATA, EXT_BOOT_RESERVED, 0);
		gpio_init(PinDef[Option.KEYBOARD_CLOCK].GPno);
		gpio_set_pulls(PinDef[Option.KEYBOARD_CLOCK].GPno,true,false);
		gpio_set_dir(PinDef[Option.KEYBOARD_CLOCK].GPno, GPIO_IN);
		gpio_set_input_hysteresis_enabled(PinDef[Option.KEYBOARD_CLOCK].GPno,true);
		gpio_init(PinDef[Option.KEYBOARD_DATA].GPno);
		gpio_set_pulls(PinDef[Option.KEYBOARD_DATA].GPno,true,false);
		gpio_set_dir(PinDef[Option.KEYBOARD_DATA].GPno, GPIO_IN);
	}
	if(Option.MOUSE_CLOCK){
		ExtCfg(Option.MOUSE_CLOCK, EXT_BOOT_RESERVED, 0);
    	ExtCfg(Option.MOUSE_DATA, EXT_BOOT_RESERVED, 0);
	}
#endif	
}

char *pinsearch(int pin){
	char *buff=GetTempMemory(STRINGSIZE);
#ifndef PICOMITEVGA
	int ssd=PinDef[Option.SSD_DATA].GPno;
	if(pin==Option.LCD_CD)strcpy(buff,"LCD DC");
	else if(pin==Option.LCD_CS)strcpy(buff,"LCD CS");
	else if(pin==Option.LCD_RD)strcpy(buff,"LCD RD");
	else if(pin==Option.LCD_Reset)strcpy(buff,"LCD Reset");
	else if(pin==Option.DISPLAY_BL)strcpy(buff,"LCD BACKLIGHT");
	else if(pin==PINMAP[Option.SSD_DC] && Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD DC");
	else if(pin==PINMAP[Option.SSD_WR] && Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD WR");
	else if(pin==PINMAP[Option.SSD_RD] && Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD RD");
	else if(pin==PINMAP[Option.SSD_RESET])strcpy(buff,"SSD RESET");
	else if(pin==PINMAP[ssd] && Option.SSD_DC)strcpy(buff,"SSD D0");
	else if(pin==PINMAP[ssd+1] && Option.SSD_DC)strcpy(buff,"SSD D1");
	else if(pin==PINMAP[ssd+2] && Option.SSD_DC)strcpy(buff,"SSD D2");
	else if(pin==PINMAP[ssd+3] && Option.SSD_DC)strcpy(buff,"SSD D3");
	else if(pin==PINMAP[ssd+4] && Option.SSD_DC)strcpy(buff,"SSD D4");
	else if(pin==PINMAP[ssd+5] && Option.SSD_DC)strcpy(buff,"SSD D5");
	else if(pin==PINMAP[ssd+6] && Option.SSD_DC)strcpy(buff,"SSD D6");
	else if(pin==PINMAP[ssd+7] && Option.SSD_DC)strcpy(buff,"SSD D7");
	else if(pin==PINMAP[ssd+8] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D8");
	else if(pin==PINMAP[ssd+9] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D9");
	else if(pin==PINMAP[ssd+10] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D10");
	else if(pin==PINMAP[ssd+11] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D11");
	else if(pin==PINMAP[ssd+12] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D12");
	else if(pin==PINMAP[ssd+13] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D13");
	else if(pin==PINMAP[ssd+14] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D14");
	else if(pin==PINMAP[ssd+15] && Option.SSD_DC && Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C)strcpy(buff,"SSD D15");
	else
#endif
	if(pin==Option.KEYBOARD_CLOCK)strcpy(buff,"KEYBOARD CLOCK");
	else if(pin==Option.KEYBOARD_DATA)strcpy(buff,"KEYBOARD DATA");
	else if(pin==Option.MOUSE_CLOCK)strcpy(buff,"MOUSE CLOCK");
	else if(pin==Option.MOUSE_DATA)strcpy(buff,"MOUSE DATA");
	else if(pin==Option.SerialTX)strcpy(buff,"CONSOLE TX");
	else if(pin==Option.SerialRX)strcpy(buff,"CONSOLE RX");
	else if(pin==Option.AUDIO_R)strcpy(buff,"AUDIO R");
	else if(pin==Option.AUDIO_L)strcpy(buff,"AUDIO L");
	else if(pin==Option.AUDIO_CLK_PIN)strcpy(buff,"AUDIO SPI CLK");
	else if(pin==Option.AUDIO_MISO_PIN)strcpy(buff,"AUDIO SPI MISO");
	else if(pin==Option.AUDIO_MOSI_PIN)strcpy(buff,"AUDIO SPI MOSI");
	else if(pin==Option.AUDIO_CS_PIN)strcpy(buff,"AUDIO CS");
	else if(pin==Option.AUDIO_DREQ_PIN)strcpy(buff,"AUDIO DREQ");
	else if(pin==Option.AUDIO_DCS_PIN)strcpy(buff,"AUDIO DCS");
	else if(pin==Option.AUDIO_RESET_PIN)strcpy(buff,"AUDIO RESET");
	else if(pin==Option.SYSTEM_I2C_SCL)strcpy(buff,"SYSTEM I2C SCL");
	else if(pin==Option.SYSTEM_I2C_SDA)strcpy(buff,"SYSTEM I2C SDA");
	else if(pin==Option.TOUCH_CS)strcpy(buff,"TOUCH CS");
	else if(pin==Option.TOUCH_IRQ)strcpy(buff,"TOUCH IRQ");
	else if(pin==Option.SD_CS)strcpy(buff,"SD CS");
	else if(pin==Option.SD_CLK_PIN)strcpy(buff,"SD CLK");
	else if(pin==Option.SD_MISO_PIN)strcpy(buff,"SD MISO");
	else if(pin==Option.SD_MOSI_PIN)strcpy(buff,"SD MOSI");
	else if(pin==Option.SYSTEM_CLK)strcpy(buff,"SPI SYSTEM CLK");
	else if(pin==Option.SYSTEM_MOSI)strcpy(buff,"SPI SYSTEM MOSI");
	else if(pin==Option.SYSTEM_MISO)strcpy(buff,"SPI SYSTEM MISO");
#if defined(PICOMITE) && defined(rp2350)
	else if(pin==Option.LCD_CLK && Option.LCD_CLK!=Option.SYSTEM_CLK)strcpy(buff,"SPI LCD CLK");
	else if(pin==Option.LCD_MOSI && Option.LCD_CLK!=Option.SYSTEM_CLK)strcpy(buff,"SPI LCD MOSI");
	else if(pin==Option.LCD_MISO && Option.LCD_CLK!=Option.SYSTEM_CLK)strcpy(buff,"SPI LCD MISO");
#endif
	else if(pin==Option.audio_i2s_data)strcpy(buff,"I2S DATA");
	else if(pin==Option.audio_i2s_bclk)strcpy(buff,"I2S BCLK");
	else if(pin==PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1])strcpy(buff,"I2S LRCK");
#ifdef PICOMITEVGA
#ifndef HDMI
	else if(pin==Option.VGA_BLUE)strcpy(buff,"VGA BLUE");
	else if(pin==Option.VGA_HSYNC)strcpy(buff,"VGA HSYNC");
	else if(pin==PINMAP[PinDef[Option.VGA_HSYNC].GPno+1])strcpy(buff,"VGA VSYNC");
	else if(pin==PINMAP[PinDef[Option.VGA_BLUE].GPno+1])strcpy(buff,"VGA GREEN L");
	else if(pin==PINMAP[PinDef[Option.VGA_BLUE].GPno+2])strcpy(buff,"VGA GREEN H");
	else if(pin==PINMAP[PinDef[Option.VGA_BLUE].GPno+3])strcpy(buff,"VGA RED");
#endif
#endif
#ifdef rp2350
	else if(pin==Option.PSRAM_CS_PIN)strcpy(buff,"PSRAM CS");
#if defined(PICOMITE)
	else if(pin==PINMAP[24] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C1");
	else if(pin==PINMAP[26] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C2");
	else if(pin==PINMAP[27] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C3");
	else if(pin==PINMAP[28] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C4");
	else if(pin==PINMAP[29] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C5");
	else if(pin==PINMAP[30] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C6");
	else if(pin==PINMAP[31] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C7");
	else if(pin==PINMAP[32] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C8");
	else if(pin==PINMAP[33] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C9");
	else if(pin==PINMAP[34] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C10");
	else if(pin==PINMAP[35] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C11");
	else if(pin==PINMAP[36] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD C12");
	else if(pin==PINMAP[37] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R1");
	else if(pin==PINMAP[38] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R2");
	else if(pin==PINMAP[39] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R3");
	else if(pin==PINMAP[40] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R4");
	else if(pin==PINMAP[41] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R5");
	else if(pin==PINMAP[42] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD R6");
	else if(pin==PINMAP[43] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD BACKLIGHT");
	else if(pin==PINMAP[44] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD LED3");
	else if(pin==PINMAP[45] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD LED1");
	else if(pin==PINMAP[46] && Option.LOCAL_KEYBOARD)strcpy(buff,"KEYBOARD LED2");
	else if(pin==PINMAP[47] && Option.LOCAL_KEYBOARD)strcpy(buff,"BATTERY VOLTAGE");
#endif
#endif
	else strcpy(buff, "NOT KNOWN");
	return buff;
}

