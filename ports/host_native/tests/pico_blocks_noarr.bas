OPTION EXPLICIT
CONST PAD_ACCEL = 0.8
CONST W% = MM.HRES
CONST H% = MM.VRES
CONST HUDH% = 18
CONST COL_BG% = RGB(BLACK)
DIM INTEGER currentLevel%=1
DIM FLOAT bx!, by!
DIM INTEGER pw%, ph%
DIM INTEGER score%, lives%
DIM fps$

SUB BeepServe(): PLAY TONE 700,700 : PAUSE 40 : PLAY STOP : END SUB

pw% = W% \ 6 : IF pw% < 30 THEN pw% = 30
ph% = 6
score% = 0 : lives% = 3
fps$ = ""
PRINT "Init done"
PRINT "pw="; pw%; " ph="; ph%
BeepServe
PRINT "Done"
END
