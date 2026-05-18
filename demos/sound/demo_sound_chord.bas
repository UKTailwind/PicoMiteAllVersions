' demo_sound_chord.bas -- 4-slot polyphonic PLAY SOUND
' Plays a major 7th chord with different timbres per slot, stereo-panned.

CLS
PRINT "Cmaj7 chord, 4 voices, 4 seconds..."

' C major 7: C4=261.63, E4=329.63, G4=392.00, B4=493.88
PLAY SOUND 1, L, S, 261.63, 20    ' C4 sine left
PLAY SOUND 2, R, T, 329.63, 20    ' E4 triangle right
PLAY SOUND 3, L, Q, 392.00, 15    ' G4 square left
PLAY SOUND 4, R, W, 493.88, 12    ' B4 sawtooth right

PAUSE 4000

PRINT "Fade out with PLAY VOLUME..."
FOR v = 100 TO 0 STEP -4
    PLAY VOLUME v, v
    PAUSE 40
NEXT v

PLAY STOP
PLAY VOLUME 100, 100
PRINT "Done."
