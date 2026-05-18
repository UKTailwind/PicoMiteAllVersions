' demo_melody.bas -- a short tune played with PLAY TONE
' Plays the opening of "Ode to Joy" using note frequencies (Hz).

DIM NOTES(12)
NOTES(0) = 329.63   ' E4
NOTES(1) = 329.63   ' E4
NOTES(2) = 349.23   ' F4
NOTES(3) = 392.00   ' G4
NOTES(4) = 392.00   ' G4
NOTES(5) = 349.23   ' F4
NOTES(6) = 329.63   ' E4
NOTES(7) = 293.66   ' D4
NOTES(8) = 261.63   ' C4
NOTES(9) = 261.63   ' C4
NOTES(10) = 293.66  ' D4
NOTES(11) = 329.63  ' E4

DIM DURS(12)
FOR I = 0 TO 11
    DURS(I) = 350
NEXT I
DURS(6) = 500      ' slight emphasis
DURS(11) = 700     ' final note longer

CLS
PRINT "Melody: Ode to Joy (first phrase)"
PRINT

FOR I = 0 TO 11
    PRINT "Note "; I + 1; " of 12  freq="; INT(NOTES(I)); " Hz"
    PLAY TONE NOTES(I), NOTES(I), DURS(I)
    PAUSE DURS(I) + 50
NEXT I

PLAY STOP
PRINT
PRINT "Fin."
