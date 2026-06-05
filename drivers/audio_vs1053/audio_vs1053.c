#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ffconf.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "drivers/audio_rp2_pwm_i2s/audio_rp2_pwm_i2s.h"
#include "drivers/audio_vs1053/audio_vs1053.h"
#include "drivers/vs1053/VS1053.h"
#include "audio_runtime.h"
#include "hal/hal_time.h"

#define MAXALBUM 20

extern int InitSDCard(void);
extern size_t onRead(void * userdata, char * pBufferOut, size_t bytesToRead);
extern const char wavheader[44];

static int8_t s_xdcs = -1, s_xcs = -1, s_dreq = -1, s_xrst = -1;
static uint8_t s_midienabled = 0;

int streamsize = 0;
volatile int * streamwritepointer = NULL;
volatile int * streamreadpointer = NULL;
char * streambuffer = NULL;

static const char toneheader[44] = {
    0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0xFF,
    0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
    0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x44, 0xAC, 0x00, 0x00, 0x10, 0xB1, 0x02, 0x00,
    0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61,
    0xFF, 0xFF, 0xFF, 0xFF
};

static void audio_vs1053_open(void) {
    s_xcs = PinDef[Option.AUDIO_CS_PIN].GPno;
    s_xdcs = PinDef[Option.AUDIO_DCS_PIN].GPno;
    s_dreq = PinDef[Option.AUDIO_DREQ_PIN].GPno;
    s_xrst = PinDef[Option.AUDIO_RESET_PIN].GPno;
    VS1053(s_xcs, s_xdcs, s_dreq, s_xrst);
}

void audio_vs1053_close_state(void) {
    if (s_xdcs == -1) return;
    VS1053reset(s_xrst);
    s_xdcs = s_xcs = s_dreq = s_xrst = -1;
    s_midienabled = 0;
    streamsize = 0;
    streamwritepointer = NULL;
    streamreadpointer = NULL;
    streambuffer = NULL;
}

void audio_vs1053_start_file(int mode) {
    audio_vs1053_open();
    switchToMp3Mode();
    loadDefaultVs1053Patches();
    setVolumes(vol_left, vol_right);
    setrate(6000);
    int count;
    if (mode == P_MOD) {
        memcpy((char *)sbuff1, wavheader, sizeof(wavheader));
        count = 44;
    } else {
        sbuff1 = GetMemory(WAV_BUFFER_SIZE);
        sbuff2 = GetMemory(WAV_BUFFER_SIZE);
        count = (int)onRead(NULL, sbuff1, WAV_BUFFER_SIZE);
    }
    CurrentlyPlaying = mode;
    playreadcomplete = 0;
    pico_audio_publish_initial_target(1, count);
    rp2_audio_enable_output_irq();
    uint64_t t = hal_time_us_64();
    uSec(25);
    while (1) {
        checkWAVinput();
        uSec(25);
        if (hal_time_us_64() - t > 500000) break;
    }
}

void audio_vs1053_play_immediate(int play) {
    if (CurrentlyPlaying == P_WAVOPEN) return;
    audio_vs1053_open();
    switchToMp3Mode();
    loadDefaultVs1053Patches();
    setVolumes(vol_left, vol_right);
    setrate(PWM_FREQ);
    CurrentlyPlaying = play;
    playChunk((uint8_t *)toneheader, sizeof(toneheader));
}

void audio_vs1053_play_wav(char * path) {
    WAV_fnbr = FindFreeFileNbr();
    if (!BasicFileOpen(path, WAV_fnbr, FA_READ)) return;
    audio_vs1053_start_file(P_WAV);
}

void audio_vs1053_play_mp3(char * path, int position) {
    WAV_fnbr = FindFreeFileNbr();
    strcpy(WAVfilename, path);
    if (!BasicFileOpen(path, WAV_fnbr, FA_READ)) return;
    positionfile(WAV_fnbr, position);
    audio_vs1053_start_file(P_MP3);
}

void audio_vs1053_play_flac(char * path) {
    WAV_fnbr = FindFreeFileNbr();
    if (!BasicFileOpen(path, WAV_fnbr, FA_READ)) return;
    audio_vs1053_start_file(P_FLAC);
}

