/*
 * drivers/pwm_synth/pwm_synth.c — device audio runtime.
 *
 * Originally lived in the `#ifndef MMBASIC_HOST` branch of Audio.c.
 * Relocated in Phase 6b step 5 so Audio.c holds only the host-side
 * cmd_play implementation — the device body (PWM/PIO tone pipeline,
 * WAV/FLAC/MP3/MOD/MIDI decoder integration, VS1053 glue) builds as
 * a driver linked per-target by CMakeLists.txt.
 *
 * The code below is moved verbatim: same cmd_play, same CloseAudio,
 * same checkWAVinput. No behavioural change. Future refinement (per
 * real-hal/phase-6-audio.md) will expand hal_audio.h to cover file
 * playback and let Audio.c become the single BASIC-dialect layer for
 * both host and device — that's out of scope for phase 6b's exit gate,
 * which only requires Audio.c to have zero target-macro ifdefs.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "ffconf.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "synth_pcm.h"
#include "audio_play_common.h"
#include "audio_stream.h"
#include "hal/hal_audio.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "hardware/regs/addressmap.h"     /* XIP_BASE */

/* Device body: VS1053 glue plus PWM/PIO-driven tone, array, MOD, and stream
 * playback. Software WAV/MP3/FLAC decoding lives in shared/audio/audio_stream.c. */
#define MOD_BUFFER_SIZE HAL_PORT_AUDIO_MOD_BUFFER_SIZE
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hxcmod.h"
#include "drivers/vs1053/VS1053.h"
extern BYTE MDD_SDSPI_CardDetectState(void);
#define MAXALBUM 20
extern int InitSDCard(void);
extern const int ErrorMap[21];
extern char *GetCWD(void);
extern void ErrorCheck(int fnbr);
extern PIO pioi2s;
extern uint8_t i2ssm;
static int arraypos=0;
static int arraysize=0;
short *leftarray=NULL,*rightarray=NULL;

static int all_sound_slots_off(void) {
	for(int i = 0; i < MAXSOUNDS; i++) {
		if(sound_mode_left[i] != (uint16_t *)nulltable) return 0;
		if(sound_mode_right[i] != (uint16_t *)nulltable) return 0;
	}
	return 1;
}

/********************************************************************************************************************************************
commands and functions
 each function is responsible for decoding a command
 all function names are in the form cmd_xxxx() (for a basic command) or fun_xxxx() (for a basic function) so, if you want to search for the
 function responsible for the NAME command look for cmd_name

 There are 4 items of information that are setup before the command is run.
 All these are globals.

 int cmdtoken	This is the token number of the command (some commands can handle multiple
				statement types and this helps them differentiate)

 char *cmdline	This is the command line terminated with a zero char and trimmed of leading
				spaces.  It may exist anywhere in memory (or even ROM).

 char *nextstmt	This is a pointer to the next statement to be executed.  The only thing a
				command can do with it is save it or change it to some other location.

 char *CurrentLinePtr  This is read only and is set to NULL if the command is in immediate mode.

 The only actions a command can do to change the program flow is to change nextstmt or
 execute longjmp(mark, 1) if it wants to abort the program.

 ********************************************************************************************************************************************/

// define the PWM output frequency for making a tone
const char* const PlayingStr[] = {"PAUSED TONE", "PAUSED FLAC", "PAUSED MP3",  "PAUSED SOUND", "PAUSED MOD", "PAUSED ARRAY", "PAUSED WAV", "OFF", 
    "OFF", "TONE", "SOUND", "WAV", "FLAC", "MP3", 
    "MIDI", "", "MOD", "STREAM", "ARRAY", "" 
}  ;                              
volatile unsigned char PWM_count = 0;
volatile float PhaseM_left, PhaseM_right;
volatile float PhaseAC_left, PhaseAC_right;
volatile uint8_t mono;
volatile uint64_t SoundPlay;

/* CurrentlyPlaying, WAVInterrupt, WAVcomplete are defined in
 * core/state/audio_state.c. Extern declarations are in Audio.h /
 * Hardware_Includes.h. */
volatile int v_left, v_right, vol_left = 100, vol_right = 100;
char *wav_buf;                                                      // pointer to the buffer for received wav data
volatile int wav_filesize;                                                   // head and tail of the ring buffer for com1
volatile int tickspersample;
int WAV_fnbr=0;
int PWM_FREQ;
volatile int swingbuf = 0,nextbuf = 0, playreadcomplete = 1;
char *sbuff1=NULL, *sbuff2=NULL;
uint16_t *ubuff1, *ubuff2;
int16_t *g_buff1, *g_buff2;
char *modbuff=NULL;
modcontext *mcontext=NULL;
int modfilesamplerate=22050;
char *pbuffp;
void audio_checks(void);
uint16_t *playbuff;
int16_t *uplaybuff;
volatile int ppos = 0;                                                       // playing position for PLAY WAV
uint8_t nchannels;
/* The PWM ISR treats bcount[1] and bcount[2] as the publication flags for
 * completed ping-pong buffers. Foreground code must fill/convert a buffer
 * first, then assign bcount[target] only inside a short critical section. */
volatile uint32_t bcount[3] = {0, 0, 0};
volatile uint8_t audiorepeat=1;
a_flist *alist=NULL;
uint8_t trackplaying=0, trackstoplay=0;
int noloop=0;
int8_t XDCS=-1,XCS=-1,DREQ=-1,XRST=-1;
uint8_t midienabled=0;
int streamsize=0;
volatile int *streamwritepointer=NULL;
volatile int *streamreadpointer=NULL;
char *streambuffer=NULL;
char WAVfilename[FF_MAX_LFN]={0};
volatile int audio_shared_stream_active = 0;
static int audio_shared_acquired_target = 0;
static void pico_audio_publish_initial_target(int target, int count);
//*************************************************************************************

static void clear_sample_buffer_state(int complete) {
	uint32_t save = save_and_disable_interrupts();
	bcount[1] = bcount[2] = 0;
	wav_filesize = 0;
	swingbuf = nextbuf = 0;
	audio_shared_acquired_target = 0;
	ppos = 0;
	playreadcomplete = complete;
	restore_interrupts(save);
}


//#define PSpeedDiv (PeripheralBusSpeed)/2
/* nulltable, squaretable, triangletable, sawtable, SineTable now
 * live in shared/audio/synth_pcm.c (shared with the ESP32 I2S sink). */
const char wavheader[44]={0x52,0x49,0x46,0x46,
                          0xFF,0xFF,0xFF,0xFF,
						  0x57,0x41,0x56,0x45,
						  0x66,0x6d,0x74,0x20,
						  0x10,0x00,0x00,0x00,
						  0x01,0x00,
						  0x02,0x00,
						  0x80,0x3E,0x00,0x00,
						  0x0,0xFA,0x00,0x00,
						  0x04,0x00,
						  0x10,0x00,
						  0x64,0x61,0x74,0x61,
						  0xFF,0xFF,0xFF,0xFF};
const char toneheader[44]={0x52,0x49,0x46,0x46,
                           0xFF,0xFF,0xFF,0xFF,
						   0x57,0x41,0x56,0x45,
						   0x66,0x6d,0x74,0x20,
						   0x10,0x00,0x00,0x00,
						   0x01,0x00,
						   0x02,0x00,
						   0x44,0xAC,0x00,0x00,
						   0x10,0xB1,0x02,0x00,
						   0x04,0x00,
						   0x10,0x00,
						   0x64,0x61,0x74,0x61,
						   0xFF,0xFF,0xFF,0xFF};

void __not_in_flash_func(iconvert)(uint16_t *ibuff, int16_t *sbuff, int count){
	int i;
	for(i=0;i<(count);i+=2){
		ibuff[i]=(uint16_t)((((int)sbuff[i]*mapping[vol_left]/2000+32768))>>4);
		ibuff[i+1]=(uint16_t)((((int)sbuff[i+1]*mapping[vol_right]/2000+32768))>>4);
	}
}
void MIPS64 __not_in_flash_func(i2sconvert)(int16_t *fbuff, int16_t *sbuff, int count){
	int i;
	for(i=0;i<(count);i+=2){
		sbuff[i]=(int16_t)((int)(fbuff[i])*mapping[vol_left]/2000);
		sbuff[i+1]=(int16_t)((int)(fbuff[i+1])*mapping[vol_right]/2000);
	}
}

