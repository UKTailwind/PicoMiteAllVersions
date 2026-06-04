' test_audio.bas -- exhaustive PLAY / synth test for MMBasic audio.
'
' Exercises the whole PLAY surface (TONE, SOUND, VOLUME, PAUSE/RESUME,
' STOP/CLOSE, every waveform, all channels, all 4 slots, polyphony, the
' user wavetable) and a broad slice of the BASIC API (SUB/FUNCTION, arrays,
' DATA/READ, SELECT CASE, ON ERROR, string + math functions, INKEY$).
'
' Run with:  RUN "test_audio"   -- PLAY SOUND is interpreter-only (not the VM).
'
' NOTE: the PicoCalc's built-in speaker is MONO (left channel). Sections
' that put sound only on the RIGHT channel are marked (headphones) -- use
' the headphone jack to hear the stereo separation.

OPTION DEFAULT FLOAT
CONST NTEST = 11

CLS
PRINT "=== MMBasic audio test ==="
PRINT "Device : "; MM.DEVICE$
PRINT "Version: "; MM.VER
PRINT
PRINT "Speaker is mono (left). Use headphones for"
PRINT "the (headphones) stereo sections."
PRINT
PRINT "Press any key to start each section, Q to quit."
PRINT
WaitKey

PLAY VOLUME 100, 100            ' known starting state

Section 1, "PLAY TONE"
  Doing "Mono 440 Hz, 500 ms (timed)"
  PLAY TONE 440, 440, 500
  PAUSE 700

  Doing "Sustain 220 Hz until STOP"
  PLAY TONE 220, 220
  PAUSE 800
  PLAY STOP

  Doing "Left 0 Hz / right 660 Hz (headphones)"
  PLAY TONE 0, 660, 700
  PAUSE 900

  Doing "Stereo split 300/500 Hz (headphones)"
  PLAY TONE 300, 500, 700
  PAUSE 900

  Doing "Mono sweep 200 -> 2000 Hz"
  FOR f = 200 TO 2000 STEP 40
    PLAY TONE f, f
    PAUSE 12
  NEXT f
  PLAY STOP

Section 2, "PLAY VOLUME (master)"
  Doing "440 Hz, ramp master volume 100 -> 0"
  PLAY TONE 440, 440
  FOR v = 100 TO 0 STEP -5
    PLAY VOLUME v, v
    PAUSE 60
  NEXT v
  PLAY STOP
  PLAY VOLUME 100, 100

  Doing "Pan left -> right via VOLUME (headphones)"
  PLAY TONE 440, 440
  FOR v = 0 TO 100 STEP 5
    PLAY VOLUME 100 - v, v
    PAUSE 40
  NEXT v
  PLAY STOP
  PLAY VOLUME 100, 100

Section 3, "PLAY SOUND -- every waveform"
  DIM wname$(5) = ("Sine S","Square Q","Triangle T","Sawtooth W","Periodic noise P","White noise N")
  wtypes$ = "SQTWPN"
  FOR i = 0 TO 5
    Doing wname$(i)
    PLAY SOUND 1, B, MID$(wtypes$, i + 1, 1), 330, 20
    PAUSE 1000
    PLAY SOUND 1, B, O
    PAUSE 150
  NEXT i

Section 4, "PLAY SOUND -- per-slot volume 0..25"
  FOR vol = 0 TO 25 STEP 5
    Doing "Square 440 Hz at volume " + STR$(vol)
    PLAY SOUND 1, B, Q, 440, vol
    PAUSE 600
  NEXT vol
  PLAY SOUND 1, B, O

Section 5, "PLAY SOUND -- frequency range"
  DIM freqs(6) = (20, 110, 440, 1000, 4000, 10000, 20000)
  FOR i = 0 TO 6
    Doing "Sine " + STR$(freqs(i)) + " Hz"
    PLAY SOUND 1, B, S, freqs(i), 22
    PAUSE 600
  NEXT i
  PLAY SOUND 1, B, O

Section 6, "Polyphony -- 4-slot chord"
  Doing "C major (C-E-G-C) across slots 1..4"
  PLAY SOUND 1, B, T, 261.63, 12     ' C4
  PLAY SOUND 2, B, T, 329.63, 12     ' E4
  PLAY SOUND 3, B, T, 392.00, 12     ' G4
  PLAY SOUND 4, B, T, 523.25, 12     ' C5
  PAUSE 1500
  ' release voices one at a time
  FOR s = 1 TO 4
    PLAY SOUND s, B, O
    PAUSE 300
  NEXT s

Section 7, "Stereo separation (headphones)"
  Doing "Slot 1 LEFT 523 Hz, slot 2 RIGHT 392 Hz"
  PLAY SOUND 1, L, Q, 523.25, 18
  PLAY SOUND 2, R, Q, 392.00, 18
  PAUSE 1200
  Doing "Channel as string M (both) reference tone"
  PLAY SOUND 1, L, O
  PLAY SOUND 2, R, O
  PLAY SOUND 1, "M", S, 440, 18
  PAUSE 800
  PLAY SOUND 1, B, O

