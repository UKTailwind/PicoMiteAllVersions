' smokes/core.bas — language fundamentals (vars, arithmetic, control flow).
' Emits OK_<name> / FAIL_<name> tags grepped by the harness.

' --- vartypes ---------------------------------------------------------
DIM i% = 42
DIM f! = 3.14
DIM s$ = "hello"
IF i% = 42 THEN PRINT "OK_int" ELSE PRINT "FAIL_int " + STR$(i%)
IF f! > 3.13 AND f! < 3.15 THEN PRINT "OK_float" ELSE PRINT "FAIL_float " + STR$(f!)
IF s$ = "hello" THEN PRINT "OK_string" ELSE PRINT "FAIL_string " + s$

' --- arithmetic -------------------------------------------------------
IF 7 + 11 = 18 THEN PRINT "OK_add" ELSE PRINT "FAIL_add"
IF 100 - 37 = 63 THEN PRINT "OK_sub" ELSE PRINT "FAIL_sub"
IF 6 * 7 = 42 THEN PRINT "OK_mul" ELSE PRINT "FAIL_mul"
IF 100 \ 7 = 14 THEN PRINT "OK_intdiv" ELSE PRINT "FAIL_intdiv"
IF 100 MOD 7 = 2 THEN PRINT "OK_mod" ELSE PRINT "FAIL_mod"
IF 2^8 = 256 THEN PRINT "OK_pow" ELSE PRINT "FAIL_pow"

' --- comparisons + logic ----------------------------------------------
IF 5 < 10 AND 10 > 5 THEN PRINT "OK_cmp" ELSE PRINT "FAIL_cmp"
IF NOT (5 > 10) THEN PRINT "OK_not" ELSE PRINT "FAIL_not"
IF (1 AND 1) AND NOT(1 AND 0) THEN PRINT "OK_andor" ELSE PRINT "FAIL_andor"

' --- control flow -----------------------------------------------------
DIM sum% = 0
FOR i% = 1 TO 10 : sum% = sum% + i% : NEXT i%
IF sum% = 55 THEN PRINT "OK_for" ELSE PRINT "FAIL_for " + STR$(sum%)

DIM k% = 0
DO : k% = k% + 1 : LOOP UNTIL k% = 5
IF k% = 5 THEN PRINT "OK_do_until" ELSE PRINT "FAIL_do_until"

k% = 0
DO WHILE k% < 7 : k% = k% + 1 : LOOP
IF k% = 7 THEN PRINT "OK_do_while" ELSE PRINT "FAIL_do_while"

' --- SELECT CASE ------------------------------------------------------
DIM tag$ = ""
SELECT CASE 3
  CASE 1 : tag$ = "one"
  CASE 2 : tag$ = "two"
  CASE 3 : tag$ = "three"
  CASE ELSE : tag$ = "other"
END SELECT
IF tag$ = "three" THEN PRINT "OK_select" ELSE PRINT "FAIL_select " + tag$

' --- SUB / FUNCTION ---------------------------------------------------
DIM total% = double_it%(21)
IF total% = 42 THEN PRINT "OK_function" ELSE PRINT "FAIL_function " + STR$(total%)

DIM marker$ = ""
set_marker
IF marker$ = "set" THEN PRINT "OK_sub" ELSE PRINT "FAIL_sub " + marker$

PRINT "SMOKE_DONE"
END

FUNCTION double_it%(x%)
  double_it% = x% * 2
END FUNCTION

SUB set_marker
  marker$ = "set"
END SUB
