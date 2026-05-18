OPTION EXPLICIT
DIM a% = 7
DIM b% = 9
DIM c% = 5
DIM i%
FOR i% = 1 TO 5
  PRINT ((a% * b%) \ 2) + c%
  a% = a% + 1
  b% = b% - 1
NEXT i%
