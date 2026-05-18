SUB CountDown(n%)
  LOCAL i%
  IF n% <= 0 THEN EXIT SUB
  FOR i% = 1 TO n%
    PRINT n%; " "; i%
  NEXT i%
  CountDown(n% - 1)
  PRINT "back "; n%
  FOR i% = 1 TO 2
    PRINT "verify "; n%; " "; i%
  NEXT i%
END SUB

CountDown(3)
