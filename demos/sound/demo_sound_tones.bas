' demo_sound_tones.bas -- PLAY TONE sweep
' Exercises: mono tone, stereo tone, duration, STOP.
'
' Run with:  RUN "demo_sound_tones"      (interpreter)
'        or  FRUN "demo_sound_tones"     (VM -- TONE/STOP only)

CLS
PRINT "Mono tone at 440 Hz for 500 ms..."
PLAY TONE 440, 440, 500
PAUSE 600

PRINT "Stereo split: 300 Hz left, 500 Hz right, 800 ms..."
PLAY TONE 300, 500, 800
PAUSE 900

PRINT "Sweep 200 -> 2000 Hz (mono)..."
FOR f = 200 TO 2000 STEP 50
    PLAY TONE f, f
    PAUSE 15
NEXT f
PLAY STOP

PRINT "Stereo chirp..."
FOR f = 100 TO 1200 STEP 25
    PLAY TONE f, 1300 - f
    PAUSE 12
NEXT f
PLAY STOP

PRINT "Done."
