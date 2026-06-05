/*
 * RP2 audio PLAY hooks and runtime glue.
 *
 * Shared/audio/Audio.c owns the BASIC PLAY dispatcher. This file provides
 * RP-only extension hooks, playlist handling, ARRAY playback, and MOD sample
 * buffer maintenance around the RP2 PWM/SPI DAC/I2S transport backend.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "ffconf.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "drivers/audio_rp2_pwm_i2s/audio_rp2_pwm_i2s.h"
#include "drivers/audio_vs1053/audio_vs1053.h"
#include "synth_pcm.h"
#include "audio_play_hooks.h"
#include "audio_play_common.h"
#include "audio_runtime.h"
#include "audio_stream.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "hardware/regs/addressmap.h" /* XIP_BASE */

/* Device body: VS1053 glue plus PWM/PIO-driven tone, array, MOD, and stream
 * playback. Software WAV/MP3/FLAC decoding lives in shared/audio/audio_stream.c. */
#define MOD_BUFFER_SIZE HAL_PORT_AUDIO_MOD_BUFFER_SIZE
#include "hxcmod.h"
extern BYTE MDD_SDSPI_CardDetectState(void);
#define MAXALBUM 20
extern int InitSDCard(void);
extern const int ErrorMap[21];
extern char * GetCWD(void);
extern void ErrorCheck(int fnbr);
static int arraypos = 0;
static int arraysize = 0;
short *leftarray = NULL, *rightarray = NULL;

// define the PWM output frequency for making a tone
volatile unsigned char PWM_count = 0;

volatile int v_left, v_right, vol_left = 100, vol_right = 100;
char * wav_buf;            // pointer to the buffer for received wav data
volatile int wav_filesize; // head and tail of the ring buffer for com1
volatile int tickspersample;
int PWM_FREQ;
volatile int swingbuf = 0, nextbuf = 0, playreadcomplete = 1;
char *sbuff1 = NULL, *sbuff2 = NULL;
uint16_t *ubuff1, *ubuff2;
int16_t *g_buff1, *g_buff2;
char * modbuff = NULL;
modcontext * mcontext = NULL;
int modfilesamplerate = 22050;
char * pbuffp;
void audio_checks(void);
uint16_t * playbuff;
int16_t * uplaybuff;
volatile int ppos = 0; // playing position for PLAY WAV
uint8_t nchannels;
/* The PWM ISR treats bcount[1] and bcount[2] as the publication flags for
 * completed ping-pong buffers. Foreground code must fill/convert a buffer
 * first, then assign bcount[target] only inside a short critical section. */
volatile uint32_t bcount[3] = {0, 0, 0};
volatile uint8_t audiorepeat = 1;
a_flist * alist = NULL;
uint8_t trackplaying = 0, trackstoplay = 0;
int noloop = 0;
char WAVfilename[FF_MAX_LFN] = {0};
//*************************************************************************************

//#define PSpeedDiv (PeripheralBusSpeed)/2
/* nulltable, squaretable, triangletable, sawtable, SineTable now
 * live in shared/audio/synth_pcm.c (shared with the ESP32 I2S sink). */
const char wavheader[44] = {0x52, 0x49, 0x46, 0x46,
                            0xFF, 0xFF, 0xFF, 0xFF,
                            0x57, 0x41, 0x56, 0x45,
                            0x66, 0x6d, 0x74, 0x20,
                            0x10, 0x00, 0x00, 0x00,
                            0x01, 0x00,
                            0x02, 0x00,
                            0x80, 0x3E, 0x00, 0x00,
                            0x0, 0xFA, 0x00, 0x00,
                            0x04, 0x00,
                            0x10, 0x00,
                            0x64, 0x61, 0x74, 0x61,
                            0xFF, 0xFF, 0xFF, 0xFF};
