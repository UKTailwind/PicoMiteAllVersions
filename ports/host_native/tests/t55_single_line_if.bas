' Test single-line IF/THEN/ELSE compilation
' These previously caused infinite loops in the compiler because
' compile_print didn't stop at tokenELSE.

' Basic THEN/ELSE
IF 1 THEN PRINT "yes" ELSE PRINT "no"
IF 0 THEN PRINT "no" ELSE PRINT "yes"

' With expressions
DIM a% = 5
IF a% > 3 THEN PRINT "big" ELSE PRINT "small"
IF a% < 3 THEN PRINT "small" ELSE PRINT "big"

' Logical operators in condition
DIM b% = 1, c% = 0
IF b% AND NOT c% THEN PRINT "pass1" ELSE PRINT "fail1"
IF b% OR c% THEN PRINT "pass2" ELSE PRINT "fail2"
IF NOT (b% AND c%) THEN PRINT "pass3" ELSE PRINT "fail3"

' THEN-only (no ELSE) still works
IF 1 THEN PRINT "only-then"
IF 0 THEN PRINT "should-not-print"

' Nested value checks
DIM x% = 2
IF x% = 1 THEN PRINT "one" ELSE PRINT "not one"
IF x% = 2 THEN PRINT "two" ELSE PRINT "not two"

' Multiple semicolons in PRINT before ELSE
IF 1 THEN PRINT "a"; "b" ELSE PRINT "c"
IF 0 THEN PRINT "c" ELSE PRINT "d"; "e"
