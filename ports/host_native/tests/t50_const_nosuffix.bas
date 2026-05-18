CONST SPEED = 1.2
CONST LIMIT = 10
CONST LABEL = "hello"
DIM x!, n%, s$
x! = SPEED
n% = LIMIT
s$ = LABEL
PRINT x!
PRINT n%
PRINT s$
x! = SPEED + 0.3
PRINT x!
IF n% < LIMIT + 5 THEN PRINT "less"
PRINT "done"
