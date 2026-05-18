' Test: EXIT FUNCTION and IF THEN SubCall patterns

FUNCTION Clamp(x%) AS INTEGER
  IF x% > 10 THEN Clamp = 10 : EXIT FUNCTION
  IF x% < 0 THEN Clamp = 0 : EXIT FUNCTION
  Clamp = x%
END FUNCTION

PRINT Clamp(5)
PRINT Clamp(20)
PRINT Clamp(-3)

' EXIT FUNCTION inside SELECT CASE
FUNCTION Lookup(k%) AS INTEGER
  SELECT CASE k%
    CASE 1
      Lookup = 100 : EXIT FUNCTION
    CASE 2
      Lookup = 200 : EXIT FUNCTION
  END SELECT
  Lookup = -1
END FUNCTION

PRINT Lookup(1)
PRINT Lookup(2)
PRINT Lookup(99)

' IF THEN bare SUB call
SUB ShowIt(v%)
  PRINT "show:" + STR$(v%)
END SUB

DIM INTEGER x%
x% = 5
IF x% > 0 THEN ShowIt x%
IF x% > 100 THEN ShowIt 999

' Nested IF THEN SUB inside a SUB
SUB Outer(n%)
  IF n% > 0 THEN ShowIt n%
END SUB
Outer 7
Outer 0

PRINT "done"