void audio_vs1053_play_midi_file(char * path) {
    if (strchr((char *)path, '.') == NULL) strcat((char *)path, ".mid");
    if (CurrentlyPlaying == P_MIDI) CloseAudio(0);
    WAV_fnbr = FindFreeFileNbr();
    if (!BasicFileOpen(path, WAV_fnbr, FA_READ)) return;
    strcpy(WAVfilename, path);
    audio_vs1053_start_file(P_MIDI);
}

void audio_vs1053_play_note(unsigned char * args) {
    unsigned char * xp;
    if ((xp = checkstring(args, (unsigned char *)"ON"))) {
        getargs(&xp, 5, (unsigned char *)",");
        if (argc != 5) error("Syntax");
        if (!s_midienabled) error("Midi output not enabled");
        uint8_t channel = getint(argv[0], 0, 15);
        uint8_t note = getint(argv[2], 0, 127);
        uint8_t velocity = getint(argv[4], 0, 127);
        noteOn(channel, note, velocity);
    } else if ((xp = checkstring(args, (unsigned char *)"OFF"))) {
        getargs(&xp, 5, (unsigned char *)",");
        if (!(argc == 5 || argc == 3)) error("Syntax");
        if (!s_midienabled) error("Midi output not enabled");
        uint8_t channel = getint(argv[0], 0, 15);
        uint8_t note = getint(argv[2], 0, 127);
        uint8_t velocity = 0;
        if (argc == 5) velocity = getint(argv[4], 0, 127);
        noteOff(channel, note, velocity);
    } else {
        error("Syntax");
    }
}

