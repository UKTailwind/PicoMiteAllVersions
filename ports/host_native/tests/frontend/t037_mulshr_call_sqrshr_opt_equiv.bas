OPTION EXPLICIT
DIM a% = 7
DIM i%
FOR i% = 1 TO 5
  PRINT MULSHR(a%, a%, 3)
  a% = a% + 1
NEXT i%