size_t onRead(void * userdata, char * pBufferOut, size_t bytesToRead) {
    ssize_t n = hal_fs_read(hal_fds[WAV_fnbr], pBufferOut, bytesToRead);
    if (n < 0) {
        FSerror = (filesource[WAV_fnbr] == FLASHFILE) ? (int)n : FR_DISK_ERR;
        ErrorCheck(WAV_fnbr);
        return 0;
    }
    if (filesource[WAV_fnbr] == FATFSFILE && !MDD_SDSPI_CardDetectState()) ErrorCheck(WAV_fnbr);
    FSerror = 0;
    return (size_t)n;
}
int audio_runtime_backend_close(int all, e_CurrentlyPlaying was_playing) {
    if (!PSRAMsize)
        modbuff = (Option.modbuff ? (char *)(XIP_BASE + RoundUpK4(TOP_OF_SYSTEM_FLASH)) : NULL);
    if (Option.audio_i2s_bclk) {
        rp2_audio_disable_output_irq_and_clear();
        rp2_audio_clear_sample_buffer_state(1);
    }
    audio_stream_stop();
    if (!Option.audio_i2s_bclk) {
        rp2_audio_clear_sample_buffer_state(0);
    }
    audio_runtime_backend_stop(was_playing);
    if (CurrentlyPlaying == P_WAVOPEN) CurrentlyPlaying = P_NOTHING;
    ForceFileClose(WAV_fnbr);
    FreeMemorySafe((void **)&sbuff1);
    FreeMemorySafe((void **)&sbuff2);
    FreeMemorySafe((void **)&noisetable);
    //FreeMemorySafe((void **)&usertable);
    usertable = NULL;
    FreeMemorySafe((void **)&mcontext);
    if (all) {
        FreeMemorySafe((void **)&alist);
        trackstoplay = 0;
        trackplaying = 0;
    }
    memset(WAVfilename, 0, sizeof(WAVfilename));
    WAVcomplete = true;
    FSerror = 0;
    if (PSRAMsize && was_playing == P_MOD) FreeMemorySafe((void **)&modbuff);
    int i;
    for (i = 0; i < MAXSOUNDS; i++) {
        sound_PhaseM_left[i] = 0;
        sound_PhaseM_right[i] = 0;
        sound_PhaseAC_left[i] = 0;
        sound_PhaseAC_right[i] = 0;
        sound_mode_left[i] = (uint16_t *)nulltable;
        sound_mode_right[i] = (uint16_t *)nulltable;
    }
    audio_vs1053_close_state();
    return 1;
}
void wavcallback(char * p) {
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".wav");
    if (CurrentlyPlaying == P_WAV) CloseAudio(0);
    strcpy(WAVfilename, p);
    if (!Option.AUDIO_MISO_PIN) {
        if (audio_stream_play_wav(p) == 0) audio_stream_service();
        return;
    }
    audio_vs1053_play_wav(p);
}
void mp3callback(char * p, int position) {
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".mp3");
    if (CurrentlyPlaying == P_MP3) {
        CloseAudio(0);
    }
    if (!Option.AUDIO_MISO_PIN) {
        if (Option.CPU_Speed < 200000) error("CPUSPEED >=200000 for MP3 playback");
        strcpy(WAVfilename, p);
        if (audio_stream_play_mp3(p) != 0) return;
        audio_stream_service();
        return;
    }
    audio_vs1053_play_mp3(p, position);
}
void flaccallback(char * p) {
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".flac");
    if (CurrentlyPlaying == P_FLAC) CloseAudio(0);
    strcpy(WAVfilename, p);
    if (!Option.AUDIO_MISO_PIN) {
        if (audio_stream_play_flac(p) == 0) audio_stream_service();
        return;
    }
    audio_vs1053_play_flac(p);
}
void rampvolume(int l, int r, int channel, int target) {
    if (optionfastaudio) {
        if (l) sound_v_left[channel] = target;
        if (r) sound_v_right[channel] = target;
    } else {
        int ramptime = 1000000 / PWM_FREQ + 2;
        if (l && r) {
            if (sound_v_left[channel] > target) {
                for (int i = sound_v_left[channel] - 1; i >= target; i--) {
                    sound_v_left[channel] = i;
                    sound_v_right[channel] = i;
                    uSec(ramptime);
                }
            } else {
                for (int i = sound_v_left[channel] + 1; i <= target; i++) {
                    sound_v_left[channel] = i;
                    sound_v_right[channel] = i;
                    uSec(ramptime);
                }
            }
        } else if (l) {
            if (sound_v_left[channel] > target) {
                for (int i = sound_v_left[channel] - 1; i >= target; i--) {
                    sound_v_left[channel] = i;
                    uSec(ramptime);
                }
            } else {
                for (int i = sound_v_left[channel] + 1; i <= target; i++) {
                    sound_v_left[channel] = i;
                    uSec(ramptime);
                }
            }
        } else if (r) {
            if (sound_v_right[channel] > target) {
                for (int i = sound_v_right[channel] - 1; i >= target; i--) {
                    sound_v_right[channel] = i;
                    uSec(ramptime);
                }
            } else {
                for (int i = sound_v_right[channel] + 1; i <= target; i++) {
                    sound_v_right[channel] = i;
                    uSec(ramptime);
                }
            }
        }
    }
}

