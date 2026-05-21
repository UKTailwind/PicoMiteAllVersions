' smokes/settick.bas — periodic SETTICK callback firing via the
' check_interrupt poll path.
'
' Interrupt handlers in MMBasic are labels with IRETURN, not SUBs.
' END SUB doesn't restore the interrupted nextstmt; only IRETURN does.

DIM tick_count% = 0

' Arm a 50ms tick.
SETTICK 50, TickHandler

' Spin for ~500ms — expect ~10 ticks. Allow slop [6, 14].
DIM t0! = TIMER
DO
LOOP UNTIL TIMER - t0! > 500

SETTICK 0, TickHandler

IF tick_count% >= 6 AND tick_count% <= 14 THEN PRINT "OK_settick_rate count=" + STR$(tick_count%) ELSE PRINT "FAIL_settick_rate count=" + STR$(tick_count%)

' PAUSE / RESUME — count should freeze while paused.
tick_count% = 0
SETTICK 50, TickHandler
t0! = TIMER
DO
LOOP UNTIL TIMER - t0! > 250
SETTICK PAUSE, 0
DIM frozen% = tick_count%
DIM t1! = TIMER
DO
LOOP UNTIL TIMER - t1! > 250
DIM after_pause% = tick_count%
SETTICK 0, TickHandler

IF after_pause% = frozen% THEN PRINT "OK_settick_pause" ELSE PRINT "FAIL_settick_pause frozen=" + STR$(frozen%) + " after=" + STR$(after_pause%)

PRINT "SMOKE_DONE"
END

TickHandler:
  tick_count% = tick_count% + 1
  IRETURN