void audio_vs1053_play_stream(unsigned char * args) {
    getargs(&args, 5, (unsigned char *)",");
    if (argc != 5) error("Syntax");
    if (!Option.AUDIO_MISO_PIN) error("Only available with VS1053 audio");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    if (s_xdcs != -1) error("VS1053 already open");
    void * ptr1 = NULL;
    int64_t * aint;
    streamsize = parseintegerarray(argv[0], &aint, 1, 1, NULL, true) * 8;
    streambuffer = (char *)aint;
    ptr1 = findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
    if (g_vartbl[g_VarIndex].type & T_INT) {
        if (g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 2 must be an integer");
        streamreadpointer = (int *)ptr1;
    } else {
        error("Argument 2 must be an integer");
    }
    ptr1 = findvar(argv[4], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
    if (g_vartbl[g_VarIndex].type & T_INT) {
        if (g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 3 must be an integer");
        streamwritepointer = (int *)ptr1;
    } else {
        error("Argument 3 must be an integer");
    }
    audio_vs1053_open();
    switchToMp3Mode();
    loadDefaultVs1053Patches();
    setVolumes(vol_left, vol_right);
    MMPrintString("Stream output enabled\r\n");
    playreadcomplete = 0;
    CurrentlyPlaying = P_STREAM;
    setrate(16000);
    rp2_audio_enable_output_irq();
}

void audio_vs1053_play_midi(unsigned char * args) {
    unsigned char * xp;
    if ((xp = checkstring(args, (unsigned char *)"CMD"))) {
        getargs(&xp, 5, (unsigned char *)",");
        if (!s_midienabled) error("Midi output not enabled");
        if (!(argc == 5 || argc == 3)) error("Syntax");
        uint8_t cmd = getint(argv[0], 128, 255);
        uint8_t data1 = getint(argv[2], 0, 127);
        uint8_t data2 = 0;
        if (argc == 5) data2 = getint(argv[4], 0, 127);
        talkMIDI(cmd, data1, data2);
        return;
    }
    if ((xp = checkstring(args, (unsigned char *)"TEST"))) {
        getargs(&xp, 1, (unsigned char *)",");
        if (!s_midienabled) error("Midi output not enabled");
        miditest(getint(argv[0], 1, 3));
        return;
    }
    if (!Option.AUDIO_MISO_PIN) error("Only available with VS1053 audio");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    audio_vs1053_open();
    setVolumes(vol_left, vol_right);
    s_midienabled = 1;
    miditest(0);
    if (!CurrentLinePtr) MMPrintString("Real Time MIDI mode enabled\r\n");
}

void audio_vs1053_play_midifile(unsigned char * args) {
    getargs(&args, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Argument count");
    if (!Option.AUDIO_MISO_PIN) error("Only available with VS1053 audio");
    if (CurrentlyPlaying == P_WAVOPEN) CloseAudio(1);
    if (CurrentlyPlaying != P_NOTHING) error("Sound output in use for $", PlayingStr[CurrentlyPlaying]);
    if (!InitSDCard()) return;
    char * p = (char *)getFstring(argv[0]);
    char q[FF_MAX_LFN] = {0};
    getfullfilename(p, q);
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    if (argc == 3) {
        if (!CurrentLinePtr) error("No program running");
        WAVInterrupt = (char *)GetIntAddress(argv[2]);
        InterruptUsed = true;
    }
    if (FatFSFileSystem) {
        FRESULT fr;
        FILINFO fno;
        int i;
        if (ExistsDir(q, q, &i)) {
            alist = GetMemory(sizeof(a_flist) * MAXALBUM);
            trackstoplay = 0;
            trackplaying = 0;
            DIR djd;
            djd.pat = "*.mid";
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
            audio_vs1053_play_midi_file(alist[trackplaying].fn);
            return;
        }
    }
    trackstoplay = 0;
    trackplaying = 0;
    memset(q, 0, sizeof(q));
    getfullfilename(p, q);
    memmove(&q[2], q, strlen(q));
    q[1] = ':';
    q[0] = FatFSFileSystem ? 'B' : 'A';
    audio_vs1053_play_midi_file(q);
}

void audio_vs1053_play_halt(unsigned char * args) {
    (void)args;
    if (CurrentlyPlaying != P_MP3) error("Not playing an MP3");
    int fnbr = FindFreeFileNbr();
    char * buff = GetTempMemory(STRINGSIZE);
    char * p = &WAVfilename[strlen(WAVfilename)];
    while (*p-- != '/') {
    }
    p += 2;
    strcpy(buff, "A:/");
    strcat(buff, p);
    str_replace(buff, ".mp3", ".mem", 1);
    str_replace(buff, ".MP3", ".mem", 1);
    str_replace(buff, ".Mp3", ".mem", 1);
    str_replace(buff, ".mP3", ".mem", 1);
    if (!BasicFileOpen(buff, fnbr, FA_WRITE | FA_CREATE_ALWAYS)) return;
    int i = (int)hal_fs_tell(hal_fds[WAV_fnbr]) + 1;
    i -= 418;
    if (i < 0) i = 0;
    IntToStr(buff, i, 10);
    FilePutStr(strlen(buff), buff, fnbr);
    FilePutChar(',', fnbr);
    FilePutStr(strlen(WAVfilename), WAVfilename, fnbr);
    FileClose(fnbr);
    CloseAudio(1);
}

void audio_vs1053_play_continue(unsigned char * args) {
    if (!Option.AUDIO_MISO_PIN) error("Only available with VS1053 audio");
    int fnbr = FindFreeFileNbr();
    char * p = (char *)getFstring(args);
    char * buff = GetTempMemory(STRINGSIZE);
    if (strchr(p, '/') || strchr(p, ':') || strchr(p, '\\') || strchr(p, '.')) error("Track name");
    strcpy(buff, "A:/");
    strcat(buff, p);
    strcat(buff, ".mem");
    if (!ExistsFile(buff)) error("Track name");
    if (!BasicFileOpen(buff, fnbr, FA_READ)) return;
    memset(buff, 0, STRINGSIZE);
    hal_fs_read(hal_fds[fnbr], buff, 255);
    FileClose(fnbr);
    p = strchr(buff, ',');
    p++;
    int num = atoi(buff);
    WAVInterrupt = NULL;
    WAVcomplete = 0;
    trackstoplay = 0;
    trackplaying = 0;
    audio_vs1053_play_mp3(p, num);
}
