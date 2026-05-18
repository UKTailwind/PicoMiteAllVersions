OPTION EXPLICIT
DIM x! = 1.0
DIM y! = 0.5
DIM i%
FOR i% = 1 TO 3
  x! = x! + y!
  x! = x! - 0.25
NEXT i%
PRINT x!
