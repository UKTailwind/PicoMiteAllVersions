' test_audio.bas -- manual PLAY acceptance suite for one configured device.
'
' Run with:  RUN "test_audio"
'
' This suite runs automatically. It never changes OPTION AUDIO, never disables
' audio, and never intentionally reboots. Put the sample files listed in
' demos/sound/README.md next to this program before running file sections.

OPTION DEFAULT FLOAT
CONST NTEST = 9

toneIrq% = 0
modIrq% = 0
fileIrq% = 0
lastFileOk% = 0

monoWav$ = "mono.wav"
stereoWav$ = "stereo.wav"
mp3File$ = "short.mp3"
flacFile$ = "short.flac"
modFile$ = "short.mod"
longFile$ = "long.wav"

CLS
PRINT "=== MMBasic manual audio acceptance ==="
PRINT "Device : "; MM.DEVICE$
PRINT "Version: "; MM.VER
PRINT "Audio  : ";
PrintAudioInfo
PRINT "State  : ";
PrintSoundInfo
PRINT
PRINT "Use the already-configured audio backend. Do not run"
PRINT "OPTION AUDIO while this suite is running."
PRINT
PRINT "The suite auto-runs. Watch/listen for each expected result."
WaitAny

PLAY STOP
PLAY CLOSE
PLAY VOLUME 100, 100

