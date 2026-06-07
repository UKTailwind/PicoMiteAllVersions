' Freenove ESP32-S3 GUI kitchen-sink demo.
' Configure GUI slots once at the prompt before running:
'   OPTION GUI CONTROLS 24
'
' Tap controls directly. Press any serial/keyboard key to exit.

GUI DELETE ALL
CLS
COLOUR RGB(WHITE), RGB(BLACK)

GUI FRAME #1, "Freenove GUI Kitchen Sink", 2, 2, 316, 236, RGB(WHITE)
GUI CAPTION #2, "Controls", 10, 18
GUI BUTTON #3, "Clear", 244, 14, 64, 24, RGB(BLACK), RGB(YELLOW)
GUI SWITCH #4, "Pen|Idle", 10, 36, 86, 26, RGB(WHITE), RGB(BLUE)
GUI CHECKBOX #5, "Snap", 112, 40, 18, RGB(YELLOW)
GUI RADIO #6, "Pulse", 184, 48, 10, RGB(CYAN)
GUI LED #7, "Touch", 270, 48, 10, RGB(GREEN)
GUI NUMBERBOX #8, 10, 72, 62, 22, RGB(WHITE), RGB(BLUE)
GUI SPINBOX #9, 84, 72, 76, 22, RGB(WHITE), RGB(BLUE), 1, 1, 8
GUI TEXTBOX #10, 172, 72, 120, 22, RGB(BLACK), RGB(CYAN)
GUI FORMATBOX #11, "2hHh(:)5m9m", 10, 106, 76, 22, RGB(WHITE), RGB(BLUE)
GUI DISPLAYBOX #12, 98, 106, 206, 22, RGB(BLACK), RGB(YELLOW)
GUI GAUGE #13, 64, 174, 38, RGB(WHITE), RGB(BLACK), 0, 100, 0, "V", RGB(GREEN), 25, RGB(YELLOW), 50, RGB(CYAN), 75, RGB(RED)
GUI BARGAUGE #14, 10, 218, 128, 14, RGB(WHITE), RGB(BLACK), 0, 100, RGB(GREEN), 25, RGB(YELLOW), 50, RGB(CYAN), 75, RGB(RED)
GUI AREA #15, 146, 134, 166, 88
GUI CAPTION #16, "Touch canvas", 188, 118
GUI BUTTON #17, "Msg", 196, 14, 40, 24, RGB(WHITE), RGB(BLUE)

CTRLVAL(4) = 1
CTRLVAL(6) = 1
CTRLVAL(8) = 42
CTRLVAL(9) = 2
CTRLVAL(10) = "hello"
CTRLVAL(11) = "09:30"
CTRLVAL(12) = "Ready"
CTRLVAL(13) = 35
CTRLVAL(14) = 65
GUI REDRAW ALL
GOSUB ClearPad

LastRef = -1
LastDown = -1
LastX = -1
LastY = -1
OldS$ = ""
V = 35
NextAnim = TIMER
NextUi = TIMER

DO
  K$ = INKEY$
  IF K$ <> "" THEN EXIT

  Ref = TOUCH(REF)
  X = TOUCH(X)
  Y = TOUCH(Y)

  IF Ref = 3 AND LastRef <> 3 THEN GOSUB ClearPad
  IF Ref = 17 AND LastRef <> 17 THEN
    M = MSGBOX("Kitchen sink~touch dialog", "OK", "Cancel")
    GUI REDRAW ALL
    GOSUB ClearPad
    CTRLVAL(12) = "MSGBOX=" + STR$(M)
    OldS$ = CTRLVAL(12)
  ENDIF
  LastRef = Ref

  Down = 0
  IF X >= 146 AND X <= 312 AND Y >= 134 AND Y <= 222 THEN Down = 1
  IF Down <> LastDown THEN
    CTRLVAL(7) = Down
    LastDown = Down
  ENDIF

  IF Down = 1 AND CTRLVAL(4) = 1 THEN
    DX = X
    DY = Y
    IF CTRLVAL(5) = 1 THEN
      DX = INT(X / 4) * 4
      DY = INT(Y / 4) * 4
    ENDIF
    IF LastX >= 0 THEN
      LINE LastX, LastY, DX, DY, CTRLVAL(9), RGB(CYAN)
    ELSE
      PIXEL DX, DY, RGB(YELLOW)
    ENDIF
    LastX = DX
    LastY = DY
  ELSE
    LastX = -1
    LastY = -1
  ENDIF

  IF TIMER - NextAnim >= 150 THEN
    NextAnim = TIMER
    IF CTRLVAL(6) = 1 THEN
      V = V + 5
      IF V > 100 THEN V = 0
      CTRLVAL(13) = V
      CTRLVAL(14) = 100 - V
    ENDIF
  ENDIF

  IF TIMER - NextUi >= 120 THEN
    NextUi = TIMER
    S$ = "R" + STR$(Ref) + " X" + STR$(X) + " Y" + STR$(Y)
    IF S$ <> OldS$ THEN
      CTRLVAL(12) = S$
      OldS$ = S$
    ENDIF
  ENDIF

  PAUSE 1
LOOP

GUI DELETE ALL
CLS
PRINT "FREENOVE_GUI_KITCHEN_SINK_DONE"
END

ClearPad:
BOX 146, 134, 166, 88, 1, RGB(WHITE), RGB(BLACK)
LINE 146, 134, 312, 222, 1, RGB(BLUE)
LINE 146, 222, 312, 134, 1, RGB(BLUE)
LastX = -1
LastY = -1
RETURN
