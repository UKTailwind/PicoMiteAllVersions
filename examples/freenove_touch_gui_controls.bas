' Freenove ESP32-S3 GUI touch example.
' Configure GUI slots once at the prompt before running:
'   OPTION GUI CONTROLS 12

GUI DELETE ALL
CLS
COLOUR RGB(WHITE), RGB(BLACK)

GUI FRAME #1, "Freenove Touch", 4, 4, 312, 232, RGB(WHITE)
GUI CAPTION #2, "Draw in the box; key exits", 14, 20
GUI BUTTON #3, "Clear", 20, 44, 76, 28, RGB(BLACK), RGB(YELLOW)
GUI SWITCH #4, "Pen|Idle", 112, 44, 94, 28, RGB(WHITE), RGB(BLUE)
GUI LED #5, "Down", 236, 58, 10, RGB(GREEN)
GUI AREA #6, 10, 82, 300, 150
CTRLVAL(4) = 1
GUI REDRAW ALL
GOSUB ClearPad

PRINT "FREENOVE_GUI_TOUCH_READY"
PRINT "Touch the drawing area. Tap Clear or press any key to exit."

LastRef = -1
LastX = -1
LastY = -1
LastDown = -1

DO
  K$ = INKEY$
  IF K$ <> "" THEN EXIT

  Ref = TOUCH(REF)
  IF Ref <> LastRef THEN
    LastRef = Ref
    IF Ref = 3 THEN GOSUB ClearPad
  ENDIF

  X = TOUCH(X)
  Y = TOUCH(Y)
  Down = 0
  IF X >= 10 AND X <= 310 AND Y >= 82 AND Y <= 232 THEN Down = 1
  IF Down <> LastDown THEN
    CTRLVAL(5) = Down
    LastDown = Down
  ENDIF
  IF Down THEN
    IF CTRLVAL(4) = 1 THEN
      IF LastX >= 0 THEN
        LINE LastX, LastY, X, Y, 2, RGB(CYAN)
      ELSE
        PIXEL X, Y, RGB(YELLOW)
      ENDIF
      LastX = X
      LastY = Y
    ENDIF
  ELSE
    LastX = -1
    LastY = -1
  ENDIF
  PAUSE 15
LOOP

GUI DELETE ALL
CLS
PRINT "FREENOVE_GUI_TOUCH_DONE"
END

ClearPad:
BOX 10, 82, 300, 150, 1, RGB(WHITE), RGB(BLACK)
LastX = -1
LastY = -1
RETURN
