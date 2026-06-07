' Freenove ESP32-S3 touch alignment diagnostic.
' Press any serial/keyboard key to exit.

GUI DELETE ALL
CLS
COLOUR RGB(WHITE), RGB(BLACK)

BOX 0, 0, 319, 239, 1, RGB(WHITE), RGB(BLACK)
LINE 0, 40, 319, 40, 1, RGB(BLUE)
LINE 0, 80, 319, 80, 1, RGB(BLUE)
LINE 0, 120, 319, 120, 1, RGB(BLUE)
LINE 0, 160, 319, 160, 1, RGB(BLUE)
LINE 0, 200, 319, 200, 1, RGB(BLUE)
LINE 40, 0, 40, 239, 1, RGB(BLUE)
LINE 80, 0, 80, 239, 1, RGB(BLUE)
LINE 120, 0, 120, 239, 1, RGB(BLUE)
LINE 160, 0, 160, 239, 1, RGB(BLUE)
LINE 200, 0, 200, 239, 1, RGB(BLUE)
LINE 240, 0, 240, 239, 1, RGB(BLUE)
LINE 280, 0, 280, 239, 1, RGB(BLUE)
TEXT 6, 6, "Touch alignment: crosshair should follow finger", "LT", 1, 1, RGB(YELLOW), RGB(BLACK)
TEXT 6, 222, "Key exits", "LT", 1, 1, RGB(YELLOW), RGB(BLACK)

LastX = -1
LastY = -1
DO
  K$ = INKEY$
  IF K$ <> "" THEN EXIT

  X = TOUCH(X)
  Y = TOUCH(Y)
  IF X >= 0 AND Y >= 0 THEN
    IF LastX >= 0 THEN GOSUB EraseMark
    LastX = X
    LastY = Y
    GOSUB DrawMark
  ENDIF
  PAUSE 1
LOOP

CLS
PRINT "FREENOVE_TOUCH_ALIGNMENT_DONE"
END

DrawMark:
LINE LastX - 8, LastY, LastX + 8, LastY, 1, RGB(RED)
LINE LastX, LastY - 8, LastX, LastY + 8, 1, RGB(RED)
CIRCLE LastX, LastY, 5, 1, 1, RGB(YELLOW)
RETURN

EraseMark:
LINE LastX - 8, LastY, LastX + 8, LastY, 1, RGB(BLACK)
LINE LastX, LastY - 8, LastX, LastY + 8, 1, RGB(BLACK)
CIRCLE LastX, LastY, 5, 1, 1, RGB(BLACK)
RETURN