size_t onRead(void  *userdata,  char  *pBufferOut,   size_t bytesToRead){
    ssize_t n = hal_fs_read(hal_fds[WAV_fnbr], pBufferOut, bytesToRead);
    if (n < 0) { FSerror = (filesource[WAV_fnbr] == FLASHFILE) ? (int)n : FR_DISK_ERR; ErrorCheck(WAV_fnbr); return 0; }
    if (filesource[WAV_fnbr] == FATFSFILE && !MDD_SDSPI_CardDetectState()) ErrorCheck(WAV_fnbr);
    FSerror = 0;
    return (size_t)n;
}
void CloseAudio(int all){
	if(!PSRAMsize)
		modbuff =  (Option.modbuff ?  (char *)(XIP_BASE + RoundUpK4(TOP_OF_SYSTEM_FLASH)) : NULL);
	int was_playing=CurrentlyPlaying;
	if(Option.audio_i2s_bclk){
		pwm_set_irq0_enabled(AUDIO_SLICE, false);
		pwm_clear_irq(AUDIO_SLICE);
		clear_sample_buffer_state(1);
	}
	audio_stream_stop();
	if(!Option.audio_i2s_bclk){
		clear_sample_buffer_state(0);
	}
	StopAudio();
	if(CurrentlyPlaying==P_WAVOPEN)CurrentlyPlaying=P_NOTHING;
	ForceFileClose(WAV_fnbr);
	FreeMemorySafe((void **)&sbuff1);
	FreeMemorySafe((void **)&sbuff2);
	FreeMemorySafe((void **)&noisetable);
	//FreeMemorySafe((void **)&usertable);
	usertable=NULL;
	FreeMemorySafe((void **)&mcontext);
	if(all){
		FreeMemorySafe((void **)&alist);
		trackstoplay=0;
		trackplaying=0;
	}
	memset(WAVfilename,0,sizeof(WAVfilename));
	WAVcomplete = true;
	FSerror = 0;
	if(PSRAMsize && was_playing == P_MOD)FreeMemorySafe((void **)&modbuff);
    int i;
    for(i=0;i<MAXSOUNDS;i++){
    	sound_PhaseM_left[i]=0;
    	sound_PhaseM_right[i]=0;
    	sound_PhaseAC_left[i]=0;
    	sound_PhaseAC_right[i]=0;
    	sound_mode_left[i]=(uint16_t *)nulltable;
    	sound_mode_right[i]=(uint16_t *)nulltable;
    }
	if(XDCS!=-1){
		VS1053reset(XRST);
		XDCS = XCS = DREQ = XRST = -1;
		midienabled=0;
		streamsize=0;
		streamwritepointer=NULL;
		streamreadpointer=NULL;
		streambuffer=NULL;
	}
    return;
}
void setrate(int rate){
	static int lastrate=0;
	if(rate==lastrate)return;
	lastrate=rate;
	AUDIO_WRAP=(Option.CPU_Speed*1000)/rate  - 1 ;
	pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
	if(Option.AUDIO_L){
		pwm_set_both_levels(AUDIO_SLICE,(int)(((AUDIO_WRAP>>1)*4000)/4096),(int)(((AUDIO_WRAP>>1)*4000)/4096));
	}
	pwm_clear_irq(AUDIO_SLICE);
	if(Option.audio_i2s_bclk){
		float clockdiv=(Option.CPU_Speed*1000.0f)/(float)(rate*128);
		pio_sm_set_clkdiv(pioi2s,i2ssm,clockdiv);
	}
}
void playvs1053(int mode){
	XCS=PinDef[Option.AUDIO_CS_PIN].GPno;
	XDCS=PinDef[Option.AUDIO_DCS_PIN].GPno;
	DREQ=PinDef[Option.AUDIO_DREQ_PIN].GPno;
	XRST=PinDef[Option.AUDIO_RESET_PIN].GPno;
	VS1053(XCS,XDCS,DREQ,XRST);
	switchToMp3Mode();
	loadDefaultVs1053Patches(); 
	setVolumes(vol_left,vol_right);
	//playing a file
	setrate(6000); //32KHz should be fast enough
	int count;
	if(mode==P_MOD){
		memcpy((char *)sbuff1,wavheader,sizeof(wavheader));
		count = 44;
	} else {
		sbuff1 = GetMemory(WAV_BUFFER_SIZE);
		sbuff2 = GetMemory(WAV_BUFFER_SIZE);
		count = (int)onRead(NULL,sbuff1,WAV_BUFFER_SIZE);
	}
	CurrentlyPlaying = mode;
	playreadcomplete=0;
	pico_audio_publish_initial_target(1, count);
	pwm_set_irq0_enabled(AUDIO_SLICE, true);
	pwm_set_enabled(AUDIO_SLICE, true); 
	uint64_t t=hal_time_us_64();
	uSec(25);
	while(1){ //read all the headers without stalling
		checkWAVinput();
		uSec(25);
		if(hal_time_us_64()-t>500000)break;
	}
}
void playimmediatevs1053(int play){
	if(CurrentlyPlaying==P_WAVOPEN)return;
	XCS=PinDef[Option.AUDIO_CS_PIN].GPno;
	XDCS=PinDef[Option.AUDIO_DCS_PIN].GPno;
	DREQ=PinDef[Option.AUDIO_DREQ_PIN].GPno;
	XRST=PinDef[Option.AUDIO_RESET_PIN].GPno;
	VS1053(XCS,XDCS,DREQ,XRST);
	switchToMp3Mode();
	loadDefaultVs1053Patches(); 
	setVolumes(vol_left,vol_right);
	//playing a file
	setrate(PWM_FREQ); //16KHz should be fast enough
	CurrentlyPlaying = play;
	playChunk((uint8_t *)toneheader,sizeof(toneheader));
}
void wavcallback(char *p){
    if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".wav");
    if(CurrentlyPlaying == P_WAV) CloseAudio(0);
	strcpy(WAVfilename,p);
	if(!Option.AUDIO_MISO_PIN){
		if(audio_stream_play_wav(p) == 0) audio_stream_service();
		return;
	}
    WAV_fnbr = FindFreeFileNbr();
    if(!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
	playvs1053(P_WAV);
}
void mp3callback(char *p, int position){
    if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".mp3");
    if(CurrentlyPlaying == P_MP3){
    	CloseAudio(0);
    }
	if(!Option.AUDIO_MISO_PIN){
		if(Option.CPU_Speed < 200000) error("CPUSPEED >=200000 for MP3 playback");
		strcpy(WAVfilename,p);
		if(audio_stream_play_mp3(p) != 0) return;
		audio_stream_service();
		return;
	}
    WAV_fnbr = FindFreeFileNbr();
	strcpy(WAVfilename,p);
    if(!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
	if(Option.AUDIO_MISO_PIN){
		positionfile(WAV_fnbr, position);
		playvs1053(P_MP3);
		return;
	}
}
void midicallback(char *p){
    if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".mid");
    if(CurrentlyPlaying == P_MIDI){
    	CloseAudio(0);
    }
    WAV_fnbr = FindFreeFileNbr();
    if(!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
	strcpy(WAVfilename,p);
	playvs1053(P_MIDI);
}
void flaccallback(char *p){
    if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".flac");
    if(CurrentlyPlaying == P_FLAC) CloseAudio(0);
	strcpy(WAVfilename,p);
	if(!Option.AUDIO_MISO_PIN){
		if(audio_stream_play_flac(p) == 0) audio_stream_service();
		return;
	}
    WAV_fnbr = FindFreeFileNbr();
    if(!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
	playvs1053(P_FLAC);
}
void rampvolume(int l, int r, int channel, int target){
	if(optionfastaudio){
		if(l)sound_v_left[channel]=target;
		if(r)sound_v_right[channel]=target;
	} else {
		int ramptime=1000000/PWM_FREQ+2;
		if(l && r){
			if(sound_v_left[channel]>target){
				for(int i=sound_v_left[channel]-1;i>=target;i--){
					sound_v_left[channel]=i;
					sound_v_right[channel]=i;
					uSec(ramptime);
				}
			} else {
				for(int i=sound_v_left[channel]+1;i<=target;i++){
					sound_v_left[channel]=i;
					sound_v_right[channel]=i;
					uSec(ramptime);
				}
			}
		} else if(l){
			if(sound_v_left[channel]>target){
				for(int i=sound_v_left[channel]-1;i>=target;i--){
					sound_v_left[channel]=i;
					uSec(ramptime);
				}
			} else {
				for(int i=sound_v_left[channel]+1;i<=target;i++){
					sound_v_left[channel]=i;
					uSec(ramptime);
				}
			}
		} else if(r){
			if(sound_v_right[channel]>target){
				for(int i=sound_v_right[channel]-1;i>=target;i--){
					sound_v_right[channel]=i;
					uSec(ramptime);
				}
			} else {
				for(int i=sound_v_right[channel]+1;i<=target;i++){
					sound_v_right[channel]=i;
					uSec(ramptime);
				}
			}
		}
	}
}

unsigned int readarray(char *sbuff){
	short *buff=(short *)sbuff;
	int count=arraysize-arraypos;
	if(count>WAV_BUFFER_SIZE/(sizeof(short)*2))count = WAV_BUFFER_SIZE/((sizeof(short))*2);
	for(int i=arraypos;i< arraypos+count;i++){
		*buff++=leftarray[i];
		*buff++=rightarray[i];
	}
	arraypos+=count;
	if(count<WAV_BUFFER_SIZE/(sizeof(short)*2))playreadcomplete = 1;
	return count*2;
};

static void pico_synth_note(int channel, int note, int velocity)
{
	if(!(CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_SOUND ||
	     CurrentlyPlaying == P_PAUSE_SOUND || CurrentlyPlaying == P_STOP ||
	     CurrentlyPlaying == P_WAVOPEN)) {
		error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
	}
	int slot = channel;
	pwm_set_irq0_enabled(AUDIO_SLICE, false);
	if(velocity <= 0) {
		sound_PhaseM_left[slot] = 0.0f;
		sound_PhaseM_right[slot] = 0.0f;
		sound_v_left[slot] = 0;
		sound_v_right[slot] = 0;
		sound_mode_left[slot] = (uint16_t *)nulltable;
		sound_mode_right[slot] = (uint16_t *)nulltable;
		if(all_sound_slots_off()) {
			StopAudio();
			return;
		}
	} else {
		float phase = (float)(audio_play_midi_note_frequency(note) /
		                      (double)PWM_FREQ * 4096.0);
		if(sound_mode_left[slot] != (uint16_t *)SineTable)
			sound_PhaseAC_left[slot] = 0.0f;
		if(sound_mode_right[slot] != (uint16_t *)SineTable)
			sound_PhaseAC_right[slot] = 0.0f;
		sound_PhaseM_left[slot] = phase;
		sound_PhaseM_right[slot] = phase;
		int volume = audio_play_volume_to_synth(
			audio_play_note_velocity_volume(velocity));
		sound_v_left[slot] = volume;
		sound_v_right[slot] = volume;
		sound_mode_left[slot] = (uint16_t *)SineTable;
		sound_mode_right[slot] = (uint16_t *)SineTable;
	}
	if(!(CurrentlyPlaying == P_SOUND || CurrentlyPlaying == P_PAUSE_SOUND)) {
		setrate(PWM_FREQ);
		pwm_set_enabled(AUDIO_SLICE, true);
	}
	CurrentlyPlaying = P_SOUND;
	pwm_set_irq0_enabled(AUDIO_SLICE, true);
}

