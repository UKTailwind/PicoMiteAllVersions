' Test edge cases and boundary conditions
' Empty string operations
DIM s$
s$ = ""
PRINT "Empty len:"; LEN(s$)
PRINT "Empty + ABC: "; s$ + "ABC"
s$ = "Hello"
PRINT "Left 0: ["; LEFT$(s$, 0); "]"
PRINT "Right 0: ["; RIGHT$(s$, 0); "]"
PRINT "Left 100: ["; LEFT$(s$, 100); "]"
PRINT "Right 100: ["; RIGHT$(s$, 100); "]"
' Integer boundary operations
DIM big%
big% = 1000000
PRINT big% * big%
PRINT big% * 1000
' Zero and one edge cases
PRINT 0 * 99999
PRINT 1 * 99999
PRINT 0 + 0
PRINT 0 - 0
PRINT 1 ^ 0
PRINT 0 ^ 1
' Boolean edge cases
PRINT NOT 0
PRINT NOT 1
PRINT NOT 99
PRINT 0 AND 1
PRINT 1 AND 1
PRINT 0 OR 0
PRINT 0 OR 1
' FOR loop edge cases
DIM count%, i%
count% = 0
FOR i% = 1 TO 1
  count% = count% + 1
NEXT i%
PRINT "FOR 1 TO 1:"; count%
count% = 0
FOR i% = 5 TO 1
  count% = count% + 1
NEXT i%
PRINT "FOR 5 TO 1 (no step):"; count%
count% = 0
FOR i% = 5 TO 1 STEP -1
  count% = count% + 1
NEXT i%
PRINT "FOR 5 TO 1 STEP -1:"; count%
' Single iteration loops
DIM x%
x% = 0
DO WHILE x% < 1
  x% = x% + 1
LOOP
PRINT "DO WHILE 1 iter:"; x%
' GOSUB/RETURN
DIM g%
g% = 0
GOSUB 100
PRINT "After gosub: g="; g%
GOTO 200
100 g% = 42
RETURN
200 PRINT "Done"
