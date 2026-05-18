' demo_sound_sfx.bas -- classic arcade-style sound effects
' Shows how to build laser / explosion / coin pickups from PLAY SOUND.

CLS
PRINT "Arcade SFX demo"
PRINT

' --- Laser: fast downward frequency sweep on a square wave ---
PRINT "Laser..."
FOR f = 1200 TO 200 STEP -30
    PLAY SOUND 1, B, Q, f, 18
    PAUSE 8
NEXT f
PLAY SOUND 1, B, O
PAUSE 200

' --- Coin: two quick square pulses a fifth apart ---
PRINT "Coin..."
PLAY SOUND 1, B, Q, 987.77, 22   ' B5
PAUSE 80
PLAY SOUND 1, B, Q, 1318.51, 22  ' E6
PAUSE 160
PLAY SOUND 1, B, O
PAUSE 300

' --- Explosion: white noise with a volume decay ---
PRINT "Explosion..."
PLAY SOUND 1, B, N, 100, 25
FOR v = 25 TO 0 STEP -1
    PLAY SOUND 1, B, N, 100, v
    PAUSE 40
NEXT v
PLAY SOUND 1, B, O
PAUSE 200

' --- Stereo alarm: square LEFT alternating with RIGHT ---
PRINT "Alarm..."
FOR i = 1 TO 8
    PLAY SOUND 1, L, Q, 880, 20
    PLAY SOUND 2, R, O
    PAUSE 120
    PLAY SOUND 1, L, O
    PLAY SOUND 2, R, Q, 660, 20
    PAUSE 120
NEXT i
PLAY SOUND 1, L, O
PLAY SOUND 2, R, O

PRINT
PRINT "Done."
