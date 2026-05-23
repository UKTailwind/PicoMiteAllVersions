' smokes/arrays.bas — arrays + DIM + bounds.

DIM a%(10)
DIM b!(5, 5)
DIM s$(3)

' Default init
IF a%(0) = 0 AND a%(10) = 0 THEN PRINT "OK_int_zero" ELSE PRINT "FAIL_int_zero"
IF b!(2, 3) = 0.0 THEN PRINT "OK_flt_zero" ELSE PRINT "FAIL_flt_zero"
IF s$(1) = "" THEN PRINT "OK_str_empty" ELSE PRINT "FAIL_str_empty"

' Write + read
DIM i%
FOR i% = 0 TO 10 : a%(i%) = i% * i% : NEXT i%
IF a%(0) = 0 AND a%(5) = 25 AND a%(10) = 100 THEN PRINT "OK_int_rw" ELSE PRINT "FAIL_int_rw"

' 2D array
b!(2, 3) = 7.5
b!(4, 1) = -2.5
IF ABS(b!(2, 3) - 7.5) < 0.001 AND ABS(b!(4, 1) - (-2.5)) < 0.001 THEN PRINT "OK_2d" ELSE PRINT "FAIL_2d"

' String array
s$(0) = "alpha"
s$(1) = "beta"
s$(2) = "gamma"
IF s$(0) = "alpha" AND s$(2) = "gamma" THEN PRINT "OK_str_rw" ELSE PRINT "FAIL_str_rw"

' Loop sum to check accumulated correctness.
DIM total% = 0
FOR i% = 0 TO 10 : total% = total% + a%(i%) : NEXT i%
' 0+1+4+9+16+25+36+49+64+81+100 = 385
IF total% = 385 THEN PRINT "OK_sum" ELSE PRINT "FAIL_sum " + STR$(total%)

' Dynamic-bounds DIM and re-DIM via ERASE.
DIM dyn%(7)
FOR i% = 0 TO 7 : dyn%(i%) = i% : NEXT i%
ERASE dyn%
DIM dyn%(3)
IF dyn%(0) = 0 AND dyn%(3) = 0 THEN PRINT "OK_erase_redim" ELSE PRINT "FAIL_erase_redim"

PRINT "SMOKE_DONE"
END
