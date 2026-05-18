OPTION EXPLICIT

DIM a%(3)
DIM i%

SUB FillArray(a%())
  LOCAL INTEGER i
  FOR i = 0 TO 3
    a%(i) = i + 10
  NEXT i
END SUB

FillArray a%()

FOR i% = 0 TO 3
  PRINT a%(i%)
NEXT i%
