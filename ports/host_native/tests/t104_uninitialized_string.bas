DIM a$
PRINT LEN(a$)
PRINT "[" + a$ + "]"
DIM b$
IF a$ = b$ THEN PRINT "equal" ELSE PRINT "not equal"
IF a$ = "" THEN PRINT "empty" ELSE PRINT "not empty"
PRINT LEFT$(a$, 3)
PRINT MID$(a$, 1, 2)
PRINT RIGHT$(a$, 1)
