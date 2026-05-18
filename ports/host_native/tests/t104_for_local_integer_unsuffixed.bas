OPTION EXPLICIT

SUB DrawDots()
  LOCAL INTEGER i, y
  y = 12
  CLS RGB(BLACK)
  FOR i = 0 TO 3
    PIXEL i * 2 + 10, y, RGB(WHITE)
  NEXT i
END SUB

DrawDots