/*  @endcond */
// The MMBasic command:  PLAY
void MIPS16 cmd_play(void) {
    unsigned char *tp;
    if(checkstring(cmdline, (unsigned char *)"STOP")) {
		if(CurrentlyPlaying == P_NOTHING)return;
        CloseAudio(1);
        return;
    }
	if(!(Option.AUDIO_L || Option.AUDIO_CLK_PIN || Option.audio_i2s_bclk))error((char *)"Audio not enabled");
    if((tp=checkstring(cmdline, (unsigned char *)"LOAD SOUND"))) {
		if(Option.AUDIO_MISO_PIN)error("Not available with VS1053 audio");
        if(usertable!=NULL)error("Already loaded");
//        unsigned int nbr;
        uint16_t *dd;
		int64_t *aint;
		skipspace(tp);
		int size=parseintegerarray(tp,&aint,1,1,NULL,false);
       	dd = (uint16_t *)aint;
        if(size!=1024) error("Array size");
		usertable=dd;
        return;
    }
    if(checkstring(cmdline, (unsigned char *)"NEXT")) {
		if(CurrentlyPlaying == P_FLAC){
			if(trackplaying==trackstoplay){
				if(!CurrentLinePtr)MMPrintString("Last track is playing\r\n");
				return;
			}
			trackplaying++;
			flaccallback(alist[trackplaying].fn);
		} else if(CurrentlyPlaying == P_WAV){
			if(trackplaying==trackstoplay){
				if(!CurrentLinePtr)MMPrintString("Last track is playing\r\n");
				return;
			}
			trackplaying++;
			wavcallback(alist[trackplaying].fn);
		} else if(CurrentlyPlaying == P_MP3){
			if(trackplaying==trackstoplay){
				if(!CurrentLinePtr)MMPrintString("Last track is playing\r\n");
				return;
			}
			trackplaying++;
			mp3callback(alist[trackplaying].fn,0);
		} else if(CurrentlyPlaying == P_MIDI){
			if(trackplaying==trackstoplay){
				if(!CurrentLinePtr)MMPrintString("Last track is playing\r\n");
				return;
			}
			trackplaying++;
			midicallback(alist[trackplaying].fn);
		} else error("Nothing to play");

    	return;
    }
    if(checkstring(cmdline, (unsigned char *)"PREVIOUS")) {
		if(CurrentlyPlaying == P_FLAC){
			if(trackplaying==0){
				if(!CurrentLinePtr)MMPrintString("First track is playing\r\n");
				return;
			}
			trackplaying--;
			flaccallback(alist[trackplaying].fn);
		} else if(CurrentlyPlaying == P_WAV){
			if(trackplaying==0){
				if(!CurrentLinePtr)MMPrintString("First track is playing\r\n");
				return;
			}
			trackplaying--;
			wavcallback(alist[trackplaying].fn);
		} else if(CurrentlyPlaying == P_MP3){
			if(trackplaying==0){
				if(!CurrentLinePtr)MMPrintString("First track is playing\r\n");
				return;
			}
			trackplaying--;
			mp3callback(alist[trackplaying].fn,0);
		} else if(CurrentlyPlaying == P_MIDI){
			if(trackplaying==0){
				if(!CurrentLinePtr)MMPrintString("First track is playing\r\n");
				return;
			}
			trackplaying--;
			midicallback(alist[trackplaying].fn);
		} else error("Nothing to play");

    	return;
    }
    if(checkstring(cmdline, (unsigned char *)"PAUSE")) {
		if(CurrentlyPlaying<P_STOP)return; //already paused
        if(CurrentlyPlaying == P_TONE) CurrentlyPlaying = P_PAUSE_TONE;
        else if(CurrentlyPlaying == P_SOUND) CurrentlyPlaying = P_PAUSE_SOUND;
        else if(CurrentlyPlaying == P_WAV)  CurrentlyPlaying = P_PAUSE_WAV;
        else if(CurrentlyPlaying == P_FLAC)  CurrentlyPlaying = P_PAUSE_FLAC;
        else if(CurrentlyPlaying == P_MP3)  CurrentlyPlaying = P_PAUSE_MP3;
        else if(CurrentlyPlaying == P_MOD)  CurrentlyPlaying = P_PAUSE_MOD;
        else if(CurrentlyPlaying == P_ARRAY)  CurrentlyPlaying = P_PAUSE_ARRAY;
        else
            error("Nothing playing");
        return;
    }
    if(checkstring(cmdline, (unsigned char *)"RESUME")) {
        if(CurrentlyPlaying == P_PAUSE_TONE) CurrentlyPlaying = P_TONE;
        else if(CurrentlyPlaying == P_PAUSE_SOUND) CurrentlyPlaying = P_SOUND;
        else if(CurrentlyPlaying == P_PAUSE_WAV) CurrentlyPlaying = P_WAV;
        else if(CurrentlyPlaying == P_PAUSE_FLAC) CurrentlyPlaying = P_FLAC;
        else if(CurrentlyPlaying == P_PAUSE_MP3)  CurrentlyPlaying = P_MP3;
        else if(CurrentlyPlaying == P_PAUSE_MOD)  CurrentlyPlaying = P_MOD;
        else if(CurrentlyPlaying == P_PAUSE_ARRAY)  CurrentlyPlaying = P_ARRAY;
        else
            error("Nothing to resume");  
        return;
    }
    if(checkstring(cmdline, (unsigned char *)"CLOSE")) {
        CloseAudio(1);
        return;
    }
    if((tp = checkstring(cmdline, (unsigned char *)"VOLUME"))) {
        getargs(&tp, 3,(unsigned char *)",");
        if(argc < 1) error("Argument count");
        if(*argv[0]) vol_left = getint(argv[0], 0, 100);
        if(argc == 3) vol_right = getint(argv[2], 0, 100);
		if(CurrentlyPlaying==P_TONE && vol_left!=vol_right && mono)mono=0;
		if(Option.AUDIO_MISO_PIN && CurrentlyPlaying!=P_NOTHING){
			pwm_set_irq0_enabled(AUDIO_SLICE, false);
			setVolumes(vol_left, vol_right);
			pwm_set_irq0_enabled(AUDIO_SLICE, true);
			pwm_set_enabled(AUDIO_SLICE, true); 
		}
        return;
    }
    if((tp = checkstring(cmdline, (unsigned char *)"TONE"))) {//
		SoundPlay=1000;                                   // this MUST be the first executable line in the function
        float f_left, f_right;
        float hw, duration;
        uint64_t PlayDuration = 0xffffffffffffffff;                     // default is to play forever
//        uint64_t  x;
        {// get the command line arguments
			getargs(&tp, 7,(unsigned char *)","); 
			if(!(argc == 3 || argc == 5 || argc == 7)) error("Argument count");
//			if(Option.AUDIO_MISO_PIN)error("Not available with VS1053 audio");
			mono=0;
			if(!(CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_TONE || CurrentlyPlaying == P_PAUSE_TONE || CurrentlyPlaying == P_STOP || CurrentlyPlaying == P_WAVOPEN)) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);
			f_left = getnumber(argv[0]);                         // get the arguments
			f_right = getnumber(argv[2]);
			if(f_left==f_right && vol_left==vol_right)mono=1;
			if(f_left<0.0 || f_left>22050.0)error("Valid is 0Hz to 20KHz");
			if(f_right<0.0 || f_right>22050.0)error("Valid is 0Hz to 20KHz");
			if(argc > 4) {
				duration = ((float)getint(argv[4], 0, INT_MAX)/1000.0); //tone duration in seconds
				PlayDuration=(uint64_t)duration;
			} else duration=1;
			if(argc == 7) {
				if(!CurrentLinePtr)error("No program running");
				WAVInterrupt = (char *)GetIntAddress(argv[6]);					// get the interrupt location
				WAVcomplete=false;
				InterruptUsed = true;
			}
			if(duration == 0) return;
			if(PlayDuration != 0xffffffffffffffff && f_left >=10.0){
				hw=((float)PWM_FREQ/(float)f_left); //number of interrupts per cycle
				duration = duration * (float)PWM_FREQ; // number of interrupts for the requested waveform
	// This should now be an exact multiple of the number per waveform
				PlayDuration=(((uint64_t)(duration/hw))*hw);
			} else if(PlayDuration != 0xffffffffffffffff)PlayDuration=duration * (float)PWM_FREQ;
			pwm_set_irq0_enabled(AUDIO_SLICE, false);
			PhaseM_left =  f_left  / (float)PWM_FREQ * 4096.0;
			PhaseM_right = f_right  / (float)PWM_FREQ * 4096.0;
			WAV_fnbr=0;

			SoundPlay = PlayDuration;
			if (!(CurrentlyPlaying == P_PAUSE_TONE || CurrentlyPlaying == P_TONE )){
				setrate(PWM_FREQ);
				PhaseAC_right=0.0;
				PhaseAC_left=0.0;
				if(Option.AUDIO_MISO_PIN)playimmediatevs1053(P_TONE);
			}
			CurrentlyPlaying = P_TONE;
			pwm_set_irq0_enabled(AUDIO_SLICE, true);
			pwm_set_enabled(AUDIO_SLICE, true); 
			return;
		}
    }
    if((tp = checkstring(cmdline, (unsigned char *)"ARRAY"))) {//PLAY ARRAY left%(), right%(), frequency, startpos, endpos, interrupt
		float freq;
        getargs(&tp, 11,(unsigned char *)",");                                       // this MUST be the first executable line in the function
        if(!(argc == 11 || argc == 9 || argc == 7 || argc == 5)) error("Argument count");
		arraysize=parseintegerarray(argv[0],(int64_t**)&leftarray,1,1,NULL,false);
		if(parseintegerarray(argv[2],(int64_t**)&rightarray,2,1,NULL,false)!=arraysize)error("Array size mismatch");
		arraysize*=4;
		freq=getnumber(argv[4]);
		if(freq<10.0 || freq> 48000.0)error("Invalid frequency 10.0 - 48000.0");
		arraypos=0;
		if(argc>=7 && *argv[6]){
			arraypos=getint(argv[6],0,arraysize-1);
		}
		if(argc>=9 && *argv[8]){
			arraysize=getint(argv[8],arraypos,arraysize);
		}
		if(argc == 11) {
			if(!CurrentLinePtr)error("No program running");
			WAVInterrupt = (char *)GetIntAddress(argv[10]);					// get the interrupt location
			WAVcomplete=false;
			InterruptUsed = true;
		}
		audiorepeat=1;
		float actualrate=freq;
		while(actualrate<32000){
			actualrate +=freq;
			audiorepeat++;
		}
		setrate(actualrate);
		FreeMemorySafe((void **)&sbuff1);
		FreeMemorySafe((void **)&sbuff2);
		sbuff1 = GetMemory(WAV_BUFFER_SIZE);
		sbuff2 = GetMemory(WAV_BUFFER_SIZE);
		ubuff1 = (uint16_t *)sbuff1;
		ubuff2 = (uint16_t *)sbuff2;
		mono=0;
		g_buff1 = (int16_t *)sbuff1;
		g_buff2 = (int16_t *)sbuff2;
		playreadcomplete=0;
		int count = (int)readarray(sbuff1);
		if(Option.audio_i2s_bclk) i2sconvert((int16_t *)sbuff1,(int16_t *)sbuff1,count);
		else iconvert(ubuff1, (int16_t *)sbuff1, count);
		CurrentlyPlaying = P_ARRAY;
		pico_audio_publish_initial_target(1, count);
		pwm_set_irq0_enabled(AUDIO_SLICE, true);
		pwm_set_enabled(AUDIO_SLICE, true); 
		return;
	}
    if((tp = checkstring(cmdline, (unsigned char *)"SOUND"))) {//PLAY SOUND channel, type, position, frequency, volume
        float f_in, PhaseM;
        int channel, left=0, right=0, lset=0, rset=0, local_sound_v_left=0,local_sound_v_right=0;
		char *p;
        uint16_t *lastleft=NULL, *lastright=NULL, *local_sound_mode_left=(uint16_t *)nulltable, *local_sound_mode_right=(uint16_t *)nulltable;
        // get the command line arguments
        getargs(&tp, 9,(unsigned char *)",");                                       // this MUST be the first executable line in the function
        if(!(argc == 9 || argc == 7 || argc == 5)) error("Argument count");
        if(checkstring(argv[4],(unsigned char *)"O")==NULL && argc == 5) error("Argument count");
		WAV_fnbr=0;
        channel=getint(argv[0],1,MAXSOUNDS)-1;
        lastleft=local_sound_mode_left=(uint16_t *)sound_mode_left[channel];
        lastright=local_sound_mode_right=(uint16_t *)sound_mode_right[channel];
        if(checkstring(argv[2],(unsigned char *)"L")!=NULL){
        	left=1;
        } else if(checkstring(argv[2],(unsigned char *)"R")!=NULL){
        	right=1;
        } else if(checkstring(argv[2],(unsigned char *)"B")!=NULL){
        	right=1;
        	left=1;
        } else {
			p=(char *)getCstring(argv[2]);
			if(strcasecmp(p,"B")==0){
				right=1;
				left=1;
			} else if(strcasecmp(p,"M")==0){
				right=1;
				left=1;
			} else if (strcasecmp(p,"L")==0){
				left=1;
			} else if (strcasecmp(p,"R")==0){
				right=1;
			} else error("Position must be L, R, or B");
		}
        if(!(CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_SOUND || CurrentlyPlaying == P_PAUSE_SOUND || CurrentlyPlaying == P_STOP || CurrentlyPlaying == P_WAVOPEN)) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);
        if(checkstring(argv[4],(unsigned char *)"O")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)nulltable;}
        if(checkstring(argv[4],(unsigned char *)"O")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)nulltable;}
        if(checkstring(argv[4],(unsigned char *)"Q")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)squaretable;}
        if(checkstring(argv[4],(unsigned char *)"Q")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)squaretable;}
        if(checkstring(argv[4],(unsigned char *)"T")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)triangletable;}
        if(checkstring(argv[4],(unsigned char *)"T")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)triangletable;}
        if(checkstring(argv[4],(unsigned char *)"W")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)sawtable;}
        if(checkstring(argv[4],(unsigned char *)"W")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)sawtable;}
        if(checkstring(argv[4],(unsigned char *)"S")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)SineTable;}
        if(checkstring(argv[4],(unsigned char *)"S")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)SineTable;}
        if(checkstring(argv[4],(unsigned char *)"P")!=NULL && left){lset=1;setnoise();local_sound_mode_left=(uint16_t *)noisetable;}
        if(checkstring(argv[4],(unsigned char *)"P")!=NULL && right){rset=1;setnoise();local_sound_mode_right=(uint16_t *)noisetable;}
        if(checkstring(argv[4],(unsigned char *)"N")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)whitenoise;}
        if(checkstring(argv[4],(unsigned char *)"N")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)whitenoise;}
        if(checkstring(argv[4],(unsigned char *)"U")!=NULL && left){lset=1;local_sound_mode_left=(uint16_t *)usertable;}
        if(checkstring(argv[4],(unsigned char *)"U")!=NULL && right){rset=1;local_sound_mode_right=(uint16_t *)usertable;}
		if(left && lset==0){
			p=(char *)getCstring(argv[4]);
			if(strcasecmp(p,"O")==0)local_sound_mode_left=(uint16_t *)nulltable;
			if(strcasecmp(p,"Q")==0)local_sound_mode_left=(uint16_t *)squaretable;
			if(strcasecmp(p,"T")==0)local_sound_mode_left=(uint16_t *)triangletable;
			if(strcasecmp(p,"W")==0)local_sound_mode_left=(uint16_t *)sawtable;
			if(strcasecmp(p,"S")==0)local_sound_mode_left=(uint16_t *)SineTable;
			if(strcasecmp(p,"P")==0){setnoise();local_sound_mode_left=(uint16_t *)noisetable;}
			if(strcasecmp(p,"N")==0)local_sound_mode_left=(uint16_t *)whitenoise;
			if(strcasecmp(p,"U")==0)local_sound_mode_left=(uint16_t *)usertable;
			if(local_sound_mode_left==NULL)error("Invalid type");
			else lset=1;
		}
		if(right && rset==0){
			p=(char *)getCstring(argv[4]);
			if(strcasecmp(p,"O")==0)local_sound_mode_right=(uint16_t *)nulltable;
			if(strcasecmp(p,"Q")==0)local_sound_mode_right=(uint16_t *)squaretable;
			if(strcasecmp(p,"T")==0)local_sound_mode_right=(uint16_t *)triangletable;
			if(strcasecmp(p,"W")==0)local_sound_mode_right=(uint16_t *)sawtable;
			if(strcasecmp(p,"S")==0)local_sound_mode_right=(uint16_t *)SineTable;
			if(strcasecmp(p,"P")==0){setnoise();local_sound_mode_right=(uint16_t *)noisetable;;}
			if(strcasecmp(p,"N")==0)local_sound_mode_right=(uint16_t *)whitenoise;
			if(strcasecmp(p,"U")==0)local_sound_mode_right=(uint16_t *)usertable;
			if(local_sound_mode_right==NULL)error("Invalid type");
			else rset=1;
		}
		if((local_sound_mode_left==usertable || local_sound_mode_right==usertable) && usertable==NULL) error("Not loaded");
        f_in=10.0;
        if(argc>=7)f_in = getnumber(argv[6]);
        // get the arguments
        if(f_in<1.0 || f_in>20000.0)error("Valid is 1Hz to 20KHz");
        if(left){
        	if(!(sound_mode_left[channel]==whitenoise )){
        		PhaseM =  f_in  / (float)PWM_FREQ * 4096.0;
        	} else {
        		PhaseM =  f_in;
        	}
        	if(lastleft!=local_sound_mode_left){
				if(!Option.AUDIO_MISO_PIN)rampvolume(1,0,channel,0);
				sound_PhaseAC_left[channel] = 0.0;
			}
        	sound_PhaseM_left[channel] = PhaseM;
            if(argc==9)local_sound_v_left=getint(argv[8],0,100/MAXSOUNDS);
            else local_sound_v_left=25;
			local_sound_v_left=local_sound_v_left*41/(100/MAXSOUNDS);
        }
        if(right){
        	if(!(sound_mode_right[channel]==whitenoise )){
        		PhaseM =  f_in  / (float)PWM_FREQ * 4096.0;
        	} else {
        		PhaseM =  f_in;
        	}
        	if(lastright!=local_sound_mode_right){
				if(!Option.AUDIO_MISO_PIN)rampvolume(0,1,channel,0);
				sound_PhaseAC_right[channel] = 0.0;
			}
        	sound_PhaseM_right[channel] = PhaseM;
            if(argc==9)local_sound_v_right=getint(argv[8],0,100/MAXSOUNDS);
            else local_sound_v_right=25;
			local_sound_v_right=local_sound_v_right*41/(100/MAXSOUNDS);
        }
		if(left && right && local_sound_v_left==local_sound_v_right && sound_v_left[channel]==sound_v_right[channel]  && !Option.AUDIO_MISO_PIN)rampvolume(1,1,channel,local_sound_v_left);
		else {
			if(left  && !Option.AUDIO_MISO_PIN)rampvolume(1,0,channel,local_sound_v_left);
			if(right  && !Option.AUDIO_MISO_PIN)rampvolume(0,1,channel,local_sound_v_right);
		}
		if(left)sound_mode_left[channel]=local_sound_mode_left;
		if(right)sound_mode_right[channel]=local_sound_mode_right;
		if(all_sound_slots_off()) {
			StopAudio();
			return;
		}
        if(!(CurrentlyPlaying == P_SOUND || CurrentlyPlaying==P_PAUSE_SOUND)){
			setrate(PWM_FREQ);
			if(Option.AUDIO_MISO_PIN)playimmediatevs1053(P_SOUND);
    		pwm_set_irq0_enabled(AUDIO_SLICE, true);
			pwm_set_enabled(AUDIO_SLICE, true); 
		}
        CurrentlyPlaying = P_SOUND;
        return;
    }
    if((tp = checkstring(cmdline, (unsigned char *)"WAV"))) {
        char *p;
		int FatFSFileSystemSave=FatFSFileSystem;
        int i __attribute((unused))=0;
        getargs(&tp, 3,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        if(!(argc == 1 || argc == 3)) error("Argument count");
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);

        p = (char *)getFstring(argv[0]);                                    // get the file name
		char q[FF_MAX_LFN]={0};
		getfullfilename(p,q);
        if(!InitSDCard()) return;
		if(!FatFSFileSystem && toupper(p[0])=='B' && p[1]==':')FatFSFileSystem=1;
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if(argc == 3) {
			if(!CurrentLinePtr)error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);					// get the interrupt location
            InterruptUsed = true;
        }
		if(FatFSFileSystem){
			FRESULT fr;
			FILINFO fno;
			int i;
			if(ExistsDir(q,q,&i)){
				alist=GetMemory(sizeof(a_flist)*MAXALBUM);
				trackstoplay=0;
				trackplaying=0;
				DIR djd;
				djd.pat="*.wav";
				if(!CurrentLinePtr)MMPrintString("Directory found - commencing player\r\n");
				FSerror = f_opendir(&djd, q);
				for(;;){
					fr=f_readdir(&djd, &fno);
					if (fr != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
					// Get a directory item
					if (pattern_matching(djd.pat, fno.fname, 0, 0)){
					// Get a directory item
						strcpy(alist[trackstoplay].fn,"B:");
						strcat(alist[trackstoplay].fn,q);
						strcat(alist[trackstoplay].fn,"/");
						strcat(alist[trackstoplay].fn,fno.fname);
						str_replace(alist[trackstoplay].fn, "//", "/",3);
						str_replace(alist[trackstoplay].fn, "/./", "/",3);
						if(!CurrentLinePtr){
							MMPrintString(fno.fname);
							PRet();
						}
						trackstoplay++;
						if(trackstoplay==MAXALBUM)break;
					}
				}
				trackstoplay--;
				f_closedir(&djd);
				wavcallback(alist[trackplaying].fn);
				return;
    	    }
			FatFSFileSystem=FatFSFileSystemSave;
		}
        // open the file
        trackstoplay=0;
        trackplaying=0;
		memset(q,0,sizeof(q));
		getfullfilename(p,q);
		memmove(&q[2],q,strlen(q));
		q[1]=':';
		q[0]=FatFSFileSystem ? 'B' : 'A';
        wavcallback(q);
        return;
    }
	if((tp = checkstring(cmdline, (unsigned char *)"FLAC"))) {
        char *p;
		int FatFSFileSystemSave=FatFSFileSystem;
        int i __attribute((unused))=0;
        getargs(&tp, 3,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        if(!(argc == 1 || argc == 3)) error("Argument count");
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);

        p = (char *)getFstring(argv[0]);                                    // get the file name
		char q[FF_MAX_LFN]={0};
		getfullfilename(p,q);
        if(!InitSDCard()) return;
		if(!FatFSFileSystem && toupper(p[0])=='B' && p[1]==':')FatFSFileSystem=1;
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if(argc == 3) {
			if(!CurrentLinePtr)error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);					// get the interrupt location
            InterruptUsed = true;
        }
		if(FatFSFileSystem){
			FRESULT fr;
			FILINFO fno;
			int i;
			if(ExistsDir(q,q,&i)){
				alist=GetMemory(sizeof(a_flist)*MAXALBUM);
				trackstoplay=0;
				trackplaying=0;
				DIR djd;
				djd.pat="*.flac";
				if(!CurrentLinePtr)MMPrintString("Directory found - commencing player\r\n");
				FSerror = f_opendir(&djd, q);
				for(;;){
					fr=f_readdir(&djd, &fno);
					if (fr != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
					// Get a directory item
					if (pattern_matching(djd.pat, fno.fname, 0, 0)){
					// Get a directory item
						strcpy(alist[trackstoplay].fn,"B:");
						strcat(alist[trackstoplay].fn,q);
						strcat(alist[trackstoplay].fn,"/");
						strcat(alist[trackstoplay].fn,fno.fname);
						str_replace(alist[trackstoplay].fn, "//", "/",3);
						str_replace(alist[trackstoplay].fn, "/./", "/",3);
						if(!CurrentLinePtr){
							MMPrintString(fno.fname);
							PRet();
						}
						trackstoplay++;
						if(trackstoplay==MAXALBUM)break;
					}
				}
				trackstoplay--;
				f_closedir(&djd);
				flaccallback(alist[trackplaying].fn);
				return;
    	    }
			FatFSFileSystem=FatFSFileSystemSave;
		}
        // open the file
        trackstoplay=0;
        trackplaying=0;
		memset(q,0,sizeof(q));
		getfullfilename(p,q);
		memmove(&q[2],q,strlen(q));
		q[1]=':';
		q[0]=FatFSFileSystem ? 'B' : 'A';
        flaccallback(q);
        return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"NOTE"))) {
		unsigned char *xp;
		if((xp = checkstring(tp, (unsigned char *)"ON"))) {
			getargs(&xp,5,(unsigned char *)",");
			if(!(argc==5))error("Syntax");
			if(Option.AUDIO_MISO_PIN) {
				if(!midienabled)error("Midi output not enabled");
				uint8_t channel=getint(argv[0],0,15);
				uint8_t note=getint(argv[2],0,127);
				uint8_t velocity=getint(argv[4],0,127);
				noteOn(channel,note,velocity);
			} else {
				int channel=getint(argv[0],0,3);
				int note=getint(argv[2],0,127);
				int velocity=getint(argv[4],0,127);
				pico_synth_note(channel, note, velocity);
			}
		} else if((xp = checkstring(tp, (unsigned char *)"OFF"))) {
			getargs(&xp,5,(unsigned char *)",");
			if(!(argc==5 || argc==3))error("Syntax");
			if(Option.AUDIO_MISO_PIN) {
				if(!midienabled)error("Midi output not enabled");
				uint8_t channel=getint(argv[0],0,15);
				uint8_t note=getint(argv[2],0,127);
				uint8_t velocity=0;
				if(argc==5)velocity=getint(argv[4],0,127);
				noteOff(channel,note,velocity);
			} else {
				int channel=getint(argv[0],0,3);
				(void)getint(argv[2],0,127);
				if(argc==5)(void)getint(argv[4],0,127);
				pico_synth_note(channel, 0, 0);
			}
		} else error("Syntax");
		return;
	}
    if((tp = checkstring(cmdline, (unsigned char *)"STREAM"))) {
		getargs(&tp,5,(unsigned char *)",");
        if(!(argc == 5 )) error("Syntax");
		if(!Option.AUDIO_MISO_PIN)error("Only available with VS1053 audio");
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);
		WAVInterrupt = NULL;
		WAVcomplete = 0;
		if(XDCS!=-1)error("VS1053 already open");
		void *ptr1 = NULL;
		int64_t *aint;
		streamsize=parseintegerarray(argv[0], &aint, 1, 1, NULL, true) * 8;
		streambuffer=(char *)aint;
		ptr1 = findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
		if(g_vartbl[g_VarIndex].type & T_INT) {
				if(g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 2 must be an integer");
				streamreadpointer = (int *)ptr1;
		} else error("Argument 2 must be an integer");
		ptr1 = findvar(argv[4], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
		if(g_vartbl[g_VarIndex].type & T_INT) {
				if(g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 3 must be an integer");
				streamwritepointer = (int *)ptr1;
		} else error("Argument 3 must be an integer");
		XCS=PinDef[Option.AUDIO_CS_PIN].GPno;
		XDCS=PinDef[Option.AUDIO_DCS_PIN].GPno;
		DREQ=PinDef[Option.AUDIO_DREQ_PIN].GPno;
		XRST=PinDef[Option.AUDIO_RESET_PIN].GPno;
		VS1053(XCS,XDCS,DREQ,XRST);
		switchToMp3Mode();
		loadDefaultVs1053Patches(); 
		setVolumes(vol_left,vol_right);
		MMPrintString("Stream output enabled\r\n");	
		playreadcomplete=0;
		CurrentlyPlaying=P_STREAM;
		setrate(16000); //16KHz should be fast enough
		pwm_set_irq0_enabled(AUDIO_SLICE, true);
		pwm_set_enabled(AUDIO_SLICE, true); 
		return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"MIDI"))) {
		unsigned char *xp;
		if((xp = checkstring(tp, (unsigned char *)"CMD"))) {
			getargs(&xp,5,(unsigned char *)",");
			if(!midienabled)error("Midi output not enabled");
			if(!(argc==5 || argc==3))error("Syntax");
			uint8_t cmd=getint(argv[0],128,255);
			uint8_t data1=getint(argv[2],0,127);
			uint8_t data2=0;
			if(argc==5)data2=getint(argv[4],0,127);
			talkMIDI(cmd, data1, data2);
			return;
		}
		if((xp = checkstring(tp, (unsigned char *)"TEST"))) {
			getargs(&xp,1,(unsigned char *)",");
			if(!midienabled)error("Midi output not enabled");
			miditest(getint(argv[0],1,3));	
			return;
		}
		if(!Option.AUDIO_MISO_PIN)error("Only available with VS1053 audio");
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);
		WAVInterrupt = NULL;
		WAVcomplete = 0;
		XCS=PinDef[Option.AUDIO_CS_PIN].GPno;
		XDCS=PinDef[Option.AUDIO_DCS_PIN].GPno;
		DREQ=PinDef[Option.AUDIO_DREQ_PIN].GPno;
		XRST=PinDef[Option.AUDIO_RESET_PIN].GPno;
		VS1053(XCS,XDCS,DREQ,XRST);
		setVolumes(vol_left,vol_right);
		midienabled=1;
		miditest(0);
		if(!CurrentLinePtr)MMPrintString("Real Time MIDI mode enabled\r\n");
		return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"MIDIFILE"))) {
        char *p;
        int i __attribute((unused))=0;
        getargs(&tp, 3,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        if(!(argc == 1 || argc == 3)) error("Argument count");
		if(!Option.AUDIO_MISO_PIN)error("Only available with VS1053 audio");
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);

        if(!InitSDCard()) return;
        p = (char *)getFstring(argv[0]);                                    // get the file name
		char q[FF_MAX_LFN]={0};
		getfullfilename(p,q);
        WAVInterrupt = NULL;

        WAVcomplete = 0;
        if(argc == 3) {
			if(!CurrentLinePtr)error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);					// get the interrupt location
            InterruptUsed = true;
        }
		if(FatFSFileSystem){
			FRESULT fr;
			FILINFO fno;
			int i;
			if(ExistsDir(q,q,&i)){
				alist=GetMemory(sizeof(a_flist)*MAXALBUM);
				trackstoplay=0;
				trackplaying=0;
				DIR djd;
				djd.pat="*.mid";
				if(!CurrentLinePtr)MMPrintString("Directory found - commencing player\r\n");
				FSerror = f_opendir(&djd, q);
				for(;;){
					fr=f_readdir(&djd, &fno);
					if (fr != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
					// Get a directory item
					if (pattern_matching(djd.pat, fno.fname, 0, 0)){
					// Get a directory item
						strcpy(alist[trackstoplay].fn,"B:");
						strcat(alist[trackstoplay].fn,q);
						strcat(alist[trackstoplay].fn,"/");
						strcat(alist[trackstoplay].fn,fno.fname);
						str_replace(alist[trackstoplay].fn, "//", "/",3);
						str_replace(alist[trackstoplay].fn, "/./", "/",3);
						if(!CurrentLinePtr){
							MMPrintString(fno.fname);
							PRet();
						}
						trackstoplay++;
						if(trackstoplay==MAXALBUM)break;
					}
				}
				trackstoplay--;
				f_closedir(&djd);
				midicallback(alist[trackplaying].fn);
				return;
    	    }
		}
        // open the file
        trackstoplay=0;
        trackplaying=0;
		memset(q,0,sizeof(q));
		getfullfilename(p,q);
		memmove(&q[2],q,strlen(q));
		q[1]=':';
		q[0]=FatFSFileSystem ? 'B' : 'A';
        midicallback(q);
        return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"HALT"))) {
        if(CurrentlyPlaying != P_MP3) error("Not playing an MP3");
    	int fnbr = FindFreeFileNbr();
		char *buff=GetTempMemory(STRINGSIZE);
		char *p=&WAVfilename[strlen(WAVfilename)];
		while(*p-- != '/'){}
		p+=2;
		strcpy(buff,"A:/");
		strcat(buff,p);
		str_replace(buff,".mp3",".mem",1);
		str_replace(buff,".MP3",".mem",1);
		str_replace(buff,".Mp3",".mem",1);
		str_replace(buff,".mP3",".mem",1);
    	if(!BasicFileOpen(buff, fnbr,  FA_WRITE | FA_CREATE_ALWAYS)) return;
		int i = (int)hal_fs_tell(hal_fds[WAV_fnbr]) + 1;
		i-=418;
		if(i<0)i=0;
		IntToStr(buff,i,10);
		FilePutStr(strlen(buff),buff,fnbr);
		FilePutChar(',',fnbr);
		FilePutStr(strlen(WAVfilename),WAVfilename,fnbr);
		FileClose(fnbr);
		CloseAudio(1);
		return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"CONTINUE"))) {
		if(!Option.AUDIO_MISO_PIN)error("Only available with VS1053 audio");
    	int fnbr = FindFreeFileNbr();
		char *p=(char *)getFstring(tp);
		char *buff=GetTempMemory(STRINGSIZE);
		if(strchr(p,'/') || strchr(p,':') || strchr(p,'\\') || strchr(p,'.'))error("Track name");
		strcpy(buff, "A:/");
		strcat(buff,p);
		strcat(buff,".mem");
		if(!ExistsFile(buff))error("Track name");
    	if(!BasicFileOpen(buff, fnbr,  FA_READ)) return;
		memset(buff,0,STRINGSIZE);
		hal_fs_read(hal_fds[fnbr], buff, 255);
		FileClose(fnbr);
		p=strchr(buff,',');
		p++;
		int num=atoi(buff);
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        trackstoplay=0;
        trackplaying=0;
		mp3callback(p,num);
		return;
	}
	if((tp = checkstring(cmdline, (unsigned char *)"MP3"))) {
        char *p;
		int FatFSFileSystemSave=FatFSFileSystem;
        int i __attribute((unused))=0;
        getargs(&tp, 3,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        if(!(argc == 1 || argc == 3)) error("Argument count");
		if (!HAL_PORT_HAS_MP3) {
            if(!Option.AUDIO_MISO_PIN)error("Only available with VS1053 audio");
        }
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use for $",PlayingStr[CurrentlyPlaying]);

         p = (char *)getFstring(argv[0]);                                    // get the file name
		char q[FF_MAX_LFN]={0};
		getfullfilename(p,q);
        if(!InitSDCard()) return;
		if(!FatFSFileSystem && toupper(p[0])=='B' && p[1]==':')FatFSFileSystem=1;
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if(argc == 3) {
			if(!CurrentLinePtr)error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);					// get the interrupt location
            InterruptUsed = true;
        }
		if(FatFSFileSystem){
			FRESULT fr;
			FILINFO fno;
			int i;
			if(ExistsDir(q,q,&i)){
				alist=GetMemory(sizeof(a_flist)*MAXALBUM);
				trackstoplay=0;
				trackplaying=0;
				DIR djd;
				djd.pat="*.mp3";
				if(!CurrentLinePtr)MMPrintString("Directory found - commencing player\r\n");
				FSerror = f_opendir(&djd, q);
				for(;;){
					fr=f_readdir(&djd, &fno);
					if (fr != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
					// Get a directory item
					if (pattern_matching(djd.pat, fno.fname, 0, 0)){
					// Get a directory item
						strcpy(alist[trackstoplay].fn,"B:");
						strcat(alist[trackstoplay].fn,q);
						strcat(alist[trackstoplay].fn,"/");
						strcat(alist[trackstoplay].fn,fno.fname);
						str_replace(alist[trackstoplay].fn, "//", "/",3);
						str_replace(alist[trackstoplay].fn, "/./", "/",3);
						if(!CurrentLinePtr){
							MMPrintString(fno.fname);
							PRet();
						}
						trackstoplay++;
						if(trackstoplay==MAXALBUM)break;
					}
				}
				trackstoplay--;
				f_closedir(&djd);
				mp3callback(alist[trackplaying].fn,0);
				return;
    	    }
			FatFSFileSystem=FatFSFileSystemSave;
		}
        // open the file
        trackstoplay=0;
        trackplaying=0;
		memset(q,0,sizeof(q));
		getfullfilename(p,q);
		memmove(&q[2],q,strlen(q));
		q[1]=':';
		q[0]=FatFSFileSystem ? 'B' : 'A';
        mp3callback(q,0);
        return;
	}
    if((tp = checkstring(cmdline, (unsigned char *)"MODFILE"))) {
        getargs(&tp, 3,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        char *p;
        int i __attribute((unused))=0,fsize;
        modfilesamplerate=22050;
		if(CurrentlyPlaying==P_WAVOPEN)CloseAudio(1);
        if(CurrentlyPlaying != P_NOTHING) error("Sound output in use");
        if(!InitSDCard()) return;
        p = (char *)getFstring(argv[0]);                                    // get the file name
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if(argc==3){
			if(!CurrentLinePtr)error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);					// get the interrupt location
            InterruptUsed = true;
        }
		if(!Option.AUDIO_MISO_PIN){
			if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".MOD");
			char q[FF_MAX_LFN]={0};
			getfullfilename(p,q);
			memmove(&q[2],q,strlen(q));
			q[1]=':';
			q[0]=FatFSFileSystem ? 'B' : 'A';
			strcpy(WAVfilename,q);
			if(audio_stream_play_mod_noloop(q, argc == 3) != 0) error("Cannot play file");
			audio_stream_service();
			return;
		}
		/* VS1053 MOD playback still uses the Pico-local hxcmod path. */
		if(!(modbuff || PSRAMsize))error("Mod playback not enabled");
        sbuff1 = GetMemory(MOD_BUFFER_SIZE);
        sbuff2 = GetMemory(MOD_BUFFER_SIZE);
        ubuff1 = (uint16_t *)sbuff1;
        ubuff2 = (uint16_t *)sbuff2;
        g_buff1 = (int16_t *)sbuff1;
        g_buff2 = (int16_t *)sbuff2;
        // open the file
        if(strchr((char *)p, '.') == NULL) strcat((char *)p, ".MOD");
		char q[FF_MAX_LFN]={0};
		getfullfilename(p,q);
		memmove(&q[2],q,strlen(q));
		q[1]=':';
		q[0]=FatFSFileSystem ? 'B' : 'A';
		strcpy(WAVfilename,q);
        WAV_fnbr = FindFreeFileNbr();
        if(!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
		if(argc==3)noloop=1;
		else noloop=0;
        i=0;
		fsize = (int)hal_fs_size(hal_fds[WAV_fnbr]);
		int alreadythere=1;
		/* PSRAM-less targets always take the flash-check branch (PSRAMsize=0);
		 * the PSRAM-load branch is live only on RP2350 with PSRAM detected. */
		if(!PSRAMsize){
			if(RoundUpK4(fsize)>1024*Option.modbuffsize)error("File too large for modbuffer");
			char *check=modbuff;
			while(!FileEOF(WAV_fnbr)) {
				if(*check++ != FileGetChar(WAV_fnbr)){
					alreadythere=0;
					break;
				}
			}
		} else {
			modbuff=GetMemory(RoundUpK4(fsize));
			positionfile(WAV_fnbr,0);
			char *r=modbuff;
			while(!FileEOF(WAV_fnbr)) {
				*r++=FileGetChar(WAV_fnbr);
			}
		}
		if(!alreadythere){
			unsigned char *r = GetTempMemory(256);
			positionfile(WAV_fnbr,0);
			uint32_t j = RoundUpK4(TOP_OF_SYSTEM_FLASH);
			fileio_flash_write_begin();
			hal_flash_erase(j, RoundUpK4(fsize));
			fileio_flash_write_end();
			while(!FileEOF(WAV_fnbr)) { 
				memset(r,0,256) ;
				for(i=0;i<256;i++) {
					if(FileEOF(WAV_fnbr))break;
					r[i] = FileGetChar(WAV_fnbr);
				}  
				fileio_flash_write_begin();
				hal_flash_program(j, (uint8_t *)r, 256);
				fileio_flash_write_end();
				routinechecks();
				j+=256;
			}
			FileClose(WAV_fnbr);
		}
        FileClose(WAV_fnbr);
		mcontext=GetMemory(sizeof(modcontext));
        hxcmod_init( mcontext );
        hxcmod_setcfg(mcontext, modfilesamplerate,1,1 );
		hxcmod_load( mcontext, (void*)modbuff, fsize );
		if(!mcontext->mod_loaded)error("Load failed");
		if(!CurrentLinePtr){
			MMPrintString("Playing ");MMPrintString((char *)mcontext->song.title);PRet();
		}
		if(Option.AUDIO_MISO_PIN){
			playvs1053(P_MOD);
			return;
		} else {
	        hxcmod_fillbuffer( mcontext, (msample*)sbuff1, MOD_BUFFER_SIZE/8, NULL, noloop );
		}
        int count = MOD_BUFFER_SIZE/4;
		if(Option.audio_i2s_bclk)i2sconvert(g_buff1, (int16_t *)sbuff1, count);
		else iconvert((uint16_t *)ubuff1, (int16_t *)sbuff1, count);
        nchannels=2;
        CurrentlyPlaying = P_MOD;
        playreadcomplete=0;
        setrate(44100);
		audiorepeat=2;
        pico_audio_publish_initial_target(1, count);
    	pwm_set_irq0_enabled(AUDIO_SLICE, true);
		pwm_set_enabled(AUDIO_SLICE, true); 
		Timer1=500;
		while(Timer1){
			checkWAVinput();
			ProcessWeb(1);
		}
        return;
    }
    if((tp = checkstring(cmdline, (unsigned char *)"MODSAMPLE"))) {
        unsigned short sampnum, seffectnum;
        unsigned char volume;
        unsigned int samprate, period;
        getargs(&tp, 5,(unsigned char *)",");                                  // this MUST be the first executable line in the function
        if(!(argc == 5 || argc == 3)) error("Argument count");

        if(!(CurrentlyPlaying == P_MOD)) error("Samples play over MOD file");

        sampnum = getint(argv[0],1,32) - 1;

        seffectnum = getint(argv[2],1,NUMMAXSEFFECTS) - 1;

        volume=63;
        if(argc >= 5 && *argv[4]) {
        	volume = getint(argv[4],0,64) - 1;
        	if (volume <0 ) volume = 0;
        }
    	samprate = 16000;

        period = 3579545 / samprate;

        hxcmod_playsoundeffect( mcontext, sampnum, seffectnum, volume, period );
        return;
    }
    error("Unknown command");
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

/******************************************************************************************
Timer interrupt.
Used to send data to the DAC
*******************************************************************************************/

/******************************************************************************************
Stop playing the music or toneb:

*******************************************************************************************/
void StopAudio(void) {

	if(CurrentlyPlaying != P_NOTHING ) {
		pwm_set_irq0_enabled(AUDIO_SLICE, false);
		pwm_clear_irq(AUDIO_SLICE);
		uSec(100); //
		if(!(Option.audio_i2s_bclk))
		{
			int ramptime = PWM_FREQ > 0 ? (1000000 / PWM_FREQ) - 1 : 0;
			uint32_t rr,r=right;
			uint32_t ll,l=left;
			uint32_t m=2000;
			l=m-l;
			r=m-r;
			for(int i=100;i>=0;i--){
				ll=(uint32_t)((int)m-(int)l*i/100);
				rr=(uint32_t)((int)m-(int)r*i/100);
				AudioOutput(ll,rr);
				uSec(ramptime);
			}
			CurrentlyPlaying = P_STOP;
			uSec(ramptime*2);
			setrate(PWM_FREQ);
		}
        ppos=0;
        if(Option.AUDIO_MISO_PIN && (CurrentlyPlaying == P_TONE || CurrentlyPlaying==P_SOUND))CurrentlyPlaying = P_WAVOPEN;
		else CurrentlyPlaying = P_NOTHING;
    }
	SoundPlay = 0;
}

int hal_audio_sample_begin(int sample_rate_hz) {
	if(Option.AUDIO_MISO_PIN) return -1;
	if(!(Option.AUDIO_L || Option.AUDIO_CLK_PIN || Option.audio_i2s_bclk)) return -1;
	FreeMemorySafe((void **)&sbuff1);
	FreeMemorySafe((void **)&sbuff2);
	sbuff1 = GetMemory(WAV_BUFFER_SIZE);
	sbuff2 = GetMemory(WAV_BUFFER_SIZE);
	ubuff1 = (uint16_t *)sbuff1;
	ubuff2 = (uint16_t *)sbuff2;
	g_buff1 = (int16_t *)sbuff1;
	g_buff2 = (int16_t *)sbuff2;
	clear_sample_buffer_state(0);
	mono = 0;
	audiorepeat = 1;
	int actualrate = sample_rate_hz;
	if(Option.AUDIO_L) {
		while(actualrate < 32000) {
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
	clear_sample_buffer_state(1);
}

void hal_audio_sample_eof(void) {
	playreadcomplete = 1;
}

static int pico_audio_stream_buffer_capacity_frames(void) {
	return WAV_BUFFER_SIZE / (int)(sizeof(int16_t) * 2);
}

static int pico_audio_stream_claim_target(void) {
	int target = 0;
	uint32_t save = save_and_disable_interrupts();
	if(audio_shared_stream_active && !audio_shared_acquired_target) {
		if(swingbuf == 0 && bcount[1] == 0 && bcount[2] == 0) {
			target = 1;
		} else if(swingbuf != nextbuf) {
			target = (swingbuf == 2) ? 1 : 2;
			if(bcount[target] != 0) target = 0;
		}
	}
	restore_interrupts(save);
	return target;
}

static void pico_audio_stream_convert_buffer(char *dst, int samples) {
	if(Option.audio_i2s_bclk) {
		i2sconvert((int16_t *)dst, (int16_t *)dst, samples);
	} else {
		iconvert((uint16_t *)dst, (int16_t *)dst, samples);
	}
}

static int pico_audio_stream_publish_target(int target, int samples) {
	if(target < 1 || target > 2 || samples <= 0) return 0;
	int published = 0;
	uint32_t save = save_and_disable_interrupts();
	if(audio_shared_stream_active && bcount[target] == 0) {
		wav_filesize = samples;
		if(swingbuf == 0) {
			swingbuf = target;
			nextbuf = (target == 1) ? 2 : 1;
			ppos = 0;
		} else {
			nextbuf = swingbuf;
		}
		/* bcount[target] publishes the completed buffer to the PWM ISR.
		 * Keep it last so the ISR cannot observe a partially filled buffer. */
		bcount[target] = samples;
		if(swingbuf == target) {
			pwm_set_irq0_enabled(AUDIO_SLICE, true);
			pwm_set_enabled(AUDIO_SLICE, true);
		}
		published = 1;
	}
	restore_interrupts(save);
	return published;
}

static void pico_audio_publish_initial_target(int target, int count) {
	if(target < 1 || target > 2) return;
	int other = (target == 1) ? 2 : 1;
	uint32_t save = save_and_disable_interrupts();
	bcount[other] = 0;
	wav_filesize = count;
	swingbuf = target;
	nextbuf = other;
	ppos = 0;
	/* bcount[target] is the completed-buffer publication flag for the ISR.
	 * It is assigned last after the foreground fill/convert has completed. */
	bcount[target] = count;
	restore_interrupts(save);
}

static void pico_audio_publish_refill_target(int target, int count) {
	if(target < 1 || target > 2) return;
	uint32_t save = save_and_disable_interrupts();
	wav_filesize = count;
	nextbuf = swingbuf;
	/* bcount[target] is the completed-buffer publication flag for the ISR.
	 * It is assigned last after the foreground fill/convert has completed. */
	bcount[target] = count;
	restore_interrupts(save);
}

int hal_audio_sample_space(void) {
	int capacity = pico_audio_stream_buffer_capacity_frames();
	if(!audio_shared_stream_active) return 0;
	if(swingbuf == 0 && bcount[1] == 0 && bcount[2] == 0) return capacity;
	if(swingbuf != nextbuf) {
		int target = (swingbuf == 2) ? 1 : 2;
		if(bcount[target] == 0) return capacity;
	}
	return 0;
}

int hal_audio_sample_queued(void) {
	int samples = 0;
	if(bcount[1] > 0) samples += bcount[1];
	if(bcount[2] > 0) samples += bcount[2];
	if(swingbuf == 1 && ppos < bcount[1]) samples -= ppos;
	else if(swingbuf == 2 && ppos < bcount[2]) samples -= ppos;
	if(samples < 0) samples = 0;
	return samples / 2;
}

int hal_audio_sample_push(const int16_t *frames, int frame_count) {
	if(!audio_shared_stream_active || frame_count <= 0 || frames == NULL) return 0;
	int capacity = pico_audio_stream_buffer_capacity_frames();
	int frames_to_copy = frame_count > capacity ? capacity : frame_count;
	int target = pico_audio_stream_claim_target();
	if(!target) return 0;
	char *dst = (target == 1) ? sbuff1 : sbuff2;
	if(!dst) return 0;
	memcpy(dst, frames, (size_t)frames_to_copy * 2u * sizeof(int16_t));
	int samples = frames_to_copy * 2;
	pico_audio_stream_convert_buffer(dst, samples);
	if(!pico_audio_stream_publish_target(target, samples)) return 0;
	return frames_to_copy;
}

int hal_audio_sample_acquire(int16_t **frames, int *frame_capacity) {
	if(!audio_shared_stream_active || frames == NULL || frame_capacity == NULL) return 0;
	int target = pico_audio_stream_claim_target();
	if(!target) return 0;
	char *dst = (target == 1) ? sbuff1 : sbuff2;
	if(!dst) return 0;
	uint32_t save = save_and_disable_interrupts();
	if(audio_shared_stream_active && !audio_shared_acquired_target && bcount[target] == 0) {
		audio_shared_acquired_target = target;
	} else {
		target = 0;
	}
	restore_interrupts(save);
	if(!target) return 0;
	*frames = (int16_t *)dst;
	*frame_capacity = pico_audio_stream_buffer_capacity_frames();
	return 1;
}

void hal_audio_sample_commit(int frame_count) {
	int target = audio_shared_acquired_target;
	if(!target || frame_count <= 0 || !audio_shared_stream_active) {
		audio_shared_acquired_target = 0;
		return;
	}
	int capacity = pico_audio_stream_buffer_capacity_frames();
	int frames_to_commit = frame_count > capacity ? capacity : frame_count;
	char *dst = (target == 1) ? sbuff1 : sbuff2;
	if(!dst) {
		audio_shared_acquired_target = 0;
		return;
	}
	int samples = frames_to_commit * 2;
	pico_audio_stream_convert_buffer(dst, samples);
	(void)pico_audio_stream_publish_target(target, samples);
	audio_shared_acquired_target = 0;
}

void *hal_audio_workmem_alloc(unsigned long bytes) {
	return GetMemory((int)bytes);
}

void *hal_audio_workmem_realloc(void *p, unsigned long bytes) {
	return ReAllocMemory(p, (int)bytes);
}

void hal_audio_workmem_free(void *p) {
	if(p) FreeMemory(p);
}

/******************************************************************************************
 * Maintain the WAV sample buffer
*******************************************************************************************/
void checkWAVinput(void){
	if(audio_stream_active()){
		e_CurrentlyPlaying was_playing = CurrentlyPlaying;
		audio_stream_service();
		if(!audio_stream_active() && CurrentlyPlaying == P_NOTHING &&
		   trackplaying < trackstoplay) {
			WAVcomplete = false;
			trackplaying++;
			if(was_playing == P_WAV) wavcallback(alist[trackplaying].fn);
			else if(was_playing == P_FLAC) flaccallback(alist[trackplaying].fn);
			else if(was_playing == P_MP3) mp3callback(alist[trackplaying].fn,0);
		}
		return;
	}
    audio_checks();
	if(playreadcomplete==1)return;
    if(swingbuf != nextbuf){ //IR has moved to next buffer
		if(Option.AUDIO_MISO_PIN){
			if(CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV || (CurrentlyPlaying == P_MP3 && Option.AUDIO_MISO_PIN) ||CurrentlyPlaying == P_MIDI){
				if(swingbuf==2){
					int count = (int)onRead(NULL,sbuff1,WAV_BUFFER_SIZE);
					pico_audio_publish_refill_target(1, count);
				} else {
					int count = (int)onRead(NULL,sbuff2,WAV_BUFFER_SIZE);
					pico_audio_publish_refill_target(2, count);
				}
				diskchecktimer=DISKCHECKRATE;
			} else if(CurrentlyPlaying == P_MOD){
				if(swingbuf==2){
					if(hxcmod_fillbuffer( mcontext, (msample*)sbuff1, WAV_BUFFER_SIZE/4,NULL, noloop ))playreadcomplete = 1;
					pico_audio_publish_refill_target(1, WAV_BUFFER_SIZE);
				} else {
					if(hxcmod_fillbuffer( mcontext, (msample*)sbuff2, WAV_BUFFER_SIZE/4,NULL, noloop ))playreadcomplete = 1;
					pico_audio_publish_refill_target(2, WAV_BUFFER_SIZE);
				}
			}
		} else {
			if(CurrentlyPlaying == P_MOD){
				if(swingbuf==2){
					if(hxcmod_fillbuffer( mcontext, (msample*)sbuff1, MOD_BUFFER_SIZE/4,NULL, noloop ))playreadcomplete = 1;
					int count = MOD_BUFFER_SIZE/2;
					if(Option.audio_i2s_bclk)i2sconvert(g_buff1, (int16_t *)sbuff1, count);
					else iconvert((uint16_t *)ubuff1, (int16_t *)sbuff1, count);
					pico_audio_publish_refill_target(1, count);
				} else {
					if(hxcmod_fillbuffer( mcontext, (msample*)sbuff2, MOD_BUFFER_SIZE/4,NULL, noloop ))playreadcomplete = 1;
					int count = MOD_BUFFER_SIZE/2;
					if(Option.audio_i2s_bclk)i2sconvert(g_buff2, (int16_t *)sbuff2, count);
					else iconvert((uint16_t *)ubuff2, (int16_t *)sbuff2, count);
					pico_audio_publish_refill_target(2, count);
				}
			} else if(CurrentlyPlaying == P_ARRAY){
				if(swingbuf==2){
					int count = (int)readarray(sbuff1);
					if(Option.audio_i2s_bclk) i2sconvert((int16_t *)sbuff1,(int16_t *)sbuff1,count);
					else iconvert(ubuff1, (int16_t *)sbuff1, count);
					pico_audio_publish_refill_target(1, count);
				} else {
					int count = (int)readarray(sbuff2);
					if(Option.audio_i2s_bclk) i2sconvert((int16_t *)sbuff2,(int16_t *)sbuff2,count);
					else iconvert(ubuff2, (int16_t *)sbuff2, count);
					pico_audio_publish_refill_target(2, count);
				}
			} 
		}
	}
    if(wav_filesize<=0 && (CurrentlyPlaying == P_WAV || (CurrentlyPlaying == P_FLAC)|| (CurrentlyPlaying == P_MP3) || (CurrentlyPlaying == P_MIDI))){
    	if(trackplaying==trackstoplay) {
    		playreadcomplete=1;
    	} else {
			if(CurrentlyPlaying == P_WAV){
    			trackplaying++;
    			wavcallback(alist[trackplaying].fn);
    		} else if(CurrentlyPlaying == P_FLAC){
    			trackplaying++;
    			flaccallback(alist[trackplaying].fn);
    		} else if(CurrentlyPlaying == P_MP3){
    			trackplaying++;
    			mp3callback(alist[trackplaying].fn,0);
    		} else if(CurrentlyPlaying == P_MIDI){
				if(Option.AUDIO_MISO_PIN && VSbuffer>32)return;
    			trackplaying++;
    			midicallback(alist[trackplaying].fn);
    		}
		}
    }
}
void audio_checks(void){
    if(playreadcomplete == 1) {
    	if(!(bcount[1] || bcount[2]) ){
			if(Option.AUDIO_MISO_PIN && VSbuffer>32)return;
			if(CurrentlyPlaying == P_MOD)FreeMemory((void *)mcontext);
            FreeMemorySafe((void **)&sbuff1);
            FreeMemorySafe((void **)&sbuff2);
            FreeMemorySafe((void **)&alist);
            if(Option.AUDIO_MISO_PIN){
    			pwm_set_irq0_enabled(AUDIO_SLICE, false);
				CurrentlyPlaying = P_NOTHING;
			} else StopAudio();
            if(CurrentlyPlaying != P_ARRAY)FileClose(WAV_fnbr);
            WAVcomplete = true;
         }
    }
}