IF StartSection%(1, "PLAY TONE control", "finite tones, stereo, zero-side silence, pause/resume/stop/close/new playback, interrupt if supported") THEN
  Doing "Mono 440 Hz for 500 ms"
  PLAY TONE 440, 440, 500
  PAUSE 750

  Doing "Left silent, right 660 Hz (headphones)"
  PLAY TONE 0, 660, 650
  PAUSE 850

  Doing "Right silent, left 660 Hz"
  PLAY TONE 660, 0, 650
  PAUSE 850

  Doing "Stereo split 300/500 Hz (headphones)"
  PLAY TONE 300, 500, 700
  PAUSE 900

  Doing "Indefinite tone, PAUSE, RESUME, STOP, CLOSE, then new tone"
  PLAY TONE 330, 330
  PAUSE 450
  PLAY PAUSE
  PAUSE 500
  PLAY RESUME
  PAUSE 500
  PLAY STOP
  PAUSE 200
  PLAY CLOSE
  PAUSE 200
  PLAY TONE 660, 660, 450
  PAUSE 650

  Doing "Completion interrupt on a short tone, if this port supports it"
  toneIrq% = 0
  ON ERROR IGNORE
  PLAY TONE 880, 880, 300, ToneDone
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      skipped interrupt: "; MM.ERRMSG$
    ON ERROR CLEAR
  ELSE
    PAUSE 800
    PRINT "      interrupt count = "; toneIrq%; " (expected 1)"
  ENDIF
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(2, "PLAY SOUND waveforms/routing", "S Q T W P N U waveforms, left/right/both routing, M alias, volume ramp") THEN
  wtypes$ = "SQTWPN"
  FOR i% = 0 TO 5
    Doing WaveName$(i%)
    PLAY SOUND 1, B, MID$(wtypes$, i% + 1, 1), 330, 20
    PAUSE 700
    PLAY SOUND 1, B, O
    PAUSE 120
  NEXT i%

  Doing "Routing: L 523 Hz, R 392 Hz, then B 440 Hz"
  PLAY SOUND 1, L, Q, 523.25, 18
  PLAY SOUND 2, R, Q, 392.00, 18
  PAUSE 1200
  PLAY SOUND 1, L, O
  PLAY SOUND 2, R, O
  PLAY SOUND 1, B, S, 440, 18
  PAUSE 700
  PLAY SOUND 1, B, O

  Doing "M alias should behave like both channels"
  PLAY SOUND 1, "M", T, 440, 18
  PAUSE 800
  PLAY SOUND 1, B, O

  Doing "Master volume ramp 100 -> 0 -> 100"
  PLAY SOUND 1, B, S, 440, 20
  FOR v% = 100 TO 0 STEP -10
    PLAY VOLUME v%, v%
    PAUSE 80
  NEXT v%
  FOR v% = 0 TO 100 STEP 10
    PLAY VOLUME v%, v%
    PAUSE 60
  NEXT v%
  PLAY SOUND 1, B, O
  PLAY VOLUME 100, 100

  Doing "User waveform U via PLAY LOAD SOUND"
  DIM baseProbe%(2)
  tableBase% = 0
  ON ERROR IGNORE
  baseProbe%(0) = 0
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    tableBase% = 1
    ON ERROR CLEAR
  ENDIF
  IF tableBase% = 0 THEN
    DIM tbl%(1023)
  ELSE
    DIM tbl%(1024)
  ENDIF
  tableLast% = tableBase% + 1023
  FOR n% = tableBase% TO tableLast%
    sample% = n% - tableBase%
    tbl%(n%) = UWave%(4 * sample%)
    tbl%(n%) = tbl%(n%) OR (UWave%(4 * sample% + 1) << 16)
    tbl%(n%) = tbl%(n%) OR (UWave%(4 * sample% + 2) << 32)
    tbl%(n%) = tbl%(n%) OR (UWave%(4 * sample% + 3) << 48)
  NEXT n%
  ON ERROR IGNORE
  PLAY LOAD SOUND tbl%()
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      skipped U waveform: "; MM.ERRMSG$
    ON ERROR CLEAR
  ELSE
    PLAY SOUND 1, B, U, 220, 22
    PAUSE 800
    PLAY SOUND 1, B, U, 330, 22
    PAUSE 800
    PLAY SOUND 1, B, O
  ENDIF
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(3, "PLAY SOUND slots/state", "four independent slots, all-off cleanup, STOP stale-mask regression") THEN
  Doing "Four-slot C major chord, then turn off one slot at a time"
  PLAY SOUND 1, B, T, 261.63, 12
  PLAY SOUND 2, B, T, 329.63, 12
  PLAY SOUND 3, B, T, 392.00, 12
  PLAY SOUND 4, B, T, 523.25, 12
  PAUSE 1400
  FOR s% = 1 TO 4
    PLAY SOUND s%, B, O
    PAUSE 280
  NEXT s%
  PRINT "      state after all slot O commands: ";
  PrintSoundInfo
  PRINT "      expected: OFF"

  Doing "PLAY STOP, then new slot playback, then O on that slot"
  PLAY SOUND 1, B, S, 440, 20
  PAUSE 500
  PLAY STOP
  PAUSE 200
  PLAY SOUND 2, B, S, 660, 20
  PAUSE 700
  PLAY SOUND 2, B, O
  PAUSE 250
  PRINT "      state after stale-mask probe: ";
  PrintSoundInfo
  PRINT "      expected: OFF"

  Doing "New playback after stale-mask probe"
  PLAY SOUND 3, B, Q, 550, 18
  PAUSE 600
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(4, "PLAY NOTE adapter", "note on/off, velocity zero off, four channels mapped to four synth slots, invalid channel error") THEN
  Doing "Channel 0 note on, then note off"
  PLAY NOTE ON 0, 60, 96
  PAUSE 700
  PLAY NOTE OFF 0, 60, 0
  PAUSE 250

  Doing "Velocity zero should act as note off"
  PLAY NOTE ON 1, 64, 96
  PAUSE 600
  PLAY NOTE ON 1, 64, 0
  PAUSE 300

  Doing "Four-note chord on channels 0..3"
  PLAY NOTE ON 0, 60, 80
  PLAY NOTE ON 1, 64, 80
  PLAY NOTE ON 2, 67, 80
  PLAY NOTE ON 3, 72, 80
  PAUSE 1400
  FOR c% = 0 TO 3
    PLAY NOTE OFF c%, 0, 0
    PAUSE 120
  NEXT c%
  PRINT "      state after NOTE OFF: ";
  PrintSoundInfo
  PRINT "      expected: OFF"

  Doing "Invalid non-MIDI channel 4 should report an error"
  ON ERROR IGNORE
  PLAY NOTE ON 4, 60, 80
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      expected error: "; MM.ERRMSG$
    ON ERROR CLEAR
  ELSE
    PRINT "      WARNING: channel 4 was accepted on this backend"
    PLAY STOP
  ENDIF
  ConfirmSection
ENDIF

