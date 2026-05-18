OPTION EXPLICIT

DIM INTEGER t0%, dt%

t0% = TIMER
PAUSE 20
dt% = TIMER - t0%

IF dt% < 15 THEN
  ERROR "timer/pause host regression"
END IF

PRINT "timer ok"
END
