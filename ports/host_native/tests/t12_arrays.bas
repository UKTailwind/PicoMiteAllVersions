' Test arrays: integer, float, string, and 2D
' Integer array
DIM a%(10)
DIM i%
FOR i% = 0 TO 10
  a%(i%) = i% * i%
NEXT i%
FOR i% = 0 TO 10
  PRINT a%(i%);
NEXT i%
PRINT
' Float array
DIM b!(5)
FOR i% = 0 TO 5
  b!(i%) = i% * 1.1
NEXT i%
FOR i% = 0 TO 5
  PRINT b!(i%);
NEXT i%
PRINT
' String array
DIM c$(3)
c$(0) = "hello"
c$(1) = "world"
c$(2) = "foo"
c$(3) = "bar"
FOR i% = 0 TO 3
  PRINT c$(i%)
NEXT i%
' 2D array
DIM m%(3,3)
DIM j%
FOR i% = 0 TO 3
  FOR j% = 0 TO 3
    m%(i%, j%) = i% * 4 + j%
  NEXT j%
NEXT i%
FOR i% = 0 TO 3
  FOR j% = 0 TO 3
    PRINT m%(i%, j%);
  NEXT j%
  PRINT
NEXT i%
