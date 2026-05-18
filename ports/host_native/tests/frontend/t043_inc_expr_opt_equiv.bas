OPTION EXPLICIT
DIM x% = 10
DIM y% = 3
DIM i%
FOR i% = 1 TO 3
  x% = x% + y%
  x% = x% - i%
NEXT i%
PRINT x%
