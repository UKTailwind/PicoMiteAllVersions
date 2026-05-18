' Test GOTO and GOSUB/RETURN
DIM x% = 0
10 x% = x% + 1
IF x% < 5 THEN GOTO 10
PRINT x%
' GOSUB
DIM g% = 0
GOSUB 100
PRINT g%
GOSUB 100
PRINT g%
GOTO 200
100 g% = g% + 10
RETURN
200 PRINT "done"
