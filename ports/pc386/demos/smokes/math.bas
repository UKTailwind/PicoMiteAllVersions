' smokes/math.bas — math functions.

DIM e! = 0.0001 ' float tolerance

IF ABS(SIN(0) - 0) < e! THEN PRINT "OK_sin0" ELSE PRINT "FAIL_sin0"
IF ABS(SIN(3.14159265 / 2) - 1) < 0.001 THEN PRINT "OK_sin_pi2" ELSE PRINT "FAIL_sin_pi2 " + STR$(SIN(3.14159265 / 2))
IF ABS(COS(0) - 1) < e! THEN PRINT "OK_cos0" ELSE PRINT "FAIL_cos0"
IF ABS(SQR(16) - 4) < e! THEN PRINT "OK_sqr" ELSE PRINT "FAIL_sqr"
IF ABS(SQR(2) - 1.41421) < 0.001 THEN PRINT "OK_sqr2" ELSE PRINT "FAIL_sqr2"
IF ABS(EXP(0) - 1) < e! THEN PRINT "OK_exp0" ELSE PRINT "FAIL_exp0"
IF ABS(LOG(1) - 0) < e! THEN PRINT "OK_log1" ELSE PRINT "FAIL_log1"
' MMBasic uses ATN (single-argument arctangent), not ATAN.
DIM atn_val! = ATN(1)
IF ABS(atn_val! - 3.14159265/4) < e! THEN PRINT "OK_atn" ELSE PRINT "FAIL_atn val=" + STR$(atn_val!) + " exp=" + STR$(3.14159265/4)
IF ABS(-5) = 5 AND ABS(7) = 7 THEN PRINT "OK_abs" ELSE PRINT "FAIL_abs"
IF INT(3.7) = 3 AND INT(-2.3) = -3 THEN PRINT "OK_int" ELSE PRINT "FAIL_int"
IF FIX(3.7) = 3 AND FIX(-2.3) = -2 THEN PRINT "OK_fix" ELSE PRINT "FAIL_fix"
IF MIN(7, 3) = 3 AND MAX(7, 3) = 7 THEN PRINT "OK_minmax" ELSE PRINT "FAIL_minmax"
IF SGN(-5) = -1 AND SGN(0) = 0 AND SGN(5) = 1 THEN PRINT "OK_sgn" ELSE PRINT "FAIL_sgn"

' (CPUID tests moved to extras.bas where they handle pre-Pentium gracefully.)

' RND determinism via seed.
RANDOMIZE 1234
DIM r1! = RND
RANDOMIZE 1234
DIM r2! = RND
IF r1! = r2! THEN PRINT "OK_rnd_seed" ELSE PRINT "FAIL_rnd_seed r1=" + STR$(r1!) + " r2=" + STR$(r2!)

PRINT "SMOKE_DONE"
END
