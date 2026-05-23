' smokes/time.bas — TIMER / PAUSE / DATE$ / TIME$.

DIM t0!, t1!, dt!

t0! = TIMER
PAUSE 100
t1! = TIMER
dt! = t1! - t0!

' PAUSE 100 should give ~100ms ± slop.
IF dt! > 50 AND dt! < 250 THEN PRINT "OK_pause_100" ELSE PRINT "FAIL_pause_100 dt=" + STR$(dt!)

' TIMER monotonic.
DIM ok% = 1
DIM i%, last!, cur!
last! = TIMER
FOR i% = 1 TO 20
  cur! = TIMER
  IF cur! < last! THEN ok% = 0
  last! = cur!
NEXT i%
IF ok% THEN PRINT "OK_timer_mono" ELSE PRINT "FAIL_timer_mono"

' DATE$ format DD-MM-YYYY (10 chars including dashes).
DIM d$ = DATE$
IF LEN(d$) = 10 AND MID$(d$, 3, 1) = "-" AND MID$(d$, 6, 1) = "-" THEN PRINT "OK_date_fmt" ELSE PRINT "FAIL_date_fmt " + d$

' TIME$ format HH:MM:SS (8 chars).
DIM tm$ = TIME$
IF LEN(tm$) = 8 AND MID$(tm$, 3, 1) = ":" AND MID$(tm$, 6, 1) = ":" THEN PRINT "OK_time_fmt" ELSE PRINT "FAIL_time_fmt " + tm$

PRINT "SMOKE_DONE"
END
