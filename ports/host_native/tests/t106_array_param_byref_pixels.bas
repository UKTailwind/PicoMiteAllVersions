OPTION EXPLICIT

DIM a%(3)

SUB FillArray(a%())
  LOCAL INTEGER i
  FOR i = 0 TO 3
    a%(i) = RGB(WHITE)
  NEXT i
END SUB

CLS RGB(BLACK)
FillArray a%()
PIXEL 10, 12, a%(0)
PIXEL 12, 12, a%(1)
PIXEL 14, 12, a%(2)
PIXEL 16, 12, a%(3)
