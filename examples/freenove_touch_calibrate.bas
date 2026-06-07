' Freenove ESP32-S3 touch calibration.
' Tap each target once and lift. The fitted calibration is saved.

GUI DELETE ALL
CLS
COLOUR RGB(WHITE), RGB(BLACK)

N = 20
SumX = 0
SumY = 0
SumXX = 0
SumYY = 0
SumTX = 0
SumTY = 0
SumXTX = 0
SumYTY = 0

PRINT "TCAL_START"

FOR I = 1 TO N
  READ TX, TY
  GOSUB WaitRelease
  GOSUB DrawTarget
  GOSUB WaitTouch
  SumX = SumX + MX
  SumY = SumY + MY
  SumXX = SumXX + MX * MX
  SumYY = SumYY + MY * MY
  SumTX = SumTX + TX
  SumTY = SumTY + TY
  SumXTX = SumXTX + MX * TX
  SumYTY = SumYTY + MY * TY
  PRINT "P,";I;",";TX;",";TY;",";MX;",";MY
NEXT I

DenX = N * SumXX - SumX * SumX
DenY = N * SumYY - SumY * SumY
IF DenX = 0 OR DenY = 0 THEN
  PRINT "TCAL_FAIL"
  END
ENDIF

SX = (N * SumXTX - SumX * SumTX) / DenX
SY = (N * SumYTY - SumY * SumTY) / DenY
BX = (SumTX - SX * SumX) / N
BY = (SumTY - SY * SumY) / N
ZX = INT((-BX / SX) + 0.5)
ZY = INT((-BY / SY) + 0.5)
IX = INT(SX * 10000 + 0.5)
IY = INT(SY * 10000 + 0.5)

OPTION TOUCH CALIBRATE ZX, ZY, IX, IY

CLS
TEXT 8, 18, "Touch calibration saved", "LT", 1, 1, RGB(YELLOW), RGB(BLACK)
TEXT 8, 48, "X zero " + STR$(ZX), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 68, "Y zero " + STR$(ZY), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 88, "X scale " + STR$(IX), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 108, "Y scale " + STR$(IY), "LT", 1, 1, RGB(WHITE), RGB(BLACK)

PRINT "TCAL_OPTION,";ZX;",";ZY;",";IX;",";IY
PRINT "TCAL_DONE"
END

WaitRelease:
DO
  IF INKEY$ <> "" THEN END
  X = TOUCH(RAWX)
  Y = TOUCH(RAWY)
  IF X < 0 OR Y < 0 THEN EXIT
  PAUSE 5
LOOP
PAUSE 80
RETURN

WaitTouch:
SX0 = 0
SY0 = 0
C = 0
DO
  IF INKEY$ <> "" THEN END
  X = TOUCH(RAWX)
  Y = TOUCH(RAWY)
  IF X >= 0 AND Y >= 0 THEN EXIT
  PAUSE 1
LOOP
T0 = TIMER
DO WHILE TIMER - T0 < 60
  X = TOUCH(RAWX)
  Y = TOUCH(RAWY)
  IF X >= 0 AND Y >= 0 THEN
    SX0 = SX0 + X
    SY0 = SY0 + Y
    C = C + 1
  ENDIF
  PAUSE 1
LOOP
IF C = 0 THEN
  SX0 = X
  SY0 = Y
  C = 1
ENDIF
MX = INT(SX0 / C + 0.5)
MY = INT(SY0 / C + 0.5)
RETURN

DrawTarget:
CLS
BOX 0, 0, 319, 239, 1, RGB(WHITE), RGB(BLACK)
FOR GX = 40 TO 280 STEP 40
  LINE GX, 0, GX, 239, 1, RGB(BLUE)
NEXT GX
FOR GY = 40 TO 200 STEP 40
  LINE 0, GY, 319, GY, 1, RGB(BLUE)
NEXT GY
TEXT 6, 6, "Tap target " + STR$(I) + " of " + STR$(N), "LT", 1, 1, RGB(YELLOW), RGB(BLACK)
LINE TX - 14, TY, TX + 14, TY, 1, RGB(RED)
LINE TX, TY - 14, TX, TY + 14, 1, RGB(RED)
CIRCLE TX, TY, 10, 1, 1, RGB(YELLOW)
CIRCLE TX, TY, 4, 1, 1, RGB(RED), RGB(RED)
RETURN

DATA 20,20, 160,20, 300,20, 20,60, 100,60
DATA 220,60, 300,60, 60,100, 160,100, 260,100
DATA 20,140, 100,140, 220,140, 300,140, 60,180
DATA 160,180, 260,180, 20,220, 160,220, 300,220