IF StartSection%(5, "WAV file playback", "mono/stereo WAV, pause/resume, stop during startup/middle/near-end, immediate next after EOF") THEN
  PlayFile monoWav$, "WAV", 1800
  PlayFile stereoWav$, "WAV", 1800
  PauseResumeFile monoWav$, "WAV"
  StopDuringFile monoWav$, "WAV", 50, "startup"
  StopDuringFile monoWav$, "WAV", 600, "middle"
  StopDuringFile monoWav$, "WAV", 1000, "near-end before EOF"

  Doing "Immediate next WAV after expected EOF"
  StartFile monoWav$, "WAV"
  IF lastFileOk% THEN
    PAUSE 1800
    StartFile stereoWav$, "WAV"
    IF lastFileOk% THEN PAUSE 1800
  ENDIF
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(6, "MP3 and FLAC playback", "codec open, pause/resume, stop during startup/middle/near-end, immediate next after EOF") THEN
  PlayFile mp3File$, "MP3", 1800
  PauseResumeFile mp3File$, "MP3"
  StopDuringFile mp3File$, "MP3", 60, "startup"
  StopDuringFile mp3File$, "MP3", 600, "middle"
  StopDuringFile mp3File$, "MP3", 1000, "near-end before EOF"

  PlayFile flacFile$, "FLAC", 1800
  PauseResumeFile flacFile$, "FLAC"
  StopDuringFile flacFile$, "FLAC", 60, "startup"
  StopDuringFile flacFile$, "FLAC", 600, "middle"
  StopDuringFile flacFile$, "FLAC", 1000, "near-end before EOF"

  Doing "Immediate MP3 -> FLAC switch after expected MP3 EOF"
  StartFile mp3File$, "MP3"
  IF lastFileOk% THEN
    PAUSE 1800
    StartFile flacFile$, "FLAC"
    IF lastFileOk% THEN PAUSE 1800
  ENDIF
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(7, "MOD file playback", "MOD looping, stop during playback, optional no-loop interrupt with short MOD") THEN
  Doing "Looping MOD playback for 5 seconds"
  StartFile modFile$, "MODFILE"
  IF lastFileOk% THEN PAUSE 5000
  PLAY STOP
  PAUSE 250

  StopDuringFile modFile$, "MODFILE", 60, "startup"
  StopDuringFile modFile$, "MODFILE", 1800, "middle"

  Doing "MOD no-loop/completion interrupt if this backend supports it"
  modIrq% = 0
  ON ERROR IGNORE
  PLAY MODFILE modFile$, ModDone
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      skipped MOD interrupt: "; MM.ERRMSG$
    ON ERROR CLEAR
  ELSE
    PAUSE 7500
    PRINT "      MOD interrupt count = "; modIrq%; " (expected 1 if no-loop interrupt is supported)"
    IF modIrq% = 0 THEN PRINT "      expected on backends that loop MODFILE or lack the interrupt"
  ENDIF
  PLAY STOP
  ConfirmSection
ENDIF

IF StartSection%(8, "Open/close transitions", "repeated open-close and immediate next across WAV/MP3/FLAC/MOD") THEN
  Doing "Repeated WAV open/close"
  FOR i% = 1 TO 3
    StartFile monoWav$, "WAV"
    IF lastFileOk% THEN PAUSE 250
    PLAY CLOSE
    PAUSE 150
  NEXT i%

  Doing "Repeated MP3/FLAC/MOD open-close"
  OpenCloseOnce mp3File$, "MP3"
  OpenCloseOnce flacFile$, "FLAC"
  OpenCloseOnce modFile$, "MODFILE"

  Doing "Immediate sequence: WAV, MP3, FLAC, MOD"
  StartFile monoWav$, "WAV"
  IF lastFileOk% THEN PAUSE 500
  StartFile mp3File$, "MP3"
  IF lastFileOk% THEN PAUSE 500
  StartFile flacFile$, "FLAC"
  IF lastFileOk% THEN PAUSE 500
  StartFile modFile$, "MODFILE"
  IF lastFileOk% THEN PAUSE 900
  PLAY STOP
  PLAY CLOSE
  ConfirmSection
ENDIF

IF StartSection%(9, "Buffer stress", "rapid STOP during decode, small files in sequence, optional long file while polling keyboard") THEN
  Doing "Rapid STOP around short files"
  FOR i% = 1 TO 4
    StopDuringFile monoWav$, "WAV", 30, "rapid WAV"
    StopDuringFile mp3File$, "MP3", 30, "rapid MP3"
    StopDuringFile flacFile$, "FLAC", 30, "rapid FLAC"
  NEXT i%

  Doing "Short-file sequence without extra close"
  FOR i% = 1 TO 2
    StartFile monoWav$, "WAV": IF lastFileOk% THEN PAUSE 350
    StartFile stereoWav$, "WAV": IF lastFileOk% THEN PAUSE 350
    StartFile mp3File$, "MP3": IF lastFileOk% THEN PAUSE 350
    StartFile flacFile$, "FLAC": IF lastFileOk% THEN PAUSE 350
    StartFile modFile$, "MODFILE": IF lastFileOk% THEN PAUSE 350
  NEXT i%
  PLAY STOP

  Doing "Optional long file with keyboard/display activity"
  StartFile longFile$, "WAV"
  IF lastFileOk% THEN
    FOR i% = 1 TO 80
      PRINT "." ;
      k$ = INKEY$
      PAUSE 50
    NEXT i%
    PRINT
  ENDIF
  PLAY STOP
  PLAY CLOSE
  ConfirmSection
