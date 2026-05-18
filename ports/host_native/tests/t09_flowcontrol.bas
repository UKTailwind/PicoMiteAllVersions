' Test all flow control statements
' IF/ELSEIF/ELSE/ENDIF
DIM x% = 2
IF x% = 1 THEN
  PRINT "one"
ELSEIF x% = 2 THEN
  PRINT "two"
ELSEIF x% = 3 THEN
  PRINT "three"
ELSE
  PRINT "other"
ENDIF
' Single-line IF
IF x% = 2 THEN PRINT "yes" ELSE PRINT "no"
IF x% = 3 THEN PRINT "no" ELSE PRINT "yes"
' Nested IF
DIM a% = 1, b% = 2
IF a% = 1 THEN
  IF b% = 2 THEN
    PRINT "a=1,b=2"
  ELSE
    PRINT "wrong"
  ENDIF
ENDIF
' FOR/NEXT
DIM i%, s%
s% = 0
FOR i% = 1 TO 10
  s% = s% + i%
NEXT i%
PRINT s%
' FOR with STEP
FOR i% = 10 TO 0 STEP -2
  PRINT i%;
NEXT i%
PRINT
' Nested FOR
DIM j%, c%
c% = 0
FOR i% = 1 TO 3
  FOR j% = 1 TO 3
    c% = c% + 1
  NEXT j%
NEXT i%
PRINT c%
' DO/LOOP WHILE
i% = 0
DO WHILE i% < 5
  i% = i% + 1
LOOP
PRINT i%
' DO/LOOP UNTIL
i% = 0
DO
  i% = i% + 1
LOOP UNTIL i% = 10
PRINT i%
' DO WHILE/LOOP
DIM n% = 1
DO WHILE n% <= 16
  n% = n% * 2
LOOP
PRINT n%
' EXIT FOR
FOR i% = 1 TO 100
  IF i% = 7 THEN EXIT FOR
NEXT i%
PRINT i%
' EXIT DO
i% = 0
DO
  i% = i% + 1
  IF i% = 12 THEN EXIT DO
LOOP
PRINT i%
' SELECT CASE
FOR i% = 1 TO 4
  SELECT CASE i%
    CASE 1
      PRINT "one"
    CASE 2, 3
      PRINT "two or three"
    CASE ELSE
      PRINT "other"
  END SELECT
NEXT i%
