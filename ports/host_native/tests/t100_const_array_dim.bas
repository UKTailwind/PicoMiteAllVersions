OPTION EXPLICIT

CONST N = 4
DIM a%(N)
DIM i%

FOR i% = 0 TO N
  a%(i%) = i%
NEXT i%

PRINT a%(0); ","; a%(N)