unsigned int readarray(char * sbuff) {
    short * buff = (short *)sbuff;
    int count = arraysize - arraypos;
    if (count > WAV_BUFFER_SIZE / (sizeof(short) * 2)) count = WAV_BUFFER_SIZE / ((sizeof(short)) * 2);
    for (int i = arraypos; i < arraypos + count; i++) {
        *buff++ = leftarray[i];
        *buff++ = rightarray[i];
    }
    arraypos += count;
    if (count < WAV_BUFFER_SIZE / (sizeof(short) * 2)) playreadcomplete = 1;
    return count * 2;
};

static void rp2_play_load_sound(unsigned char * args) {
    if (Option.AUDIO_MISO_PIN) error("Not available with VS1053 audio");
    if (usertable != NULL) error("Already loaded");
    int64_t * aint;
    skipspace(args);
    int size = parseintegerarray(args, &aint, 1, 1, NULL, false);
    if (size != 1024) error("Array size");
    usertable = (unsigned short *)aint;
}

static void rp2_play_playlist_advance(int direction) {
    if (CurrentlyPlaying == P_FLAC) {
        if ((direction > 0 && trackplaying == trackstoplay) ||
            (direction < 0 && trackplaying == 0)) {
            if (!CurrentLinePtr) MMPrintString(direction > 0 ? "Last track is playing\r\n" : "First track is playing\r\n");
            return;
        }
        trackplaying += direction;
        flaccallback(alist[trackplaying].fn);
    } else if (CurrentlyPlaying == P_WAV) {
        if ((direction > 0 && trackplaying == trackstoplay) ||
            (direction < 0 && trackplaying == 0)) {
            if (!CurrentLinePtr) MMPrintString(direction > 0 ? "Last track is playing\r\n" : "First track is playing\r\n");
            return;
        }
        trackplaying += direction;
        wavcallback(alist[trackplaying].fn);
    } else if (CurrentlyPlaying == P_MP3) {
        if ((direction > 0 && trackplaying == trackstoplay) ||
            (direction < 0 && trackplaying == 0)) {
            if (!CurrentLinePtr) MMPrintString(direction > 0 ? "Last track is playing\r\n" : "First track is playing\r\n");
            return;
        }
        trackplaying += direction;
        mp3callback(alist[trackplaying].fn, 0);
    } else if (CurrentlyPlaying == P_MIDI) {
        if ((direction > 0 && trackplaying == trackstoplay) ||
            (direction < 0 && trackplaying == 0)) {
            if (!CurrentLinePtr) MMPrintString(direction > 0 ? "Last track is playing\r\n" : "First track is playing\r\n");
            return;
        }
        trackplaying += direction;
        audio_vs1053_play_midi_file(alist[trackplaying].fn);
    } else {
        error("Nothing to play");
    }
}

static void rp2_play_playlist_next(void) {
    rp2_play_playlist_advance(1);
}

static void rp2_play_playlist_previous(void) {
    rp2_play_playlist_advance(-1);
}

