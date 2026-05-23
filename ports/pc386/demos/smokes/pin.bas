' smokes/pin.bas — LPT1 parallel-port GPIO round-trip.
'
' Pins are exposed via SETPIN / PIN / fun_pin (numbered 1..NBRPINS).
' Setting a pin as DOUT and reading it back should reflect the last
' value written (the parallel data register feeds back to the latch).

' GP1 as output, drive high, read back high.
SETPIN GP1, DOUT
PIN(GP1) = 1
DIM v% = PIN(GP1)
IF v% = 1 THEN PRINT "OK_pin_high" ELSE PRINT "FAIL_pin_high val=" + STR$(v%)

' Drive low, read back low.
PIN(GP1) = 0
v% = PIN(GP1)
IF v% = 0 THEN PRINT "OK_pin_low" ELSE PRINT "FAIL_pin_low val=" + STR$(v%)

' Toggle a second pin without disturbing the first.
SETPIN GP2, DOUT
PIN(GP1) = 1
PIN(GP2) = 1
IF PIN(GP1) = 1 AND PIN(GP2) = 1 THEN PRINT "OK_pin_indep_high" ELSE PRINT "FAIL_pin_indep_high"
PIN(GP1) = 0
IF PIN(GP1) = 0 AND PIN(GP2) = 1 THEN PRINT "OK_pin_indep_low" ELSE PRINT "FAIL_pin_indep_low"

' Disable both before exit.
SETPIN GP1, OFF
SETPIN GP2, OFF

PRINT "SMOKE_DONE"
END
