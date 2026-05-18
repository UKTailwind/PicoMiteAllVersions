' demo_sound_waves.bas -- PLAY SOUND all waveform types
' Cycles through every documented waveform on slot 1.
' Requires RUN (interpreter) -- PLAY SOUND isn't in the VM.

CLS
PRINT "Waveform demo"
PRINT

DIM LABELS$(5)
LABELS$(0) = "Sine (S)"
LABELS$(1) = "Square (Q)"
LABELS$(2) = "Triangle (T)"
LABELS$(3) = "Sawtooth (W)"
LABELS$(4) = "Periodic noise (P)"
LABELS$(5) = "White noise (N)"

TYPES$ = "SQTWPN"

FOR I = 0 TO 5
    PRINT "Playing: "; LABELS$(I)
    T$ = MID$(TYPES$, I + 1, 1)
    PLAY SOUND 1, B, T$, 440, 20
    PAUSE 1200
NEXT I

PLAY SOUND 1, B, O
PRINT
PRINT "Done."
