OPTION EXPLICIT
DIM INTEGER x%, y%
x% = 10
y% = 20
PRINT x% + y%

FUNCTION Add(a%, b%) AS INTEGER
  Add = a% + b%
END FUNCTION
PRINT Add(3, 4)

' EXIT FUNCTION with OPTION EXPLICIT
FUNCTION Clamp(v%, lo%, hi%) AS INTEGER
  IF v% < lo% THEN Clamp = lo% : EXIT FUNCTION
  IF v% > hi% THEN Clamp = hi% : EXIT FUNCTION
  Clamp = v%
END FUNCTION
PRINT Clamp(5, 0, 10)
PRINT Clamp(-1, 0, 10)
PRINT Clamp(99, 0, 10)

' IF THEN bare SUB call with OPTION EXPLICIT
SUB Greet(n%)
  PRINT "hi" + STR$(n%)
END SUB
IF x% > 0 THEN Greet x%

PRINT "done"