ENDIF

PLAY STOP
PLAY CLOSE
CLS
PRINT "=== audio acceptance complete ==="
PRINT "Final state: ";
PrintSoundInfo
END

ToneDone:
  toneIrq% = toneIrq% + 1
  IRETURN

ModDone:
  modIrq% = modIrq% + 1
  IRETURN

FileDone:
  fileIrq% = fileIrq% + 1
  IRETURN

' ---------------------------------------------------------------------------
' Helpers
' ---------------------------------------------------------------------------

FUNCTION UWave%(n%)
  LOCAL FLOAT th, y
  th = 2 * PI * (n% MOD 4096) / 4096
  y = SIN(th)
  y = y + 0.50 * SIN(2 * th)
  y = y + 0.66 * SIN(3 * th)
  y = y + 0.40 * SIN(4 * th)
  y = y + 0.28 * SIN(5 * th)
  y = y + 0.18 * SIN(6 * th)
  y = y + 0.12 * SIN(7 * th)
  y = 2000 + 560 * y
  IF y < 100 THEN y = 100
  IF y > 3900 THEN y = 3900
  UWave% = INT(y)
END FUNCTION

FUNCTION WaveName$(i%)
  SELECT CASE i%
    CASE 0: WaveName$ = "Sine S"
    CASE 1: WaveName$ = "Square Q"
    CASE 2: WaveName$ = "Triangle T"
    CASE 3: WaveName$ = "Sawtooth W"
    CASE 4: WaveName$ = "Periodic noise P"
    CASE 5: WaveName$ = "White noise N"
  END SELECT
END FUNCTION

FUNCTION StartSection%(n%, title$, expect$)
  PRINT
  PRINT "[" + STR$(n%) + "/" + STR$(NTEST) + "] " + title$
  PRINT "Expect: " + expect$
  PAUSE 1200
  StartSection% = 1
END FUNCTION

SUB ConfirmSection
  PLAY STOP
  PRINT "   section complete"
  PAUSE 700
END SUB

SUB Doing(label$)
  PRINT "   - " + label$
END SUB

SUB WaitAny
  PAUSE 1500
END SUB

SUB CleanQuit
  PLAY STOP
  PLAY CLOSE
  PRINT
  PRINT "Aborted."
  END
END SUB

SUB PrintAudioInfo
  ON ERROR IGNORE
  PRINT MM.INFO(AUDIO)
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "(unavailable: "; MM.ERRMSG$; ")"
    ON ERROR CLEAR
  ENDIF
END SUB

SUB PrintSoundInfo
  ON ERROR IGNORE
  PRINT MM.INFO(SOUND)
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "(unavailable: "; MM.ERRMSG$; ")"
    ON ERROR CLEAR
  ENDIF
END SUB

SUB StartFile(fname$, kind$)
  Doing kind$ + ": start " + fname$
  lastFileOk% = 0
  ON ERROR IGNORE
  SELECT CASE kind$
    CASE "WAV":     PLAY WAV fname$
    CASE "MP3":     PLAY MP3 fname$
    CASE "FLAC":    PLAY FLAC fname$
    CASE "MODFILE": PLAY MODFILE fname$
  END SELECT
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      skipped: "; MM.ERRMSG$
    ON ERROR CLEAR
  ELSE
    lastFileOk% = 1
  ENDIF
END SUB

SUB PlayFile(fname$, kind$, ms%)
  StartFile fname$, kind$
  IF lastFileOk% THEN PAUSE ms%
  PLAY STOP
  PAUSE 200
END SUB

SUB PauseResumeFile(fname$, kind$)
  Doing kind$ + ": pause/resume"
  StartFile fname$, kind$
  IF lastFileOk% THEN
    PAUSE 450
    PLAY PAUSE
    PAUSE 500
    PLAY RESUME
    PAUSE 900
  ENDIF
  PLAY STOP
  PAUSE 200
END SUB

SUB StopDuringFile(fname$, kind$, delay%, label$)
  Doing kind$ + ": STOP during " + label$
  StartFile fname$, kind$
  IF lastFileOk% THEN PAUSE delay%
  PLAY STOP
  PAUSE 120
END SUB

SUB OpenCloseOnce(fname$, kind$)
  StartFile fname$, kind$
  IF lastFileOk% THEN PAUSE 300
  PLAY CLOSE
  PAUSE 150
END SUB