static void rp2_play_array(unsigned char * args) {
    float freq;
    getargs(&args, 11, (unsigned char *)",");
    if (!(argc == 11 || argc == 9 || argc == 7 || argc == 5)) error("Argument count");
    arraysize = parseintegerarray(argv[0], (int64_t **)&leftarray, 1, 1, NULL, false);
    if (parseintegerarray(argv[2], (int64_t **)&rightarray, 2, 1, NULL, false) != arraysize) error("Array size mismatch");
    arraysize *= 4;
    freq = getnumber(argv[4]);
    if (freq < 10.0 || freq > 48000.0) error("Invalid frequency 10.0 - 48000.0");
    arraypos = 0;
    if (argc >= 7 && *argv[6]) arraypos = getint(argv[6], 0, arraysize - 1);
    if (argc >= 9 && *argv[8]) arraysize = getint(argv[8], arraypos, arraysize);
    if (argc == 11) {
        if (!CurrentLinePtr) error("No program running");
        WAVInterrupt = (char *)GetIntAddress(argv[10]);
        WAVcomplete = false;
        InterruptUsed = true;
    }
    audiorepeat = 1;
    float actualrate = freq;
    while (actualrate < 32000) {
        actualrate += freq;
        audiorepeat++;
    }
    setrate(actualrate);
    FreeMemorySafe((void **)&sbuff1);
    FreeMemorySafe((void **)&sbuff2);
    sbuff1 = GetMemory(WAV_BUFFER_SIZE);
    sbuff2 = GetMemory(WAV_BUFFER_SIZE);
    ubuff1 = (uint16_t *)sbuff1;
    ubuff2 = (uint16_t *)sbuff2;
    mono = 0;
    g_buff1 = (int16_t *)sbuff1;
    g_buff2 = (int16_t *)sbuff2;
    playreadcomplete = 0;
    int count = (int)readarray(sbuff1);
    if (Option.audio_i2s_bclk)
        i2sconvert((int16_t *)sbuff1, (int16_t *)sbuff1, count);
    else
        iconvert(ubuff1, (int16_t *)sbuff1, count);
    CurrentlyPlaying = P_ARRAY;
    pico_audio_publish_initial_target(1, count);
    rp2_audio_enable_output_irq();
}

static void rp2_start_file_interrupt(int local_argc, unsigned char ** local_argv,
                                     int arg_index) {
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    if (local_argc == 3) {
        if (!CurrentLinePtr) error("No program running");
        WAVInterrupt = (char *)GetIntAddress(local_argv[arg_index]);
        InterruptUsed = true;
    }
}

