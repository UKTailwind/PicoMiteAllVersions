OPTION EXPLICIT

DIM x% = 1

FUNCTION bump%()
  x% = 100
  bump% = 10
END FUNCTION

x% = x% + bump%()
PRINT x%