Section 8, "User wavetable (PLAY LOAD SOUND + U)"
  Doing "Build a rich additive wave, load it, play an arpeggio"
  DIM tbl%(1023)
  FOR n% = 0 TO 1023
    tbl%(n%) =  UWave%(4*n%)
    tbl%(n%) = tbl%(n%) OR (UWave%(4*n%+1) << 16)
    tbl%(n%) = tbl%(n%) OR (UWave%(4*n%+2) << 32)
    tbl%(n%) = tbl%(n%) OR (UWave%(4*n%+3) << 48)
  NEXT n%
  ON ERROR SKIP
  PLAY LOAD SOUND tbl%()
  IF MM.ERRNO <> 0 THEN
    PRINT "  (LOAD SOUND unavailable here: "; MM.ERRMSG$; ")"
  ELSE
    DIM uf(4) = (220.00, 277.18, 329.63, 440.00, 554.37)   ' A major up an octave
    FOR i = 0 TO 4
      PLAY SOUND 1, B, U, uf(i), 22
      PAUSE 450
    NEXT i
    PLAY SOUND 1, B, U, 220.00, 22                          ' hold the root to hear the timbre
    PAUSE 1000
    PLAY SOUND 1, B, O
  ENDIF
  ON ERROR CLEAR

Section 9, "PAUSE / RESUME"
  Doing "Chord, PAUSE 500 ms, RESUME"
  PLAY SOUND 1, B, S, 440, 14
  PLAY SOUND 2, B, S, 554.37, 14
  PAUSE 600
  PLAY PAUSE
  PAUSE 500
  PLAY RESUME
  PAUSE 800
  PLAY STOP
  PLAY SOUND 1, B, O
  PLAY SOUND 2, B, O

Section 10, "Melody via DATA / READ"
  Doing "Tune through the SOUND engine"
  RESTORE notes
  DO
    READ nt, ms
    IF nt < 0 THEN EXIT DO
    IF nt = 0 THEN
      PLAY SOUND 1, B, O
    ELSE
      PLAY SOUND 1, B, T, nt, 18
    ENDIF
    PAUSE ms
  LOOP
  PLAY SOUND 1, B, O

Section 11, "File playback (WAV / MP3 / FLAC / MOD)"
  ' Decoded-file playback. Works fully on PicoMite devices; on the ESP32
  ' only WAV is wired so far; on stdio/host it skips. Files live next to
  ' this program (A: drive / SD card).
  PlayFile "chime.wav",  "WAV",     2500
  PlayFile "sweep.wav",  "WAV",     2500
  PlayFile "chime.mp3",  "MP3",     2500
  PlayFile "chime.flac", "FLAC",    2500
  PlayFile "laamaa.mod", "MODFILE", 6000

' --- all done: full teardown ---
PLAY STOP
PLAY CLOSE
CLS
PRINT "=== audio test complete ==="
PRINT "Synth + file-playback paths exercised."
END

notes:
DATA 261.63,250, 293.66,250, 329.63,250, 349.23,250
DATA 392.00,250, 440.00,250, 493.88,250, 523.25,400
DATA 0,120, 523.25,200, 392.00,200, 440.00,400
DATA 0,120, 261.63,500
DATA -1,0

' ---------------------------------------------------------------------------
' Helpers (exercise SUB / FUNCTION / SELECT CASE / INKEY$)
' ---------------------------------------------------------------------------

' One uint16 wavetable sample (0..4095 -> ~100..3900).
' Additive "reed organ" timbre: fundamental + a rolled-off harmonic
' series with the odd harmonics emphasised, for a bright, hollow tone
' that's clearly distinct from the built-in S/Q/T/W waveforms.
FUNCTION UWave%(n%)
  LOCAL FLOAT th, y
  th = 2 * PI * (n% MOD 4096) / 4096
  y =            SIN(th)
  y = y + 0.50 * SIN(2 * th)
  y = y + 0.66 * SIN(3 * th)
  y = y + 0.40 * SIN(4 * th)
  y = y + 0.28 * SIN(5 * th)
  y = y + 0.18 * SIN(6 * th)
  y = y + 0.12 * SIN(7 * th)
  y = 2000 + 560 * y                ' coeff sum 3.14 -> peak well inside range
  IF y < 100 THEN y = 100
  IF y > 3900 THEN y = 3900
  UWave% = INT(y)
END FUNCTION

SUB Section(n%, title$)
  PRINT
  PRINT "[" + STR$(n%) + "/" + STR$(NTEST) + "] " + title$
  WaitKey
END SUB

SUB Doing(label$)
  PRINT "   - " + label$
END SUB

' Play one decoded file in the background for `ms`, then stop. Skips
' cleanly (prints the reason) on ports without that codec or file.
SUB PlayFile(fname$, kind$, ms)
  Doing kind$ + ": " + fname$
  ON ERROR IGNORE
  SELECT CASE kind$
    CASE "WAV":     PLAY WAV fname$
    CASE "MP3":     PLAY MP3 fname$
    CASE "FLAC":    PLAY FLAC fname$
    CASE "MODFILE": PLAY MODFILE fname$
  END SELECT
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN
    PRINT "      (skipped: "; MM.ERRMSG$; ")"
    ON ERROR CLEAR
    EXIT SUB
  ENDIF
  PAUSE ms
  PLAY STOP
  PAUSE 200
END SUB

' Wait for a key; Q quits the whole program cleanly.
SUB WaitKey
  LOCAL k$
  DO
    k$ = INKEY$
  LOOP UNTIL k$ <> ""
  IF UCASE$(k$) = "Q" THEN
    PLAY STOP
    PLAY CLOSE
    PRINT
    PRINT "Aborted."
    END
  ENDIF
END SUB