static int rp2_prepare_playlist(const char * q, const char * pattern) {
    FRESULT fr;
    FILINFO fno;
    int i;
    if (!ExistsDir((char *)q, (char *)q, &i)) return 0;
    alist = GetMemory(sizeof(a_flist) * MAXALBUM);
    trackstoplay = 0;
    trackplaying = 0;
    DIR djd;
    djd.pat = (char *)pattern;
    if (!CurrentLinePtr) MMPrintString("Directory found - commencing player\r\n");
    FSerror = f_opendir(&djd, q);
    for (;;) {
        fr = f_readdir(&djd, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        if (pattern_matching(djd.pat, fno.fname, 0, 0)) {
            strcpy(alist[trackstoplay].fn, "B:");
            strcat(alist[trackstoplay].fn, q);
            strcat(alist[trackstoplay].fn, "/");
            strcat(alist[trackstoplay].fn, fno.fname);
            str_replace(alist[trackstoplay].fn, "//", "/", 3);
            str_replace(alist[trackstoplay].fn, "/./", "/", 3);
            if (!CurrentLinePtr) {
                MMPrintString(fno.fname);
                PRet();
            }
            trackstoplay++;
            if (trackstoplay == MAXALBUM) break;
        }
    }
    trackstoplay--;
    f_closedir(&djd);
    return 1;
}

static void rp2_play_wav(unsigned char * args) {
    int FatFSFileSystemSave = FatFSFileSystem;
    getargs(&args, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Argument count");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    char * p = (char *)getFstring(argv[0]);
    char q[FF_MAX_LFN] = {0};
    getfullfilename(p, q);
    if (!InitSDCard()) return;
    if (!FatFSFileSystem && toupper(p[0]) == 'B' && p[1] == ':') FatFSFileSystem = 1;
    rp2_start_file_interrupt(argc, argv, 2);
    if (FatFSFileSystem && rp2_prepare_playlist(q, "*.wav")) {
        wavcallback(alist[trackplaying].fn);
        return;
    }
    FatFSFileSystem = FatFSFileSystemSave;
    trackstoplay = 0;
    trackplaying = 0;
    memset(q, 0, sizeof(q));
    getfullfilename(p, q);
    memmove(&q[2], q, strlen(q));
    q[1] = ':';
    q[0] = FatFSFileSystem ? 'B' : 'A';
    wavcallback(q);
}

static void rp2_play_flac(unsigned char * args) {
    int FatFSFileSystemSave = FatFSFileSystem;
    getargs(&args, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Argument count");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    char * p = (char *)getFstring(argv[0]);
    char q[FF_MAX_LFN] = {0};
    getfullfilename(p, q);
    if (!InitSDCard()) return;
    if (!FatFSFileSystem && toupper(p[0]) == 'B' && p[1] == ':') FatFSFileSystem = 1;
    rp2_start_file_interrupt(argc, argv, 2);
    if (FatFSFileSystem && rp2_prepare_playlist(q, "*.flac")) {
        flaccallback(alist[trackplaying].fn);
        return;
    }
    FatFSFileSystem = FatFSFileSystemSave;
    trackstoplay = 0;
    trackplaying = 0;
    memset(q, 0, sizeof(q));
    getfullfilename(p, q);
    memmove(&q[2], q, strlen(q));
    q[1] = ':';
    q[0] = FatFSFileSystem ? 'B' : 'A';
    flaccallback(q);
}

static void rp2_play_mp3(unsigned char * args) {
    int FatFSFileSystemSave = FatFSFileSystem;
    getargs(&args, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Argument count");
    if (!HAL_PORT_HAS_MP3 && !Option.AUDIO_MISO_PIN) error("Only available with VS1053 audio");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    char * p = (char *)getFstring(argv[0]);
    char q[FF_MAX_LFN] = {0};
    getfullfilename(p, q);
    if (!InitSDCard()) return;
    if (!FatFSFileSystem && toupper(p[0]) == 'B' && p[1] == ':') FatFSFileSystem = 1;
    rp2_start_file_interrupt(argc, argv, 2);
    if (FatFSFileSystem && rp2_prepare_playlist(q, "*.mp3")) {
        mp3callback(alist[trackplaying].fn, 0);
        return;
    }
    FatFSFileSystem = FatFSFileSystemSave;
    trackstoplay = 0;
    trackplaying = 0;
    memset(q, 0, sizeof(q));
    getfullfilename(p, q);
    memmove(&q[2], q, strlen(q));
    q[1] = ':';
    q[0] = FatFSFileSystem ? 'B' : 'A';
    mp3callback(q, 0);
}

static void rp2_play_modfile(unsigned char * args) {
    getargs(&args, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Argument count");
    char * p;
    int i __attribute((unused)) = 0, fsize;
    modfilesamplerate = 22050;
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use");
    if (!InitSDCard()) return;
    p = (char *)getFstring(argv[0]);
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    if (argc == 3) {
        if (!CurrentLinePtr) error("No program running");
        WAVInterrupt = (char *)GetIntAddress(argv[2]);
        InterruptUsed = true;
    }
    if (!Option.AUDIO_MISO_PIN) {
        if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".MOD");
        char q[FF_MAX_LFN] = {0};
        getfullfilename(p, q);
        memmove(&q[2], q, strlen(q));
        q[1] = ':';
        q[0] = FatFSFileSystem ? 'B' : 'A';
        strcpy(WAVfilename, q);
        if (audio_stream_play_mod_noloop(q, argc == 3) != 0) error("Cannot play file");
        audio_stream_service();
        return;
    }
    if (!(modbuff || PSRAMsize)) error("Mod playback not enabled");
    sbuff1 = GetMemory(MOD_BUFFER_SIZE);
    sbuff2 = GetMemory(MOD_BUFFER_SIZE);
    ubuff1 = (uint16_t *)sbuff1;
    ubuff2 = (uint16_t *)sbuff2;
    g_buff1 = (int16_t *)sbuff1;
    g_buff2 = (int16_t *)sbuff2;
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".MOD");
    char q[FF_MAX_LFN] = {0};
    getfullfilename(p, q);
    memmove(&q[2], q, strlen(q));
    q[1] = ':';
    q[0] = FatFSFileSystem ? 'B' : 'A';
    strcpy(WAVfilename, q);
    WAV_fnbr = FindFreeFileNbr();
    if (!BasicFileOpen(p, WAV_fnbr, FA_READ)) return;
    noloop = (argc == 3) ? 1 : 0;
    fsize = (int)hal_fs_size(hal_fds[WAV_fnbr]);
    int alreadythere = 1;
    if (!PSRAMsize) {
        if (RoundUpK4(fsize) > 1024 * Option.modbuffsize) error("File too large for modbuffer");
        char * check = modbuff;
        while (!FileEOF(WAV_fnbr)) {
            if (*check++ != FileGetChar(WAV_fnbr)) {
                alreadythere = 0;
                break;
            }
        }
    } else {
        modbuff = GetMemory(RoundUpK4(fsize));
        positionfile(WAV_fnbr, 0);
        char * r = modbuff;
        while (!FileEOF(WAV_fnbr)) *r++ = FileGetChar(WAV_fnbr);
    }
    if (!alreadythere) {
        unsigned char * r = GetTempMemory(256);
        positionfile(WAV_fnbr, 0);
        uint32_t j = RoundUpK4(TOP_OF_SYSTEM_FLASH);
        fileio_flash_write_begin();
        hal_flash_erase(j, RoundUpK4(fsize));
        fileio_flash_write_end();
        while (!FileEOF(WAV_fnbr)) {
            memset(r, 0, 256);
            for (i = 0; i < 256; i++) {
                if (FileEOF(WAV_fnbr)) break;
                r[i] = FileGetChar(WAV_fnbr);
            }
            fileio_flash_write_begin();
            hal_flash_program(j, (uint8_t *)r, 256);
            fileio_flash_write_end();
            routinechecks();
            j += 256;
        }
        FileClose(WAV_fnbr);
    }
    FileClose(WAV_fnbr);
    mcontext = GetMemory(sizeof(modcontext));
    hxcmod_init(mcontext);
    hxcmod_setcfg(mcontext, modfilesamplerate, 1, 1);
    hxcmod_load(mcontext, (void *)modbuff, fsize);
    if (!mcontext->mod_loaded) error("Load failed");
    if (!CurrentLinePtr) {
        MMPrintString("Playing ");
        MMPrintString((char *)mcontext->song.title);
        PRet();
    }
    audio_vs1053_start_file(P_MOD);
}

static void rp2_play_file(const char * verb, unsigned char * args) {
    if (!strcasecmp(verb, "WAV")) {
        rp2_play_wav(args);
    } else if (!strcasecmp(verb, "MP3")) {
        rp2_play_mp3(args);
    } else if (!strcasecmp(verb, "FLAC")) {
        rp2_play_flac(args);
    } else if (!strcasecmp(verb, "MODFILE")) {
        rp2_play_modfile(args);
    } else {
        error("Unknown command");
    }
}

static void rp2_play_modsample(unsigned char * args) {
    unsigned short sampnum, seffectnum;
    unsigned char volume;
    unsigned int samprate, period;
    getargs(&args, 5, (unsigned char *)",");
    if (!(argc == 5 || argc == 3)) error("Argument count");
    if (CurrentlyPlaying != P_MOD) error("Samples play over MOD file");
    sampnum = getint(argv[0], 1, 32) - 1;
    seffectnum = getint(argv[2], 1, NUMMAXSEFFECTS) - 1;
    volume = 63;
    if (argc >= 5 && *argv[4]) {
        volume = getint(argv[4], 0, 64) - 1;
        if (volume < 0) volume = 0;
    }
    samprate = 16000;
    period = 3579545 / samprate;
    hxcmod_playsoundeffect(mcontext, sampnum, seffectnum, volume, period);
}

const audio_play_hooks_t * audio_play_hooks_get(void) {
    static const audio_play_hooks_t hooks = {
        .play_load_sound = rp2_play_load_sound,
        .play_note = audio_vs1053_play_note,
        .play_file = rp2_play_file,
        .play_midifile = audio_vs1053_play_midifile,
        .play_midi = audio_vs1053_play_midi,
        .play_stream = audio_vs1053_play_stream,
        .play_array = rp2_play_array,
        .play_halt = audio_vs1053_play_halt,
        .play_continue = audio_vs1053_play_continue,
        .play_modsample = rp2_play_modsample,
        .play_playlist_next = rp2_play_playlist_next,
        .play_playlist_previous = rp2_play_playlist_previous,
    };
    return &hooks;
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
void audio_runtime_backend_stop(e_CurrentlyPlaying was_playing) {
    (void)was_playing;

    if (CurrentlyPlaying != P_NOTHING) {
        rp2_audio_disable_output_irq_and_clear();
        uSec(100); //
        if (!(Option.audio_i2s_bclk)) {
            int ramptime = PWM_FREQ > 0 ? (1000000 / PWM_FREQ) - 1 : 0;
            uint32_t rr, r = right;
            uint32_t ll, l = left;
            uint32_t m = 2000;
            l = m - l;
            r = m - r;
            for (int i = 100; i >= 0; i--) {
                ll = (uint32_t)((int)m - (int)l * i / 100);
                rr = (uint32_t)((int)m - (int)r * i / 100);
                rp2_audio_output(ll, rr);
                uSec(ramptime);
            }
            CurrentlyPlaying = P_STOP;
            uSec(ramptime * 2);
            setrate(PWM_FREQ);
        }
        ppos = 0;
        if (Option.AUDIO_MISO_PIN && (CurrentlyPlaying == P_TONE || CurrentlyPlaying == P_SOUND))
            CurrentlyPlaying = P_WAVOPEN;
        else
            CurrentlyPlaying = P_NOTHING;
    }
    SoundPlay = 0;
}

/******************************************************************************************
 * Maintain the WAV sample buffer
*******************************************************************************************/
static void pwm_audio_service(void) {
    if (audio_stream_active()) {
        e_CurrentlyPlaying was_playing = CurrentlyPlaying;
        audio_stream_service();
        if (!audio_stream_active() && CurrentlyPlaying == P_NOTHING &&
            trackplaying < trackstoplay) {
            WAVcomplete = false;
            trackplaying++;
            if (was_playing == P_WAV)
                wavcallback(alist[trackplaying].fn);
            else if (was_playing == P_FLAC)
                flaccallback(alist[trackplaying].fn);
            else if (was_playing == P_MP3)
                mp3callback(alist[trackplaying].fn, 0);
        }
        return;
    }
    audio_checks();
    if (playreadcomplete == 1) return;
    if (swingbuf != nextbuf) { //IR has moved to next buffer
        if (Option.AUDIO_MISO_PIN) {
            if (CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV || (CurrentlyPlaying == P_MP3 && Option.AUDIO_MISO_PIN) || CurrentlyPlaying == P_MIDI) {
                if (swingbuf == 2) {
                    int count = (int)onRead(NULL, sbuff1, WAV_BUFFER_SIZE);
                    pico_audio_publish_refill_target(1, count);
                } else {
                    int count = (int)onRead(NULL, sbuff2, WAV_BUFFER_SIZE);
                    pico_audio_publish_refill_target(2, count);
                }
                diskchecktimer = DISKCHECKRATE;
            } else if (CurrentlyPlaying == P_MOD) {
                if (swingbuf == 2) {
                    if (hxcmod_fillbuffer(mcontext, (msample *)sbuff1, WAV_BUFFER_SIZE / 4, NULL, noloop)) playreadcomplete = 1;
                    pico_audio_publish_refill_target(1, WAV_BUFFER_SIZE);
                } else {
                    if (hxcmod_fillbuffer(mcontext, (msample *)sbuff2, WAV_BUFFER_SIZE / 4, NULL, noloop)) playreadcomplete = 1;
                    pico_audio_publish_refill_target(2, WAV_BUFFER_SIZE);
                }
            }
        } else {
            if (CurrentlyPlaying == P_MOD) {
                if (swingbuf == 2) {
                    if (hxcmod_fillbuffer(mcontext, (msample *)sbuff1, MOD_BUFFER_SIZE / 4, NULL, noloop)) playreadcomplete = 1;
                    int count = MOD_BUFFER_SIZE / 2;
                    if (Option.audio_i2s_bclk)
                        i2sconvert(g_buff1, (int16_t *)sbuff1, count);
                    else
                        iconvert((uint16_t *)ubuff1, (int16_t *)sbuff1, count);
                    pico_audio_publish_refill_target(1, count);
                } else {
                    if (hxcmod_fillbuffer(mcontext, (msample *)sbuff2, MOD_BUFFER_SIZE / 4, NULL, noloop)) playreadcomplete = 1;
                    int count = MOD_BUFFER_SIZE / 2;
                    if (Option.audio_i2s_bclk)
                        i2sconvert(g_buff2, (int16_t *)sbuff2, count);
                    else
                        iconvert((uint16_t *)ubuff2, (int16_t *)sbuff2, count);
                    pico_audio_publish_refill_target(2, count);
                }
            } else if (CurrentlyPlaying == P_ARRAY) {
                if (swingbuf == 2) {
                    int count = (int)readarray(sbuff1);
                    if (Option.audio_i2s_bclk)
                        i2sconvert((int16_t *)sbuff1, (int16_t *)sbuff1, count);
                    else
                        iconvert(ubuff1, (int16_t *)sbuff1, count);
                    pico_audio_publish_refill_target(1, count);
                } else {
                    int count = (int)readarray(sbuff2);
                    if (Option.audio_i2s_bclk)
                        i2sconvert((int16_t *)sbuff2, (int16_t *)sbuff2, count);
                    else
                        iconvert(ubuff2, (int16_t *)sbuff2, count);
                    pico_audio_publish_refill_target(2, count);
                }
            }
        }
    }
    if (wav_filesize <= 0 && (CurrentlyPlaying == P_WAV || (CurrentlyPlaying == P_FLAC) || (CurrentlyPlaying == P_MP3) || (CurrentlyPlaying == P_MIDI))) {
        if (trackplaying == trackstoplay) {
            playreadcomplete = 1;
        } else {
            if (CurrentlyPlaying == P_WAV) {
                trackplaying++;
                wavcallback(alist[trackplaying].fn);
            } else if (CurrentlyPlaying == P_FLAC) {
                trackplaying++;
                flaccallback(alist[trackplaying].fn);
            } else if (CurrentlyPlaying == P_MP3) {
                trackplaying++;
                mp3callback(alist[trackplaying].fn, 0);
            } else if (CurrentlyPlaying == P_MIDI) {
                if (Option.AUDIO_MISO_PIN && VSbuffer > 32) return;
                trackplaying++;
                audio_vs1053_play_midi_file(alist[trackplaying].fn);
            }
        }
    }
}

void audio_runtime_backend_service(void) {
    pwm_audio_service();
}
void audio_checks(void) {
    if (playreadcomplete == 1) {
        if (!(bcount[1] || bcount[2])) {
            if (Option.AUDIO_MISO_PIN && VSbuffer > 32) return;
            if (CurrentlyPlaying == P_MOD) FreeMemory((void *)mcontext);
            FreeMemorySafe((void **)&sbuff1);
            FreeMemorySafe((void **)&sbuff2);
            FreeMemorySafe((void **)&alist);
            if (Option.AUDIO_MISO_PIN) {
                rp2_audio_disable_output_irq();
                CurrentlyPlaying = P_NOTHING;
            } else
                StopAudio();
            if (CurrentlyPlaying != P_ARRAY) FileClose(WAV_fnbr);
            WAVcomplete = true;
        }
    }
}
