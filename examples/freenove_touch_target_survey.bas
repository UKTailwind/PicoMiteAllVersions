' Freenove ESP32-S3 touch target survey.
' Tap each target once. Press any serial/keyboard key to abort.

GUI DELETE ALL
CLS
COLOUR RGB(WHITE), RGB(BLACK)

N = 20
SumDx = 0
SumDy = 0
SumAbsX = 0
SumAbsY = 0
SumD2 = 0
WorstD2 = -1
WorstI = 0
WorstX = 0
WorstY = 0
WorstMX = 0
WorstMY = 0

PRINT "TS_START"

FOR I = 1 TO N
  READ TX, TY
  GOSUB WaitRelease
  GOSUB DrawTarget
  GOSUB WaitTouch
  DX = MX - TX
  DY = MY - TY
  D2 = DX * DX + DY * DY
  SumDx = SumDx + DX
  SumDy = SumDy + DY
  SumAbsX = SumAbsX + ABS(DX)
  SumAbsY = SumAbsY + ABS(DY)
  SumD2 = SumD2 + D2
  IF D2 > WorstD2 THEN
    WorstD2 = D2
    WorstI = I
    WorstX = TX
    WorstY = TY
    WorstMX = MX
    WorstMY = MY
  ENDIF
  PRINT "P,";I;",";TX;",";TY;",";MX;",";MY
NEXT I

CLS
TEXT 8, 18, "Survey complete", "LT", 1, 1, RGB(YELLOW), RGB(BLACK)
TEXT 8, 46, "Mean dx " + STR$(SumDx / N), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 66, "Mean dy " + STR$(SumDy / N), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 86, "Mean abs x " + STR$(SumAbsX / N), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 106, "Mean abs y " + STR$(SumAbsY / N), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 126, "RMS " + STR$(SQR(SumD2 / N)), "LT", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 8, 146, "Worst #" + STR$(WorstI), "LT", 1, 1, RGB(WHITE), RGB(BLACK)

PRINT "MDX,";SumDx / N
PRINT "MDY,";SumDy / N
PRINT "MAX,";SumAbsX / N
PRINT "MAY,";SumAbsY / N
PRINT "RMS,";SQR(SumD2 / N)
PRINT "W,";WorstI;",";WorstX;",";WorstY;",";WorstMX;",";WorstMY
PRINT "TS_DONE"
END

WaitRelease:
DO
  IF INKEY$ <> "" THEN END
  X = TOUCH(X)
  Y = TOUCH(Y)
  IF X < 0 OR Y < 0 THEN EXIT
  PAUSE 5
LOOP
PAUSE 80
RETURN

WaitTouch:
SX = 0
SY = 0
C = 0
DO
  IF INKEY$ <> "" THEN END
  X = TOUCH(X)
  Y = TOUCH(Y)
  IF X >= 0 AND Y >= 0 THEN
    EXIT
  ENDIF
  PAUSE 1
LOOP
T0 = TIMER
DO WHILE TIMER - T0 < 60
  X = TOUCH(X)
  Y = TOUCH(Y)
  IF X >= 0 AND Y >= 0 THEN
    SX = SX + X
    SY = SY + Y
    C = C + 1
  ENDIF
  PAUSE 1
LOOP
IF C = 0 THEN
  SX = X
  SY = Y
  C = 1
ENDIF
MX = INT(SX / C + 0.5)
MY = INT(SY / C + 0.5)
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
