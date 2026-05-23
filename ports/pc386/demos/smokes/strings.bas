' smokes/strings.bas — string operations.

DIM s$ = "Hello, World!"

IF LEN(s$) = 13 THEN PRINT "OK_len" ELSE PRINT "FAIL_len"
IF LEFT$(s$, 5) = "Hello" THEN PRINT "OK_left" ELSE PRINT "FAIL_left " + LEFT$(s$, 5)
IF RIGHT$(s$, 6) = "World!" THEN PRINT "OK_right" ELSE PRINT "FAIL_right " + RIGHT$(s$, 6)
IF MID$(s$, 8, 5) = "World" THEN PRINT "OK_mid" ELSE PRINT "FAIL_mid " + MID$(s$, 8, 5)
IF INSTR(s$, "World") = 8 THEN PRINT "OK_instr" ELSE PRINT "FAIL_instr " + STR$(INSTR(s$, "World"))
IF INSTR(s$, "xyz") = 0 THEN PRINT "OK_instr_miss" ELSE PRINT "FAIL_instr_miss"
IF UCASE$("Hello") = "HELLO" THEN PRINT "OK_ucase" ELSE PRINT "FAIL_ucase"
IF LCASE$("Hello") = "hello" THEN PRINT "OK_lcase" ELSE PRINT "FAIL_lcase"
IF CHR$(65) = "A" THEN PRINT "OK_chr" ELSE PRINT "FAIL_chr"
IF ASC("A") = 65 THEN PRINT "OK_asc" ELSE PRINT "FAIL_asc"
' MMBasic STR$() returns the value without a leading space (unlike some BASIC dialects).
IF STR$(42) = "42" THEN PRINT "OK_str" ELSE PRINT "FAIL_str [" + STR$(42) + "]"
IF VAL("42") = 42 THEN PRINT "OK_val_int" ELSE PRINT "FAIL_val_int"
IF VAL("3.14") > 3.13 AND VAL("3.14") < 3.15 THEN PRINT "OK_val_flt" ELSE PRINT "FAIL_val_flt"
IF SPACE$(3) = "   " THEN PRINT "OK_space" ELSE PRINT "FAIL_space"
IF STRING$(5, "X") = "XXXXX" THEN PRINT "OK_string" ELSE PRINT "FAIL_string"

' Concatenation
DIM a$ = "foo", b$ = "bar"
IF a$ + b$ = "foobar" THEN PRINT "OK_concat" ELSE PRINT "FAIL_concat"

' Substring replacement via MID$ assignment (if supported).
DIM dst$ = "abcdefg"
MID$(dst$, 3, 2) = "XY"
IF dst$ = "abXYefg" THEN PRINT "OK_mid_assign" ELSE PRINT "FAIL_mid_assign " + dst$

PRINT "SMOKE_DONE"
END
